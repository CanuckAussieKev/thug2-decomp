#include "engine/gfx/view_cam.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/settings.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity.h"

namespace gfx {

static_assert(offsetof(ViewCam, old_pos_) == 0x4C && offsetof(ViewCam, at_point_) == 0x5C &&
                  offsetof(ViewCam, basis_frame_) == 0x6C && offsetof(ViewCam, h_fov_) == 0xAC &&
                  offsetof(ViewCam, corrected_horiz_fov_) == 0xB0 && offsetof(ViewCam, unknownb4_) == 0xB4 &&
                  offsetof(ViewCam, near_dist_) == 0xB8 && offsetof(ViewCam, far_clip_) == 0xBC &&
                  offsetof(ViewCam, unknownc0_) == 0xC0 && sizeof(ViewCam) == 0xC4,
              "the original's layout");

namespace {

extern void** const kVtable;
constexpr uint32_t kSetHfovRadians = 0x44EA2B6D; // the member function (name unknown): the angle in radians

float float_from_bits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
// The original's constants (0x00648800 pi / 360, 0x00648804 360 / pi, 0x006453BC 0.5, 0x00645464 pi / 180,
// 0x00645468 180 / pi).
const float kDegreesToHalfRadians = float_from_bits(0x3C0EFA35);
const float kHalfRadiansToDegrees = float_from_bits(0x42E52EE0);
const float kHalf = 0.5f;
const float kRadiansPerDegree = float_from_bits(0x3C8EFA35);
const float kDegreesPerRadian = float_from_bits(0x42652EE0);

} // namespace

int g_draw_distance_setting;

// 0x00485BA0
ViewCam* ViewCam::make_new()
{
    objects::Entity::make_new();
    *reinterpret_cast<void***>(this) = kVtable;
    unknownc0_ = 1.0f;
    h_fov_ = render::get_screen_angle();
    refresh_effective_h_fov();
    basis_frame_.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    basis_frame_.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    basis_frame_.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    basis_frame_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    near_dist_ = 12.0f;
    far_clip_ = 12000.0f;
    if (render::g_distance_clipping) { // distance clipping (setting 0x007D6444)
        // (the setting as an unsigned number: FILD of a signed one, 2^32 added when negative)
        double distance = static_cast<double>(g_draw_distance_setting);
        if (g_draw_distance_setting < 0)
            distance += 4294967296.0;
        far_clip_ = static_cast<float>(distance * 30.0);
    }
    // NOT ORIGINAL - [Graphics] draw_distance_scale (engine/gfx/graphics_options.h).
    far_clip_ = render::graphics_options::get_scaled(far_clip_, render::graphics_options::get_pull().draw_distance_scale_);
    at_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
    old_pos_ = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x004859F0
void ViewCam::destroy_body()
{
    *reinterpret_cast<void***>(this) = kVtable;
    objects::Entity::destroy_body();
}

// 0x00485A20 (vtable slot 0)
ViewCam* ViewCam::scalar_delete(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x00485AA0 (x87 as the original, so results round the same): the angle as drawn is the 4:3 one widened by the
// screen's angle factor (and divided by unknownc0_); unknownb4_ is tan(half that) over tan(half the cameras'
// default angle).
void ViewCam::refresh_effective_h_fov()
{
    const float to_half_radians = kDegreesToHalfRadians, to_degrees = kHalfRadiansToDegrees, half = kHalf,
                to_radians = kRadiansPerDegree;
    float half_angle;
    float h_fov = h_fov_;
    __asm {
        fld h_fov
        fmul to_half_radians
        fstp half_angle
    }
    float factor = render::fetch_fov_ratio();
    float divisor = unknownc0_;
    float adjusted, tan_adjusted;
    __asm {
        fld factor
        fld half_angle
        fptan
        fstp st(0)
        fdiv divisor
        fmulp st(1), st(0)
        fld1
        fpatan
        fmul to_degrees
        fst adjusted
        fmul to_half_radians
        fptan
        fstp st(0)
        fstp tan_adjusted
    }
    corrected_horiz_fov_ = adjusted;
    float default_angle = render::get_screen_angle();
    float ratio;
    __asm {
        fld default_angle
        fmul half
        fmul to_radians
        fptan
        fstp st(0)
        fdivr tan_adjusted
        fstp ratio
    }
    unknownb4_ = ratio;
}

// 0x00485B40
bool ViewCam::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name != kSetHfovRadians)
        return objects::Entity::invoke_method_by_name(name, params, script);
    // (the original reads the float into the stack slot of `params`: without one, the pointer's bits are the angle)
    float radians;
    std::memcpy(&radians, &params, 4);
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

namespace {
static VtableSlot g_vtable_slots[] = {&gfx::ViewCam::scalar_delete, &objects::Entity::change_traits,
                static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::Entity::invoke_method_by_name),
                &gfx::ViewCam::invoke_method_by_name, stubs::ignore_argument, &objects::Entity::pass_event_to_script,
                &objects::Entity::flag_as_dead, stubs::return_true};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);
} // namespace

} // namespace Gfx
