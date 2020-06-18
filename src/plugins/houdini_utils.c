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

#include "ofxCore.h"
#include "ofxMeshEffect.h"

#include "houdini_utils.h"

const char* HAPI_ResultMessage(HAPI_Result res) {
	static const char* messages[] = {
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

const char* houdini_to_ofx_type(HAPI_ParmType houdini_type, int size) {
	switch (houdini_type) {
	case HAPI_PARMTYPE_FLOAT:
		switch (size) {
		case 1:
			return kOfxParamTypeDouble;
		case 2:
			return kOfxParamTypeDouble2D;
		case 3:
			return kOfxParamTypeDouble3D;
		default:
			return NULL;
		}
	case HAPI_PARMTYPE_INT:
		switch (size) {
		case 1:
			return kOfxParamTypeInteger;
		case 2:
			return kOfxParamTypeInteger2D;
		case 3:
			return kOfxParamTypeInteger3D;
		default:
			return NULL;
		}
	case HAPI_PARMTYPE_COLOR:
		switch (size) {
		case 3:
			return kOfxParamTypeRGB;
		case 4:
			return kOfxParamTypeRGBA;
		default:
			return NULL;
		}
	case HAPI_PARMTYPE_STRING:
		return kOfxParamTypeString;
	default:
		return NULL;
	}
}

size_t storageByteSize(HAPI_StorageType storage)
{
	switch (storage)
	{
	case HAPI_STORAGETYPE_INT:
		return sizeof(int);
	case HAPI_STORAGETYPE_INT64:
		return 2 * sizeof(int);
	case HAPI_STORAGETYPE_FLOAT:
		return sizeof(float);
	case HAPI_STORAGETYPE_FLOAT64:
		return sizeof(double);
	default:
		return 0;
	}
}

HAPI_StorageType attribute_type_to_houdini_storage(enum AttributeType type)
{
	switch (type)
	{
	case MFX_UBYTE_ATTR:
		return HAPI_STORAGETYPE_FLOAT; // requires conversion, ubytes are not supported by houdini
	case MFX_INT_ATTR:
		return HAPI_STORAGETYPE_INT;
	case MFX_FLOAT_ATTR:
		return HAPI_STORAGETYPE_FLOAT;
	default:
		return HAPI_STORAGETYPE_INVALID;
	}
}
