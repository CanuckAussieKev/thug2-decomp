// The node array, object hook manager and rail manager components (node_components.h).

#include "game/obj/node_components.h"

#include <cstdio>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/node_objects.h"
#include "game/obj/rail_manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

static_assert(sizeof(CClassNodeList) == 0x4C && sizeof(CRailManager) == 0x54, "the sizes allocated here");

namespace {

void** const kNodeArrayVtable = reinterpret_cast<void**>(0x0064628C);
void** const kHookManagerVtable = reinterpret_cast<void**>(0x006462D4);
void** const kRailManagerVtable = reinterpret_cast<void**>(0x0064637C);

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

REPLACE(0x00428010, CNodeArrayComponent::Create);
REPLACE(0x00427EA0, &CNodeArrayComponent::DestroyBody);
REPLACE(0x00427EF0, &CNodeArrayComponent::Destroy);
REPLACE(0x00427F20, &CNodeArrayComponent::InitFromStructure);
REPLACE(0x00427EB0, &CNodeArrayComponent::RefreshFromStructure);
REPLACE(0x00428130, CObjectHookManagerComponent::Create);
REPLACE(0x004281B0, &CObjectHookManagerComponent::DestroyBody);
REPLACE(0x00428220, &CObjectHookManagerComponent::Destroy);
REPLACE(0x00428090, &CObjectHookManagerComponent::InitFromStructure);
REPLACE(0x0042B920, CRailManagerComponent::Create);
REPLACE(0x0042B9A0, &CRailManagerComponent::DestroyBody);
REPLACE(0x0042BA90, &CRailManagerComponent::Destroy);
REPLACE(0x0042B880, &CRailManagerComponent::InitFromStructure);
REPLACE(0x0042BA10, &CRailManagerComponent::UpdateRailMatrix);
