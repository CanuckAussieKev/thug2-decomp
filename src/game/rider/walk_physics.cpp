// Pieces of the Walk component (GameObjects::FootMovement): see walk_physics.h. From the disassembly of 0x00524E10-
// 0x00529F62 (0x00525E40, 0x00525E90, 0x00525EA0, 0x00525F60, 0x00526060, 0x00528580-0x00528740 and 0x00528890
// aren't Ghidra functions).

#include "game/rider/walk_physics.h"

#include <cstdint>
#include <cstring>

#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/walk_fields.h"
#include "engine/maths/x87_math.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "game/skate/skate_module.h"
#include "engine/core/code_address.h"
#include "engine/net/gamenet_manager.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "platform/crt.h"

namespace objects {

uint32_t g_walk_member_functions[114]{
    0x893213E5, 0x726E85AA, 0, 0, 0, 0,
    0x5012082E, 0x726E85AA, 0, 0, 0, 0,
    0x9A3CA853, 0x726E85AA, 0, 0, 0, 0,
    0x19702CA8, 0x726E85AA, 0, 0, 0, 0,
    0x8FE2B013, 0x726E85AA, 0, 0, 0, 0,
    0xCE64576C, 0x726E85AA, 0, 0, 0, 0,
    0x83E4BD70, 0x726E85AA, 0, 0, 0, 0,
    0xAF04B983, 0x726E85AA, 0, 0, 0, 0,
    0x56112C03, 0x726E85AA, 0, 0, 0, 0,
    0x9D3EEBE8, 0x726E85AA, 0, 0, 0, 0,
    0xC6CD659E, 0x726E85AA, 0, 0, 0, 0,
    0x55BF1CD6, 0x726E85AA, 0, 0, 0, 0,
    0xE9B1CDE8, 0x726E85AA, 0, 0, 0, 0,
    0xAC7B36C8, 0x726E85AA, 0, 0, 0, 0,
    0x7E01B923, 0x726E85AA, 0, 0, 0, 0,
    0x492EBA50, 0x726E85AA, 0, 0, 0, 0,
    0xF8F754EF, 0x726E85AA, 0, 0, 0, 0,
    0x8BC2C0EC, 0x726E85AA, 0, 0, 0, 0,
    0, 0x726E85AA, 0, 0, 0, 0};

namespace {

using namespace walk_fields;

extern void** const kVtable;

// Script names.
constexpr uint32_t kHangControlDebounceTime = 0xAC96D24B,
                   kProbeElevation = 0x6DA7F696, kStandPosSearchDepth = 0xAEBFA9CD, kRunAdjustRate = 0x3FB31DFC,
                   kGravity = 0xA5E2DA58, kCheatMoon = 0x9C8C6DF1, kMaxSlowWalkSpeed = 0xF3649996,
                   kMaxFastWalkSpeed = 0x6A5805D8, kMaxSlowRunSpeed = 0x1C94CC9C;
constexpr uint32_t kOff = 0xD443A2BC, kRun = 0xAF895B3F, kLedgeMoveFlag = 0x6384F1DA, kLadderMove = 0xA2BFE505,
                   kStandardSpeedName = 0xB2D59BAF, kCamIdKey = 0xC4E311FA, kWalkCameraType = 0xC7D37EC2,
                   kStateTimeName = 0x5AB23CC9, kSpeedScale = 0x092C388F, kWalkSlow = 0x1150CABB,
                   kWalkFast = 0x0131F2A2, kRunSlow = 0x5606D106, kRunFast = 0x4667E91F,
                   kHangInitAnimTypeName = 0x85FA9AC4, kStateDuration = 0x4B88E4E4, kPreviousStateName = 0xF78635DA,
                   kStateName = 0x5C6C2D04, kHangAngle = 0xEAD7D286, kOn = 0xF649D637, kSkid = 0x1D537EFF,
                   kStand = 0x9B46E749, kSkaterScore = 0xD262802F;

constexpr float kHangDebounceScale = -1000.0f; // 0x0064604C
constexpr float kSeventhTurn = 0.8975979089736938f; // 0x00646968 (2 pi / 7)
constexpr float kDegrees = 57.2957763671875f;  // 0x00645468
constexpr float kMs = 0.001f;                  // 0x006461D8 (0.0010000000474974513f)
constexpr float kHalf = 0.5f;                  // 0x006453BC

// The state names (0x0064C618: WALKING_GROUND, WALKING_AIR, ...).
const uint32_t kWalkStateNames[5]{0x8CF3CB28, 0x9D1F1A2C, 0x74FFC46D, 0x1CB1F465, 0x79BE4637};
uint32_t get_state_name(int state) { return kWalkStateNames[state]; }

// The skate module's career (0x007CE478's +0x20).
RiderStoryProgress* get_career() { return get_at<RiderStoryProgress*>(static_cast<void*>(modules::g_skate_module), 0x20); }

// asin (the game's C runtime) of `value` in degrees (x 57.29578), negated when `negate` - the result kept in the
// register until it is rounded to float, as the original.
float asin_degrees(double value, bool negate)
{
    float result;
    void* function = crt::asin_x87();
    float degrees = kDegrees;
    __asm {
        fld value
        call function
        cmp negate, 0
        je positive
        fchs
    positive:
        fmul degrees
        fstp result
    }
    return result;
}

// get_turned_facing's turn (0x00528220): the cosine rounded to float, the sine kept in the register.
void turn_flat(double angle, float x, float z, float* turned_x, float* turned_z)
{
    float cosine, rx, rz;
    __asm {
        fld angle
        fld st(0)
        fcos
        fstp cosine
        fsin                // sin
        fld cosine
        fmul x              // c*x, sin
        fld st(1)
        fmul z              // sin*z, c*x, sin
        fsubp st(1), st     // c*x - sin*z, sin
        fstp rx             // sin
        fmul x              // sin*x
        fld cosine
        fmul z              // c*z, sin*x
        faddp st(1), st
        fstp rz
    }
    *turned_x = rx;
    *turned_z = rz;
}

} // namespace

// 0x00525E40
void __cdecl FootMovement::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kWalk), g_walk_member_functions,
                                        0x18);
}

