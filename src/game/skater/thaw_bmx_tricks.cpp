// NOT ORIGINAL - THAW's BMX ridden in THUG2, phase 4 (game/skater/thaw_bmx.h):
// with THUG2's vehicle mode gone ("on the bike" is the bike flag alone), THAW's own gates of what the bike can do -
// wallrides on square too, skitching on R2, no vert wallplants, no lip tricks, the bike's wallplant -, the tire tap,
// the manual's pose on the rider and the bike, the bike's sounds and the THAW-only members its bike scripts ask. Our
// addition at the user's request - not THUG2's code and not PartyMod's. Read from THAW's reference build
// (game/skater/thaw_bmx.h - no tracing); our THUG2 functions call these only while ThawBmx::Active(core).

#include <cmath>
#include <cstdint>

#include "game/core/crc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/terrain.h"
#include "game/skate/score.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/core_physics.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
#include "game/skater/special_item.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Skater::ThawBmx {

namespace {

template <typename T>
T& At(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
Mth::Vector& VectorAt(const void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }

// The core physics component's fields (THUG2's; THAW's in the comment).
constexpr uint32_t kObject = 0x0C;
constexpr uint32_t kDisplayMatrix = 0x18;  // (rows: +0x18 right, +0x28 up, +0x38 forward)
constexpr uint32_t kDisplayUp = 0x28;

constexpr uint32_t kFrameLength = 0x8C;    // THAW +0x9C
constexpr uint32_t kHitFlags = 0xF8;
constexpr uint32_t kHitPoint = 0x120;
constexpr uint32_t kFeelerStart = 0x208;
constexpr uint32_t kFeelerEnd = 0x218;
constexpr uint32_t kInput = 0x150C;        // THAW +0x1910 (the pad at its +0x20)
constexpr uint32_t kBalanceTricks = 0x1524; // THAW +0x1928 (the balance trick component: +0x18 its kind)
constexpr uint32_t kFlags = 0x1528;        // THAW +0x192C
constexpr uint32_t kGroundNormal = 0x68;   // THAW +0x68 (THAW's 0x005D0BD0 sets it - the same place as THUG2's)
// The timed flags (THUG2's; THAW's in the comment).
constexpr uint32_t kFlagVert = 0x30;       // THAW 0x30
constexpr uint32_t kFlag38 = 0x38;         // THAW 0x38
constexpr uint32_t kFlagVertGround = 0x40; // THAW 0x40 (THAW clears 0x48 too: ours 0x48)
constexpr uint32_t kFlag48 = 0x48;
constexpr uint32_t kFlag70 = 0x70;         // THAW 0x78
constexpr uint32_t kFlagAcidDrop = 0x78;   // THAW 0x80
// The skater object's.
constexpr uint32_t kPosition = 0x4C;       // THAW +0x68
constexpr uint32_t kMatrix = 0x6C;         // THAW +0x88
constexpr uint32_t kUp = 0x7C;             // THAW +0x98
constexpr uint32_t kFacing = 0x8C;         // THAW +0xA8
constexpr uint32_t kVelocity = 0xAC;       // THAW +0xC8
// The pad's buttons (offsets in THUG2's pad = THAW's).
constexpr uint32_t kR1 = 0xE0, kR2 = 0x100, kSquare = 0x160;
// THAW's stall state (CoreFields::kBikeStall).
constexpr int kStateBikeStall = 6;
// Components.
constexpr uint32_t kSpecialItem = 0xF27A294D;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kModel = 0x286A8D26;       // model
constexpr uint32_t kModelOfComponent = 0x208; // the model component's renderer model
constexpr uint32_t kModelFlags = 0x44;        // the renderer model's flags (0x2 shown, 0x4 hidden)

// THAW's constants.
constexpr float kDegreesToRadians = 0.0174532924f;  // 0x006DB474
constexpr float kTapAbove = -30.0f;                 // 0x006EDDA0 (in vert: nothing this near below - the tap's room)
constexpr float kTapSearchStart = 100.0f;           // 0x006DCBC0 (in vert: the depths looked at, from this ...)
constexpr float kTapSearchStep = 3.0f;              // 0x006DC7F0 (... down by this ...)
constexpr float kTapSearchEnd = 0.01f;              // 0x006DB448 (... while above this)
constexpr float kTapDrop = 1000.0f;                 // 0x006DB3D0 (each feeler's length down)
constexpr float kTapAhead = 100.0f;                 // 0x006DCBC0 (out of vert the ledge looked for this far ahead)
constexpr float kTapMargin = 90.0f;                 // 0x42B40000 (the ledge search's margin)
constexpr float kTapPitch = 0.785398185f;           // 0x3F490FDB (45 degrees: up held the nose down, down the tail)
constexpr float kTapPitchTime = 0.5f;               // 0x006DB3CC (the bike turned over to it in this long)
constexpr float kTapReach = 5.0f;                   // 0x006DD10C (the wheel moved this far a frame towards the point)
constexpr float kTapSlowing = 50.0f;                // 0x006DD6C8 (the horizontal speed's loss a frame)
constexpr float kTapPull = 3.0f;                    // 0x006DC7F0 (let go: pulled towards where the point lies)
constexpr float kTapRiseFast = 100.0f;              // 0x006DCBC0
constexpr float kTapRiseFraction = 0.1f;            // 0x006DBD8C
constexpr float kTapRiseSlow = 15.0f;               // 0x006DD430
constexpr double kManualWobble = 0.01;              // 0x006ED758 (the manual angle to the wobble's fraction)
constexpr float kBalanceMiddle = 4096.0f;           // THAW 0x006E1BC4 (a balance's lean to the wobble's fraction:
constexpr float kBalanceScale = 0.0001220703125f;   // THAW 0x006EF068  (4096 +- lean) / 8192)
constexpr float kLedgeHeight = 12.0f;               // (THUG2's ledge search: a rail at most this far below in the air)
constexpr double kTapLedgeWeight = 0.001;           // THAW 0x006DBD98 (a ledge's distance against a surface's, in vert)
constexpr double kTapNearer = 0.001;                // THAW 0x006E1B90 (a double: "no further than the best so far")
constexpr float kTapFar = 10000.0f;                 // THAW 0x006E0834 (the nearest surface so far, to begin with)
constexpr uint32_t kHangMaxRailAscent = 0x98724EBC; // hang_max_rail_ascent (walk_parameters)
constexpr uint32_t kWalkParameters = 0x6775E538;    // walk_parameters

// The scripts' names (tools/pipeline/thaw_bmx/bike_scripts.ts, ride.ts).
constexpr const char* kWallplantTrick = "bike_Wallplant_Trick";
constexpr uint32_t kThugWallplantTrick = 0x5D1F84A7; // Wallplant_Trick
constexpr const char* kBikeObject = "ThawBmx";
const char* const kSoundTables[] = {
    "thaw_bmx_terrain_land", "thaw_bmx_terrain_grindland", "thaw_bmx_terrain_ollie", "thaw_bmx_terrain_grindjump",
    "thaw_bmx_terrain_bonk", "thaw_bmx_terrain_roll",      "thaw_bmx_terrain_grind",
};

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }
void* ObjectOf(const void* core) { return At<void*>(core, kObject); }
uint8_t* FlagsOf(const void* core) { return At<uint8_t*>(core, kFlags); }
uint8_t* PadOf(const void* core) { return At<uint8_t*>(core, kInput) + 0x20; }
float FrameLength(const void* core) { return At<float>(core, kFrameLength); }
int Now() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

void SetTimedFlag(const void* core, uint32_t flag, bool on)
{
    uint8_t* f = FlagsOf(core) + flag;
    if ((*f != 0) == on)
        return;
    *f = on ? 1 : 0;
    At<int>(f, 4) = Now();
}

double Dot3(const Mth::Vector& a, const Mth::Vector& b)
{
    return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}

// The level's rails (the skate module's rail manager: 0x007CE478's +0x24 - THAW's 0x008A3B5C's +0x24).
Obj::CRailManager* LevelRails()
{
    uint8_t* module = GameVariable<uint8_t*>(0x007CE478);
    return module ? At<Obj::CRailManager*>(module, 0x24) : nullptr;
}

// The tire tap's ledge: THAW 0x0058C850 on the level's rails (the rails its 0x0058C856.. walks, flags read as THAW's:
// a ladder never, switched on, with a next node; a rail that can't be climbed on is fine on the bike). Phase 7, read
// whole: it is THUG2's CRailManager::FindRail (0x0057DDA0) with that one bike exception,
// and its checker is the walk component's hang check (THAW 0x0060AC40 - THUG2's CWalkComponent::CheckRail): in the
// air a rail not above the line and within 12 below it, not steeper than hang_max_rail_ascent, room to hang. Ours
// takes the first two (the hang's room is for a hanging skater, not a wheel) and the rail's own point - THAW's is the
// hang point CheckRail makes, which its ledge state (7, not ported) then works from.
float RailAscentSine()
{
    float degrees = 0.0f;
    if (Script::CStruct* walk = Script::GetStructure(kWalkParameters))
        walk->GetFloat(kHangMaxRailAscent, &degrees, 0);
    return static_cast<float>(std::sin(static_cast<double>(degrees) * kDegreesToRadians));
}

bool FindLedge(const Mth::Vector& start, const Mth::Vector& end, const Mth::Vector& direction, Mth::Vector* point)
{
    Obj::CRailManager* rails = LevelRails();
    if (!rails)
        return false;
    bool found = false;
    float best = 1e14f;
    const float steepest = RailAscentSine();
    for (int index = 0; index < rails->m_count; ++index) {
        const Obj::CRailNode* node = &rails->m_nodes[index];
        if ((node->m_flags & Obj::RAIL_FLAG_LADDER) || !(node->m_flags & Obj::RAIL_FLAG_ACTIVE) || !node->m_next)
            continue;
        Mth::Vector a, b, at;
        rails->GetWorldPos(&a, node);
        rails->GetWorldPos(&b, node->m_next);
        float score = 0.0f;
        if (!Bike::LedgePoint(start, end, direction, a, b, kTapMargin, &at, &score) || score > best)
            continue;
        Mth::Vector along = b - a;
        along.Normalize();
        if (std::fabs(along.y) > steepest)
            continue;
        if (!Obj::RailClear(a, b))
            continue;
        best = score;
        *point = at;
        found = true;
    }
    return found;
}

// The bike (the skater's special item named ThawBmx), or null.
Obj::CCompositeObject* BikeOf(void* skater)
{
    auto* items = static_cast<Obj::CSpecialItemComponent*>(
        static_cast<Obj::CCompositeObject*>(skater)->GetComponent(kSpecialItem));
    if (!items)
        return nullptr;
    const uint32_t id = Checksum(kBikeObject);
    for (Obj::CCompositeObject* item : items->m_items) {
        if (item && (item->m_id == id || item->m_id == id + static_cast<Obj::CObject*>(skater)->m_id))
            return item;
    }
    return nullptr;
}

void SetWobble(Obj::CCompositeObject* object, float fraction)
{
    if (auto* blender = static_cast<Obj::CAnimBlenderComponent*>(object->GetComponent(kAnimBlender)))
        blender->FirstChannel47E280(fraction, 1);
}

} // namespace

