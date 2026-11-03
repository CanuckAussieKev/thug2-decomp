// The acid drop: looking ahead (and down) for a vert wall to drop into, and starting the drop - the skater thrown
// along an arc onto it, its matrix slerped round to the wall's; and its cousin the spine (or hip) transfer, out of
// vert over onto the ramp behind.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "engine/maths/shape_tests.h"
#include "engine/core/enum_ops.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/walk_physics.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "game/rider/rider_shared.h"
#include "engine/core/checksums.h"
#include "engine/render/coll_obj.h"
#include <gsl/pointers>

namespace skater {

using namespace physics;

namespace {
constexpr float kHalf = 0.5f;

constexpr uint32_t kWalkSearchDistance = 0xE50A9D56; // Physics_Acid_Drop_Walking_On_Ground_Search_Distance
constexpr uint32_t kPopSpeed = 0x95A79C32;           // Physics_Acid_Drop_Pop_Speed
constexpr uint32_t kMinAirTime = 0x32C20F7E;         // Physics_Acid_Drop_Min_Air_Time
constexpr uint32_t kWalk = 0x726E85AA;               // Walk (a component)
constexpr uint32_t kDropHeight = 0xBB00FE40;         // DropHeight
constexpr uint32_t kAcidDropTriggered = 0xC7ED5FEF;  // SkaterAcidDropTriggered (a script)
constexpr uint32_t kOllied = 0x8FFEFB28;             // Ollied (an event)

constexpr float kMinSpeed = 0.01f;        // 0x0064544C
constexpr float kSearchReach = 500.0f;    // (0x43FA0000)
constexpr float kWalkLift = 200.0f;       // (0x43480000)
constexpr float kWalkBack = 12.0f;        // 0x0064546C
constexpr float kBoxDepth = 4200.0f;      // 0x0064BD34
constexpr float kFeelerDepth = 4000.0f;   // 0x0064B8F8
constexpr float kFirstStep = 0.01f;       // (0x3C23D70A)
constexpr float kMaxWallY = 0.707f;       // 0x006467BC
constexpr float kWalkFacing = -0.25f;     // 0x0064BD30
constexpr float kFacing = 0.05f;          // 0x00645D6C
constexpr float kFarStep = 100.0f;        // 0x006463D4
constexpr float kFarStepExtra = 24.0f;    // 0x00645EE8
constexpr float kStep = 3.0f;             // 0x006467A8
constexpr float kMinHorizontal = 0.0001f; // 0x006453D4
constexpr float kNeverLands = 0.00001f;   // 0x006453B8
constexpr float kRise = 24.0f;            // 0x00645EE8

} // namespace

// The walking component's state (+0x1530's +0x2C): walking on the ground (its +0x118 clear and +0x1C == `state`).
bool SkaterPhysicsModule::walking_state(objects::RiderPoseState::PhysicsMode state)
{
    const auto* pose = physics_control_->pose();
    return !pose->in_vehicle() && pose->physics_mode() == state;
}

// The time to fall back to `height` below the start of a jump of upward speed `upward` (the gravity flag +0x78
// set while the gravity's read): (sqrt(upward^2 + 2 * height * g) + upward) / g, or 0.00001 if never.
double SkaterPhysicsModule::fall_time(float height, float upward)
{
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    const double down = -current_gravity(this);
    const double discriminant = (static_cast<double>(upward) * upward) + (2.0 * (static_cast<double>(height) * down));
    double time = 0.0;
    if (discriminant >= 0.0 || discriminant != discriminant)
        time = (std::sqrt(discriminant) + upward) / down;
    else
        time = kNeverLands;
    set_timed_flag(this, PoseFlag::kAcidDrop, 0);
    return time;
}

// 0x00502240
bool SkaterPhysicsModule::find_acid_drop(bool pop, const maths::Vec4* position, const maths::Vec4* last_position,
                                         maths::Vec4* velocity, AcidDrop* drop)
{
    // Looking along the horizontal velocity (walking: the walking direction).
    maths::Vec4 direction{};
    if (thaw_bmx::live_now(this)) {
        // Not original: THAW 0x005D1890 - on the bike along its facing (the other way with the stick pulled back).
        thaw_bmx::acid_drop_direction(this, &direction);
    } else if (walking_state(objects::RiderPoseState::PhysicsMode::kSkating)) {
        const double x = velocity->x;
        const double z = velocity->z;
        const double length = std::sqrt((x * x) + (z * z));
        if (length < kMinSpeed)
            return false;
        const double inverse = 1.0 / length;
        direction = {.x = static_cast<float>(inverse * x), .y = static_cast<float>(0.0f * inverse),
                     .z = static_cast<float>(z * inverse), .w = static_cast<float>(velocity->w * inverse)};
    } else {
        direction = walk_->facing();
    }
    maths::Vec4 base = *last_position;
    if (position->y > last_position->y)
        base.y = position->y;
    float reach = kSearchReach;
    float lift = 0.0f;
    if (walking_state(objects::RiderPoseState::PhysicsMode::kWalking)) {
        if (!static_cast<uint32_t>(walk_->get_state())) {
            reach = script::fetch_float(kWalkSearchDistance);
            lift = kWalkLift;
        } else {
            base.x = static_cast<float>(base.x - (static_cast<double>(direction.x) * kWalkBack));
            base.y = static_cast<float>(base.y - (static_cast<double>(direction.y) * kWalkBack));
            base.z = static_cast<float>(base.z - (static_cast<double>(direction.z) * kWalkBack));
            base.w = static_cast<float>(base.w - (static_cast<double>(direction.w) * kWalkBack));
        }
    }

    // The collision near the whole search, cached.
    render::ProbeRay feeler;
    render::HitMeshCache cache;
    maths::BBox box;
    maths::Vec4 corner = {.x = base.x, .y = base.y + lift, .z = base.z, .w = base.w};
    box.append_spot(corner);
    corner.y = static_cast<float>(corner.y - static_cast<double>(kBoxDepth));
    box.append_spot(corner);
    {
        const float rz = direction.z * reach;
        const float ry = direction.y * reach;
        const float rx = direction.x * reach;
        const double rw = static_cast<double>(direction.w) * reach;
        const float y = base.y + ry;
        corner = {.x = rx + base.x, .y = y + lift, .z = base.z + rz, .w = static_cast<float>(rw + base.w)};
    }
    box.append_spot(corner);
    corner.y = static_cast<float>(corner.y - static_cast<double>(kBoxDepth));
    box.append_spot(corner);
    cache.tick_frame(box);
    feeler.set_hit_cache(&cache);

    // Feelers straight down, further and further ahead, for a vert wall facing away (or, walking up to one, towards).
    maths::Vec4 saved{};
    bool found = false;
    maths::Vec4 hit{};
    maths::Vec4 normal{};
    bool alive = false;
    float hit_height = 0.0f;
    float distance = 0.0f;
    float horizontal = 0.0f;
    float step = kFirstStep;
    bool searching = kFirstStep < reach;
    while (searching) {
        const float sz = direction.z * step;
        const float sy = direction.y * step;
        const float sx = direction.x * step;
        const double sw = static_cast<double>(direction.w) * step;
        const float x = sx + base.x;
        const float y = base.y + sy;
        const float z = base.z + sz;
        const auto w = static_cast<float>(sw + base.w);
        feeler.line().start = {.x = x, .y = y + lift, .z = z, .w = w};
        feeler.line().end = {.x = x, .y = static_cast<float>(y - static_cast<double>(kFeelerDepth)), .z = z, .w = w};
        if (feeler.get_hit_shape(true, false) && (core::any(feeler.hit().face_flags_ & render::FaceFlag::kVert)) &&
            std::fabs(feeler.surface_dir().y) < kMaxWallY) {
            maths::Vec4 flat = {.x = feeler.surface_dir().x, .y = 0.0f, .z = feeler.surface_dir().z, .w = feeler.surface_dir().w};
            normal = feeler.surface_dir();
            flat.make_unit();
            const double facing = (static_cast<double>(flat.z) * direction.z) +
                                  (static_cast<double>(flat.y) * direction.y) +
                                  (static_cast<double>(flat.x) * direction.x);
            const bool walking_in = walking_state(objects::RiderPoseState::PhysicsMode::kWalking) && walk_->get_state() == 1;
            if ((walking_in && facing <= kWalkFacing) || facing >= kFacing) {
                found = true;
                break;
            }
        }
        double next = step;
        if (step > kFarStep)
            next += kFarStepExtra;
        next += kStep;
        step = static_cast<float>(next);
        searching = next < reach;
    }
    if (!found) {
        return false;
    }

    // The drop: the arc from here to the hit, along the horizontal direction to it.
    hit = feeler.contact_pos();
    hit_height = hit.y;
    alive = feeler.hit_object_alive();
    const double dw = static_cast<double>(hit.w) - position->w;
    auto dz = static_cast<float>(hit.z - static_cast<double>(position->z));
    const double dx_unrounded = static_cast<double>(hit.x) - position->x;
    auto dx = static_cast<float>(dx_unrounded);
    distance = static_cast<float>(std::sqrt((dx_unrounded * dx) + (static_cast<double>(dz) * dz)));
    const double sideways = (static_cast<double>(direction.z) * dz) + (static_cast<double>(dx) * direction.x) +
                            (static_cast<double>(direction.y) * 0.0f);
    if (sideways < 0.0)
        distance = -distance;
    const double inverse = 1.0 / distance;
    saved = *velocity;
    auto inverse_float = static_cast<float>(inverse);
    direction = {.x = static_cast<float>(static_cast<double>(inverse_float) * dx),
                 .y = static_cast<float>(static_cast<double>(inverse_float) * 0.0f),
                 .z = static_cast<float>(static_cast<double>(inverse_float) * dz), .w = static_cast<float>(dw * inverse)};
    if (walking_state(objects::RiderPoseState::PhysicsMode::kWalking)) {
        velocity->project_onto(direction);
        velocity->y = saved.y;
    }
    horizontal = static_cast<float>(
        std::sqrt((static_cast<double>(velocity->z) * velocity->z) + (static_cast<double>(velocity->x) * velocity->x)));
    if (walking_state(objects::RiderPoseState::PhysicsMode::kWalking)) {
        // (walking: at least the walk's speed - 0x0043DAA0)
        auto least = static_cast<float>(walk_->run_speed());
        if (horizontal < least) {
            auto* walk = core::down_cast<objects::FootMovement>(object_of(this)->fetch_module(kWalk));
            velocity->x = static_cast<float>(static_cast<double>(least) * walk->facing().x);
            walk = core::down_cast<objects::FootMovement>(object_of(this)->fetch_module(kWalk));
            velocity->z = static_cast<float>(static_cast<double>(least) * walk->facing().z);
            horizontal = least;
        }
    }
    objects::GameEntity* object = object_of(this);
    if (pop) {
        // At least Physics_Acid_Drop_Pop_Speed up.
        const float least = tuning_float_lookup(kPopSpeed, object);
        const float upward = velocity->y;
        velocity->y = upward > least ? upward : least;
    }
    if (!walking_state(objects::RiderPoseState::PhysicsMode::kWalking) || static_cast<uint32_t>(walk_->get_state())) {
        // At most twice that.
        const double most = static_cast<double>(tuning_float_lookup(kPopSpeed, object)) * 2.0;
        const float upward = velocity->y;
        velocity->y = upward < most ? upward : static_cast<float>(most);
    }
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    auto gravity = static_cast<float>(current_gravity(this));
    set_timed_flag(this, PoseFlag::kAcidDrop, 0);
    // The highest the arc gets on its way to the wall.
    float peak = 0.0f;
    if (distance > 0.0f && horizontal > kMinHorizontal) {
        const double ratio = static_cast<double>(distance) / horizontal;
        peak =
            static_cast<float>((((static_cast<double>(gravity) * ratio * kHalf) + velocity->y) * ratio) + position->y);
    } else {
        peak = position->y;
    }
    if (walking_state(objects::RiderPoseState::PhysicsMode::kWalking) && velocity->y > 0.0f && position->y < hit.y) {
        const double upward = velocity->y;
        const double top = (upward * upward / (static_cast<double>(gravity) * -2.0f)) + position->y;
        const double to_wall = std::fabs(static_cast<double>(distance)) / horizontal;
        const double to_top = -(upward / gravity);
        if (to_wall < to_top)
            peak = static_cast<float>(top);
    }
    auto fail = [&] {
        *velocity = saved;
        return false;
    };
    if (peak < hit.y)
        return fail();
    auto air_time =
        static_cast<float>(fall_time(static_cast<float>(position->y - static_cast<double>(hit.y)), velocity->y));
    if (script::fetch_float(kMinAirTime) > air_time)
        return fail();
    if (!(hit.y < peak))
        return fail();
    // Up the wall from the hit, 24 at a time, for a point the arc reaches with nothing in the way.
    auto half_distance = static_cast<float>(static_cast<double>(distance) * kHalf);
    for (;;) {
        feeler.line().start = *position;
        auto rise = static_cast<float>(position->y - static_cast<double>(hit.y));
        auto half = static_cast<float>(fall_time(rise, velocity->y) * kHalf);
        const double scale = static_cast<double>(half_distance) / half;
        auto y = static_cast<float>((static_cast<double>(half) * half * gravity * kHalf) +
                                    (static_cast<double>(half) * velocity->y) + position->y);
        const double along = scale * half;
        auto aw = static_cast<float>(direction.w * along);
        auto az = static_cast<float>(direction.z * along);
        auto ay = static_cast<float>(direction.y * along);
        const double ax = along * direction.x;
        maths::Vec4& end = feeler.line().end;
        end = {.x = static_cast<float>(ax + position->x), .y = static_cast<float>(static_cast<double>(ay) + y), .z = position->z + az,
               .w = position->w + aw};
        if (!feeler.get_hit_shape(true, false)) {
            // Nothing on the way up to the arc's middle: nothing on the way down from there to the point either?
            feeler.line().start = feeler.line().end;
            feeler.line().end = {.x = hit.x, .y = hit.y + 1.0f, .z = hit.z, .w = hit.w};
            if (!feeler.get_hit_shape(true, false)) {
                drop->point = hit;
                drop->normal = normal;
                drop->hit_height_ = hit_height;
                drop->object_hit_ = alive;
                return true;
            }
        }
        const double higher = static_cast<double>(hit.y) + kRise;
        hit.y = static_cast<float>(higher);
        if (!(higher < peak))
            return fail();
    }
}

// 0x00502F20
void SkaterPhysicsModule::start_acid_drop(const AcidDrop* drop)
{
    // The horizontal direction to the drop point and the speed to get there in the fall's time.
    const objects::GameEntity* object = object_of(this);
    const maths::Vec4& position = object->at_point();
    const double dw = static_cast<double>(drop->point.w) - position.w;
    auto dz = static_cast<float>(drop->point.z - static_cast<double>(position.z));
    const double dx_unrounded = static_cast<double>(drop->point.x) - position.x;
    auto dx = static_cast<float>(dx_unrounded);
    auto distance = static_cast<float>(std::sqrt((dx_unrounded * dx) + (static_cast<double>(dz) * dz)));
    const maths::Vec4& velocity = object->vel();
    const double sideways = (static_cast<double>(dz) * velocity.z) + (static_cast<double>(dx) * velocity.x) +
                            (static_cast<double>(velocity.y) * 0.0f);
    if (sideways < 0.0)
        distance = -distance;
    const double inverse = 1.0 / distance;
    auto inverse_float = static_cast<float>(inverse);
    const maths::Vec4 direction = {.x = static_cast<float>(static_cast<double>(inverse_float) * dx),
                                   .y = static_cast<float>(static_cast<double>(inverse_float) * 0.0f),
                                   .z = static_cast<float>(static_cast<double>(inverse_float) * dz),
                                   .w = static_cast<float>(dw * inverse)};
    auto rise = static_cast<float>(entity()->at_point().y - static_cast<double>(drop->point.y));
    auto time = static_cast<float>(fall_time(rise, entity()->vel().y));
    auto speed = static_cast<float>(static_cast<double>(distance) / time);
    maths::Vec4& flat = state_->drop_velocity();
    flat.y = 0.0f;
    flat.w = 1.0f;
    flat.x = direction.x * speed;
    flat.z = direction.z * speed;
    rider_shared().acid_hold() = state_->drop_velocity(); // (a copy kept for debugging, it seems)
    drop_target_height_ = drop->point.y;
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    set_timed_flag(this, PoseFlag::kVertAir, 1);
    set_timed_flag(this, PoseFlag::kFromAcidDrop, 1);
    set_timed_flag(this, PoseFlag::kVertWall, 0);
    set_timed_flag(this, PoseFlag::kSpin, 0);
    drop_object_hit_ = drop->object_hit_;
    entity()->vel().x = 0.0f;
    entity()->vel().z = 0.0f;
    const maths::Mat4 start = entity()->basis_frame();

    // The wall's orientation: along the drop turned into the wall's plane, its normal up, across them.
    const maths::Vec4& normal = drop->normal;
    double upward =
        -(((static_cast<double>(direction.x) * normal.x) + (static_cast<double>(direction.z) * normal.z)) / normal.y);
    const double length = std::sqrt((static_cast<double>(direction.x) * direction.x) + (upward * upward) +
                                    (static_cast<double>(direction.z) * direction.z));
    maths::Vec4 along = {.x = direction.x, .y = 0.0f, .z = direction.z, .w = direction.w};
    if (length > 0.0) {
        const double scale = 1.0 / length;
        along.x = static_cast<float>(direction.x * scale);
        upward *= scale;
        along.z = static_cast<float>(direction.z * scale);
    }
    along.y = static_cast<float>(upward);
    along.flatten_onto_plane(normal);
    along.make_unit();
    const maths::Vec4 up = normal;
    drop_heading_ = along;
    maths::Mat4 wall;
    core::at(wall.row, 0) = {
        .x = static_cast<float>((static_cast<double>(up.y) * along.z) - (static_cast<double>(up.z) * along.y)),
        .y = static_cast<float>((static_cast<double>(up.z) * along.x) - (static_cast<double>(up.x) * along.z)),
        .z = static_cast<float>((static_cast<double>(up.x) * along.y) - (static_cast<double>(up.y) * along.x)),
        .w = 0.0f};
    core::at(wall.row, 1) = up;
    core::at(wall.row, 2) = along;
    core::at(wall.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::Mat4& stored = drop_matrix_;
    stored = wall;
    if (core::at(stored.row, 2).y > 0.0f) { // (along the drop pointing up: turned round, across with it)
        core::at(stored.row, 2) *= -1.0f;
        core::at(stored.row, 0) *= -1.0f;
    }
    // Turned to face the way the skater faces (flattened) relative to the drop.
    object = object_of(this);
    maths::Vec4 facing = core::at(object->basis_frame().row, 2);
    facing.y = 0.0f;
    auto turn = static_cast<float>(-maths::get_turn_around_axis(facing, direction, core::at(object->basis_frame().row, 1)));
    wall.yaw_locally(turn);
    drop_slerp_.set_matrices(start, wall);
    drop_time_ = 0.0f;
    drop_duration_ = time;
    drop_last_matrix_ = start;

    // SkaterAcidDropTriggered with DropHeight (how far above the hit's height the skater is).
    gsl::owner<script::Struct*> params = script::new_struct();
    params->push_float(checksums::Name{kDropHeight}, static_cast<float>(entity()->at_point().y - static_cast<double>(drop->hit_height_)));
    object_of(this)->start_script_task(kAcidDropTriggered, -1, false, false, params);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    set_timed_flag(this, PoseFlag::kNoAcidDrop, 1);
    object_of(this)->clear_event_handler(kOllied);
}

namespace {

constexpr uint32_t kAwardTransfer = 0xA5179E9E;    // SkaterAwardTransfer (a script)
constexpr uint32_t kAwardHipTransfer = 0x283BB5D6; // SkaterAwardHipTransfer (a script)

constexpr float kQuarterTurn = 1.57079632679f; // (0x3FC90FDB)
constexpr float kAhead = 0.5f;                 // 0x006453BC
constexpr float kFlatTarget = 0.9f;            // 0x00649FE4 (also the drop's least duration)
constexpr float kFarTarget = 24.0f;            // 0x00645EE8
constexpr float kLeastFallTime = 0.1f;         // 0x00645D74
constexpr float kLeastTurn = 0.5235988f;       // 0x0064BD48 (30 degrees)

} // namespace

// 0x004FEE10 (not rewritten yet): a spine or hip to transfer to, looking along `look` from the vert wall of
// (horizontal) normal `normal`: true with the point to land on (`target`), its normal, whether it's a hip and
// whether the object hit is still there.
bool SkaterPhysicsModule::find_transfer_target(maths::Vec4* look, maths::Vec4* normal, bool* hip,
                          VertTarget* target, bool* object_hit)
{
    return find_vert_target(look, normal, hip, target,
                                                                          object_hit);
}

// 0x00505F70
bool SkaterPhysicsModule::try_spine_transfer()
{
    // The skater's up, flattened and normalised (the wall's way out), and the way back down the wall: against the
    // velocity, turned a quarter round about the horizontal across the wall.
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& up_row = core::at(object->basis_frame().row, 1);
    maths::Vec4 out = {.x = up_row.x, .y = 0.0f, .z = up_row.z, .w = 0.0f};
    {
        const double length = std::sqrt((static_cast<double>(out.x) * out.x) + (static_cast<double>(out.z) * out.z));
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            out.x = static_cast<float>(out.x * inverse);
            out.z = static_cast<float>(out.z * inverse);
        }
    }
    const maths::Vec4 across = {.x = -out.z, .y = 0.0f, .z = out.x, .w = 0.0f};
    const maths::Vec4& velocity = object->vel();
    maths::Vec4 down = {.x = -velocity.x, .y = -velocity.y, .z = -velocity.z, .w = -velocity.w};
    {
        const double length =
            std::sqrt((static_cast<double>(down.x) * down.x) + (static_cast<double>(down.y) * down.y) +
                      (static_cast<double>(down.z) * down.z));
        if (length > 0.0) { // (x, y, z only: w stays the velocity's, negated)
            const double inverse = 1.0 / length;
            down.x = static_cast<float>(down.x * inverse);
            down.y = static_cast<float>(down.y * inverse);
            down.z = static_cast<float>(down.z * inverse);
        }
    }
    maths::QuatRotation rotation{};
    maths::QuatRotation* quat = &rotation;
    quat->init_from_axis_angle(across, kQuarterTurn);
    maths::Vec4 turned{};
    maths::Vec4 target = *quat->spin_by(&turned, down);

    // A feeler straight down from half a unit that way: the wall the skater is on.
    bool hip = false;
    bool object_hit = false;
    render::ProbeRay feeler;
    const maths::Vec4& position = object->at_point();
    const maths::Vec4 ahead = {.x = target.x * kAhead, .y = target.y * kAhead, .z = target.z * kAhead, .w = target.w * kAhead};
    feeler.line().start = {.x = static_cast<float>(static_cast<double>(ahead.x) + position.x),
                          .y = static_cast<float>(static_cast<double>(ahead.y) + position.y),
                          .z = static_cast<float>(static_cast<double>(ahead.z) + position.z),
                          .w = static_cast<float>(static_cast<double>(ahead.w) + position.w)};
    feeler.line().end = feeler.line().start;
    feeler.line().end.y = static_cast<float>(feeler.line().end.y - static_cast<double>(kFeelerDepth));
    if (!feeler.get_hit_shape(true, false))
        return false;
    const float wall_x = feeler.contact_pos().x;
    const float wall_z = feeler.contact_pos().z;
    maths::Vec4 normal = {.x = feeler.surface_dir().x, .y = 0.0f, .z = feeler.surface_dir().z, .w = feeler.surface_dir().w};
    normal.make_unit();

    // Something to transfer to, looking back the way the skater came - or, with one of the two buttons (&input->pad_primary_.left_side_
    // / +0x80, not both) held, the same turned that way along the wall.
    const maths::Vec4 back = {.x = -target.x, .y = -target.y, .z = -target.z, .w = -target.w};
    maths::Vec4 look = back;
    maths::Vec4 target_normal{};
    VertTarget aim{.point = target, .normal = target_normal};
    bool found = find_transfer_target(&look, &normal, &hip, &aim, &object_hit);
    target = aim.point;
    target_normal = aim.normal;
    if (!found) {
        const objects::PadInputModule* input = input_;
        if (!input->pad_primary().left_side().held_down() == !input->pad_primary().right_side().held_down())
            return false;
        if (input->pad_primary().left_side().held_down()) {
            look = {.x = static_cast<float>(static_cast<double>(back.x) + -normal.z), .y = back.y,
                    .z = static_cast<float>(static_cast<double>(back.z) + normal.x),
                    .w = static_cast<float>(static_cast<double>(back.w) + 1.0f)};
        } else {
            const maths::Vec4 side = {.x = normal.z, .y = 0.0f, .z = -normal.x, .w = -1.0f};
            look = side + back;
        }
        look.make_unit();
        aim = {.point = target, .normal = target_normal};
        found = find_transfer_target(&look, &normal, &hip, &aim, &object_hit);
        target = aim.point;
        target_normal = aim.normal;
        if (!found)
            return false;
    }

    // Found: how far the target is from the wall (flat), the target's normal flattened.
    const float height = position.y; // (the target is taken at the skater's own height)
    const double side_shift_x = static_cast<double>(target.x) - wall_x;
    const double off_z = static_cast<double>(target.z) - wall_z;
    auto distance = static_cast<float>(std::sqrt((side_shift_x * side_shift_x) + (off_z * off_z)));
    maths::Vec4 flat_normal = {.x = target_normal.x, .y = 0.0f, .z = target_normal.z, .w = target_normal.w};
    flat_normal.make_unit();
    maths::Vec4& object_velocity = object->vel();
    const maths::Vec4 old_velocity = object_velocity;
    const double facing = (static_cast<double>(flat_normal.z) * out.z) + (static_cast<double>(flat_normal.y) * out.y) +
                          (static_cast<double>(flat_normal.x) * out.x);
    if (std::fabs(facing) < kFlatTarget || distance > kFarTarget) { // (all of the speed straight up)
        object_velocity.y = static_cast<float>(length_unrounded(object_velocity));
        object_velocity.x = 0.0f;
        object_velocity.z = 0.0f;
    }
    auto reach = static_cast<float>(static_cast<double>(distance) + 1.0f);
    auto rise = static_cast<float>(position.y - static_cast<double>(height));
    auto time = static_cast<float>(fall_time(rise, object_velocity.y));
    time = std::max(time, kLeastFallTime);
    const double inverse_time = 1.0 / time;
    auto per_second = static_cast<float>(inverse_time);
    if (reach > kFarTarget) { // (fast enough to get there in the time?)
        const double speed = inverse_time * reach;
        if (length_sqr_unrounded(object_velocity) < speed * speed)
            return false;
    }

    // Nothing in the way to it: the transfer (blocked: flagged for break_vert, which tries again).
    feeler.line().start = position;
    feeler.line().end = {.x = target.x, .y = position.y, .z = target.z, .w = target.w};
    if (feeler.get_hit_shape(true, false)) {
        transfer_blocked_ = 1;
        return true;
    }
    transfer_blocked_ = 0;

    // The way the skater lands: over a spine, towards the target along its slope; over a hip, straight up or down
    // (down when the target lies on the far side of its normal).
    const double to_w = static_cast<double>(target.w) - position.w;
    const double to_z = static_cast<double>(target.z) - position.z;
    const double to_x = static_cast<double>(target.x) - position.x;
    maths::Vec4 along{};
    if (!hip) {
        along = {.x = static_cast<float>(to_x),
                 .y = static_cast<float>(-1.0f / static_cast<double>(target_normal.y) *
                                         ((target_normal.z * to_z) + (target_normal.x * to_x))),
                 .z = static_cast<float>(to_z),
                 .w = static_cast<float>(to_w)};
        along.make_unit();
    } else {
        along = {.x = static_cast<float>(to_x), .y = static_cast<float>(static_cast<double>(height) - position.y),
                 .z = static_cast<float>(to_z), .w = static_cast<float>(to_w)};
        along.make_unit();
        const double side = (static_cast<double>(along.z) * flat_normal.z) +
                            (static_cast<double>(along.y) * flat_normal.y) +
                            (static_cast<double>(along.x) * flat_normal.x);
        along = {.x = 0.0f, .y = side < 0.0 ? 1.0f : -1.0f, .z = 0.0f, .w = 1.0f};
    }

    // The orientation at the target (rows: across, the target's normal, along it in its plane), turned by the
    // skater's facing against its old velocity (none under 30 degrees); the slerp there from the skater's.
    const maths::Mat4 start = object->basis_frame();
    maths::Vec4 forward = along;
    forward.flatten_onto_plane(target_normal);
    forward.make_unit();
    maths::Mat4 end;
    core::at(end.row, 0) = maths::cross_vec(target_normal, forward);
    core::at(end.row, 1) = target_normal;
    core::at(end.row, 2) = forward;
    core::at(end.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    drop_heading_ = forward;
    double turn = maths::get_turn_around_axis(core::at(object->basis_frame().row, 2), old_velocity, core::at(object->basis_frame().row, 1));
    if (std::fabs(turn) < kLeastTurn)
        turn = 0.0;
    end.yaw_locally(static_cast<float>(-turn));
    maths::ArcRotationBlender& slerp = drop_slerp_;
    slerp.set_matrices(start, end);
    drop_time_ = 0.0f;
    drop_duration_ = time < kFlatTarget ? kFlatTarget : time;
    drop_last_matrix_ = start;
    maths::Mat4 halfway;
    slerp.fetch_mat(halfway, kHalf);
    if (core::at(halfway.row, 1).y < 0.0f) // (the short way round turns upside down: the long way)
        slerp.flip_heading();

    // The arc there in the fall's time: its horizontal velocity into the flags block, as the acid drop's.
    drop_target_height_ = height;
    auto fly_x = static_cast<float>(target.x - static_cast<double>(position.x));
    auto fly_y = static_cast<float>(height - static_cast<double>(position.y));
    auto fly_z = static_cast<float>(target.z - static_cast<double>(position.z));
    maths::Vec4& fly = state_->drop_velocity();
    fly = {.x = static_cast<float>(static_cast<double>(per_second) * fly_x),
           .y = static_cast<float>(static_cast<double>(per_second) * fly_y),
           .z = static_cast<float>(static_cast<double>(per_second) * fly_z), .w = static_cast<float>(to_w * per_second)};
    fly.y = 0.0f;
    object_of(this)->start_script_task(hip ? kAwardHipTransfer : kAwardTransfer, 0, false, false, nullptr);
    object_of(this)->clear_event_handler(kOllied);
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    set_timed_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_timed_flag(this, PoseFlag::kVertWall, 0);
    set_timed_flag(this, PoseFlag::kLeftVert, 0);
    drop_object_hit_ = object_hit;
    return true;
}

} // namespace skater
