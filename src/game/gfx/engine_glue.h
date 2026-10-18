#pragma once

#include <cstdint>

namespace Mth {
class Vector;
}

// The engine's platform calls (Nx::CEngine's s_plat_* on PC, cdecl): the engine's own entry points at
// 0x0048BF10-0x0048C860 are jumps to these, and most of these pass straight on into the PC engine.
namespace Nx {

// 0x004B2270: the MemViewToggle command's flag (0x00701588; nothing reads it).
void __cdecl ToggleMemView();
// 0x004B2280: the frame's BeginScene, unless the device is lost.
void __cdecl StartRender();
// 0x004B22A0: an engine object deleted through its virtual destructor (none: nothing).
void __cdecl DeleteObject(void* object);
// 0x004B22B0: a jump to 0x004D3CA0 - the 2D clip rectangle (four values) set and turned on.
void __cdecl SetScreenClip(const void* rect);
// 0x004B22C0: a debug shape: type 4 is a quad (four points, 0x004D8D70) in `colour`; others nothing.
void __cdecl AddDebugShape(int type, const Mth::Vector* points, uint32_t colour);
// 0x004B22F0 / 0x004B2300: jumps to 0x004D8C90 (the debug lines with this id get `flag` in their byte at +0)
// and 0x004D8CC0 (all debug lines cleared).
void __cdecl FlagDebugLines(uint32_t id, bool flag);
void __cdecl ClearDebugLines();
// 0x004B2310: a jump to 0x004DB570 - whether a sphere (a centre, a radius) is in view (1 / 0).
int __cdecl SphereVisible(const Mth::Vector* centre, float radius);
// 0x004B2320: the platform's file extension, "Xbx".
const char* __cdecl PlatformExtension();

} // namespace Nx
