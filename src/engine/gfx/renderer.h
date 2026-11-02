#pragma once

#include <windows.h>

#include <d3d9.h>

#include <array>
#include <cstdint>

#include "engine/gfx/draw_frame.h"
#include "engine/maths/mat4.h"

// NOT ORIGINAL - the renderer's state: the Direct3D 9 device and what was made with it, the frame's camera, matrices
// and counters, and the render state the device was last sent (the original's globals; each member's comment or its
// record's gives the original's address where known).
namespace render {

// The camera's view as render::choose_camera and the instance pass leave it: the matrices, the device viewport, the
// camera, the view's shape (culling) and the culling world matrix (none or the copy). The level editor's preview draws
// with its own and puts this back.
struct ViewSnapshot {
    maths::Mat4 world_matrix_;
    maths::Mat4 view_matrix_;
    maths::Mat4 projection_matrix_;
    maths::Mat4 heading_matrix_;
    DeviceViewport device_viewport_;
    RenderCamera camera_;
    ViewShape view_shape_;
    const float* world_transform_;
    std::array<float, 16> world_transform_copy_;
};

// The renderer's state (device.cpp, draw_frame.cpp and render_state.cpp use it; the rest of the game reads it through
// these methods). Trivially destructible with constant initial values: the accessor is a constant-initialised static,
// no guard.
class Renderer {
public:
    [[nodiscard]] ViewSnapshot save_view() const
    {
        return {.world_matrix_ = world_matrix_,
                .view_matrix_ = view_matrix_,
                .projection_matrix_ = projection_matrix_,
                .heading_matrix_ = heading_matrix_,
                .device_viewport_ = device_viewport_,
                .camera_ = camera_,
                .view_shape_ = view_shape_,
                .world_transform_ = world_transform_,
                .world_transform_copy_ = world_transform_copy_};
    }
    void restore_view(const ViewSnapshot& saved)
    {
        world_matrix_ = saved.world_matrix_;
        view_matrix_ = saved.view_matrix_;
        projection_matrix_ = saved.projection_matrix_;
        heading_matrix_ = saved.heading_matrix_;
        device_viewport_ = saved.device_viewport_;
        camera_ = saved.camera_;
        view_shape_ = saved.view_shape_;
        world_transform_ = saved.world_transform_;
        world_transform_copy_ = saved.world_transform_copy_;
    }
    [[nodiscard]] IDirect3DDevice9* d3d_device() const { return d3d_device_; }
    void set_d3d_device(IDirect3DDevice9* value) { d3d_device_ = value; }
    [[nodiscard]] bool stream_offset_supported() const { return stream_offset_supported_; }
    void set_stream_offset_supported(bool value) { stream_offset_supported_ = value; }
    [[nodiscard]] uint32_t loading_bar_timer() const { return loading_bar_timer_; }
    void set_loading_bar_timer(uint32_t value) { loading_bar_timer_ = value; }
    [[nodiscard]] float display_factor_x() const { return display_factor_x_; }
    void set_display_factor_x(float value) { display_factor_x_ = value; }
    [[nodiscard]] IDirect3DDevice9* draw_device() const { return draw_device_; }
    void set_draw_device(IDirect3DDevice9* value) { draw_device_ = value; }
    std::array<IDirect3DTexture9*, 9>& screen_textures() { return screen_textures_; }
    [[nodiscard]] uint8_t frequency_setting_allowed() const { return set_frequency_allowed_; }
    void set_frequency_setting_allowed(uint8_t value) { set_frequency_allowed_ = value; }
    [[nodiscard]] int resolution_setting() const { return resolution_setting_; }
    void set_resolution_setting(int value) { resolution_setting_ = value; }
    [[nodiscard]] bool antialiasing() const { return antialiasing_; }
    void set_antialiasing(bool value) { antialiasing_ = value; }
    [[nodiscard]] int screen_offset_y() const { return screen_offset_y_; }
    void set_screen_offset_y(int value) { screen_offset_y_ = value; }
    [[nodiscard]] int screen_offset_x() const { return screen_offset_x_; }
    void set_screen_offset_x(int value) { screen_offset_x_ = value; }
    [[nodiscard]] float display_factor_y() const { return display_factor_y_; }
    void set_display_factor_y(float value) { display_factor_y_ = value; }
    [[nodiscard]] IDirect3DSurface9* z_buffer_surface() const { return z_buffer_surface_; }
    void set_z_buffer_surface(IDirect3DSurface9* value) { z_buffer_surface_ = value; }
    std::array<IDirect3DSurface9*, 9>& screen_surfaces() { return screen_surfaces_; }
    [[nodiscard]] IDirect3DSurface9* back_depth() const { return back_depth_; }
    void set_back_depth(IDirect3DSurface9* value) { back_depth_ = value; }
    [[nodiscard]] IDirect3DSurface9* back_buffer() const { return back_buffer_; }
    void set_back_buffer(IDirect3DSurface9* value) { back_buffer_ = value; }
    [[nodiscard]] bool blend_factor_supported() const { return blend_factor_supported_; }
    void set_blend_factor_supported(bool value) { blend_factor_supported_ = value; }
    [[nodiscard]] bool border_addressing_supported() const { return border_addressing_supported_; }
    void set_border_addressing_supported(bool value) { border_addressing_supported_ = value; }
    [[nodiscard]] uint32_t max_anisotropy() const { return max_anisotropy_; }
    void set_max_anisotropy(uint32_t value) { max_anisotropy_ = value; }
    [[nodiscard]] HWND game_window() const { return game_window_; }
    void set_game_window(HWND value) { game_window_ = value; }
    [[nodiscard]] IDirect3D9* direct_3d() const { return direct_3d_; }
    void set_direct_3d(IDirect3D9* value) { direct_3d_ = value; }
    DeviceViewport& device_viewport() { return device_viewport_; }
    RenderCamera& render_camera() { return camera_; }
    ViewShape& view_shape() { return view_shape_; }
    [[nodiscard]] bool radeon8500() const { return radeon8500_; }
    void set_radeon8500(bool value) { radeon8500_ = value; }
    [[nodiscard]] int frames_rendered() const { return frames_rendered_; }
    void count_frame() { ++frames_rendered_; }
    maths::Mat4& world_matrix() { return world_matrix_; }
    maths::Mat4& view_matrix() { return view_matrix_; }
    maths::Mat4& projection_matrix() { return projection_matrix_; }
    maths::Mat4& heading_matrix() { return heading_matrix_; }
    [[nodiscard]] int step_seconds() const { return step_seconds_; }
    void set_step_seconds(int value) { step_seconds_ = value; }
    [[nodiscard]] int screen_copy_index() const { return screen_copy_index_; }
    void set_screen_copy_index(int value) { screen_copy_index_ = value; }
    [[nodiscard]] int back_buffer_width() const { return back_buffer_width_; }
    void set_back_buffer_width(int value) { back_buffer_width_ = value; }
    [[nodiscard]] int back_buffer_height() const { return back_buffer_height_; }
    void set_back_buffer_height(int value) { back_buffer_height_ = value; }
    // One draw call of `triangles` more this frame (the frame's counts).
    void count_draw(uint32_t triangles)
    {
        ++draw_calls_;
        drawn_triangles_ += triangles;
    }
    void reset_draw_counts()
    {
        draw_calls_ = 0;
        drawn_triangles_ = 0;
    }
    [[nodiscard]] float fog_begin_distance() const { return fog_begin_distance_; }
    void set_fog_begin_distance(float value) { fog_begin_distance_ = value; }
    [[nodiscard]] float fog_end() const { return fog_end_; }
    void set_fog_end(float value) { fog_end_ = value; }
    [[nodiscard]] bool shaders_available() const { return shaders_available_; }
    void set_shaders_available(bool value) { shaders_available_ = value; }
    [[nodiscard]] uint32_t specular_state() const { return specular_; }
    void set_specular(uint32_t value) { specular_ = value; }
    [[nodiscard]] uint8_t z_write() const { return z_write_; }
    void set_z_write(uint8_t value) { z_write_ = value; }
    [[nodiscard]] uint32_t depth_bias() const { return depth_bias_; }
    void set_depth_bias(uint32_t value) { depth_bias_ = value; }
    [[nodiscard]] uint32_t cull_mode() const { return cull_mode_; }
    void set_cull_mode(uint32_t value) { cull_mode_ = value; }
    [[nodiscard]] bool near_camera() const { return near_camera_; }
    void set_near_camera(bool value) { near_camera_ = value; }
    std::array<void*, 4>& stage_textures() { return textures_; }
    std::array<uint32_t, 4>& border_colours() { return border_colours_; }
    [[nodiscard]] const void* pixel_shader_locked() const { return pixel_shader_locked_; }
    void set_pixel_shader_locked(const void* value) { pixel_shader_locked_ = value; }
    [[nodiscard]] void* current_pixel_shader() const { return current_pixel_shader_; }
    void set_current_pixel_shader(void* value) { current_pixel_shader_ = value; }
    std::array<float, 20>& ps_constant_bank() { return ps_constant_bank_; }
    [[nodiscard]] uint32_t instance_colour_on() const { return instance_colour_on_; }
    void set_instance_colour_on(uint32_t value) { instance_colour_on_ = value; }
    std::array<uint8_t, 4>& instance_colour() { return instance_colour_; }
    [[nodiscard]] uint8_t pixel_shader_changed() const { return pixel_shader_changed_; }
    void set_pixel_shader_changed(uint8_t value) { pixel_shader_changed_ = value; }
    [[nodiscard]] uint32_t vertex_format_locked() const { return vertex_format_locked_; }
    void set_vertex_format_locked(uint32_t value) { vertex_format_locked_ = value; }
    uint32_t& skipped_passes() { return skipped_passes_; }
    [[nodiscard]] uint32_t submit_locked() const { return submit_locked_; }
    void set_submit_locked(uint32_t value) { submit_locked_ = value; }
    [[nodiscard]] uint32_t fog_active() const { return fog_active_; }
    void set_fog_active(uint32_t value) { fog_active_ = value; }
    const float*& current_world_transform() { return world_transform_; }
    std::array<float, 16>& world_transform_copy() { return world_transform_copy_; }

private:
    // The device, what was made with it and its settings (device.cpp). The draw device is the same device, kept twice;
    // the display factors scale 640x480 to the back buffer.
    IDirect3DDevice9* d3d_device_{};
    bool stream_offset_supported_{};
    uint32_t loading_bar_timer_{}; // the timer's id (timeSetEvent)
    float display_factor_x_{}; // 640x480 -> back buffer scale
    IDirect3DDevice9* draw_device_{}; // the same device, kept twice
    std::array<IDirect3DTexture9*, 9> screen_textures_{};
    uint8_t set_frequency_allowed_{1};
    int resolution_setting_{};
    bool antialiasing_{};
    int screen_offset_y_{};
    int screen_offset_x_{};
    float display_factor_y_{};
    IDirect3DSurface9* z_buffer_surface_{};
    std::array<IDirect3DSurface9*, 9> screen_surfaces_{}; // their level 0
    IDirect3DSurface9* back_depth_{};
    IDirect3DSurface9* back_buffer_{};
    bool blend_factor_supported_{};
    bool border_addressing_supported_{};
    uint32_t max_anisotropy_{};
    HWND game_window_{};
    IDirect3D9* direct_3d_{};

