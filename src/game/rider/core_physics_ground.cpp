// The skater core physics component: friction, braking and pushing off, the ground's normal and going up vert
// (0x004FE1F0-0x004FEE0B).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/rider.h"
#include "game/park_editing/park_editor.h"

namespace skater {

using namespace physics;

void __cdecl tracking_print(int index, const maths::Vec4* a, const maths::Vec4* b);

namespace {

// Physics values, stats and script values (names not recovered unless given).
constexpr uint32_t kSlopeTurnSpeed = 0x62A1FA03;
constexpr uint32_t kSlopeTurnNormal = 0xC3527EF2;
constexpr uint32_t kPushSpeedCrouched = 0x92E0247C;
constexpr uint32_t kKickoffSpeed = 0x4610C2E3;
constexpr uint32_t kPushCrouched = 0x3D24128E;
constexpr uint32_t kPush = 0x5F9B864D;
constexpr uint32_t kFrictionCrouched = 0xBED96EDA;
constexpr uint32_t kFriction = 0x1A78B6FC;
constexpr uint32_t kNormalBlendRate = 0xD8120182;
constexpr uint32_t kVertPushOut = 0x78384871;
constexpr uint32_t kOff = 0xD443A2BC;               // Off

// The game's constants.
constexpr float kBarelyMovingSquared = 0.00001f;     // 0x006453B8
constexpr float kFramesPerSecond = 60.0f;            // 0x00645EA0
constexpr float kSlow = 50.0f;                       // 0x006463CC
constexpr float kLockedSlow = 0.5f;                  // 0x006453BC
constexpr float kStill = 0.0001f;                    // 0x006453D4
constexpr float kNormalFeeler = 72.0f;               // 0x00646244
constexpr float kFlat = 0.001f;                      // 0x006461D8

// The component's fields used here.
constexpr uint32_t kDrawnNormal = 0x58;
constexpr uint32_t kOldNormal = 0x78;
constexpr uint32_t kNormalBlend = 0x88;
constexpr uint32_t kDisplayUp = 0x28;
constexpr uint32_t kDisplayFacing = 0x38;
constexpr uint32_t kTakeOffPoint = 0x268;
constexpr uint32_t kWallFaceVector = 0x278;
constexpr uint32_t kVertFacing = 0x288;
constexpr uint32_t kVertTime = 0x298;
constexpr uint32_t kAutoKick = 0x2D9;
constexpr uint32_t kCrouchStat = 0x303;
constexpr uint32_t kNoKick = 0x310;
constexpr uint32_t kCanBrake = 0x311;
constexpr uint32_t kRollingFrictionValue = 0x314;
constexpr uint32_t kLimitsTime = 0x350;
constexpr uint32_t kLimitsFriction = 0x35C;
constexpr uint32_t kLockVelocity = 0x388;

// The state component's flags.
constexpr uint32_t kFlagCrouched = 0x20;
constexpr uint32_t kFlagVertAir = 0x30;
constexpr uint32_t kFlagVertWall = 0x38;
constexpr uint32_t kFlagOnVert = 0x40;
constexpr uint32_t kFlagSpin = 0x68;

// The pad's buttons (offsets into the input component).
constexpr uint32_t kBrakeButton = 0x40;
constexpr uint32_t kLeftButton = 0x60;
constexpr uint32_t kRightKeyHash = 0x80;
constexpr uint32_t kKickButton = 0x180;

bool is_crouched(void* component) { return flags_of(component)[kFlagCrouched] != 0; }
double speed_of(const maths::Vec4& v) { return length_unrounded(v); }

} // namespace

// 0x004FE1F0
void SkaterPhysicsModule::apply_friction(float friction)
{
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    float length_squared = static_cast<float>(length_sqr_unrounded(velocity));
    if (length_squared < kBarelyMovingSquared)
        return;
    float slowing = static_cast<float>(static_cast<double>(length_squared) * get_at<float>(this, kFrameLength) * friction *
                                       kFramesPerSecond);
    maths::Vec4 change = velocity;
    change.set_length(slowing);
    double change_squared = (static_cast<double>(change.z) * change.z + static_cast<double>(change.x) * change.x) +
                           static_cast<double>(change.y) * change.y;
    if (change_squared > length_squared) {
        velocity = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    velocity.x -= change.x;
    velocity.y -= change.y;
    velocity.z -= change.z;
    velocity.w -= change.w;
}

// 0x004FE320
bool SkaterPhysicsModule::should_brake()
{
    uint8_t* input = input_of(this);
    if (!get_at<uint8_t>(this, kAutoKick) && !input[kKickButton] && speed_of(vector_at(object_of(this), kVelocity)) < kSlow)
        return true;
    if (!input[kBrakeButton] || !get_at<uint8_t>(this, kCanBrake))
        return false;
    if (speed_of(vector_at(object_of(this), kVelocity)) < kSlow)
        return true;
    if (!input[kRightKeyHash] && !input[kLeftButton])
        return true;
    void* object = object_of(this);
    const maths::Vec4& v = vector_at(object, kVelocity);
    const maths::Vec4& f = vector_at(object, kForward);
    double along = (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(f.x) * v.x;
    return along < 0.0;
}

// 0x004FE420
bool SkaterPhysicsModule::may_turn_on_slope()
{
    float speed = static_cast<float>(speed_of(vector_at(object_of(this), kVelocity)));
    if (!(script::fetch_float(kSlopeTurnSpeed) > speed))
        return false;
    return script::fetch_float(kSlopeTurnNormal) > get_at<float>(this, kFloorUpVector + 4);
}

// 0x004FE490
bool SkaterPhysicsModule::can_push_off()
{
    uint8_t* input = input_of(this);
    if (get_at<uint8_t>(this, kNoKick))
        return false;
    if (!get_at<uint8_t>(this, kAutoKick) && !input[kKickButton])
        return false;
    if (input[kBrakeButton])
        return false;
    float speed = static_cast<float>(speed_of(vector_at(object_of(this), kVelocity)));
    bool crouched = is_crouched(this) || get_at<uint8_t>(this, kCrouchStat);
    float limit = static_cast<float>(stat_value(object_of(this), crouched ? kPushSpeedCrouched : kKickoffSpeed));
    return !(limit < speed);
}

// 0x004FE530
void SkaterPhysicsModule::push_off()
{
    void* object = object_of(this);
    maths::Vec4 direction;
    if (get_at<uint8_t>(this, kLockVelocity)) {
        direction = vector_at(object, kVelocity);
        const maths::Vec4& v = direction;
        double speed = std::sqrt((static_cast<double>(v.z) * v.z + static_cast<double>(v.y) * v.y) +
                                 static_cast<double>(v.x) * v.x);
        if (speed < kLockedSlow)
            direction = vector_at(object, kForward);
        else
            direction.make_unit();
    } else {
        direction = vector_at(object, kForward);
    }
    bool crouched = is_crouched(this) || get_at<uint8_t>(this, kCrouchStat);
    float push = static_cast<float>(stat_value(object, crouched ? kPushCrouched : kPush));
    double frame = get_at<float>(this, kFrameLength);
    float x = static_cast<float>(static_cast<double>(direction.x) * push * frame);
    double y = static_cast<double>(direction.y) * push * frame;
    double z = static_cast<double>(static_cast<float>(static_cast<double>(direction.z) * push)) * frame;
    float w = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(direction.w) * push)) * frame);
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    velocity.x += x;
    velocity.y = static_cast<float>(velocity.y + y);
    velocity.z = static_cast<float>(velocity.z + z);
    velocity.w += w;
}

