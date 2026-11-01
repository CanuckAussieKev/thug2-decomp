// Pieces of the Walk component (GameObjects::FootMovement): see walk_physics.h. From the disassembly of 0x00524E10-
// 0x00529F62 (0x00525E40, 0x00525E90, 0x00525EA0, 0x00525F60, 0x00526060, 0x00528580-0x00528740 and 0x00528890
// aren't Ghidra functions).

#include "game/rider/walk_physics.h"

#include <cmath>
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
#include "game/rider/walk_camera.h"
#include "engine/objects/input_component.h"
#include "engine/objects/model_component.h"
#include "engine/objects/trigger_component.h"
#include "engine/objects/movable_contact.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "game/rider/core_physics.h"
#include "game/rider/tagging.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "game/skate/skate_module.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "platform/crt.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_score.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/down_cast.h"

namespace objects {

uint32_t g_walk_member_functions[114]{
    0x893213E5, 0x726E85AA, 0, 0, 0, 0, 0x5012082E, 0x726E85AA, 0, 0, 0, 0, 0x9A3CA853, 0x726E85AA, 0, 0, 0, 0,
    0x19702CA8, 0x726E85AA, 0, 0, 0, 0, 0x8FE2B013, 0x726E85AA, 0, 0, 0, 0, 0xCE64576C, 0x726E85AA, 0, 0, 0, 0,
    0x83E4BD70, 0x726E85AA, 0, 0, 0, 0, 0xAF04B983, 0x726E85AA, 0, 0, 0, 0, 0x56112C03, 0x726E85AA, 0, 0, 0, 0,
    0x9D3EEBE8, 0x726E85AA, 0, 0, 0, 0, 0xC6CD659E, 0x726E85AA, 0, 0, 0, 0, 0x55BF1CD6, 0x726E85AA, 0, 0, 0, 0,
    0xE9B1CDE8, 0x726E85AA, 0, 0, 0, 0, 0xAC7B36C8, 0x726E85AA, 0, 0, 0, 0, 0x7E01B923, 0x726E85AA, 0, 0, 0, 0,
    0x492EBA50, 0x726E85AA, 0, 0, 0, 0, 0xF8F754EF, 0x726E85AA, 0, 0, 0, 0, 0x8BC2C0EC, 0x726E85AA, 0, 0, 0, 0,
    0,          0x726E85AA, 0, 0, 0, 0};

namespace {

// One of the components it finds in Finalize: the object's component of `type`, kept in `slot`.
template <typename Module>
void find_module(GameEntity* object, Module*& slot, uint32_t type)
{
    slot = static_cast<Module*>(object->fetch_module(type));
}

using namespace walk_fields;

// Script names.
constexpr uint32_t kHangControlDebounceTime = 0xAC96D24B, kProbeElevation = 0x6DA7F696,
                   kStandPosSearchDepth = 0xAEBFA9CD, kRunAdjustRate = 0x3FB31DFC, kGravity = 0xA5E2DA58,
                   kCheatMoon = 0x9C8C6DF1, kMaxSlowWalkSpeed = 0xF3649996, kMaxFastWalkSpeed = 0x6A5805D8,
                   kMaxSlowRunSpeed = 0x1C94CC9C;
constexpr uint32_t kOff = 0xD443A2BC, kRun = 0xAF895B3F, kLedgeMoveFlag = 0x6384F1DA, kLadderMove = 0xA2BFE505,
                   kStandardSpeedName = 0xB2D59BAF, kCamIdKey = 0xC4E311FA, kWalkCameraType = 0xC7D37EC2,
                   kStateTimeName = 0x5AB23CC9, kSpeedScale = 0x092C388F, kWalkSlow = 0x1150CABB,
                   kWalkFast = 0x0131F2A2, kRunSlow = 0x5606D106, kRunFast = 0x4667E91F,
                   kHangInitAnimTypeName = 0x85FA9AC4, kStateDuration = 0x4B88E4E4, kPreviousStateName = 0xF78635DA,
                   kStateName = 0x5C6C2D04, kHangAngle = 0xEAD7D286, kOn = 0xF649D637, kSkid = 0x1D537EFF,
                   kStand = 0x9B46E749, kSkaterScore = 0xD262802F;

constexpr float kHangDebounceScale = -1000.0f;      // 0x0064604C
constexpr float kSeventhTurn = 0.8975979089736938f; // 0x00646968 (2 pi / 7)
constexpr float kDegrees = 57.2957763671875f;       // 0x00645468
constexpr float kMs = 0.001f;                       // 0x006461D8 (0.0010000000474974513f)
constexpr float kHalf = 0.5f;                       // 0x006453BC

// The state names (0x0064C618: WALKING_GROUND, WALKING_AIR, ...).
const uint32_t kWalkStateNames[5]{0x8CF3CB28, 0x9D1F1A2C, 0x74FFC46D, 0x1CB1F465, 0x79BE4637};
uint32_t get_state_name(int state) { return kWalkStateNames[state]; }

// The skate module's career (0x007CE478's +0x20).
RiderStoryProgress* get_career() { return modules::g_skate_module->career_.get(); }

// asin of `value` in degrees (x 57.29578), negated when `negate`, rounded to float once.
float asin_degrees(double value, bool negate)
{
    const double angle = std::asin(value);
    return static_cast<float>((negate ? -angle : angle) * kDegrees);
}

// get_turned_facing's turn (0x00528220): the cosine rounded to float.
void turn_flat(double angle, float x, float z, float* turned_x, float* turned_z)
{
    const double cosine = static_cast<float>(std::cos(angle));
    const double sine = std::sin(angle);
    *turned_x = static_cast<float>(cosine * x - sine * z);
    *turned_z = static_cast<float>(sine * x + cosine * z);
}

} // namespace

// 0x00525E40
void FootMovement::publish_method_table()
{
    script::register_member_function_table(cell::pointer<void>(kWalk), g_walk_member_functions, 0x18);
}

// 0x005288B0 (0x0052A290 the deleting one)
// (the members' destructors, as the original's order: the feeler's object let go, the cache unregistered, the
// contacts' feelers last to first)
FootMovement::~FootMovement() = default;

// 0x00526060
void FootMovement::configure_from_data(script::Struct* params)
{
    uint32_t id = 0;
    if (params->read_hash_value(kCamIdKey, &id, 0)) {
        auto* camera = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(id));
        camera_ = camera;
        walk_camera_ = core::down_cast<FootView>(camera->fetch_module(kWalkCameraType));
    }
    unknown710_ = 0;
}

