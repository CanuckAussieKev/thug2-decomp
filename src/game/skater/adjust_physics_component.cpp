// The skater's adjust-physics component (adjust_physics.h): its life, its frame and the checks around Update
// (adjust_physics.cpp). From the disassembly of 0x004F8040-0x004F840F and 0x004F8AA0-0x004F8CC2.

#include "game/skater/adjust_physics.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/obj/movable_contact.h"
#include "game/obj/walk_hang.h"
#include "game/sys/game_clock.h"
#include "game/skater/core_physics.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kModel = 0x286A8D26, kShadow = 0x8A897DD2,
                   kMovableContact = 0x408D2AA9, kSkaterState = 0x43686585;
constexpr uint32_t kUnderTheFloor = 0xAFAA46BA; // (as Update sends it)

// The core physics component's: the position as the frame began, its state component (+0x18 the physics state,
// +0x60 landed, +0x64 when, +0x88 skitching).
constexpr uint32_t kFrameStart = 0x98;
constexpr uint32_t kTimedFlags = 0x1528;

constexpr float kFeelerReach = 30.0f; // 0x00646240
constexpr float kBrightnessAbove = 12.0f; // 0x0064546C

template <typename T>
T& At(const void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

void ChangePhase(void* core, int state) { static_cast<Skater::CorePhysicsComponent*>(core)->SetPhysicsState(state); }

// The debug message's strings as the original passes them ("M:%s:%i: %s\n", the source file, "found to be inside a
// moving object").

} // namespace

// 0x004F80E0
CObjectModule* __cdecl CRiderPhysicsTweaks::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderPhysicsTweaks));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderPhysicsTweaks*>(memory);
    component->CObjectModule::MakeNew();
    component->mp_core = nullptr;
    component->m_meshInstance = nullptr;
    component->m_shadowHandle = nullptr;
    component->mp_ride = nullptr;
    component->m_uberFrigged = 0;
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    return component;
}

// 0x004F8040
void CRiderPhysicsTweaks::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x004F8050
CRiderPhysicsTweaks* CRiderPhysicsTweaks::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004F8080
void CRiderPhysicsTweaks::Seal()
{
    mp_core = m_entity->FetchModule(kSkaterCorePhysics);
    m_meshInstance = reinterpret_cast<CMeshInstanceModule*>(m_entity->FetchModule(kModel));
    m_shadowHandle = reinterpret_cast<CGroundShadowModule*>(m_entity->FetchModule(kShadow));
    mp_ride = reinterpret_cast<CMovingSurfaceModule*>(m_entity->FetchModule(kMovableContact));
    mp_state = reinterpret_cast<CRiderPoseState*>(m_entity->FetchModule(kSkaterState));
}

// 0x004F8C40
void CRiderPhysicsTweaks::FrameUpdate()
{
    uint8_t* flags = At<uint8_t*>(mp_core, kTimedFlags);
    if (At<int>(flags, 0x18) == 0 || m_entity->m_vel.y < 0.0f) {
        if (flags[0x60] != 1) {
            flags[0x60] = 1;
            At<int>(flags, 0x64) = FrameClock::GameClockElapsed() + g_clockOffset;
        }
    }
    if (!At<uint8_t*>(mp_core, kTimedFlags)[0x88])
        CheckInsideMovingObject();
    TickFrame();
    m_entity->m_oldPos = m_entity->m_atPoint;
}

// 0x004F8AA0
void CRiderPhysicsTweaks::CheckInsideMovingObject()
{
    Maths::Vec4& frameStart = At<Maths::Vec4>(mp_core, kFrameStart);
    if (!mp_ride->FindContactAhead(&m_entity->m_atPoint, &frameStart))
        return;
    DebugOut::Printf("M:%s:%i: %s\n", "mponents\\SkaterAdjustPhysicsComponent.cpp", 0x182, "found to be inside a moving object");
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    const Maths::Vec4& pos = m_entity->m_atPoint;
    Maths::Vec4 start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kFeelerReach), pos.z, pos.w};
    feeler.ChangeOrigin(&start);
    Maths::Vec4 end = {pos.x, static_cast<float>(static_cast<double>(pos.y) - kFeelerReach), pos.z, pos.w};
    feeler.ChangeFinish(&end);
    if (feeler.GetHitShape(true, false) && feeler.HitObjectAlive())
        reinterpret_cast<Walk::MovableContact*>(mp_ride)->StartRiding(feeler.GetHitObject());
    if (At<int>(At<uint8_t*>(mp_core, kTimedFlags), 0x18) == 4) {
        ChangePhase(mp_core, 1);
        m_entity->PostOwnEvent(kUnderTheFloor, nullptr);
    } else if (m_entity->m_atPoint == frameStart) {
        m_entity->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    feeler.m_entity.Release();
}

// 0x004F82F0
void CRiderPhysicsTweaks::UpdateBrightness(Engine3D::CProbeRay* ground)
{
    bool hit = ground->GetHitShape(false, false) != 0;
    Engine3D::CProbeRay feeler;
    feeler.CopyConstruct(*ground);
    feeler.SetExcluded(0, 0x20);
    feeler.m_line.start.y = static_cast<float>(static_cast<double>(feeler.m_line.start.y) + kBrightnessAbove);
    if (hit)
        feeler.m_line.end = {ground->m_contactPos.x, static_cast<float>(static_cast<double>(ground->m_contactPos.y) - 1.0f),
                             ground->m_contactPos.z, ground->m_contactPos.w};
    else
        feeler.m_line.end = feeler.m_line.start;
    if (feeler.GetHitShape(false, false))
        m_meshInstance->ApplyBrightness(&feeler, true, 1.0f);
    else if (hit)
        m_meshInstance->ApplyBrightness(ground, true, 1.0f);
    feeler.m_entity.Release();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderPhysicsTweaks::Teardown,
         &GameObjects::CRiderPhysicsTweaks::FrameUpdate,
         Stubs::IgnoreArgument,
         &GameObjects::CRiderPhysicsTweaks::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
