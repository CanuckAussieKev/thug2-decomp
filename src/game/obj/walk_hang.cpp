// The walk component's hanging, ladder and rail-climbing code (see walk_hang.h). Floating point follows the
// original's x87 code: values it keeps on the x87 stack are doubles here, values it stores are floats.

#include "game/obj/walk_hang.h"

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Walk {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
template <typename T>
const T& At(const void* object, uint32_t offset)
{
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(object) + offset);
}

// --- the walk component's fields (offsets) ---
constexpr uint32_t kObject = 0x0C;          // the skater's object (Obj::CCompositeObject*)
constexpr uint32_t kPos = 0x24;             // the position (a vector)
constexpr uint32_t kVelocity = 0x34;        // the velocity (a vector)
constexpr uint32_t kSpeed = 0x44;           // (a float with it)
constexpr uint32_t kFacing = 0x48;          // the facing (a vector)
constexpr uint32_t kOldPos = 0x68;          // last frame's position
constexpr uint32_t kUnknown74 = 0x74;       // (a float set to 1 by a drop to hang)
constexpr uint32_t kAnim = 0x78;            // the animation to play (checksum)
constexpr uint32_t kHeightOffset = 0x9C;    // the origin's height offset
constexpr uint32_t kHangOffset = 0x71C;     // the hang's offset from the critical point (a vector)
constexpr uint32_t kTurnTarget = 0x1760;    // told when the hang turns round (0x005250A0)
constexpr uint32_t kRailStart = 0x1764;     // the rail held: its node...
constexpr uint32_t kRailEnd = 0x1768;       // ... and the next
constexpr uint32_t kRailManager = 0x176C;   // whose rail manager
constexpr uint32_t kModel = 0x1778;         // (+0x208: a node with flag 8 at +0x44 and a fraction at +0x20)
constexpr uint32_t kTrigger = 0x177C;       // the trigger component
constexpr uint32_t kContact = 0x1784;       // the movable contact component (+0x18: its ride tracker)
constexpr uint32_t kPhysics = 0x178C;       // the skater core physics component
constexpr uint32_t kPullUpFeeler = 0x17B4;  // the feeler a pull-up's ledge was found with (Nx::CFeeler)
constexpr uint32_t kPullUpHit = 0x1864;     // (whether that feeler hit something)
constexpr uint32_t kHitNormal = 0x1794;     // its hit's normal
constexpr uint32_t kRunBlend = 0x1868;      // run_speed (0) to combo_run_speed (1)
constexpr uint32_t kExitFacing = 0x1878;    // the facing kept on leaving a hang (a vector)
constexpr uint32_t kStartPos = 0x18E4;      // an animated move's start ...
constexpr uint32_t kEndPos = 0x1904;        // ... and end (vectors)
constexpr uint32_t kAlong = 0x18CC;         // how far along the rail (a fraction)
constexpr uint32_t kRailUp = 0x18D0;        // the hang point's offset from the rail: up ...
constexpr uint32_t kRailOut = 0x18D4;       // ... and out
constexpr uint32_t kStartHeightOffset = 0x1914; // the height offset at an animated move's start ...
constexpr uint32_t kEndHeightOffset = 0x1918;   // ... and end
constexpr uint32_t kAnimWaiting = 0x1944;   // an animation is playing: the callback waits
constexpr uint32_t kAnimDone = 0x1948;      // the callback (a member function pointer: the code, then 3 words)

// The walk component's own code (not rewritten): its state (0 walking, 1 in the air, 2 hanging, 3 on a ladder or
// rail, 4 an animated move), 0x0052AB90 (the velocity from `velocity` / `speed` - its +0x34 / +0x44), 0x00527720
// (turning to face a direction, at a rate).
void SetState(void* walk, int state) { GameFunction<void(__thiscall*)(void*, int)>(0x005260E0)(walk, state); }
void SetVelocity(void* walk, Mth::Vector* velocity, float* speed)
{
    GameFunction<void(__thiscall*)(void*, Mth::Vector*, float*)>(0x0052AB90)(walk, velocity, speed);
}
void TurnTo(void* walk, const Mth::Vector* facing, float rate)
{
    GameFunction<void(__thiscall*)(void*, const Mth::Vector*, float)>(0x00527720)(walk, facing, rate);
}

// The events the rail scripts and triggers get (const int[3]).
constexpr uint32_t kHangEvents = 0x006468DC;     // {0xA, 3, 0}
constexpr uint32_t kExitHangEvents = 0x006468E8; // {0xA, 4, 0}
constexpr uint32_t kPullUpEvents = 0x006468D0;   // {1, 3, 0}