    // The frame's camera, matrices, viewport and counters (draw_frame.cpp).
    DeviceViewport device_viewport_{};
    RenderCamera camera_{};
    ViewShape view_shape_{};
    bool radeon8500_{};
    int frames_rendered_{};
    maths::Mat4 world_matrix_{};
    maths::Mat4 view_matrix_{};
    maths::Mat4 projection_matrix_{};
    maths::Mat4 heading_matrix_{};
    int step_seconds_{};
    int screen_copy_index_{};
    int back_buffer_width_{};
    int back_buffer_height_{};
    uint32_t draw_calls_{};
    uint32_t drawn_triangles_{};
    float fog_begin_distance_{};
    float fog_end_{};

    // The render state the device was last sent (render_state.cpp).
    bool shaders_available_{};
    uint32_t specular_{};
    uint8_t z_write_{};
    uint32_t depth_bias_{};
    uint32_t cull_mode_{};
    bool near_camera_{};
    std::array<void*, 4> textures_{};
    std::array<uint32_t, 4> border_colours_{};
    const void* pixel_shader_locked_{}; // (a shader set for a whole pass: set_pixel_shader leaves the device's alone)
    void* current_pixel_shader_{};
    std::array<float, 20> ps_constant_bank_{};
    uint32_t instance_colour_on_{};
    std::array<uint8_t, 4> instance_colour_{};
    uint8_t pixel_shader_changed_{};
    uint32_t vertex_format_locked_{};
    uint32_t skipped_passes_{};
    uint32_t submit_locked_{};
    uint32_t fog_active_{};
    const float* world_transform_{};
    std::array<float, 16> world_transform_copy_{};
};

// The renderer's state (made on first use, never destroyed).
Renderer& renderer();

} // namespace render
