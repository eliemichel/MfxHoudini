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

/** \file
 * \ingroup openmesheffect
 *
 * Plugin Supports - helpers common to all plugins
 *
 */

#ifndef __MFX_PLUGIN_SUPPORT_H__
#define __MFX_PLUGIN_SUPPORT_H__

#include "ofxCore.h"
#include "ofxMessage.h"
#include "ofxMeshEffect.h"

typedef struct PluginRuntime {
    OfxPlugin plugin;
    int pluginIndex;
    OfxHost *host;
    const OfxPropertySuiteV1 *propertySuite;
    const OfxParameterSuiteV1 *parameterSuite;
    const OfxMeshEffectSuiteV1 *meshEffectSuite;
    const OfxMessageSuiteV2* messageSuite;
    void* userData;
} PluginRuntime;

enum AttributeType {
  MFX_UNKNOWN_ATTR = -1,
  MFX_UBYTE_ATTR,
  MFX_INT_ATTR,
  MFX_FLOAT_ATTR,
};

typedef struct Attribute {
  enum AttributeType type;
  int stride;
  int componentCount;
  char *data;
} Attribute;

/**
 * Expect that host has been set and that we are in the kOfxPluginLoad action
 */
void loadPluginRuntimeSuites(PluginRuntime* runtime);

/**
 * Convert a type string from MeshEffect API to its local enum counterpart
 */
enum AttributeType mfxAttrAsEnum(const char *attr_type);

/**
 * Return the byte size of an attribute type
 */
size_t attributeTypeByteSize(enum AttributeType type);

/**
 * Get attribute info from low level open mesh effect API and store it in a struct Attribute
 */
OfxStatus getAttribute(PluginRuntime *runtime, OfxMeshHandle mesh, const char *attachment, const char *name, Attribute *attr);

// Sugar for getAttribute()
OfxStatus getPointAttribute(PluginRuntime* runtime, OfxMeshHandle mesh, const char *name, Attribute *attr);
OfxStatus getVertexAttribute(PluginRuntime* runtime, OfxMeshHandle mesh, const char *name, Attribute *attr);
OfxStatus getFaceAttribute(PluginRuntime* runtime, OfxMeshHandle mesh, const char *name, Attribute *attr);

/**
 * Copy attribute and try to cast. If number of component is different, copy the common components
 * only.
 */
OfxStatus copyAttribute(Attribute *destination, const Attribute *source, int start, int count);

#endif // __MFX_PLUGIN_SUPPORT_H__