// 0x004FE6A0
void SkaterPhysicsModule::apply_stance_friction()
{
    void* object = object_of(this);
    float crouched = tuning_float_lookup(kFrictionCrouched, object);
    float standing = tuning_float_lookup(kFriction, object);
    if (get_at<float>(this, kLimitsTime) != 0.0f) {
        crouched = get_at<float>(this, kLimitsFriction);
        standing = crouched;
    }
    apply_friction(is_crouched(this) ? crouched : standing);
}

// 0x004FE730
void SkaterPhysicsModule::apply_wheel_drag()
{
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    double x = velocity.x;
    double w = velocity.w;
    float vy = velocity.y;
    float vz = velocity.z;
    double speed = std::sqrt((static_cast<double>(vz) * vz + static_cast<double>(vy) * vy) + x * x);
    float speed_rounded = static_cast<float>(speed);
    if (speed < kStill) {
        velocity = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    double k = static_cast<double>(get_at<float>(this, kFrameLength)) / speed_rounded * get_at<float>(this, kRollingFrictionValue) *
               kFramesPerSecond;
    float dx = static_cast<float>(k * x);
    float dy = static_cast<float>(vy * k);
    float dz = static_cast<float>(vz * k);
    double dw = w * k;
    double change_squared = (static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
    if (static_cast<double>(speed_rounded) * speed_rounded < change_squared) {
        velocity = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    velocity.x -= dx;
    velocity.y -= dy;
    velocity.z -= dz;
    velocity.w = static_cast<float>(velocity.w - dw);
}

// 0x004FE860
void SkaterPhysicsModule::turn_to_normal(maths::Vec4 normal)
{
    uint8_t* settings = reinterpret_cast<uint8_t*>(park_editing::g_park_editor);
    if (get_at<uint32_t>(settings, 0x40) != kOff && normal.y > 0.0f) {
        const maths::Vec4& position = vector_at(object_of(this), kPosition);
        maths::Vec4 end;
        end.x = static_cast<float>(static_cast<float>(normal.x * static_cast<double>(kNormalFeeler)) + position.x);
        end.y = static_cast<float>(static_cast<float>(normal.y * static_cast<double>(kNormalFeeler)) + position.y);
        end.z = static_cast<float>(static_cast<float>(normal.z * static_cast<double>(kNormalFeeler)) + position.z);
        end.w = static_cast<float>(normal.w * static_cast<double>(kNormalFeeler) + position.w);
        render::ProbeRay feeler;
        feeler.make_new(&position, &end);
        if (feeler.get_hit_shape(true, false))
            normal = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    }
    if (vector_at(this, kFloorUpVector) != normal) {
        vector_at(this, kFloorUpVector) = normal;
        vector_at(this, kOldNormal) = vector_at(this, kDrawnNormal);
        get_at<float>(this, kNormalBlend) = 1.0f;
        vector_at(object_of(this), kUp) = normal;
        get_at<maths::Mat4>(object_of(this), kMatrix).square_up_axes(1);
    }
}

// 0x004FEA50
void SkaterPhysicsModule::update_ground_normal()
{
    float& blend = get_at<float>(this, kNormalBlend);
    if (blend != 0.0f) {
        if (vector_at(this, kOldNormal) == vector_at(this, kFloorUpVector)) {
            blend = 0.0f;
        } else {
            double left = blend - static_cast<double>(script::fetch_float(kNormalBlendRate)) * get_at<float>(this, kFrameLength) *
                                      kFramesPerSecond;
            float t = static_cast<float>(left);
            blend = t;
            if (left > 0.0 || left != left) {
                maths::Vec4 blended;
                walk::get_lerp(&blended, &vector_at(this, kFloorUpVector), &vector_at(this, kOldNormal), t);
                vector_at(this, kDrawnNormal) = blended;
                vector_at(this, kDrawnNormal).make_unit();
            } else {
                blend = 0.0f;
                vector_at(this, kDrawnNormal) = vector_at(this, kFloorUpVector);
            }
        }
    }
    if (vector_at(this, kDisplayUp) != vector_at(this, kDrawnNormal)) {
        vector_at(this, kDisplayUp) = vector_at(this, kDrawnNormal);
        maths::Mat4& display = get_at<maths::Mat4>(this, kDisplayMatrix);
        display.square_up_axes(1);
        display.row[0] = vector_at(object_of(this), kMatrix);
        vector_at(this, kDisplayFacing) = vector_at(object_of(this), kForward);
    }
}

// 0x004FEBA0
void SkaterPhysicsModule::start_vert_air()
{
    if (!flags_of(this)[kFlagOnVert]) {
        set_timed_flag(this, kFlagVertAir, 0);
        return;
    }
    void* object = object_of(this);
    tracking_print(3, &vector_at(object, kOldPosition), &vector_at(object, kPosition));
    maths::Vec4 across = vector_at(this, kFloorUpVector);
    vector_at(this, kWallFaceVector) = vector_at(this, kFloorUpVector);
    vector_at(this, kTakeOffPoint) = vector_at(object_of(this), kOldPosition);
    get_at<float>(this, kTakeOffPoint + 4) = static_cast<float>(get_at<float>(this, kTakeOffPoint + 4) - 1.0);
    across.y = 0.0f;
    if (!(std::sqrt(static_cast<double>(across.z) * across.z + static_cast<double>(across.x) * across.x) > kFlat)) {
        set_timed_flag(this, kFlagVertAir, 0);
        return;
    }
    across.make_unit();
    vector_at(object_of(this), kVelocity).align_to_plane(across);
    turn_to_normal(across);
    vector_at(this, kVertFacing) = vector_at(object_of(this), kForward);
    get_at<float>(this, kVertFacing + 4) = -get_at<float>(this, kVertFacing + 4);
    double out = script::fetch_float(kVertPushOut);
    maths::Vec4& position = vector_at(object_of(this), kPosition);
    float y = static_cast<float>(across.y * out);
    float z = static_cast<float>(across.z * out);
    float w = static_cast<float>(across.w * out);
    position.x = static_cast<float>(across.x * out + position.x);
    position.y += y;
    position.z += z;
    position.w += w;
    set_timed_flag(this, kFlagVertAir, 1);
    set_timed_flag(this, kFlagVertWall, 1);
    set_timed_flag(this, kFlagSpin, 1);
    get_at<float>(this, kVertTime) = 6.0f;
}

} // namespace Skater

using skater::SkaterPhysicsModule;
