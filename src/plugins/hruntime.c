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

#include "hruntime.h"
#include "houdini_utils.h"
#include "util/memory_util.h"

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

 // Global session
static HAPI_Session global_hsession;
static int global_hsession_users = 0;

void hruntime_set_error(HoudiniRuntime* hr, const char* fmt, ...) {
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

#ifdef LOCAL_HSESSION
static bool houdini_session_init(HoudiniRuntime* hr)
{
	HAPI_Result res;
	HAPI_CookOptions cookOptions;
	cookOptions.maxVerticesPerPrimitive = -1;

	printf("Creating Houdini Session\n");

	H_CHECK(HAPI_CreateInProcessSession(&global_hsession));

	H_CHECK_OR(HAPI_Initialize(&global_hsession, &cookOptions, false /* threaded cooking */, -1, NULL, NULL, NULL, NULL, NULL))
	{
		if (HAPI_RESULT_ALREADY_INITIALIZED != res)
			return false;
	}

	return true;
}
#else // LOCAL_HSESSION
static bool houdini_session_init(HoudiniRuntime* hr)
{
	HAPI_Result res;

	// HARS server options
	HAPI_ThriftServerOptions serverOptions;
	serverOptions.autoClose = true;
	serverOptions.timeoutMs = 3000.0f;

	// Start our HARS server using the "hapi" named pipe
	// This call can be ignored if you have launched HARS manually before
	H_CHECK(HAPI_StartThriftNamedPipeServer(&serverOptions, "hapi", NULL));

	// Create a new HAPI session to use with that server
	H_CHECK(HAPI_CreateThriftNamedPipeSession(&global_hsession, "hapi"));

	// Initialize HAPI
	HAPI_CookOptions cookOptions = HAPI_CookOptions_Create();
	H_CHECK(HAPI_Initialize(
		&global_hsession,           // session
		&cookOptions,       // cook options
		false,                       // use_cooking_thread
		-1,                         // cooking_thread_stack_size
		"",                         // houdini_environment_files
		NULL,            // otl_search_path
		NULL,            // dso_search_path
		NULL,            // image_dso_search_path
		NULL));              // audio_dso_search_path
	
	return true;
}
#endif // else LOCAL_HSESSION

bool hruntime_init(HoudiniRuntime* hr) {
	HAPI_Result res;

	if (0 == global_hsession_users) {
		if (!houdini_session_init(hr)) return false;
	}

	global_hsession_users++;

	hr->hsession = global_hsession;
	hr->current_library_path[0] = '\0';
	hr->current_asset_index = -1;
	hr->asset_names_array = NULL;
	hr->asset_count = 0;
	hr->parm_infos_array = NULL;
	hr->sop_array = NULL;
	hr->parm_count = 0;
	hr->error_message = NULL;
	hr->input_node_id = -1;
	hr->input_sop_id = -1;

	return true;
}

void hruntime_free(HoudiniRuntime* hr) {
	if (NULL != hr->asset_names_array) {
		free_array(hr->asset_names_array);
	}
	if (NULL != hr->parm_infos_array) {
		free_array(hr->parm_infos_array);
	}
	if (NULL != hr->sop_array) {
		free_array(hr->sop_array);
	}
	if (NULL != hr->error_message) {
		free_array(hr->error_message);
	}

	global_hsession_users--;
	if (0 == global_hsession_users) {
		HAPI_Result res;

		printf("Releasing Houdini Session\n");

		H_CHECK_OR(HAPI_Cleanup(&hr->hsession)) {}
	}
	free_array(hr);
}

// private
static void hruntime_close_library(HoudiniRuntime* hr) {
	// TODO: Find a way to release the HAPI_AssetLibraryId

	if (NULL != hr->asset_names_array) {
		free_array(hr->asset_names_array);
		hr->asset_names_array = NULL;
	}
}

// private
static bool hruntime_load_library(HoudiniRuntime* hr) {
	// Load library
	HAPI_Result res;
	printf("Loading Houdini library %s...\n", hr->current_library_path);

	H_CHECK(HAPI_LoadAssetLibraryFromFile(&hr->hsession, hr->current_library_path, true, &hr->library));

	// Update asset count
	H_CHECK(HAPI_GetAvailableAssetCount(&hr->hsession, hr->library, &hr->asset_count));

	hr->asset_names_array = malloc_array(sizeof(HAPI_StringHandle), hr->asset_count, "houdini asset names");
	H_CHECK(HAPI_GetAvailableAssets(&hr->hsession, hr->library, hr->asset_names_array, hr->asset_count));

	return true;
}

void hruntime_set_library(HoudiniRuntime* hr, const char* new_library_path) {
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
void hruntime_create_node(HoudiniRuntime* hr) {
	HAPI_Result res;

	char asset_name[MOD_HOUDINI_MAX_ASSET_NAME];
	H_CHECK_OR(HAPI_GetString(&hr->hsession, hr->asset_names_array[hr->current_asset_index], asset_name, MOD_HOUDINI_MAX_ASSET_NAME))
		return;

	H_CHECK_OR(HAPI_CreateNode(&hr->hsession, -1, asset_name, NULL, false /* cook */, &hr->node_id))
		return;

	HAPI_NodeInfo node_info;
	H_CHECK_OR(HAPI_GetNodeInfo(&hr->hsession, hr->node_id, &node_info))
		return;
	
	hr->input_node_id = -1;
	hr->input_sop_id = -1;

	// If node is a SOP, create context OBJ and input node
	if (HAPI_NODETYPE_SOP == node_info.type) {
		H_CHECK_OR(HAPI_DeleteNode(&hr->hsession, hr->node_id))
			return;

		H_CHECK_OR(HAPI_CreateInputNode(&hr->hsession, &hr->input_node_id, NULL))
			return;
		
		HAPI_GeoInfo geo_info;
		H_CHECK_OR(HAPI_GetDisplayGeoInfo(&hr->hsession, hr->input_node_id, &geo_info))
			return;
		hr->input_sop_id = geo_info.nodeId;

		//res = HAPI_CreateNode(&hr->hsession, hr->input_node_id, asset_name, NULL, false /* cook */, &hr->node_id);
		H_CHECK_OR(HAPI_CreateNode(&hr->hsession, -1, asset_name, NULL, false /* cook */, &hr->node_id))
			return;
		
		H_CHECK_OR(HAPI_ConnectNodeInput(&hr->hsession, hr->node_id, 0, hr->input_sop_id, 0))
			return;
	}
}

void hruntime_destroy_node(HoudiniRuntime* hr) {
	HAPI_Result res;

	H_CHECK_OR(HAPI_DeleteNode(&hr->hsession, hr->node_id))
		return;
	
	if (-1 != hr->input_node_id) {
		H_CHECK_OR(HAPI_DeleteNode(&hr->hsession, hr->node_id))
			return;
	}
}

/**
 * /pre hruntime_create_node has been called
 */
void hruntime_fetch_parameters(HoudiniRuntime* hr) {
	HAPI_Result res;

	if (NULL != hr->parm_infos_array) {
		free_array(hr->parm_infos_array);
		hr->parm_infos_array = NULL;
		hr->parm_count = 0;
	}

	HAPI_NodeInfo node_info;
	H_CHECK_OR(HAPI_GetNodeInfo(&hr->hsession, hr->node_id, &node_info))
		return;

	hr->parm_count = node_info.parmCount;

	if (0 != node_info.parmCount) {
		hr->parm_infos_array = malloc_array(sizeof(HAPI_ParmInfo), node_info.parmCount, "houdini parameter info");

		H_CHECK_OR(HAPI_GetParameters(&hr->hsession, hr->node_id, hr->parm_infos_array, 0, node_info.parmCount))
			return;
	}
}

/**
 * /pre hruntime_fetch_parameters has been called
 */
void hruntime_get_parameter_name(HoudiniRuntime* hr, int parm_index, char* name) {
	HAPI_Result res;
	H_CHECK_OR(HAPI_GetString(&hr->hsession, hr->parm_infos_array[parm_index].nameSH, name, MOD_HOUDINI_MAX_PARAMETER_NAME)) {}
}

void hruntime_set_float_parm(HoudiniRuntime* hr, int parm_index, const float* values, int length) {
	HAPI_Result res;
	H_CHECK_OR(HAPI_SetParmFloatValues(&hr->hsession, hr->node_id, values, hr->parm_infos_array[parm_index].floatValuesIndex, length)) {}
}

void hruntime_set_int_parm(HoudiniRuntime* hr, int parm_index, const int* values, int length) {
	HAPI_Result res;
	H_CHECK_OR(HAPI_SetParmIntValues(&hr->hsession, hr->node_id, values, hr->parm_infos_array[parm_index].intValuesIndex, length)) {}
}

bool hruntime_cook_asset(HoudiniRuntime* hr) {
	HAPI_Result res;
	int status;
	HAPI_State cooking_state;

	printf("Houdini: cooking root node...\n");
	H_CHECK(HAPI_CookNode(&hr->hsession, hr->node_id, NULL));

	res = HAPI_GetStatus(&hr->hsession, HAPI_STATUS_COOK_STATE, &status);
	cooking_state = (HAPI_State)status;
	if (HAPI_RESULT_SUCCESS != res) {
		ERR("Houdini error in HAPI_GetStatus: %u (%s)\n", res, HAPI_ResultMessage(res));
		cooking_state = HAPI_STATE_LOADING;
	}

	printf("Houdini cooking state: %u\n", cooking_state);
	bool is_ready = cooking_state <= HAPI_STATE_MAX_READY_STATE;

	if (is_ready) {
		if (cooking_state == HAPI_STATE_READY_WITH_FATAL_ERRORS) {
			printf("Warning: Houdini Cooking terminated with fatal errors.\n");
		}
		else if (cooking_state == HAPI_STATE_READY_WITH_COOK_ERRORS) {
			printf("Warning: Houdini Cooking terminated with cook errors.\n");
		}
	}

	if (!is_ready) {
		printf("Cooking not finished, skipping Houdini modifier.\n");
		return false;
	}

	return true;
}

bool hruntime_fetch_sops(HoudiniRuntime* hr) {
	HAPI_Result res;
	HAPI_NodeInfo node_info;

	H_CHECK(HAPI_GetNodeInfo(&hr->hsession, hr->node_id, &node_info));

	printf("Node type: %d\n", node_info.type);

	if (NULL != hr->sop_array) {
		free_array(hr->sop_array);
		hr->sop_array = NULL;
	}

	switch (node_info.type) {
	case HAPI_NODETYPE_SOP:
	{
		hr->sop_count = 1;
		hr->sop_array = malloc_array(sizeof(HAPI_NodeId), hr->sop_count, "houdini cooked SOPs");
		hr->sop_array[0] = hr->node_id;
		return true;
	}

	case HAPI_NODETYPE_OBJ:
	{
		H_CHECK(HAPI_ComposeChildNodeList(&hr->hsession, hr->node_id, HAPI_NODETYPE_SOP, HAPI_NODEFLAGS_DISPLAY, true, &hr->sop_count));

		hr->sop_array = malloc_array(sizeof(HAPI_NodeId), hr->sop_count, "houdini cooked SOPs");

		H_CHECK(HAPI_GetComposedChildNodeList(&hr->hsession, hr->node_id, hr->sop_array, hr->sop_count));

		printf("Asset has %d Display SOP(s).\n", hr->sop_count);
		return true;
	}

	default:
		printf("Houdini modifier for Blender only supports SOP and OBJ digital asset, but this asset has type %d.\n", node_info.type);
		return false;
	}
}

void hruntime_consolidate_geo_counts(HoudiniRuntime* hr, int* point_count_ptr, int* vertex_count_ptr, int* face_count_ptr) {
	for (int sid = 0; sid < hr->sop_count; ++sid) {
		HAPI_Result res;
		HAPI_GeoInfo geo_info;
		HAPI_NodeId node_id = hr->sop_array[sid];

		printf("Handling SOP #%d.\n", sid);

		H_CHECK_OR(HAPI_GetGeoInfo(&hr->hsession, node_id, &geo_info))
			continue;

		if (geo_info.partCount == 0) {
			H_CHECK_OR(HAPI_CookNode(&hr->hsession, node_id, NULL)) {}

			H_CHECK_OR(HAPI_GetGeoInfo(&hr->hsession, node_id, &geo_info))
				continue;
		}

		char name[256];
		HAPI_GetString(&hr->hsession, geo_info.nameSH, name, 256);
		printf("Geo '%s' has %d parts and has type %d.\n", name, geo_info.partCount, geo_info.type);

		for (int i = 0; i < geo_info.partCount; ++i) {
			HAPI_PartInfo part_info;
			HAPI_PartId part_id = (HAPI_PartId)i;
			H_CHECK_OR(HAPI_GetPartInfo(&hr->hsession, node_id, part_id, &part_info))
				continue;

			printf("Part #%d: type %d, %d points, %d vertices, %d faces.\n", i, part_info.type, part_info.pointCount, part_info.vertexCount, part_info.faceCount);

			if (part_info.type != HAPI_PARTTYPE_MESH) {
				printf("Ignoring non-mesh part.\n");
				continue;
			}

			*point_count_ptr += part_info.pointCount;
			*vertex_count_ptr += part_info.vertexCount;
			*face_count_ptr += part_info.faceCount;
		}
	}
}

void hruntime_fill_mesh(HoudiniRuntime* hr,
	Attribute point_data, int point_count,
	Attribute vertex_data, int vertex_count,
	Attribute face_data, int face_count) {
	int current_point = 0, current_vertex = 0, current_face = 0;
	size_t minimum_point_stride = point_data.componentCount * attributeTypeByteSize(point_data.type);
	assert(minimum_point_stride == 3 * sizeof(float));
	bool is_point_contiguous = point_data.stride == minimum_point_stride;

	size_t minimum_face_stride = face_data.componentCount * attributeTypeByteSize(face_data.type);
	assert(minimum_face_stride == 1 * sizeof(int));
	bool is_face_contiguous = face_data.stride == minimum_face_stride;

	for (int sid = 0; sid < hr->sop_count; ++sid) {
		HAPI_Result res;
		HAPI_GeoInfo geo_info;
		HAPI_NodeId node_id = hr->sop_array[sid];

		printf("Loading SOP #%d.\n", sid);

		H_CHECK_OR(HAPI_GetGeoInfo(&hr->hsession, node_id, &geo_info))
			continue;

		for (int i = 0; i < geo_info.partCount; ++i) {
			HAPI_PartInfo part_info;
			HAPI_PartId part_id = (HAPI_PartId)i;

			H_CHECK_OR(HAPI_GetPartInfo(&hr->hsession, node_id, part_id, &part_info))
				continue;

			printf("Part #%d: type %d, %d points, %d vertices, %d faces.\n", i, part_info.type, part_info.pointCount, part_info.vertexCount, part_info.faceCount);

			if (part_info.type != HAPI_PARTTYPE_MESH) {
				printf("Ignoring non-mesh part.\n");
				continue;
			}

			HAPI_AttributeInfo pos_attr_info;
			H_CHECK_OR(HAPI_GetAttributeInfo(&hr->hsession, node_id, part_id, "P", HAPI_ATTROWNER_POINT, &pos_attr_info))
				continue;

			// Get Point data
			float* part_point_data =
				is_point_contiguous
				? point_data.data + point_data.stride * current_point
				: malloc_array(minimum_point_stride, part_info.pointCount, "houdini point list");
			H_CHECK_OR(HAPI_GetAttributeFloatData(&hr->hsession, node_id, part_id, "P", &pos_attr_info, -1, part_point_data, 0, part_info.pointCount))
			{
				if (!is_point_contiguous) free_array(part_point_data);
				continue;
			}

			if (!is_point_contiguous)
			{
				// TODO: can be vectorized
				for (int i = 0; i < part_info.pointCount; ++i) {
					memcpy(
						point_data.data + point_data.stride * (current_point + i),
						part_point_data + minimum_point_stride * i,
						minimum_point_stride);
				}
				free_array(part_point_data);
			}

			// Get Vertex Data
			int* part_vertex_data = malloc_array(sizeof(int), part_info.vertexCount, "houdini vertex list");
			H_CHECK_OR(HAPI_GetVertexList(&hr->hsession, node_id, part_id, part_vertex_data, 0, part_info.vertexCount))
			{
				free_array(part_vertex_data);
				continue;
			}

			// TODO: can be vectorized
			for (int vid = 0; vid < part_info.vertexCount; ++vid) {
				int* v = (int*)(vertex_data.data + vertex_data.stride * (current_vertex + vid));
				*v = current_point + part_vertex_data[vid];
			}
			free_array(part_vertex_data);

			// Get face data
			int* part_face_data =
				is_face_contiguous
				? face_data.data + face_data.stride * current_face
				: malloc_array(minimum_face_stride, part_info.faceCount, "houdini face list");

			H_CHECK_OR(HAPI_GetFaceCounts(&hr->hsession, node_id, part_id, part_face_data, 0, part_info.faceCount))
			{
				if (!is_face_contiguous) free_array(part_face_data);
				continue;
			}

			if (!is_face_contiguous)
			{
				// TODO: can be vectorized
				for (int i = 0; i < part_info.faceCount; ++i) {
					memcpy(
						face_data.data + face_data.stride * (current_face + i),
						part_face_data + minimum_face_stride * i,
						minimum_face_stride);
				}
				free_array(part_face_data);
			}

			current_point += part_info.pointCount;
			current_vertex += part_info.vertexCount;
			current_face += part_info.faceCount;
		}
	}
}

/**
 * For each of points, vertices and faces, Houdini's HAPI expects contiguous
 * arrays while Attribute variables contain strided arrays. In general, we
 * need to reallocate them, but when possible (ie. when already contiguous)
 * we use the raw pointer to avoid extra memory allocation.
 *
 * TODO: cache contiguous_data arrays?
 * TODO: parallelize this strided memcpy?
 *
 * @param count is the number of elements in the attribute
 * If the return value must_free is set to true, returned data has been newly
 * allocated and must hence be freed by the calling code.
 */
static char* contiguousAttributeData(Attribute attr, int count, bool* must_free)
{
	int minimum_stride = attr.componentCount * attributeTypeByteSize(attr.type);
	bool is_contiguous = attr.stride == minimum_stride;
	if (is_contiguous)
	{
		*must_free = false;
		return attr.data;
	}
	else
	{
		*must_free = true;
		char* contiguous_data = malloc_array(sizeof(char), minimum_stride * count, "contiguous input point data");
		for (int i = 0; i < count; ++i) {
			memcpy(contiguous_data + minimum_stride * i, attr.data + attr.stride * i, minimum_stride);
		}
		return contiguous_data;
	}
}

bool hruntime_feed_input_data(HoudiniRuntime* hr,
	Attribute point_data, int point_count,
	Attribute vertex_data, int vertex_count,
	Attribute face_data, int face_count) {
	HAPI_Result res;

	if (hr->input_sop_id == -1) {
		return true;
	}

	HAPI_PartInfo part_info = HAPI_PartInfo_Create();
	part_info.pointCount = point_count;
	part_info.vertexCount = vertex_count;
	part_info.faceCount = face_count;
	part_info.isInstanced = false;
	H_CHECK(HAPI_SetPartInfo(&hr->hsession, hr->input_sop_id, 0, &part_info));

	HAPI_AttributeInfo attrib_info = HAPI_AttributeInfo_Create();
	attrib_info.exists = true;
	attrib_info.owner = HAPI_ATTROWNER_POINT;
	attrib_info.count = point_count;
	attrib_info.tupleSize = point_data.componentCount;
	attrib_info.storage = HAPI_STORAGETYPE_FLOAT;
	attrib_info.typeInfo = HAPI_ATTRIBUTE_TYPE_POINT;

	H_CHECK(HAPI_AddAttribute(&hr->hsession, hr->input_sop_id, 0, HAPI_ATTRIB_POSITION, &attrib_info));

	bool must_free;

	float* contiguous_point_data = (float*)contiguousAttributeData(point_data, point_count, &must_free);
	H_CHECK(HAPI_SetAttributeFloatData(&hr->hsession, hr->input_sop_id, 0, HAPI_ATTRIB_POSITION, &attrib_info, contiguous_point_data, 0, point_count));
	if (must_free) free_array(contiguous_point_data);

	int* contiguous_vertex_data = (int*)contiguousAttributeData(vertex_data, vertex_count, &must_free);
	H_CHECK(HAPI_SetVertexList(&hr->hsession, hr->input_sop_id, 0, contiguous_vertex_data, 0, vertex_count));
	if (must_free) free_array(contiguous_vertex_data);

	int* contiguous_face_data = (int*)contiguousAttributeData(face_data, face_count, &must_free);
	H_CHECK(HAPI_SetFaceCounts(&hr->hsession, hr->input_sop_id, 0, contiguous_face_data, 0, face_count));
	if (must_free) free_array(contiguous_face_data);

	H_CHECK(HAPI_CommitGeo(&hr->hsession, hr->input_sop_id));

	return true;
}


