// The acid drop: looking ahead (and down) for a vert wall to drop into, and starting the drop - the skater thrown
// along an arc onto it, its matrix slerped round to the wall's.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kWalkSearchDistance = 0xE50A9D56; // Physics_Acid_Drop_Walking_On_Ground_Search_Distance
constexpr uint32_t kPopSpeed = 0x95A79C32;           // Physics_Acid_Drop_Pop_Speed
constexpr uint32_t kMinAirTime = 0x32C20F7E;         // Physics_Acid_Drop_Min_Air_Time
constexpr uint32_t kWalk = 0x726E85AA;               // Walk (a component)
constexpr uint32_t kDropHeight = 0xBB00FE40;         // DropHeight
constexpr uint32_t kAcidDropTriggered = 0xC7ED5FEF;  // SkaterAcidDropTriggered (a script)
constexpr uint32_t kOllied = 0x8FFEFB28;             // Ollied (an event)

constexpr uint32_t kWalkState = 0x1534;    // (+0x18: on the ground; +0x48: the walking direction)
constexpr uint32_t kDropFlags = 0xE4;      // (in the flags block: the drop's horizontal velocity)
constexpr uint32_t kDropTargetHeight = 0x37C;
constexpr uint32_t kRideObjectHit = 0x14C0;
constexpr uint32_t kDropMatrix = 0x3B0;    // the wall's orientation (rows: across, the normal, along)
constexpr uint32_t kDropDirection = 0x490;
constexpr uint32_t kSlerp = 0x3F0;
constexpr uint32_t kDropTime = 0x488;
constexpr uint32_t kDropDuration = 0x48C;
constexpr uint32_t kDropLastMatrix = 0x4A0;

constexpr float kMinSpeed = 0.01f;         // 0x0064544C
constexpr float kSearchReach = 500.0f;     // (0x43FA0000)
constexpr float kWalkLift = 200.0f;        // (0x43480000)
constexpr float kWalkBack = 12.0f;         // 0x0064546C
constexpr float kBoxDepth = 4200.0f;       // 0x0064BD34
constexpr float kFeelerDepth = 4000.0f;    // 0x0064B8F8
constexpr float kFirstStep = 0.01f;        // (0x3C23D70A)
constexpr float kMaxWallY = 0.707f;        // 0x006467BC
constexpr float kWalkFacing = -0.25f;      // 0x0064BD30
constexpr float kFacing = 0.05f;           // 0x00645D6C
constexpr float kFarStep = 100.0f;         // 0x006463D4
constexpr float kFarStepExtra = 24.0f;     // 0x00645EE8
constexpr float kStep = 3.0f;              // 0x006467A8
constexpr float kMinHorizontal = 0.0001f;  // 0x006453D4
constexpr float kNeverLands = 0.00001f;    // 0x006453B8
constexpr float kRise = 24.0f;             // 0x00645EE8

Obj::CCompositeObject* SkaterObject(void* component) { return static_cast<Obj::CCompositeObject*>(ObjectOf(component)); }

// The walking component's state (+0x1530's +0x2C): walking on the ground (its +0x118 clear and +0x1C == `state`).
bool WalkingState(void* component, int state)
{
    uint8_t* walk = At<uint8_t*>(At<void*>(component, kStateComponent), 0x2C);
    return !walk[0x118] && At<int>(walk, 0x1C) == state;
}

// The time to fall back to `height` below the start of a jump of upward speed `upward` (the gravity flag +0x78
// set while the gravity's read): (sqrt(upward^2 + 2 * height * g) + upward) / g, or 0.00001 if never.
double FallTime(void* component, float height, float upward)
{
    SetTimedFlag(component, 0x78, 1);
    double down = -Gravity(component);
    double discriminant = static_cast<double>(upward) * upward + 2.0 * (static_cast<double>(height) * down);
    double time;
    if (discriminant >= 0.0 || discriminant != discriminant)
        time = (std::sqrt(discriminant) + upward) / down;
    else
        time = kNeverLands;
    SetTimedFlag(component, 0x78, 0);
    return time;
}

} // namespace

