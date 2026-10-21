// The texture projection component (game/obj/texture_projection_component.h).

#include "game/obj/texture_projection_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/movie_manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646604);
void** const kHeadVtable = reinterpret_cast<void**>(0x00646638);     // the head as made
void** const kDeadHeadVtable = reinterpret_cast<void**>(0x00646600); // the head's destructor's

constexpr uint32_t kTextureProjections = 0xFF7E0DAD; // texture_projections

// A projection (the renderer's, 0xC8 bytes; a list node with its checksum at +0x14): made (0x004C7E80), set up from
// its structure (0x004A04C0) and updated from an object (0x0049FFE0).
Mem::ListNode* NewProjection()
{
    void* memory = Mem::Calloc(0xC8);
    return memory ? GameFunction<Mem::ListNode*(__thiscall*)(void*)>(0x004C7E80)(memory) : nullptr;
}

bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

} // namespace

// 0x00434870
CBaseComponent* __cdecl CTextureProjectionComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CTextureProjectionComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CTextureProjectionComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    Mem::ListNode& head = component->m_projections.m_node;
    head.priority = 0;
    head.data = reinterpret_cast<void*>(-1);
    head.next = &head;
    head.prev = &head;
    head.vtable = kHeadVtable;
    component->mp_suspend = nullptr;
    component->mp_skeleton = nullptr;
    component->mp_model = nullptr;
    component->m_type = kType;
    return component;
}

// 0x00434670
CTextureProjectionComponent::ProjectionList* CTextureProjectionComponent::ProjectionList::Destroy(unsigned int flags)
{
    m_node.UnlinkAs(kDeadHeadVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004346B0
void CTextureProjectionComponent::DestroyBody()
{
    m_vtable = kVtable;
    reinterpret_cast<ListHead*>(&m_projections)->DeleteAllNodes();
    m_projections.m_node.UnlinkAs(kDeadHeadVtable);
    CBaseComponent::DestroyBody();
}

// 0x00434840
CTextureProjectionComponent* CTextureProjectionComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00434730
void CTextureProjectionComponent::InitFromStructure(Script::CStruct* params)
{
    Script::CArray* projections = nullptr;
    params->GetArray(kTextureProjections, &projections, 1);
    Mem::ListNode& head = m_projections.m_node;
    for (uint32_t i = 0; i < projections->m_size; ++i) {
        auto* settings = reinterpret_cast<Script::CStruct*>(projections->GetElement(i));
        uint32_t name = 0; // (read into the parameters' own argument slot)
        settings->GetChecksum(0x1EE8B7B9, &name, 1);
        Mem::ListNode* projection = NewProjection();
        // (out of memory: written through null, as the original)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(projection) + 0x14) = name;
        GameFunction<void(__thiscall*)(Mem::ListNode*, Script::CStruct*)>(0x004A04C0)(projection, settings);
        // At the front.
        projection->prev = &head;
        projection->next = head.next;
        head.next->prev = projection;
        head.next = projection;
    }
}

// 0x00434630
void CTextureProjectionComponent::Finalize()
{
    mp_suspend = m_object->GetComponent(0xCE0CA665);
    mp_skeleton = m_object->GetComponent(0x222756D5);
    mp_model = m_object->GetComponent(0x286A8D26);
}

// 0x00434810
void CTextureProjectionComponent::Update()
{
    // (the head test before the null test, as the original)
    for (Mem::ListNode* node = m_projections.m_node.next; !IsHead(node) && node; node = node->next)
        GameFunction<void(__thiscall*)(Mem::ListNode*, CCompositeObject*)>(0x0049FFE0)(node, m_object);
}

} // namespace Obj

REPLACE(0x00434870, Obj::CTextureProjectionComponent::Create);
REPLACE(0x00434670, &Obj::CTextureProjectionComponent::ProjectionList::Destroy);
REPLACE(0x004346B0, &Obj::CTextureProjectionComponent::DestroyBody);
REPLACE(0x00434840, &Obj::CTextureProjectionComponent::Destroy);
REPLACE(0x00434730, &Obj::CTextureProjectionComponent::InitFromStructure);
REPLACE(0x00434630, &Obj::CTextureProjectionComponent::Finalize);
REPLACE(0x00434810, &Obj::CTextureProjectionComponent::Update);
