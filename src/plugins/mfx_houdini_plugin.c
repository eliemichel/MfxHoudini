/*
 * Copyright 2019 - 2020 Elie Michel
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
#include "util/plugin_support.h"

#include "ofxCore.h"
#include "ofxMeshEffect.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32

#include "houdini_utils.h"
#include "hruntime.h"

// Houdini


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
/*
typedef struct PluginRuntime {
	OfxPlugin plugin;
	int pluginIndex;
	OfxHost *host;
	OfxPropertySuiteV1 *propertySuite;
	OfxParameterSuiteV1 *parameterSuite;
	OfxMeshEffectSuiteV1 *meshEffectSuite;
	HoudiniRuntime *houdiniRuntime;
} PluginRuntime;
*/
static OfxStatus plugin_load(PluginRuntime *runtime) {
	OfxHost *h = runtime->host;
	runtime->propertySuite = (OfxPropertySuiteV1*)h->fetchSuite(h->host, kOfxPropertySuite, 1);
	runtime->parameterSuite = (OfxParameterSuiteV1*)h->fetchSuite(h->host, kOfxParameterSuite, 1);
	runtime->meshEffectSuite = (OfxMeshEffectSuiteV1*)h->fetchSuite(h->host, kOfxMeshEffectSuite, 1);
	runtime->userData = malloc_array(sizeof(HoudiniRuntime), 1, "houdini runtime");
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;
	if (false == hruntime_init(hr)) {
		return kOfxStatFailed;
	}

	hruntime_set_library(hr, get_hda_path());
	hr->current_asset_index = runtime->pluginIndex;
	return kOfxStatOK;
}

static OfxStatus plugin_unload(PluginRuntime *runtime) {
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;
	hruntime_free(hr);
	return kOfxStatOK;
}

static void plugin_set_default_parameter(const PluginRuntime* runtime, OfxPropertySetHandle paramProps, const HAPI_ParmInfo *info)
{
	HAPI_Result res;
	OfxStatus status;
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;

	if (info->size > 4)
	{
		printf("unsupported default value for parameter with dimension > 4");
		return;
	}

	switch (info->type)
	{
	case HAPI_PARMTYPE_FLOAT:
	case HAPI_PARMTYPE_COLOR:
	{
		double dvalues[4];
		float fvalues[4];
		H_CHECK_OR(HAPI_GetParmFloatValues(&hr->hsession, hr->node_id, fvalues, info->floatValuesIndex, info->size)) {}
		for (int i = 0; i < info->size; ++i) {
			dvalues[i] = (double)fvalues[i];
		}
		MFX_CHECK(propertySuite->propSetDoubleN(paramProps, kOfxParamPropDefault, info->size, dvalues));
		break;
	}
	case HAPI_PARMTYPE_INT:
	{
		int values[4];
		H_CHECK_OR(HAPI_GetParmIntValues(&hr->hsession, hr->node_id, values, info->intValuesIndex, info->size)) {}
		MFX_CHECK(propertySuite->propSetIntN(paramProps, kOfxParamPropDefault, info->size, values));
		break;
	}
	}
}

static OfxStatus plugin_describe(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	if (NULL == runtime->propertySuite || NULL == runtime->meshEffectSuite) {
		return kOfxStatErrMissingHostFeature;
	}

	OfxStatus status;
	OfxPropertySetHandle propHandle;

	MFX_CHECK(meshEffectSuite->getPropertySet(meshEffect, &propHandle));

	MFX_CHECK(propertySuite->propSetString(propHandle, kOfxMeshEffectPropContext, 0, kOfxMeshEffectContextFilter));

	// Shall move into "describe in context" when it will exist
	OfxPropertySetHandle inputProperties;
	MFX_CHECK(meshEffectSuite->inputDefine(meshEffect, kOfxMeshMainInput, &inputProperties));

	MFX_CHECK(propertySuite->propSetString(inputProperties, kOfxPropLabel, 0, "Main Input"));

	OfxPropertySetHandle outputProperties;
	MFX_CHECK(meshEffectSuite->inputDefine(meshEffect, kOfxMeshMainOutput, &outputProperties)); // yes, output are also "inputs", I should change this name in the API
	
	MFX_CHECK(propertySuite->propSetString(outputProperties, kOfxPropLabel, 0, "Main Output"));

	// Declare parameters
	OfxParamSetHandle parameters;
	OfxParamHandle param;
	OfxPropertySetHandle paramProps;
	MFX_CHECK(meshEffectSuite->getParamSet(meshEffect, &parameters));

	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;
	hruntime_create_node(hr);
	hruntime_fetch_parameters(hr);
	char name[MOD_HOUDINI_MAX_PARAMETER_NAME];
	for (int i = 0 ; i < hr->parm_count ; ++i) {
		hruntime_get_parameter_name(hr, i, name);

		HAPI_ParmInfo info = hr->parm_infos_array[i];
		const char *type = houdini_to_ofx_type(info.type, info.size);

		if (NULL != type && 0 == strncmp(name, "mfx_", 4)) {
			printf("Defining parameter %s\n", name);
			MFX_CHECK(parameterSuite->paramDefine(parameters, type, name, &paramProps));
			plugin_set_default_parameter(runtime, paramProps, &info);
		}
	}
	hruntime_destroy_node(hr);

	return kOfxStatOK;
}