// 0x00502240
bool CorePhysicsComponent::FindAcidDrop(bool pop, const Mth::Vector* position, const Mth::Vector* lastPosition,
                                        Mth::Vector* velocity, AcidDrop* drop)
{
    // Looking along the horizontal velocity (walking: the walking direction).
    Mth::Vector direction;
    if (WalkingState(this, 0)) {
        double x = velocity->x, z = velocity->z;
        double length = std::sqrt(x * x + z * z);
        if (length < kMinSpeed)
            return false;
        double inverse = 1.0 / length;
        direction = {static_cast<float>(inverse * x), static_cast<float>(0.0f * inverse),
                     static_cast<float>(z * inverse), static_cast<float>(velocity->w * inverse)};
    } else {
        direction = VectorAt(At<void*>(this, kWalkState), 0x48);
    }
    Mth::Vector base = *lastPosition;
    if (position->y > lastPosition->y)
        base.y = position->y;
    float reach = kSearchReach, lift = 0.0f;
    if (WalkingState(this, 1)) {
        if (!At<uint32_t>(At<void*>(this, kWalkState), 0x18)) {
            reach = Script::GetFloat(kWalkSearchDistance);
            lift = kWalkLift;
        } else {
            base.x = static_cast<float>(base.x - static_cast<double>(direction.x) * kWalkBack);
            base.y = static_cast<float>(base.y - static_cast<double>(direction.y) * kWalkBack);
            base.z = static_cast<float>(base.z - static_cast<double>(direction.z) * kWalkBack);
            base.w = static_cast<float>(base.w - static_cast<double>(direction.w) * kWalkBack);
        }
    }

    // The collision near the whole search, cached.
    Nx::CFeeler feeler;
    feeler.Construct();
    Nx::CCollCache cache;
    cache.Construct();
    Mth::BBox box;
    Mth::Vector corner = {base.x, base.y + lift, base.z, base.w};
    box.AddPoint(corner);
    corner.y = static_cast<float>(corner.y - static_cast<double>(kBoxDepth));
    box.AddPoint(corner);
    {
        float rz = direction.z * reach, ry = direction.y * reach, rx = direction.x * reach;
        double rw = static_cast<double>(direction.w) * reach;
        float y = base.y + ry;
        corner = {rx + base.x, y + lift, base.z + rz, static_cast<float>(rw + base.w)};
    }
    box.AddPoint(corner);
    corner.y = static_cast<float>(corner.y - static_cast<double>(kBoxDepth));
    box.AddPoint(corner);
    cache.Update(box);
    feeler.mp_cache = &cache;

    // Feelers straight down, further and further ahead, for a vert wall facing away (or, walking up to one, towards).
    Mth::Vector saved;
    bool found = false;
    Mth::Vector hit, normal;
    bool alive = false;
    float hitHeight = 0.0f, distance = 0.0f, horizontal = 0.0f;
    float step = kFirstStep;
    bool searching = kFirstStep < reach;
    while (searching) {
        float sz = direction.z * step, sy = direction.y * step, sx = direction.x * step;
        double sw = static_cast<double>(direction.w) * step;
        float x = sx + base.x, y = base.y + sy, z = base.z + sz, w = static_cast<float>(sw + base.w);
        feeler.m_line.start = {x, y + lift, z, w};
        feeler.m_line.end = {x, static_cast<float>(y - static_cast<double>(kFeelerDepth)), z, w};
        if (feeler.GetCollision(true, false) && (feeler.m_hit.faceFlags & 8) && std::fabs(feeler.m_normal.y) < kMaxWallY) {
            Mth::Vector flat = {feeler.m_normal.x, 0.0f, feeler.m_normal.z, feeler.m_normal.w};
            normal = feeler.m_normal;
            flat.Normalize();
            double facing = static_cast<double>(flat.z) * direction.z + static_cast<double>(flat.y) * direction.y +
                            static_cast<double>(flat.x) * direction.x;
            bool walkingIn = WalkingState(this, 1) && At<int>(At<void*>(this, kWalkState), 0x18) == 1;
            if ((walkingIn && facing <= kWalkFacing) || facing >= kFacing) {
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
        cache.Destruct();
        return false;
    }

    // The drop: the arc from here to the hit, along the horizontal direction to it.
    hit = feeler.m_point;
    hitHeight = hit.y;
    alive = feeler.HitObjectAlive();
    double dw = static_cast<double>(hit.w) - position->w;
    float dz = static_cast<float>(hit.z - static_cast<double>(position->z));
    double dxUnrounded = static_cast<double>(hit.x) - position->x;
    float dx = static_cast<float>(dxUnrounded);
    distance = static_cast<float>(std::sqrt(dxUnrounded * dx + static_cast<double>(dz) * dz));
    double sideways = static_cast<double>(direction.z) * dz + static_cast<double>(dx) * direction.x +
                      static_cast<double>(direction.y) * 0.0f;
    if (sideways < 0.0)
        distance = -distance;
    double inverse = 1.0 / distance;
    saved = *velocity;
    float inverseFloat = static_cast<float>(inverse);
    direction = {static_cast<float>(static_cast<double>(inverseFloat) * dx),
                 static_cast<float>(static_cast<double>(inverseFloat) * 0.0f),
                 static_cast<float>(static_cast<double>(inverseFloat) * dz), static_cast<float>(dw * inverse)};
    if (WalkingState(this, 1)) {
        velocity->ProjectOnto(direction);
        velocity->y = saved.y;
    }
    horizontal = static_cast<float>(
        std::sqrt(static_cast<double>(velocity->z) * velocity->z + static_cast<double>(velocity->x) * velocity->x));
    if (WalkingState(this, 1)) {
        // (walking: at least the walk's speed - 0x0043DAA0)
        float least = static_cast<float>(GameFunction<double(__thiscall*)(void*)>(0x0043DAA0)(At<void*>(this, kWalkState)));
        if (horizontal < least) {
            void* walk = SkaterObject(this)->GetComponent(kWalk);
            velocity->x = static_cast<float>(static_cast<double>(least) * At<float>(walk, 0x48));
            walk = SkaterObject(this)->GetComponent(kWalk);
            velocity->z = static_cast<float>(static_cast<double>(least) * At<float>(walk, 0x50));
            horizontal = least;
        }
    }
    void* object = ObjectOf(this);
    if (pop) {
        // At least Physics_Acid_Drop_Pop_Speed up.
        float least = PhysicsFloat(kPopSpeed, object);
        float upward = velocity->y;
        velocity->y = upward > least ? upward : least;
    }
    if (!WalkingState(this, 1) || At<uint32_t>(At<void*>(this, kWalkState), 0x18)) {
        // At most twice that.
        double most = static_cast<double>(PhysicsFloat(kPopSpeed, object)) * 2.0;
        float upward = velocity->y;
        velocity->y = upward < most ? upward : static_cast<float>(most);
    }
    SetTimedFlag(this, 0x78, 1);
    float gravity = static_cast<float>(Gravity(this));
    SetTimedFlag(this, 0x78, 0);
    // The highest the arc gets on its way to the wall.
    float peak;
    if (distance > 0.0f && horizontal > kMinHorizontal) {
        double ratio = static_cast<double>(distance) / horizontal;
        peak = static_cast<float>((static_cast<double>(gravity) * ratio * 0.5f + velocity->y) * ratio + position->y);
    } else {
        peak = position->y;
    }
    if (WalkingState(this, 1) && velocity->y > 0.0f && position->y < hit.y) {
        double upward = velocity->y;
        double top = upward * upward / (static_cast<double>(gravity) * -2.0f) + position->y;
        double toWall = std::fabs(static_cast<double>(distance)) / horizontal;
        double toTop = -(upward / gravity);
        if (toWall < toTop)
            peak = static_cast<float>(top);
    }
    auto fail = [&] {
        *velocity = saved;
        cache.Destruct();
        return false;
    };
    if (peak < hit.y)
        return fail();
    float airTime = static_cast<float>(FallTime(this, static_cast<float>(position->y - static_cast<double>(hit.y)),
                                               velocity->y));
    if (Script::GetFloat(kMinAirTime) > airTime)
        return fail();
    if (!(hit.y < peak))
        return fail();
    // Up the wall from the hit, 24 at a time, for a point the arc reaches with nothing in the way.
    float halfDistance = static_cast<float>(static_cast<double>(distance) * 0.5f);
    for (;;) {
        feeler.m_line.start = *position;
        float rise = static_cast<float>(position->y - static_cast<double>(hit.y));
        float half = static_cast<float>(FallTime(this, rise, velocity->y) * 0.5f);
        double scale = static_cast<double>(halfDistance) / half;
        float y = static_cast<float>(static_cast<double>(half) * half * gravity * 0.5f +
                                     static_cast<double>(half) * velocity->y + position->y);
        double along = scale * half;
        float aw = static_cast<float>(direction.w * along), az = static_cast<float>(direction.z * along),
              ay = static_cast<float>(direction.y * along);
        double ax = along * direction.x;
        Mth::Vector& end = feeler.m_line.end;
        end = {static_cast<float>(ax + position->x), static_cast<float>(static_cast<double>(ay) + y), position->z + az,
               position->w + aw};
        if (!feeler.GetCollision(true, false)) {
            // Nothing on the way up to the arc's middle: nothing on the way down from there to the point either?
            feeler.m_line.start = feeler.m_line.end;
            feeler.m_line.end = {hit.x, hit.y + 1.0f, hit.z, hit.w};
            if (!feeler.GetCollision(true, false)) {
                drop->point = hit;
                drop->normal = normal;
                drop->hitHeight = hitHeight;
                drop->objectHit = alive;
                cache.Destruct();
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
void CorePhysicsComponent::StartAcidDrop(const AcidDrop* drop)
{
    // The horizontal direction to the drop point and the speed to get there in the fall's time.
    void* object = ObjectOf(this);
    const Mth::Vector& position = VectorAt(object, kPosition);
    double dw = static_cast<double>(drop->point.w) - position.w;
    float dz = static_cast<float>(drop->point.z - static_cast<double>(position.z));
    double dxUnrounded = static_cast<double>(drop->point.x) - position.x;
    float dx = static_cast<float>(dxUnrounded);
    float distance = static_cast<float>(std::sqrt(dxUnrounded * dx + static_cast<double>(dz) * dz));
    const Mth::Vector& velocity = VectorAt(object, kVelocity);
    double sideways = static_cast<double>(dz) * velocity.z + static_cast<double>(dx) * velocity.x +
                      static_cast<double>(velocity.y) * 0.0f;
    if (sideways < 0.0)
        distance = -distance;
    double inverse = 1.0 / distance;
    float inverseFloat = static_cast<float>(inverse);
    Mth::Vector direction = {static_cast<float>(static_cast<double>(inverseFloat) * dx),
                             static_cast<float>(static_cast<double>(inverseFloat) * 0.0f),
                             static_cast<float>(static_cast<double>(inverseFloat) * dz),
                             static_cast<float>(dw * inverse)};
    float rise = static_cast<float>(VectorAt(ObjectOf(this), kPosition).y - static_cast<double>(drop->point.y));
    float time = static_cast<float>(FallTime(this, rise, VectorAt(ObjectOf(this), kVelocity).y));
    float speed = static_cast<float>(static_cast<double>(distance) / time);
    Mth::Vector& flat = VectorAt(FlagsOf(this), kDropFlags);
    flat.y = 0.0f;
    flat.w = 1.0f;
    flat.x = direction.x * speed;
    flat.z = direction.z * speed;
    GameVariable<Mth::Vector>(0x007CCDBC) = VectorAt(FlagsOf(this), kDropFlags); // (a copy kept for debugging, it seems)
    At<float>(this, kDropTargetHeight) = drop->point.y;
    SetTimedFlag(this, 0x78, 1);
    SetTimedFlag(this, 0x30, 1);
    SetTimedFlag(this, 0xA8, 1);
    SetTimedFlag(this, 0x38, 0);
    SetTimedFlag(this, 0x68, 0);
    At<uint8_t>(this, kRideObjectHit) = drop->objectHit;
    VectorAt(ObjectOf(this), kVelocity).x = 0.0f;
    VectorAt(ObjectOf(this), kVelocity).z = 0.0f;
    Mth::Matrix start = At<Mth::Matrix>(ObjectOf(this), kMatrix);

    // The wall's orientation: along the drop turned into the wall's plane, its normal up, across them.
    const Mth::Vector& normal = drop->normal;
    double upward = -((static_cast<double>(direction.x) * normal.x + static_cast<double>(direction.z) * normal.z) /
                      normal.y);
    double length = std::sqrt(static_cast<double>(direction.x) * direction.x + upward * upward +
                              static_cast<double>(direction.z) * direction.z);
    Mth::Vector along = {direction.x, 0.0f, direction.z, direction.w};
    if (length > 0.0) {
        double scale = 1.0 / length;
        along.x = static_cast<float>(direction.x * scale);
        upward *= scale;
        along.z = static_cast<float>(direction.z * scale);
    }
    along.y = static_cast<float>(upward);
    along.ProjectToPlane(normal);
    along.Normalize();
    Mth::Vector up = normal;
    VectorAt(this, kDropDirection) = along;
    Mth::Matrix wall;
    wall.row[0] = {static_cast<float>(static_cast<double>(up.y) * along.z - static_cast<double>(up.z) * along.y),
                   static_cast<float>(static_cast<double>(up.z) * along.x - static_cast<double>(up.x) * along.z),
                   static_cast<float>(static_cast<double>(up.x) * along.y - static_cast<double>(up.y) * along.x), 0.0f};
    wall.row[1] = up;
    wall.row[2] = along;
    wall.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Matrix& stored = At<Mth::Matrix>(this, kDropMatrix);
    stored = wall;
    if (stored.row[2].y > 0.0f) { // (along the drop pointing up: turned round, across with it)
        stored.row[2] *= -1.0f;
        stored.row[0] *= -1.0f;
    }
    // Turned to face the way the skater faces (flattened) relative to the drop.
    object = ObjectOf(this);
    Mth::Vector facing = VectorAt(object, kForward);
    facing.y = 0.0f;
    float turn = static_cast<float>(-Mth::GetAngleAbout(facing, direction, VectorAt(object, kUp)));
    wall.RotateYLocal(turn);
    At<Mth::SlerpInterpolator>(this, kSlerp).SetMatrices(start, wall);
    At<float>(this, kDropTime) = 0.0f;
    At<float>(this, kDropDuration) = time;
    At<Mth::Matrix>(this, kDropLastMatrix) = start;

    // SkaterAcidDropTriggered with DropHeight (how far above the hit's height the skater is).
    Script::CStruct* params = Script::NewStruct();
    params = params ? params->Init() : nullptr;
    params->AddFloat(kDropHeight,
                     static_cast<float>(VectorAt(ObjectOf(this), kPosition).y - static_cast<double>(drop->hitHeight)));
    SkaterObject(this)->SpawnAndRunScript(kAcidDropTriggered, -1, false, false, params);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    SetTimedFlag(this, 0xB0, 1);
    SkaterObject(this)->ClearEventHandler(kOllied);
}

} // namespace Skater

REPLACE(0x00502240, &Skater::CorePhysicsComponent::FindAcidDrop);
REPLACE(0x00502F20, &Skater::CorePhysicsComponent::StartAcidDrop);
