// The node array, object hook manager and rail manager components (node_components.h).

#include "game/obj/node_components.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/node_objects.h"
#include "game/obj/rail_manager.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/net/buddy_status.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

static_assert(sizeof(CClassNodeList) == 0x4C && sizeof(CGrindRailHub) == 0x54, "the sizes allocated here");

namespace {

extern void** const kNodeArrayVtable;
extern void** const kHookManagerVtable;
extern void** const kRailManagerVtable;

constexpr uint32_t kModel = 0x286A8D26; // model

// The node array of an object's node array component (its vtable slot 13).
Scripts::CArray* NodeArrayOf(CObjectModule* component)
{
    return reinterpret_cast<Scripts::CArray*(__thiscall*)(CObjectModule*)>(component->m_vtable[13])(component);
}

} // namespace

// --- the node array ---

// 0x00428010
CObjectModule* __cdecl CWaypointListModule::Create()
{
    auto* component = static_cast<CWaypointListModule*>(MemSys::AllocZeroed(sizeof(CWaypointListModule)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_vtable = kNodeArrayVtable;
    component->m_type = kType;
    component->m_nodes = nullptr;
    return component;
}

// 0x00427EA0
void CWaypointListModule::DestroyBody()
{
    m_vtable = kNodeArrayVtable;
    CObjectModule::DestroyBody();
}

// 0x00427EF0
CWaypointListModule* CWaypointListModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00427F20
void CWaypointListModule::ConfigureFromData(Scripts::CStruct* params)
{
    const char* model = nullptr;
    if (!params->GetString(kModel, &model, 0))
        return;
    char path[0x100];
    std::snprintf(path, sizeof(path), "models\\%s", model); // (the original: strcpy / strcat, unbounded)
    if (char* dot = std::strstr(path, "."))
        std::memcpy(dot, ".qb", 4);
    else
        std::snprintf(path + std::strlen(path), sizeof(path) - std::strlen(path), ".qb");
    m_nodes = static_cast<Scripts::CArray*>(
        AssetStore::AssetManager()->FetchOrLoadResource(path, 0, 0, 0, false, false, false, nullptr, 0));
}

// 0x00427EB0
void CWaypointListModule::ReapplyFromData(Scripts::CStruct* params)
{
    const char* model = nullptr;
    if (params->GetString(kModel, &model, 0))
        CObjectModule::ConfigureFromData(params);
}

// --- the object hooks ---

// 0x00428130
CObjectModule* __cdecl CHookPointRegistryModule::Create()
{
    auto* component = static_cast<CHookPointRegistryModule*>(MemSys::AllocZeroed(sizeof(CHookPointRegistryModule)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_vtable = kHookManagerVtable;
    component->m_type = kType;
    component->mp_hooks = nullptr;
    return component;
}

// 0x004281B0
void CHookPointRegistryModule::DestroyBody()
{
    m_vtable = kHookManagerVtable;
    if (CClassNodeList* hooks = mp_hooks) {
        hooks->Clear(); // (its destructor)
        MemSys::Manager::Get()->Free(hooks);
    }
    CObjectModule::DestroyBody();
}

// 0x00428220
CHookPointRegistryModule* CHookPointRegistryModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00428090
void CHookPointRegistryModule::ConfigureFromData(Scripts::CStruct*)
{
    CObjectModule* nodeArray = m_object->GetComponent(CWaypointListModule::kType);
    if (CClassNodeList* hooks = mp_hooks) {
        hooks->Clear();
        MemSys::Manager::Get()->Free(hooks);
    }
    void* memory = MemSys::AllocZeroed(sizeof(CClassNodeList));
    mp_hooks = memory ? static_cast<CClassNodeList*>(memory)->Init() : nullptr;
    mp_hooks->SetNodes(NodeArrayOf(nodeArray));
}

// --- the rails ---

// 0x0042B920
CObjectModule* __cdecl CGrindPathLinkModule::Create()
{
    auto* component = static_cast<CGrindPathLinkModule*>(MemSys::AllocZeroed(sizeof(CGrindPathLinkModule)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_vtable = kRailManagerVtable;
    component->m_type = kType;
    component->m_railGroups = nullptr;
    return component;
}

// 0x0042B9A0
void CGrindPathLinkModule::DestroyBody()
{
    m_vtable = kRailManagerVtable;
    if (CGrindRailHub* rails = m_railGroups) {
        rails->Cleanup(); // (its destructor)
        MemSys::Manager::Get()->Free(rails);
    }
    CObjectModule::DestroyBody();
}

// 0x0042BA90
CGrindPathLinkModule* CGrindPathLinkModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0042B880
void CGrindPathLinkModule::ConfigureFromData(Scripts::CStruct*)
{
    CObjectModule* nodeArray = m_object->GetComponent(CWaypointListModule::kType);
    if (CGrindRailHub* rails = m_railGroups) {
        rails->Cleanup();
        MemSys::Manager::Get()->Free(rails);
    }
    void* memory = MemSys::AllocZeroed(sizeof(CGrindRailHub));
    m_railGroups = memory ? static_cast<CGrindRailHub*>(memory)->Init() : nullptr;
    m_railGroups->AddFromNodeArray(NodeArrayOf(nodeArray));
}

// 0x0042BA10
Maths::Matrix* CGrindPathLinkModule::UpdateRailMatrix(Maths::Matrix* out)
{
    Maths::Matrix placement;
    placement = m_object->m_basisFrame;
    placement.row[0].w = 0.0f;
    placement.row[1].w = 0.0f;
    placement.row[2].w = 0.0f;
    const Maths::Vector& pos = m_object->m_pos;
    placement.row[3] = {pos.x, pos.y, pos.z, 1.0f};
    m_railGroups->SetObjectMatrix(placement);
    *out = placement;
    return out;
}

} // namespace Obj

using GameObjects::CWaypointListModule;
using GameObjects::CHookPointRegistryModule;
using GameObjects::CGrindPathLinkModule;

namespace GameObjects {
namespace {

static VtableSlot kNodeArrayVtable_slots[] = {&CWaypointListModule::Destroy,
         &GameObjects::ComponentWithoutUpdate::Update,
         &CWaypointListModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         &CWaypointListModule::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName,
         &OnlinePlay::FoldedGetters::Word18};
void** const kNodeArrayVtable = reinterpret_cast<void**>(kNodeArrayVtable_slots);
static VtableSlot kHookManagerVtable_slots[] = {&CHookPointRegistryModule::Destroy,
         &GameObjects::ComponentWithoutUpdate::Update,
         &CHookPointRegistryModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName,
         &OnlinePlay::FoldedGetters::Word18};
void** const kHookManagerVtable = reinterpret_cast<void**>(kHookManagerVtable_slots);
static VtableSlot kRailManagerVtable_slots[] = {&CGrindPathLinkModule::Destroy,
         &GameObjects::ComponentWithoutUpdate::Update,
         &CGrindPathLinkModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName,
         &OnlinePlay::FoldedGetters::Word18};
void** const kRailManagerVtable = reinterpret_cast<void**>(kRailManagerVtable_slots);

} // namespace
} // namespace Obj
