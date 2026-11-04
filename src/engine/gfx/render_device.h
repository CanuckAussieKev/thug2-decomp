#pragma once

// NOT ORIGINAL - render::Device: what the engine side asks of the graphics backend. The backend is the original's PC
// platform layer (the engine's s_plat_* calls and what they reach, 0x004B2270-0x004E0A90, with the Direct3D files of
// our own); the engine side is everything above it - the engine's own entry points, the scenes, models, effects and
// lights as the engine sees them, the scripts, the game. Every call the engine side makes into the backend for a
// frame, a resource, a state block or a draw batch goes through the one installed device; app/ makes it and installs
// it at start-up (install_device), a run-time choice - Direct3D 9's (engine/gfx/d3d9_device.h) is the only one so far.
// The interface is shaped so that another backend (Vulkan) can stand in: resources are made explicitly and given
// back, states are set as the engine groups them (the fog, the gamma, the primitives' states), the frame is begun,
// drawn and finished as a whole, and a lost device is reset by the engine's main loop in steps.
//
// The grain is coarse: a call per frame, per resource made or released, per state block, per draw batch (a flash, a
// shatter, a splat set) - a few dozen a frame. Never per vertex or per object: what the engine does per vertex or
// per object (a primitive's vertices, a sphere's visibility) stays a direct call (render_device.cpp's notes).
//
// The methods keep the names and arguments of the functions they stand for (the backend's own, in the files the
// comments name), and each D3D9 method is that function: the same calls to Direct3D, in the same order.
//
// Not installed, a call dereferences null: the device is installed before any of the original's code runs.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gsl/pointers>

#include "base/core/checksums.h"
#include "base/maths/rect.h"

namespace gfx {
class ViewCam;
}
namespace maths {
class BBox;
class Vec4;
}
namespace sys::screenshot {
struct TickCycle;
}

namespace render {

struct WorldScene;
struct EmitterInstance;
class TextureSet;
struct VertexArena;
class PcMesh;
class RenderModel;
class Geom;
class BillboardQuad;
class TexturedQuad;
class ModelLights;
class PcViewport;
class BitmapTexture;
class PcGeom;
class PcShatter;
class HitMesh;
struct SkinScaling;
struct Flash;
struct TextureSplats;
struct SplatParams;
class Typeface;

// The primitives' depth test (set_primitive_depth_test): the state's index and its value.
struct DepthTest {
    int state{};
    int value{};
};

class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    // --- the frame (engine_glue.h, draw_frame.h) ---
    // The device made, then the backend's particle manager, weather and screen effects (start_engine).
    virtual void start_engine() = 0;
    // The frame's scene begun, unless the loading screen is up (begin_frame_draw).
    virtual void begin_frame_draw() = 0;
    // The frame's views drawn - every viewport's world, the 2D, the screen effects (draw_world).
    virtual void draw_world() = 0;
    // The frame ended and shown, the draw counts reset, a pending screenshot taken, the next frame's clear
    // (finish_rendering).
    virtual void finish_rendering() = 0;

    // --- a lost device (the main loop's reset_if_lost, in its steps) ---
    // Whether the device can be reset now (not while it is still lost).
    virtual bool can_reset() = 0;
    // The objects a reset drops (render targets' textures, the dynamic buffers, the screen copies), and made again.
    virtual void release_device_objects() = 0;
    virtual void restore_device_objects() = 0;
    // The device reset with its present parameters; false if that failed (it is tried again next frame).
    virtual bool reset() = 0;

    // --- resources: made (null without memory) and given back; as the engine's own types (the backend's objects
    // derive from them) ---
    // Scenes (engine_glue.h, scene_load.h): an empty one, one loaded from memory or a file, one's sectors let go.
    virtual gsl::owner<WorldScene*> make_empty_scene(const char* name, TextureSet* textures, bool with_super_sectors) = 0;
    virtual gsl::owner<WorldScene*> load_scene_from_memory(const uint8_t* data, size_t size, const TextureSet* textures,
                                                           bool with_super_sectors, bool flag_8d, bool mesh_flag) = 0;
    virtual gsl::owner<WorldScene*> load_scene_from_file(const char* name, const TextureSet* textures,
                                                         bool with_super_sectors, bool flag_8d, bool mesh_flag,
                                                         VertexArena* arena) = 0;
    virtual bool unload_scene(WorldScene* scene) = 0;
    // Meshes from a scene file or from memory (scene_load.h).
    virtual gsl::owner<PcMesh*> load_mesh_file(const char* name, TextureSet* textures) = 0;
    virtual gsl::owner<PcMesh*> load_mesh_from_memory(uint32_t checksum, const uint8_t* data, size_t size,
                                                      const void* skin, size_t skin_size, TextureSet* textures) = 0;
    // A model, a geom, a sprite, a textured quad, a render target with its texture (engine_glue.h), a model's lights
    // (pc_lights.h).
    virtual gsl::owner<RenderModel*> spawn_model() = 0;
    virtual gsl::owner<Geom*> create_geom() = 0;
    virtual gsl::owner<BillboardQuad*> build_sprite_quad() = 0;
    virtual gsl::owner<TexturedQuad*> create_textured_quad() = 0;
    virtual gsl::owner<void*> create_render_target(maths::PixelSize size, uint8_t depth, int unused) = 0;
    virtual gsl::owner<ModelLights*> spawn_model_lights() = 0;
    // Texture dictionaries: from a file, from memory, an empty one (pc_texture.h).
    virtual gsl::owner<TextureSet*> load_tex_dict_file(const char* name) = 0;
    virtual gsl::owner<TextureSet*> load_tex_dict_memory(uint32_t checksum, const uint8_t* data, size_t size) = 0;
    virtual gsl::owner<TextureSet*> create_tex_dict(uint32_t checksum) = 0;
    // A viewport (pc_viewport.h), a flexible particle system (pc_flexible_particles.h).
    virtual gsl::owner<PcViewport*> create_viewport(const std::array<float, 4>* rect, gfx::ViewCam* camera) = 0;
    virtual gsl::owner<EmitterInstance*> create_flexible_particles(checksums::Name system_name, checksums::Name kind,
                                                                   int particle_cap, int max_streams, uint32_t texture,
                                                                   uint32_t blend_mode, int fixed_alpha, int segments,
                                                                   float unknown, int history, const char* model) = 0;
    // Fonts and the texts' pool (pc_font.h), the 2D windows' pool (pc_billboards.h): made once at start-up.
    virtual gsl::owner<Typeface*> create_pc_font(const char* name) = 0;
    virtual void unload_pc_font(Typeface* font) = 0;
    virtual void allocate_text_pool(uint32_t count) = 0;
    virtual void create_window_pool() = 0;

