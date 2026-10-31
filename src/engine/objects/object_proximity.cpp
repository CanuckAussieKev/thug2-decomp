// The object proximity component (GameObjects::CObjectProximityComponent): see object_proximity.h. From the disassembly of
// 0x004EC220-0x004EC557 (the member functions 0x004EC250-0x004EC364 and 0x004EC480 aren't Ghidra functions).

#include "engine/objects/object_proximity.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"

namespace GameObjects {

uint32_t g_objectProximityMemberFunctions[42]{
            0x82A0A873, 0x1D5B1AE6, 0, 0, 0, 0,
            0x8CB5D0C0, 0x1D5B1AE6, 0, 0, 0, 0,
            0x50F28D36, 0x1D5B1AE6, 0, 0, 0, 0,
            0x2D703B94, 0x1D5B1AE6, 0, 0, 0, 0,
            0x13C0D8DC, 0x1D5B1AE6, 0, 0, 0, 0,
            0x9522F906, 0x1D5B1AE6, 0, 0, 0, 0,
            0, 0x1D5B1AE6, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kObjectProximity = 0x1D5B1AE6;
constexpr float kTwelveInchScale = 12.0f; // 0x0064546C

// 0x0064B6F8 (bits 0x55CE224C, about 2.8e13): "no skater".
float FarAway()
{
    uint32_t bits = 0x55CE224C;
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// The unnamed float, if given, as (12 x it) squared (rounded once, as the original's x87 product).
bool SetSquaredRadius(Scripts::CStruct* params, float& radius)
{
    if (params->FetchFloat(0u, &radius, 0)) {
        double inches = static_cast<double>(radius) * kTwelveInchScale;
        radius = static_cast<float>(inches * inches);
    }
    return true;
}

double DistanceSquared(const CGameEntity* a, const CGameEntity* b)
{
    double dx = static_cast<double>(a->m_atPoint.x) - b->m_atPoint.x;
    double dy = static_cast<double>(a->m_atPoint.y) - b->m_atPoint.y;
    double dz = static_cast<double>(a->m_atPoint.z) - b->m_atPoint.z;
    return (dx * dx + dz * dz) + dy * dy;
}

} // namespace

// 0x004EC420
CObjectModule* __cdecl CObjectProximityComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CObjectProximityComponent));
    return memory ? static_cast<CObjectProximityComponent*>(memory)->MakeNew() : nullptr;
}

// 0x004EC220
void __cdecl CObjectProximityComponent::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kObjectProximity), g_objectProximityMemberFunctions,
                                        0x18);
}

// 0x004EC370
CObjectProximityComponent* CObjectProximityComponent::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kObjectProximity;
    return this;
}

// 0x004EC240
void CObjectProximityComponent::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x004EC390
CObjectProximityComponent* CObjectProximityComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004EC3C0
double CObjectProximityComponent::LocalSkaterDistanceSquared(uint32_t* skaterId)
{
    auto* skater = reinterpret_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
    if (!skater || skater == m_entity)
        return FarAway();
    *skaterId = skater->m_ident;
    return DistanceSquared(skater, m_entity);
}

// 0x004EC4B0
float CObjectProximityComponent::NearestSkaterDistanceSquared(uint32_t* skaterId)
{
    float nearest = FarAway();
    GameModules::Skate* skate = GameModules::Skate::Sole();
    int count = skate->CountRiders();
    for (int i = 0; i < count; ++i) {
        CGameEntity* skater = GameModules::Skate::Sole()->GetRiderAt(i);
        if (!skater || skater == m_entity)
            continue;
        double distance = DistanceSquared(skater, m_entity);
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            *skaterId = skater->m_ident;
        }
    }
    return nearest;
}

