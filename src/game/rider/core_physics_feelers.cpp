// The skater's collision feelers: the component's own feeler check (what was hit: skatable, vert, a wall), the
// side collisions that push the skater off walls, the rail and ground probes, and its script member functions.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/maths.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/shadow_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/park_editing/park_editor.h"

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
constexpr uint32_t kLip = 0xA549B57B;                  // Lip
constexpr uint32_t kLipHorizOffset = 0xB92CBE3B;       // SkateInAble_LipHorizOffset
constexpr uint32_t kLipDownOffset = 0xEFDFE781;        // SkateInAble_LipDownOffset
constexpr uint32_t kLipExtraHorizOffset = 0x108613CB;  // SkateInAble_LipExtraCheckHorizOffset
constexpr uint32_t kLipExtraDownOffset = 0xF9DC446D;   // SkateInAble_LipExtraCheckDownOffset
constexpr uint32_t kSidewaysShift = 0xC78EBE56;          // SkateInAble_HorizOffset
constexpr uint32_t kDownOffset = 0xFCA3378C;           // SkateInAble_DownOffset
constexpr uint32_t kCount = 0x7A26BB9D;                // count
constexpr uint32_t kAmmoMeterChange = 0x0C8E5500;      // ammo_meter_change (a script)
constexpr uint32_t kDir = 0x455485EF;                  // dir
constexpr uint32_t kUpLeft = 0xB7231A95;               // UpLeft
constexpr uint32_t kUpRight = 0xA50950C5;              // UpRight
constexpr uint32_t kDownLeft = 0xD8847EFA;             // DownLeft
constexpr uint32_t kDownRight = 0x786B8B68;            // DownRight
constexpr uint32_t kDown = 0xE3006FC4;                 // Down

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
constexpr uint32_t kProjectileCount = 0x2F8;   // (the ammo)

constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kNearGroundReach = 16.0f;          // 0x00645D68
constexpr float kNearGroundHeight = 20.0f;         // 0x006486AC
constexpr float kRailEndDistance = 6.0f;           // 0x00646284
// The natas exit's turns about Y (radians; the code's immediates).
constexpr float kEighthTurn = 0.785398185f;        // 0x3F490FDB
constexpr float kQuarterTurn = 1.57079637f;        // 0x3FC90FDB
constexpr float kThreeEighthsTurn = 2.3561945f;    // 0x4016CBE4
constexpr float kHalfTurn = 3.14159274f;           // 0x40490FDB

GameObjects::CGameEntity* SkaterObject(void* component) { return static_cast<GameObjects::CGameEntity*>(ObjectOf(component)); }

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C): out = a * b.
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// 0x0057CB60 (GameObjects::CGrindRailHub::GetAbsolutePos): a rail node's position.
Maths::Vec4 RailNodePosition(void* manager, const void* node)
{
    Maths::Vec4 position;
    static_cast<GameObjects::CGrindRailHub*>(manager)->GetAbsolutePos(&position, static_cast<const GameObjects::CGrindPoint*>(node));
    return position;
}

} // namespace

// 0x005003A0
bool SkaterPhysicsModule::ProbeTouchesZone(uint32_t ignore, uint32_t require)
{
    auto* feeler = reinterpret_cast<Engine3D::CProbeRay*>(&At<uint8_t>(this, kFeeler));
    feeler->ChangeStrand(&VectorAt(this, kProbeOrigin), &VectorAt(this, kProbeTip));
    feeler->SetExcluded(static_cast<uint16_t>(ignore), static_cast<uint16_t>(require));
    uint8_t hit = static_cast<uint8_t>(feeler->GetHitShape(true, false));
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
        double limit = X87Sin(static_cast<double>(Scripts::FetchFloat(kWallNonSkatableAngle)) * kRadiansPerDegree);
        skatable = !(limit > normalY);
    }
    At<uint8_t>(this, kHitSkatable) = skatable ? 1 : 0;
    return true;
}

