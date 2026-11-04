// The node array, object hook manager and rail manager components (node_components.h).

#include "engine/objects/node_components.h"

#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>
#include <gsl/assert>

#include "engine/assets/resource_entry.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/objects/node_objects.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/param_struct.h"
#include "engine/maths/fwd.h"
#include "engine/script/fwd.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace objects {

constexpr std::size_t kClassNodeListBytes = 0x4C;
constexpr std::size_t kGrindRailHubBytes = 0x54;
static_assert(sizeof(ClassNodeList) == kClassNodeListBytes && sizeof(GrindRailHub) == kGrindRailHubBytes, "the sizes allocated here");

namespace {
constexpr std::size_t kPathBytes = 0x100;

constexpr uint32_t kModel = 0x286A8D26; // model

// The node array of an object's node array component (the original's slot 13).
script::Array* node_array_of(ObjectModule* component)
{
    return core::down_cast<WaypointListModule>(component)->get_node_array();
}

} // namespace

// --- the node array ---

// 0x00428010
gsl::owner<ObjectModule*> WaypointListModule::fabricate() { return new WaypointListModule; }

// (the constructor: inlined in fabricate in the original)
WaypointListModule::WaypointListModule() { set_kind_tag(kType); }

// 0x00427EA0 (0x00427EF0 the deleting one)
WaypointListModule::~WaypointListModule() = default;

// 0x00427F20
void WaypointListModule::configure_from_data(script::Struct* params)
{
    const char* model = nullptr;
    if (!params->fetch_text(kModel, &model, 0)) {
        return;
    }
    std::array<char, kPathBytes> path{};
    text::format_to(path, "models\\%s", model); // (the original: strcpy / strcat, unbounded)
    if (char* dot = std::strstr(path.data(), ".")) {
        std::memcpy(dot, ".qb", 4);
    } else {
        text::format_to(std::span(path).subspan(std::strlen(path.data())), ".qb");
    }
    nodes_ = static_cast<script::Array*>(
        assets::asset_manager()->fetch_or_load_resource(path.data(), assets::ResourceKind::kNone, 0, 0, false, false, false, nullptr, nullptr));
}

// 0x00427EB0
void WaypointListModule::reapply_from_data(script::Struct* params)
{
    const char* model = nullptr;
    if (params->fetch_text(kModel, &model, 0)) {
        configure_from_data(params);
    }
}

// --- the object hooks ---

// 0x00428130
gsl::owner<ObjectModule*> HookPointRegistryModule::fabricate() { return new HookPointRegistryModule; }

// (the constructor: inlined in fabricate in the original)
HookPointRegistryModule::HookPointRegistryModule() { set_kind_tag(kType); }

// 0x004281B0 (0x00428220 the deleting one)
HookPointRegistryModule::~HookPointRegistryModule()
{
    if (ClassNodeList* hooks = hooks_ptr_) {
        hooks->get_clear(); // (its destructor)
        memory::Hub::dealloc(hooks);
    }
}

// 0x00428090
void HookPointRegistryModule::configure_from_data(script::Struct* /*params*/)
{
    ObjectModule* node_array = entity()->fetch_module(WaypointListModule::kType);
    if (ClassNodeList* hooks = hooks_ptr_) {
        hooks->get_clear();
        memory::Hub::dealloc(hooks);
    }
    void* memory = memory::alloc_zeroed(sizeof(ClassNodeList));
    hooks_ptr_ = memory ? memory::construct_in<ClassNodeList>(memory) : nullptr;
    Expects(hooks_ptr_ != nullptr);
    hooks_ptr_->set_nodes(node_array_of(node_array));
}

// --- the rails ---

// 0x0042B920
gsl::owner<ObjectModule*> GrindPathLinkModule::fabricate() { return new GrindPathLinkModule; }

// (the constructor: inlined in fabricate in the original)
GrindPathLinkModule::GrindPathLinkModule() { set_kind_tag(kType); }

// 0x0042B9A0 (0x0042BA90 the deleting one): the rails destroyed and freed (rail_groups_'s destructor)
GrindPathLinkModule::~GrindPathLinkModule() = default;

// 0x0042B880
void GrindPathLinkModule::configure_from_data(script::Struct* /*params*/)
{
    ObjectModule* node_array = entity()->fetch_module(WaypointListModule::kType);
    rail_groups_.reset(); // (the old ones destroyed and freed first)
    rail_groups_ = memory::make_zeroed<GrindRailHub>();
    rail_groups_->add_from_node_array(node_array_of(node_array));
}

// 0x0042BA10
maths::Mat4* GrindPathLinkModule::update_rail_matrix(maths::Mat4* out)
{
    maths::Mat4 placement{};
    placement = entity()->basis_frame();
    core::at(placement.row, 0).w = 0.0F;
    core::at(placement.row, 1).w = 0.0F;
    core::at(placement.row, 2).w = 0.0F;
    const maths::Vec4& pos = entity()->at_point();
    core::at(placement.row, 3) = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0F};
    rail_groups_->change_body_transform(placement);
    *out = placement;
    return out;
}

} // namespace objects

