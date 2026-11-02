#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/gfx/viewport.h"

// NOT ORIGINAL - the views' state: the viewports and the split-screen mode, the occlusion requests and query slots, the
// field of view, the 2D-in-3D scale, the viewport the particles are drawn for, the graphics options that shape the view
// (fog, distance clipping, shadows) (the original's globals).
namespace render {

// The views' state (viewport.cpp, pc_viewport.cpp, screen.cpp, settings.cpp, occlusion.cpp, pc_billboards.cpp and
// flexible_particle_manager.cpp use it). Trivially destructible: the accessor is a constant-initialised static, no
// guard.
class ViewSetup {
public:
    std::array<OcclusionRequest, 4>& occlusion_requests() { return occlusion_requests_; }
    [[nodiscard]] int screen_mode_setting() const { return screen_mode_; }
    void set_screen_mode(int value) { screen_mode_ = value; }
    std::array<ViewRegion*, 10>& viewports() { return viewports_; }
    [[nodiscard]] std::span<const int> mode_map() const { return mode_map_; }
    void set_mode_map(std::span<const int> value) { mode_map_ = value; }
    std::array<uint8_t, 1024>& occlusion_slots() { return occlusion_slots_; }
    [[nodiscard]] float scale_2d_in_3d_numerator() const { return scale_2d_in_3d_numerator_; }
    void set_scale_2d_in_3d_numerator(float value) { scale_2d_in_3d_numerator_ = value; }
    [[nodiscard]] float scale_2d_in_3d_max() const { return scale_2d_in_3d_max_; }
    void set_scale_2d_in_3d_max(float value) { scale_2d_in_3d_max_ = value; }
    [[nodiscard]] float scale_2d_in_3d_min() const { return scale_2d_in_3d_min_; }
    void set_scale_2d_in_3d_min(float value) { scale_2d_in_3d_min_ = value; }
    [[nodiscard]] float base_fov_angle() const { return base_fov_angle_; }
    [[nodiscard]] float get_screen_angle() const { return base_fov_angle_; }
    void set_base_fov_angle(float value) { base_fov_angle_ = value; }
    [[nodiscard]] float angle_factor() const { return angle_factor_; }
    void set_angle_factor(float value) { angle_factor_ = value; }
    [[nodiscard]] uint8_t hq_shadows() const { return hq_shadows_; }
    void set_hq_shadows(uint8_t value) { hq_shadows_ = value; }
    [[nodiscard]] bool fog_setting() const { return fog_setting_; }
    void set_fog_setting(bool value) { fog_setting_ = value; }
    [[nodiscard]] bool distance_clipping() const { return distance_clipping_; }
    void set_distance_clipping(bool value) { distance_clipping_ = value; }
    [[nodiscard]] float last_sphere_distance() const { return last_sphere_distance_; }
    [[nodiscard]] float sphere_depth() const { return last_sphere_distance_; }
    void set_last_sphere_distance(float value) { last_sphere_distance_ = value; }
    [[nodiscard]] uint8_t billboard_flag() const { return billboard_flag_; }
    void set_billboard_flag(uint8_t value) { billboard_flag_ = value; }
    [[nodiscard]] float frame_time_scale() const { return frame_time_scale_; }
    void set_frame_time_scale(float value) { frame_time_scale_ = value; }
    [[nodiscard]] int drawn_viewport() const { return drawn_viewport_; }
    void set_drawn_viewport(int value) { drawn_viewport_ = value; }

private:
    // The viewports, the split-screen mode and the occlusion requests (viewport.cpp).
    std::array<OcclusionRequest, 4> occlusion_requests_{};
    int screen_mode_{};
    std::array<ViewRegion*, 10> viewports_{};
    std::span<const int> mode_map_{kModeMaps};

    // The occlusion query slots and the 2D-in-3D scale (pc_viewport.cpp).
    std::array<uint8_t, 1024> occlusion_slots_{{1}};
    float scale_2d_in_3d_numerator_{300.0f};
    float scale_2d_in_3d_max_{3.0f};
    float scale_2d_in_3d_min_{};

    // The field of view (screen.cpp).
    float base_fov_angle_{};
    float angle_factor_{1.0f};

    // The graphics options (settings.cpp).
    uint8_t hq_shadows_{};
    bool fog_setting_{};
    bool distance_clipping_{};

    // The last visibility test's distance (occlusion.cpp).
    float last_sphere_distance_{};

    // The billboards' flag (pc_billboards.cpp).
    uint8_t billboard_flag_{};

    // The particles' frame: the time scale and the viewport drawn (flexible_particle_manager.cpp).
    float frame_time_scale_{};
    int drawn_viewport_{};
};

// The views' state (made on first use, never destroyed).
ViewSetup& view_setup();

} // namespace render
