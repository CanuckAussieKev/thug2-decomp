// The skater camera component's life, parameters and small pieces (0x004F9EB0-0x004FA858), and the skater's
// cleanup-state component (0x004FBD90-0x004FBE76).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include "game/rider/skater_camera_component.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/camera_util.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/rider.h"
#include "game/rider/walk_camera.h"
#include "platform/stopwatch.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/skater_camera_component.h"

namespace objects {

namespace {

using skater::physics::get_at;
using skater::physics::x87_sin;

constexpr uint32_t kSkaterCamera = 0x5E43A604;   // skatercamera (the component type)
constexpr uint32_t kCleanupState = 0xD8CC3428;   // SkaterCleanupState (the component type)
constexpr uint32_t kSkaterState = 0x43686585;    // SkaterState (a component)
constexpr uint32_t kPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterId = 0x4C48DA58;       // (configure_from_data's object id; name not recovered)
constexpr uint32_t kShake = 0x75A516D3;          // (the shake member function; name not recovered)
constexpr uint32_t kSetMode = 0xD7867FFE;        // (the camera mode member function; name not recovered)
constexpr uint32_t kMode = 0x6835B854;           // (its parameter; name not recovered)

// The shake's parameters (the game's strings).
const char* const kDuration = "duration";  // "duration"
const char* const kVertAmp = "vert_amp";   // "vert_amp"
const char* const kHorizAmp = "horiz_amp"; // "horiz_amp"
const char* const kVertVel = "vert_vel";   // "vert_vel"
const char* const kHorizVel = "horiz_vel"; // "horiz_vel"

// The game's constants.
constexpr float kTwoPi = 6.2831854820251465f;     // 0x0064545C
constexpr float kTiltBias = 0.628f;               // 0x0064BB00
constexpr float kTiltScale = 1.5923566818237305f; // 0x0064BAFC
constexpr float kTiltRange = 0.6f;                // 0x006463C4
constexpr float kTiltBase = 0.4f;                 // 0x0064BAF8
constexpr float kZoomedHeight = 3.0f;             // 0x006467A8

// In vert air: the state's +0x30 on, +0x48 and +0x78 off.
bool vert_air(const uint8_t* state) { return state[0x30] && !state[0x48] && !state[0x78]; }

} // namespace

// 0x004F9EB0
bool RiderChaseView::get_look_down()
{
    if (!rider_ref_)
        return false;
    uint8_t* state = state_ref_;
    if (get_at<int>(state, 0x1C))
        return false;
    if (get_at<int>(state, 0x18) == 3 || vert_air(state))
        return true;
    return state[0xF4] != 0;
}

// 0x004F9F00
void RiderChaseView::apply_shake(maths::Vec4* position, const maths::Mat4* orientation)
{
    double left = shake_left_;
    double fraction = left / shake_length_;
    double across = x87_sin(shake_rate_x_ * left * kTwoPi) * shake_amp_x_ * fraction;
    double up = x87_sin(shake_rate_y_ * left * kTwoPi) * shake_amp_y_ * fraction;
    const maths::Vec4& row0 = orientation->row[0];
    const maths::Vec4& row1 = orientation->row[1];
    float y0 = static_cast<float>(up * row0.y);
    float z0 = static_cast<float>(up * row0.z);
    float w0 = static_cast<float>(up * row0.w);
    float x = static_cast<float>(up * row0.x + position->x);
    position->x = x;
    float y = y0 + position->y;
    position->y = y;
    float z = z0 + position->z;
    position->z = z;
    float w = w0 + position->w;
    position->w = w;
    float x1 = static_cast<float>(across * row1.x);
    float y1 = static_cast<float>(across * row1.y);
    float z1 = static_cast<float>(across * row1.z);
    position->x = x + x1;
    position->y = y + y1;
    position->z = z + z1;
    position->w = static_cast<float>(w + across * row1.w);
    double now = shake_left_ - frame_clock::get_slomo_frame_seconds();
    shake_left_ = static_cast<float>(now);
    if (now < 0.0)
        shake_left_ = 0.0f;
}

// 0x004FA020
void RiderChaseView::fetch_goal(maths::Vec4* out, bool instant)
{
    float seconds = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    GameEntity* skater = rider_ref_;
    if (!skater) {
        *out = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    maths::Vec4 position = skater->at_point_;
    if (rate_time_ > 0.0f) {
        double left = rate_time_ - static_cast<double>(seconds);
        rate_time_ = static_cast<float>(left);
        if (left > 0.0) {
            rate_scale_ = static_cast<float>(static_cast<double>(seconds) * rate_rise_ + rate_scale_);
        } else {
            rate_time_ = 0.0f;
            rate_scale_ = 1.0f;
        }
    } else {
        rate_scale_ = 1.0f;
    }
    uint8_t* state = state_ref_;
    float across;
    double up;
    bool snap = instant;
    if (vert_air(state)) {
        across = static_cast<float>(
            frame_blend(static_cast<float>(vert_air_lerp_xz_ * static_cast<double>(rate_scale_)), seconds));
        up = frame_blend(static_cast<float>(halfpipe_up_blend_ * static_cast<double>(rate_scale_)), seconds);
    } else {
        across =
            static_cast<float>(frame_blend(static_cast<float>(lerp_xz_ * static_cast<double>(rate_scale_)), seconds));
        up = frame_blend(static_cast<float>(lerp_y_ * static_cast<double>(rate_scale_)), seconds);
        state = state_ref_;
        if (state[0x98])
            up = 1.0;
        if (state[0xA0])
            snap = true;
    }
    maths::Vec4& target = aim_point_;
    if (snap) {
        target = position;
    } else {
        float old_y = target.y;
        float old_z = target.z;
        target.w = 1.0f;
        target.x = static_cast<float>((static_cast<double>(position.x) - target.x) * across + target.x);
        target.y = static_cast<float>((static_cast<double>(position.y) - old_y) * up + old_y);
        target.z = static_cast<float>((static_cast<double>(position.z) - old_z) * across + old_z);
    }
    *out = target;
}

// 0x004FA230
void RiderChaseView::get_zoomed_offsets(float* height, float* distance)
{
    if (!rider_ref_)
        return;
    zoom_target_ = 1.0f;
    uint8_t* state = state_ref_;
    if (!vert_zoom_on_ && vert_air(state) && state[0xE0])
        vert_zoom_on_ = 1;
    else if (!vert_air(state_ref_))
        vert_zoom_on_ = 0;
    state = state_ref_;
    if (vert_zoom_on_)
        zoom_target_ = big_air_trick_zoom_;
    else if (get_at<int>(state, 0x18) == 4)
        zoom_target_ = rail_zoom_factor_;
    else if (get_at<int>(state, 0x18) == 3)
        zoom_target_ = lip_trick_zoom_;
    FreeLookModule* look_around = free_look_module_;
    if (look_around->turning_ && std::bit_cast<uint32_t>(look_around->factor_) != 0x3F800000u)
        zoom_target_ = static_cast<float>(static_cast<double>(look_around->factor_) * zoom_target_);
    double eased = (static_cast<double>(zoom_target_) - zoom_) * zoom_ease_rate_ + zoom_;
    zoom_ = static_cast<float>(eased);
    double behind = eased * behind_;
    *distance = static_cast<float>(behind);
    float tilt = free_look_module_->lean_angle_;
    if (tilt < 0.0f)
        *distance = static_cast<float>(((tilt + static_cast<double>(kTiltBias)) * kTiltScale * kTiltRange + kTiltBase) *
                                       behind);
    float above = get_at<int>(state_ref_, 0x18) == 3 ? lip_trick_above_ : above_;
    if (zoom_ < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kZoomedHeight) * zoom_ + kZoomedHeight);
    else
        *height = above;
}

// 0x004FA3C0
void RiderChaseView::bind_rider(GameEntity* skater)
{
    rider_ref_ = skater;
    if (!skater) {
        state_ref_ = nullptr;
        control_ = nullptr;
        return;
    }
    state_ref_ = reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterState));
    control_ = reinterpret_cast<uint8_t*>(rider_ref_->fetch_module(kPhysicsControl));
    instant_frames_ = 3;
}