// 0x00525FB0
void FootMovement::seal()
{
    find_module(entity_, input_, 0x27D7CD28);           // Input
    find_module(entity_, anim_blender_, 0x7149B753);    // AnimBlender
    find_module(entity_, model_, 0x286A8D26);           // Model
    find_module(entity_, trigger_, 0xE594F0A2);         // Trigger
    find_module(entity_, physics_control_, 0xD9151D4D); // SkaterPhysicsControl
    find_module(entity_, ride_, 0x408D2AA9);            // MovableContact
    find_module(entity_, skater_state_, 0x43686585);    // SkaterState
    find_module(entity_, core_physics_, 0x5BD63B29);    // SkaterCorePhysics
    find_module(entity_, tagging_, 0x380D909A);         // TaggingControl
}

// 0x00525E60
void FootMovement::choose_camera(GameEntity* camera)
{
    camera_ = camera;
    walk_camera_ = core::down_cast<FootView>(camera->fetch_module(kWalkCameraType));
}

// 0x005260E0
void FootMovement::change_phase(int state)
{
    if (state == state_)
        return;
    const int previous = state_;
    previous_state_ = previous;
    state_ = state;
    state_time_ = frame_clock::game_clock_elapsed() + g_clock_offset;
    if (previous == 0 || previous == 1) {
        barrier_timed_ = false;
        running_ = false;
    }
    if (previous == 1) {
        leap_duration_ = 0.0f;
        jump_velocity_ = {0.0f, 0.0f, 0.0f, 1.0f};
        off_vert_ = false;
    }
    if (state == 0) {
        skid_time_ = 10000.0f;
        unknown1874_ = true;
    }
    if (state == 4)
        climb_offset_ = {0.0f, 0.0f, 0.0f, 1.0f};
    if (state != 1)
        pushing_ = false;
    if (state == 0 || previous_state_ != 1)
        blocked_ = false;
    if (state == 2) {
        climb_scale_ = 0.0f;
        if (previous_state_ == 1) {
            // Just caught a ledge: the pad's hang controls ignored for hang_control_debounce_time.
            objects::PadSnapshot* pad = &input_->pad_primary_;
            float debounce = walk_parameter(kHangControlDebounceTime);
            uint32_t now = frame_clock::get_time();
            pad->lock_[0] = now - truncate(static_cast<double>(debounce) * kHangDebounceScale);
            debounce = walk_parameter(kHangControlDebounceTime);
            now = frame_clock::get_time();
            pad->lock_[1] = now - truncate(static_cast<double>(debounce) * kHangDebounceScale);
        }
    }
}

