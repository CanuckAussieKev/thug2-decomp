#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/gfx/render_camera.h"
#include "base/maths/mat4.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/renderer_caps.h"
#include "engine/gfx/renderer_display.h"
#include "engine/gfx/renderer_fog.h"
#include "engine/gfx/renderer_frame.h"
#include "engine/gfx/renderer_pass.h"
#include "engine/gfx/renderer_view.h"

// NOT ORIGINAL - the renderer's state (the original's globals): what the device can do (DeviceCaps), the display drawn
// to (DisplaySettings), the frames' counters and clocks (FrameCounters), the camera's view (ViewState), the fog
// (FogState) and the render state the device was last sent (PassState) - each a class of its own, held by value. No
// graphics API's type: the Direct3D objects themselves are in render_pc/d3d9_state.h, for the files that talk to
// Direct3D; a backend fills the device's capabilities and the display, and PassState is the D3D9 backend's own.
namespace render {

// The renderer's state (device.cpp, draw_frame.cpp and render_state.cpp use it; the rest of the game reads it through
// these methods, inline one-liners that forward to the parts). Trivially destructible with constant initial values:
// the accessor is a constant-initialised static, no guard.
class Renderer {
public:
    // The view as it is / put back (the level editor's preview draws with its own).
    [[nodiscard]] ViewState save_view() const { return view_; }
    void restore_view(const ViewState& saved) { view_ = saved; }
    [[nodiscard]] bool stream_offset_supported() const { return caps_.stream_offset_supported(); }
    void set_stream_offset_supported(bool value) { caps_.set_stream_offset_supported(value); }
    [[nodiscard]] uint32_t loading_bar_timer() const { return frame_.loading_bar_timer(); }
    void set_loading_bar_timer(uint32_t value) { frame_.set_loading_bar_timer(value); }
    [[nodiscard]] float display_factor_x() const { return display_.display_factor_x(); }
    void set_display_factor_x(float value) { display_.set_display_factor_x(value); }
    [[nodiscard]] uint8_t frequency_setting_allowed() const { return display_.frequency_setting_allowed(); }
    void set_frequency_setting_allowed(uint8_t value) { display_.set_frequency_setting_allowed(value); }
    [[nodiscard]] int resolution_setting() const { return display_.resolution_setting(); }
    void set_resolution_setting(int value) { display_.set_resolution_setting(value); }
    [[nodiscard]] bool antialiasing() const { return display_.antialiasing(); }
    void set_antialiasing(bool value) { display_.set_antialiasing(value); }
    [[nodiscard]] int screen_offset_y() const { return display_.screen_offset_y(); }
    void set_screen_offset_y(int value) { display_.set_screen_offset_y(value); }
    [[nodiscard]] int screen_offset_x() const { return display_.screen_offset_x(); }
    void set_screen_offset_x(int value) { display_.set_screen_offset_x(value); }
    [[nodiscard]] float display_factor_y() const { return display_.display_factor_y(); }
    void set_display_factor_y(float value) { display_.set_display_factor_y(value); }
    [[nodiscard]] bool blend_factor_supported() const { return caps_.blend_factor_supported(); }
    void set_blend_factor_supported(bool value) { caps_.set_blend_factor_supported(value); }
    [[nodiscard]] bool border_addressing_supported() const { return caps_.border_addressing_supported(); }
    void set_border_addressing_supported(bool value) { caps_.set_border_addressing_supported(value); }
    [[nodiscard]] uint32_t max_anisotropy() const { return caps_.max_anisotropy(); }
    void set_max_anisotropy(uint32_t value) { caps_.set_max_anisotropy(value); }
    [[nodiscard]] const DeviceViewport& device_viewport() const { return view_.device_viewport(); }
    void set_device_viewport(const DeviceViewport& value) { view_.set_device_viewport(value); }
    // The camera (choose_camera sets it whole; the draw code's flags one by one).
    [[nodiscard]] const RenderCamera& render_camera() const { return view_.render_camera(); }
    void set_render_camera(const RenderCamera& value) { view_.set_render_camera(value); }
    void set_custom_transforms(uint8_t value) { view_.set_custom_transforms(value); }
    void set_clear_colour_and_depth(bool value) { view_.set_clear_colour_and_depth(value); }
    void set_letterbox_active(uint8_t value) { view_.set_letterbox_active(value); }
    void set_background_colour(uint32_t value) { view_.set_background_colour(value); }
    [[nodiscard]] const ViewShape& view_shape() const { return view_.view_shape(); }
    void set_view_shape(const ViewShape& value) { view_.set_view_shape(value); }
    [[nodiscard]] bool radeon8500() const { return caps_.radeon8500(); }
    void set_radeon8500(bool value) { caps_.set_radeon8500(value); }
    [[nodiscard]] int frames_rendered() const { return frame_.frames_rendered(); }
    void count_frame() { frame_.count_frame(); }
    [[nodiscard]] const maths::Mat4& world_matrix() const { return view_.world_matrix(); }
    void set_world_matrix(const maths::Mat4& value) { view_.set_world_matrix(value); }
    [[nodiscard]] const maths::Mat4& view_matrix() const { return view_.view_matrix(); }
    void set_view_matrix(const maths::Mat4& value) { view_.set_view_matrix(value); }
    [[nodiscard]] const maths::Mat4& projection_matrix() const { return view_.projection_matrix(); }
    void set_projection_matrix(const maths::Mat4& value) { view_.set_projection_matrix(value); }
    [[nodiscard]] const maths::Mat4& heading_matrix() const { return view_.heading_matrix(); }
    void set_heading_matrix(const maths::Mat4& value) { view_.set_heading_matrix(value); }
    // The world, view and projection matrices made identity (reset_render_state).
    void reset_transforms() { view_.reset_transforms(); }
    [[nodiscard]] int step_seconds() const { return frame_.step_seconds(); }
    void set_step_seconds(int value) { frame_.set_step_seconds(value); }
    [[nodiscard]] int screen_copy_index() const { return frame_.screen_copy_index(); }
    void set_screen_copy_index(int value) { frame_.set_screen_copy_index(value); }
    [[nodiscard]] int back_buffer_width() const { return display_.back_buffer_width(); }
    void set_back_buffer_width(int value) { display_.set_back_buffer_width(value); }
    [[nodiscard]] int back_buffer_height() const { return display_.back_buffer_height(); }
    void set_back_buffer_height(int value) { display_.set_back_buffer_height(value); }
    // One draw call of `triangles` more this frame (the frame's counts).
    void count_draw(uint32_t triangles) { frame_.count_draw(triangles); }
    void reset_draw_counts() { frame_.reset_draw_counts(); }
    [[nodiscard]] float fog_begin_distance() const { return fog_.begin_distance(); }
    void set_fog_begin_distance(float value) { fog_.set_begin_distance(value); }
    [[nodiscard]] float fog_end() const { return fog_.end(); }
    void set_fog_end(float value) { fog_.set_end(value); }
    [[nodiscard]] bool shaders_available() const { return caps_.shaders_available(); }
    void set_shaders_available(bool value) { caps_.set_shaders_available(value); }
    [[nodiscard]] uint32_t specular_state() const { return pass_.specular_state(); }
    void set_specular(uint32_t value) { pass_.set_specular(value); }
    [[nodiscard]] uint8_t z_write() const { return pass_.z_write(); }
    void set_z_write(uint8_t value) { pass_.set_z_write(value); }
    [[nodiscard]] uint32_t depth_bias() const { return pass_.depth_bias(); }
    void set_depth_bias(uint32_t value) { pass_.set_depth_bias(value); }
    [[nodiscard]] uint32_t cull_mode() const { return pass_.cull_mode(); }
    void set_cull_mode(uint32_t value) { pass_.set_cull_mode(value); }
    [[nodiscard]] bool near_camera() const { return pass_.near_camera(); }
    void set_near_camera(bool value) { pass_.set_near_camera(value); }
    // The texture set on each stage, and the border colour (as the device has them).
    [[nodiscard]] GpuTexture* stage_texture(int stage) const { return pass_.stage_texture(stage); }
    void set_stage_texture(int stage, GpuTexture* texture) { pass_.set_stage_texture(stage, texture); }
    [[nodiscard]] uint32_t border_colour(uint32_t stage) const { return pass_.border_colour(stage); }
    void set_border_colour(uint32_t stage, uint32_t colour) { pass_.set_border_colour(stage, colour); }
    [[nodiscard]] const void* pixel_shader_locked() const { return pass_.pixel_shader_locked(); }
    void set_pixel_shader_locked(const void* value) { pass_.set_pixel_shader_locked(value); }
    [[nodiscard]] void* current_pixel_shader() const { return pass_.current_pixel_shader(); }
    void set_current_pixel_shader(void* value) { pass_.set_current_pixel_shader(value); }
    // The pixel shader's five constants (four passes' colours, then the fog's / constant 4).
    [[nodiscard]] const std::array<float, kPsConstantFloats>& ps_constants() const { return pass_.ps_constants(); }
    void set_ps_constant(int index, float value) { pass_.set_ps_constant(index, value); }
    // The first `values.size()` constants from `values` (at most the bank's 20).
    void load_ps_constants(std::span<const float> values) { pass_.load_ps_constants(values); }
    // The first `count` passes' colour constants (r, g, b and a fourth value each; at most four).
    void load_pass_colours(const std::array<std::array<float, 4>, 4>& colours, uint32_t count)
    {
        pass_.load_pass_colours(colours, count);
    }
    // A pass's colour (its constant's r, g, b) scaled by r, g, b, as the instance colour does.
    void scale_pass_colour(uint32_t pass, PassState::ColourScale scale) { pass_.scale_pass_colour(pass, scale); }
    [[nodiscard]] uint32_t instance_colour_on() const { return pass_.instance_colour_on(); }
    void set_instance_colour_on(uint32_t value) { pass_.set_instance_colour_on(value); }
    [[nodiscard]] const std::array<uint8_t, 4>& instance_colour() const { return pass_.instance_colour(); }
    void set_instance_colour(const std::array<uint8_t, 4>& value) { pass_.set_instance_colour(value); }
    [[nodiscard]] uint8_t pixel_shader_changed() const { return pass_.pixel_shader_changed(); }
    void set_pixel_shader_changed(uint8_t value) { pass_.set_pixel_shader_changed(value); }
    [[nodiscard]] uint32_t vertex_format_locked() const { return pass_.vertex_format_locked(); }
    void set_vertex_format_locked(uint32_t value) { pass_.set_vertex_format_locked(value); }
    [[nodiscard]] uint32_t skipped_passes() const { return pass_.skipped_passes(); }
    void set_skipped_passes(uint32_t value) { pass_.set_skipped_passes(value); }
    [[nodiscard]] uint32_t submit_locked() const { return pass_.submit_locked(); }
    void set_submit_locked(uint32_t value) { pass_.set_submit_locked(value); }
    [[nodiscard]] uint32_t fog_active() const { return fog_.active(); }
    void set_fog_active(uint32_t value) { fog_.set_active(value); }
    // The world transform the instances are drawn with: a copy of the given matrix (its last column 0, 0, 0, 1), or
    // none (null).
    [[nodiscard]] const float* current_world_transform() const { return view_.current_world_transform(); }
    void set_world_transform(const float* matrix) { view_.set_world_transform(matrix); }

private:
    DeviceCaps caps_;         // what the device can do
    DisplaySettings display_; // the display drawn to
    FrameCounters frame_;     // the frames' counters and clocks
    ViewState view_;          // the camera's view (draw_frame.cpp)
    FogState fog_;            // the distance fog
    PassState pass_;          // the render state the device was last sent (render_state.cpp)
};

// The renderer's state (made on first use, never destroyed).
Renderer& renderer();

} // namespace render
