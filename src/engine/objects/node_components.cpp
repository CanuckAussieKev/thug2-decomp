// The node array, object hook manager and rail manager components (node_components.h).

#include "engine/objects/node_components.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/node_objects.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

static_assert(sizeof(ClassNodeList) == 0x4C && sizeof(GrindRailHub) == 0x54, "the sizes allocated here");

namespace {

extern void** const kNodeArrayVtable;
extern void** const kHookManagerVtable;
extern void** const kRailManagerVtable;

constexpr uint32_t kModel = 0x286A8D26; // model

// The node array of an object's node array component (its vtable slot 13).
script::Array* node_array_of(ObjectModule* component)
{
    return reinterpret_cast<script::Array*(__thiscall*)(ObjectModule*)>(component->vtable_[13])(component);
}

} // namespace

// --- the node array ---

// 0x00428010
ObjectModule* __cdecl WaypointListModule::fabricate()
{
    auto* component = static_cast<WaypointListModule*>(memory::alloc_zeroed(sizeof(WaypointListModule)));
    if (!component)
        return nullptr;
    component->ObjectModule::make_new();
    component->vtable_ = kNodeArrayVtable;
    component->kind_tag_ = kType;
    component->nodes_ = nullptr;
    return component;
}

// 0x00427EA0
void WaypointListModule::destroy_body()
{
    vtable_ = kNodeArrayVtable;
    ObjectModule::destroy_body();
}

// 0x00427EF0
WaypointListModule* WaypointListModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00427F20
void WaypointListModule::configure_from_data(script::Struct* params)
{
    const char* model = nullptr;
    if (!params->fetch_text(kModel, &model, 0))
        return;
    char path[0x100];
    std::snprintf(path, sizeof(path), "models\\%s", model); // (the original: strcpy / strcat, unbounded)
    if (char* dot = std::strstr(path, "."))
        std::memcpy(dot, ".qb", 4);
    else
        std::snprintf(path + std::strlen(path), sizeof(path) - std::strlen(path), ".qb");
    nodes_ = static_cast<script::Array*>(
        assets::asset_manager()->fetch_or_load_resource(path, 0, 0, 0, false, false, false, nullptr, 0));
}

// 0x00427EB0
void WaypointListModule::reapply_from_data(script::Struct* params)
{
    const char* model = nullptr;
    if (params->fetch_text(kModel, &model, 0))
        ObjectModule::configure_from_data(params);
}

// --- the object hooks ---

// 0x00428130
ObjectModule* __cdecl HookPointRegistryModule::fabricate()
{
    auto* component = static_cast<HookPointRegistryModule*>(memory::alloc_zeroed(sizeof(HookPointRegistryModule)));
    if (!component)
        return nullptr;
    component->ObjectModule::make_new();
    component->vtable_ = kHookManagerVtable;
    component->kind_tag_ = kType;
    component->hooks_ptr_ = nullptr;
    return component;
}

// 0x004281B0
void HookPointRegistryModule::destroy_body()
{
    vtable_ = kHookManagerVtable;
    if (ClassNodeList* hooks = hooks_ptr_) {
        hooks->get_clear(); // (its destructor)
        memory::Hub::get_pull()->dealloc(hooks);
    }
    ObjectModule::destroy_body();
}

// 0x00428220
HookPointRegistryModule* HookPointRegistryModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00428090
void HookPointRegistryModule::configure_from_data(script::Struct*)
{
    ObjectModule* node_array = entity_->fetch_module(WaypointListModule::kType);
    if (ClassNodeList* hooks = hooks_ptr_) {
        hooks->get_clear();
        memory::Hub::get_pull()->dealloc(hooks);
    }
    void* memory = memory::alloc_zeroed(sizeof(ClassNodeList));
    hooks_ptr_ = memory ? static_cast<ClassNodeList*>(memory)->prepare_all() : nullptr;
    hooks_ptr_->set_nodes(node_array_of(node_array));
}

// --- the rails ---

// 0x0042B920
ObjectModule* __cdecl GrindPathLinkModule::fabricate()
{
    auto* component = static_cast<GrindPathLinkModule*>(memory::alloc_zeroed(sizeof(GrindPathLinkModule)));
    if (!component)
        return nullptr;
    component->ObjectModule::make_new();
    component->vtable_ = kRailManagerVtable;
    component->kind_tag_ = kType;
    component->rail_groups_ = nullptr;
    return component;
}

// 0x0042B9A0
void GrindPathLinkModule::destroy_body()
{
    vtable_ = kRailManagerVtable;
    if (GrindRailHub* rails = rail_groups_) {
        rails->tidy_up(); // (its destructor)
        memory::Hub::get_pull()->dealloc(rails);
    }
    ObjectModule::destroy_body();
}

// 0x0042BA90
GrindPathLinkModule* GrindPathLinkModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0042B880
void GrindPathLinkModule::configure_from_data(script::Struct*)
{
    ObjectModule* node_array = entity_->fetch_module(WaypointListModule::kType);
    if (GrindRailHub* rails = rail_groups_) {
        rails->tidy_up();
        memory::Hub::get_pull()->dealloc(rails);
    }
    void* memory = memory::alloc_zeroed(sizeof(GrindRailHub));
    rail_groups_ = memory ? static_cast<GrindRailHub*>(memory)->prepare_all() : nullptr;
    rail_groups_->add_from_node_array(node_array_of(node_array));
}

// 0x0042BA10
maths::Mat4* GrindPathLinkModule::update_rail_matrix(maths::Mat4* out)
{
    maths::Mat4 placement;
    placement = entity_->basis_frame_;
    placement.row[0].w = 0.0f;
    placement.row[1].w = 0.0f;
    placement.row[2].w = 0.0f;
    const maths::Vec4& pos = entity_->at_point_;
    placement.row[3] = {pos.x, pos.y, pos.z, 1.0f};
    rail_groups_->change_body_transform(placement);
    *out = placement;
    return out;
}

} // namespace Obj

using objects::WaypointListModule;
using objects::HookPointRegistryModule;
using objects::GrindPathLinkModule;

namespace objects {
namespace {

static VtableSlot g_node_array_vtable_slots[] = {&WaypointListModule::teardown,
         &objects::ComponentWithoutUpdate::tick_frame,
         &WaypointListModule::configure_from_data,
         stubs::do_nothing,
         stubs::do_nothing,
         &WaypointListModule::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name,
         &online::FoldedGetters::word18};
void** const kNodeArrayVtable = reinterpret_cast<void**>(g_node_array_vtable_slots);
static VtableSlot g_hook_manager_vtable_slots[] = {&HookPointRegistryModule::teardown,
         &objects::ComponentWithoutUpdate::tick_frame,
         &HookPointRegistryModule::configure_from_data,
         stubs::do_nothing,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name,
         &online::FoldedGetters::word18};
void** const kHookManagerVtable = reinterpret_cast<void**>(g_hook_manager_vtable_slots);
static VtableSlot g_rail_manager_vtable_slots[] = {&GrindPathLinkModule::teardown,
         &objects::ComponentWithoutUpdate::tick_frame,
         &GrindPathLinkModule::configure_from_data,
         stubs::do_nothing,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name,
         &online::FoldedGetters::word18};
void** const kRailManagerVtable = reinterpret_cast<void**>(g_rail_manager_vtable_slots);

} // namespace
} // namespace Obj