constexpr uint32_t kWalkParameters = 0x6775E538;       // "walk_parameters"
constexpr uint32_t kComboRunSpeed = 0x7213EF07;        // "combo_run_speed"
constexpr uint32_t kRunSpeed = 0xCC461B87;             // "run_speed"
constexpr uint32_t kHangVertOriginOffset = 0xA8C13E74; // "hang_vert_origin_offset"
constexpr uint32_t kHangCriticalPointVertOffset = 0xDE86132E; // "hang_critical_point_vert_offset"
constexpr uint32_t kMaxCasScaling = 0x7201AD48;        // "max_cas_scaling"
constexpr uint32_t kHangCriticalPointHorizOffset = 0xC3263E6E; // "hang_critical_point_horiz_offset"
constexpr uint32_t kHangObstructionFeelerSide = 0xAFBF7687; // "hang_obstruction_feeler_side"
constexpr uint32_t kHangObstructionFeelerUp = 0x8F35F29F;   // "hang_obstruction_feeler_up"
constexpr uint32_t kSkaterExitHang = 0xBEEDB40D;       // "SkaterExitHang"
constexpr uint32_t kPushFeelerLength = 0xA20C43B7;     // "push_feeler_length"
constexpr uint32_t kFeelerHeight = 0x6DA7F696;         // "feeler_height"
constexpr uint32_t kPullUpObstructionHeight = 0x9B2818CF; // "pull_up_obstruction_height"
constexpr uint32_t kPullUpOffsetForward = 0x21DFBE77;  // "pull_up_offset_forward"
constexpr uint32_t kDropToHangSpeedFactor = 0x01D1E6AF; // "drop_to_hang_speed_factor"
constexpr uint32_t kDropToClimbMaxSnap = 0xA8276303;   // "Drop_To_Climb_Max_Snap" (a global float)
constexpr uint32_t kClimbMaxSnap = 0x30CE7F2C;         // "Climb_Max_Snap" (a global float)
constexpr uint32_t kDropToHangRotateFactor = 0xFDC8AEC0; // "drop_to_hang_rotate_factor"
constexpr uint32_t kHangMoveCollisionSideLength = 0x3344A6D0; // "hang_move_collision_side_length"
constexpr uint32_t kHangMoveCollisionUp = 0x84FF931C;   // "hang_move_collision_up"
constexpr uint32_t kHangMoveCollisionBack = 0xCBEB3D89; // "hang_move_collision_back"
constexpr uint32_t kHangMoveCollisionSideHeight = 0xC774DD6D; // "hang_move_collision_side_height"
constexpr uint32_t kRailManagerComponent = 0x1CDFB2A3; // "railmanager" (the component type)
constexpr uint32_t kAnimPullUpFromHang = 0x9EF9F8F1;   // "PullUpFromHang"
constexpr uint32_t kAnimDropToHang = 0xEB71D98E;       // "DropToHang"
constexpr uint32_t kAnimHang = 0x4194ECCA;             // "Hang"

// More fields: the facing aimed at in an animated move, the height there, the hang point's offset along the rail.
constexpr uint32_t kTargetFacing = 0x18B4;
constexpr uint32_t kTargetHeight = 0x18C8;
constexpr uint32_t kRail2C = 0x18D8;

constexpr uint32_t kTaggingControl = 0x380D909A;       // "TaggingControl"

Obj::CCompositeObject* ObjectOf(void* walk) { return At<Obj::CCompositeObject*>(walk, kObject); }
Obj::CRailManager*& RailsOf(void* walk) { return At<Obj::CRailManager*>(walk, kRailManager); }
Skater::CorePhysicsComponent* PhysicsOf(void* walk) { return At<Skater::CorePhysicsComponent*>(walk, kPhysics); }
Mth::Vector& VectorAt(void* walk, uint32_t offset) { return At<Mth::Vector>(walk, offset); }

// What FindRail finds (its 0x34-byte record): the point on the rail, the direction to face, how far along, out and
// up from the rail, and the rail's node.
struct FoundRail {
    Mth::Vector point;     // +0x00
    Mth::Vector facing;    // +0x10
    float along;           // +0x20
    float out;             // +0x24
    float up;              // +0x28
    float unknown2C;       // +0x2C
    Obj::CRailNode* node;  // +0x30
};
static_assert(sizeof(FoundRail) == sizeof(Obj::Record34), "the rail manager's record");

// The ground for a pull-up near `target` (the walk component's 0x00528280): its point into `point`, the feeler that
// found it filled in. Whether there is some.
bool FindPullUpGround(void* walk, Mth::Vector* target, Mth::Vector* point, Nx::CFeeler* feeler)
{
    return GameFunction<bool(__thiscall*)(void*, Mth::Vector*, Mth::Vector*, Nx::CFeeler*)>(0x00528280)(walk, target,
                                                                                                      point, feeler);
}
// A feeler copied (its assignment 0x0043A930).
void AssignFeeler(Nx::CFeeler* to, const Nx::CFeeler* from)
{
    GameFunction<void*(__thiscall*)(Nx::CFeeler*, const Nx::CFeeler*)>(0x0043A930)(to, from);
}
// The movable contact: no ride any more (0x00427C70).
void StopRiding(void* walk) { GameFunction<void(__thiscall*)(void*)>(0x00427C70)(At<void*>(walk, kContact)); }
// A rail manager component's object's matrix (0x0042BA10) and its rail manager (vtable +0x34).
void ObjectMatrixOf(Obj::CBaseComponent* component, Mth::Matrix* matrix)
{
    GameFunction<void(__thiscall*)(Obj::CBaseComponent*, Mth::Matrix*)>(0x0042BA10)(component, matrix);
}
Obj::CRailManager* RailManagerOf(Obj::CBaseComponent* component)
{
    using Get = Obj::CRailManager*(__thiscall*)(Obj::CBaseComponent*);
    return (*reinterpret_cast<Get**>(component))[0x34 / 4](component);
}
// The skate module's rail manager (the level's).
Obj::CRailManager* LevelRails() { return At<Obj::CRailManager*>(GameVariable<uint8_t*>(0x007CE478), 0x24); }

