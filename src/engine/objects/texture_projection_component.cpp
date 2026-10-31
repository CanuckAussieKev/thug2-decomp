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

namespace objects {

namespace {

extern void** const kVtable;
extern void** const kHeadVtable;     // the head as made

constexpr uint32_t kTextureProjections = 0xFF7E0DAD; // texture_projections

// A projection (the renderer's, 0xC8 bytes; a list node with its checksum at +0x14): made (0x004C7E80), set up from
// its structure (0x004A04C0) and updated from an object (0x0049FFE0).
memory::ListNode* new_projection()
{
    void* memory = memory::alloc_zeroed(0xC8);
    return memory ? reinterpret_cast<memory::ListNode*>(static_cast<render::PcTextureProjection*>(memory)->make_new()) : nullptr;
}

bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

} // namespace

// 0x00434870
ObjectModule* __cdecl TextureProjectionComponent::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(TextureProjectionComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<TextureProjectionComponent*>(memory);
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    memory::ListNode& head = component->projections_.list_link_;
    head.priority = 0;
    head.data = reinterpret_cast<void*>(-1);
    head.next = &head;
    head.prev = &head;
    head.vtable = kHeadVtable;
    component->suspend_ = nullptr;
    component->bone_rig_ = nullptr;
    component->mesh_instance_ = nullptr;
    component->kind_tag_ = kType;
    return component;
}

// 0x00434670
TextureProjectionComponent::ProjectionList* TextureProjectionComponent::ProjectionList::teardown(unsigned int flags)
{
    list_link_.unlink_as(vt::kProjectionNode);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004346B0
void TextureProjectionComponent::destroy_body()
{
    vtable_ = kVtable;
    reinterpret_cast<ListHead*>(&projections_)->delete_all_nodes();
    projections_.list_link_.unlink_as(vt::kProjectionNode);
    ObjectModule::destroy_body();
}

// 0x00434840
TextureProjectionComponent* TextureProjectionComponent::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00434730
void TextureProjectionComponent::configure_from_data(script::Struct* params)
{
    script::Array* projections = nullptr;
    params->fetch_list(kTextureProjections, &projections, 1);
    memory::ListNode& head = projections_.list_link_;
    for (uint32_t i = 0; i < projections->extent_; ++i) {
        auto* settings = reinterpret_cast<script::Struct*>(projections->fetch_part(i));
        uint32_t name = 0; // (read into the parameters' own argument slot)
        settings->read_hash_value(0x1EE8B7B9, &name, 1);
        memory::ListNode* projection = new_projection();
        // (out of memory: written through null, as the original)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(projection) + 0x14) = name;
        reinterpret_cast<render::TextureProjection*>(projection)->configure_from_data(settings);
        // At the front.
        projection->prev = &head;
        projection->next = head.next;
        head.next->prev = projection;
        head.next = projection;
    }
}

// 0x00434630
void TextureProjectionComponent::seal()
{
    suspend_ = entity_->fetch_module(0xCE0CA665);
    bone_rig_ = entity_->fetch_module(0x222756D5);
    mesh_instance_ = entity_->fetch_module(0x286A8D26);
}

// 0x00434810
void TextureProjectionComponent::tick_frame()
{
    // (the head test before the null test, as the original)
    for (memory::ListNode* node = projections_.list_link_.next; !is_head(node) && node; node = node->next)
        reinterpret_cast<render::TextureProjection*>(node)->tick_frame(entity_);
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::TextureProjectionComponent::teardown,
         &objects::TextureProjectionComponent::tick_frame,
         &objects::TextureProjectionComponent::configure_from_data,
         &objects::TextureProjectionComponent::seal,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);
static VtableSlot g_head_vtable_slots[] = {&render::PcTextureProjection::teardown};
void** const kHeadVtable = reinterpret_cast<void**>(g_head_vtable_slots);

} // namespace
} // namespace Obj
