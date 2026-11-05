#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu_handles.h"

namespace render {

constexpr size_t kPsConstantFloats = 20; // the pixel shader constant bank

// NOT ORIGINAL (the class; its fields were the original's globals): the render state the device was last sent
// (render_state.cpp; the backend skips what is already set) - specular, z writes, depth bias, culling, the texture
// and border colour on each stage, the pixel shader (its lock and change flag) and its constant bank, the instance
// colour, the vertex format and submit locks, the passes skipped, and whether the instance drawn is near the camera.
// D3D9-shaped: another backend keeps its own. Held by the renderer.
class PassState {
public:
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
    [[nodiscard]] GpuTexture* stage_texture(int stage) const { return core::at(textures_, stage); }
    void set_stage_texture(int stage, GpuTexture* texture) { core::at(textures_, stage) = texture; }
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

private:
    uint32_t specular_{};
    uint8_t z_write_{};
    uint32_t depth_bias_{};
    uint32_t cull_mode_{};
    bool near_camera_{};
    std::array<GpuTexture*, 4> textures_{};
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
};

} // namespace render