// FindRail for a walk component between `start` and `end`: the level's rails, else each moving rail manager's (its
// object's space: riding that object if one is found). `direction` / the margin's global float are read again for
// each. The rail manager used is left at +0x176C.
bool FindWalkRail(Component* walk, const Mth::Vector* start, const Mth::Vector* end,
                  const Mth::Vector* (*direction)(Component*, Mth::Vector*), uint32_t margin, FoundRail* found)
{
    Mth::Vector buffer;
    RailsOf(walk) = LevelRails();
    const Mth::Vector* towards = direction(walk, &buffer);
    float snap = Script::GetFloat(margin);
    auto* record = reinterpret_cast<Obj::Record34*>(found);
    if (RailsOf(walk)->FindRail(start, end, towards, walk, record, snap)) {
        StopRiding(walk);
        return true;
    }
    for (Obj::CBaseComponent* component = Obj::CompositeObjectManager()->GetFirstComponentByType(kRailManagerComponent);
         component; component = At<Obj::CBaseComponent*>(component, 0x14)) {
        Mth::Matrix toLocal;
        ObjectMatrixOf(component, &toLocal);
        toLocal.Invert();
        Mth::Vector localStart, localEnd;
        toLocal.Transform(localStart, *start);
        toLocal.Transform(localEnd, *end);
        RailsOf(walk) = RailManagerOf(component);
        towards = direction(walk, &buffer);
        snap = Script::GetFloat(margin);
        if (RailsOf(walk)->FindRail(&localStart, &localEnd, towards, walk, record, snap)) {
            reinterpret_cast<MovableContact*>(At<void*>(walk, kContact))
                ->StartRiding(At<Obj::CCompositeObject*>(component, 0x0C));
            return true;
        }
    }
    return false;
}
const Mth::Vector* FacingAway(Component* walk, Mth::Vector* buffer)
{
    const Mth::Vector& facing = At<Mth::Vector>(walk, 0x48);
    *buffer = {-facing.x, -facing.y, -facing.z, -facing.w};
    return buffer;
}
const Mth::Vector* Facing(Component* walk, Mth::Vector*) { return &At<Mth::Vector>(walk, 0x48); }

// The animation-end callback (+0x1948): a member function pointer as the compiler lays it out - the code (the game's
// address: in the game, a jump to ours) and three words of adjustment.
void SetAnimDone(void* walk, uint32_t code)
{
    uint32_t* callback = &At<uint32_t>(walk, kAnimDone);
    callback[0] = code;
    callback[1] = 0;
    callback[2] = 0;
    callback[3] = 0;
}

// The skater core's rail node and rail manager (its +0x22C / +0x230), for its rail scripts.
void TellPhysicsRail(void* walk)
{
    if (Obj::CRailNode* node = At<Obj::CRailNode*>(walk, kRailStart))
        At<Obj::CRailNode*>(PhysicsOf(walk), 0x22C) = node;
    if (Obj::CRailManager* rails = RailsOf(walk))
        At<Obj::CRailManager*>(PhysicsOf(walk), 0x230) = rails;
}
void TriggerRailScript(void* walk, uint32_t events)
{
    PhysicsOf(walk)->TriggerRailScript(reinterpret_cast<const int*>(events));
}
// The hang offset (+0x71C) for the facing: hang_critical_point_horiz_offset along it, the critical point's height up.
void SetHangOffset(Component* walk)
{
    float horiz = Component::Param(kHangCriticalPointHorizOffset);
    const Mth::Vector& facing = VectorAt(walk, kFacing);
    Mth::Vector& offset = VectorAt(walk, kHangOffset);
    offset.z = horiz * facing.z;
    offset.w = horiz * facing.w;
    offset.x = horiz * facing.x;
    offset.y = horiz * facing.y;
    offset.y = static_cast<float>(walk->CriticalPointUp());
}

// The trigger component told a feeler hit something (for `events`), and the movable contact the feeler's hit.
void TriggerFeeler(void* walk, uint32_t events, Nx::CFeeler* feeler)
{
    GameFunction<void(__thiscall*)(void*, const int*, Nx::CFeeler*)>(0x00434BF0)(
        At<void*>(walk, kTrigger), reinterpret_cast<const int*>(events), feeler);
}
void ContactFeeler(void* walk, Nx::CFeeler* feeler)
{
    GameFunction<void(__thiscall*)(void*, Nx::CFeeler*)>(0x00427CD0)(At<void*>(walk, kContact), feeler);
}

// The game's clock with its offset.
int Now() { return GameFunction<int(__cdecl*)()>(0x0044ECE0)() + GameVariable<int>(0x00696D60); }

// The x87's cosine (the original's FCOS), rounded to a double.
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// 0x006468F8: 60 degrees (a double)
constexpr double kSixtyDegrees = 1.0471975511965976;

} // namespace

// --- small helpers ---

