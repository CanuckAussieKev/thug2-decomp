// The skater's collision feelers: the component's own feeler check (what was hit: skatable, vert, a wall), the
// side collisions that push the skater off walls, the rail and ground probes, and its script member functions.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kWallNonSkatableAngle = 0x3EEDE4D3; // Wall_Non_Skatable_Angle (degrees)
constexpr uint32_t kRight = 0x4B358AEB;                // Right
constexpr uint32_t kLedge = 0x315D9ED4;                // Ledge (a member function)
constexpr uint32_t kObjMoveToNode = 0x8819DD8B;        // Obj_MoveToNode (a member function)
constexpr uint32_t kMotion = 0x0A015E17;               // motion (a component)
constexpr uint32_t kShadow = 0x8A897DD2;               // Shadow (a component)
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;  // SkaterFlipAndRotate (a component)
constexpr uint32_t kNoReset = 0xD607E2E6;              // NoReset
constexpr uint32_t kOrient = 0x90A91232;               // orient
constexpr uint32_t kNose = 0x7F03932C;                 // nose
constexpr uint32_t kTail = 0x83C84BA2;                 // tail
constexpr uint32_t kLeft = 0x85981897;                 // left
constexpr uint32_t kAxisX = 0x7323E97C;                // x
constexpr uint32_t kAxisZ = 0x9D2D8850;                // z
constexpr uint32_t kOff = 0xD443A2BC;                  // Off
constexpr uint32_t kSideCollideHeight = 0xB45B39E2;    // Skater_side_collide_height

// The feeler's hit (the feeler at +0xA8: its CollisionHit at +0xCC).
constexpr uint32_t kHitObjectChecksum = 0x100;
constexpr uint32_t kHitUnknown38 = 0x104;
constexpr uint32_t kHitFaceFlag40 = 0x115;
constexpr uint32_t kHitDistance = 0x11C;

// Face flags (the feeler's hit).
constexpr uint16_t kFaceSkatable = 0x1;
constexpr uint16_t kFaceNotSkatable = 0x2;
constexpr uint16_t kFaceWall = 0x4;
constexpr uint16_t kFaceVert = 0x8;

// The other fields used here.
constexpr uint32_t kLastRail = 0x22C;          // the rail node last ground
constexpr uint32_t kRailManager = 0x230;
constexpr uint32_t kRailLeftTime = 0x234;
constexpr uint32_t kRailWait = 0x23C;          // (ms) before the same rail can be ground again
constexpr uint32_t kOnLedge = 0x248;
constexpr uint32_t kNearestSide = 0x2D4;
constexpr uint32_t kSideNormalSum = 0x2AC;     // the side feelers' hit normals added up
constexpr uint32_t kSideNearest = 0x2BC;       // the nearest side hit (0..1 along the feeler)
constexpr uint32_t kSideDistance = 0x2C0;      // how far the side feelers reach (0: off)
constexpr uint32_t kSidePush = 0x2C4;
constexpr uint32_t kSideTurn = 0x2C8;
constexpr uint32_t kSideTimes = 0x328;         // the clock when each side last hit (4)

constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464
constexpr float kNearGroundReach = 16.0f;          // 0x00645D68
constexpr float kNearGroundHeight = 20.0f;         // 0x006486AC
constexpr float kRailEndDistance = 6.0f;           // 0x00646284

Obj::CCompositeObject* SkaterObject(void* component) { return static_cast<Obj::CCompositeObject*>(ObjectOf(component)); }

// 0x0057CB60 (the rail manager's, not rewritten yet): a rail node's position.
Mth::Vector RailNodePosition(void* manager, const void* node)
{
    Mth::Vector position;
    GameFunction<Mth::Vector*(__thiscall*)(void*, Mth::Vector*, const void*)>(0x0057CB60)(manager, &position, node);
    return position;
}

} // namespace