Obj::CCompositeObject* BikeObjectOf(void* skater) { return BikeOf(skater); }

// --- the maths ---------------------------------------------------------------------------------------------------

namespace Bike {

Mth::Vector TireTapVelocity(const Mth::Vector& velocity)
{
    Mth::Vector v = velocity;
    double flat = std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.z) * v.z);
    if (flat > 0.0) {
        double keep = flat > kTapSlowing ? (flat - kTapSlowing) / flat : 0.0;
        v.x = static_cast<float>(v.x * keep);
        v.z = static_cast<float>(v.z * keep);
    }
    if (v.y > 0.0f)
        v.y = v.y > kTapRiseFast ? v.y - v.y * kTapRiseFraction : v.y - kTapRiseSlow;
    return v;
}

TapStep TireTapStep(const Mth::Vector& position, const Mth::Vector& tap)
{
    TapStep step = {position, false, false};
    Mth::Vector toward = {tap.x - position.x, 0.0f, tap.z - position.z, 0.0f};
    double distance = std::sqrt(Dot3(toward, toward));
    if (distance < kTapReach) {
        if (tap.y > position.y) {
            step.position = {tap.x, tap.y, tap.z, position.w};
            step.lands = true;
        } else {
            step.position.x = tap.x;
            step.position.z = tap.z;
        }
        return step;
    }
    if (tap.y > position.y) {
        step.ends = true;
        return step;
    }
    float scale = static_cast<float>(kTapReach / distance);
    step.position.x += toward.x * scale;
    step.position.z += toward.z * scale;
    return step;
}