// 0x0043D990
Mth::Vector* __cdecl Lerp(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b, float t)
{
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    float dz = static_cast<float>(static_cast<double>(b->z) - a->z);
    float dw = static_cast<float>(static_cast<double>(b->w) - a->w);
    float sx = static_cast<float>(dx * t);
    float sz = static_cast<float>(static_cast<double>(dz) * t);
    float sw = static_cast<float>(static_cast<double>(dw) * t);
    out->w = static_cast<float>(static_cast<double>(sw) + a->w);
    out->x = static_cast<float>(static_cast<double>(sx) + a->x);
    out->y = static_cast<float>(dy * t + a->y);
    out->z = static_cast<float>(static_cast<double>(sz) + a->z);
    return out;
}

// 0x0043DA20
int SkaterTurn::TurnedRound() const
{
    const void* skater = this;
    if (At<int>(skater, 0x234) == 0 && At<float>(skater, 0x22C) != 0.0f &&
        3.1415927410125732 - std::fabs(static_cast<double>(At<float>(skater, 0x228))) < 0.78539818525314331)
        return 1;
    return 0;
}

// 0x0043DA70
float __cdecl Component::Param(uint32_t name)
{
    float value;
    Script::GetStructure(kWalkParameters)->GetFloat(name, &value, 1);
    return value;
}

// 0x0043DAA0
double Component::RunSpeed()
{
    float combo = Param(kComboRunSpeed);
    float run = Param(kRunSpeed);
    return (static_cast<double>(combo) - run) * At<float>(this, kRunBlend) + run;
}

// 0x0043DC00
double Component::HangHeightOffset()
{
    const uint8_t* node = At<uint8_t*>(At<void*>(this, kModel), 0x208);
    if (!(node[0x44] & 8))
        return 0.0;
    float fraction = At<float>(node, 0x20);
    float offset = Param(kHangVertOriginOffset);
    return (1.0 - fraction) * offset;
}

// 0x0043DC60
double Component::CriticalPointUp()
{
    float vert = Param(kHangCriticalPointVertOffset);
    float scaling = Param(kMaxCasScaling);
    float origin = Param(kHangVertOriginOffset);
    return vert - static_cast<double>(origin) * scaling;
}

// 0x0043DB00
void Component::EndPullUp()
{
    SetState(this, 0);
    ObjectOf(this)->SelfEvent(kSkaterExitHang, nullptr);
    TriggerRailScript(this, kExitHangEvents);
    if (At<bool>(this, kPullUpHit)) {
        auto* feeler = &At<Nx::CFeeler>(this, kPullUpFeeler);
        TriggerFeeler(this, kPullUpEvents, feeler);
        ContactFeeler(this, feeler);
    }
    VectorAt(this, kHangOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kHeightOffset) = At<float>(this, kEndHeightOffset);
}

// 0x0043DB90
void Component::EndDropFromHang()
{
    SetState(this, 1);
    ObjectOf(this)->SelfEvent(kSkaterExitHang, nullptr);
    TriggerRailScript(this, kExitHangEvents);
    VectorAt(this, kExitFacing) = VectorAt(this, kFacing);
    SetVelocity(this, &VectorAt(this, kVelocity), &At<float>(this, kSpeed));
    At<float>(this, kHeightOffset) = At<float>(this, kEndHeightOffset);
}

// 0x0043DCF0
void Component::EndDropToHang()
{
    SetHangOffset(this);
    SetState(this, 2);
    TellPhysicsRail(this);
    TriggerRailScript(this, kHangEvents);
    At<bool>(ObjectOf(this)->GetComponent(kTaggingControl), 0x1D) = false;
}

// 0x0043DDD0
bool Component::SideClear(Nx::CFeeler* feeler, const Mth::Vector* hang, const Mth::Vector* side)
{
    float sideLength = Param(kHangObstructionFeelerSide);
    float sx = sideLength * side->x;
    float sy = sideLength * side->y;
    float sz = sideLength * side->z;
    float sw = sideLength * side->w;
    float horiz = Param(kHangCriticalPointHorizOffset);
    const Mth::Vector& point = hang[0];
    const Mth::Vector& out = hang[1];
    // the critical point (the y left unrounded, as the original keeps it on the x87 stack), moved along the side
    float cx = static_cast<float>(static_cast<double>(horiz) * out.x + point.x);
    double cy = static_cast<double>(horiz) * out.y + point.y;
    float hz = horiz * out.z;
    float hw = horiz * out.w;
    float cz = static_cast<float>(static_cast<double>(hz) + point.z);
    float cw = static_cast<float>(static_cast<double>(hw) + point.w);
    Mth::Line& line = feeler->m_line;
    line.start.w = cw + sw;
    line.start.x = cx + sx;
    line.start.y = static_cast<float>(cy + sy);
    line.start.z = static_cast<float>(static_cast<double>(cz) + sz);
    line.end = line.start;
    float origin = Param(kHangVertOriginOffset);
    line.start.y = static_cast<float>(static_cast<double>(line.start.y) - origin + CriticalPointUp());
    float up = Param(kHangObstructionFeelerUp);
    line.end.y = up + line.end.y;
    if (feeler->GetCollision(true, false))
        return false;
    Mth::Vector start = line.start;
    line.start = line.end;
    line.end = start;
    return !feeler->GetCollision(true, false);
}

