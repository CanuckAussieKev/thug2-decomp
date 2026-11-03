#pragma once

#include <array>
#include <cstdint>

#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_shadows.h"
#include "engine/gfx/shatter.h"
#include "engine/gfx/spawned_polys.h"
#include "engine/gfx/texture_splats.h"
#include "engine/lists/object_set.h"
#include "engine/maths/vec4.h"

// NOT ORIGINAL - the screen effects' state: glass shatters, texture splats, spawned polygons, shadow edges, the screen
// copy, the bloom and the blur, the screenshot asked for (the original's globals).
namespace render {

// The effects' state (engine_effects.cpp, shatter.cpp, texture_splats.cpp, screen_fx.cpp, pc_scene.cpp,
// engine_shadow_edges.cpp and engine_glue.cpp use it). Made in place on first use: the object sets have destructors
// that never run.
class EffectsState {
public:
    maths::Vec4& glass_burst_velocity() { return glass_burst_velocity_; }
    [[nodiscard]] float shatter_speed() const { return shatter_speed_; }
    void set_shatter_speed(float value) { shatter_speed_ = value; }
    lists::ObjectSet<TextureSplats>& splat_sets() { return splat_sets_; }
    lists::ObjectSet<SpawnedPoly>& spawned_polys() { return spawned_polys_; }
    [[nodiscard]] float shatter_velocity_scale() const { return shatter_velocity_scale_; }
    void set_shatter_velocity_scale(float value) { shatter_velocity_scale_ = value; }
    lists::ObjectSet<PcShatter>& shatters() { return shatters_; }
    VertexStack& vertex_stack() { return vertex_stack_; }
    [[nodiscard]] float split_area_limit() const { return split_area_limit_; }
    void set_split_area_limit(float value) { split_area_limit_ = value; }
    std::array<ShadowVolume*, 16>& shadow_volumes() { return shadow_volumes_; }
    lists::ObjectSet<ShadowEdges>& shadow_edge_table() { return shadow_edge_table_; }
    bool& screen_copy_in_use() { return screen_copy_in_use_; }
    [[nodiscard]] uint32_t bloom_on() const { return bloom_on_; }
    void set_bloom_on(uint32_t value) { bloom_on_ = value; }
    [[nodiscard]] uint32_t bloom_alpha_first() const { return bloom_alpha_first_; }
    void set_bloom_alpha_first(uint32_t value) { bloom_alpha_first_ = value; }
    [[nodiscard]] uint32_t bloom_alpha_second() const { return bloom_alpha_second_; }
    void set_bloom_alpha_second(uint32_t value) { bloom_alpha_second_ = value; }
    [[nodiscard]] uint32_t bloom_cut_off() const { return bloom_cut_off_; }
    void set_bloom_cut_off(uint32_t value) { bloom_cut_off_ = value; }
    uint32_t& screen_blur_frames() { return screen_blur_frames_; }
    auto& screen_blur() { return screen_blur_; }
    bool& screen_shot_name_exact() { return screen_shot_name_exact_; }
    std::array<char, 128>& pending_screen_shot() { return pending_screen_shot_; }

private:
    // Glass bursts, texture splat sets and spawned polygons (engine_effects.cpp).
    maths::Vec4 glass_burst_velocity_{};
    float shatter_speed_{};
    lists::ObjectSet<TextureSplats> splat_sets_{};
    lists::ObjectSet<SpawnedPoly> spawned_polys_{};
    float shatter_velocity_scale_{1.0f};

    // The glass shatters (shatter.cpp).
    lists::ObjectSet<PcShatter> shatters_{};

    // The texture splats' vertices (texture_splats.cpp).
    VertexStack vertex_stack_{};
    float split_area_limit_{82944.0f};

    // The shadow volumes and edges (engine_shadow_edges.cpp).
    std::array<ShadowVolume*, 16> shadow_volumes_{};
    lists::ObjectSet<ShadowEdges> shadow_edge_table_{};

    // The screen copy (screen_fx.cpp).
    bool screen_copy_in_use_{};

    // The bloom (pc_scene.cpp).
    uint32_t bloom_on_{};
    uint32_t bloom_alpha_first_{0x10};
    uint32_t bloom_alpha_second_{0x10};
    uint32_t bloom_cut_off_{0x80808080};

    // The screen blur and the screenshot asked for (engine_glue.cpp).
    uint32_t screen_blur_frames_{};
    uint32_t screen_blur_{};
    bool screen_shot_name_exact_{};
    std::array<char, 128> pending_screen_shot_{}; // the screenshot asked for by name
};

// The effects' state (made on first use, never destroyed).
EffectsState& effects_state();

} // namespace render