float ManualWobble(float manualAngle) { return static_cast<float>(manualAngle * kManualWobble); }

uint32_t BalanceOffset(uint32_t kind)
{
    // THAW 0x006207A8: the balance trick component's balance for its kind (THUG2's layout is the same).
    switch (kind) {
    case 0x255ED86F: // Grind
    case 0x8D10119D: // Slide
        return 0x7C;
    case 0xEF24413B: // Manual
    case 0x0AC90769: // NoseManual
        return 0x24;
    case 0xA549B57B: // Lip
        return 0xD4;
    case 0x3506CE64: // Skitch
        return 0x12C;
    default:
        return 0;
    }
}

float BalanceWobble(float lean, bool flip) { return (flip ? kBalanceMiddle - lean : lean + kBalanceMiddle) * kBalanceScale; }

bool DisplayPitchBeyond(const Mth::Vector& up, const Mth::Vector& normal, float degrees)
{
    return std::cos(static_cast<double>(degrees) * kDegreesToRadians) > Dot3(up, normal);
}

bool LedgePoint(const Mth::Vector& start, const Mth::Vector& end, const Mth::Vector& direction, const Mth::Vector& a,
                const Mth::Vector& b, float margin, Mth::Vector* point, float* score)
{
    Mth::Line line(start, end), rail(a, b);
    Mth::Vector onLine, onRail;
    float s = 0.0f, t = 0.0f;
    if (!Mth::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true)) {
        // (along the line, or a rail of no length: the rail's point nearest the line's start)
        Mth::Vector along = b - a;
        double length = Dot3(along, along);
        double k = length > 0.0 ? Dot3(start - a, along) / length : 0.0;
        k = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
        onRail = a + static_cast<float>(k) * along;
        onLine = start;
    }
    if (onRail.y > onLine.y || onLine.y - onRail.y > kLedgeHeight)
        return false;
    Mth::Vector apart = onRail - onLine;
    if (Dot3(apart, apart) > static_cast<double>(margin) * margin)
        return false;
    double forward = Dot3(apart, direction);
    if (forward > 0.0)
        apart -= static_cast<float>(forward * 0.6666667f) * direction;
    *score = static_cast<float>(Dot3(apart, apart));
    *point = onRail;
    return true;
}

} // namespace Bike

