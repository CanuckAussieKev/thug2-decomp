#pragma once

// NOT ORIGINAL - forward declarations of the script runtime's classes, for headers and sources that only name them
// by pointer or reference (include the real header where a member is used). Sits in the owning subsystem so that no
// file reaches into a higher layer for a `class Script;`.
namespace script {
class Array;
class Context;
class Script;
class Struct;
} // namespace script
namespace prefs {
class Preferences;
} // namespace prefs