// 0x005003A0
bool CorePhysicsComponent::CheckFeeler(uint32_t ignore, uint32_t require)
{
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(&At<uint8_t>(this, kFeeler));
    feeler->SetLine(&VectorAt(this, kFeelerStart), &VectorAt(this, kFeelerEnd));
    feeler->SetIgnore(static_cast<uint16_t>(ignore), static_cast<uint16_t>(require));
    uint8_t hit = static_cast<uint8_t>(feeler->GetCollision(true, false));
    if (!hit)
        return false;
    if (At<uint32_t>(this, kHitObjectChecksum) && At<uint8_t>(this, kHitFaceFlag40))
        At<uint32_t>(this, kHitUnknown38) = 0;
    uint16_t flags = At<uint16_t>(this, kHitFlags);
    float normalY = VectorAt(this, kHitNormal).y;
    At<uint8_t>(this, kHitWall) = 0;
    At<uint8_t>(this, kHitVert) = (flags & kFaceVert) ? 1 : 0;
    bool skatable;
    if (flags & kFaceSkatable) {
        skatable = true;
    } else if (flags & kFaceNotSkatable) {
        skatable = false;
    } else if (flags & kFaceVert) {
        skatable = true;
    } else if (flags & kFaceWall) {
        At<uint8_t>(this, kHitSkatable) = 0;
        At<uint8_t>(this, kHitWall) = 1;
        return true;
    } else {
        // Neither: skatable unless steeper than Wall_Non_Skatable_Angle (its sine above the normal's y).
        double limit = X87Sin(static_cast<double>(Script::GetFloat(kWallNonSkatableAngle)) * kDegreesToRadians);
        skatable = !(limit > normalY);
    }
    At<uint8_t>(this, kHitSkatable) = skatable ? 1 : 0;
    return true;
}

// 0x00500020
bool CorePhysicsComponent::CanGrindRail(const void* rail, const void*, bool anyTime)
{
    struct RailNode {
        uint8_t unknown[0x40];
        const RailNode* previous; // +0x40
        const RailNode* next;     // +0x44
    };
    const auto* node = static_cast<const RailNode*>(rail);
    uint8_t* flags = FlagsOf(this);
    const auto* last = At<const RailNode*>(this, kLastRail);
    auto sameRail = [&] { return last == node || last == node->previous || last == node->next; };
    // In the air after a grind: not the same rail (or its neighbours) again.
    if (At<int>(flags, kState) == 1 && flags[0xA8] && flags[0xD0] && sameRail())
        return false;

    bool newRail = false;
    if (!last) {
        newRail = true;
    } else if (!sameRail()) {
        // Another rail. In a created park, only if its ends are all more than 6 from the last one's (pieces' rails
        // meet end to end).
        uint8_t* park = GameVariable<uint8_t*>(0x007CE5D8); // (the park editor)
        if (At<uint32_t>(park, 0x40) == kOff || !last->previous || !node->previous) {
            newRail = true;
        } else {
            // (Each pair in the original's order: this rail's end first, then the last one's.)
            auto between = [this](const void* from, const void* to) {
                Mth::Vector a = RailNodePosition(At<void*>(this, kRailManager), from);
                Mth::Vector b = RailNodePosition(At<void*>(this, kRailManager), to);
                return b - a;
            };
            Mth::Vector d1 = between(node, At<const RailNode*>(this, kLastRail));
            Mth::Vector d2 = between(node->previous, At<const RailNode*>(this, kLastRail));
            const RailNode* lastPrevious = At<const RailNode*>(this, kLastRail)->previous;
            Mth::Vector d3 = between(node, lastPrevious);
            lastPrevious = At<const RailNode*>(this, kLastRail)->previous;
            Mth::Vector d4 = between(node->previous, lastPrevious);
            newRail = std::fabs(LengthUnrounded(d1)) > kRailEndDistance && std::fabs(LengthUnrounded(d2)) > kRailEndDistance &&
                      std::fabs(LengthUnrounded(d3)) > kRailEndDistance && std::fabs(LengthUnrounded(d4)) > kRailEndDistance;
        }
    }
    if (newRail)
        SetTimedFlag(this, 0x50, 1);

    // The same rail: only after the wait since leaving it.
    if (!FlagsOf(this)[0x50] && Since(At<uint32_t>(this, kRailLeftTime)) <= At<uint32_t>(this, kRailWait))
        return false;
    if (anyTime)
        return true;
    flags = FlagsOf(this);
    if (At<int>(flags, kState) == 4)
        return false;
    // (On a vert wall, only while moving up.)
    if (flags[0x38] && !(VectorAt(ObjectOf(this), kVelocity).y > 0.0f))
        return false;
    return true;
}

