/*
 * Copyright 2019 Elie Michel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HAPI/HAPI.h"

#include "util/ofx_util.h"
#include "util/memory_util.h"

#include "ofxCore.h"
#include "ofxMeshEffect.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define kMainInput "MainInput"
#define kMainOutput "MainOutput"

// Houdini

typedef struct HoudiniRuntime {
  HAPI_Session hsession;
  HAPI_AssetLibraryId library;
  HAPI_NodeId node_id;
  HAPI_StringHandle *asset_names_array;
  char current_library_path[1024];
  int current_asset_index;
  int asset_count;
  
  int parm_count;
  HAPI_ParmInfo *parm_infos_array;
  int sop_count;
  HAPI_NodeId *sop_array;
} HoudiniRuntime;

// Global session
static HAPI_Session global_hsession;
static int global_hsession_users = 0;

static bool hruntime_init(HoudiniRuntime *hr) {
  HAPI_Result res;
  
  if (0 == global_hsession_users) {
    HAPI_CookOptions cookOptions;
    cookOptions.maxVerticesPerPrimitive = 3; // TODO: switch back to -1 when it works with 3

    res = HAPI_CreateInProcessSession(&global_hsession);
    if (HAPI_RESULT_SUCCESS != res) {
      printf("Houdini error in HAPI_CreateInProcessSession: %u\n", res);
      return false;
    }

    res = HAPI_Initialize(&global_hsession, &cookOptions, false /* threaded cooking */, -1, NULL, NULL, NULL, NULL, NULL);
    if (HAPI_RESULT_SUCCESS != res) {
      printf("Houdini error in HAPI_Initialize: %u\n", res);
      return false;
    }
  }

  global_hsession_users++;

  hr->hsession = global_hsession;
  hr->current_library_path[0] = '\0';
  hr->current_asset_index = -1;
  hr->asset_names_array = NULL;
  hr->asset_count = 0;
  hr->parm_infos_array = NULL;
  hr->parm_count = 0;

  return true;
}

static void hruntime_free(HoudiniRuntime *hr) {
  if (NULL != hr->asset_names_array) {
    free_array(hr->asset_names_array);
  }
  if (NULL != hr->parm_infos_array) {
    free_array(hr->parm_infos_array);
  }

  global_hsession_users--;
  if (0 == global_hsession_users) {
    HAPI_Result res;
    res = HAPI_Cleanup(&hr->hsession);
    if (HAPI_RESULT_SUCCESS != res) {
      printf("Houdini error in HAPI_Cleanup: %u\n", res);
    }
  }
  free_array(hr);
}

// OFX

typedef struct PluginRuntime {
    OfxHost *host;
    OfxPropertySuiteV1 *propertySuite;
    OfxParameterSuiteV1 *parameterSuite;
    OfxMeshEffectSuiteV1 *meshEffectSuite;
} PluginRuntime;

PluginRuntime plugin0_runtime;
PluginRuntime plugin1_runtime;

static OfxStatus plugin0_load(PluginRuntime *runtime) {
    OfxHost *h = runtime->host;
    runtime->propertySuite = (OfxPropertySuiteV1*)h->fetchSuite(h->host, kOfxPropertySuite, 1);
    runtime->parameterSuite = (OfxParameterSuiteV1*)h->fetchSuite(h->host, kOfxParameterSuite, 1);
    runtime->meshEffectSuite = (OfxMeshEffectSuiteV1*)h->fetchSuite(h->host, kOfxMeshEffectSuite, 1);
    return kOfxStatOK;
}