// 0x0043DFC0
void Component::FaceHangRail(float distance, bool fromEnd, const Mth::Vector* direction)
{
    Obj::CRailManager* rails = RailsOf(this);
    Mth::Vector a, b;
    rails->GetWorldPos(&a, At<Obj::CRailNode*>(this, kRailEnd));
    rails->GetWorldPos(&b, At<Obj::CRailNode*>(this, kRailStart));
    Mth::Vector d = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    float xx = d.x * d.x;
    double length = std::sqrt(static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y + xx);
    if (distance > length)
        distance = static_cast<float>(length);
    double fraction = distance / length;
    At<float>(this, kAlong) = static_cast<float>(fraction);
    if (fromEnd)
        At<float>(this, kAlong) = static_cast<float>(1.0 - fraction);
    // across the rail, flat
    Mth::Vector across = {-d.z, 0.0f, d.x, 0.0f};
    double acrossLength = std::sqrt(static_cast<double>(across.x) * across.x + xx);
    if (acrossLength > 0.0) {
        double scale = 1.0 / acrossLength;
        across.x = static_cast<float>(across.x * scale);
        across.y = static_cast<float>(0.0 * scale);
        across.z = static_cast<float>(scale * d.x);
    }
    Mth::Vector reversed = {-d.x, -d.y, -d.z, -d.w};
    const Mth::Vector& along = fromEnd ? reversed : d;
    const Mth::Vector& facing = VectorAt(this, kFacing);
    double turn = static_cast<double>(direction->z) * facing.x - static_cast<double>(facing.z) * direction->x;
    double side = static_cast<double>(across.x) * along.z - static_cast<double>(across.z) * along.x;
    if (side * turn < 0.0) {
        across.x = -across.x;
        across.y = -across.y;
        across.z = -across.z;
    }
    double dot = static_cast<double>(across.z) * facing.z + static_cast<double>(across.y) * facing.y +
                 static_cast<double>(across.x) * facing.x;
    if (X87Cos(kSixtyDegrees) > dot)
        GameFunction<void(__thiscall*)(void*)>(0x005250A0)(At<void*>(this, kTurnTarget));
    VectorAt(this, kFacing) = {across.x, across.y, across.z, 1.0f};
    SetHangOffset(this);
}

// 0x0043E230
bool Component::NextHangRail(float* t, Obj::CRailNode* start, Obj::CRailNode* end, Obj::CRailNode** newStart,
                             Obj::CRailNode** newEnd)
{
    Obj::CRailManager* rails = RailsOf(this);
    auto grabbable = [rails](Obj::CRailNode* from, Obj::CRailNode* to) {
        Mth::Vector a, b;
        rails->GetWorldPos(&b, to);
        rails->GetWorldPos(&a, from);
        return Obj::RailClear(a, b);
    };
    // a segment's flags are its first node's
    auto usable = [](const Obj::CRailNode* first) {
        return (first->m_flags & Obj::RAIL_FLAG_ACTIVE) && !(first->m_flags & Obj::RAIL_FLAG_NO_CLIMBING);
    };
    Obj::CRailNode* joining = nullptr;
    if (*t < 0.0f) {
        // back past the start: the segment before it
        Obj::CRailNode* previous = start->m_prev;
        if (previous && usable(previous) && grabbable(previous, start)) {
            *newEnd = start;
            *newStart = start->m_prev;
            return true;
        }
        if (!rails->FindJoiningRail(start, 0x28, &joining))
            return false;
    } else {
        Obj::CRailNode* next = end->m_next;
        if (next && usable(end) && grabbable(end, next)) {
            *newStart = end;
            *newEnd = end->m_next;
            return true;
        }
        if (!rails->FindJoiningRail(end, 0x28, &joining))
            return false;
    }
    // a joining rail: does it run on the same way?
    bool sameWay = Obj::NodesTogether(joining, end) || Obj::NodesTogether(joining->m_next, start);
    Mth::Vector a, b;
    rails->GetWorldPos(&a, start);
    rails->GetWorldPos(&b, end);
    Mth::Vector here = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    here.Normalize();
    Obj::CRailNode* joinedNext = joining->m_next;
    rails->GetWorldPos(&a, joining);
    rails->GetWorldPos(&b, joinedNext);
    Mth::Vector there = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    there.Normalize();
    double tx = there.x, ty = there.y, tz = there.z;
    if (!sameWay) {
        tx *= -1.0f;
        ty *= -1.0f;
        tz *= -1.0f;
    }
    double dot = here.z * tz + ty * here.y + tx * here.x;
    if (dot < -0.86599999666213989) {
        Dbg::Printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", 0x4F2, "BAD ANGLE ON COINCIDENT RAIL");
        return false;
    }
    if (!sameWay)
        *t = 1.0f - *t;
    *newStart = joining;
    *newEnd = joining->m_next;
    Dbg::Printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", 0x4FC, "FOUND COINCIDENT RAIL");
    return true;
}

