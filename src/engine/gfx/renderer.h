#pragma once

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/gfx/draw_frame.h"
#include "engine/maths/mat4.h"

// NOT ORIGINAL - the renderer's state: the Direct3D 9 device and what was made with it, the frame's camera, matrices
// and counters, and the render state the device was last sent (the original's globals; each member's comment or its
// record's gives the original's address where known).
namespace render {

constexpr size_t kScreenTextureCount = 9;
constexpr size_t kPsConstantFloats = 20; // the pixel shader constant bank

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
    std::array<float, maths::kMatrixFloats> world_transform_copy_;
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
    std::array<IDirect3DTexture9*, kScreenTextureCount>& screen_textures() { return screen_textures_; }
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
    std::array<IDirect3DSurface9*, kScreenTextureCount>& screen_surfaces() { return screen_surfaces_; }
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
    [[nodiscard]] const DeviceViewport& device_viewport() const { return device_viewport_; }
    void set_device_viewport(const DeviceViewport& value) { device_viewport_ = value; }
    // The camera (choose_camera sets it whole; the draw code's flags one by one).
    [[nodiscard]] const RenderCamera& render_camera() const { return camera_; }
    void set_render_camera(const RenderCamera& value) { camera_ = value; }
    void set_custom_transforms(uint8_t value) { camera_.custom_transforms_ = value; }
    void set_clear_colour_and_depth(bool value) { camera_.clear_colour_and_depth_ = value; }
    void set_letterbox_active(uint8_t value) { camera_.letterbox_active_ = value; }
    void set_background_colour(uint32_t value) { camera_.background_colour_ = value; }
    [[nodiscard]] const ViewShape& view_shape() const { return view_shape_; }
    void set_view_shape(const ViewShape& value) { view_shape_ = value; }
    [[nodiscard]] bool radeon8500() const { return radeon8500_; }
    void set_radeon8500(bool value) { radeon8500_ = value; }
    [[nodiscard]] int frames_rendered() const { return frames_rendered_; }
    void count_frame() { ++frames_rendered_; }
    [[nodiscard]] const maths::Mat4& world_matrix() const { return world_matrix_; }
    void set_world_matrix(const maths::Mat4& value) { world_matrix_ = value; }
    [[nodiscard]] const maths::Mat4& view_matrix() const { return view_matrix_; }
    void set_view_matrix(const maths::Mat4& value) { view_matrix_ = value; }
    [[nodiscard]] const maths::Mat4& projection_matrix() const { return projection_matrix_; }
    void set_projection_matrix(const maths::Mat4& value) { projection_matrix_ = value; }
    [[nodiscard]] const maths::Mat4& heading_matrix() const { return heading_matrix_; }
    void set_heading_matrix(const maths::Mat4& value) { heading_matrix_ = value; }
    // The world, view and projection matrices made identity (reset_render_state).
    void reset_transforms()
    {
        for (maths::Mat4* matrix : {&world_matrix_, &view_matrix_, &projection_matrix_}) {
            const auto elements = matrix->floats();
            for (int i = 0; i < maths::kMatrixFloats; ++i) {
                core::at(elements, i) = (i % maths::kIdentityStride == 0) ? 1.0F : 0.0F;
            }
        }
    }
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
    // The texture set on each stage, and the border colour (as the device has them).
    [[nodiscard]] void* stage_texture(int stage) const { return core::at(textures_, stage); }
    void set_stage_texture(int stage, void* texture) { core::at(textures_, stage) = texture; }
    [[nodiscard]] uint32_t border_colour(uint32_t stage) const { return core::at(border_colours_, stage); }
    void set_border_colour(uint32_t stage, uint32_t colour) { core::at(border_colours_, stage) = colour; }
    [[nodiscard]] const void* pixel_shader_locked() const { return pixel_shader_locked_; }
    void set_pixel_shader_locked(const void* value) { pixel_shader_locked_ = value; }
    [[nodiscard]] void* current_pixel_shader() const { return current_pixel_shader_; }
    void set_current_pixel_shader(void* value) { current_pixel_shader_ = value; }
    // The pixel shader's five constants (four passes' colours, then the fog's / constant 4).
    [[nodiscard]] const std::array<float, kPsConstantFloats>& ps_constants() const { return ps_constant_bank_; }
    void set_ps_constant(int index, float value) { core::at(ps_constant_bank_, index) = value; }
    // The first `values.size()` constants from `values` (at most the bank's 20).
    void load_ps_constants(std::span<const float> values)
    {
        std::ranges::copy(values, std::span<float>(ps_constant_bank_).first(values.size()).begin());
    }
    // The first `count` passes' colour constants (r, g, b and a fourth value each; at most four).
    void load_pass_colours(const std::array<std::array<float, 4>, 4>& colours, uint32_t count)
    {
        const auto passes = std::span(colours).first(count);
        std::memcpy(ps_constant_bank_.data(), passes.data(), passes.size_bytes());
    }
    // A colour's scale per channel.
    struct ColourScale {
        double r{};
        double g{};
        double b{};
    };