// --- the gates ---------------------------------------------------------------------------------------------------

bool WallrideButton(void* core) { return PadOf(core)[kSquare] != 0; }

bool WallplantBySquare(void* core) { return PadOf(core)[kSquare] != 0; }

uint32_t WallplantTrickList()
{
    uint32_t bike = Checksum(kWallplantTrick);
    return Script::GetArray(bike) ? bike : kThugWallplantTrick;
}

uint8_t* SkitchButton(void* core) { return PadOf(core) + kR2; }

// --- the tire tap ------------------------------------------------------------------------------------------------

bool TireTapping(const void* core)
{
    const BikeState* state = StateOf(core, false);
    return state && state->tireTap;
}

namespace {

// THAW 0x005DC290: in the air, R1 and up or down held: a point to tap the wheel on - in vert the surface below (not
// within 30 under the bike already; feelers 1000 down from points along the bike's down, from 100 by 3), else a ledge
// (a rail) on the way ahead (100 along the flattened velocity; THAW's ledge search through the walk component's hang
// check, here FindLedge). Found: the tire tap on, the bike turned 45 degrees over (up held: onto the front wheel),
// THAW's vert flags off. In vert, once asked, it goes on looking every frame without the buttons (THAW's +0x54C) -
// for the coping still to come; out of vert only while they're held.
void StartTireTap(void* core, BikeState& state)
{
    uint8_t* flags = FlagsOf(core);
    if (flags[kFlagAcidDrop]) {
        state.tapArmed = false;
        return;
    }
    bool notVert = !flags[kFlagVert];
    if (notVert)
        state.tapArmed = false;
    uint8_t* pad = PadOf(core);
    bool held = pad[kR1] && (state.stickUp.down || state.stickDown.down);
    if (!state.tapArmed && !held)
        return;
    void* object = ObjectOf(core);
    auto* physics = static_cast<CorePhysicsComponent*>(core);
    const Mth::Vector position = VectorAt(object, kPosition);
    const Mth::Vector up = VectorAt(object, kUp);
    bool found = false;
    float best = kTapFar;
    if (!notVert) {
        VectorAt(core, kFeelerStart) = position;
        VectorAt(core, kFeelerEnd) = position + kTapAbove * up;
        if (physics->CheckFeeler(0x10, 0)) {
            state.tapArmed = true;
            return;
        }
        // (phase 7: THAW's search direction is the bike's down flattened - THAW 0x005DC290 zeroes its y - out
        // over the deck; phase 4 took the down itself)
        Mth::Vector down = {-up.x, 0.0f, -up.z, -up.w};
        down.Normalize();
        for (float depth = kTapSearchStart; depth > kTapSearchEnd; depth -= kTapSearchStep) {
            Mth::Vector from = position + depth * down;
            Mth::Vector to = {from.x, from.y - kTapDrop, from.z, from.w};
            VectorAt(core, kFeelerStart) = from;
            VectorAt(core, kFeelerEnd) = to;
            if (!physics->CheckFeeler(0x10, 0) || (At<uint16_t>(core, kHitFlags) & 8))
                continue;
            float distance = from.y - VectorAt(core, kHitPoint).y;
            if (distance > best + kTapNearer)
                continue;
            best = distance;
            state.tapPoint = VectorAt(core, kHitPoint);
            state.tapStart = position;
            state.tapLedge = false;
            found = true;
        }
    }
    Mth::Vector direction = VectorAt(object, kVelocity);
    direction.y = 0.0f;
    direction.w = 0.0f;
    if (Dot3(direction, direction) > 0.0)
        direction.Normalize();
    Mth::Vector end = {position.x, position.y - kTapDrop, position.z, 1.0f};
    if (notVert)
        end = position + kTapAhead * direction;
    // A ledge (phase 7, THAW 0x005DC290: looked for in vert too, and taken over a surface found below unless it's
    // a thousand times further away).
    Mth::Vector ledge;
    if (FindLedge(position, end, direction, &ledge)) {
        Mth::Vector apart = ledge - position;
        double distance = std::sqrt(Dot3(apart, apart)) * kTapLedgeWeight;
        if (!found || distance <= static_cast<double>(best) + kTapNearer) {
            state.tapPoint = ledge;
            state.tapStart = position;
            state.tapLedge = true;
            found = true;
        }
    }
    if (!found) {
        state.tapArmed = true;
        return;
    }
    // On.
    state.tireTap = true;
    state.tireTapNose = state.stickUp.down;
    state.tapPitch = 0.0f;
    SetTimedFlag(core, kFlagVert, false);
    SetTimedFlag(core, kFlag38, false);
    SetTimedFlag(core, kFlag48, false);
}

// THAW 0x005CE920: the tire tap going on - over when an acid drop starts; while R1 and up / down stay held (or the point
// is a ledge's) the bike slows and goes towards the point (reaching it above: put on it - a ledge's: landed from the
// tire tap), the point above it while still far off: over; let go, the bike is pulled the way the point lies. Rising, it
// slows (THAW's fixed amounts a frame).
void CarryTireTap(void* core, BikeState& state)
{
    uint8_t* flags = FlagsOf(core);
    if (flags[kFlag70]) {
        state.tireTap = false;
        state.tapLedge = false;
        return;
    }
    state.tapArmed = false;
    void* object = ObjectOf(core);
    Mth::Vector& velocity = VectorAt(object, kVelocity);
    Mth::Vector& position = VectorAt(object, kPosition);
    uint8_t* pad = PadOf(core);
    bool held = pad[kR1] && (state.stickUp.down || state.stickDown.down);
    if (held || state.tapLedge) {
        velocity = Bike::TireTapVelocity(velocity);
        Bike::TapStep step = Bike::TireTapStep(position, state.tapPoint);
        if (step.ends) {
            state.tireTap = false;
            state.tapLedge = false;
            return;
        }
        position = step.position;
        if (step.lands) {
            // (THAW: its ledge state 7 and Landed; ours: on the point, falling no more - THUG2's ground check lands
            // the bike on it next.)
            if (velocity.y > 0.0f)
                velocity.y = 0.0f;
            state.landedFromTireTap = state.tapLedge;
        }
    } else {
        Mth::Vector pull = {state.tapPoint.x - state.tapStart.x, 0.0f, state.tapPoint.z - state.tapStart.z, 0.0f};
        if (Dot3(pull, pull) > 0.0)
            pull.Normalize();
        velocity.x += pull.x * kTapPull;
        velocity.z += pull.z * kTapPull;
        if (velocity.y > 0.0f)
            velocity.y = velocity.y > kTapRiseFast ? velocity.y - velocity.y * kTapRiseFraction
                                                    : velocity.y - kTapRiseSlow;
    }
    // THAW 0x005DC9E9..: the bike turned over to 45 degrees in half a second (THAW slerps its display there; here the
    // skater's matrix and the display's, a step a frame).
    float target = state.tireTapNose ? kTapPitch : -kTapPitch;
    float remaining = target - state.tapPitch;
    if (remaining != 0.0f) {
        float most = kTapPitch / kTapPitchTime * FrameLength(core);
        float step = std::fabs(remaining) < most ? remaining : (remaining > 0.0f ? most : -most);
        state.tapPitch += step;
        At<Mth::Matrix>(object, kMatrix).RotateXLocal(step);
        At<Mth::Matrix>(core, kDisplayMatrix).RotateXLocal(step);
    }
}

} // namespace

