// The motion component's members of 0x00423130-0x00423642 (GameObjects::MotionPathMembers): see motion_path.h. From their
// disassembly (0x00423160-0x004234F0 are no Ghidra functions; their names from the member-function table's static
// initialiser, 0x00640E30). The x87 roundings are kept: values the original stores are rounded to float, the ones it
// keeps on the FPU stack are worked in double.

#include "engine/objects/motion_path.h"

#include <cstdint>
#include <array>

#include "engine/objects/entity.h"
#include "engine/objects/motion.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"

namespace objects {
namespace {
constexpr uint32_t kMotionFlagRotatingAlt = 0x40u;
constexpr int kBlockReasonJumpFinished = 7;
constexpr int kBlockReasonRotate = 9;
constexpr int kBlockReasonMove = 5;
constexpr int kBlockReasonStop = 8;
constexpr uint32_t kObjIsMoving = 0x21ACCF0D; // Obj_IsMoving
constexpr uint32_t kMotion = 0x0A015E17; // Motion
constexpr uint32_t kObjIsRotating = 0x7BC461AE; // Obj_IsRotating
constexpr uint32_t kObjStorePos = 0x8E52ABEA; // Obj_StorePos
constexpr uint32_t kHashC8CC6820 = 0xC8CC6820; // (text unknown)
constexpr uint32_t kObjStopMoving = 0x68E1F022; // Obj_StopMoving
constexpr uint32_t kObjStopRotating = 0xDFBE3DB3; // Obj_StopRotating
constexpr uint32_t kObjSetPathVelocity = 0xFB603EA9; // Obj_SetPathVelocity
constexpr uint32_t kObjSetPathMinStopVel = 0x2CA63CDF; // Obj_SetPathMinStopVel
constexpr uint32_t kObjSetPathAcceleration = 0xD6697AD0; // Obj_SetPathAcceleration
constexpr uint32_t kObjSetPathDeceleration = 0xA67FC783; // Obj_SetPathDeceleration
constexpr uint32_t kObjRotate = 0x5CF1B7D4; // Obj_Rotate
constexpr uint32_t kObjRotX = 0x0B231A9D; // Obj_RotX
constexpr uint32_t kObjRotY = 0x7C242A0B; // Obj_RotY
constexpr uint32_t kObjRotZ = 0xE52D7BB1; // Obj_RotZ
constexpr uint32_t kObjPathHeading = 0xD8F4BC32; // Obj_PathHeading
constexpr uint32_t kHashAA3CB476 = 0xAA3CB476; // (text unknown)
constexpr uint32_t kObjGetNextObjOnPath = 0xB75589CE; // Obj_GetNextObjOnPath
constexpr uint32_t kObjGetRandomLink = 0x6866312F; // Obj_GetRandomLink
constexpr uint32_t kObjRandomPathMode = 0x434EA1FB; // Obj_RandomPathMode
constexpr uint32_t kObjSetPathTurnDist = 0x76003D82; // Obj_SetPathTurnDist
constexpr uint32_t kObjFollowPath = 0xBFB0031D; // Obj_FollowPath
constexpr uint32_t kObjLookAtObject = 0x33E04032; // Obj_LookAtObject
constexpr uint32_t kHash103BD253 = 0x103BD253; // (text unknown)
constexpr uint32_t kObjLookAtNodeLinked = 0x6419CEFF; // Obj_LookAtNodeLinked
constexpr uint32_t kHashA5A2D218 = 0xA5A2D218; // (text unknown)
constexpr uint32_t kHash74C30242 = 0x74C30242; // (text unknown)
constexpr uint32_t kHashE8D121CB = 0xE8D121CB; // (text unknown)
constexpr uint32_t kHash33DF698F = 0x33DF698F; // (text unknown)
constexpr uint32_t kObjStopAlongPath = 0xFE71453F; // Obj_StopAlongPath
constexpr uint32_t kObjStartAlongPath = 0xBD6C6807; // Obj_StartAlongPath
constexpr uint32_t kObjFollowPathLinked = 0x0DA2D86D; // Obj_FollowPathLinked
constexpr uint32_t kHashCC19C48A = 0xCC19C48A; // (text unknown)
constexpr uint32_t kHash8B669799 = 0x8B669799; // (text unknown)
constexpr uint32_t kObjMoveForward = 0xF389285D; // Obj_MoveForward
constexpr uint32_t kHash5CE058FD = 0x5CE058FD; // (text unknown)
constexpr uint32_t kObjMoveToPos = 0x84EC6600; // Obj_MoveToPos
constexpr uint32_t kHashEA81A32B = 0xEA81A32B; // (text unknown)
constexpr uint32_t kObjMoveToLink = 0x3BCAAC3F; // Obj_MoveToLink
constexpr uint32_t kObjStickToGround = 0xC5ECA638; // Obj_StickToGround
constexpr uint32_t kObjWaitJumpFinished = 0x1E094B84; // Obj_WaitJumpFinished
constexpr uint32_t kObjWaitRotate = 0xBDC4C878; // Obj_WaitRotate
constexpr uint32_t kObjWaitMove = 0xDBFDD02F; // Obj_WaitMove
constexpr uint32_t kObjWaitStop = 0x8D95F1E1; // Obj_WaitStop
constexpr uint32_t kHash3407AD3D = 0x3407AD3D; // (text unknown)
constexpr uint32_t kObjHover = 0xE606EBA2; // Obj_Hover
constexpr uint32_t kObjGetSpeed = 0xE76D73D2; // Obj_GetSpeed
constexpr uint32_t kObjJump = 0x1AE46261; // Obj_Jump
constexpr uint32_t kHash2FAD370D = 0x2FAD370D; // (text unknown)
} // namespace

std::array<script::MemberFunctionEntry, kMotionPathMemberFunctionCount>& motion_path_member_functions()
{
    static std::array<script::MemberFunctionEntry, kMotionPathMemberFunctionCount> motion_path_member_functions{{
        {.callsign_ = kObjIsMoving, .owner_kind_ = kMotion}, {.callsign_ = kObjIsRotating, .owner_kind_ = kMotion}, {.callsign_ = kObjStorePos, .owner_kind_ = kMotion}, {.callsign_ = kHashC8CC6820, .owner_kind_ = kMotion},
        {.callsign_ = kObjStopMoving, .owner_kind_ = kMotion}, {.callsign_ = kObjStopRotating, .owner_kind_ = kMotion}, {.callsign_ = kObjSetPathVelocity, .owner_kind_ = kMotion}, {.callsign_ = kObjSetPathMinStopVel, .owner_kind_ = kMotion},
        {.callsign_ = kObjSetPathAcceleration, .owner_kind_ = kMotion}, {.callsign_ = kObjSetPathDeceleration, .owner_kind_ = kMotion}, {.callsign_ = kObjRotate, .owner_kind_ = kMotion}, {.callsign_ = kObjRotX, .owner_kind_ = kMotion},
        {.callsign_ = kObjRotY, .owner_kind_ = kMotion}, {.callsign_ = kObjRotZ, .owner_kind_ = kMotion}, {.callsign_ = kObjPathHeading, .owner_kind_ = kMotion}, {.callsign_ = kHashAA3CB476, .owner_kind_ = kMotion},
        {.callsign_ = kObjGetNextObjOnPath, .owner_kind_ = kMotion}, {.callsign_ = kObjGetRandomLink, .owner_kind_ = kMotion}, {.callsign_ = kObjRandomPathMode, .owner_kind_ = kMotion}, {.callsign_ = kObjSetPathTurnDist, .owner_kind_ = kMotion},
        {.callsign_ = kObjFollowPath, .owner_kind_ = kMotion}, {.callsign_ = kObjLookAtObject, .owner_kind_ = kMotion}, {.callsign_ = kHash103BD253, .owner_kind_ = kMotion}, {.callsign_ = kObjLookAtNodeLinked, .owner_kind_ = kMotion},
        {.callsign_ = kHashA5A2D218, .owner_kind_ = kMotion}, {.callsign_ = kHash74C30242, .owner_kind_ = kMotion}, {.callsign_ = kHashE8D121CB, .owner_kind_ = kMotion}, {.callsign_ = kHash33DF698F, .owner_kind_ = kMotion},
        {.callsign_ = kObjStopAlongPath, .owner_kind_ = kMotion}, {.callsign_ = kObjStartAlongPath, .owner_kind_ = kMotion}, {.callsign_ = kObjFollowPathLinked, .owner_kind_ = kMotion}, {.callsign_ = kHashCC19C48A, .owner_kind_ = kMotion},
        {.callsign_ = kHash8B669799, .owner_kind_ = kMotion}, {.callsign_ = kObjMoveForward, .owner_kind_ = kMotion}, {.callsign_ = kHash5CE058FD, .owner_kind_ = kMotion}, {.callsign_ = kObjMoveToPos, .owner_kind_ = kMotion},
        {.callsign_ = kHashEA81A32B, .owner_kind_ = kMotion}, {.callsign_ = kObjMoveToLink, .owner_kind_ = kMotion}, {.callsign_ = kObjStickToGround, .owner_kind_ = kMotion}, {.callsign_ = kObjWaitJumpFinished, .owner_kind_ = kMotion},
        {.callsign_ = kObjWaitRotate, .owner_kind_ = kMotion}, {.callsign_ = kObjWaitMove, .owner_kind_ = kMotion}, {.callsign_ = kObjWaitStop, .owner_kind_ = kMotion}, {.callsign_ = kHash3407AD3D, .owner_kind_ = kMotion},
        {.callsign_ = kObjHover, .owner_kind_ = kMotion}, {.callsign_ = kObjGetSpeed, .owner_kind_ = kMotion}, {.callsign_ = kObjJump, .owner_kind_ = kMotion}, {.callsign_ = kHash2FAD370D, .owner_kind_ = kMotion},
        {.callsign_ = 0, .owner_kind_ = kMotion}}};
    return motion_path_member_functions;
}

namespace {

constexpr uint32_t kFps = 0xAAD7C80F, kIps = 0xA18B8F32;     // speeds: feet / inches a second
constexpr uint32_t kFpsps = 0xF414A5EA, kIpsps = 0x7644323B; // accelerations: feet / inches a second per second
constexpr uint32_t kOff = 0xD443A2BC;
constexpr uint32_t kInches = 0xF915B4F3;

constexpr float kTwelveInchScale = 12.0f;              // 0x0064546C
constexpr float kMilesToInches = 63360.0f;             // 0x006461E4
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
int wait_on(script::Script* script, KinematicsModule* component, script::BlockReason type)
{
    script->set_wait_on_component(type, component, 0);
    return 1;
}

} // namespace

// 0x00423130
void KinematicsModule::publish_method_table()
{
    script::register_member_function_table(motion_path_member_functions());
}

// 0x00423150
bool KinematicsModule::is_spinning() const { return (motion_flags_ & kMotionFlagRotating) != 0; }

// 0x00423520
bool KinematicsModule::picks_random_link() const { return (motion_flags2_ & kMotionPathRandom) != 0; }

// 0x00423530
void KinematicsModule::update_path_speed()
{
    if (!(motion_flags_ & kMotionFlagOnPath))
        return;
    if (motion_flags2_ & kMotionPathStopping) {
        if (path_stop_covered_ >= path_stop_distance_)
            return; // (NaN goes on)
        auto covered = static_cast<float>(static_cast<double>(step_seconds_) * path_speed_ + path_stop_covered_);
        path_stop_covered_ = covered;
        if (path_stop_distance_ == 0.0f || covered >= path_stop_distance_) {
            path_speed_ = 0.0f;
            return;
        }
        const double t = static_cast<double>(covered) / path_stop_distance_;
        path_speed_ = static_cast<float>((1.0 - t) * path_stop_start_speed_ + t * path_min_stop_speed_);
        return;
    }
    if (path_speed_ < path_max_speed_) {
        // Up to the path velocity.
        if (path_acceleration_ == 0.0f) {
            path_speed_ = path_max_speed_;
            return;
        }
        const double velocity = static_cast<double>(step_seconds_) * path_acceleration_ + path_speed_;
        path_speed_ = static_cast<float>(velocity);
        if (velocity > path_max_speed_)
            path_speed_ = path_max_speed_;
        return;
    }
    if (!(path_speed_ > path_max_speed_))
        return; // (there, or NaN)
    // Down to it.
    if (path_deceleration_ == 0.0f) {
        path_speed_ = path_max_speed_;
        return;
    }
    const double velocity = path_speed_ - static_cast<double>(step_seconds_) * path_deceleration_;
    path_speed_ = static_cast<float>(velocity);
    if (velocity < path_max_speed_)
        path_speed_ = path_max_speed_;
}

// 0x00423160
int KinematicsModule::is_moving_from_script(script::Struct*, script::Script*)
{
    return (motion_flags_ & kMotionFlagMoving) ? 1 : 0;
}

// 0x00423170
int KinematicsModule::is_rotating_from_script(script::Struct*, script::Script*)
{
    return (motion_flags_ & kMotionFlagRotating) ? 1 : 0;
}

// 0x00423180
int KinematicsModule::stop_moving(script::Struct*, script::Script*)
{
    motion_flags_ &= ~kMotionFlagMoving;
    return 1;
}

// 0x00423190
int KinematicsModule::stop_rotating(script::Struct*, script::Script*)
{
    motion_flags_ &= ~(kMotionFlagRotating | kMotionFlagRotatingAlt);
    return 1;
}

// 0x004231A0
int KinematicsModule::set_path_velocity(script::Struct* params, script::Script*)
{
    get_speed(params, &path_max_speed_, kFps, kIps);
    return 1;
}

// 0x00423210
int KinematicsModule::set_path_min_stop_vel(script::Struct* params, script::Script*)
{
    get_speed(params, &path_min_stop_speed_, kFps, kIps);
    return 1;
}

// 0x00423280
int KinematicsModule::set_path_acceleration(script::Struct* params, script::Script*)
{
    get_speed(params, &path_acceleration_, kFpsps, kIpsps);
    return 1;
}

// 0x004232F0
int KinematicsModule::set_path_deceleration(script::Struct* params, script::Script*)
{
    get_speed(params, &path_deceleration_, kFpsps, kIpsps);
    return 1;
}

// 0x00423360
int KinematicsModule::route_facing(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kOff))
        motion_flags2_ |= kMotionPathNoHeading;
    else
        motion_flags2_ &= ~kMotionPathNoHeading;
    return 1;
}

