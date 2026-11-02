#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include <memory>
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/font.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine_shadows.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/loading_bar.h"
#include "engine/gfx/loading_screen.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/screen_fx.h"
#include "engine/gfx/shatter.h"
#include "engine/gfx/text.h"
#include "engine/gfx/texture_splats.h"
#include "engine/gfx/window2d.h"

namespace render {

// NOT ORIGINAL grouping - engine_effects.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (engine_effects_shared()).
struct EngineEffectsShared {
    maths::Vec4 glass_burst_velocity_{};
    float shatter_speed_{};
    lists::ObjectSet<TextureSplats> splat_sets_{};
    lists::ObjectSet<SpawnedPoly> spawned_polys_{};
    float shatter_velocity_scale_{1.0f};
};
EngineEffectsShared& engine_effects_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - engine_font.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (engine_font_shared()).
struct EngineFontShared {
    std::array<char, 32> glyph_alias_table_{};
    std::array<FontSlot, 16> font_slots_{};
};
EngineFontShared& engine_font_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - engine_glue.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (engine_glue_shared()).
struct EngineGlueShared {
    uint32_t screen_blur_frames_{};
    uint32_t screen_blur_{};
    bool screen_shot_name_exact_{};
    std::array<char, 128> pending_screen_shot_{}; // the screenshot asked for by name
};
EngineGlueShared& engine_glue_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - engine_model_lights.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (engine_model_lights_shared()).
struct EngineModelLightsShared {
    uint32_t light_count_{};
};
EngineModelLightsShared& engine_model_lights_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - engine_scene_list.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (engine_scene_list_shared()).
struct EngineSceneListShared {
    std::array<WorldScene*, 16> collision_scenes_{};
};
EngineSceneListShared& engine_scene_list_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - engine_shadow_edges.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (engine_shadow_edges_shared()).
struct EngineShadowEdgesShared {
    std::array<ShadowVolume*, 16> shadow_volumes_{};
    lists::ObjectSet<ShadowEdges> shadow_edge_table_{};
};
EngineShadowEdgesShared& engine_shadow_edges_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - engine_text.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (engine_text_shared()).
struct EngineTextShared {
    GlyphString* free_texts_{};
};
EngineTextShared& engine_text_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - font.cpp's state shared with the folder's other files (the original's globals). Made on first
// use, never destroyed (font_shared()).
struct FontShared {
    lists::KeyMap font_table_{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(4) at start-up)
};
FontShared& font_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - light_manager.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (light_manager_shared()).
struct LightManagerShared {
    std::span<float> intensities_{};
    uint32_t model_light_count_{};
    float ambient_intensity_{1.0f};
    LightingState active_lighting_{.ambient = 0, .directions = {}, .colours = {}, .ambient_low_ = 0.5f, .ambient_high_ = 0.0f, .light_low_ = {}, .light_high_ = {}};
};
LightManagerShared& light_manager_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - loading_bar.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (loading_bar_shared()).
struct LoadingBarShared {
    int gradient_width_{};
    void* screen_mutex_{}; // the loading screen's lock (a HANDLE), held while it is drawn
    float tick_seconds_{};
    float elapsed_seconds_{};
    float total_seconds_{};
    std::array<int, 2> bar_position_{{258, 400}};
    int progress_width_{140};
    int progress_height_{8};
    std::array<int, 2> bar_border_{{5, 5}};
    std::array<uint8_t, 4> progress_from_color_{{0x00, 0x4C, 0x81, 0x80}};
    std::array<uint8_t, 4> progress_to_color_{{0xAC, 0xD3, 0x73, 0x80}};
    std::array<uint8_t, 4> progress_edge_color_{{0x28, 0x28, 0x28, 0x80}};
};
LoadingBarShared& loading_bar_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - loading_screen.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (loading_screen_shared()).
struct LoadingScreenShared {
    std::unique_ptr<render::BitmapTexture> loading_screen_picture_{};
};
LoadingScreenShared& loading_screen_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - occlusion.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (occlusion_shared()).
struct OcclusionShared {
    float last_sphere_distance_{};
};
OcclusionShared& occlusion_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - pc_billboards.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (pc_billboards_shared()).
struct PcBillboardsShared {
    uint8_t billboard_flag_{};
};
PcBillboardsShared& pc_billboards_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - pc_buffers.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (pc_buffers_shared()).
struct PcBuffersShared {
    uint8_t skin_scaling_on_{};
    SkinScaling skin_settings_{}; // (+ a fifth word, only ever cleared)
};
PcBuffersShared& pc_buffers_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - pc_instance.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (pc_instance_shared()).
struct PcInstanceShared {
    Singleton* current_instance_{};
};
PcInstanceShared& pc_instance_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - pc_lights.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (pc_lights_shared()).
struct PcLightsShared {
    std::array<float, 4 + 8 * (2 + 32)> light_constants_{};
};
PcLightsShared& pc_lights_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - pc_mesh_load.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (pc_mesh_load_shared()).
struct PcMeshLoadShared {
    std::array<uint32_t, 6> vertex_shaders_{};
};
PcMeshLoadShared& pc_mesh_load_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - pc_scene.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (pc_scene_shared()).
struct PcSceneShared {
    uint32_t bloom_on_{};
    uint32_t bloom_alpha_first_{0x10};
    uint32_t bloom_alpha_second_{0x10};
    uint32_t bloom_cut_off_{0x80808080};
};
PcSceneShared& pc_scene_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - screen_fx.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (screen_fx_shared()).
struct ScreenFxShared {
    bool screen_copy_in_use_{};
};
ScreenFxShared& screen_fx_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - shatter.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (shatter_shared()).
struct ShatterShared {
    lists::ObjectSet<PcShatter> shatters_{};
};
ShatterShared& shatter_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - text.cpp's state shared with the folder's other files (the original's globals). Made on first
// use, never destroyed (text_shared()).
struct TextShared {
    TypefaceData* glyph_typeface_{};
};
TextShared& text_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - texture_splats.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (texture_splats_shared()).
struct TextureSplatsShared {
    VertexStack vertex_stack_{};
    float split_area_limit_{82944.0f};
};
TextureSplatsShared& texture_splats_shared();

} // namespace render

namespace render {

// NOT ORIGINAL grouping - window2d.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (window2d_shared()).
struct Window2dShared {
    ScreenWindow* free_windows_{};
};
Window2dShared& window2d_shared();

} // namespace render
