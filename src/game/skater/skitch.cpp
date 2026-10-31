// The skitch component: skitching allowed or not, and a car's hook points. See skitch.h.

#include "game/skater/skitch.h"

#include <bit>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/node_objects.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

namespace {

extern void** const kVtable;
constexpr uint32_t kObjectHookManager = 0x27443708;
constexpr uint32_t kAllowSkitching = 0xC38CCE3B; // Obj_AllowSkitching
const char* const kOff = "off"; // "off"

// The hook manager's node list (its vtable +0x34), or null.
CClassNodeList* HookNodes(CBaseComponent* hooks)
{
    return reinterpret_cast<CClassNodeList*(__thiscall*)(CBaseComponent*)>(hooks->m_vtable[13])(hooks);
}

// The nodes' transform: the object's orientation (the w column cleared) at its position.
void PlaceNodes(CClassNodeList* nodes, CCompositeObject* object)
{
    Mth::Matrix transform;
    transform = object->m_matrix;
    transform.row[3].x = object->m_pos.x;
    transform.row[3].z = object->m_pos.z;
    transform.row[0].w = 0.0f;
    transform.row[1].w = 0.0f;
    transform.row[2].w = 0.0f;
    transform.row[3].y = object->m_pos.y;
    transform.row[3].w = 1.0f;
    nodes->SetTransform(&transform);
}

} // namespace

// 0x0051B970
void ComponentWithoutUpdate::Update() { VirtualSuspend(true); }

// 0x0051BA10
CBaseComponent* __cdecl CSkitchComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkitchComponent));
    if (!memory)
        return nullptr;
    auto* skitch = static_cast<CSkitchComponent*>(memory);
    skitch->CBaseComponent::Construct();
    skitch->m_vtable = kVtable;
    skitch->m_type = kType;
    skitch->m_allowed = true;
    return skitch;
}

// 0x0051B960
void CSkitchComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0051B980
CSkitchComponent* CSkitchComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0051B9B0
void CSkitchComponent::InitFromStructure(Script::CStruct*) { m_hooks = m_object->GetComponent(kObjectHookManager); }

// 0x0051B9D0
int CSkitchComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    if (name != kAllowSkitching)
        return 2;
    m_allowed = !params->ContainsFlag(kOff);
    return 1;
}

// 0x0051BA70
int CSkitchComponent::GetNearestHookPoint(Mth::Vector* out, const Mth::Vector* target)
{
    int nearest = -1;
    CCompositeObject* object = m_object;
    if (!m_hooks || !object)
        return nearest;
    CClassNodeList* nodes = HookNodes(m_hooks);
    if (!nodes)
        return nearest;
    PlaceNodes(nodes, object);
    float nearestDistance = std::bit_cast<float>(0x56B5E621u); // (far enough)
    Mth::Vector nearestPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    int index = 0;
    for (CClassNodeList::Entry* node = nodes->m_entries; node; node = node->next, ++index) {
        Mth::Vector point;
        nodes->Transform(&point, &node->pos);
        const Mth::Vector& pos = object->m_pos;
        point.x -= pos.x;
        point.y -= pos.y;
        point.z -= pos.z;
        point.w -= pos.w;
        point.ProjectToPlane(object->m_matrix.row[1]);
        point.x += pos.x;
        point.y += pos.y;
        point.z += pos.z;
        point.w += pos.w;
        double dx = static_cast<double>(point.x) - target->x;
        double dy = static_cast<double>(point.y) - target->y;
        double dz = static_cast<double>(point.z) - target->z;
        double distance = (dz * dz + dy * dy) + dx * dx;
        if (distance < nearestDistance) {
            nearestDistance = static_cast<float>(distance);
            nearestPoint = point;
            nearest = index;
        }
    }
    *out = nearestPoint;
    return nearest;
}

// 0x0051BC70
bool CSkitchComponent::GetHookPoint(Mth::Vector* out, int index)
{
    CCompositeObject* object = m_object;
    if (!m_hooks || !object)
        return false;
    CClassNodeList* nodes = HookNodes(m_hooks);
    if (!nodes)
        return false;
    PlaceNodes(nodes, object);
    int i = 0;
    for (CClassNodeList::Entry* node = nodes->m_entries; node; node = node->next, ++i) {
        if (i != index)
            continue;
        Mth::Vector point;
        nodes->Transform(&point, &node->pos);
        point -= object->m_pos;
        point.ProjectToPlane(object->m_matrix.row[1]);
        point += object->m_pos;
        *out = point;
        return true;
    }
    return true;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CSkitchComponent::Destroy,
         &Obj::ComponentWithoutUpdate::Update,
         &Obj::CSkitchComponent::InitFromStructure,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::CSkitchComponent::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