// 0x0043E530
bool Component::PullUp(Mth::Vector* target)
{
    // the ground to stand on: near the target, else 12 to one side of it, else 12 to the other
    Nx::CFeeler ledge;
    ledge.Construct();
    Mth::Vector ground;
    bool found = FindPullUpGround(this, target, &ground, &ledge);
    if (!found) {
        float across = -VectorAt(this, kFacing).z;
        Mth::Vector first = ground;
        float facingX = VectorAt(this, kFacing).x;
        target->x = static_cast<float>(static_cast<double>(across) * 12.0f + target->x);
        target->z = static_cast<float>(static_cast<double>(facingX) * 12.0f + target->z);
        target->w = target->w + 12.0f;
        found = FindPullUpGround(this, target, &ground, &ledge);
        if (!found) {
            target->x = static_cast<float>(target->x - static_cast<double>(across) * 24.0f);
            target->z = static_cast<float>(target->z - static_cast<double>(facingX) * 24.0f);
            target->w = target->w - 24.0f;
            found = FindPullUpGround(this, target, &ground, &ledge);
            if (!found)
                ground = first;
        }
    }
    // seven feelers round the ground point, half the push feeler's length out (an x87 sine and cosine each)
    Nx::CFeeler probe;
    probe.Construct();
    float push = Param(kPushFeelerLength);
    float halfPush = static_cast<float>(static_cast<double>(push) * 0.5f);
    float height = Param(kFeelerHeight);
    float noRise = static_cast<float>(static_cast<double>(halfPush) * 0.0f);
    const Mth::Vector& facing = VectorAt(this, kFacing);
    for (int i = 0; i < 7; ++i) {
        double angle = static_cast<double>(i) * 0.89759790897369385;
        double c = X87Cos(angle);
        double s = X87Sin(angle);
        probe.m_line.start = {ground.x, static_cast<float>(static_cast<double>(ground.y) + height), ground.z, ground.w};
        float dx = static_cast<float>(c * facing.x - s * facing.z);
        double dz = s * facing.x + c * facing.z;
        float outX = static_cast<float>(static_cast<double>(dx) * halfPush);
        float outZ = static_cast<float>(dz * halfPush);
        float rise = noRise + ground.y;
        probe.m_line.end = {outX + ground.x, static_cast<float>(static_cast<double>(rise) + height), outZ + ground.z,
                            ground.w + halfPush};
        if (probe.GetCollision(true, false))
            return false;
    }
    // and from behind the skater, up past the obstruction height, to just above the ground point
    const Mth::Vector& pos = VectorAt(this, kPos);
    probe.m_line.start = {static_cast<float>(static_cast<double>(facing.x) * -6.0f + pos.x),
                          static_cast<float>(pos.y + static_cast<double>(facing.y) * -6.0f), pos.z + facing.z * -6.0f,
                          pos.w + facing.w * -6.0f};
    float top = Param(kHangVertOriginOffset);
    float obstruction = Param(kPullUpObstructionHeight);
    probe.m_line.start.y = static_cast<float>(static_cast<double>(probe.m_line.start.y) + obstruction + top);
    probe.m_line.end = {ground.x, ground.y + 1.0f, ground.z, ground.w};
    if (probe.GetCollision(true, false))
        return false;

    // the animated move: walking at the end (the ledge's feeler kept), or in the air with no ground
    if (found) {
        SetAnimDone(this, 0x0043DB00);
        AssignFeeler(&At<Nx::CFeeler>(this, kPullUpFeeler), &ledge);
        At<bool>(this, kPullUpHit) = true;
        VectorAt(this, kHitNormal) = ledge.m_normal;
    } else {
        SetAnimDone(this, 0x0043DB90);
    }
    VectorAt(this, kStartPos) = pos;
    VectorAt(this, kEndPos) = ground;
    TurnTo(this, &VectorAt(this, kFacing), 1.0f);
    At<int>(this, kAnimWaiting) = 1;
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = 0.0f;
    double back = -(static_cast<double>(Param(kPullUpOffsetForward)) + 6.0f);
    float backF = static_cast<float>(back);
    Mth::Vector& offset = VectorAt(this, kHangOffset);
    offset = {backF * facing.x, backF * facing.y, backF * facing.z, static_cast<float>(back * facing.w)};
    offset.y = 1.0f;
    VectorAt(this, kTargetFacing) = facing;
    At<float>(this, kTargetHeight) = pos.y - At<float>(this, kRailUp);
    SetState(this, 4);
    At<uint32_t>(this, kAnim) = kAnimPullUpFromHang;
    return true;
}

// 0x0043EC40
bool Component::DropToHang()
{
    if (At<bool>(At<uint8_t*>(PhysicsOf(this), 0x1528), 0x70))
        return false;
    float factor = Param(kDropToHangSpeedFactor);
    double speed = RunSpeed() * factor;
    const Mth::Vector& velocity = VectorAt(this, kVelocity);
    double moving = static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y +
                    static_cast<double>(velocity.z) * velocity.z;
    if (speed * speed < moving)
        return false;
    Obj::CBaseComponent* tagging = ObjectOf(this)->GetComponent(kTaggingControl);
    if (At<bool>(tagging, 0x1C))
        return false;
    At<float>(this, kUnknown74) = 1.0f;
    VectorAt(this, kPos).w = 1.0f;
    FoundRail rail;
    if (!FindWalkRail(this, &VectorAt(this, kOldPos), &VectorAt(this, kPos), FacingAway, kDropToClimbMaxSnap, &rail))
        return false;
    At<Obj::CRailNode*>(this, kRailStart) = rail.node;
    At<Obj::CRailNode*>(this, kRailEnd) = rail.node->m_next;
    At<float>(this, kSpeed) = 0.0f;
    VectorAt(this, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kAlong) = rail.along;
    At<float>(this, kRailOut) = rail.out;
    At<float>(this, kRailUp) = rail.up;
    At<bool>(tagging, 0x1D) = true;
    VectorAt(this, kStartPos) = VectorAt(this, kPos);
    VectorAt(this, kEndPos) = rail.point;
    SetAnimDone(this, 0x0043DCF0);
    TurnTo(this, &rail.facing, Param(kDropToHangRotateFactor));
    At<int>(this, kAnimWaiting) = 1;
    float horiz = Param(kHangCriticalPointHorizOffset);
    VectorAt(this, kHangOffset) = {rail.facing.x * horiz, rail.facing.y * horiz, rail.facing.z * horiz,
                                   rail.facing.w * horiz};
    VectorAt(this, kTargetFacing) = rail.facing;
    At<float>(this, kTargetHeight) = VectorAt(this, kPos).y;
    At<float>(this, kStartHeightOffset) = At<float>(this, kHeightOffset);
    At<float>(this, kEndHeightOffset) = static_cast<float>(HangHeightOffset());
    SetState(this, 4);
    At<uint32_t>(this, kAnim) = kAnimDropToHang;
    return true;
}