// 0x005012B0
int CorePhysicsComponent::NearGround(Script::CStruct* params, Script::CScript*)
{
    bool right = params->FindComponent(kRight) != nullptr;
    void* object = ObjectOf(this);
    const Mth::Vector& side = VectorAt(object, kMatrix);
    const Mth::Vector& position = VectorAt(object, kPosition);
    double x = static_cast<double>(side.x) * kNearGroundReach; // (left unrounded)
    float y = side.y * kNearGroundReach, z = side.z * kNearGroundReach, w = side.w * kNearGroundReach;
    Mth::Vector& start = VectorAt(this, kFeelerStart);
    Mth::Vector& end = VectorAt(this, kFeelerEnd);
    // A feeler 20 up and down, a little to the side (Right: the other way).
    float startX, startY;
    if (right) {
        startX = static_cast<float>(position.x - x);
        startY = position.y - y;
        start.z = position.z - z;
        start.w = position.w - w;
    } else {
        startX = static_cast<float>(x + position.x);
        startY = y + position.y;
        start.z = z + position.z;
        start.w = w + position.w;
    }
    start.x = startX;
    start.y = startY;
    start.y = start.y + kNearGroundHeight;
    end = start;
    end.y = end.y - kNearGroundHeight;
    return CheckFeeler(0x10, 0) ? 1 : 0;
}

