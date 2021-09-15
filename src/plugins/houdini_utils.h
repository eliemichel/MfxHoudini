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

#ifndef H_HOUDINI_UTILS
#define H_HOUDINI_UTILS

#include "HAPI/HAPI.h"
#include "util/plugin_support.h"

#define MAX_NUM_PLUGINS 10
#define MAX_BUNDLE_DIRECTORY 1024
#define MOD_HOUDINI_MAX_ASSET_NAME 1024
#define MOD_HOUDINI_MAX_PARAMETER_NAME 256

#define kOfxPropHoudiniNodeId "OfxPropHoudiniNodeId"

// A series of macros to automatically add debug info when calling either houdini of open mesh effect apis

#define MFX_CHECK(op) status = runtime->op; \
if (kOfxStatOK != status) {\
printf("Suite method call '" #op "' returned status %d (%s)\n", status, getOfxStateName(status)); \
}
#define MFX_CHECK2(op) status = op; \
if (kOfxStatOK != status) {\
printf("Suite method call '" #op "' returned status %d (%s)\n", status, getOfxStateName(status)); \
}

#define H_CHECK(op) res = op; \
if (HAPI_RESULT_SUCCESS != res) { \
	ERR("Houdini error during call '" #op "': %u (%s)\n", res, HAPI_ResultMessage(res)); \
	return false; \
}

#define H_CHECK_OR(op) res = op; \
if (HAPI_RESULT_SUCCESS != res) { \
	ERR("Houdini error during call '" #op "': %u (%s)\n", res, HAPI_ResultMessage(res)); \
} \
if (HAPI_RESULT_SUCCESS != res) \

#include <Windows.h>
static void houdini_output_debug_printf(const char* strOutputString, ...)
{
    char strBuffer[4096] = { 0 };
    va_list vlArgs;
    va_start(vlArgs, strOutputString);

    _vsnprintf_s(strBuffer, sizeof(strBuffer) - 1, sizeof(strBuffer) - 1, strOutputString, vlArgs);
    va_end(vlArgs);
    printf(strBuffer);
    OutputDebugStringA(strBuffer);
}

#include <time.h>

#define MFX_CLOCK_CONCAT_INDIRECT(x,y) x##y

#define MFX_CLOCK_CONCAT(x,y) MFX_CLOCK_CONCAT_INDIRECT(x,y)

#define MFX_CLOCK_BEGIN(action) clock_t MFX_CLOCK_CONCAT(before, action) = clock();

#define MFX_CLOCK_END(action) clock_t MFX_CLOCK_CONCAT(difference, action) = clock() - MFX_CLOCK_CONCAT(before, action);\
int MFX_CLOCK_CONCAT(msec, action) = MFX_CLOCK_CONCAT(difference, action) * 1000 / CLOCKS_PER_SEC;\
houdini_output_debug_printf("%s time cost %d ms\n", #action, MFX_CLOCK_CONCAT(msec, action));

 // Utils

inline int max(int a, int b) {
	return (a > b) ? a : b;
}

inline int min(int a, int b) {
	return (a < b) ? a : b;
}

/**
 * Convert houdini error code into human readable message
 */
const char* HAPI_ResultMessage(HAPI_Result res);

/**
 * Convert parameter types from houdini (HAPI_ParmType + size) to open mesh effect (const char *)
 */
const char* houdini_to_ofx_type(HAPI_ParmType houdini_type, int size);

size_t storageByteSize(HAPI_StorageType storage);

HAPI_StorageType attribute_type_to_houdini_storage(enum AttributeType type);

#endif // H_HOUDINI_UTILS