// 0x005260C0
void FootMovement::clear_model_offset() { model_->shift_ = {0.0f, 0.0f, 0.0f, 1.0f}; }

// 0x00524E10
bool FootMovement::using_dpad()
{
    const objects::PadInputModule* input = input_;
    if (!(input->pad_primary_.stick_[2] == 0.0f) || !(input->pad_primary_.stick_[3] == 0.0f))
        return false;
    return control_full_;
}

// 0x00524E60
maths::Vec4* FootMovement::get_camera_offset(maths::Vec4* out)
{
    constexpr float kCameraOffset = -18.0f; // 0x0064C614
    const int state = state_;
    bool offset = false;
    if (state == 1) {
        const int previous = previous_state_;
        offset = (previous == 2 || previous == 3) && static_cast<uint32_t>(clock_since(state_time_)) < 150;
    } else {
        offset = state > 1 && state <= 4;
    }
    if (!offset) {
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
        return out;
    }
    const maths::Vec4& facing = facing_;
    *out = {facing.x * kCameraOffset, facing.y * kCameraOffset, facing.z * kCameraOffset, facing.w * kCameraOffset};
    return out;
}

// 0x00528560
maths::Vec4* FootMovement::get_facing(maths::Vec4* out)
{
    *out = facing_;
    return out;
}

// 0x00528220
maths::Vec4* FootMovement::get_turned_facing(maths::Vec4* out, int index)
{
    const maths::Vec4& facing = facing_;
    float x = 0.0f, z = 0.0f;
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
    const modules::PointTally* score =
        core::down_cast<RiderPointKeeper>(entity_->fetch_module(kSkaterScore))->points_held_;
    if (truncate(static_cast<double>(score->pot_) * score->multiplier_) > 0) {
        run_adjustment_ = 1.0f;
        return;
    }
    const float rate = walk_parameter(kRunAdjustRate);
    double adjustment = run_adjustment_ - frame_clock::get_slomo_frame_seconds() * rate;
    if (adjustment < 0.0)
        adjustment = 0.0f;
    run_adjustment_ = static_cast<float>(adjustment);
}

// 0x00528400
void FootMovement::clamp_skater_speed(uint32_t stop_anim)
{
    if (state_ != 0)
        return;
    if (static_cast<uint32_t>(clock_since(state_time_)) < 100) {
        last_speed_ = speed_;
        return;
    }
    const float speed = speed_;
    const float last = last_speed_;
    const float limited = speed < last ? speed : last;
    speed_ = limited;
    last_speed_ = speed;
    if (anim_state_ == kRun && limited == 0.0f)
        anim_state_ = stop_anim == kSkid ? kSkid : kStand;
}