static OfxStatus plugin_create_instance(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;
	OfxPropertySetHandle propHandle;
	hruntime_create_node(hr);
	hruntime_fetch_parameters(hr);
	runtime->meshEffectSuite->getPropertySet(meshEffect, &propHandle);
	runtime->propertySuite->propSetInt(propHandle, kOfxPropHoudiniNodeId, 0, hr->node_id);
	return kOfxStatOK;
}

static OfxStatus plugin_destroy_instance(const PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;
	OfxPropertySetHandle propHandle;
	runtime->meshEffectSuite->getPropertySet(meshEffect, &propHandle);
	runtime->propertySuite->propGetInt(propHandle, kOfxPropHoudiniNodeId, 0, &hr->node_id);
	hruntime_destroy_node(hr);
	return kOfxStatOK;
}

static void log_status() {

}

static void copy_d4_to_f4(float float_values[4], double double_values[4]) {
	float_values[0] = (float)double_values[0];
	float_values[1] = (float)double_values[1];
	float_values[2] = (float)double_values[2];
	float_values[3] = (float)double_values[3];
}

static bool plugin_get_parm_from_ofx(PluginRuntime *runtime, int parm_index, HAPI_ParmType type, int size, OfxParamHandle param) {
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;
	double double_values[4];
	float float_values[4];
	int int_values[4];
	switch (type) {
	case HAPI_PARMTYPE_INT:
		switch (size) {
		case 0:
			size = 1;
		case 1:
			runtime->parameterSuite->paramGetValue(param, int_values+0);
			break;
		case 2:
			runtime->parameterSuite->paramGetValue(param, int_values+0, int_values+1);
			break;
		case 3:
			runtime->parameterSuite->paramGetValue(param, int_values+0, int_values+1, int_values+2);
			break;
		default:
			return false;
		}
		hruntime_set_int_parm(hr, parm_index, int_values, size);
		break;
	case HAPI_PARMTYPE_FLOAT:
		switch (size) {
		case 0:
			size = 1;
		case 1:
			runtime->parameterSuite->paramGetValue(param, double_values+0);
			break;
		case 2:
			runtime->parameterSuite->paramGetValue(param, double_values+0, double_values+1);
			break;
		case 3:
			runtime->parameterSuite->paramGetValue(param, double_values+0, double_values+1, double_values+2);
			break;
		default:
			return false;
		}
		copy_d4_to_f4(float_values, double_values);
		hruntime_set_float_parm(hr, parm_index, float_values, size);
		break;
	case HAPI_PARMTYPE_COLOR:
		switch (size) {
		case 3:
			runtime->parameterSuite->paramGetValue(param, double_values+0, double_values+1, double_values+2);
			break;
		case 4:
			runtime->parameterSuite->paramGetValue(param, double_values+0, double_values+1, double_values+2, double_values+3);
			break;
		default:
			return false;
		}
		copy_d4_to_f4(float_values, double_values);
		hruntime_set_float_parm(hr, parm_index, float_values, size);
		break;
	case HAPI_PARMTYPE_STRING:
		return false; // TODO
	default:
		return false;
	}
	return true;
}