void TireTap(void* core)
{
    BikeState& state = *StateOf(core, true);
    state.landedFromTireTap = false;
    if (state.tireTap)
        CarryTireTap(core, state);
    else
        StartTireTap(core, state);
}

// --- the manual's pose -------------------------------------------------------------------------------------------

void ManualPose(void* core)
{
    auto* skater = static_cast<Obj::CCompositeObject*>(ObjectOf(core));
    Obj::CCompositeObject* bike = BikeOf(skater);
    const BikeState* state = StateOf(core, false);
    if (!state)
        return;
    float fraction;
    if (state->manual) {
        fraction = Bike::ManualWobble(state->manualAngle);
    } else {
        // THAW 0x006207A8: else the running balance trick's balance (the grind's and the stall's range animations
        // among them) - THAW keeps one wobble fraction, which its animation trees play on the rider and the bike alike.
        // THUG2's balance (CBalance::Update) moves only the rider's animation: the bike stayed on its range
        // animation's first or last frame - in a stall (played from its end) beside the rail, twisted. The flip as
        // THUG2's balance takes it for the rider (THAW asks its own stance there), so the two agree.
        uint8_t* tricks = At<uint8_t*>(core, kBalanceTricks);
        uint32_t at = tricks ? Bike::BalanceOffset(At<uint32_t>(tricks, 0x18)) : 0;
        if (!at)
            return;
        const auto* balance = reinterpret_cast<const Obj::CBalance*>(tricks + at);
        bool flip = balance->m_flip;
        if (balance->m_flipped && At<uint8_t*>(core, kFlags)[0x28])
            flip = !flip;
        fraction = Bike::BalanceWobble(balance->m_lean, flip);
    }
    SetWobble(skater, fraction);
    if (bike)
        SetWobble(bike, fraction);
}

