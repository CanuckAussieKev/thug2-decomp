// The skater's core physics component: the lip state's update, and getting back on the board.

#include <cstdint>

#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/obj/walk_hang.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kLipKind = 0xA549B57B;        // Lip (the balance trick's kind)
constexpr uint32_t kPerfectRail = 0xCD09E062;    // CHEAT_PERFECT_RAIL
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kLipOffEvents = 0x0064BBE0;   // (getting off a lip)
constexpr uint32_t kOffLipEvents = 0x0064BBD4;   // {7, 4, 0}

void ChangeState(void* component, int state) { GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(component, state); }

} // namespace

// 0x0050BF50
void CorePhysicsComponent::DoLipPhysics()
{
    SetFlag(this, 0x78, 0);
    SetFlag(this, 0xA8, 0);
    SetFlag(this, 0x80, 0);
    At<uint8_t>(this, 0x2E4) = 0;
    // Carried by what the skater rides on: the position, and the start of the lip (+0x258) - 0x00427A00 works it out.
    if (Obj::CRideTracker* tracker = Tracker(this)) {
        if (!tracker->m_object.mp_object || !tracker->Update(&VectorAt(ObjectOf(this), kPosition))) {
            StopRiding(this);
        } else {
            // (0x00427A00 returns `where`; ours is void: the point is read from `where`)
            auto* contact = At<Obj::CMovableContactComponent*>(this, kRide);
            Mth::Vector where;
            void* object = ObjectOf(this);
            contact->GetContactPoint(&where);
            VectorAt(object, kPosition) = where;
            At<Obj::CMovableContactComponent*>(this, kRide)->GetContactPoint(&where);
            VectorAt(this, 0x258) = where;
            if (Tracker(this)->m_turned) {
                auto multiply =
                    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C);
                Mth::Matrix& matrix = MatrixAt(ObjectOf(this), kMatrix);
                multiply(&matrix, &matrix, &Tracker(this)->m_turn);
                multiply(&MatrixAt(this, kDisplayMatrix), &MatrixAt(this, kDisplayMatrix), &Tracker(this)->m_turn);
            }
        }
    }
    // The lip's balance (the Perfect Rail cheat: its global flag, and no lean).
    if (At<uint32_t>(At<void*>(this, kBalanceTrick), 0x18) == kLipKind) {
        auto career = [] { return At<Obj::CSkaterCareer*>(GameVariable<uint8_t*>(0x007CE478), 0x20); };
        if (career()->GetCheat(kPerfectRail)) {
            career()->SetGlobalFlag(Script::GetInteger(kPerfectRail));
            At<uint32_t>(At<void*>(this, kBalanceTrick), 0x104) = 0;
            At<uint32_t>(At<void*>(this, kBalanceTrick), 0x108) = 0;
            GameVariable<uint8_t>(0x007CE4FD) = 1;
        }
        reinterpret_cast<Obj::CBalance*>(At<uint8_t*>(this, kBalanceTrick) + 0xD4)->Update();
    }
    if (!Flag(this, 0x20) && InputOf(this)[0x1C0])
        SetFlag(this, 0x20, 1);
    // Off the lip (0x004FFB10): the rail's TriggerScript, twice.
    if (GameFunction<bool(__thiscall*)(void*)>(0x004FFB10)(this)) {
        SetFlag(this, 0xD8, 1);
        TriggerRailScript(reinterpret_cast<const int*>(kLipOffEvents));
        TriggerRailScript(reinterpret_cast<const int*>(kOffLipEvents));
    }
    auto* node = At<Obj::CRailNode*>(this, kRailNode);
    if (node && At<Obj::CRailManager*>(this, kRailManager)->m_nodeArray) {
        Script::CStruct* script = SkateScript::GetNode(node->m_name, true);
        script->GetChecksum(kName, &At<uint32_t>(this, kRailScriptNode), 1);
        static_cast<Obj::CTrickComponent*>(At<void*>(this, kSkaterComponent))
            ->PassToTrickObjects(At<uint32_t>(this, kRailScriptNode));
    }
    At<int>(this, 0x238) = Now();
}

