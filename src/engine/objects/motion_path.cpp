// The motion component's members of 0x00423130-0x00423642 (GameObjects::MotionPathMembers): see motion_path.h. From their
// disassembly (0x00423160-0x004234F0 are no Ghidra functions; their names from the member-function table's static
// initialiser, 0x00640E30). The x87 roundings are kept: values the original stores are rounded to float, the ones it
// keeps on the FPU stack are worked in double.

#include "engine/objects/motion_path.h"

#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"

namespace objects {

uint32_t g_motion_path_member_functions[294]{
            0x21ACCF0D, 0x0A015E17, 0, 0, 0, 0,
            0x7BC461AE, 0x0A015E17, 0, 0, 0, 0,
            0x8E52ABEA, 0x0A015E17, 0, 0, 0, 0,
            0xC8CC6820, 0x0A015E17, 0, 0, 0, 0,
            0x68E1F022, 0x0A015E17, 0, 0, 0, 0,
            0xDFBE3DB3, 0x0A015E17, 0, 0, 0, 0,
            0xFB603EA9, 0x0A015E17, 0, 0, 0, 0,
            0x2CA63CDF, 0x0A015E17, 0, 0, 0, 0,
            0xD6697AD0, 0x0A015E17, 0, 0, 0, 0,
            0xA67FC783, 0x0A015E17, 0, 0, 0, 0,
            0x5CF1B7D4, 0x0A015E17, 0, 0, 0, 0,
            0x0B231A9D, 0x0A015E17, 0, 0, 0, 0,
            0x7C242A0B, 0x0A015E17, 0, 0, 0, 0,
            0xE52D7BB1, 0x0A015E17, 0, 0, 0, 0,
            0xD8F4BC32, 0x0A015E17, 0, 0, 0, 0,
            0xAA3CB476, 0x0A015E17, 0, 0, 0, 0,
            0xB75589CE, 0x0A015E17, 0, 0, 0, 0,
            0x6866312F, 0x0A015E17, 0, 0, 0, 0,
            0x434EA1FB, 0x0A015E17, 0, 0, 0, 0,
            0x76003D82, 0x0A015E17, 0, 0, 0, 0,
            0xBFB0031D, 0x0A015E17, 0, 0, 0, 0,
            0x33E04032, 0x0A015E17, 0, 0, 0, 0,
            0x103BD253, 0x0A015E17, 0, 0, 0, 0,
            0x6419CEFF, 0x0A015E17, 0, 0, 0, 0,
            0xA5A2D218, 0x0A015E17, 0, 0, 0, 0,
            0x74C30242, 0x0A015E17, 0, 0, 0, 0,
            0xE8D121CB, 0x0A015E17, 0, 0, 0, 0,
            0x33DF698F, 0x0A015E17, 0, 0, 0, 0,
            0xFE71453F, 0x0A015E17, 0, 0, 0, 0,
            0xBD6C6807, 0x0A015E17, 0, 0, 0, 0,
            0x0DA2D86D, 0x0A015E17, 0, 0, 0, 0,
            0xCC19C48A, 0x0A015E17, 0, 0, 0, 0,
            0x8B669799, 0x0A015E17, 0, 0, 0, 0,
            0xF389285D, 0x0A015E17, 0, 0, 0, 0,
            0x5CE058FD, 0x0A015E17, 0, 0, 0, 0,
            0x84EC6600, 0x0A015E17, 0, 0, 0, 0,
            0xEA81A32B, 0x0A015E17, 0, 0, 0, 0,
            0x3BCAAC3F, 0x0A015E17, 0, 0, 0, 0,
            0xC5ECA638, 0x0A015E17, 0, 0, 0, 0,
            0x1E094B84, 0x0A015E17, 0, 0, 0, 0,
            0xBDC4C878, 0x0A015E17, 0, 0, 0, 0,
            0xDBFDD02F, 0x0A015E17, 0, 0, 0, 0,
            0x8D95F1E1, 0x0A015E17, 0, 0, 0, 0,
            0x3407AD3D, 0x0A015E17, 0, 0, 0, 0,
            0xE606EBA2, 0x0A015E17, 0, 0, 0, 0,
            0xE76D73D2, 0x0A015E17, 0, 0, 0, 0,
            0x1AE46261, 0x0A015E17, 0, 0, 0, 0,
            0x2FAD370D, 0x0A015E17, 0, 0, 0, 0,
            0, 0x0A015E17, 0, 0, 0, 0};

namespace {

constexpr uint32_t kFps = 0xAAD7C80F, kIps = 0xA18B8F32;     // speeds: feet / inches a second
constexpr uint32_t kFpsps = 0xF414A5EA, kIpsps = 0x7644323B; // accelerations: feet / inches a second per second
constexpr uint32_t kOff = 0xD443A2BC;
constexpr uint32_t kInches = 0xF915B4F3;

constexpr float kTwelveInchScale = 12.0f;          // 0x0064546C
constexpr float kMilesToInches = 63360.0f;      // 0x006461E4
constexpr float kPerHourToPerSecond = 0.000277777785f; // 0x006461E0 (1 / 3600)
constexpr float kDefaultMinStopVelocity = 15.0f;

// A speed (or acceleration) from the unnamed float into `value`: miles an hour, feet (`feet`) or inches (`inches`) a
// second.
void get_speed(script::Struct* params, float* value, uint32_t feet, uint32_t inches)
{
    if (!params->fetch_float(0u, value, 0))
        return;
    if (params->has_bare_flag(feet)) {
        *value *= kTwelveInchScale;
        return;
    }
    if (params->has_bare_flag(inches))
        return;
    *value = static_cast<float>(static_cast<double>(*value) * kMilesToInches * kPerHourToPerSecond);
}

// The script waits on the component (CScript::set_wait_on_component).
int wait_on(script::Script* script, MotionPathMembers* component, script::BlockReason type)
{
    script->set_wait_on_component(type, reinterpret_cast<ObjectModule*>(component), 0);
    return 1;
}

} // namespace

// 0x00423130
void __cdecl MotionPathMembers::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_motion_path_member_functions, 0x18);
}

