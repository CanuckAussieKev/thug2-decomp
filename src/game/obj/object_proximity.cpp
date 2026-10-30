// The object proximity component (Obj::CObjectProximityComponent): see object_proximity.h. From the disassembly of
// 0x004EC220-0x004EC557 (the member functions 0x004EC250-0x004EC364 and 0x004EC480 aren't Ghidra functions).

#include "game/obj/object_proximity.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(uint32_t[42], g_objectProximityMemberFunctions, 0x00682620,
            0x82A0A873, 0x1D5B1AE6, 0, 0, 0, 0,
            0x8CB5D0C0, 0x1D5B1AE6, 0, 0, 0, 0,
            0x50F28D36, 0x1D5B1AE6, 0, 0, 0, 0,
            0x2D703B94, 0x1D5B1AE6, 0, 0, 0, 0,
            0x13C0D8DC, 0x1D5B1AE6, 0, 0, 0, 0,
            0x9522F906, 0x1D5B1AE6, 0, 0, 0, 0,
            0, 0x1D5B1AE6, 0, 0, 0, 0);

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064B6C4);
constexpr uint32_t kObjectProximity = 0x1D5B1AE6;
constexpr float kFeetToInches = 12.0f; // 0x0064546C

// 0x0064B6F8 (bits 0x55CE224C, about 2.8e13): "no skater".
float FarAway()
{
    uint32_t bits = 0x55CE224C;
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// The unnamed float, if given, as (12 x it) squared (rounded once, as the original's x87 product).
bool SetSquaredRadius(Script::CStruct* params, float& radius)
{
    if (params->GetFloat(0u, &radius, 0)) {
        double inches = static_cast<double>(radius) * kFeetToInches;
        radius = static_cast<float>(inches * inches);
    }
    return true;
}

double DistanceSquared(const CCompositeObject* a, const CCompositeObject* b)
{
    double dx = static_cast<double>(a->m_pos.x) - b->m_pos.x;
    double dy = static_cast<double>(a->m_pos.y) - b->m_pos.y;
    double dz = static_cast<double>(a->m_pos.z) - b->m_pos.z;
    return (dx * dx + dz * dz) + dy * dy;
}

} // namespace

// 0x004EC420
CBaseComponent* __cdecl CObjectProximityComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CObjectProximityComponent));
    return memory ? static_cast<CObjectProximityComponent*>(memory)->Construct() : nullptr;
}

// 0x004EC220
void __cdecl CObjectProximityComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kObjectProximity), g_objectProximityMemberFunctions,
                                        0x18);
}

// 0x004EC370
CObjectProximityComponent* CObjectProximityComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kObjectProximity;
    return this;
}

// 0x004EC240
void CObjectProximityComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x004EC390
CObjectProximityComponent* CObjectProximityComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004EC3C0
double CObjectProximityComponent::LocalSkaterDistanceSquared(uint32_t* skaterId)
{
    auto* skater = reinterpret_cast<CCompositeObject*>(Mdl::Skate::Instance()->GetLocalSkater());
    if (!skater || skater == m_object)
        return FarAway();
    *skaterId = skater->m_id;
    return DistanceSquared(skater, m_object);
}

// 0x004EC4B0
float CObjectProximityComponent::NearestSkaterDistanceSquared(uint32_t* skaterId)
{
    float nearest = FarAway();
    Mdl::Skate* skate = Mdl::Skate::Instance();
    int count = skate->GetNumSkaters();
    for (int i = 0; i < count; ++i) {
        CCompositeObject* skater = Mdl::Skate::Instance()->GetSkater(i);
        if (!skater || skater == m_object)
            continue;
        double distance = DistanceSquared(skater, m_object);
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            *skaterId = skater->m_id;
        }
    }
    return nearest;
}

