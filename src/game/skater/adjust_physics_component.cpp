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
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064B964);

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

void SetState(void* core, int state) { GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(core, state); }

// The debug message's strings as the original passes them ("M:%s:%i: %s\n", the source file, "found to be inside a
// moving object").
const char* GameString(uint32_t address) { return reinterpret_cast<const char*>(address); }

} // namespace

// 0x004F80E0
CBaseComponent* __cdecl CSkaterAdjustPhysicsComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterAdjustPhysicsComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterAdjustPhysicsComponent*>(memory);
    component->CBaseComponent::Construct();
    component->mp_core = nullptr;
    component->mp_model = nullptr;
    component->mp_shadow = nullptr;
    component->mp_ride = nullptr;
    component->m_uberFrigged = 0;
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x004F8040
void CSkaterAdjustPhysicsComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x004F8050
CSkaterAdjustPhysicsComponent* CSkaterAdjustPhysicsComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004F8080
void CSkaterAdjustPhysicsComponent::Finalize()
{
    mp_core = m_object->GetComponent(kSkaterCorePhysics);
    mp_model = reinterpret_cast<CModelComponent*>(m_object->GetComponent(kModel));
    mp_shadow = reinterpret_cast<CShadowComponent*>(m_object->GetComponent(kShadow));
    mp_ride = reinterpret_cast<CMovableContactComponent*>(m_object->GetComponent(kMovableContact));
    mp_state = reinterpret_cast<CSkaterStateComponent*>(m_object->GetComponent(kSkaterState));
}

// 0x004F8C40
void CSkaterAdjustPhysicsComponent::FrameUpdate()
{
    uint8_t* flags = At<uint8_t*>(mp_core, kTimedFlags);
    if (At<int>(flags, 0x18) == 0 || m_object->m_vel.y < 0.0f) {
        if (flags[0x60] != 1) {
            flags[0x60] = 1;
            At<int>(flags, 0x64) = Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60);
        }
    }
    if (!At<uint8_t*>(mp_core, kTimedFlags)[0x88])
        CheckInsideMovingObject();
    Update();
    m_object->m_oldPos = m_object->m_pos;
}

// 0x004F8AA0
void CSkaterAdjustPhysicsComponent::CheckInsideMovingObject()
{
    Mth::Vector& frameStart = At<Mth::Vector>(mp_core, kFrameStart);
    if (!mp_ride->FindContactAhead(&m_object->m_pos, &frameStart))
        return;
    Dbg::Printf(GameString(0x006466B0), GameString(0x0064B9E0), 0x182, GameString(0x0064BA0C));
    Nx::CFeeler feeler;
    feeler.Construct();
    const Mth::Vector& pos = m_object->m_pos;
    Mth::Vector start = {pos.x, static_cast<float>(static_cast<double>(pos.y) + kFeelerReach), pos.z, pos.w};
    feeler.SetStart(&start);
    Mth::Vector end = {pos.x, static_cast<float>(static_cast<double>(pos.y) - kFeelerReach), pos.z, pos.w};
    feeler.SetEnd(&end);
    if (feeler.GetCollision(true, false) && feeler.HitObjectAlive())
        reinterpret_cast<Walk::MovableContact*>(mp_ride)->StartRiding(feeler.GetHitObject());
    if (At<int>(At<uint8_t*>(mp_core, kTimedFlags), 0x18) == 4) {
        SetState(mp_core, 1);
        m_object->SelfEvent(kUnderTheFloor, nullptr);
    } else if (m_object->m_pos == frameStart) {
        m_object->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    feeler.m_object.Release();
}

// 0x004F82F0
void CSkaterAdjustPhysicsComponent::UpdateBrightness(Nx::CFeeler* ground)
{
    bool hit = ground->GetCollision(false, false) != 0;
    Nx::CFeeler feeler;
    feeler.CopyConstruct(*ground);
    feeler.SetIgnore(0, 0x20);
    feeler.m_line.start.y = static_cast<float>(static_cast<double>(feeler.m_line.start.y) + kBrightnessAbove);
    if (hit)
        feeler.m_line.end = {ground->m_point.x, static_cast<float>(static_cast<double>(ground->m_point.y) - 1.0f),
                             ground->m_point.z, ground->m_point.w};
    else
        feeler.m_line.end = feeler.m_line.start;
    if (feeler.GetCollision(false, false))
        mp_model->ApplyBrightness(&feeler, true, 1.0f);
    else if (hit)
        mp_model->ApplyBrightness(ground, true, 1.0f);
    feeler.m_object.Release();
}

} // namespace Obj

REPLACE(0x004F80E0, Obj::CSkaterAdjustPhysicsComponent::Create);
REPLACE(0x004F8040, &Obj::CSkaterAdjustPhysicsComponent::DestroyBody);
REPLACE(0x004F8050, &Obj::CSkaterAdjustPhysicsComponent::Destroy);
REPLACE(0x004F8080, &Obj::CSkaterAdjustPhysicsComponent::Finalize);
REPLACE(0x004F8C40, &Obj::CSkaterAdjustPhysicsComponent::FrameUpdate);
REPLACE(0x004F8AA0, &Obj::CSkaterAdjustPhysicsComponent::CheckInsideMovingObject);
REPLACE(0x004F82F0, &Obj::CSkaterAdjustPhysicsComponent::UpdateBrightness);