// 0x004FA420
void RiderChaseView::save_state(WalkCameraState* state)
{
    std::memcpy(&state->orientation, orientation_, sizeof(orientation_));
    state->orientation.row[3] = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    state->target = aim_point_;
    state->turn_cosine_ = turn_cosine_;
    state->zoom = zoom_;
}

// 0x004FA4B0
RiderChaseView::RiderChaseView()
{
    kind_tag_ = kSkaterCamera;
    mode_sel_ = 0;
    if (!memory::g_manager_ready)
        memory::ensure_manager();
    void* memory = memory::Hub::get_pull()->carve(0x98, 1, nullptr);
    // (The interpolator's constructor does nothing: folded with many empty ones at 0x005902E0.)
    arc_blender_ = static_cast<maths::ArcRotationBlender*>(memory);
    zoom_ = 1.0f;
    turn_cosine_ = 1.0f;
    vert_air_lerp_xz_ = 1.0f;
    halfpipe_up_blend_ = 1.0f;
    rider_ref_ = nullptr;
    state_ref_ = nullptr;
    control_ = nullptr;
    zoom_ease_rate_ = 0.0625f;
    lerp_xz_ = 0.25f;
    lerp_y_ = 0.5f;
    look_rate_ = 0.1f;
}

// 0x004FA5C0
void RiderChaseView::configure_from_data(script::Struct* params)
{
    maths::Mat4& matrix = entity_->basis_frame_;
    matrix.row[0] = maths::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = maths::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = maths::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::memcpy(orientation_, &matrix, sizeof(orientation_));
    uint32_t id = 0;
    params->read_hash_value(kSkaterId, &id, 1);
    bind_rider(static_cast<GameEntity*>(reinterpret_cast<ObjectHubBase*>(g_composite_registry)->look_up_entity(id)));
}

