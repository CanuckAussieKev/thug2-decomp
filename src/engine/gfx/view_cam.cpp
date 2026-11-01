#include "engine/gfx/view_cam.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/settings.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include "engine/core/checked_index.h"

namespace gfx {

namespace {

constexpr uint32_t kSetHfovRadians = 0x44EA2B6D; // the member function (name unknown): the angle in radians

constexpr float float_from_bits(uint32_t bits) noexcept { return std::bit_cast<float>(bits); }
// The original's constants (0x00648800 pi / 360, 0x00648804 360 / pi, 0x006453BC 0.5, 0x00645464 pi / 180,
// 0x00645468 180 / pi).
constexpr float kDegreesToHalfRadians = float_from_bits(0x3C0EFA35);
constexpr float kHalfRadiansToDegrees = float_from_bits(0x42E52EE0);
const float kHalf = 0.5f;
constexpr float kRadiansPerDegree = float_from_bits(0x3C8EFA35);
constexpr float kDegreesPerRadian = float_from_bits(0x42652EE0);

} // namespace

int g_draw_distance_setting;

// 0x00485BA0
ViewCam::ViewCam() : h_fov_(render::get_screen_angle())
{
    refresh_effective_h_fov();
    core::at(basis_frame_.row, 0) = {1.0f, 0.0f, 0.0f, 0.0f};
    core::at(basis_frame_.row, 1) = {0.0f, 1.0f, 0.0f, 0.0f};
    core::at(basis_frame_.row, 2) = {0.0f, 0.0f, 1.0f, 0.0f};
    core::at(basis_frame_.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    near_dist_ = 12.0f;
    far_clip_ = 12000.0f;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    if (render::g_distance_clipping) { // distance clipping (setting 0x007D6444)
        // (the setting as an unsigned number: FILD of a signed one, 2^32 added when negative)
        auto distance = static_cast<double>(g_draw_distance_setting);
        if (g_draw_distance_setting < 0)
            distance += 4294967296.0;
        far_clip_ = static_cast<float>(distance * 30.0);
    }
    // NOT ORIGINAL - [Graphics] draw_distance_scale (engine/gfx/graphics_options.h).
    far_clip_ =
        render::graphics_options::get_scaled(far_clip_, render::graphics_options::get_pull().draw_distance_scale_);
    at_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
    old_pos_ = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00485AA0: the angle as drawn is the 4:3 one widened by the screen's angle factor (and divided by unknownc0_);
// unknownb4_ is tan(half that) over tan(half the cameras' default angle). (Rounded to float where the original's
// x87 code stores.)
void ViewCam::refresh_effective_h_fov()
{
    const auto half_angle = static_cast<float>(static_cast<double>(h_fov_) * kDegreesToHalfRadians);
    const double factor = render::fetch_fov_ratio();
    const double angle =
        std::atan(factor * (std::tan(static_cast<double>(half_angle)) / unknownc0_)) * kHalfRadiansToDegrees;
    corrected_horiz_fov_ = static_cast<float>(angle);
    const auto tan_adjusted = static_cast<float>(std::tan(angle * kDegreesToHalfRadians));
    const double default_angle = render::get_screen_angle();
    unknownb4_ = static_cast<float>(tan_adjusted / std::tan(default_angle * kHalf * kRadiansPerDegree));
}

// 0x00485B40
bool ViewCam::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name != kSetHfovRadians)
        return objects::Entity::invoke_method_by_name(name, params, script);
    // (the original reads the float into the stack slot of `params`: without one, the pointer's bits are the angle)
    float radians = 0.0f;
    std::memcpy(&radians, static_cast<const void*>(&params), 4);
    params->fetch_float(0u, &radians, 1);
    h_fov_ = radians * kDegreesPerRadian;
    refresh_effective_h_fov();
    return true;
}

// 0x00485A50
void ViewCam::remember_prev_pos()
{
    std::memcpy(&old_pos_, &at_point_, sizeof(at_point_)); // (copied as words)
}

// 0x00485A70
void ViewCam::change_location(const maths::Vec4& pos)
{
    std::memcpy(&at_point_, &pos, sizeof(at_point_)); // (copied as words)
}

// 0x00485A90
maths::Mat4& ViewCam::change_mat(const maths::Mat4& matrix)
{
    basis_frame_ = matrix;
    return basis_frame_;
}

// 0x00485B10
float ViewCam::fetch_effective_h_fov()
{
    refresh_effective_h_fov();
    return corrected_horiz_fov_;
}

// 0x00485B20
void ViewCam::change_h_fov(float degrees)
{
    h_fov_ = degrees;
    refresh_effective_h_fov();
}

} // namespace gfx