// 0x004EC560
void CObjectProximityComponent::TickFrame()
{
    // (the skater's id: the original's is a stack slot only written when a skater is found - left as it was
    // otherwise; ours starts at 0)
    uint32_t id = 0;
    float distance = 0.0f;
    bool dead;
    auto event = [this](uint32_t type, Scripts::CStruct* data) { m_entity->PostOwnEvent(type, data); };
    auto newData = []() {
        Scripts::CStruct* data = Scripts::SpawnParamBag();
        return data ? data->PrepareAll() : nullptr;
    };
    auto deleteData = [](Scripts::CStruct* data) {
        if (data) {
            data->Clear();
            Scripts::DeleteStruct(data);
        }
    };
    if (m_innerRadiusSquared != 0.0f) {
        double local = LocalSkaterDistanceSquared(&id);
        distance = static_cast<float>(local);
        if (local <= m_innerRadiusSquared) {
            // The local skater inside: the enter event, and the any-skater one.
            Scripts::CStruct* data = newData();
            data->PutNameKey(0xF79C90AE, id);
            data->PutInt(0xDB1E28CD, id);
            event(0x7048029A, data);
            if (!(m_entity->m_objectFlags & OBJECT_FLAG_DEAD))
                event(0xF5B2A042, data);
            deleteData(data);
        } else {
            // Another skater inside: the any-skater event.
            if (NearestSkaterDistanceSquared(&id) <= m_innerRadiusSquared) {
                Scripts::CStruct* data = newData();
                data->PutNameKey(0xF79C90AE, id);
                data->PutInt(0xDB1E28CD, id);
                event(0xF5B2A042, data);
                deleteData(data);
            }
            distance = 0.0f;
        }
    }
    if (m_outerRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = NearestSkaterDistanceSquared(&id);
        if (distance >= m_outerRadiusSquared && !(m_entity->m_objectFlags & OBJECT_FLAG_DEAD)) {
            Scripts::CStruct* data = newData();
            data->PutNameKey(0x873A40D0, id);
            data->PutInt(0x5F3602F5, id);
            event(0x91BCAD1D, data);
            deleteData(data);
        }
    }
    if (m_innerAvoidRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(LocalSkaterDistanceSquared(&id));
        if (distance <= m_innerAvoidRadiusSquared) {
            Scripts::CStruct* data = newData();
            data->PutNameKey(0xF79C90AE, id);
            if (!(m_entity->m_objectFlags & OBJECT_FLAG_DEAD))
                event(0xD69BD3DE, data);
            deleteData(data);
        }
    }
    if (m_outerAvoidRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(LocalSkaterDistanceSquared(&id));
        if (distance >= m_outerAvoidRadiusSquared && !(m_entity->m_objectFlags & OBJECT_FLAG_DEAD))
            event(0x35FDAFAE, nullptr);
    }
    if (m_attentionRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(LocalSkaterDistanceSquared(&id));
        dead = (m_entity->m_objectFlags & OBJECT_FLAG_DEAD) != 0;
        if (!dead)
            event(distance <= m_attentionRadiusSquared ? 0xA06C9031 : 0x9C1CDE7A, nullptr);
    }
}

// 0x004EC250
bool CObjectProximityComponent::Obj_SetInnerRadius(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_innerRadiusSquared);
}

// 0x004EC280
bool CObjectProximityComponent::Obj_SetOuterRadius(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_outerRadiusSquared);
}

// 0x004EC2B0
bool CObjectProximityComponent::Obj_SetInnerAvoidRadius(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_innerAvoidRadiusSquared);
}

// 0x004EC2E0
bool CObjectProximityComponent::Obj_SetOuterAvoidRadius(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_outerAvoidRadiusSquared);
}

// 0x004EC310
bool CObjectProximityComponent::Obj_SetAttentionRadius(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    // (with the flag, only a radius still 0 is set; not a number counts as set)
    if (params->HasBareFlag(0xFD2EACAD) && !(m_attentionRadiusSquared == 0.0))
        return true;
    return SetSquaredRadius(params, m_attentionRadiusSquared);
}

// 0x004EC480
bool CObjectProximityComponent::Obj_ObjectInAttentionRadius(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    uint32_t skater;
    return LocalSkaterDistanceSquared(&skater) <= m_attentionRadiusSquared;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CObjectProximityComponent::Teardown,
         &GameObjects::CObjectProximityComponent::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
