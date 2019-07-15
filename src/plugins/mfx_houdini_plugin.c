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
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32

#define kMainInput "MainInput"
#define kMainOutput "MainOutput"

#define MAX_NUM_PLUGINS 10
#define MAX_BUNDLE_DIRECTORY 1024
#define MOD_HOUDINI_MAX_ASSET_NAME 1024
#define MOD_HOUDINI_MAX_PARAMETER_NAME 256

#define kOfxPropHoudiniNodeId "OfxPropHoudiniNodeId"

// Utils

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

const char * HAPI_ResultMessage(HAPI_Result res) {
	static const char *messages[] = {
		"HAPI_RESULT_SUCCESS",
		"HAPI_RESULT_FAILURE",
		"HAPI_RESULT_ALREADY_INITIALIZED",
		"HAPI_RESULT_NOT_INITIALIZED",
		"HAPI_RESULT_CANT_LOADFILE",
		"HAPI_RESULT_PARM_SET_FAILED",
		"HAPI_RESULT_INVALID_ARGUMENT",
		"HAPI_RESULT_CANT_LOAD_GEO",
		"HAPI_RESULT_CANT_GENERATE_PRESET",
		"HAPI_RESULT_CANT_LOAD_PRESET",
		"HAPI_RESULT_ASSET_DEF_ALREADY_LOADED",

		"HAPI_RESULT_NO_LICENSE_FOUND",
		"HAPI_RESULT_DISALLOWED_NC_LICENSE_FOUND",
		"HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_C_LICENSE",
		"HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_LC_LICENSE",
		"HAPI_RESULT_DISALLOWED_LC_ASSET_WITH_C_LICENSE",
		"HAPI_RESULT_DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN",

		"HAPI_RESULT_ASSET_INVALID",
		"HAPI_RESULT_NODE_INVALID",

		"HAPI_RESULT_USER_INTERRUPTED",

		"HAPI_RESULT_INVALID_SESSION",
		"Unknown HAPI Result",
	};
	if (res <= 10) {
		return messages[res];
	}
	if (res <= 160) {
		return messages[res / 10];
	}
	switch (res) {
	case HAPI_RESULT_ASSET_INVALID:
		return messages[17];
	case HAPI_RESULT_NODE_INVALID:
		return messages[18];
	case HAPI_RESULT_USER_INTERRUPTED:
		return messages[19];
	case HAPI_RESULT_INVALID_SESSION:
		return messages[20];
	default:
		return messages[21];
	}
}

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
	char *error_message;
} HoudiniRuntime;

// Global session
static HAPI_Session global_hsession;
static int global_hsession_users = 0;

static void hruntime_set_error(HoudiniRuntime *hr, const char *fmt, ...) {
	va_list args;
	int len;

	if (NULL == hr->error_message) {
		free_array(hr->error_message);
		hr->error_message = NULL;
	}

	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	hr->error_message = malloc_array(sizeof(char), len + 1, "hruntime error message");

	va_start(args, fmt);
	vsprintf(hr->error_message, fmt, args);
	va_end(args);

	fprintf(stderr, "Houdini Runtime error: %s", hr->error_message);
}
#define ERR(...) hruntime_set_error(hr, __VA_ARGS__)

