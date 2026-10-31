// The skater's adjust-physics component (adjust_physics.h). From the disassembly of 0x004F8420.

#include "game/skater/adjust_physics.h"

#include <cstdint>

#include "game/park_editing/park_editor.h"
#include "game/maths/vec4.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/movable_contact.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/shadow_component.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater_state.h"
#include "game/skater/thaw_bmx.h"

namespace GameObjects {

namespace {

// The core physics component's fields read here.
constexpr uint32_t kFirstTick = 0x98;    // the position as the frame began
constexpr uint32_t kFeeler = 0xA8;        // its feeler (the last ground hit)
constexpr uint32_t kHitPoint = 0x120;
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kProbeOrigin = 0x208;
constexpr uint32_t kProbeTip = 0x218;
constexpr uint32_t kTimedFlags = 0x1528;  // the skater's state component (+0x18 the physics state)
// The skater object's.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kVelocity = 0xAC;
// The state component's: the height over the ground the skater stands on.
constexpr uint32_t kHeightOverGround = 0x11C;

constexpr uint32_t kUberFrigCurrentHeight = 0x801C9CB6; // uber_frig_current_height (feet)
constexpr uint32_t kUnderTheFloor = 0xAFAA46BA;         // the event sent when lifted out from under a floor (lip state)
constexpr uint32_t kParkEditorTestPlay = 0xD443A2BC;    // the park editor's +0x40 while its park is being played

constexpr float kUpNudge = 0.01f;          // 0x0064544C
constexpr float kFeelerAbove = 8.0f;       // 0x006461D4
constexpr float kFeet = 12.0f;             // 0x0064546C
constexpr float kLeastHeight = 0.001f;     // 0x006461D8
constexpr float kFloorProbeHeight = 3000.0f; // 0x0064B9D0
constexpr float kFloorNear = 36.0f;        // 0x0064B9CC (6 squared)
constexpr float kOne = 1.0f;               // 0x006453A8
constexpr float kBounce = -0.5f;           // (0xBF000000)
constexpr float kLift = 2.0f;              // 0x00645448
// The velocity's nudge after the uber frig (0x0064B9C8, 0x0064B9C8, 0x0064B9C4, 0x006453A8).
constexpr float kNudgeXY = 0.101f, kNudgeZ = 0.1001f;
// The uber frig's offsets from the frame's start, one a try (the original's function-local static, 0x007CCD60).
constexpr float kUberFrigOffsets[5][4] = {
    {0.0f, 0.0f, 0.0f, 1.0f}, {0.1f, 0.1f, 0.1f, 1.0f}, {-0.2f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -0.2f, 1.0f}, {0.2f, 0.0f, 0.0f, 1.0f},
};

template <typename T>
T& At(const void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
Maths::Vec4& VectorAt(const void* base, uint32_t offset) { return At<Maths::Vec4>(base, offset); }

int StateOf(void* core) { return At<int>(At<void*>(core, kTimedFlags), 0x18); }
void ChangePhase(void* core, int state) { static_cast<Skater::SkaterPhysicsModule*>(core)->SetPhysicsState(state); }

} // namespace

// 0x004F8420
void CRiderPhysicsTweaks::TickFrame()
{
    void* core = m_corePtr;
    void* object = m_entity;
    m_uberFrigged = 0;
    Maths::Vec4& position = VectorAt(object, kPosition);
    const Maths::Vec4& frameStart = VectorAt(core, kFirstTick);
    if (position.x == frameStart.x && position.y == frameStart.y && position.z == frameStart.z &&
        position.w == frameStart.w)
        return; // (not moved this frame)

    // The ground below: a feeler from just over the skater (8 up) to uber_frig_current_height feet under it.
    Maths::Vec4& start = VectorAt(core, kProbeOrigin);
    Maths::Vec4& end = VectorAt(core, kProbeTip);
    start = position;
    end = position;
    const Maths::Vec4& up = VectorAt(object, kUp);
    start.x = static_cast<float>(static_cast<double>(up.x) * kUpNudge + start.x);
    start.y = static_cast<float>(static_cast<double>(up.y) * kUpNudge + start.y);
    start.z = static_cast<float>(static_cast<double>(up.z) * kUpNudge + start.z);
    start.w = static_cast<float>(static_cast<double>(up.w) * kUpNudge + start.w);
    start.y = static_cast<float>(static_cast<double>(start.y) + kFeelerAbove);
    end.y = static_cast<float>(static_cast<double>(end.y) -
                               static_cast<double>(Scripts::FetchFloat(kUberFrigCurrentHeight)) * kFeet);
    // Not original: THAW 0x005C2820 - on the bike the feeler starts higher (thaw_bmx_physics.cpp).
    float raise = Skater::ThawBmx::LiveNow(core) ? Skater::ThawBmx::StuckCheckRaise(core) : 0.0f;
    if (raise != 0.0f)
        start.y += raise;
    auto* physics = static_cast<Skater::SkaterPhysicsModule*>(core);
    uint8_t* parkEditor = reinterpret_cast<uint8_t*>(static_cast<void*>(ParkEditing::g_parkEditor));

    if (!physics->ProbeTouchesZone(0x10, 0)) {
        // Nothing at all: the uber frig (not in the park editor's play while in states 3 / 4): back where the frame
        // began (a little off it each try), going the other way; out of the air states, into the air.
        int state = StateOf(core);
        if (!((state == 4 || state == 3) && At<uint32_t>(parkEditor, 0x40) == kParkEditorTestPlay)) {
            m_uberFrigged = 1;
            if (m_uberFrigTries < 5) {
                const float* offset = kUberFrigOffsets[m_uberFrigTries++];
                const Maths::Vec4& from = VectorAt(core, kFirstTick);
                position = {offset[0] + from.x, from.y + offset[1], from.z + offset[2], from.w + offset[3]};
            } else {
                m_uberFrigTries = 0;
            }
            Maths::Vec4& velocity = VectorAt(object, kVelocity);
            velocity = {-velocity.x + kNudgeXY, -velocity.y + kNudgeXY, -velocity.z + kNudgeZ, -velocity.w + kOne};
            state = StateOf(core);
            if (state != 0 && state != 1)
                ChangePhase(core, 1);
        }
        At<float>(m_stateRef, kHeightOverGround) = 0.0f;
        return;
    }

    // Ground. Standing on a moving object (the skater on the ground or in the air, not skitching (+0x88) nor
    // wallriding (+0x78)) that isn't what the feeler hit: off it, with its velocity.
    uint8_t* flags = At<uint8_t*>(core, kTimedFlags);
    if (!flags[0x88] && !flags[0x78] && m_ride->m_ride) {
        CRideTracker* ride = m_ride->m_ride;
        bool leave = true;
        if (ride->m_entity.m_ownerObject) {
            int state = At<int>(flags, 0x18);
            if (state == 0 || state == 1) {
                Engine3D::CProbeRay& feeler = At<Engine3D::CProbeRay>(core, kFeeler);
                if (feeler.HitObjectAlive() && feeler.GetHitObject() == ride->m_entity.m_ownerObject)
                    leave = false;
            } else {
                leave = false;
            }
            if (leave)
                VectorAt(object, kVelocity) += VectorAt(m_ride->m_ride->m_entity.m_ownerObject, kVelocity);
        }
        if (leave)
            m_ride->StopRiding();
    }

    // Kept just over the ground.
    {
        double over = static_cast<double>(position.y) - VectorAt(core, kHitPoint).y;
        At<float>(m_stateRef, kHeightOverGround) = static_cast<float>(over);
        if (over < kLeastHeight)
            position.y = static_cast<float>(static_cast<double>(VectorAt(core, kHitPoint).y) + kLeastHeight);
    }
    m_shadowHandle->ChangePlace(&VectorAt(core, kHitPoint));
    m_shadowHandle->SetNormal(&VectorAt(core, kHitNormal));
    if (StateOf(core) != 4)
        UpdateBrightness(&At<Engine3D::CProbeRay>(core, kFeeler));

    if (At<uint32_t>(parkEditor, 0x40) != kParkEditorTestPlay) {
        // Under the floor: a feeler from far above down to a unit over the skater (faces flagged 0x800 - "under ok" -
        // passed over) hits a floor. Within 6 of it: onto it. Further, moved this frame: back where the frame began
        // at half the speed the other way (a lip: out of it, into the air, 2 up); hardly moved: onto it.
        Engine3D::CProbeRay feeler;
        feeler.MakeNew();
        feeler.SetExcluded(0x810, 0);
        Maths::Vec4 from = {position.x, static_cast<float>(static_cast<double>(position.y) + kFloorProbeHeight),
                            position.z, position.w};
        feeler.ChangeOrigin(&from);
        Maths::Vec4 to = {up.x + position.x, position.y + up.y, position.z + up.z, position.w + up.w};
        // Not original: on the bike as far up as the ground check's start (thaw_bmx_physics.cpp StuckCheckRaise).
        if (raise != 0.0f)
            to.y += raise;
        feeler.ChangeFinish(&to);
        if (feeler.GetHitShape(true, false)) {
            Maths::Vec4 off = {position.x - feeler.m_contactPos.x, position.y - feeler.m_contactPos.y,
                               position.z - feeler.m_contactPos.z, position.w - feeler.m_contactPos.w};
            double near = static_cast<double>(off.x) * off.x + static_cast<double>(off.y) * off.y +
                          static_cast<double>(off.z) * off.z;
            if (near < kFloorNear) {
                position = feeler.m_contactPos;
            } else {
                const Maths::Vec4& began = VectorAt(core, kFirstTick);
                Maths::Vec4 moved = {position.x - began.x, position.y - began.y, position.z - began.z,
                                     position.w - began.w};
                double movedSqr = static_cast<double>(moved.x) * moved.x + static_cast<double>(moved.y) * moved.y +
                                  static_cast<double>(moved.z) * moved.z;
                position = movedSqr < kOne ? feeler.m_contactPos : began;
                Maths::Vec4& velocity = VectorAt(object, kVelocity);
                velocity = {kBounce * velocity.x, kBounce * velocity.y, kBounce * velocity.z, kBounce * velocity.w};
                if (StateOf(core) == 4) {
                    ChangePhase(core, 1);
                    static_cast<CEntity*>(object)->PostOwnEvent(kUnderTheFloor, nullptr);
                    position.y = static_cast<float>(static_cast<double>(position.y) + kLift);
                }
            }
        }
        feeler.m_entity.Release();
    }
    m_uberFrigTries = 0;
    VectorAt(core, kFirstTick) = position;
}

} // namespace Obj
