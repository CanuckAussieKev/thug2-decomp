// The skitch component: skitching allowed or not, and a car's hook points. See skitch.h.

#include "game/skater/skitch.h"

#include <bit>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/mat4.h"
#include "game/mth/vec4.h"
#include "game/obj/node_objects.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kObjectHookManager = 0x27443708;
constexpr uint32_t kAllowSkitching = 0xC38CCE3B; // Obj_AllowSkitching
const char* const kOff = "off"; // "off"

// The hook manager's node list (its vtable +0x34), or null.
CClassNodeList* HookNodes(CObjectModule* hooks)
{
    return reinterpret_cast<CClassNodeList*(__thiscall*)(CObjectModule*)>(hooks->m_vtable[13])(hooks);
}

// The nodes' transform: the object's orientation (the w column cleared) at its position.
void PlaceNodes(CClassNodeList* nodes, CGameEntity* object)
{
    Maths::Mat4 transform;
    transform = object->m_basisFrame;
    transform.row[3].x = object->m_atPoint.x;
    transform.row[3].z = object->m_atPoint.z;
    transform.row[0].w = 0.0f;
    transform.row[1].w = 0.0f;
    transform.row[2].w = 0.0f;
    transform.row[3].y = object->m_atPoint.y;
    transform.row[3].w = 1.0f;
    nodes->SetTransform(&transform);
}

} // namespace

// 0x0051B970
void ComponentWithoutUpdate::TickFrame() { VirtualSuspend(true); }

// 0x0051BA10
CObjectModule* __cdecl CTowRideHandler::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CTowRideHandler));
    if (!memory)
        return nullptr;
    auto* skitch = static_cast<CTowRideHandler*>(memory);
    skitch->CObjectModule::MakeNew();
    skitch->m_vtable = kVtable;
    skitch->m_kindTag = kType;
    skitch->m_allowed = true;
    return skitch;
}

// 0x0051B960
void CTowRideHandler::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0051B980
CTowRideHandler* CTowRideHandler::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0051B9B0
void CTowRideHandler::ConfigureFromData(Scripts::CStruct*) { m_hooks = m_entity->FetchModule(kObjectHookManager); }

// 0x0051B9D0
int CTowRideHandler::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    if (name != kAllowSkitching)
        return 2;
    m_allowed = !params->HasBareFlag(kOff);
    return 1;
}

// 0x0051BA70
int CTowRideHandler::GetNearestHookPoint(Maths::Vec4* out, const Maths::Vec4* target)
{
    int nearest = -1;
    CGameEntity* object = m_entity;
    if (!m_hooks || !object)
        return nearest;
    CClassNodeList* nodes = HookNodes(m_hooks);
    if (!nodes)
        return nearest;
    PlaceNodes(nodes, object);
    float nearestDistance = std::bit_cast<float>(0x56B5E621u); // (far enough)
    Maths::Vec4 nearestPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    int index = 0;
    for (CClassNodeList::TableRow* node = nodes->m_entries; node; node = node->next, ++index) {
        Maths::Vec4 point;
        nodes->Xform(&point, &node->pos);
        const Maths::Vec4& pos = object->m_atPoint;
        point.x -= pos.x;
        point.y -= pos.y;
        point.z -= pos.z;
        point.w -= pos.w;
        point.FlattenOntoPlane(object->m_basisFrame.row[1]);
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
bool CTowRideHandler::GetHookPoint(Maths::Vec4* out, int index)
{
    CGameEntity* object = m_entity;
    if (!m_hooks || !object)
        return false;
    CClassNodeList* nodes = HookNodes(m_hooks);
    if (!nodes)
        return false;
    PlaceNodes(nodes, object);
    int i = 0;
    for (CClassNodeList::TableRow* node = nodes->m_entries; node; node = node->next, ++i) {
        if (i != index)
            continue;
        Maths::Vec4 point;
        nodes->Xform(&point, &node->pos);
        point -= object->m_atPoint;
        point.FlattenOntoPlane(object->m_basisFrame.row[1]);
        point += object->m_atPoint;
        *out = point;
        return true;
    }
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CTowRideHandler::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CTowRideHandler::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CTowRideHandler::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