static bool hruntime_init(HoudiniRuntime *hr) {
	HAPI_Result res;

	if (0 == global_hsession_users) {
		HAPI_CookOptions cookOptions;
		cookOptions.maxVerticesPerPrimitive = 3; // TODO: switch back to -1 when it works with 3

		printf("Creating Houdini Session\n");

		res = HAPI_CreateInProcessSession(&global_hsession);
		if (HAPI_RESULT_SUCCESS != res) {
			ERR("Houdini error in HAPI_CreateInProcessSession: %u (%s)\n", res, HAPI_ResultMessage(res));
			return false;
		}

		res = HAPI_Initialize(&global_hsession, &cookOptions, false /* threaded cooking */, -1, NULL, NULL, NULL, NULL, NULL);
		if (HAPI_RESULT_SUCCESS != res) {
			ERR("Houdini error in HAPI_Initialize: %u (%s)\n", res, HAPI_ResultMessage(res));
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
	hr->error_message = NULL;

	return true;
}

static void hruntime_free(HoudiniRuntime *hr) {
	if (NULL != hr->asset_names_array) {
		free_array(hr->asset_names_array);
	}
	if (NULL != hr->parm_infos_array) {
		free_array(hr->parm_infos_array);
	}

	if (NULL != hr->error_message) {
		free_array(hr->error_message);
	}

	global_hsession_users--;
	if (0 == global_hsession_users) {
		HAPI_Result res;

		printf("Releasing Houdini Session\n");

		res = HAPI_Cleanup(&hr->hsession);
		if (HAPI_RESULT_SUCCESS != res) {
			ERR("Houdini error in HAPI_Cleanup: %u (%s)\n", res, HAPI_ResultMessage(res));
		}
	}
	free_array(hr);
}

// private
static void hruntime_close_library(HoudiniRuntime *hr) {
	// TODO: Find a way to release the HAPI_AssetLibraryId

	if (NULL != hr->asset_names_array) {
		free_array(hr->asset_names_array);
		hr->asset_names_array = NULL;
	}
}

// private
static bool hruntime_load_library(HoudiniRuntime *hr) {
	// Load library
	HAPI_Result res;
	printf("Loading Houdini library %s...\n", hr->current_library_path);

	res = HAPI_LoadAssetLibraryFromFile(&hr->hsession, hr->current_library_path, true, &hr->library);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_LoadAssetLibraryFromFile: %u (%s)\n", res, HAPI_ResultMessage(res));
		return false;
	}

	// Update asset count
	res = HAPI_GetAvailableAssetCount(&hr->hsession, hr->library, &hr->asset_count);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_GetAvailableAssetCount: %u (%s)\n", res, HAPI_ResultMessage(res));
		return false;
	}

	hr->asset_names_array = malloc_array(sizeof(HAPI_StringHandle), hr->asset_count, "houdini asset names");
	res = HAPI_GetAvailableAssets(&hr->hsession, hr->library, hr->asset_names_array, hr->asset_count);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_GetAvailableAssets: %u (%s)\n", res, HAPI_ResultMessage(res));
		return false;
	}

	return true;
}

static void hruntime_set_library(HoudiniRuntime *hr, const char *new_library_path) {
	if (0 != strcmp(hr->current_library_path, "")) {
		hruntime_close_library(hr);
	}

	strcpy(hr->current_library_path, new_library_path);

	if (0 == strcmp(hr->current_library_path, "")) {
		printf("No Houdini library selected\n");
		hr->asset_count = 0;
		hr->current_asset_index = -1;
	}
	else {
		hruntime_load_library(hr);
	}
}

/**
 * /pre hruntime_set_library_path has been called
 */
static void hruntime_create_node(HoudiniRuntime *hr) {
	HAPI_Result res;

	char asset_name[MOD_HOUDINI_MAX_ASSET_NAME];
	res = HAPI_GetString(&hr->hsession, hr->asset_names_array[hr->current_asset_index], asset_name, MOD_HOUDINI_MAX_ASSET_NAME);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_GetString: %u (%s)\n", res, HAPI_ResultMessage(res));
		return;
	}

	res = HAPI_CreateNode(&hr->hsession, -1, asset_name, NULL, false /* cook */, &hr->node_id);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_CreateNode: %u (%s)\n", res, HAPI_ResultMessage(res));
		return;
	}
}

static void hruntime_destroy_node(HoudiniRuntime *hr) {
	HAPI_Result res;
	res = HAPI_DeleteNode(&hr->hsession, hr->node_id);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_CreateNode: %u (%s)\n", res, HAPI_ResultMessage(res));
		return;
	}
}

/**
 * /pre hruntime_create_node has been called
 */