// 0x00528880
WalkContact* WalkContact::make_new()
{
    get_at<render::ProbeRay>(this, 0x1C).make_new();
    return this;
}

// 0x00528890
void WalkContact::teardown() { get_at<SharedRef<GameEntity>>(this, 0xB4).get_release(); }

// 0x005288B0
void FootMovement::destroy_body()
{
    vtable_ = kVtable;
    get_at<SharedRef<Entity>>(this, kObjectRef).get_release();
    get_at<render::HitMeshCache>(this, kHitMeshStore).destruct();
    for (int i = 7; i >= 0; --i)
        get_at<WalkContact>(this, kContacts + i * 0xCC).teardown();
    ObjectModule::destroy_body();
}

// 0x00526060
void FootMovement::configure_from_data(script::Struct* params)
{
    uint32_t id;
    if (params->read_hash_value(kCamIdKey, &id, 0)) {
        auto* camera = static_cast<GameEntity*>(object_hub_slot()->look_up_entity(id));
        get_at<GameEntity*>(this, kCamera) = camera;
        get_at<ObjectModule*>(this, kWalkCamera) = camera->fetch_module(kWalkCameraType);
    }
    get_at<int>(this, kFrames710) = 0;
}

// 0x00525FB0
void FootMovement::seal()
{
    static const uint32_t kComponents[] = {0x27D7CD28, 0x7149B753, 0x286A8D26, 0xE594F0A2, 0xD9151D4D,
                                           0x408D2AA9, 0x43686585, 0x5BD63B29, 0x380D909A};
    for (int i = 0; i < 9; ++i)
        get_at<ObjectModule*>(this, kInput + 4 * i) = entity_->fetch_module(kComponents[i]);
}