// --- the shadow ----------------------------------------------------------------------------------------------------

namespace Bike {

void* ShadowCompanionOf(const void* casterModel, const void* riderModel, void* bikeModel, uint8_t bikeModelFlags)
{
    if (!casterModel || casterModel != riderModel || !bikeModel)
        return nullptr;
    return (bikeModelFlags & 0x2) && !(bikeModelFlags & 0x4) ? bikeModel : nullptr;
}

} // namespace Bike

void* ShadowCompanion(void* casterModel)
{
    // THAW's bike casts a volume shadow of its own (bike_details: Volume_BMX_Shadow.shd - THUG2 can't read it); here
    // the bike is drawn into the rider's detailed shadow, so the two are one shadow along the rider's light (phase 4's
    // detailed shadow of the bike's own was cast straight down, apart from his). [features] thaw_bmx_shadow=0: none.
    static const bool shadow = ConfigInt("features", "thaw_bmx_shadow", 1) != 0;
    if (!Enabled() || !shadow || !Mdl::Skate::Instance())
        return nullptr;
    auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
    if (!skater || !skater->m_corePhysics || !OnBike(skater->m_corePhysics))
        return nullptr;
    Obj::CBaseComponent* riderModel = skater->GetComponent(kModel);
    Obj::CCompositeObject* bike = BikeOf(skater);
    Obj::CBaseComponent* bikeModel = bike ? bike->GetComponent(kModel) : nullptr;
    if (!riderModel || !bikeModel)
        return nullptr;
    uint8_t* bikeRenderer = At<uint8_t*>(bikeModel, kModelOfComponent);
    return Bike::ShadowCompanionOf(casterModel, At<void*>(riderModel, kModelOfComponent), bikeRenderer,
                                   bikeRenderer ? bikeRenderer[kModelFlags] : 0);
}