// 0x00500020
bool SkaterPhysicsModule::CanGrindRail(const void* rail, const void*, bool anyTime)
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
        uint8_t* park = reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor); // (the park editor)
        if (At<uint32_t>(park, 0x40) == kOff || !last->previous || !node->previous) {
            newRail = true;
        } else {
            // (Each pair in the original's order: this rail's end first, then the last one's.)
            auto between = [this](const void* from, const void* to) {
                Maths::Vec4 a = RailNodePosition(At<void*>(this, kRailManager), from);
                Maths::Vec4 b = RailNodePosition(At<void*>(this, kRailManager), to);
                return b - a;
            };
            Maths::Vec4 d1 = between(node, At<const RailNode*>(this, kLastRail));
            Maths::Vec4 d2 = between(node->previous, At<const RailNode*>(this, kLastRail));
            const RailNode* lastPrevious = At<const RailNode*>(this, kLastRail)->previous;
            Maths::Vec4 d3 = between(node, lastPrevious);
            lastPrevious = At<const RailNode*>(this, kLastRail)->previous;
            Maths::Vec4 d4 = between(node->previous, lastPrevious);
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
int SkaterPhysicsModule::NearGround(Scripts::CStruct* params, Scripts::CScript*)
{
    bool right = params->FindComponent(kRight) != nullptr;
    void* object = ObjectOf(this);
    const Maths::Vec4& side = VectorAt(object, kMatrix);
    const Maths::Vec4& position = VectorAt(object, kPosition);
    double x = static_cast<double>(side.x) * kNearGroundReach; // (left unrounded)
    float y = side.y * kNearGroundReach, z = side.z * kNearGroundReach, w = side.w * kNearGroundReach;
    Maths::Vec4& start = VectorAt(this, kProbeOrigin);
    Maths::Vec4& end = VectorAt(this, kProbeTip);
    // A feeler 20 up and down, a little to the side (Right: the other way).
    float originCoordX, originCoordY;
    if (right) {
        originCoordX = static_cast<float>(position.x - x);
        originCoordY = position.y - y;
        start.z = position.z - z;
        start.w = position.w - w;
    } else {
        originCoordX = static_cast<float>(x + position.x);
        originCoordY = y + position.y;
        start.z = z + position.z;
        start.w = w + position.w;
    }
    start.x = originCoordX;
    start.y = originCoordY;
    start.y = start.y + kNearGroundHeight;
    end = start;
    end.y = end.y - kNearGroundHeight;
    return ProbeTouchesZone(0x10, 0) ? 1 : 0;
}

// 0x00500F10
int SkaterPhysicsModule::SkateInAble(Scripts::CStruct* params, Scripts::CScript*)
{
    void* object = ObjectOf(this);
    Maths::Vec4& start = VectorAt(this, kProbeOrigin);
    Maths::Vec4& end = VectorAt(this, kProbeTip);
    // (each line's x - and across the skater its y - from an unrounded product, as the original's FPU code has them)
    if (params->HasBareFlag(kLip)) {
        // Down from just behind the position (back along up), then from a little further.
        float back = TuningFloatLookup(kLipHorizOffset, object);
        float down = TuningFloatLookup(kLipDownOffset, object);
        end = VectorAt(ObjectOf(this), kPosition);
        end.y = end.y - down;
        const Maths::Vec4& up = VectorAt(ObjectOf(this), kUp);
        start.x = static_cast<float>(end.x - static_cast<double>(back) * up.x);
        start.y = end.y - back * up.y;
        start.z = end.z - back * up.z;
        start.w = end.w - back * up.w;
        if (!ProbeTouchesZone(0x10, 0)) {
            back = TuningFloatLookup(kLipExtraHorizOffset, object);
            down = TuningFloatLookup(kLipExtraDownOffset, object);
            const Maths::Vec4& position = VectorAt(ObjectOf(this), kPosition);
            const Maths::Vec4& up2 = VectorAt(ObjectOf(this), kUp);
            start.x = static_cast<float>(position.x - static_cast<double>(back) * up2.x);
            start.y = position.y - back * up2.y;
            start.z = position.z - back * up2.z;
            start.w = position.w - back * up2.w;
            end = start;
            end.y = end.y - down;
            if (!ProbeTouchesZone(0x10, 0))
                return 0;
        }
        return At<uint8_t>(this, kHitVert) ? 1 : 0;
    }
    // Across the skater: from `across` to one side (lowered) to as far to the other.
    float across = TuningFloatLookup(kSidewaysShift, object);
    const Maths::Vec4& side = VectorAt(ObjectOf(this), kMatrix);
    const Maths::Vec4& position = VectorAt(ObjectOf(this), kPosition);
    Maths::Vec4 nearSide;
    nearSide.x = static_cast<float>(static_cast<double>(across) * side.x + position.x);
    nearSide.y = static_cast<float>(static_cast<double>(across) * side.y + position.y);
    nearSide.z = across * side.z + position.z;
    nearSide.w = across * side.w + position.w;
    float down = TuningFloatLookup(kDownOffset, object);
    nearSide.y = nearSide.y - down;
    const Maths::Vec4& side2 = VectorAt(ObjectOf(this), kMatrix);
    const double twice = static_cast<double>(across) + across;
    Maths::Vec4 farSide;
    farSide.x = static_cast<float>(nearSide.x - twice * side2.x);
    farSide.y = nearSide.y - static_cast<float>(twice * side2.y);
    farSide.z = nearSide.z - static_cast<float>(twice * side2.z);
    farSide.w = nearSide.w - static_cast<float>(twice * side2.w);
    if (params->HasBareFlag(kLeft)) {
        start = nearSide;
        end = farSide;
    } else {
        start = farSide;
        end = nearSide;
    }
    if (!ProbeTouchesZone(0x10, 0))
        return 0;
    return At<uint8_t>(this, kHitVert) ? 1 : 0;
}

// 0x005013E0
int SkaterPhysicsModule::SetAmmo(Scripts::CStruct* params, Scripts::CScript*)
{
    int& ammo = At<int>(this, kProjectileCount);
    int before = ammo;
    params->FetchIntValue(uint32_t{0}, &ammo, 0); // (the unnamed integer)
    int change = ammo - before;
    Scripts::CStruct* changed = Scripts::SpawnParamBag();
    changed = changed ? changed->PrepareAll() : nullptr;
    changed->PutInt(kCount, change); // (on a null structure if out of memory, as the original)
    Scripts::EndChildScriptsOfEntityByName(SkaterObject(this), kAmmoMeterChange, nullptr);
    SkaterObject(this)->SpawnScript(kAmmoMeterChange, changed, 0, false);
    if (changed) {
        changed->Clear();
        Scripts::DeleteStruct(changed);
    }
    return 1;
}

// 0x005014E0 (some turns by Mat4::RotateY, the others by a rotation matrix and D3DXMatrixMultiply, as the original
// has them)
int SkaterPhysicsModule::CheckNatasExitDirection(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t dir = 0; // (left as it was on the stack without one, in the original)
    params->ReadHashValue(kDir, &dir, 1);
    Maths::Mat4 m;
    m = At<Maths::Mat4>(static_cast<GameObjects::CRider*>(SkaterObject(this))->GetSkaterCamera(), kMatrix);
    switch (dir) {
    case kDownRight:
        m.RotateY(kEighthTurn);
        break;
    case kRight:
        m.RotateY(kQuarterTurn);
        break;
    case kLeft:
        m.RotateY(-kQuarterTurn);
        break;
    case kUpRight:
        m.RotateY(kThreeEighthsTurn);
        break;
    case kDown:
        m.RotateY(0.0f);
        break;
    default: { // UpLeft, DownLeft; Up and anything else half a turn
        float turn = dir == kUpLeft ? -kThreeEighthsTurn : dir == kDownLeft ? -kEighthTurn : kHalfTurn;
        Maths::Mat4 rotation;
        MatrixMultiply(&m, &m, &Maths::MakeYawMatrix(rotation, turn));
        break;
    }
    }
    At<Maths::Mat4>(ObjectOf(this), kMatrix) = m;
    return 1;
}

// 0x00501600
int SkaterPhysicsModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    if (name == kLedge)
        return At<uint8_t>(this, kOnLedge) ? 1 : 0;
    if (name != kObjMoveToNode) {
        // Not original - THAW's bike member functions (SkaterOnBike, IsSkaterOnBike...: game/rider/thaw_bmx.h)
        // while [features] thaw_bmx is on.
        // Original behaviour: any other name isn't this component's (2).
        if (ThawBmx::IsSwitchedOn())
            return ThawBmx::InvokeMethodByName(this, name, params, script);
        return 2;
    }
    // Obj_MoveToNode: the motion component moves the skater; then everything that follows the position is put there.
    GameObjects::CGameEntity* object = SkaterObject(this);
    GameObjects::CObjectModule* motion = object->FetchModule(kMotion);
    bool moved = motion->InvokeMethodByName(kObjMoveToNode, params, script) != 0;
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    auto* shadow = static_cast<GameObjects::CGroundShadowModule*>(SkaterObject(this)->FetchModule(kShadow));
    shadow->ChangePlace(&VectorAt(ObjectOf(this), kPosition));
    shadow->SetNormal(&VectorAt(ObjectOf(this), kUp));
    VectorAt(this, kSafePosition) = VectorAt(ObjectOf(this), kPosition);
    VectorAt(ObjectOf(this), kOldPosition) = VectorAt(ObjectOf(this), kPosition);
    SkaterObject(this)->MarkJumpedAhead(true);
    if (At<uint8_t>(ObjectOf(this), 0xBC)) {
        // (the skater's camera follows at once)
        void* camera = static_cast<GameObjects::CRider*>(ObjectOf(this))->GetSkaterCamera();
        if (camera)
            static_cast<GameObjects::CGameEntity*>(camera)->TickFrame();
    }
    if (!params->HasBareFlag(kNoReset)) {
        At<uint8_t>(this, 0x1C5) = 0;
        VectorAt(ObjectOf(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        Physics::ChangePhase(this, 0);
        SetTimedFlag(this, 0x28, At<uint8_t>(ObjectOf(this), 0x110) == 0);
        static_cast<GameObjects::CRiderSpinAndFlip*>(SkaterObject(this)->FetchModule(kSkaterFlipAndRotate))
            ->ApplyFlipToAnims();
    } else if (params->HasBareFlag(kOrient)) {
        // The horizontal speed kept, along the new facing.
        Maths::Vec4& velocity = VectorAt(ObjectOf(this), kVelocity);
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
void SkaterPhysicsModule::FindNearestWallSide()
{
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
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
        const Maths::Vec4& row = VectorAt(ObjectOf(this), side.axis == kAxisX ? kMatrix : kForward);
        const Maths::Vec4& start = feeler.m_line.start;
        double distance = side.distance;
        Maths::Vec4& end = feeler.m_line.end;
        end.x = static_cast<float>(distance * row.x + start.x);
        end.y = static_cast<float>(distance * row.y + start.y);
        end.z = static_cast<float>(distance * row.z + start.z);
        end.w = static_cast<float>(distance * row.w + start.w);
        if (feeler.GetHitShape(true, false) && feeler.m_reach < nearest) {
            nearest = feeler.m_reach;
            At<uint32_t>(this, kNearestSide) = side.side;
        }
    }
}

// 0x00501C90
void SkaterPhysicsModule::CheckSideFeeler(const Maths::Vec4* start, const Maths::Vec4* end, uint32_t side)
{
    // (The original's loop over the other three sides checks the opposite side's time each time round.)
    for (uint32_t other = 0; other < 4; ++other) {
        if (other != side && Since(At<uint32_t>(this, kSideTimes + (side ^ 1) * 4)) > 500)
            return;
    }
    VectorAt(this, kProbeOrigin) = *start;
    VectorAt(this, kProbeTip) = *end;
    if (!ProbeTouchesZone(0x10, 0))
        return;
    Maths::Vec4& sum = VectorAt(this, kSideNormalSum);
    const Maths::Vec4& normal = VectorAt(this, kHitNormal);
    sum.x = normal.x + sum.x;
    sum.y = normal.y + sum.y;
    sum.z = normal.z + sum.z;
    sum.w = normal.w + sum.w;
    if (At<float>(this, kHitDistance) < At<float>(this, kSideNearest))
        At<float>(this, kSideNearest) = At<float>(this, kHitDistance);
    At<int>(this, kSideTimes + side * 4) = Now();
}

// 0x005055B0
void SkaterPhysicsModule::CheckSideFeelers()
{
    if (At<float>(this, kSideDistance) == 0.0f || At<float>(this, kSidePush) == 0.0f)
        return;
    VectorAt(this, kSideNormalSum) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kSideNearest) = 1.0f;
    // The feelers start at the side collision height up the skater.
    void* object = ObjectOf(this);
    double height = Scripts::FetchFloat(kSideCollideHeight);
    const Maths::Vec4& up = VectorAt(object, kUp);
    const Maths::Vec4& position = VectorAt(ObjectOf(this), kPosition);
    Maths::Vec4 base;
    base.x = static_cast<float>(height * up.x + position.x);
    base.y = static_cast<float>(height * up.y) + position.y;
    base.z = static_cast<float>(height * up.z) + position.z;
    base.w = static_cast<float>(height * up.w) + position.w;
    Maths::Vec4 start = base;
    // Sides 0 and 1: along the side row and back; 2 and 3: along the forward row and back.
    for (uint32_t side = 0; side < 4; ++side) {
        const Maths::Vec4& row = VectorAt(ObjectOf(this), side < 2 ? kMatrix : kForward);
        double reach = At<float>(this, kSideDistance);
        float x = static_cast<float>(reach * row.x), y = static_cast<float>(reach * row.y),
              z = static_cast<float>(reach * row.z);
        double w = reach * row.w; // (left unrounded)
        Maths::Vec4 end;
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
        CheckSideFeeler(&start, &end, side);
    }
    if (At<uint32_t>(this, kSideNearest) == 0x3F800000u) // (nothing hit: still 1)
        return;
    // Pushed off by the walls hit, the nearer the harder; turned along a wall it faces into.
    Maths::Vec4& push = VectorAt(this, kSideNormalSum);
    SetLength(push, static_cast<float>((1.0 - At<float>(this, kSideNearest)) * At<float>(this, kSidePush)));
    Maths::Vec4& velocity = VectorAt(ObjectOf(this), kVelocity);
    velocity.x = push.x + velocity.x;
    velocity.y = push.y + velocity.y;
    velocity.z = push.z + velocity.z;
    velocity.w = push.w + velocity.w;
    VectorAt(ObjectOf(this), kVelocity).AlignToPlane(VectorAt(this, kFloorUpVector));
    object = ObjectOf(this);
    const Maths::Vec4& forward = VectorAt(object, kForward);
    const Maths::Vec4& normal = VectorAt(this, kHitNormal);
    double into = static_cast<double>(forward.z) * normal.z + static_cast<double>(forward.y) * normal.y +
                  static_cast<double>(forward.x) * normal.x;
    if (!(into < 0.0))
        return;
    Maths::Vec4 along = forward;
    along.AlignToPlane(normal);
    float angle = static_cast<float>(Maths::GetTurnAroundAxis(VectorAt(ObjectOf(this), kForward), along,
                                                        VectorAt(ObjectOf(this), kUp)));
    double turn = static_cast<double>(Maths::Sign(angle)) * At<float>(this, kSideTurn) * At<float>(this, kFrameLength);
    float step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn))
        step = angle;
    At<Maths::Mat4>(ObjectOf(this), kMatrix).YawLocally(step);
    At<Maths::Mat4>(this, kDisplayMatrix).YawLocally(step);
}