// 0x0043F030
bool Component::GrabRail(Mth::Vector start, Mth::Vector end)
{
    if (At<float>(this, kSpeed) > 50.0f)
        return false;
    start.y = start.y + Param(kHangVertOriginOffset);
    end.y = end.y + Param(kHangVertOriginOffset);
    start.w = 1.0f;
    end.w = 1.0f;
    FoundRail rail;
    if (!FindWalkRail(this, &start, &end, Facing, kClimbMaxSnap, &rail))
        return false;
    At<Obj::CRailNode*>(this, kRailStart) = rail.node;
    At<Obj::CRailNode*>(this, kRailEnd) = rail.node->m_next;
    VectorAt(this, kPos) = rail.point;
    VectorAt(this, kFacing) = rail.facing;
    At<float>(this, kAlong) = rail.along;
    At<float>(this, kRailUp) = rail.up;
    At<float>(this, kRailOut) = rail.out;
    At<float>(this, kRail2C) = rail.unknown2C;
    SetHangOffset(this);
    VectorAt(this, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    At<float>(this, kSpeed) = 0.0f;
    At<float>(this, kHeightOffset) = static_cast<float>(HangHeightOffset());
    SetState(this, 2);
    TellPhysicsRail(this);
    TriggerRailScript(this, kHangEvents);
    At<uint32_t>(this, kAnim) = kAnimHang;
    uint8_t* flags = At<uint8_t*>(PhysicsOf(this), 0x1528);
    if (At<uint8_t>(flags, 0xA0) != 1) {
        At<uint8_t>(flags, 0xA0) = 1;
        At<int>(flags, 0xA4) = Now();
    }
    return true;
}

// 0x0043F3A0
bool Component::ShimmyBlocked(const Mth::Vector* out, float along, const Mth::Vector* point, bool left,
                              const Mth::Vector* move, Mth::Vector segment, Obj::CRailNode* start, Obj::CRailNode* end)
{
    double dot = static_cast<double>(segment.y) * move->y + static_cast<double>(segment.x) * move->x +
                 static_cast<double>(segment.z) * move->z;
    bool backwards = dot < 0.0 ? left : !left;
    Mth::Vector sideways = left ? *move : Mth::Vector{-move->x, -move->y, -move->z, -move->w};
    Mth::Vector outwards = *out;
    double t = backwards ? 1.0 - along : along;
    double segmentLength = std::sqrt(static_cast<double>(segment.z) * segment.z +
                                     static_cast<double>(segment.y) * segment.y +
                                     static_cast<double>(segment.x) * segment.x);
    float distance = static_cast<float>(segmentLength * t);
    float sideLength = Param(kHangMoveCollisionSideLength);
    if (static_cast<double>(sideLength) + 1.0f > distance) {
        // near the segment's end: look along the next segment instead, if it doesn't turn too far
        along = (backwards ? 2.0f : -2.0f) + along;
        Obj::CRailNode* nextStart;
        Obj::CRailNode* nextEnd;
        if (NextHangRail(&along, start, end, &nextStart, &nextEnd)) {
            float sign = along > 0.5f ? -1.0f : 1.0f;
            Mth::Vector a, b;
            RailsOf(this)->GetWorldPos(&a, nextEnd);
            RailsOf(this)->GetWorldPos(&b, nextStart);
            double dw = static_cast<double>(b.w) - a.w;
            float dz = b.z - a.z;
            float dx = b.x - a.x;
            Mth::Vector next = {dx * sign, 0.0f, dz * sign, static_cast<float>(dw * sign)};
            next.Normalize();
            double turn = static_cast<double>(next.y) * out->y + static_cast<double>(next.z) * out->z +
                          static_cast<double>(next.x) * out->x;
            if (turn < 0.0 && turn > -0.87000000476837158) {
                sideways = next;
                if (left) {
                    outwards.x = next.z;
                    outwards.z = -next.x;
                } else {
                    outwards.z = next.x;
                    outwards.x = -next.z;
                }
            }
        }
    }
    // the feeler: from the hang point, raised and pulled back out of the wall
    Nx::CFeeler feeler;
    feeler.Construct();
    Mth::Line& line = feeler.m_line;
    line.start = *point;
    line.start.y = line.start.y + Param(kHangMoveCollisionUp);
    float back = Param(kHangMoveCollisionBack);
    line.start.x = static_cast<float>(line.start.x - static_cast<double>(back) * out->x);
    line.start.y = static_cast<float>(line.start.y - static_cast<double>(back) * out->y);
    line.start.z = static_cast<float>(line.start.z - static_cast<double>(back) * out->z);
    line.start.w = static_cast<float>(line.start.w - static_cast<double>(back) * out->w);
    // `v` times `scale` added to the end (each product unrounded, as the x87 code adds it)
    auto addScaled = [&line](float scale, const Mth::Vector& v) {
        line.end.x = static_cast<float>(static_cast<double>(scale) * v.x + line.end.x);
        line.end.y = static_cast<float>(static_cast<double>(scale) * v.y + line.end.y);
        line.end.z = static_cast<float>(static_cast<double>(scale) * v.z + line.end.z);
        line.end.w = static_cast<float>(static_cast<double>(scale) * v.w + line.end.w);
    };
    // sideways at the hang point's height, at the side height and at half of it
    line.end = line.start;
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    if (feeler.GetCollision(true, false))
        return true;
    line.end = line.start;
    line.end.y = line.end.y + Param(kHangMoveCollisionSideHeight);
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    if (feeler.GetCollision(true, false))
        return true;
    line.end = line.start;
    line.end.y = static_cast<float>(static_cast<double>(Param(kHangMoveCollisionSideHeight)) * 0.5f + line.end.y);
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    if (feeler.GetCollision(true, false))
        return true;
    // down at the critical point, out from the wall, sideways and one more along the segment
    line.end = line.start;
    float up = Param(kHangMoveCollisionUp);
    line.end.y = static_cast<float>(CriticalPointUp() - up - 1.0f + line.end.y);
    addScaled(Param(kHangCriticalPointHorizOffset), outwards);
    addScaled(Param(kHangMoveCollisionSideLength), sideways);
    segment.Normalize();
    const Mth::Vector onward = backwards ? Mth::Vector{-segment.x, -segment.y, -segment.z, -segment.w} : segment;
    line.end.x = line.end.x + onward.x;
    line.end.y = line.end.y + onward.y;
    line.end.z = line.end.z + onward.z;
    line.end.w = line.end.w + onward.w;
    if (feeler.GetCollision(true, false))
        return true;
    // half the side height down, out and sideways (each step rounded: the original's vector operators)
    line.end = line.start;
    line.end.y = static_cast<float>(line.end.y - static_cast<double>(Param(kHangMoveCollisionSideHeight)) * 0.5f);
    line.end += Param(kHangCriticalPointHorizOffset) * outwards;
    line.end += Param(kHangMoveCollisionSideLength) * sideways;
    return feeler.GetCollision(true, false) != 0;
}

// 0x0043EB80
void MovableContact::StartRiding(Obj::CCompositeObject* object)
{
    if (auto* tracker = At<Obj::CRideTracker*>(this, 0x18)) {
        At<Obj::CCompositeObject*>(this, 0x6C) = tracker->m_object.Get(); // (the object last ridden)
        tracker->m_object.Release();
        Mem::Free(tracker);
        At<Obj::CRideTracker*>(this, 0x18) = nullptr;
        At<int>(this, 0x70) = Now();
    }
    void* memory = Mem::New(sizeof(Obj::CRideTracker));
    At<Obj::CRideTracker*>(this, 0x18) = memory ? static_cast<Obj::CRideTracker*>(memory)->Construct(object) : nullptr;
}

} // namespace Walk

