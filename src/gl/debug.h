#pragma once

// GL's validation story: a KHR_debug callback the *driver* calls when something is wrong.
// (VK equivalent: validation layers + VK_EXT_debug_utils messenger. GL checks are built into the driver
// and far less thorough; VK's layers are separate code you switch on.)

namespace glint::gl {

// Logs GL_VENDOR / GL_RENDERER / GL_VERSION / GLSL version.
void logContextInfo();

// Installs the KHR_debug callback. Needs a debug context (WindowDesc::glDebugContext) to see everything.
void enableDebugOutput();

// Messages of severity >= medium seen so far. "Validation clean" for a GL technique means this stays 0.
int debugIssueCount();

// Name shown in the device string of the stats panel ("AMD Radeon Graphics (radeonsi, renoir, ...)").
const char* rendererName();

}  // namespace glint::gl
