// The acid drop: looking ahead (and down) for a vert wall to drop into, and starting the drop - the skater thrown
// along an arc onto it, its matrix slerped round to the wall's; and its cousin the spine (or hip) transfer, out of
// vert over onto the ramp behind.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/shape_tests.h"
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

namespace skater {

using namespace physics;

namespace {

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

objects::GameEntity* skater_object(SkaterPhysicsModule* component)
{
    return static_cast<objects::GameEntity*>(object_of(component));
}

// The walking component's state (+0x1530's +0x2C): walking on the ground (its +0x118 clear and +0x1C == `state`).
bool walking_state(SkaterPhysicsModule* component, int state)
{
    const auto* pose = component->physics_control_->pose_;
    return !pose->in_vehicle_ && pose->physics_mode_ == state;
}

// The time to fall back to `height` below the start of a jump of upward speed `upward` (the gravity flag +0x78
// set while the gravity's read): (sqrt(upward^2 + 2 * height * g) + upward) / g, or 0.00001 if never.
double fall_time(SkaterPhysicsModule* component, float height, float upward)
{
    set_timed_flag(component, PoseFlag::kAcidDrop, 1);
    double down = -current_gravity(component);
    double discriminant = static_cast<double>(upward) * upward + 2.0 * (static_cast<double>(height) * down);
    double time;
    if (discriminant >= 0.0 || discriminant != discriminant)
        time = (std::sqrt(discriminant) + upward) / down;
    else
        time = kNeverLands;
    set_timed_flag(component, PoseFlag::kAcidDrop, 0);
    return time;
}

} // namespace

// 0x00502240
bool SkaterPhysicsModule::find_acid_drop(bool pop, const maths::Vec4* position, const maths::Vec4* last_position,
                                         maths::Vec4* velocity, AcidDrop* drop)
{
    // Looking along the horizontal velocity (walking: the walking direction).
    maths::Vec4 direction;
    if (thaw_bmx::live_now(this)) {
        // Not original: THAW 0x005D1890 - on the bike along its facing (the other way with the stick pulled back).
        thaw_bmx::acid_drop_direction(this, &direction);
    } else if (walking_state(this, 0)) {
        double x = velocity->x, z = velocity->z;
        double length = std::sqrt(x * x + z * z);
        if (length < kMinSpeed)
            return false;
        double inverse = 1.0 / length;
        direction = {static_cast<float>(inverse * x), static_cast<float>(0.0f * inverse),
                     static_cast<float>(z * inverse), static_cast<float>(velocity->w * inverse)};
    } else {
        direction = walk_->facing_;
    }
    maths::Vec4 base = *last_position;
    if (position->y > last_position->y)
        base.y = position->y;
    float reach = kSearchReach, lift = 0.0f;
    if (walking_state(this, 1)) {
        if (!static_cast<uint32_t>(walk_->state_)) {
            reach = script::fetch_float(kWalkSearchDistance);
            lift = kWalkLift;
        } else {
            base.x = static_cast<float>(base.x - static_cast<double>(direction.x) * kWalkBack);
            base.y = static_cast<float>(base.y - static_cast<double>(direction.y) * kWalkBack);
            base.z = static_cast<float>(base.z - static_cast<double>(direction.z) * kWalkBack);
            base.w = static_cast<float>(base.w - static_cast<double>(direction.w) * kWalkBack);
        }
    }

    // The collision near the whole search, cached.
    render::ProbeRay feeler;
    feeler.make_new();
    render::HitMeshCache cache;
    cache.make_new();
    maths::BBox box;
    maths::Vec4 corner = {base.x, base.y + lift, base.z, base.w};
    box.append_spot(corner);
    corner.y = static_cast<float>(corner.y - static_cast<double>(kBoxDepth));
    box.append_spot(corner);
    {
        float rz = direction.z * reach, ry = direction.y * reach, rx = direction.x * reach;
        double rw = static_cast<double>(direction.w) * reach;
        float y = base.y + ry;
        corner = {rx + base.x, y + lift, base.z + rz, static_cast<float>(rw + base.w)};
    }
    box.append_spot(corner);
    corner.y = static_cast<float>(corner.y - static_cast<double>(kBoxDepth));
    box.append_spot(corner);
    cache.tick_frame(box);
    feeler.hit_cache_ = &cache;

    // Feelers straight down, further and further ahead, for a vert wall facing away (or, walking up to one, towards).
    maths::Vec4 saved;
    bool found = false;
    maths::Vec4 hit, normal;
    bool alive = false;
    float hit_height = 0.0f, distance = 0.0f, horizontal = 0.0f;
    float step = kFirstStep;
    bool searching = kFirstStep < reach;
    while (searching) {
        float sz = direction.z * step, sy = direction.y * step, sx = direction.x * step;
        double sw = static_cast<double>(direction.w) * step;
        float x = sx + base.x, y = base.y + sy, z = base.z + sz, w = static_cast<float>(sw + base.w);
        feeler.line_.start = {x, y + lift, z, w};
        feeler.line_.end = {x, static_cast<float>(y - static_cast<double>(kFeelerDepth)), z, w};
        if (feeler.get_hit_shape(true, false) && (feeler.hit_.face_flags_ & 8) &&
            std::fabs(feeler.surface_dir_.y) < kMaxWallY) {
            maths::Vec4 flat = {feeler.surface_dir_.x, 0.0f, feeler.surface_dir_.z, feeler.surface_dir_.w};
            normal = feeler.surface_dir_;
            flat.make_unit();
            double facing = static_cast<double>(flat.z) * direction.z + static_cast<double>(flat.y) * direction.y +
                            static_cast<double>(flat.x) * direction.x;
            bool walking_in = walking_state(this, 1) && walk_->state_ == 1;
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
        cache.destruct();
        return false;
    }

    // The drop: the arc from here to the hit, along the horizontal direction to it.
    hit = feeler.contact_pos_;
    hit_height = hit.y;
    alive = feeler.hit_object_alive();
    double dw = static_cast<double>(hit.w) - position->w;
    float dz = static_cast<float>(hit.z - static_cast<double>(position->z));
    double dx_unrounded = static_cast<double>(hit.x) - position->x;
    float dx = static_cast<float>(dx_unrounded);
    distance = static_cast<float>(std::sqrt(dx_unrounded * dx + static_cast<double>(dz) * dz));
    double sideways = static_cast<double>(direction.z) * dz + static_cast<double>(dx) * direction.x +
                      static_cast<double>(direction.y) * 0.0f;
    if (sideways < 0.0)
        distance = -distance;
    double inverse = 1.0 / distance;
    saved = *velocity;
    float inverse_float = static_cast<float>(inverse);
    direction = {static_cast<float>(static_cast<double>(inverse_float) * dx),
                 static_cast<float>(static_cast<double>(inverse_float) * 0.0f),
                 static_cast<float>(static_cast<double>(inverse_float) * dz), static_cast<float>(dw * inverse)};
    if (walking_state(this, 1)) {
        velocity->project_onto(direction);
        velocity->y = saved.y;
    }
    horizontal = static_cast<float>(
        std::sqrt(static_cast<double>(velocity->z) * velocity->z + static_cast<double>(velocity->x) * velocity->x));
    if (walking_state(this, 1)) {
        // (walking: at least the walk's speed - 0x0043DAA0)
        float least = static_cast<float>(walk_->run_speed());
        if (horizontal < least) {
            auto* walk = static_cast<objects::FootMovement*>(skater_object(this)->fetch_module(kWalk));
            velocity->x = static_cast<float>(static_cast<double>(least) * walk->facing_.x);
            walk = static_cast<objects::FootMovement*>(skater_object(this)->fetch_module(kWalk));
            velocity->z = static_cast<float>(static_cast<double>(least) * walk->facing_.z);
            horizontal = least;
        }
    }
    objects::GameEntity* object = object_of(this);
    if (pop) {
        // At least Physics_Acid_Drop_Pop_Speed up.
        float least = tuning_float_lookup(kPopSpeed, object);
        float upward = velocity->y;
        velocity->y = upward > least ? upward : least;
    }
    if (!walking_state(this, 1) || static_cast<uint32_t>(walk_->state_)) {
        // At most twice that.
        double most = static_cast<double>(tuning_float_lookup(kPopSpeed, object)) * 2.0;
        float upward = velocity->y;
        velocity->y = upward < most ? upward : static_cast<float>(most);
    }
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    float gravity = static_cast<float>(current_gravity(this));
    set_timed_flag(this, PoseFlag::kAcidDrop, 0);
    // The highest the arc gets on its way to the wall.
    float peak;
    if (distance > 0.0f && horizontal > kMinHorizontal) {
        double ratio = static_cast<double>(distance) / horizontal;
        peak = static_cast<float>((static_cast<double>(gravity) * ratio * 0.5f + velocity->y) * ratio + position->y);
    } else {
        peak = position->y;
    }
    if (walking_state(this, 1) && velocity->y > 0.0f && position->y < hit.y) {
        double upward = velocity->y;
        double top = upward * upward / (static_cast<double>(gravity) * -2.0f) + position->y;
        double to_wall = std::fabs(static_cast<double>(distance)) / horizontal;
        double to_top = -(upward / gravity);
        if (to_wall < to_top)
            peak = static_cast<float>(top);
    }
    auto fail = [&] {
        *velocity = saved;
        cache.destruct();
        return false;
    };
    if (peak < hit.y)
        return fail();
    float air_time =
        static_cast<float>(fall_time(this, static_cast<float>(position->y - static_cast<double>(hit.y)), velocity->y));
    if (script::fetch_float(kMinAirTime) > air_time)
        return fail();
    if (!(hit.y < peak))
        return fail();
    // Up the wall from the hit, 24 at a time, for a point the arc reaches with nothing in the way.
    float half_distance = static_cast<float>(static_cast<double>(distance) * 0.5f);
    for (;;) {
        feeler.line_.start = *position;
        float rise = static_cast<float>(position->y - static_cast<double>(hit.y));
        float half = static_cast<float>(fall_time(this, rise, velocity->y) * 0.5f);
        double scale = static_cast<double>(half_distance) / half;
        float y = static_cast<float>(static_cast<double>(half) * half * gravity * 0.5f +
                                     static_cast<double>(half) * velocity->y + position->y);
        double along = scale * half;
        float aw = static_cast<float>(direction.w * along), az = static_cast<float>(direction.z * along),
              ay = static_cast<float>(direction.y * along);
        double ax = along * direction.x;
        maths::Vec4& end = feeler.line_.end;
        end = {static_cast<float>(ax + position->x), static_cast<float>(static_cast<double>(ay) + y), position->z + az,
               position->w + aw};
        if (!feeler.get_hit_shape(true, false)) {
            // Nothing on the way up to the arc's middle: nothing on the way down from there to the point either?
            feeler.line_.start = feeler.line_.end;
            feeler.line_.end = {hit.x, hit.y + 1.0f, hit.z, hit.w};
            if (!feeler.get_hit_shape(true, false)) {
                drop->point = hit;
                drop->normal = normal;
                drop->hit_height_ = hit_height;
                drop->object_hit_ = alive;
                cache.destruct();
                return true;
            }
        }
        double higher = static_cast<double>(hit.y) + kRise;
        hit.y = static_cast<float>(higher);
        if (!(higher < peak))
            return fail();
    }
}

// 0x00502F20
void SkaterPhysicsModule::start_acid_drop(const AcidDrop* drop)
{
    // The horizontal direction to the drop point and the speed to get there in the fall's time.
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& position = object->at_point_;
    double dw = static_cast<double>(drop->point.w) - position.w;
    float dz = static_cast<float>(drop->point.z - static_cast<double>(position.z));
    double dx_unrounded = static_cast<double>(drop->point.x) - position.x;
    float dx = static_cast<float>(dx_unrounded);
    float distance = static_cast<float>(std::sqrt(dx_unrounded * dx + static_cast<double>(dz) * dz));
    const maths::Vec4& velocity = object->vel_;
    double sideways = static_cast<double>(dz) * velocity.z + static_cast<double>(dx) * velocity.x +
                      static_cast<double>(velocity.y) * 0.0f;
    if (sideways < 0.0)
        distance = -distance;
    double inverse = 1.0 / distance;
    float inverse_float = static_cast<float>(inverse);
    maths::Vec4 direction = {static_cast<float>(static_cast<double>(inverse_float) * dx),
                             static_cast<float>(static_cast<double>(inverse_float) * 0.0f),
                             static_cast<float>(static_cast<double>(inverse_float) * dz),
                             static_cast<float>(dw * inverse)};
    float rise = static_cast<float>(entity_->at_point_.y - static_cast<double>(drop->point.y));
    float time = static_cast<float>(fall_time(this, rise, entity_->vel_.y));
    float speed = static_cast<float>(static_cast<double>(distance) / time);
    maths::Vec4& flat = state_->drop_velocity_;
    flat.y = 0.0f;
    flat.w = 1.0f;
    flat.x = direction.x * speed;
    flat.z = direction.z * speed;
    g_acid_hold = state_->drop_velocity_; // (a copy kept for debugging, it seems)
    drop_target_height_ = drop->point.y;
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    set_timed_flag(this, PoseFlag::kVertAir, 1);
    set_timed_flag(this, PoseFlag::kFromAcidDrop, 1);
    set_timed_flag(this, PoseFlag::kVertWall, 0);
    set_timed_flag(this, PoseFlag::kSpin, 0);
    drop_object_hit_ = drop->object_hit_;
    entity_->vel_.x = 0.0f;
    entity_->vel_.z = 0.0f;
    maths::Mat4 start = entity_->basis_frame_;

    // The wall's orientation: along the drop turned into the wall's plane, its normal up, across them.
    const maths::Vec4& normal = drop->normal;
    double upward =
        -((static_cast<double>(direction.x) * normal.x + static_cast<double>(direction.z) * normal.z) / normal.y);
    double length = std::sqrt(static_cast<double>(direction.x) * direction.x + upward * upward +
                              static_cast<double>(direction.z) * direction.z);
    maths::Vec4 along = {direction.x, 0.0f, direction.z, direction.w};
    if (length > 0.0) {
        double scale = 1.0 / length;
        along.x = static_cast<float>(direction.x * scale);
        upward *= scale;
        along.z = static_cast<float>(direction.z * scale);
    }
    along.y = static_cast<float>(upward);
    along.flatten_onto_plane(normal);
    along.make_unit();
    maths::Vec4 up = normal;
    drop_heading_ = along;
    maths::Mat4 wall;
    wall.row[0] = {static_cast<float>(static_cast<double>(up.y) * along.z - static_cast<double>(up.z) * along.y),
                   static_cast<float>(static_cast<double>(up.z) * along.x - static_cast<double>(up.x) * along.z),
                   static_cast<float>(static_cast<double>(up.x) * along.y - static_cast<double>(up.y) * along.x), 0.0f};
    wall.row[1] = up;
    wall.row[2] = along;
    wall.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Mat4& stored = drop_matrix_;
    stored = wall;
    if (stored.row[2].y > 0.0f) { // (along the drop pointing up: turned round, across with it)
        stored.row[2] *= -1.0f;
        stored.row[0] *= -1.0f;
    }
    // Turned to face the way the skater faces (flattened) relative to the drop.
    object = object_of(this);
    maths::Vec4 facing = object->basis_frame_.row[2];
    facing.y = 0.0f;
    float turn = static_cast<float>(-maths::get_turn_around_axis(facing, direction, object->basis_frame_.row[1]));
    wall.yaw_locally(turn);
    drop_slerp_.set_matrices(start, wall);
    drop_time_ = 0.0f;
    drop_duration_ = time;
    drop_last_matrix_ = start;

    // SkaterAcidDropTriggered with DropHeight (how far above the hit's height the skater is).
    script::Struct* params = script::spawn_param_bag();
    params = params ? params->prepare_all() : nullptr;
    params->push_float(kDropHeight, static_cast<float>(entity_->at_point_.y - static_cast<double>(drop->hit_height_)));
    skater_object(this)->start_script_task(kAcidDropTriggered, -1, false, false, params);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    set_timed_flag(this, PoseFlag::kNoAcidDrop, 1);
    skater_object(this)->clear_event_handler(kOllied);
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

// 0x004FEE10 (not rewritten yet): a spine or hip to transfer to, looking along `look` from the vert wall of
// (horizontal) normal `normal`: true with the point to land on (`target`), its normal, whether it's a hip and
// whether the object hit is still there.
bool find_transfer_target(SkaterPhysicsModule* component, maths::Vec4* look, maths::Vec4* normal, bool* hip,
                          maths::Vec4* target, maths::Vec4* target_normal, bool* object_hit)
{
    return static_cast<SkaterPhysicsModule*>(component)->find_vert_target(look, normal, hip, target, target_normal,
                                                                          object_hit);
}

} // namespace

// 0x00505F70
bool SkaterPhysicsModule::try_spine_transfer()
{
    // The skater's up, flattened and normalised (the wall's way out), and the way back down the wall: against the
    // velocity, turned a quarter round about the horizontal across the wall.
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& up_row = object->basis_frame_.row[1];
    maths::Vec4 out = {up_row.x, 0.0f, up_row.z, 0.0f};
    {
        double length = std::sqrt(static_cast<double>(out.x) * out.x + static_cast<double>(out.z) * out.z);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            out.x = static_cast<float>(out.x * inverse);
            out.z = static_cast<float>(out.z * inverse);
        }
    }
    maths::Vec4 across = {-out.z, 0.0f, out.x, 0.0f};
    const maths::Vec4& velocity = object->vel_;
    maths::Vec4 down = {-velocity.x, -velocity.y, -velocity.z, -velocity.w};
    {
        double length = std::sqrt(static_cast<double>(down.x) * down.x + static_cast<double>(down.y) * down.y +
                                  static_cast<double>(down.z) * down.z);
        if (length > 0.0) { // (x, y, z only: w stays the velocity's, negated)
            double inverse = 1.0 / length;
            down.x = static_cast<float>(down.x * inverse);
            down.y = static_cast<float>(down.y * inverse);
            down.z = static_cast<float>(down.z * inverse);
        }
    }
    alignas(16) uint8_t rotation[0x10]; // (a quaternion, it seems)
    auto* quat = reinterpret_cast<maths::QuatRotation*>(rotation);
    quat->init_from_axis_angle(across, kQuarterTurn);
    maths::Vec4 turned;
    maths::Vec4 target = *quat->spin_by(&turned, down);

    // A feeler straight down from half a unit that way: the wall the skater is on.
    bool hip = false, object_hit = false;
    render::ProbeRay feeler;
    feeler.make_new();
    const maths::Vec4& position = object->at_point_;
    maths::Vec4 ahead = {target.x * kAhead, target.y * kAhead, target.z * kAhead, target.w * kAhead};
    feeler.line_.start = {static_cast<float>(static_cast<double>(ahead.x) + position.x),
                          static_cast<float>(static_cast<double>(ahead.y) + position.y),
                          static_cast<float>(static_cast<double>(ahead.z) + position.z),
                          static_cast<float>(static_cast<double>(ahead.w) + position.w)};
    feeler.line_.end = feeler.line_.start;
    feeler.line_.end.y = static_cast<float>(feeler.line_.end.y - static_cast<double>(kFeelerDepth));
    if (!feeler.get_hit_shape(true, false))
        return false;
    float wall_x = feeler.contact_pos_.x, wall_z = feeler.contact_pos_.z;
    maths::Vec4 normal = {feeler.surface_dir_.x, 0.0f, feeler.surface_dir_.z, feeler.surface_dir_.w};
    normal.make_unit();

    // Something to transfer to, looking back the way the skater came - or, with one of the two buttons (&input->pad_primary_.left_side_
    // / +0x80, not both) held, the same turned that way along the wall.
    maths::Vec4 back = {-target.x, -target.y, -target.z, -target.w};
    maths::Vec4 look = back;
    maths::Vec4 target_normal;
    if (!find_transfer_target(this, &look, &normal, &hip, &target, &target_normal, &object_hit)) {
        objects::PadInputModule* input = input_;
        if (!input->pad_primary_.left_side_.held_down_ == !input->pad_primary_.right_side_.held_down_)
            return false;
        if (input->pad_primary_.left_side_.held_down_) {
            look = {static_cast<float>(static_cast<double>(back.x) + -normal.z), back.y,
                    static_cast<float>(static_cast<double>(back.z) + normal.x),
                    static_cast<float>(static_cast<double>(back.w) + 1.0f)};
        } else {
            maths::Vec4 side = {normal.z, 0.0f, -normal.x, -1.0f};
            look = side + back;
        }
        look.make_unit();
        if (!find_transfer_target(this, &look, &normal, &hip, &target, &target_normal, &object_hit))
            return false;
    }

    // Found: how far the target is from the wall (flat), the target's normal flattened.
    float height = position.y; // (the target is taken at the skater's own height)
    double side_shift_x = static_cast<double>(target.x) - wall_x, off_z = static_cast<double>(target.z) - wall_z;
    float distance = static_cast<float>(std::sqrt(side_shift_x * side_shift_x + off_z * off_z));
    maths::Vec4 flat_normal = {target_normal.x, 0.0f, target_normal.z, target_normal.w};
    flat_normal.make_unit();
    maths::Vec4& object_velocity = object->vel_;
    maths::Vec4 old_velocity = object_velocity;
    double facing = static_cast<double>(flat_normal.z) * out.z + static_cast<double>(flat_normal.y) * out.y +
                    static_cast<double>(flat_normal.x) * out.x;
    if (std::fabs(facing) < kFlatTarget || distance > kFarTarget) { // (all of the speed straight up)
        object_velocity.y = static_cast<float>(length_unrounded(object_velocity));
        object_velocity.x = 0.0f;
        object_velocity.z = 0.0f;
    }
    float reach = static_cast<float>(static_cast<double>(distance) + 1.0f);
    float rise = static_cast<float>(position.y - static_cast<double>(height));
    float time = static_cast<float>(fall_time(this, rise, object_velocity.y));
    if (time < kLeastFallTime)
        time = kLeastFallTime;
    double inverse_time = 1.0 / time;
    float per_second = static_cast<float>(inverse_time);
    if (reach > kFarTarget) { // (fast enough to get there in the time?)
        double speed = inverse_time * reach;
        if (length_sqr_unrounded(object_velocity) < speed * speed)
            return false;
    }

    // Nothing in the way to it: the transfer (blocked: flagged for break_vert, which tries again).
    feeler.line_.start = position;
    feeler.line_.end = {target.x, position.y, target.z, target.w};
    if (feeler.get_hit_shape(true, false)) {
        transfer_blocked_ = 1;
        return true;
    }
    transfer_blocked_ = 0;

    // The way the skater lands: over a spine, towards the target along its slope; over a hip, straight up or down
    // (down when the target lies on the far side of its normal).
    double to_w = static_cast<double>(target.w) - position.w, to_z = static_cast<double>(target.z) - position.z;
    double to_x = static_cast<double>(target.x) - position.x;
    maths::Vec4 along;
    if (!hip) {
        along = {static_cast<float>(to_x),
                 static_cast<float>(-1.0f / static_cast<double>(target_normal.y) *
                                    (target_normal.z * to_z + target_normal.x * to_x)),
                 static_cast<float>(to_z), static_cast<float>(to_w)};
        along.make_unit();
    } else {
        along = {static_cast<float>(to_x), static_cast<float>(static_cast<double>(height) - position.y),
                 static_cast<float>(to_z), static_cast<float>(to_w)};
        along.make_unit();
        double side = static_cast<double>(along.z) * flat_normal.z + static_cast<double>(along.y) * flat_normal.y +
                      static_cast<double>(along.x) * flat_normal.x;
        along = {0.0f, side < 0.0 ? 1.0f : -1.0f, 0.0f, 1.0f};
    }

    // The orientation at the target (rows: across, the target's normal, along it in its plane), turned by the
    // skater's facing against its old velocity (none under 30 degrees); the slerp there from the skater's.
    maths::Mat4 start = object->basis_frame_;
    maths::Vec4 forward = along;
    forward.flatten_onto_plane(target_normal);
    forward.make_unit();
    maths::Mat4 end;
    end.row[0] = maths::cross_vec(target_normal, forward);
    end.row[1] = target_normal;
    end.row[2] = forward;
    end.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    drop_heading_ = forward;
    double turn = maths::get_turn_around_axis(object->basis_frame_.row[2], old_velocity, object->basis_frame_.row[1]);
    if (std::fabs(turn) < kLeastTurn)
        turn = 0.0;
    end.yaw_locally(static_cast<float>(-turn));
    maths::ArcRotationBlender& slerp = drop_slerp_;
    slerp.set_matrices(start, end);
    drop_time_ = 0.0f;
    drop_duration_ = time < kFlatTarget ? kFlatTarget : time;
    drop_last_matrix_ = start;
    maths::Mat4 halfway;
    slerp.fetch_mat(halfway, 0.5f);
    if (halfway.row[1].y < 0.0f) // (the short way round turns upside down: the long way)
        slerp.flip_heading();

    // The arc there in the fall's time: its horizontal velocity into the flags block, as the acid drop's.
    drop_target_height_ = height;
    float fly_x = static_cast<float>(target.x - static_cast<double>(position.x));
    float fly_y = static_cast<float>(height - static_cast<double>(position.y));
    float fly_z = static_cast<float>(target.z - static_cast<double>(position.z));
    maths::Vec4& fly = state_->drop_velocity_;
    fly = {static_cast<float>(static_cast<double>(per_second) * fly_x),
           static_cast<float>(static_cast<double>(per_second) * fly_y),
           static_cast<float>(static_cast<double>(per_second) * fly_z), static_cast<float>(to_w * per_second)};
    fly.y = 0.0f;
    skater_object(this)->start_script_task(hip ? kAwardHipTransfer : kAwardTransfer, 0, false, false, nullptr);
    skater_object(this)->clear_event_handler(kOllied);
    set_timed_flag(this, PoseFlag::kAcidDrop, 1);
    set_timed_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_timed_flag(this, PoseFlag::kVertWall, 0);
    set_timed_flag(this, PoseFlag::kLeftVert, 0);
    drop_object_hit_ = object_hit;
    return true;
}

} // namespace skater