// 0x00501600
int CorePhysicsComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name == kLedge)
        return At<uint8_t>(this, kOnLedge) ? 1 : 0;
    if (name != kObjMoveToNode)
        return 2;
    // Obj_MoveToNode: the motion component moves the skater; then everything that follows the position is put there.
    Obj::CCompositeObject* object = SkaterObject(this);
    Obj::CBaseComponent* motion = object->GetComponent(kMotion);
    bool moved = motion->CallMemberFunction(kObjMoveToNode, params, script) != 0;
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    void* shadow = SkaterObject(this)->GetComponent(kShadow);
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004317C0)(shadow, &VectorAt(ObjectOf(this), kPosition));
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004317E0)(shadow, &VectorAt(ObjectOf(this), kUp));
    VectorAt(this, kSafePosition) = VectorAt(ObjectOf(this), kPosition);
    VectorAt(ObjectOf(this), kOldPosition) = VectorAt(ObjectOf(this), kPosition);
    SkaterObject(this)->SetTeleported(true);
    if (At<uint8_t>(ObjectOf(this), 0xBC)) {
        // (the skater's camera follows at once)
        void* camera = GameFunction<void*(__thiscall*)(void*)>(0x00580880)(ObjectOf(this));
        if (camera)
            static_cast<Obj::CCompositeObject*>(camera)->Update();
    }
    if (!params->ContainsFlag(kNoReset)) {
        At<uint8_t>(this, 0x1C5) = 0;
        VectorAt(ObjectOf(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        SetState(this, 0);
        SetTimedFlag(this, 0x28, At<uint8_t>(ObjectOf(this), 0x110) == 0);
        void* flipAndRotate = SkaterObject(this)->GetComponent(kSkaterFlipAndRotate);
        GameFunction<void(__thiscall*)(void*)>(0x00511420)(flipAndRotate);
    } else if (params->ContainsFlag(kOrient)) {
        // The horizontal speed kept, along the new facing.
        Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
        float upward = velocity.y;
        velocity.y = 0.0f;
        void* o = ObjectOf(this);
        float speed = static_cast<float>(LengthUnrounded(VectorAt(o, kVelocity)));
        VectorAt(o, kVelocity) = VectorAt(o, kForward) * speed;
        VectorAt(ObjectOf(this), kVelocity).y = upward;
    }
    return moved ? 1 : 0;
}

// 0x005009C0
void CorePhysicsComponent::FindNearestWallSide()
{
    Nx::CFeeler feeler;
    feeler.Construct();
    At<uint32_t>(this, kNearestSide) = 0;
    feeler.m_line.start = VectorAt(ObjectOf(this), kPosition);
    float nearest = 999.0f;
    // Each end and side of the board: 12 along the forward or side row.
    static const struct {
        uint32_t side;
        uint32_t axis;
        float distance;
    } kSides[4] = {{kNose, kAxisZ, 12.0f}, {kTail, kAxisZ, -12.0f}, {kLeft, kAxisX, 12.0f}, {kRight, kAxisX, -12.0f}};
    for (const auto& side : kSides) {
        const Mth::Vector& row = VectorAt(ObjectOf(this), side.axis == kAxisX ? kMatrix : kForward);
        const Mth::Vector& start = feeler.m_line.start;
        double distance = side.distance;
        Mth::Vector& end = feeler.m_line.end;
        end.x = static_cast<float>(distance * row.x + start.x);
        end.y = static_cast<float>(distance * row.y + start.y);
        end.z = static_cast<float>(distance * row.z + start.z);
        end.w = static_cast<float>(distance * row.w + start.w);
        if (feeler.GetCollision(true, false) && feeler.m_distance < nearest) {
            nearest = feeler.m_distance;
            At<uint32_t>(this, kNearestSide) = side.side;
        }
    }
}

// 0x00501C90
void CorePhysicsComponent::CheckSideCollision(const Mth::Vector* start, const Mth::Vector* end, uint32_t side)
{
    // (The original's loop over the other three sides checks the opposite side's time each time round.)
    for (uint32_t other = 0; other < 4; ++other) {
        if (other != side && Since(At<uint32_t>(this, kSideTimes + (side ^ 1) * 4)) > 500)
            return;
    }
    VectorAt(this, kFeelerStart) = *start;
    VectorAt(this, kFeelerEnd) = *end;
    if (!CheckFeeler(0x10, 0))
        return;
    Mth::Vector& sum = VectorAt(this, kSideNormalSum);
    const Mth::Vector& normal = VectorAt(this, kHitNormal);
    sum.x = normal.x + sum.x;
    sum.y = normal.y + sum.y;
    sum.z = normal.z + sum.z;
    sum.w = normal.w + sum.w;
    if (At<float>(this, kHitDistance) < At<float>(this, kSideNearest))
        At<float>(this, kSideNearest) = At<float>(this, kHitDistance);
    At<int>(this, kSideTimes + side * 4) = Now();
}

// 0x005055B0
void CorePhysicsComponent::CheckSideCollisions()
{
    if (At<float>(this, kSideDistance) == 0.0f || At<float>(this, kSidePush) == 0.0f)
        return;
    VectorAt(this, kSideNormalSum) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kSideNearest) = 1.0f;
    // The feelers start at the side collision height up the skater.
    void* object = ObjectOf(this);
    double height = Script::GetFloat(kSideCollideHeight);
    const Mth::Vector& up = VectorAt(object, kUp);
    const Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
    Mth::Vector base;
    base.x = static_cast<float>(height * up.x + position.x);
    base.y = static_cast<float>(height * up.y) + position.y;
    base.z = static_cast<float>(height * up.z) + position.z;
    base.w = static_cast<float>(height * up.w) + position.w;
    Mth::Vector start = base;
    // Sides 0 and 1: along the side row and back; 2 and 3: along the forward row and back.
    for (uint32_t side = 0; side < 4; ++side) {
        const Mth::Vector& row = VectorAt(ObjectOf(this), side < 2 ? kMatrix : kForward);
        double reach = At<float>(this, kSideDistance);
        float x = static_cast<float>(reach * row.x), y = static_cast<float>(reach * row.y),
              z = static_cast<float>(reach * row.z);
        double w = reach * row.w; // (left unrounded)
        Mth::Vector end;
        if (side % 2 == 0) {
            end.x = x + base.x;
            end.y = y + base.y;
            end.z = z + base.z;
            end.w = static_cast<float>(w + base.w);
        } else {
            end.x = base.x - x;
            end.y = base.y - y;
            end.z = base.z - z;
            end.w = static_cast<float>(base.w - w);
        }
        CheckSideCollision(&start, &end, side);
    }
    if (At<uint32_t>(this, kSideNearest) == 0x3F800000u) // (nothing hit: still 1)
        return;
    // Pushed off by the walls hit, the nearer the harder; turned along a wall it faces into.
    Mth::Vector& push = VectorAt(this, kSideNormalSum);
    SetLength(push, static_cast<float>((1.0 - At<float>(this, kSideNearest)) * At<float>(this, kSidePush)));
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    velocity.x = push.x + velocity.x;
    velocity.y = push.y + velocity.y;
    velocity.z = push.z + velocity.z;
    velocity.w = push.w + velocity.w;
    VectorAt(ObjectOf(this), kVelocity).RotateToPlane(VectorAt(this, kGroundNormal));
    object = ObjectOf(this);
    const Mth::Vector& forward = VectorAt(object, kForward);
    const Mth::Vector& normal = VectorAt(this, kHitNormal);
    double into = static_cast<double>(forward.z) * normal.z + static_cast<double>(forward.y) * normal.y +
                  static_cast<double>(forward.x) * normal.x;
    if (!(into < 0.0))
        return;
    Mth::Vector along = forward;
    along.RotateToPlane(normal);
    float angle = static_cast<float>(Mth::GetAngleAbout(VectorAt(ObjectOf(this), kForward), along,
                                                        VectorAt(ObjectOf(this), kUp)));
    double turn = static_cast<double>(Mth::Sign(angle)) * At<float>(this, kSideTurn) * At<float>(this, kFrameLength);
    float step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn))
        step = angle;
    At<Mth::Matrix>(ObjectOf(this), kMatrix).RotateYLocal(step);
    At<Mth::Matrix>(this, kDisplayMatrix).RotateYLocal(step);
}