// 0x004EC560
void CObjectProximityComponent::Update()
{
    // (the skater's id: the original's is a stack slot only written when a skater is found - left as it was
    // otherwise; ours starts at 0)
    uint32_t id = 0;
    float distance = 0.0f;
    bool dead;
    auto event = [this](uint32_t type, Script::CStruct* data) { m_object->SelfEvent(type, data); };
    auto newData = []() {
        Script::CStruct* data = Script::NewStruct();
        return data ? data->Init() : nullptr;
    };
    auto deleteData = [](Script::CStruct* data) {
        if (data) {
            data->Clear();
            Script::DeleteStruct(data);
        }
    };
    if (m_innerRadiusSquared != 0.0f) {
        double local = LocalSkaterDistanceSquared(&id);
        distance = static_cast<float>(local);
        if (local <= m_innerRadiusSquared) {
            // The local skater inside: the enter event, and the any-skater one.
            Script::CStruct* data = newData();
            data->AddChecksum(0xF79C90AE, id);
            data->AddInteger(0xDB1E28CD, id);
            event(0x7048029A, data);
            if (!(m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                event(0xF5B2A042, data);
            deleteData(data);
        } else {
            // Another skater inside: the any-skater event.
            if (NearestSkaterDistanceSquared(&id) <= m_innerRadiusSquared) {
                Script::CStruct* data = newData();
                data->AddChecksum(0xF79C90AE, id);
                data->AddInteger(0xDB1E28CD, id);
                event(0xF5B2A042, data);
                deleteData(data);
            }
            distance = 0.0f;
        }
    }
    if (m_outerRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = NearestSkaterDistanceSquared(&id);
        if (distance >= m_outerRadiusSquared && !(m_object->m_objectFlags & OBJECT_FLAG_DEAD)) {
            Script::CStruct* data = newData();
            data->AddChecksum(0x873A40D0, id);
            data->AddInteger(0x5F3602F5, id);
            event(0x91BCAD1D, data);
            deleteData(data);
        }
    }
    if (m_innerAvoidRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(LocalSkaterDistanceSquared(&id));
        if (distance <= m_innerAvoidRadiusSquared) {
            Script::CStruct* data = newData();
            data->AddChecksum(0xF79C90AE, id);
            if (!(m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                event(0xD69BD3DE, data);
            deleteData(data);
        }
    }
    if (m_outerAvoidRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(LocalSkaterDistanceSquared(&id));
        if (distance >= m_outerAvoidRadiusSquared && !(m_object->m_objectFlags & OBJECT_FLAG_DEAD))
            event(0x35FDAFAE, nullptr);
    }
    if (m_attentionRadiusSquared != 0.0f) {
        if (distance == 0.0f)
            distance = static_cast<float>(LocalSkaterDistanceSquared(&id));
        dead = (m_object->m_objectFlags & OBJECT_FLAG_DEAD) != 0;
        if (!dead)
            event(distance <= m_attentionRadiusSquared ? 0xA06C9031 : 0x9C1CDE7A, nullptr);
    }
}

// 0x004EC250
bool CObjectProximityComponent::Obj_SetInnerRadius(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_innerRadiusSquared);
}

// 0x004EC280
bool CObjectProximityComponent::Obj_SetOuterRadius(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_outerRadiusSquared);
}

// 0x004EC2B0
bool CObjectProximityComponent::Obj_SetInnerAvoidRadius(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_innerAvoidRadiusSquared);
}

// 0x004EC2E0
bool CObjectProximityComponent::Obj_SetOuterAvoidRadius(Script::CStruct* params, Script::CScript* /*script*/)
{
    return SetSquaredRadius(params, m_outerAvoidRadiusSquared);
}

// 0x004EC310
bool CObjectProximityComponent::Obj_SetAttentionRadius(Script::CStruct* params, Script::CScript* /*script*/)
{
    // (with the flag, only a radius still 0 is set; not a number counts as set)
    if (params->ContainsFlag(0xFD2EACAD) && !(m_attentionRadiusSquared == 0.0))
        return true;
    return SetSquaredRadius(params, m_attentionRadiusSquared);
}

// 0x004EC480
bool CObjectProximityComponent::Obj_ObjectInAttentionRadius(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    uint32_t skater;
    return LocalSkaterDistanceSquared(&skater) <= m_attentionRadiusSquared;
}

} // namespace Obj

REPLACE(0x004EC420, Obj::CObjectProximityComponent::Create);
REPLACE(0x004EC220, Obj::CObjectProximityComponent::RegisterMemberFunctions);
REPLACE(0x004EC370, &Obj::CObjectProximityComponent::Construct);
REPLACE(0x004EC240, &Obj::CObjectProximityComponent::DestroyBody);
REPLACE(0x004EC390, &Obj::CObjectProximityComponent::Destroy);
REPLACE(0x004EC560, &Obj::CObjectProximityComponent::Update);
REPLACE(0x004EC3C0, &Obj::CObjectProximityComponent::LocalSkaterDistanceSquared);
REPLACE(0x004EC4B0, &Obj::CObjectProximityComponent::NearestSkaterDistanceSquared);
REPLACE(0x004EC250, &Obj::CObjectProximityComponent::Obj_SetInnerRadius);
REPLACE(0x004EC280, &Obj::CObjectProximityComponent::Obj_SetOuterRadius);
REPLACE(0x004EC2B0, &Obj::CObjectProximityComponent::Obj_SetInnerAvoidRadius);
REPLACE(0x004EC2E0, &Obj::CObjectProximityComponent::Obj_SetOuterAvoidRadius);
REPLACE(0x004EC310, &Obj::CObjectProximityComponent::Obj_SetAttentionRadius);
REPLACE(0x004EC480, &Obj::CObjectProximityComponent::Obj_ObjectInAttentionRadius);
