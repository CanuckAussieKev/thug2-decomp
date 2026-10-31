// The texture projection component (engine/objects/texture_projection_component.h).

#include "engine/objects/texture_projection_component.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_texture_projection.h"
#include "engine/gfx/pc_texture_projection.h"
#include "engine/objects/movie_manager.h"
#include "engine/script/param_struct.h"
#include "engine/core/game_vtables.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
extern void** const kHeadVtable;     // the head as made

constexpr uint32_t kTextureProjections = 0xFF7E0DAD; // texture_projections

// A projection (the renderer's, 0xC8 bytes; a list node with its checksum at +0x14): made (0x004C7E80), set up from
// its structure (0x004A04C0) and updated from an object (0x0049FFE0).
MemSys::ListNode* NewProjection()
{
    void* memory = MemSys::AllocZeroed(0xC8);
    return memory ? reinterpret_cast<MemSys::ListNode*>(static_cast<Engine3D::CPcTextureProjection*>(memory)->MakeNew()) : nullptr;
}

bool IsHead(const MemSys::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

} // namespace

// 0x00434870
CObjectModule* __cdecl CTextureProjectionComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CTextureProjectionComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CTextureProjectionComponent*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    MemSys::ListNode& head = component->m_projections.m_listLink;
    head.priority = 0;
    head.data = reinterpret_cast<void*>(-1);
    head.next = &head;
    head.prev = &head;
    head.vtable = kHeadVtable;
    component->m_suspend = nullptr;
    component->m_boneRig = nullptr;
    component->m_meshInstance = nullptr;
    component->m_kindTag = kType;
    return component;
}

// 0x00434670
CTextureProjectionComponent::ProjectionList* CTextureProjectionComponent::ProjectionList::Teardown(unsigned int flags)
{
    m_listLink.UnlinkAs(Vt::kProjectionNode);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004346B0
void CTextureProjectionComponent::DestroyBody()
{
    m_vtable = kVtable;
    reinterpret_cast<ListHead*>(&m_projections)->DeleteAllNodes();
    m_projections.m_listLink.UnlinkAs(Vt::kProjectionNode);
    CObjectModule::DestroyBody();
}

// 0x00434840
CTextureProjectionComponent* CTextureProjectionComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00434730
void CTextureProjectionComponent::ConfigureFromData(Scripts::CStruct* params)
{
    Scripts::CArray* projections = nullptr;
    params->FetchList(kTextureProjections, &projections, 1);
    MemSys::ListNode& head = m_projections.m_listLink;
    for (uint32_t i = 0; i < projections->m_extent; ++i) {
        auto* settings = reinterpret_cast<Scripts::CStruct*>(projections->FetchPart(i));
        uint32_t name = 0; // (read into the parameters' own argument slot)
        settings->ReadHashValue(0x1EE8B7B9, &name, 1);
        MemSys::ListNode* projection = NewProjection();
        // (out of memory: written through null, as the original)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(projection) + 0x14) = name;
        reinterpret_cast<Engine3D::CTextureProjection*>(projection)->ConfigureFromData(settings);
        // At the front.
        projection->prev = &head;
        projection->next = head.next;
        head.next->prev = projection;
        head.next = projection;
    }
}

// 0x00434630
void CTextureProjectionComponent::Seal()
{
    m_suspend = m_entity->FetchModule(0xCE0CA665);
    m_boneRig = m_entity->FetchModule(0x222756D5);
    m_meshInstance = m_entity->FetchModule(0x286A8D26);
}

// 0x00434810
void CTextureProjectionComponent::TickFrame()
{
    // (the head test before the null test, as the original)
    for (MemSys::ListNode* node = m_projections.m_listLink.next; !IsHead(node) && node; node = node->next)
        reinterpret_cast<Engine3D::CTextureProjection*>(node)->TickFrame(m_entity);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CTextureProjectionComponent::Teardown,
         &GameObjects::CTextureProjectionComponent::TickFrame,
         &GameObjects::CTextureProjectionComponent::ConfigureFromData,
         &GameObjects::CTextureProjectionComponent::Seal,
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
static VtableSlot kHeadVtable_slots[] = {&Engine3D::CPcTextureProjection::Teardown};
void** const kHeadVtable = reinterpret_cast<void**>(kHeadVtable_slots);

} // namespace
} // namespace Obj