static void hruntime_fetch_parameters(HoudiniRuntime *hr) {
	HAPI_Result res;

	if (NULL != hr->parm_infos_array) {
		free_array(hr->parm_infos_array);
		hr->parm_infos_array = NULL;
		hr->parm_count = 0;
	}

	HAPI_NodeInfo node_info;
	res = HAPI_GetNodeInfo(&hr->hsession, hr->node_id, &node_info);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_GetNodeInfo: %u (%s)\n", res, HAPI_ResultMessage(res));
		return;
	}

	hr->parm_count = node_info.parmCount;

	if (0 != node_info.parmCount) {
		hr->parm_infos_array = malloc_array(sizeof(HAPI_ParmInfo), node_info.parmCount, "houdini parameter info");

		res = HAPI_GetParameters(&hr->hsession, hr->node_id, hr->parm_infos_array, 0, node_info.parmCount);
		if (HAPI_RESULT_SUCCESS != res) {
			ERR("Houdini error in HAPI_GetParameters: %u (%s)\n", res, HAPI_ResultMessage(res));
			return;
		}
	}
}

/**
 * /pre hruntime_fetch_parameters has been called
 */
static void hruntime_get_parameter_name(HoudiniRuntime *hr, int idx, char *name) {
	HAPI_Result res;
	res = HAPI_GetString(&hr->hsession, hr->parm_infos_array[idx].nameSH, name, MOD_HOUDINI_MAX_PARAMETER_NAME);
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_GetString: %u (%s)\n", res, HAPI_ResultMessage(res));
	}
}

// Resources

static char bundle_directory[MAX_BUNDLE_DIRECTORY];

const char * get_hda_path() {
	static char path[MAX_BUNDLE_DIRECTORY];
	size_t len = strlen(bundle_directory);
	strcpy(path, bundle_directory);
	strncpy(path + len, "\\library.hda", MAX_BUNDLE_DIRECTORY - len); // TODO: forward slash, resource folder
	return path;
}

// OFX

typedef struct PluginRuntime {
	OfxPlugin plugin;
	int pluginIndex;
	OfxHost *host;
	OfxPropertySuiteV1 *propertySuite;
	OfxParameterSuiteV1 *parameterSuite;
	OfxMeshEffectSuiteV1 *meshEffectSuite;
	HoudiniRuntime *houdiniRuntime;
} PluginRuntime;

static OfxStatus plugin_load(PluginRuntime *runtime) {
	OfxHost *h = runtime->host;
	runtime->propertySuite = (OfxPropertySuiteV1*)h->fetchSuite(h->host, kOfxPropertySuite, 1);
	runtime->parameterSuite = (OfxParameterSuiteV1*)h->fetchSuite(h->host, kOfxParameterSuite, 1);
	runtime->meshEffectSuite = (OfxMeshEffectSuiteV1*)h->fetchSuite(h->host, kOfxMeshEffectSuite, 1);
	runtime->houdiniRuntime = malloc_array(sizeof(HoudiniRuntime), 1, "houdini runtime");
	if (false == hruntime_init(runtime->houdiniRuntime)) {
		return kOfxStatFailed;
	}

	hruntime_set_library(runtime->houdiniRuntime, get_hda_path());
	runtime->houdiniRuntime->current_asset_index = runtime->pluginIndex;
	return kOfxStatOK;
}

static OfxStatus plugin_unload(PluginRuntime *runtime) {
	hruntime_free(runtime->houdiniRuntime);
	return kOfxStatOK;
}

