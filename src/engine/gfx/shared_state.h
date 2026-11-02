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

// NOT ORIGINAL grouping - texture_splats.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (texture_splats_shared()).
struct TextureSplatsShared {
    VertexStack vertex_stack_{};
    float split_area_limit_{82944.0f};
};
TextureSplatsShared& texture_splats_shared();

} // namespace render