// 0x005054A0
bool SkaterPhysicsModule::ApplyModelRotations()
{
    void* model = SkaterObject(this)->FetchModule(0x286A8D26); // (Model)
    if (!At<uint8_t>(model, 0xC8) && !At<uint8_t>(model, 0x15C) && !At<uint8_t>(model, 0x1F0))
        return false;
    void* object = ObjectOf(this);
    Maths::Vec4 axis = VectorAt(object, kUp);
    static_cast<GameObjects::CMeshInstanceModule*>(model)->GetDrawMatrix(&At<Maths::Mat4>(object, kMatrix));
    auto* yRotation = &At<GameObjects::CModelSpinAxis>(model, 0xCC);
    float angle = yRotation->Bearing();
    if (angle != 0.0f) {
        // The velocity turned with the model about the up axis.
        Maths::Vec4& velocity = VectorAt(ObjectOf(this), kVelocity);
        alignas(16) uint8_t rotation[0x10]; // (a quaternion, it seems)
        auto* quat = reinterpret_cast<Maths::QuatRotation*>(rotation);
        quat->InitFromAxisAngle(axis, angle);
        velocity = *quat->SpinBy(&axis, velocity);
    }
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    At<GameObjects::CModelSpinAxis>(model, 0x38).Clear();
    yRotation->Clear();
    At<GameObjects::CModelSpinAxis>(model, 0x160).Clear();
    return true;
}

} // namespace Skater