static OfxStatus plugin_describe(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
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

	hruntime_create_node(runtime->houdiniRuntime);
	hruntime_fetch_parameters(runtime->houdiniRuntime);
	char name[MOD_HOUDINI_MAX_PARAMETER_NAME];
	for (int i = 0 ; i < runtime->houdiniRuntime->parm_count ; ++i) {
		hruntime_get_parameter_name(runtime->houdiniRuntime, i, name);

		char *type;
		switch(runtime->houdiniRuntime->parm_infos_array[i].type) {
		case HAPI_PARMTYPE_FLOAT:
			type = kOfxParamTypeDouble;
			break;
		case HAPI_PARMTYPE_INT:
			type = kOfxParamTypeInteger;
			break;
		case HAPI_PARMTYPE_STRING:
			type = kOfxParamTypeString;
			break;
		default:
			type = NULL;
		}

		if (NULL != type && 0 == strcmp(kOfxParamTypeDouble, type) && 0 == strncmp(name, "hbridge_", 8)) {
			printf("Defining parameter %s\n", name);
			status = runtime->parameterSuite->paramDefine(parameters, type, name, NULL);
			printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));
		}
	}
	hruntime_destroy_node(runtime->houdiniRuntime);

	status = runtime->parameterSuite->paramDefine(parameters, kOfxParamTypeDouble, "width", NULL);
	printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));

	status = runtime->parameterSuite->paramDefine(parameters, kOfxParamTypeInteger, "steps", NULL);
	printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));

	status = runtime->parameterSuite->paramDefine(parameters, kOfxParamTypeString, "path", NULL);
	printf("Suite method 'paramDefine' returned status %d (%s)\n", status, getOfxStateName(status));

	return kOfxStatOK;
}

static OfxStatus plugin_create_instance(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	HoudiniRuntime *hr = runtime->houdiniRuntime;
	OfxPropertySetHandle propHandle;
	hruntime_create_node(hr);
	runtime->meshEffectSuite->getPropertySet(meshEffect, &propHandle);
	runtime->propertySuite->propSetInt(propHandle, kOfxPropHoudiniNodeId, 0, hr->node_id);
	return kOfxStatOK;
}

static OfxStatus plugin_destroy_instance(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	HoudiniRuntime *hr = runtime->houdiniRuntime;
	OfxPropertySetHandle propHandle;
	runtime->meshEffectSuite->getPropertySet(meshEffect, &propHandle);
	runtime->propertySuite->propGetInt(propHandle, kOfxPropHoudiniNodeId, 0, &hr->node_id);
	hruntime_destroy_node(hr);
	return kOfxStatOK;
}

static OfxStatus plugin_cook(PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	OfxStatus status;
	OfxMeshInputHandle input, output;
	OfxPropertySetHandle propertySet, effectProperties;
	HoudiniRuntime *hr = runtime->houdiniRuntime;

	// Set node id in houdini runtime to match this mesh effect instance
	runtime->meshEffectSuite->getPropertySet(meshEffect, &effectProperties);
	runtime->propertySuite->propGetInt(effectProperties, kOfxPropHoudiniNodeId, 0, &hr->node_id);

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

	for (int i = 0; i < 4; ++i) output_vertices[i] = i;

	output_faces[0] = 4;

	status = runtime->meshEffectSuite->inputReleaseMesh(output_mesh);
	printf("Suite method 'inputReleaseMesh' returned status %d (%s)\n", status, getOfxStateName(status));

	return kOfxStatOK;
}

static PluginRuntime plugins[MAX_NUM_PLUGINS];

static void setHost(int nth, OfxHost *host) {
	plugins[nth].host = host;
}

static OfxStatus mainEntry(int nth,
	                       const char *action,
	                       const void *handle,
	                       OfxPropertySetHandle inArgs,
	                       OfxPropertySetHandle outArgs) {
	if (0 == strcmp(action, kOfxActionLoad)) {
		return plugin_load(&plugins[nth]);
	}
	if (0 == strcmp(action, kOfxActionUnload)) {
		return plugin_unload(&plugins[nth]);
	}
	if (0 == strcmp(action, kOfxActionDescribe)) {
		return plugin_describe(&plugins[nth], (OfxMeshEffectHandle)handle);
	}
	if (0 == strcmp(action, kOfxActionCreateInstance)) {
		return plugin_create_instance(&plugins[nth], (OfxMeshEffectHandle)handle);
	}
	if (0 == strcmp(action, kOfxActionDestroyInstance)) {
		return plugin_destroy_instance(&plugins[nth], (OfxMeshEffectHandle)handle);
	}
	if (0 == strcmp(action, kOfxMeshEffectActionCook)) {
		return plugin_cook(&plugins[nth], (OfxMeshEffectHandle)handle);
	}
	return kOfxStatReplyDefault;
}