// 0x005054A0
bool CorePhysicsComponent::ApplyModelRotations()
{
    void* model = SkaterObject(this)->GetComponent(0x286A8D26); // (Model)
    if (!At<uint8_t>(model, 0xC8) && !At<uint8_t>(model, 0x15C) && !At<uint8_t>(model, 0x1F0))
        return false;
    void* object = ObjectOf(this);
    Mth::Vector axis = VectorAt(object, kUp);
    GameFunction<void(__thiscall*)(void*, Mth::Matrix*)>(0x004215F0)(model, &At<Mth::Matrix>(object, kMatrix));
    uint8_t* yRotation = &At<uint8_t>(model, 0xCC);
    float angle = GameFunction<float(__thiscall*)(void*)>(0x004213E0)(yRotation);
    if (angle != 0.0f) {
        // The velocity turned with the model about the up axis.
        Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
        alignas(16) uint8_t rotation[0x10]; // (a quaternion, it seems)
        GameFunction<void*(__thiscall*)(void*, Mth::Vector*, float)>(0x00420AC0)(rotation, &axis, angle);
        velocity = *GameFunction<Mth::Vector*(__thiscall*)(void*, Mth::Vector*, Mth::Vector*)>(0x00420B50)(
            rotation, &axis, &velocity);
    }
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    auto reset = GameFunction<void(__thiscall*)(void*)>(0x004204F0);
    reset(&At<uint8_t>(model, 0x38));
    reset(yRotation);
    reset(&At<uint8_t>(model, 0x160));
    return true;
}

} // namespace Skater

REPLACE(0x005003A0, &Skater::CorePhysicsComponent::CheckFeeler);
REPLACE(0x00500020, &Skater::CorePhysicsComponent::CanGrindRail);
REPLACE(0x005012B0, &Skater::CorePhysicsComponent::NearGround);
REPLACE(0x00501600, &Skater::CorePhysicsComponent::CallMemberFunction);
REPLACE(0x005009C0, &Skater::CorePhysicsComponent::FindNearestWallSide);
REPLACE(0x00501C90, &Skater::CorePhysicsComponent::CheckSideCollision);
REPLACE(0x005055B0, &Skater::CorePhysicsComponent::CheckSideCollisions);
REPLACE(0x005054A0, &Skater::CorePhysicsComponent::ApplyModelRotations);
