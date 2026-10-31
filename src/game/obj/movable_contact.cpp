// The movable contact component (GameObjects::CMovingSurfaceModule, movable_contact.h): riding moving objects.

#include "game/obj/movable_contact.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/nx/feeler.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr float kFeelerTop = 400.0f;   // 0x00646288
constexpr float kFeelerBottom = 6.0f;  // 0x00646284
constexpr float kNudge = 0.1f;         // 0x00645D74 (as the float it is)

float Sum(float a, double b) { return static_cast<float>(static_cast<double>(a) + b); }

} // namespace

// 0x00427AB0
CObjectModule* __cdecl CMovingSurfaceModule::Fabricate()
{
    auto* contact = static_cast<CMovingSurfaceModule*>(MemSys::AllocZeroed(sizeof(CMovingSurfaceModule)));
    if (!contact)
        return nullptr;
    contact->CObjectModule::MakeNew();
    contact->m_vtable = kVtable;
    contact->m_kindTag = kType;
    contact->mp_ride = nullptr;
    contact->m_stopTime = 0;
    return contact;
}

// 0x004276C0
void CMovingSurfaceModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x004276D0
CMovingSurfaceModule* CMovingSurfaceModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00427700
CGameEntity* CMovingSurfaceModule::FindContact(const Maths::Vec4* point, Maths::Vec4* hit)
{
    Engine3D::CProbeRay down;
    down.MakeNew();
    down.m_line.start = {point->x, Sum(point->y, kFeelerTop), point->z, point->w};
    down.m_line.end = {point->x, Sum(point->y, kFeelerBottom), point->z, point->w};
    Engine3D::CProbeRay up;
    up.MakeNew();
    for (int tries = 1;;) {
        if (!down.GetHitShape(true, false) || !down.HitObjectAlive())
            return nullptr;
        // (nothing between the point and just over the hit: it's what the point is on)
        up.m_line.start = *point;
        up.m_line.end = {down.m_contactPos.x, Sum(down.m_contactPos.y, kNudge), down.m_contactPos.z, down.m_contactPos.w};
        if (!up.GetHitShape(true, false)) {
            *hit = down.m_contactPos;
            return down.GetHitObject();
        }
        down.m_line.start = {down.m_contactPos.x, Sum(down.m_contactPos.y, -static_cast<double>(kNudge)), down.m_contactPos.z,
                             down.m_contactPos.w};
        if (++tries == 10)
            return nullptr;
    }
}

// 0x00427B30
CGameEntity* CMovingSurfaceModule::FindContactAhead(Maths::Vec4* pos, const Maths::Vec4* next)
{
    Maths::Vec4 hit, other;
    CGameEntity* object = FindContact(pos, &hit);
    if (!object)
        return nullptr;
    if (!FindContact(next, &other)) {
        *pos = *next;
        return object;
    }
    // (two frames of the object's velocity ahead: x through the unrounded product, the others rounded first)
    double frame = FrameClock::GetSlomoFrameSeconds();
    const Maths::Vec4& vel = object->m_vel;
    double x = frame * vel.x;
    float y = static_cast<float>(frame * vel.y);
    float z = static_cast<float>(frame * vel.z);
    float w = static_cast<float>(frame * vel.w);
    Maths::Vec4 ahead = {Sum(pos->x, x + x), Sum(pos->y, static_cast<double>(y) + y), Sum(pos->z, static_cast<double>(z) + z),
                         Sum(pos->w, static_cast<double>(w) + w)};
    if (!FindContact(&ahead, &other)) {
        *pos = ahead;
        return object;
    }
    *pos = hit;
    pos->y = Sum(pos->y, 1.0f);
    return object;
}

// 0x00427A00
Maths::Vec4* CMovingSurfaceModule::GetContactPoint(Maths::Vec4* out)
{
    Maths::Vec4 point = m_contactPoint;
    if (mp_lastObject) {
        Maths::Mat4 matrix;
        matrix = mp_lastObject->m_basisFrame;
        Maths::Vec4 turned;
        matrix.RotateVector(turned, point);
        const Maths::Vec4& at = mp_lastObject->m_atPoint;
        point = {turned.x + at.x, turned.y + at.y, turned.z + at.z, turned.w + at.w};
    }
    *out = point;
    return out;
}

// 0x00427C70
void CMovingSurfaceModule::StopRiding()
{
    CRideTracker* ride = mp_ride;
    if (!ride)
        return;
    mp_lastObject = ride->m_entity.m_ownerObject;
    reinterpret_cast<CSharedRef<CSharedBase>*>(&ride->m_entity)->Release();
    MemSys::Hub::Pull()->Dealloc(ride);
    mp_ride = nullptr;
    m_stopTime = FrameClock::GameClockElapsed() + g_clockOffset;
}

// 0x00427CB0
bool CMovingSurfaceModule::IsRiding()
{
    if (!mp_ride)
        return false;
    if (mp_ride->m_entity.m_ownerObject)
        return true;
    StopRiding();
    return false;
}

// 0x00427CD0
bool CMovingSurfaceModule::StartRiding(Engine3D::CProbeRay* feeler)
{
    if (!feeler->HitObjectAlive()) {
        if (mp_ride)
            StopRiding();
        return false;
    }
    if (mp_ride) {
        CGameEntity* riding = mp_ride->m_entity.m_ownerObject;
        if (!riding || riding != feeler->GetHitObject())
            StopRiding();
    }
    if (mp_ride) {
        if (mp_ride->m_entity.m_ownerObject)
            return false; // (already riding it)
        StopRiding();
    }
    if (!MemSys::g_managerReady) // (the memory manager not set up yet)
        MemSys::EnsureManager();
    void* memory = MemSys::Hub::Pull()->Carve(sizeof(CRideTracker), 1, nullptr);
    if (!memory) {
        mp_ride = nullptr;
        return true;
    }
    mp_ride = static_cast<CRideTracker*>(memory)->MakeNew(feeler->GetHitObject());
    return true;
}

// 0x00427DD0
void CMovingSurfaceModule::UpdateContactPoint()
{
    CGameEntity* object;
    if (mp_ride && mp_ride->m_entity.m_ownerObject) {
        object = mp_ride->m_entity.m_ownerObject;
    } else {
        if (mp_ride)
            StopRiding();
        object = mp_lastObject;
        if (!object)
            return;
    }
    Maths::Vec4 at = object->m_atPoint;
    Maths::Mat4 inverse;
    inverse = object->m_basisFrame;
    const Maths::Vec4& pos = m_entity->m_atPoint;
    m_contactPoint = {static_cast<float>(static_cast<double>(pos.x) - at.x),
                      static_cast<float>(static_cast<double>(pos.y) - at.y),
                      static_cast<float>(static_cast<double>(pos.z) - at.z),
                      static_cast<float>(static_cast<double>(pos.w) - at.w)};
    inverse.Upend();
    Maths::Vec4 local;
    inverse.RotateVector(local, m_contactPoint);
    m_contactPoint = local;
}

} // namespace Obj

using GameObjects::CMovingSurfaceModule;

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&CMovingSurfaceModule::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
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