// 0x004233A0
int KinematicsModule::random_path_mode(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kOff))
        motion_flags2_ &= ~kMotionPathRandom;
    else
        motion_flags2_ |= kMotionPathRandom;
    return 1;
}

// 0x004233E0
int KinematicsModule::stop_along_path(script::Struct* params, script::Script*)
{
    path_stop_distance_ = 0.0f;
    path_stop_covered_ = 0.0f;
    if (path_min_stop_speed_ == 0.0f)
        path_min_stop_speed_ = kDefaultMinStopVelocity;
    params->fetch_float(0u, &path_stop_distance_, 0);
    if (!params->has_bare_flag(kInches)) {
        // (in feet: the stop's starting speed noted - not with `inches`, as the original)
        path_stop_start_speed_ = path_speed_;
        path_stop_distance_ *= kTwelveInchScale;
    }
    if (path_stop_distance_ == 0.0f)
        path_speed_ = 0.0f;
    motion_flags2_ |= kMotionPathStopping;
    return 1;
}

// 0x00423480
int KinematicsModule::start_along_path(script::Struct*, script::Script*)
{
    motion_flags2_ &= ~kMotionPathStopping;
    return 1;
}

// 0x00423490
int KinematicsModule::wait_jump_finished(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(kBlockReasonJumpFinished));
}

// 0x004234B0
int KinematicsModule::wait_rotate(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(kBlockReasonRotate));
}

// 0x004234D0
int KinematicsModule::wait_move(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(kBlockReasonMove));
}

// 0x004234F0
int KinematicsModule::wait_stop(script::Struct*, script::Script* script)
{
    return wait_on(script, this, static_cast<script::BlockReason>(kBlockReasonStop));
}

} // namespace objects