// 0x004FA680
int RiderChaseView::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    if (name == kShake) {
        float duration = 0.0f, vert_amp = 0.0f, horiz_amp = 0.0f, vert_vel = 0.0f, horiz_vel = 0.0f;
        params->fetch_float(kDuration, &duration, 0);
        params->fetch_float(kVertAmp, &vert_amp, 0);
        params->fetch_float(kHorizAmp, &horiz_amp, 0);
        params->fetch_float(kVertVel, &vert_vel, 0);
        params->fetch_float(kHorizVel, &horiz_vel, 0);
        shake_amp_x_ = vert_amp;
        shake_left_ = duration;
        shake_length_ = duration;
        shake_rate_y_ = horiz_vel;
        shake_amp_y_ = horiz_amp;
        shake_rate_x_ = vert_vel;
        return 1;
    }
    if (name == kSetMode) {
        int mode = 0;
        params->fetch_int_value(kMode, &mode, 1);
        change_regime(mode, 1.0f);
        return 1;
    }
    return 2;
}

// 0x004FA7C0
void RiderChaseView::restore_default_state() { change_regime(mode_sel_, 1.0f); }

// 0x004FA7D0
void RiderChaseView::next_mode()
{
    if (mode_sel_ >= 5)
        return;
    if (++mode_sel_ >= 5)
        mode_sel_ = 1;
    change_regime(mode_sel_, 1.0f);
}

// 0x004FA800
ObjectModule* __cdecl RiderChaseView::fabricate() { return new RiderChaseView; }

// --- the cleanup state ---

// 0x004FBD90 (0x004FBDB0 the deleting one)
RiderStateReset::~RiderStateReset() {}

// 0x004FBDA0
void RiderStateReset::configure_from_data(script::Struct*) { state_ref_ = nullptr; }

// 0x004FBDE0
void RiderStateReset::seal() { state_ref_ = entity_->fetch_module(kSkaterState); }

// 0x004FBE00
void RiderStateReset::tick_frame()
{
    GameEntity* skater = entity_;
    reinterpret_cast<Rider*>(skater)->update_shadow_light(reinterpret_cast<int>(&skater->at_point_),
                                                          reinterpret_cast<int>(&skater->basis_frame_));
}

// 0x004FBE20
ObjectModule* __cdecl RiderStateReset::fabricate() { return new RiderStateReset; }

// (the constructor: inlined in fabricate in the original)
RiderStateReset::RiderStateReset() { kind_tag_ = kCleanupState; }

} // namespace objects

using objects::RiderChaseView;
using objects::RiderStateReset;

namespace objects {} // namespace objects
