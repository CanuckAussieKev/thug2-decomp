// The movable contact component (Obj::CMovableContactComponent, movable_contact.h): riding moving objects.

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

namespace Obj {

namespace {

extern void** const kVtable;

constexpr float kFeelerTop = 400.0f;   // 0x00646288
constexpr float kFeelerBottom = 6.0f;  // 0x00646284
constexpr float kNudge = 0.1f;         // 0x00645D74 (as the float it is)

float Sum(float a, double b) { return static_cast<float>(static_cast<double>(a) + b); }

} // namespace

// 0x00427AB0
CBaseComponent* __cdecl CMovableContactComponent::Create()
{
    auto* contact = static_cast<CMovableContactComponent*>(Mem::Calloc(sizeof(CMovableContactComponent)));
    if (!contact)
        return nullptr;
    contact->CBaseComponent::Construct();
    contact->m_vtable = kVtable;
    contact->m_type = kType;
    contact->mp_ride = nullptr;
    contact->m_stopTime = 0;
    return contact;
}

// 0x004276C0
void CMovableContactComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x004276D0
CMovableContactComponent* CMovableContactComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00427700
CCompositeObject* CMovableContactComponent::FindContact(const Mth::Vector* point, Mth::Vector* hit)
{
    Nx::CFeeler down;
    down.Construct();
    down.m_line.start = {point->x, Sum(point->y, kFeelerTop), point->z, point->w};
    down.m_line.end = {point->x, Sum(point->y, kFeelerBottom), point->z, point->w};
    Nx::CFeeler up;
    up.Construct();
    for (int tries = 1;;) {
        if (!down.GetCollision(true, false) || !down.HitObjectAlive())
            return nullptr;
        // (nothing between the point and just over the hit: it's what the point is on)
        up.m_line.start = *point;
        up.m_line.end = {down.m_point.x, Sum(down.m_point.y, kNudge), down.m_point.z, down.m_point.w};
        if (!up.GetCollision(true, false)) {
            *hit = down.m_point;
            return down.GetHitObject();
        }
        down.m_line.start = {down.m_point.x, Sum(down.m_point.y, -static_cast<double>(kNudge)), down.m_point.z,
                             down.m_point.w};
        if (++tries == 10)
            return nullptr;
    }
}

// 0x00427B30
CCompositeObject* CMovableContactComponent::FindContactAhead(Mth::Vector* pos, const Mth::Vector* next)
{
    Mth::Vector hit, other;
    CCompositeObject* object = FindContact(pos, &hit);
    if (!object)
        return nullptr;
    if (!FindContact(next, &other)) {
        *pos = *next;
        return object;
    }
    // (two frames of the object's velocity ahead: x through the unrounded product, the others rounded first)
    double frame = Tmr::GetSlomoFrameSeconds();
    const Mth::Vector& vel = object->m_vel;
    double x = frame * vel.x;
    float y = static_cast<float>(frame * vel.y);
    float z = static_cast<float>(frame * vel.z);
    float w = static_cast<float>(frame * vel.w);
    Mth::Vector ahead = {Sum(pos->x, x + x), Sum(pos->y, static_cast<double>(y) + y), Sum(pos->z, static_cast<double>(z) + z),
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
Mth::Vector* CMovableContactComponent::GetContactPoint(Mth::Vector* out)
{
    Mth::Vector point = m_contactPoint;
    if (mp_lastObject) {
        Mth::Matrix matrix;
        matrix = mp_lastObject->m_matrix;
        Mth::Vector turned;
        matrix.RotateVector(turned, point);
        const Mth::Vector& at = mp_lastObject->m_pos;
        point = {turned.x + at.x, turned.y + at.y, turned.z + at.z, turned.w + at.w};
    }
    *out = point;
    return out;
}

// 0x00427C70
void CMovableContactComponent::StopRiding()
{
    CRideTracker* ride = mp_ride;
    if (!ride)
        return;
    mp_lastObject = ride->m_object.mp_object;
    reinterpret_cast<CSmtPtr<CRefCounted>*>(&ride->m_object)->Release();
    Mem::Manager::Get()->Free(ride);
    mp_ride = nullptr;
    m_stopTime = Tmr::GameClockElapsed() + g_clockOffset;
}

// 0x00427CB0
bool CMovableContactComponent::IsRiding()
{
    if (!mp_ride)
        return false;
    if (mp_ride->m_object.mp_object)
        return true;
    StopRiding();
    return false;
}

// 0x00427CD0
bool CMovableContactComponent::StartRiding(Nx::CFeeler* feeler)
{
    if (!feeler->HitObjectAlive()) {
        if (mp_ride)
            StopRiding();
        return false;
    }
    if (mp_ride) {
        CCompositeObject* riding = mp_ride->m_object.mp_object;
        if (!riding || riding != feeler->GetHitObject())
            StopRiding();
    }
    if (mp_ride) {
        if (mp_ride->m_object.mp_object)
            return false; // (already riding it)
        StopRiding();
    }
    if (!Mem::g_managerReady) // (the memory manager not set up yet)
        Mem::EnsureManager();
    void* memory = Mem::Manager::Get()->Allocate(sizeof(CRideTracker), 1, nullptr);
    if (!memory) {
        mp_ride = nullptr;
        return true;
    }
    mp_ride = static_cast<CRideTracker*>(memory)->Construct(feeler->GetHitObject());
    return true;
}

// 0x00427DD0
void CMovableContactComponent::UpdateContactPoint()
{
    CCompositeObject* object;
    if (mp_ride && mp_ride->m_object.mp_object) {
        object = mp_ride->m_object.mp_object;
    } else {
        if (mp_ride)
            StopRiding();
        object = mp_lastObject;
        if (!object)
            return;
    }
    Mth::Vector at = object->m_pos;
    Mth::Matrix inverse;
    inverse = object->m_matrix;
    const Mth::Vector& pos = m_object->m_pos;
    m_contactPoint = {static_cast<float>(static_cast<double>(pos.x) - at.x),
                      static_cast<float>(static_cast<double>(pos.y) - at.y),
                      static_cast<float>(static_cast<double>(pos.z) - at.z),
                      static_cast<float>(static_cast<double>(pos.w) - at.w)};
    inverse.Invert();
    Mth::Vector local;
    inverse.RotateVector(local, m_contactPoint);
    m_contactPoint = local;
}

} // namespace Obj

using Obj::CMovableContactComponent;

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&CMovableContactComponent::Destroy,
         &Obj::ComponentWithoutUpdate::Update,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
