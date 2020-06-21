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

#ifndef H_HRUNTIME
#define H_HRUNTIME

#include "util/plugin_support.h" // for Attribute

#include "HAPI/HAPI.h"

#include <stdbool.h>

typedef struct HoudiniRuntime {
	HAPI_Session hsession;
	HAPI_AssetLibraryId library;
	HAPI_NodeId node_id;
	HAPI_NodeId input_node_id;
	HAPI_NodeId input_sop_id;
	HAPI_StringHandle* asset_names_array;
	char current_library_path[1024];
	int current_asset_index;
	int asset_count;

	int parm_count;
	HAPI_ParmInfo* parm_infos_array;
	int sop_count;
	HAPI_NodeId* sop_array;
	char* error_message;
} HoudiniRuntime;


void hruntime_set_error(HoudiniRuntime* hr, const char* fmt, ...);

bool hruntime_init(HoudiniRuntime* hr);

void hruntime_free(HoudiniRuntime* hr);

void hruntime_set_library(HoudiniRuntime* hr, const char* new_library_path);

/**
 * /pre hruntime_set_library_path has been called
 */
void hruntime_create_node(HoudiniRuntime* hr);

void hruntime_destroy_node(HoudiniRuntime* hr);

/**
 * /pre hruntime_create_node has been called
 */
void hruntime_fetch_parameters(HoudiniRuntime* hr);

/**
 * /pre hruntime_fetch_parameters has been called
 */
void hruntime_get_parameter_name(HoudiniRuntime* hr, int parm_index, char* name);

void hruntime_set_float_parm(HoudiniRuntime* hr, int parm_index, const float* values, int length);

void hruntime_set_int_parm(HoudiniRuntime* hr, int parm_index, const int* values, int length);

bool hruntime_cook_asset(HoudiniRuntime* hr);

bool hruntime_fetch_sops(HoudiniRuntime* hr);

void hruntime_consolidate_geo_counts(
    HoudiniRuntime* hr,
    int* point_count_ptr,
    int* vertex_count_ptr,
    int* face_count_ptr);

bool hruntime_has_vertex_attribute(HoudiniRuntime* hr, const char* attr_name);

void hruntime_fill_mesh(
    HoudiniRuntime* hr,
    Attribute point_data, int point_count,
    Attribute vertex_data, int vertex_count,
    Attribute face_data, int face_count);

void hruntime_fill_vertex_attribute(
    HoudiniRuntime* hr,
    Attribute uv_data,
    const char* attr_name);

bool hruntime_feed_input_data(
    HoudiniRuntime* hr,
    Attribute point_data, int point_count,
    Attribute vertex_data, int vertex_count,
    Attribute face_data, int face_count);

bool hruntime_feed_vertex_attribute(
    HoudiniRuntime* hr,
    const char *attr_name,
    Attribute attr_data, int vertex_count);

/**
 * /post hruntime_feed_input_data will not longer be called, nor hruntime_feed_vertex_attribute
 */
bool hruntime_commit_geo(HoudiniRuntime* hr);

/**
 * If the returned message is not null, caller must free it itself
 */
char* hruntime_get_cook_error(HoudiniRuntime* hr);

#define ERR(...) hruntime_set_error(hr, __VA_ARGS__)

#endif // H_HRUNTIME