static OfxStatus plugin0_describe(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
    if (NULL == runtime->propertySuite || NULL == runtime->meshEffectSuite) {
        return kOfxStatErrMissingHostFeature;
    }

    OfxStatus status;
    OfxPropertySetHandle propHandle;

    status = runtime->meshEffectSuite->getPropertySet(meshEffect, &propHandle);
    printf("Suite method 'getPropertySet' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->propertySuite->propSetString(propHandle, kOfxMeshEffectPropContext, 0, kOfxMeshEffectContextFilter);
    printf("Suite method 'propSetString' returned status %d (%s)\n", status, getOfxStateName(status));

    // Shall move into "describe in context" when it will exist
    OfxPropertySetHandle inputProperties;
    status = runtime->meshEffectSuite->inputDefine(meshEffect, kMainInput, &inputProperties);
    printf("Suite method 'inputDefine' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->propertySuite->propSetString(inputProperties, kOfxPropLabel, 0, "Main Input");
    printf("Suite method 'propSetString' returned status %d (%s)\n", status, getOfxStateName(status));

    OfxPropertySetHandle outputProperties;
    status = runtime->meshEffectSuite->inputDefine(meshEffect, kMainOutput, &outputProperties); // yes, output are also "inputs", I should change this name in the API
    printf("Suite method 'inputDefine' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->propertySuite->propSetString(outputProperties, kOfxPropLabel, 0, "Main Output");
    printf("Suite method 'propSetString' returned status %d (%s)\n", status, getOfxStateName(status));

    // Declare parameters
    OfxParamSetHandle parameters;
    OfxParamHandle param;
    status = runtime->meshEffectSuite->getParamSet(meshEffect, &parameters);
    printf("Suite method 'getParamSet' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->parameterSuite->paramDefine(parameters, kOfxParamTypeDouble, "width", NULL);
    printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->parameterSuite->paramDefine(parameters, kOfxParamTypeInteger, "steps", NULL);
    printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->parameterSuite->paramDefine(parameters, kOfxParamTypeString, "path", NULL);
    printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));

    return kOfxStatOK;
}

static OfxStatus plugin0_cook(PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
    OfxStatus status;
    OfxMeshInputHandle input, output;
    OfxPropertySetHandle propertySet;

    status = runtime->meshEffectSuite->inputGetHandle(meshEffect, kMainInput, &input, &propertySet);
    printf("Suite method 'inputGetHandle' returned status %d (%s)\n", status, getOfxStateName(status));
    if (status != kOfxStatOK) {
        return kOfxStatErrUnknown;
    }

    status = runtime->meshEffectSuite->inputGetHandle(meshEffect, kMainOutput, &output, &propertySet);
    printf("Suite method 'inputGetHandle' returned status %d (%s)\n", status, getOfxStateName(status));
    if (status != kOfxStatOK) {
        return kOfxStatErrUnknown;
    }

    OfxTime time = 0;
    OfxPropertySetHandle input_mesh;
    status = runtime->meshEffectSuite->inputGetMesh(input, time, &input_mesh);
    printf("Suite method 'inputGetMesh' returned status %d (%s)\n", status, getOfxStateName(status));

    int input_point_count;
    status = runtime->propertySuite->propGetInt(input_mesh, kOfxMeshPropPointCount, 0, &input_point_count);
    printf("Suite method 'propGetInt' returned status %d (%s)\n", status, getOfxStateName(status));

    float *input_points;
    status = runtime->propertySuite->propGetPointer(input_mesh, kOfxMeshPropPointData, 0, (void**)&input_points);
    printf("Suite method 'propGetPointer' returned status %d (%s)\n", status, getOfxStateName(status));

    printf("DEBUG: Found %d in input mesh\n", input_point_count);

    // TODO: store input data

    status = runtime->meshEffectSuite->inputReleaseMesh(input_mesh);
    printf("Suite method 'inputReleaseMesh' returned status %d (%s)\n", status, getOfxStateName(status));

    // Get parameters
    OfxParamSetHandle parameters;
    OfxParamHandle param;
    status = runtime->meshEffectSuite->getParamSet(meshEffect, &parameters);
    printf("Suite method 'getParamSet' returned status %d (%s)\n", status, getOfxStateName(status));

    status = runtime->parameterSuite->paramGetHandle(parameters, "width", &param, NULL);
    printf("Suite method 'paramGetHandle' returned status %d (%s)\n", status, getOfxStateName(status));

    double width;
    status = runtime->parameterSuite->paramGetValue(param, &width);
    printf("Suite method 'paramGetValue' returned status %d (%s)\n", status, getOfxStateName(status));

    printf("-- width parameter set to: %f\n", width);

    // TODO: core cook

    OfxPropertySetHandle output_mesh;
    status = runtime->meshEffectSuite->inputGetMesh(output, time, &output_mesh);
    printf("Suite method 'inputGetMesh' returned status %d (%s)\n", status, getOfxStateName(status));

    int output_point_count = 0, output_vertex_count = 0, output_face_count = 0;

    // TODO: Consolidate geo counts
    output_point_count = 4;
    output_vertex_count = 4;
    output_face_count = 1;

    printf("DEBUG: Allocating output mesh data: %d points, %d vertices, %d faces\n", output_point_count, output_vertex_count, output_face_count);

    status = runtime->meshEffectSuite->meshAlloc(output_mesh, output_point_count, output_vertex_count, output_face_count);
    printf("Suite method 'meshAlloc' returned status %d (%s)\n", status, getOfxStateName(status));

    float *output_points;
    status = runtime->propertySuite->propGetPointer(output_mesh, kOfxMeshPropPointData, 0, (void**)&output_points);
    printf("Suite method 'propGetPointer' returned status %d (%s)\n", status, getOfxStateName(status));

    int *output_vertices;
    status = runtime->propertySuite->propGetPointer(output_mesh, kOfxMeshPropVertexData, 0, (void**)&output_vertices);
    printf("Suite method 'propGetPointer' returned status %d (%s)\n", status, getOfxStateName(status));

    int *output_faces;
    status = runtime->propertySuite->propGetPointer(output_mesh, kOfxMeshPropFaceData, 0, (void**)&output_faces);
    printf("Suite method 'propGetPointer' returned status %d (%s)\n", status, getOfxStateName(status));

    // TODO: Fill data
    output_points[0 * 3 + 0] = -1.0f;
    output_points[0 * 3 + 1] = -width;
    output_points[0 * 3 + 2] = 0.0f;

    output_points[1 * 3 + 0] = 1.0f;
    output_points[1 * 3 + 1] = -width;
    output_points[1 * 3 + 2] = 0.0f;

    output_points[2 * 3 + 0] = 1.0f;
    output_points[2 * 3 + 1] = width;
    output_points[2 * 3 + 2] = 0.0f;

    output_points[3 * 3 + 0] = -1.0f;
    output_points[3 * 3 + 1] = width;
    output_points[3 * 3 + 2] = 0.0f;

    for (int i = 0 ; i < 4 ; ++i) output_vertices[i] = i;

    output_faces[0] = 4;

    status = runtime->meshEffectSuite->inputReleaseMesh(output_mesh);
    printf("Suite method 'inputReleaseMesh' returned status %d (%s)\n", status, getOfxStateName(status));

    return kOfxStatOK;
}

static OfxStatus plugin0_mainEntry(const char *action,
                                   const void *handle,
                                   OfxPropertySetHandle inArgs,
                                   OfxPropertySetHandle outArgs) {
    if (0 == strcmp(action, kOfxActionLoad)) {
        return plugin0_load(&plugin0_runtime);
    }
    if (0 == strcmp(action, kOfxActionDescribe)) {
        return plugin0_describe(&plugin0_runtime, (OfxMeshEffectHandle)handle);
    }
    if (0 == strcmp(action, kOfxActionCreateInstance)) {
        return kOfxStatOK;
    }
    if (0 == strcmp(action, kOfxActionDestroyInstance)) {
        return kOfxStatOK;
    }
    if (0 == strcmp(action, kOfxMeshEffectActionCook)) {
        return plugin0_cook(&plugin0_runtime, (OfxMeshEffectHandle)handle);
    }
    return kOfxStatReplyDefault;
}

static void plugin0_setHost(OfxHost *host) {
    plugin0_runtime.host = host;
}

static OfxStatus plugin1_mainEntry(const char *action,
                                   const void *handle,
                                   OfxPropertySetHandle inArgs,
                                   OfxPropertySetHandle outArgs) {
    (void)action;
    (void)handle;
    (void)inArgs;
    (void)outArgs;
    return kOfxStatReplyDefault;
}

static void plugin1_setHost(OfxHost *host) {
    plugin1_runtime.host = host;
}

OfxExport int OfxGetNumberOfPlugins(void) {
    return 2;
}

OfxExport OfxPlugin *OfxGetPlugin(int nth) {
    static OfxPlugin plugins[] = {
        {
        /* pluginApi */          kOfxMeshEffectPluginApi,
        /* apiVersion */         kOfxMeshEffectPluginApiVersion,
        /* pluginIdentifier */   "MfxSamplePlugin0",
        /* pluginVersionMajor */ 1,
        /* pluginVersionMinor */ 0,
        /* setHost */            plugin0_setHost,
        /* mainEntry */          plugin0_mainEntry
        },
        {
        /* pluginApi */          kOfxMeshEffectPluginApi,
        /* apiVersion */         kOfxMeshEffectPluginApiVersion,
        /* pluginIdentifier */   "MfxSamplePlugin1",
        /* pluginVersionMajor */ 1,
        /* pluginVersionMinor */ 0,
        /* setHost */            plugin1_setHost,
        /* mainEntry */          plugin1_mainEntry
        }
    };
    return plugins + nth;
}