// --- the flip ----------------------------------------------------------------------------------------------------

int PlayFlip(void* object)
{
    if (!Enabled() || !object || !Mdl::Skate::Instance())
        return -1;
    auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
    if (!skater || !skater->m_corePhysics || !OnBike(skater->m_corePhysics))
        return -1;
    if (object == skater) {
        // (THAW's Skater_Playanim: HandleFlipOrBoardRotateAfter before the branch - the component's own PlayAnim
        // answer does it too, after this or before: done once, the flags cleared)
        if (auto* flipAndRotate =
                static_cast<Obj::CSkaterFlipAndRotateComponent*>(skater->GetComponent(kSkaterFlipAndRotate)))
            flipAndRotate->DoAfterAnim();
    } else if (object != BikeOf(skater)) {
        return -1;
    }
    return At<uint8_t*>(skater->m_corePhysics, kFlags)[0x28] ? 1 : 0;
}

void FlipAnimsToFlag(void* core)
{
    // (off the bike THUG2's way again: the rider's animations as his flag says - on the bike a Flip after the last
    // animation played left them as they were)
    auto* skater = static_cast<Obj::CCompositeObject*>(ObjectOf(core));
    if (auto* flipAndRotate =
            skater ? static_cast<Obj::CSkaterFlipAndRotateComponent*>(skater->GetComponent(kSkaterFlipAndRotate))
                   : nullptr)
        flipAndRotate->ApplyFlipToAnims();
}

// --- the sounds --------------------------------------------------------------------------------------------------