// 0x00423150
bool MotionPathMembers::is_spinning() const { return (motion_flags_ & kMotionFlagRotating) != 0; }

// 0x00423520
bool MotionPathMembers::picks_random_link() const { return (path_flags_ & kMotionPathRandom) != 0; }

// 0x00423530
void MotionPathMembers::update_path_speed()
{
    if (!(motion_flags_ & kMotionFlagOnPath))
        return;
    if (path_flags_ & kMotionPathStopping) {
        if (stop_covered_ >= stop_distance_)
            return; // (NaN goes on)
        float covered = static_cast<float>(static_cast<double>(step_seconds_) * velocity_ + stop_covered_);
        stop_covered_ = covered;
        if (stop_distance_ == 0.0f || covered >= stop_distance_) {
            velocity_ = 0.0f;
            return;
        }
        double t = static_cast<double>(covered) / stop_distance_;
        velocity_ = static_cast<float>((1.0 - t) * stop_start_velocity_ + t * min_stop_velocity_);
        return;
    }
    if (velocity_ < path_velocity_) {
        // Up to the path velocity.
        if (acceleration_ == 0.0f) {
            velocity_ = path_velocity_;
            return;
        }
        double velocity = static_cast<double>(step_seconds_) * acceleration_ + velocity_;
        velocity_ = static_cast<float>(velocity);
        if (velocity > path_velocity_)
            velocity_ = path_velocity_;
        return;
    }
    if (!(velocity_ > path_velocity_))
        return; // (there, or NaN)
    // Down to it.
    if (slowdown_rate_ == 0.0f) {
        velocity_ = path_velocity_;
        return;
    }
    double velocity = velocity_ - static_cast<double>(step_seconds_) * slowdown_rate_;
    velocity_ = static_cast<float>(velocity);
    if (velocity < path_velocity_)
        velocity_ = path_velocity_;
}

// 0x00423160
int MotionPathMembers::is_moving_from_script(script::Struct*, script::Script*)
{
    return (motion_flags_ & kMotionFlagMoving) ? 1 : 0;
}

// 0x00423170
int MotionPathMembers::is_rotating_from_script(script::Struct*, script::Script*)
{
    return (motion_flags_ & kMotionFlagRotating) ? 1 : 0;
}

// 0x00423180
int MotionPathMembers::stop_moving(script::Struct*, script::Script*)
{
    motion_flags_ &= ~kMotionFlagMoving;
    return 1;
}

// 0x00423190
int MotionPathMembers::stop_rotating(script::Struct*, script::Script*)
{
    motion_flags_ &= ~(kMotionFlagRotating | 0x40u);
    return 1;
}

// 0x004231A0
int MotionPathMembers::set_path_velocity(script::Struct* params, script::Script*)
{
    get_speed(params, &path_velocity_, kFps, kIps);
    return 1;
}

// 0x00423210
int MotionPathMembers::set_path_min_stop_vel(script::Struct* params, script::Script*)
{
    get_speed(params, &min_stop_velocity_, kFps, kIps);
    return 1;
}

// 0x00423280
int MotionPathMembers::set_path_acceleration(script::Struct* params, script::Script*)
{
    get_speed(params, &acceleration_, kFpsps, kIpsps);
    return 1;
}

// 0x004232F0
int MotionPathMembers::set_path_deceleration(script::Struct* params, script::Script*)
{
    get_speed(params, &slowdown_rate_, kFpsps, kIpsps);
    return 1;
}

// 0x00423360
int MotionPathMembers::route_facing(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kOff))
        path_flags_ |= kMotionPathNoHeading;
    else
        path_flags_ &= ~kMotionPathNoHeading;
    return 1;
}

// 0x004233A0
int MotionPathMembers::random_path_mode(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kOff))
        path_flags_ &= ~kMotionPathRandom;
    else
        path_flags_ |= kMotionPathRandom;
    return 1;
}

// 0x004233E0
int MotionPathMembers::stop_along_path(script::Struct* params, script::Script*)
{
    stop_distance_ = 0.0f;
    stop_covered_ = 0.0f;
    if (min_stop_velocity_ == 0.0f)
        min_stop_velocity_ = kDefaultMinStopVelocity;
    params->fetch_float(0u, &stop_distance_, 0);
    if (!params->has_bare_flag(kInches)) {
        // (in feet: the stop's starting speed noted - not with `inches`, as the original)
        stop_start_velocity_ = velocity_;
        stop_distance_ *= kTwelveInchScale;
    }
    if (stop_distance_ == 0.0f)
        velocity_ = 0.0f;
    path_flags_ |= kMotionPathStopping;
    return 1;
}

// 0x00423480
int MotionPathMembers::start_along_path(script::Struct*, script::Script*)
{
    path_flags_ &= ~kMotionPathStopping;
    return 1;
}

// 0x00423490
int MotionPathMembers::wait_jump_finished(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(7));
}

// 0x004234B0
int MotionPathMembers::wait_rotate(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(9));
}

// 0x004234D0
int MotionPathMembers::wait_move(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(5));
}

// 0x004234F0
int MotionPathMembers::wait_stop(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(8));
}

} // namespace Obj
