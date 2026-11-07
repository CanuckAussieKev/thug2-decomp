#pragma once

// NOT ORIGINAL - the engine API: the functions of the exe that a plugin (a DLL in plugins\ next to the exe: the level
// editor - engine/ext/plugin.h) calls. Each is declared THUG2_API: exported by thug2-decomp.exe (its import library,
// thug2-decomp.lib, is what a plugin links), imported by a plugin. A plugin is compiled with THUG2_PLUGIN defined;
// everything else is the exe's. tools/plugin_api.ts checks that the exe exports exactly what the plugins import.
// (A mark on each declaration, not a .def file of one compiler's decorated names: GCC / MinGW reads it the same way.)

#ifdef _WIN32

#ifdef THUG2_PLUGIN
#define THUG2_API __declspec(dllimport)
#else
#define THUG2_API __declspec(dllexport)
#endif
// A plugin's own export: its entry point (thug2_plugin_entry).
#define THUG2_PLUGIN_EXPORT __declspec(dllexport)

#else

#define THUG2_API __attribute__((visibility("default")))
#define THUG2_PLUGIN_EXPORT __attribute__((visibility("default")))

#endif