// 0x005284C0
double FootMovement::get_gravity()
{
    RiderStoryProgress* career = get_career();
    if (!career->is_code_on(kCheatMoon))
        return walk_parameter(kGravity);
    get_career()->set_global_flag(script::fetch_int_value(kCheatMoon));
    online::g_cheat_flag = true;
    return static_cast<double>(walk_parameter(kGravity)) * kHalf;
}

// 0x00528280
bool FootMovement::find_stand_position(const maths::Vec4* position, maths::Vec4* out, render::ProbeRay* feeler)
{
    const float height = walk_parameter(kProbeElevation);
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
bool FootMovement::walk_ground(script::Struct* /*params*/, script::Script* /*script*/) { return state_ == 0; }

// 0x00525EA0
bool FootMovement::walk_scale_anim_speed(script::Struct* params, script::Script* /*script*/)
{
    if (params->has_bare_flag(kOff)) {
        if (anim_speed_scaling_) {
            anim_speed_scaling_ = 0;
            float one = 0.0f;
            uint32_t bits = 1;
            std::memcpy(&one, &bits, 4);
            anim_blender_->first_channel_47e_850(1.0f, 0.0f, one, 0.0f);
        }
    } else if (params->has_bare_flag(kRun)) {
        anim_speed_scaling_ = 1;
    } else if (params->has_bare_flag(kLedgeMoveFlag)) {
        anim_speed_scaling_ = 2;
    } else if (params->has_bare_flag(kLadderMove)) {
        anim_speed_scaling_ = 3;
        ladder_sign_ = -1.0f;
    }
    params->fetch_float(kStandardSpeedName, &standard_speed_, 0);
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
    script->args_->put_int(kStateTimeName, clock_since(state_time_));
    return true;
}

// 0x005285C0
bool FootMovement::walk_get_speed_scale(script::Struct* /*params*/, script::Script* script)
{
    const float speed = speed_;
    uint32_t scale = 0;
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
    script->args_->put_name_key(kHangInitAnimTypeName, hang_init_anim_type_);
    return true;
}

// 0x005286C0
bool FootMovement::walk_get_state_duration(script::Struct* /*params*/, script::Script* script)
{
    auto elapsed = static_cast<uint32_t>(clock_since(state_time_));
    script->args_->push_float(kStateDuration, static_cast<float>(static_cast<double>(elapsed) * kMs));
    return true;
}

// 0x00528710
bool FootMovement::walk_get_previous_state(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_name_key(kPreviousStateName, get_state_name(previous_state_));
    return true;
}

// 0x00528740
bool FootMovement::walk_get_state(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_name_key(kStateName, get_state_name(state_));
    return true;
}

// 0x00528770
bool FootMovement::walk_get_hang_angle(script::Struct* /*params*/, script::Script* script)
{
    // The angle the rail between the two hang nodes climbs (asin of its direction's y, in degrees), signed by
    // which way along it the skater faces.
    GrindRailHub* rails = rail_manager_;
    maths::Vec4 from{}, to{};
    const maths::Vec4* a = rails->get_absolute_pos(&from, hang_node_);
    const maths::Vec4* b = rails->get_absolute_pos(&to, hang_next_node_);
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    const double dz_full = static_cast<double>(b->z) - a->z;
    auto dz = static_cast<float>(dz_full);
    // (z's square from its unrounded difference times its rounded one, as the original's x87 code)
    const double length = std::sqrt((dz_full * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    const maths::Vec4& facing = facing_;
    const bool negate = static_cast<double>(dz) * facing.x - static_cast<double>(dx) * facing.z < 0.0;
    script->args_->push_float(kHangAngle, asin_degrees(dy, negate));
    return true;
}

// 0x00528860
bool FootMovement::walk_set_forward_control_lock(script::Struct* params, script::Script* /*script*/)
{
    locked_to_heading_ = params->has_bare_flag(kOn);
    return true;
}

} // namespace objects