    // A pass's colour (its constant's r, g, b) scaled by r, g, b, as the instance colour does.
    void scale_pass_colour(uint32_t pass, ColourScale scale)
    {
        const auto scale_red = scale.r;
        const auto scale_green = scale.g;
        const auto scale_blue = scale.b;
        float& c0 = core::at(ps_constant_bank_, 4 * pass);
        float& c1 = core::at(ps_constant_bank_, (4 * pass) + 1);
        float& c2 = core::at(ps_constant_bank_, (4 * pass) + 2);
        c0 = static_cast<float>(scale_red * c0);
        c1 = static_cast<float>(scale_green * c1);
        c2 = static_cast<float>(scale_blue * c2);
    }
    [[nodiscard]] uint32_t instance_colour_on() const { return instance_colour_on_; }
    void set_instance_colour_on(uint32_t value) { instance_colour_on_ = value; }
    [[nodiscard]] const std::array<uint8_t, 4>& instance_colour() const { return instance_colour_; }
    void set_instance_colour(const std::array<uint8_t, 4>& value) { instance_colour_ = value; }
    [[nodiscard]] uint8_t pixel_shader_changed() const { return pixel_shader_changed_; }
    void set_pixel_shader_changed(uint8_t value) { pixel_shader_changed_ = value; }
    [[nodiscard]] uint32_t vertex_format_locked() const { return vertex_format_locked_; }
    void set_vertex_format_locked(uint32_t value) { vertex_format_locked_ = value; }
    [[nodiscard]] uint32_t skipped_passes() const { return skipped_passes_; }
    void set_skipped_passes(uint32_t value) { skipped_passes_ = value; }
    [[nodiscard]] uint32_t submit_locked() const { return submit_locked_; }
    void set_submit_locked(uint32_t value) { submit_locked_ = value; }
    [[nodiscard]] uint32_t fog_active() const { return fog_active_; }
    void set_fog_active(uint32_t value) { fog_active_ = value; }
    // The world transform the instances are drawn with: a copy of the given matrix (its last column 0, 0, 0, 1), or
    // none (null).
    [[nodiscard]] const float* current_world_transform() const { return world_transform_; }
    void set_world_transform(const float* matrix)
    {
        if (!matrix) {
            world_transform_ = nullptr;
            return;
        }
        world_transform_ = world_transform_copy_.data();
        const std::span<const float, 16> given(matrix, 16);
        for (int row = 0; row < 4; ++row) {
            core::at(world_transform_copy_, (row * 4) + 0) = core::at(given, (row * 4) + 0);
            core::at(world_transform_copy_, (row * 4) + 1) = core::at(given, (row * 4) + 1);
            core::at(world_transform_copy_, (row * 4) + 2) = core::at(given, (row * 4) + 2);
            core::at(world_transform_copy_, (row * 4) + 3) = row == 3 ? 1.0F : 0.0F;
        }
    }

private:
    // The device, what was made with it and its settings (device.cpp). The draw device is the same device, kept twice;
    // the display factors scale 640x480 to the back buffer.
    IDirect3DDevice9* d3d_device_{};
    bool stream_offset_supported_{};
    uint32_t loading_bar_timer_{}; // the timer's id (timeSetEvent)
    float display_factor_x_{}; // 640x480 -> back buffer scale
    IDirect3DDevice9* draw_device_{}; // the same device, kept twice
    std::array<IDirect3DTexture9*, kScreenTextureCount> screen_textures_{};
    uint8_t set_frequency_allowed_{1};
    int resolution_setting_{};
    bool antialiasing_{};
    int screen_offset_y_{};
    int screen_offset_x_{};
    float display_factor_y_{};
    IDirect3DSurface9* z_buffer_surface_{};
    std::array<IDirect3DSurface9*, kScreenTextureCount> screen_surfaces_{}; // their level 0
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
    std::array<float, kPsConstantFloats> ps_constant_bank_{};
    uint32_t instance_colour_on_{};
    std::array<uint8_t, 4> instance_colour_{};
    uint8_t pixel_shader_changed_{};
    uint32_t vertex_format_locked_{};
    uint32_t skipped_passes_{};
    uint32_t submit_locked_{};
    uint32_t fog_active_{};
    const float* world_transform_{};
    std::array<float, maths::kMatrixFloats> world_transform_copy_{};
};

// The renderer's state (made on first use, never destroyed).
Renderer& renderer();

} // namespace render
