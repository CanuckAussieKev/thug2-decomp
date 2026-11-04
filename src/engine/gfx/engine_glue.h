#pragma once

#include <cstdint>
#include <span>

#include "base/maths/vec4.h"

namespace maths {
class Vec4;
}
namespace gfx {
class ViewCam;
}

// The engine's platform calls (render::CEngine's s_plat_* on PC, cdecl) that need no graphics device: the engine's own
// entry points at 0x0048BF10-0x0048C860 are jumps to these. The ones that draw or make device objects are the graphics
// backend's (pc_engine.h), reached through render::Device.
namespace render {
struct SkinScaling;

struct PcScene;
struct WorldScene;
class BitmapTexture;
class TextureSet;

class WeatherSystem;
class ScreenEffects;
class RenderModel;
class Geom;
class BillboardQuad;
class TexturedQuad;

// 0x004B2270: the MemViewToggle command's flag (0x00701588; nothing reads it).
void toggle_mem_view();
// 0x004B22C0: a debug shape: type 4 is a quad (four points, 0x004D8D70) in `colour`; others nothing.
// (What it reaches is occlusion.h's insert_occluder_poly - `colour` is the polygon's checksum; the two below are
// activate_occluder_poly and clear_occluder_polys, and sphere_visible is sphere_outside_view: 1 when the sphere is
// outside the view. The names here are kept for their callers.)
void add_debug_shape(int type, std::span<const maths::Vec4> points, uint32_t colour);
// 0x004B22F0 / 0x004B2300: jumps to 0x004D8C90 (the debug lines with this id get `flag` in their byte at +0)
// and 0x004D8CC0 (all debug lines cleared).
void flag_debug_lines(uint32_t id, bool flag);
void clear_debug_lines();
// 0x004B2310: a jump to 0x004DB570 - whether a sphere (a centre, a radius) is in view (1 / 0).
int sphere_visible(const maths::Vec4* centre, float radius);
// 0x004B2320: the platform's file extension, "Xbx".
const char* platform_suffix();

// The weather, the screen effects, the fog colour and the loading screen flag (engine_glue.cpp's record).
WeatherSystem* weather();
ScreenEffects* screen_effects();

} // namespace render
