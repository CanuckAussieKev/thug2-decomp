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

namespace Obj {

static_assert(sizeof(CClassNodeList) == 0x4C && sizeof(CRailManager) == 0x54, "the sizes allocated here");

namespace {

extern void** const kNodeArrayVtable;
extern void** const kHookManagerVtable;
extern void** const kRailManagerVtable;

constexpr uint32_t kModel = 0x286A8D26; // model

// The node array of an object's node array component (its vtable slot 13).
Script::CArray* NodeArrayOf(CBaseComponent* component)
{
    return reinterpret_cast<Script::CArray*(__thiscall*)(CBaseComponent*)>(component->m_vtable[13])(component);
}

} // namespace

// --- the node array ---

// 0x00428010
CBaseComponent* __cdecl CNodeArrayComponent::Create()
{
    auto* component = static_cast<CNodeArrayComponent*>(Mem::Calloc(sizeof(CNodeArrayComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kNodeArrayVtable;
    component->m_type = kType;
    component->m_nodes = nullptr;
    return component;
}

// 0x00427EA0
void CNodeArrayComponent::DestroyBody()
{
    m_vtable = kNodeArrayVtable;
    CBaseComponent::DestroyBody();
}

// 0x00427EF0
CNodeArrayComponent* CNodeArrayComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00427F20
void CNodeArrayComponent::InitFromStructure(Script::CStruct* params)
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
    m_nodes = static_cast<Script::CArray*>(
        Ass::AssetManager()->LoadOrGetAsset(path, 0, 0, 0, false, false, false, nullptr, 0));
}

// 0x00427EB0
void CNodeArrayComponent::RefreshFromStructure(Script::CStruct* params)
{
    const char* model = nullptr;
    if (params->GetString(kModel, &model, 0))
        CBaseComponent::InitFromStructure(params);
}

// --- the object hooks ---

// 0x00428130
CBaseComponent* __cdecl CObjectHookManagerComponent::Create()
{
    auto* component = static_cast<CObjectHookManagerComponent*>(Mem::Calloc(sizeof(CObjectHookManagerComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kHookManagerVtable;
    component->m_type = kType;
    component->mp_hooks = nullptr;
    return component;
}

// 0x004281B0
void CObjectHookManagerComponent::DestroyBody()
{
    m_vtable = kHookManagerVtable;
    if (CClassNodeList* hooks = mp_hooks) {
        hooks->Clear(); // (its destructor)
        Mem::Manager::Get()->Free(hooks);
    }
    CBaseComponent::DestroyBody();
}

// 0x00428220
CObjectHookManagerComponent* CObjectHookManagerComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00428090
void CObjectHookManagerComponent::InitFromStructure(Script::CStruct*)
{
    CBaseComponent* nodeArray = m_object->GetComponent(CNodeArrayComponent::kType);
    if (CClassNodeList* hooks = mp_hooks) {
        hooks->Clear();
        Mem::Manager::Get()->Free(hooks);
    }
    void* memory = Mem::Calloc(sizeof(CClassNodeList));
    mp_hooks = memory ? static_cast<CClassNodeList*>(memory)->Init() : nullptr;
    mp_hooks->SetNodes(NodeArrayOf(nodeArray));
}

// --- the rails ---

// 0x0042B920
CBaseComponent* __cdecl CRailManagerComponent::Create()
{
    auto* component = static_cast<CRailManagerComponent*>(Mem::Calloc(sizeof(CRailManagerComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kRailManagerVtable;
    component->m_type = kType;
    component->mp_rails = nullptr;
    return component;
}

// 0x0042B9A0
void CRailManagerComponent::DestroyBody()
{
    m_vtable = kRailManagerVtable;
    if (CRailManager* rails = mp_rails) {
        rails->Cleanup(); // (its destructor)
        Mem::Manager::Get()->Free(rails);
    }
    CBaseComponent::DestroyBody();
}

// 0x0042BA90
CRailManagerComponent* CRailManagerComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0042B880
void CRailManagerComponent::InitFromStructure(Script::CStruct*)
{
    CBaseComponent* nodeArray = m_object->GetComponent(CNodeArrayComponent::kType);
    if (CRailManager* rails = mp_rails) {
        rails->Cleanup();
        Mem::Manager::Get()->Free(rails);
    }
    void* memory = Mem::Calloc(sizeof(CRailManager));
    mp_rails = memory ? static_cast<CRailManager*>(memory)->Init() : nullptr;
    mp_rails->AddFromNodeArray(NodeArrayOf(nodeArray));
}

// 0x0042BA10
Mth::Matrix* CRailManagerComponent::UpdateRailMatrix(Mth::Matrix* out)
{
    Mth::Matrix placement;
    placement = m_object->m_matrix;
    placement.row[0].w = 0.0f;
    placement.row[1].w = 0.0f;
    placement.row[2].w = 0.0f;
    const Mth::Vector& pos = m_object->m_pos;
    placement.row[3] = {pos.x, pos.y, pos.z, 1.0f};
    mp_rails->SetObjectMatrix(placement);
    *out = placement;
    return out;
}

} // namespace Obj

using Obj::CNodeArrayComponent;
using Obj::CObjectHookManagerComponent;
using Obj::CRailManagerComponent;

namespace Obj {
namespace {

static VtableSlot kNodeArrayVtable_slots[] = {&CNodeArrayComponent::Destroy,
         &Obj::ComponentWithoutUpdate::Update,
         &CNodeArrayComponent::InitFromStructure,
         Stubs::DoNothing,
         Stubs::DoNothing,
         &CNodeArrayComponent::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction,
         &GameNet::FoldedGetters::Word18};
void** const kNodeArrayVtable = reinterpret_cast<void**>(kNodeArrayVtable_slots);
static VtableSlot kHookManagerVtable_slots[] = {&CObjectHookManagerComponent::Destroy,
         &Obj::ComponentWithoutUpdate::Update,
         &CObjectHookManagerComponent::InitFromStructure,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction,
         &GameNet::FoldedGetters::Word18};
void** const kHookManagerVtable = reinterpret_cast<void**>(kHookManagerVtable_slots);
static VtableSlot kRailManagerVtable_slots[] = {&CRailManagerComponent::Destroy,
         &Obj::ComponentWithoutUpdate::Update,
         &CRailManagerComponent::InitFromStructure,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction,
         &GameNet::FoldedGetters::Word18};
void** const kRailManagerVtable = reinterpret_cast<void**>(kRailManagerVtable_slots);

} // namespace
} // namespace Obj