// Closure mechanisme
// to dynamically define OfxPlugin structs

typedef void (OfxPluginSetHost)(OfxHost *host);

static OfxPluginSetHost * setHost_pointers[MAX_NUM_PLUGINS];
static OfxPluginEntryPoint * mainEntry_pointers[MAX_NUM_PLUGINS];
static char pluginIdentifier_pointers[MAX_NUM_PLUGINS][MOD_HOUDINI_MAX_ASSET_NAME];

#define MAKE_PLUGIN_CLOSURES(nth) \
static void plugin ## nth ## _setHost(OfxHost *host) { \
	setHost(nth, host); \
} \
static OfxStatus plugin ## nth ## _mainEntry(const char *action, \
	                                         const void *handle, \
	                                         OfxPropertySetHandle inArgs, \
	                                         OfxPropertySetHandle outArgs) { \
	return mainEntry(nth, action, handle, inArgs, outArgs); \
}

#define REGISTER_PLUGIN_CLOSURES(nth) \
setHost_pointers[nth] = plugin ## nth ## _setHost; \
mainEntry_pointers[nth] = plugin ## nth ## _mainEntry;

MAKE_PLUGIN_CLOSURES(0)
MAKE_PLUGIN_CLOSURES(1)
MAKE_PLUGIN_CLOSURES(2)
MAKE_PLUGIN_CLOSURES(3)
MAKE_PLUGIN_CLOSURES(4)
MAKE_PLUGIN_CLOSURES(5)
MAKE_PLUGIN_CLOSURES(6)
MAKE_PLUGIN_CLOSURES(7)
MAKE_PLUGIN_CLOSURES(8)
MAKE_PLUGIN_CLOSURES(9)
// MAX_NUM_PLUGINS

OfxExport void OfxSetBundleDirectory(const char *path) {
	strncpy(bundle_directory, path, MAX_BUNDLE_DIRECTORY);
}

OfxExport int OfxGetNumberOfPlugins(void) {
	REGISTER_PLUGIN_CLOSURES(0)
	REGISTER_PLUGIN_CLOSURES(1)
	REGISTER_PLUGIN_CLOSURES(2)
	REGISTER_PLUGIN_CLOSURES(3)
	REGISTER_PLUGIN_CLOSURES(4)
	REGISTER_PLUGIN_CLOSURES(5)
	REGISTER_PLUGIN_CLOSURES(6)
	REGISTER_PLUGIN_CLOSURES(7)
	REGISTER_PLUGIN_CLOSURES(8)
	REGISTER_PLUGIN_CLOSURES(9)
	// MAX_NUM_PLUGINS

	int num_plugins;
	HoudiniRuntime *hr = malloc_array(sizeof(HoudiniRuntime), 1, "houdini runtime");
	if (false == hruntime_init(hr)) {
		return 0;
	}

	hruntime_set_library(hr, get_hda_path());
	num_plugins = hr->asset_count;

	for (int i = 0 ; i < num_plugins ; ++i) {
		HAPI_Result res;
		res = HAPI_GetString(&hr->hsession, hr->asset_names_array[i], pluginIdentifier_pointers[i], MOD_HOUDINI_MAX_ASSET_NAME);
		if (HAPI_RESULT_SUCCESS != res) {
			ERR("Houdini error in HAPI_GetString: %u\n", res);
		}

		OfxPlugin *plugin = &plugins[i].plugin;
		plugin->pluginApi = kOfxMeshEffectPluginApi;
		plugin->apiVersion = kOfxMeshEffectPluginApiVersion;
		plugin->pluginIdentifier = pluginIdentifier_pointers[i];
		plugin->pluginVersionMajor = 1;
		plugin->pluginVersionMinor = 0;
		plugin->setHost = setHost_pointers[i];
		plugin->mainEntry = mainEntry_pointers[i];

		plugins[i].pluginIndex = i;
	}

	hruntime_free(hr);
	return min(num_plugins, MAX_NUM_PLUGINS);
}

OfxExport OfxPlugin *OfxGetPlugin(int nth) {
	return &plugins[nth].plugin;
}