int BikeSoundOf(const void* skater, BikeSound kind, void* info)
{
    if (!Enabled() || !skater)
        return -1;
    const void* core = static_cast<const Obj::CSkater*>(skater)->m_corePhysics;
    if (!core || !OnBike(core))
        return -1;
    if (kind == BikeSound::Air)
        return 0;
    Script::CArray* table = Script::GetArray(Checksum(kSoundTables[static_cast<int>(kind)]));
    if (!table || table->m_size == 0)
        return -1;
    auto* entry = reinterpret_cast<Script::CStruct*>(table->GetElement(0));
    if (!Terrain::GetSoundInfo(entry, static_cast<Terrain::SoundInfo*>(info)))
        return 0;
    // (the first of each kind logged: the bike's sounds can be checked from the log)
    static bool s_logged[8] = {};
    int k = static_cast<int>(kind);
    if (!s_logged[k]) {
        s_logged[k] = true;
        Log("thaw_bmx: bike sound %s -> %s", kSoundTables[k], static_cast<Terrain::SoundInfo*>(info)->name);
    }
    return 1;
}

// --- the members -------------------------------------------------------------------------------------------------

int CallTrickMember(void* core, uint32_t name, Script::CStruct* params, Script::CScript*)
{
    static const uint32_t kDisplayPitch = Checksum("DisplayPitchGreaterThan"),
                          kLandedFromTap = Checksum("LandedFromTireTap"), kInTireTap = Checksum("InTireTap"),
                          kPitchingForward = Checksum("PitchingForward"),
                          kPitchingBackward = Checksum("PitchingBackward"),
                          kShouldStall = Checksum("ShouldRunStallScript"),
                          kClearStall = Checksum("ClearStallScriptFlag"), kExitStallBoost = Checksum("ExitStallBoost"),
                          kOnStall = Checksum("OnStall");
    if (name == kDisplayPitch) {
        // 0x005CA490: the model's display up (the model component's matrix: THUG2's model draws
        // with the display matrix) against the GROUND normal (+0x68 - phase 4 took THAW's +0x68 for the display
        // normal; THAW's 0x005D0BD0 shows it's the ground's, as THUG2's).
        float degrees = 0.0f;
        if (params)
            params->GetFloat(static_cast<uint32_t>(0), &degrees, 0);
        return Bike::DisplayPitchBeyond(VectorAt(core, kDisplayUp), VectorAt(core, kGroundNormal), degrees) ? 1 : 0;
    }
    if (name == kLandedFromTap) { // 0x005C7AF0
        const BikeState* state = StateOf(core, false);
        return state && state->landedFromTireTap ? 1 : 0;
    }
    if (name == kInTireTap)
        return TireTapping(core) ? 1 : 0;
    if (name == kPitchingForward || name == kPitchingBackward) {
        // THAW's matrix queries (THAW 0x005EBBC0 / 0x005EBBF0): the air pitch so far as the last frame in the air
        // began (+0x18C0) - above 0 forward (nose down), below 0 backward (nose up). bike_land2 starts the wheelie
        // from the air on it (with the stick held back / forward as it lands).
        const BikeState* state = StateOf(core, false);
        float pitch = state ? state->lastAirPitch : 0.0f;
        return (name == kPitchingForward ? pitch > 0.0f : pitch < 0.0f) ? 1 : 0;
    }
    if (name == kShouldStall) { // 0x005C8410: +0x49D
        const BikeState* state = StateOf(core, false);
        return state && state->stallScript ? 1 : 0;
    }
    if (name == kClearStall) { // 0x005C8420: cleared only once in the stall state
        BikeState* state = StateOf(core, false);
        if (state && At<int>(FlagsOf(core), 0x18) == kStateBikeStall)
            state->stallScript = false;
        return 1;
    }
    if (name == kOnStall) // (its state component's: the physics state the stall's)
        return At<int>(FlagsOf(core), 0x18) == kStateBikeStall ? 1 : 0;
    if (name == kExitStallBoost) {
        // 0x005C83D0: the velocity's x / z from THAW's +0x714 / +0x71C, which nothing of THAW's
        // writes that a search finds (the skater's stall boost) - left out; only BIKE_OLLIE's Boost asks, which no bike
        // script passes.
        return 1;
    }
    return 2;
}

} // namespace Skater::ThawBmx