namespace Obj {

// 0x0043D930
CBaseComponent* __cdecl CreateWaitAndDieComponent()
{
    void* memory = Mem::Calloc(0x28);
    if (!memory)
        return nullptr;
    auto* component = static_cast<CBaseComponent*>(memory);
    component->Construct();
    *reinterpret_cast<uint32_t*>(component) = 0x00646898; // its vtable
    reinterpret_cast<uint32_t*>(component)[1] = 0x03AC8065; // its type
    return component;
}

} // namespace Obj

REPLACE(0x0043D930, Obj::CreateWaitAndDieComponent);
REPLACE(0x0043D990, Walk::Lerp);
REPLACE(0x0043DA20, &Walk::SkaterTurn::TurnedRound);
REPLACE(0x0043DA70, Walk::Component::Param);
REPLACE(0x0043DAA0, &Walk::Component::RunSpeed);
REPLACE(0x0043DB00, &Walk::Component::EndPullUp);
REPLACE(0x0043DB90, &Walk::Component::EndDropFromHang);
REPLACE(0x0043DC00, &Walk::Component::HangHeightOffset);
REPLACE(0x0043DC60, &Walk::Component::CriticalPointUp);
REPLACE(0x0043DCF0, &Walk::Component::EndDropToHang);
REPLACE(0x0043DDD0, &Walk::Component::SideClear);
REPLACE(0x0043DFC0, &Walk::Component::FaceHangRail);
REPLACE(0x0043E230, &Walk::Component::NextHangRail);
REPLACE(0x0043EB80, &Walk::MovableContact::StartRiding);
REPLACE(0x0043E530, &Walk::Component::PullUp);
REPLACE(0x0043EC40, &Walk::Component::DropToHang);
REPLACE(0x0043F030, &Walk::Component::GrabRail);
REPLACE(0x0043F3A0, &Walk::Component::ShimmyBlocked);