static OfxStatus plugin_cook(PluginRuntime *runtime, OfxMeshEffectHandle meshEffect) {
	OfxStatus status;
	OfxMeshInputHandle input, output;
	OfxPropertySetHandle propertySet, effectProperties;
	HoudiniRuntime* hr = (HoudiniRuntime*)runtime->userData;

	// Set node id in houdini runtime to match this mesh effect instance
	runtime->meshEffectSuite->getPropertySet(meshEffect, &effectProperties);
	runtime->propertySuite->propGetInt(effectProperties, kOfxPropHoudiniNodeId, 0, &hr->node_id);

	status = runtime->meshEffectSuite->inputGetHandle(meshEffect, kOfxMeshMainInput, &input, &propertySet);
	printf("Suite method 'inputGetHandle' returned status %d (%s)\n", status, getOfxStateName(status));
	if (status != kOfxStatOK) {
		return kOfxStatErrUnknown;
	}

	status = runtime->meshEffectSuite->inputGetHandle(meshEffect, kOfxMeshMainOutput, &output, &propertySet);
	printf("Suite method 'inputGetHandle' returned status %d (%s)\n", status, getOfxStateName(status));
	if (status != kOfxStatOK) {
		return kOfxStatErrUnknown;
	}

	OfxTime time = 0;
	OfxMeshHandle input_mesh;
	OfxPropertySetHandle input_mesh_prop;
	
	MFX_CHECK(meshEffectSuite->inputGetMesh(input, time, &input_mesh, &input_mesh_prop));

	// Get input data
	int input_point_count = 0, input_vertex_count = 0, input_face_count = 0;
	MFX_CHECK(propertySuite->propGetInt(input_mesh_prop, kOfxMeshPropPointCount, 0, &input_point_count));
	MFX_CHECK(propertySuite->propGetInt(input_mesh_prop, kOfxMeshPropVertexCount, 0, &input_vertex_count));
	MFX_CHECK(propertySuite->propGetInt(input_mesh_prop, kOfxMeshPropFaceCount, 0, &input_face_count));

	Attribute input_pos, input_vertpoint, input_facecounts;
	MFX_CHECK2(getPointAttribute(runtime, input_mesh, kOfxMeshAttribPointPosition, &input_pos));
	MFX_CHECK2(getVertexAttribute(runtime, input_mesh, kOfxMeshAttribVertexPoint, &input_vertpoint));
	MFX_CHECK2(getFaceAttribute(runtime, input_mesh, kOfxMeshAttribFaceCounts, &input_facecounts));

	printf("DEBUG: Found %d points in input mesh\n", input_point_count);

	hruntime_feed_input_data(hr,
		                     input_pos, input_point_count,
		                     input_vertpoint, input_vertex_count,
		                     input_facecounts, input_face_count);
	
	MFX_CHECK(meshEffectSuite->inputReleaseMesh(input_mesh));

	// Get parameters
	OfxParamSetHandle parameters;
	OfxParamHandle param;
	MFX_CHECK(meshEffectSuite->getParamSet(meshEffect, &parameters));

	char name[MOD_HOUDINI_MAX_PARAMETER_NAME];
	for (int i = 0 ; i < hr->parm_count ; ++i) {
		hruntime_get_parameter_name(hr, i, name);

		HAPI_ParmInfo info = hr->parm_infos_array[i];
		const char *type = houdini_to_ofx_type(info.type, info.size);

		if (NULL != type && 0 == strncmp(name, "mfx_", 4)) {
			runtime->parameterSuite->paramGetHandle(parameters, name, &param, NULL);
			if (false == plugin_get_parm_from_ofx(runtime, i, info.type, info.size, param)) {
				printf("Could not get value from ofx for parm #%d (%s) -- type = %d, size = %d\n", i, name, info.type, info.size);
			}
		}
	}

	// Core cook

	if (false == hruntime_cook_asset(hr)) {
		return kOfxStatErrUnknown;
	}
	if (false == hruntime_fetch_sops(hr)) {
		return kOfxStatErrUnknown;
	}

	OfxMeshHandle output_mesh;
	OfxPropertySetHandle output_mesh_prop;
	status = runtime->meshEffectSuite->inputGetMesh(output, time, &output_mesh, &output_mesh_prop);
	printf("Suite method 'inputGetMesh' returned status %d (%s)\n", status, getOfxStateName(status));

	// Consolidate geo counts
	int output_point_count = 0, output_vertex_count = 0, output_face_count = 0;
	hruntime_consolidate_geo_counts(hr,
		                            &output_point_count,
		                            &output_vertex_count,
		                            &output_face_count);

	printf("DEBUG: Allocating output mesh data: %d points, %d vertices, %d faces\n", output_point_count, output_vertex_count, output_face_count);

	MFX_CHECK(propertySuite->propSetInt(output_mesh_prop, kOfxMeshPropPointCount, 0, output_point_count));
	MFX_CHECK(propertySuite->propSetInt(output_mesh_prop, kOfxMeshPropVertexCount, 0, output_vertex_count));
	MFX_CHECK(propertySuite->propSetInt(output_mesh_prop, kOfxMeshPropFaceCount, 0, output_face_count));

	// Declare output attributes
	bool has_uv = hruntime_has_vertex_attribute(hr, "uv");
	if (has_uv) {
		OfxPropertySetHandle uv_attrib;
		MFX_CHECK(meshEffectSuite->attributeDefine(output_mesh, kOfxMeshAttribVertex, "uv0", 2, kOfxMeshAttribTypeFloat, &uv_attrib));
	}

	MFX_CHECK(meshEffectSuite->meshAlloc(output_mesh));

	Attribute output_pos, output_vertpoint, output_facecounts, output_uv;
	MFX_CHECK2(getPointAttribute(runtime, output_mesh, kOfxMeshAttribPointPosition, &output_pos));
	MFX_CHECK2(getVertexAttribute(runtime, output_mesh, kOfxMeshAttribVertexPoint, &output_vertpoint));
	MFX_CHECK2(getFaceAttribute(runtime, output_mesh, kOfxMeshAttribFaceCounts, &output_facecounts));

	// Fill data
	hruntime_fill_mesh(hr,
		               output_pos, output_point_count,
		               output_vertpoint, output_vertex_count,
		               output_facecounts, output_face_count);

	if (has_uv) {
		MFX_CHECK2(getVertexAttribute(runtime, output_mesh, "uv0", &output_uv));
		hruntime_fill_vertex_attribute(hr, output_uv, "uv");
	}

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
