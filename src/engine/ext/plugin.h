#pragma once

#include <cstdint>

#include "base/core/api.h"

namespace ext {
class Extension;
}

// NOT ORIGINAL - plugins: DLLs in the plugins folder next to the exe (the level editor:
// plugins\thug2-level-editor.dll) that add an extension (extension.h). A plugin links the exe's import library - the
// engine API, the functions marked THUG2_API (base/core/api.h) - and exports one C function, thug2_plugin_entry, that
// gives its PluginInfo. At start-up the exe loads every plugins\*.dll (load_plugins) and puts each plugin's extension
// at the place in the extension order its slot names (app/extensions.cpp: the order is every point's call order); a
// slot no plugin fills keeps what the exe has there without it (the editor's: its script commands answered "no
// editor").
namespace ext {

// The API's version: raised with every change a plugin sees (a THUG2_API function's signature, a class or struct a
// plugin uses, the extension points). A plugin built for another version is not loaded.
inline constexpr uint32_t kPluginApiVersion = 13;

// The build a plugin must match: the same compiler (C++ has no common ABI between compilers, nor between MSVC's debug
// and release libraries), the same pointer size.
enum class Compiler : uint8_t { kMsvc = 1, kClang = 2, kGcc = 3, kOther = 4 };
struct PluginBuild {
    Compiler compiler;
    uint32_t compiler_version; // _MSC_VER, or the major * 100 + minor of Clang / GCC
    uint32_t pointer_bytes;
    uint32_t debug; // 1: a Debug build (the debug C runtime and library checks)
};

// The build this file is compiled in (the exe's, or the plugin's).
inline constexpr PluginBuild kThisBuild = {
#if defined(_MSC_VER) && !defined(__clang__)
    .compiler = Compiler::kMsvc,
    .compiler_version = _MSC_VER,
#elif defined(__clang__)
    .compiler = Compiler::kClang,
    .compiler_version = (__clang_major__ * 100) + __clang_minor__,
#elif defined(__GNUC__)
    .compiler = Compiler::kGcc,
    .compiler_version = (__GNUC__ * 100) + __GNUC_MINOR__,
#else
    .compiler = Compiler::kOther,
    .compiler_version = 0,
#endif
    .pointer_bytes = sizeof(void*),
#ifdef _DEBUG
    .debug = 1,
#else
    .debug = 0,
#endif
};

// What thug2_plugin_entry gives: plain data, read by the exe before anything else of the plugin is called (the
// version and the build first, so a mismatched plugin is never called).
struct PluginInfo {
    uint32_t api_version; // kPluginApiVersion as the plugin was built
    PluginBuild build;    // kThisBuild as the plugin was built
    const char* name;     // for the run log ("level editor")
    const char* slot;     // its place in the extension order ("editor": app/extensions.cpp)
    Extension* extension;
};

// The plugin's entry point (thug2_plugin_entry, below), as the exe finds it by name.
using PluginEntry = const PluginInfo* (*)();
inline constexpr const char* kPluginEntryName = "thug2_plugin_entry";

// The exe's start-up, before the extensions are registered: every plugins\*.dll next to the exe loaded, its entry
// called and its info checked (one built for another API version or build is logged and left out). Once.
void load_plugins();
// The extension of the plugin loaded for `slot`, or none.
Extension* plugin_at(const char* slot);

} // namespace ext

// A plugin's entry point, the one function it exports by its plain name: its info (static: valid while it is loaded).
extern "C" THUG2_PLUGIN_EXPORT const ext::PluginInfo* thug2_plugin_entry();