// 0x00525E60
void FootMovement::choose_camera(GameEntity* camera)
{
    get_at<GameEntity*>(this, kCamera) = camera;
    get_at<ObjectModule*>(this, kWalkCamera) = camera->fetch_module(kWalkCameraType);
}

// 0x005260E0
void FootMovement::change_phase(int state)
{
    if (state == get_at<int>(this, kState))
        return;
    int previous = get_at<int>(this, kState);
    get_at<int>(this, kPreviousState) = previous;
    get_at<int>(this, kState) = state;
    get_at<int>(this, kStateTime) = frame_clock::game_clock_elapsed() + g_clock_offset;
    if (previous == 0 || previous == 1) {
        get_at<bool>(this, kFlag17a8) = false;
        get_at<bool>(this, kRunning) = false;
    }
    if (previous == 1) {
        get_at<float>(this, kLeapDuration) = 0.0f;
        get_at<maths::Vec4>(this, kJumpVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        get_at<bool>(this, kOnRail) = false;
    }
    if (state == 0) {
        get_at<float>(this, kHeight1870) = 10000.0f;
        get_at<bool>(this, kFlag1874) = true;
    }
    if (state == 4)
        get_at<maths::Vec4>(this, kClimbOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
    if (state != 1)
        get_at<bool>(this, kPushing) = false;
    if (state == 0 || get_at<int>(this, kPreviousState) != 1)
        get_at<bool>(this, kFlag186c) = false;
    if (state == 2) {
        get_at<float>(this, kClimbScale) = 0.0f;
        if (get_at<int>(this, kPreviousState) == 1) {
            // Just caught a ledge: the pad's hang controls ignored for hang_control_debounce_time.
            uint8_t* pad = get_at<uint8_t*>(this, kInput) + 0x20;
            float debounce = walk_parameter(kHangControlDebounceTime);
            uint32_t now = frame_clock::get_time();
            get_at<uint32_t>(pad, 0x230) = now - truncate(static_cast<double>(debounce) * kHangDebounceScale);
            debounce = walk_parameter(kHangControlDebounceTime);
            now = frame_clock::get_time();
            get_at<uint32_t>(pad, 0x234) = now - truncate(static_cast<double>(debounce) * kHangDebounceScale);
        }
    }
}

// 0x005260C0
void FootMovement::clear_model_offset()
{
    get_at<maths::Vec4>(get_at<void*>(this, kModel), 0x1F8) = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00524E10
bool FootMovement::using_dpad()
{
    uint8_t* input = get_at<uint8_t*>(this, kInput);
    if (!(get_at<float>(input, 0x228) == 0.0f) || !(get_at<float>(input, 0x22C) == 0.0f))
        return false;
    return get_at<bool>(this, 0x98);
}

// 0x00524E60
maths::Vec4* FootMovement::get_camera_offset(maths::Vec4* out)
{
    constexpr float kCameraOffset = -18.0f; // 0x0064C614
    int state = get_at<int>(this, kState);
    bool offset;
    if (state == 1) {
        int previous = get_at<int>(this, kPreviousState);
        offset = (previous == 2 || previous == 3) && static_cast<uint32_t>(clock_since(get_at<int>(this, kStateTime))) < 150;
    } else {
        offset = state > 1 && state <= 4;
    }
    if (!offset) {
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
        return out;
    }
    const maths::Vec4& facing = get_at<maths::Vec4>(this, kFacing);
    *out = {facing.x * kCameraOffset, facing.y * kCameraOffset, facing.z * kCameraOffset, facing.w * kCameraOffset};
    return out;
}

// 0x00528560
maths::Vec4* FootMovement::get_facing(maths::Vec4* out)
{
    *out = get_at<maths::Vec4>(this, kFacing);
    return out;
}

// 0x00528220
maths::Vec4* FootMovement::get_turned_facing(maths::Vec4* out, int index)
{
    const maths::Vec4& facing = get_at<maths::Vec4>(this, kFacing);
    float x, z;
    turn_flat(static_cast<double>(index) * kSeventhTurn, facing.x, facing.z, &x, &z);
    out->y = 0.0f;
    out->w = 1.0f;
    out->x = x;
    out->z = z;
    return out;
}

// 0x00528370
void FootMovement::update_run_adjustment()
{
    uint8_t* score = get_at<uint8_t*>(entity_->fetch_module(kSkaterScore), 0x18);
    if (truncate(static_cast<double>(get_at<int>(score, 0x14)) * get_at<float>(score, 0x60)) > 0) {
        get_at<float>(this, kRunAdjustment) = 1.0f;
        return;
    }
    float rate = walk_parameter(kRunAdjustRate);
    double adjustment = get_at<float>(this, kRunAdjustment) - frame_clock::get_slomo_frame_seconds() * rate;
    if (adjustment < 0.0)
        adjustment = 0.0f;
    get_at<float>(this, kRunAdjustment) = static_cast<float>(adjustment);
}

// 0x00528400
void FootMovement::clamp_skater_speed(uint32_t stop_anim)
{
    if (get_at<int>(this, kState) != 0)
        return;
    if (static_cast<uint32_t>(clock_since(get_at<int>(this, kStateTime))) < 100) {
        get_at<float>(this, kLastSpeed) = get_at<float>(this, kSpeed);
        return;
    }
    float speed = get_at<float>(this, kSpeed);
    float last = get_at<float>(this, kLastSpeed);
    float limited = speed < last ? speed : last;
    get_at<float>(this, kSpeed) = limited;
    get_at<float>(this, kLastSpeed) = speed;
    if (get_at<uint32_t>(this, kAnimState) == kRun && limited == 0.0f)
        get_at<uint32_t>(this, kAnimState) = stop_anim == kSkid ? kSkid : kStand;
}

// 0x005284C0
double __cdecl FootMovement::get_gravity()
{
    RiderStoryProgress* career = get_career();
    if (!career->is_code_on(kCheatMoon))
        return walk_parameter(kGravity);
    get_career()->set_global_flag(script::fetch_int_value(kCheatMoon));
    online::g_cheat_flag = true;
    return static_cast<double>(walk_parameter(kGravity)) * kHalf;
}

// 0x00528280
bool __stdcall FootMovement::find_stand_position(const maths::Vec4* position, maths::Vec4* out, render::ProbeRay* feeler)
{
    float height = walk_parameter(kProbeElevation);
    feeler->line_.start = *position;
    feeler->line_.start.y = static_cast<float>(static_cast<double>(height) + height + feeler->line_.start.y);
    feeler->line_.end = *position;
    feeler->line_.end.y = feeler->line_.end.y - walk_parameter(kStandPosSearchDepth);
    if (feeler->get_hit_shape(true, false)) {
        *out = feeler->contact_pos_;
        return true;
    }
    *out = *position;
    return false;
}

// 0x00525E90
bool FootMovement::walk_ground(script::Struct* /*params*/, script::Script* /*script*/)
{
    return get_at<int>(this, kState) == 0;
}

// 0x00525EA0
bool FootMovement::walk_scale_anim_speed(script::Struct* params, script::Script* /*script*/)
{
    if (params->has_bare_flag(kOff)) {
        if (get_at<int>(this, kAnimSpeedScaling)) {
            get_at<int>(this, kAnimSpeedScaling) = 0;
            float one;
            uint32_t bits = 1;
            std::memcpy(&one, &bits, 4);
            get_at<AnimBlenderComponent*>(this, kAnimBlender)->first_channel_47e_850(1.0f, 0.0f, one, 0.0f);
        }
    } else if (params->has_bare_flag(kRun)) {
        get_at<int>(this, kAnimSpeedScaling) = 1;
    } else if (params->has_bare_flag(kLedgeMoveFlag)) {
        get_at<int>(this, kAnimSpeedScaling) = 2;
    } else if (params->has_bare_flag(kLadderMove)) {
        get_at<int>(this, kAnimSpeedScaling) = 3;
        get_at<float>(this, kLadderSign) = -1.0f;
    }
    params->fetch_float(kStandardSpeedName, &get_at<float>(this, kStandardSpeed), 0);
    return true;
}

// 0x00525F60
bool FootMovement::walk_release_hang(script::Struct* /*params*/, script::Script* /*script*/)
{
    end_drop_from_hang();
    return true;
}

// 0x00528580
bool FootMovement::walk_get_state_time(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_int(kStateTimeName, clock_since(get_at<int>(this, kStateTime)));
    return true;
}

// 0x005285C0
bool FootMovement::walk_get_speed_scale(script::Struct* /*params*/, script::Script* script)
{
    float speed = get_at<float>(this, kSpeed);
    uint32_t scale;
    if (speed < walk_parameter(kMaxSlowWalkSpeed))
        scale = kWalkSlow;
    else if (speed < walk_parameter(kMaxFastWalkSpeed))
        scale = kWalkFast;
    else if (speed < walk_parameter(kMaxSlowRunSpeed))
        scale = kRunSlow;
    else
        scale = kRunFast;
    script->args_->put_name_key(kSpeedScale, scale);
    return true;
}

// 0x005286A0
bool FootMovement::walk_get_hang_init_anim_type(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_name_key(kHangInitAnimTypeName, get_at<uint32_t>(this, kHangInitAnimType));
    return true;
}

// 0x005286C0
bool FootMovement::walk_get_state_duration(script::Struct* /*params*/, script::Script* script)
{
    uint32_t elapsed = static_cast<uint32_t>(clock_since(get_at<int>(this, kStateTime)));
    script->args_->push_float(kStateDuration, static_cast<float>(static_cast<double>(elapsed) * kMs));
    return true;
}

// 0x00528710
bool FootMovement::walk_get_previous_state(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_name_key(kPreviousStateName, get_state_name(get_at<int>(this, kPreviousState)));
    return true;
}

// 0x00528740
bool FootMovement::walk_get_state(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_name_key(kStateName, get_state_name(get_at<int>(this, kState)));
    return true;
}

// 0x00528770
bool FootMovement::walk_get_hang_angle(script::Struct* /*params*/, script::Script* script)
{
    // The angle the rail between the two hang nodes climbs (asin of its direction's y, in degrees), signed by
    // which way along it the skater faces.
    GrindRailHub* rails = get_at<GrindRailHub*>(this, kRailManager);
    maths::Vec4 from, to;
    const maths::Vec4* a = rails->get_absolute_pos(&from, get_at<const GrindPoint*>(this, kHangNode));
    const maths::Vec4* b = rails->get_absolute_pos(&to, get_at<const GrindPoint*>(this, kHangNextNode));
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    double dz_full = static_cast<double>(b->z) - a->z;
    float dz = static_cast<float>(dz_full);
    // (z's square from its unrounded difference times its rounded one, as the original's x87 code)
    double length = x87::get_sqrt((dz_full * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    const maths::Vec4& facing = get_at<maths::Vec4>(this, kFacing);
    bool negate = static_cast<double>(dz) * facing.x - static_cast<double>(dx) * facing.z < 0.0;
    script->args_->push_float(kHangAngle, asin_degrees(dy, negate));
    return true;
}

// 0x00528860
bool FootMovement::walk_set_forward_control_lock(script::Struct* params, script::Script* /*script*/)
{
    get_at<bool>(this, kLockedToHeading) = params->has_bare_flag(kOn);
    return true;
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&FootMovement::teardown,
         &FootMovement::tick_frame,
         &objects::FootMovement::configure_from_data,
         &objects::FootMovement::seal,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         &objects::ObjectModule::dispatch_script_method,
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace Obj