// 0x0050C670
void CorePhysicsComponent::ResumeSkating(bool onGround, const uint8_t* grind, void* acidDrop)
{
    GameFunction<void(__thiscall*)(void*)>(0x004FDE40)(this);
    if (onGround) {
        ChangeState(this, kGround);
        At<int>(this, 0x94) = kGround;
    } else {
        ChangeState(this, kAir);
        At<int>(this, 0x94) = kAir;
        SetFlag(this, 0xC0, 1);
        if (At<uint8_t>(At<void*>(this, 0x1534), 0x9B))
            SetFlag(this, 0xB0, 1);
    }
    At<int>(this, 0x90) = Now();
    MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
    // The ground normals from the up (+0x68, +0x78, +0x58), the position kept as the last.
    VectorAt(this, kGroundNormal) = VectorAt(ObjectOf(this), kUp);
    VectorAt(this, 0x78) = VectorAt(this, kGroundNormal);
    VectorAt(this, 0x58) = VectorAt(this, 0x78);
    VectorAt(this, 0x98) = VectorAt(ObjectOf(this), kPosition);
    VectorAt(ObjectOf(this), kOldPosition) = VectorAt(this, 0x98);
    // The jump / trick state cleared.
    At<uint8_t>(this, 0x1C5) = 0;
    At<int>(this, 0x2C0) = 0;
    At<int>(this, kTenseTime) = 0;
    At<int>(this, 0x39C) = 0;
    At<int>(this, 0x3A0) = 0;
    At<uint8_t>(this, 0x370) = 0;
    At<uint8_t>(this, 0x310) = 0;
    At<uint8_t>(this, 0x311) = 1;
    At<uint8_t>(FlagsOf(this), 0xF4) = 0; // (without its time)
    if (At<uint32_t>(this, 0x350) != 0xC0000000)
        At<uint32_t>(this, 0x350) = 0;
    At<float>(this, 0x3AC) = 1.0f;
    At<int>(this, 0x4E0) = 0;
    At<int>(this, 0x4E4) = 0;
    At<int>(this, kJumpTime) = 0;
    At<uint8_t>(this, 0x4E8) = 0;
    if (grind) {
        auto* rail = const_cast<uint8_t*>(grind);
        // Onto a rail: the old ride let go (its object kept at +0x6C), the rail's object ridden.
        void* ride = At<void*>(this, kRide);
        if (Obj::CRideTracker* tracker = At<Obj::CRideTracker*>(ride, 0x18)) {
            At<void*>(ride, 0x6C) = tracker->m_object.mp_object;
            reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>*>(&tracker->m_object)->Release();
            Mem::Manager::Get()->Free(tracker);
            At<Obj::CRideTracker*>(ride, 0x18) = nullptr;
            At<int>(ride, 0x70) = Now();
        }
        if (void* object = At<void*>(rail, 0x18))
            At<Walk::MovableContact*>(this, kRide)->StartRiding(static_cast<Obj::CCompositeObject*>(object));
        StartGrind(reinterpret_cast<const Mth::Vector*>(grind), At<Obj::CRailNode*>(rail, 0x14),
                   At<Obj::CRailManager*>(rail, 0x10), true, true);
        VectorAt(this, 0x98) = VectorAt(ObjectOf(this), kPosition);
        VectorAt(ObjectOf(this), kOldPosition) = VectorAt(this, 0x98);
    } else if (acidDrop) {
        StartAcidDrop(static_cast<const AcidDrop*>(acidDrop));
    }
}

} // namespace Skater

REPLACE(0x0050BF50, &Skater::CorePhysicsComponent::DoLipPhysics);
REPLACE(0x0050C670, &Skater::CorePhysicsComponent::ResumeSkating);
