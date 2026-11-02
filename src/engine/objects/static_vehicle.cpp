// The static vehicle component (engine/objects/static_vehicle.h).

#include "engine/objects/static_vehicle.h"

#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "platform/code_address.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/scene_load.h"

namespace objects {

namespace {

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;

// The model component's model (+0x208) and that model's hierarchy (0x00493040, not rewritten yet): objects of 0x50
// bytes from +0x50, each with its matrix at +0x10.
std::span<const render::HierarchyNode> hierarchy_of(ObjectModule* model)
{
    return core::down_cast<objects::MeshInstanceModule>(model)->mesh_instance_->asset_hierarchy_run();
}

// The skeleton component's skeleton.
gfx::Rig* skeleton_of(ObjectModule* skeleton) { return core::down_cast<objects::BoneRigModule>(skeleton)->bone_rig_; }

// A node's place (its matrix's translation) in the game's axes: x and the model's y negated, y and z swapped.
maths::Vec4 from_model(const render::HierarchyNode& node)
{
    const maths::Vec4& p = core::at(node.matrix.row, 3);
    return {.x = -p.x, .y = p.z, .z = -p.y, .w = 1.0f};
}

} // namespace

// 0x00433690
gsl::owner<ObjectModule*> ParkedVehicleModule::fabricate() { return new ParkedVehicleModule; }

// (the constructor: inlined in fabricate in the original)
ParkedVehicleModule::ParkedVehicleModule() { kind_tag_ = kType; }

// 0x00433650 (0x00433660 the deleting one)
ParkedVehicleModule::~ParkedVehicleModule() = default;

// 0x004336F0
void ParkedVehicleModule::seal()
{
    const std::span<const render::HierarchyNode> hierarchy = hierarchy_of(entity_->fetch_module(kModel));
    // The wheels: the hierarchy's nodes 2-5 (their matrices' translations).
    std::array<maths::Vec4, 4> wheels{};
    for (int k = 0; k < 4; ++k)
        core::at(wheels, k) = from_model(core::at(hierarchy, 2 + k));
    // The body: node 1's translation.
    const maths::Vec4 body = from_model(core::at(hierarchy, 1));

    ObjectModule* skeleton_component = entity_->fetch_module(kSkeleton);
    const std::span<maths::Mat4> bones = skeleton_of(skeleton_component)->matrices();
    for (int i = 0; i < skeleton_of(skeleton_component)->get_num_bones(); ++i) {
        maths::Mat4& bone = core::at(bones, i);
        std::memset(&bone, 0, sizeof(bone));
        if (i == 0)
            continue;
        if (i == 1) {
            core::at(bone.row, 0).x = 1.0f;
            core::at(bone.row, 1).z = -1.0f;
            core::at(bone.row, 2).y = 1.0f;
            core::at(bone.row, 3) = body;
        } else {
            core::at(bone.row, 0).x = -1.0f;
            core::at(bone.row, 1).z = 1.0f;
            core::at(bone.row, 2).y = 1.0f;
            // Not original - a seventh bone or more would be a fifth wheel: the original reads it from past the end of
            // its four (its own stack - garbage); ours stands it at the origin.
            core::at(bone.row, 3) = i - 2 < 4 ? core::at(wheels, i - 2) : maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
        }
    }
}

} // namespace objects
