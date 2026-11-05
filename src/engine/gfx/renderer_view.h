#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "base/core/checked_index.h"
#include "base/maths/mat4.h"
#include "engine/gfx/render_camera.h"

namespace render {

// NOT ORIGINAL (the class; its fields were the original's globals): the camera's view as render::choose_camera and
// the instance pass leave it - the world, view, projection and heading matrices, the device viewport, the camera, the
// view's shape (culling) and the world transform the instances are drawn with (none, or the copy). Held by the
// renderer; a copy is the snapshot the level editor's preview draws around (save_view / restore_view - the copy's
// world transform still points at the renderer's own copy, which the restore puts back too).
class ViewState {
public:
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
    maths::Mat4 world_matrix_{};
    maths::Mat4 view_matrix_{};
    maths::Mat4 projection_matrix_{};
    maths::Mat4 heading_matrix_{};
    DeviceViewport device_viewport_{};
    RenderCamera camera_{};
    ViewShape view_shape_{};
    const float* world_transform_{};
    std::array<float, maths::kMatrixFloats> world_transform_copy_{};
};

} // namespace render
