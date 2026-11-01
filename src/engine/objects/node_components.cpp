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
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

static_assert(sizeof(ClassNodeList) == 0x4C && sizeof(GrindRailHub) == 0x54, "the sizes allocated here");

namespace {

constexpr uint32_t kModel = 0x286A8D26; // model

// The node array of an object's node array component (the original's slot 13).
script::Array* node_array_of(ObjectModule* component)
{
    return static_cast<WaypointListModule*>(component)->get_node_array();
}

} // namespace

// --- the node array ---

// 0x00428010
memory::owner<ObjectModule*> WaypointListModule::fabricate() { return new WaypointListModule; }

// (the constructor: inlined in fabricate in the original)
WaypointListModule::WaypointListModule()
{
    kind_tag_ = kType;
    nodes_ = nullptr;
}

// 0x00427EA0 (0x00427EF0 the deleting one)
WaypointListModule::~WaypointListModule() {}

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
        configure_from_data(params);
}

// --- the object hooks ---

// 0x00428130
memory::owner<ObjectModule*> HookPointRegistryModule::fabricate() { return new HookPointRegistryModule; }

// (the constructor: inlined in fabricate in the original)
HookPointRegistryModule::HookPointRegistryModule()
{
    kind_tag_ = kType;
    hooks_ptr_ = nullptr;
}

// 0x004281B0 (0x00428220 the deleting one)
HookPointRegistryModule::~HookPointRegistryModule()
{
    if (ClassNodeList* hooks = hooks_ptr_) {
        hooks->get_clear(); // (its destructor)
        memory::Hub::get_pull()->dealloc(hooks);
    }
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
    hooks_ptr_ = memory ? new (memory) ClassNodeList : nullptr;
    hooks_ptr_->set_nodes(node_array_of(node_array));
}

// --- the rails ---

// 0x0042B920
memory::owner<ObjectModule*> GrindPathLinkModule::fabricate() { return new GrindPathLinkModule; }

// (the constructor: inlined in fabricate in the original)
GrindPathLinkModule::GrindPathLinkModule() { kind_tag_ = kType; }

// 0x0042B9A0 (0x0042BA90 the deleting one): the rails destroyed and freed (rail_groups_'s destructor)
GrindPathLinkModule::~GrindPathLinkModule() = default;

// 0x0042B880
void GrindPathLinkModule::configure_from_data(script::Struct*)
{
    ObjectModule* node_array = entity_->fetch_module(WaypointListModule::kType);
    rail_groups_.reset(); // (the old ones destroyed and freed first)
    rail_groups_ = memory::make_zeroed<GrindRailHub>();
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

} // namespace objects

using objects::GrindPathLinkModule;
using objects::HookPointRegistryModule;
using objects::WaypointListModule;
