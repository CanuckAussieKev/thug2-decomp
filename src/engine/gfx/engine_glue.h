#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <gsl/pointers>
#include <array>

namespace maths {
class Vec4;
}
namespace gfx {
class ViewCam;
}

// The engine's platform calls (render::CEngine's s_plat_* on PC, cdecl): the engine's own entry points at
// 0x0048BF10-0x0048C860 are jumps to these, and most of these pass straight on into the PC engine.
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
// 0x004B2280: the frame's BeginScene, unless the loading screen is up (0x00786D04).
void begin_frame_draw();
// 0x004B22B0: a jump to 0x004D3CA0 - the 2D clip rectangle (four values) set and turned on.
void set_screen_clip(const SkinScaling* settings);
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

// 0x004B24C0 / 0x004B24D0: jumps to 0x004D07B0 / 0x004D06A0 - the gamma ramp set from / read back as three
// factors.
void set_gamma(float red, float green, float blue);
void get_gamma(float* red, float* green, float* blue);
// 0x004B24E0: the front buffer saved as a JPEG: "<name>.jpg" when the pending screenshot's name is to be used as
// it is (0x00786E14), else the first "<name>NNN.jpg" with no file yet. The second argument isn't used.
void screen_shot(const char* name, int unused);
// 0x004B25F0: the device made (init_direct_3d), then the particle manager (0x006FA7C4), the weather (0x006FA7C8) and
// the screen effects (0x006FA7CC).
void start_engine();
// 0x004B2750: the frame ended and shown (unless the loading screen is up / rendering is skipped), the draw counts
// reset, a pending screenshot taken (0x00786D94), and the next frame's clear.
void finish_rendering();
// 0x004B28C0: a new PC scene (0x284 bytes; its mesh list at +0x280); `with_super_sectors` kept at +0x8C. The name and
// textures aren't used here.
gsl::owner<PcScene*> make_empty_scene(const char* name, TextureSet* textures, bool with_super_sectors);
// 0x004B2990: a scene's sectors let go (0x004C6200) and its mesh list deleted. Always true.
bool unload_scene(WorldScene* scene);
// 0x004B29C0 / 0x004B2A20 / 0x004B2A80 / 0x004B2AF0: a new PC model (0x50 bytes, zeroed), geom (0x64, zeroed),
// sprite (0x48) and textured quad (0x20) - null when out of memory.
gsl::owner<RenderModel*> spawn_model();
gsl::owner<Geom*> create_geom();
gsl::owner<BillboardQuad*> build_sprite_quad();
gsl::owner<TexturedQuad*> create_textured_quad();
// 0x004B2B60: a new render target (0x14 bytes) with a texture of its own made a width x height render target.
gsl::owner<void*> create_render_target(int width, int height, uint8_t depth, int unused);
// 0x004B2C30 / 0x004B2C50 / 0x004B2D10: a texture's shadow caster (the key: its +0x10, through one more pointer
// with its flag 1 of +0x08) added for `model`, aimed from the camera along its matrix's third row, and removed.
void add_texture_shadow_caster(const BitmapTexture* texture, void* model, WorldScene* unused);
void aim_texture_shadow_caster(const BitmapTexture* texture, gfx::ViewCam* camera);
void remove_texture_shadow_caster(const BitmapTexture* texture);

// The weather, the screen effects, the fog colour and the loading screen flag (engine_glue.cpp's record).
bool loading_screen_up();
void set_loading_screen_up(bool value);
WeatherSystem* weather();
ScreenEffects* screen_effects();
uint32_t fog_colour();
void set_fog_colour(uint32_t value);

} // namespace render