    // --- state blocks: set as the engine groups them ---
    // The fog: on or off, where it starts, its colour (pc_fog.h).
    virtual void toggle_fog(bool on) = 0;
    virtual void set_fog_distance(float start) = 0;
    virtual void set_fog_color(uint32_t rgba) = 0;
    // The gamma ramp from / as three factors (engine_glue.h).
    virtual void set_gamma(float red, float green, float blue) = 0;
    virtual std::array<float, 3> get_gamma() = 0;
    // The 2D clip rectangle set and turned on (engine_glue.h).
    virtual void set_screen_clip(const SkinScaling* settings) = 0;
    // The primitives' states around the imposters, and their depth test (primitive_states.h, pc_primitive.h).
    virtual void set_primitive_states() = 0;
    virtual void reset_primitive_states() = 0;
    virtual void set_primitive_depth_test(DepthTest depth_test) = 0;
    // The world's lights as the light manager sets them: the ambient colour, a light's colour and direction - each
    // sent on, and read back - and all of them again (pc_lights.h).
    virtual bool update_ambient_light() = 0;
    virtual uint32_t* fetch_ambient_hue(uint32_t* out) = 0;
    virtual bool update_light_color(int light) = 0;
    virtual uint32_t* fetch_light_tint(uint32_t* out, int light) = 0;
    virtual bool update_light_direction(int light) = 0;
    virtual maths::Vec4* get_light_direction(int light) = 0;
    virtual void update_all_lights() = 0;

    // --- effects: made, stepped and drawn a batch (a flash, a shatter, a splat set) at a time ---
    // A flash over its viewport (flash.h).
    virtual void draw_flash(const Flash* flash) = 0;
    // A geom's glass shattered, a shatter stepped and drawn, all of them gone (shatter.h).
    virtual void shatter_geom(PcGeom* geom) = 0;
    virtual void update_shatter(PcShatter* shatter, float step) = 0;
    virtual void draw_shatter(PcShatter* shatter) = 0;
    virtual void kill_all_shatters() = 0;
    // A texture splat set made on the sectors and objects hit, lit, and splats removed (texture_splats.h).
    virtual TextureSplats* create_texture_splat(void** sectors, HitMesh* const* objects, SplatParams* params) = 0;
    virtual void light_texture_splats(TextureSplats* splats) = 0;
    virtual void kill_all_texture_splats() = 0;
    virtual void kill_texture_splats_in_box(const maths::BBox* box) = 0;
    // A texture's shadow caster added for a model, aimed from a camera, removed (engine_glue.h); the model whose
    // shadow a caster's model also casts (shadow_base.h).
    virtual void add_texture_shadow_caster(const BitmapTexture* texture, void* model, WorldScene* unused) = 0;
    virtual void aim_texture_shadow_caster(const BitmapTexture* texture, gfx::ViewCam* camera) = 0;
    virtual void remove_texture_shadow_caster(const BitmapTexture* texture) = 0;
    virtual void set_shadow_companion(void* (*companion)(void* caster_model)) = 0;
    // The sun's occlusion query: the pixels drawn, when its result is in (pc_viewport.h).
    virtual uint32_t occlusion_pixels() = 0;

    // --- what is shown: the back buffer, the screens drawn and presented outside the frame ---
    // The front buffer saved as a JPEG (engine_glue.h).
    virtual void screen_shot(const char* name, int unused) = 0;
    // The frame as it will be shown, copied (the screenshot key's: screenshot_capture.h).
    virtual bool capture_back_buffer(sys::screenshot::TickCycle* out, std::string* error) = 0;
    // The back buffer's width the device was made with; whether the screen copies were made (device.h).
    virtual uint32_t presented_width() = 0;
    virtual bool screen_copies_made() = 0;
    // The disc read error screen (device.h).
    [[noreturn]] virtual void show_disc_error() = 0;
    // The loading screen: its picture shown (drawn and presented at once), taken down, its second picture deleted;
    // the bar's colours worked out, the bar filled over `seconds` (loading_screen.h, loading_bar.h).
    virtual void show_loading_screen(const char* name, bool keep, bool black) = 0;
    virtual void hide_loading_screen() = 0;
    virtual void free_loading_screen_sprite() = 0;
    virtual void update_loading_bar_colors() = 0;
    virtual void show_progress_bar(float seconds) = 0;

protected:
    ~Device() = default;
};

// The installed device (app/ installs it at start-up, once).
Device& device();
void install_device(Device& device);

} // namespace render
