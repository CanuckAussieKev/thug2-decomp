// The static vehicle component (engine/objects/static_vehicle.h).

#include "engine/objects/static_vehicle.h"

#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>
#include <span>

#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "base/maths/fwd.h"
#include "engine/objects/entity.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/gfx/scene_load.h"

namespace objects {

namespace {

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;

// The model component's model (+0x208) and that model's hierarchy (0x00493040, not rewritten yet): objects of 0x50
// bytes from +0x50, each with its matrix at +0x10.
std::span<const render::HierarchyNode> hierarchy_of(ObjectModule* model)
{
    return core::down_cast<objects::MeshInstanceModule>(model)->mesh_instance()->asset_hierarchy_run();
}

// The skeleton component's skeleton.
gfx::Rig* skeleton_of(ObjectModule* skeleton) { return core::down_cast<objects::BoneRigModule>(skeleton)->bone_rig(); }

// A node's place (its matrix's translation) in the game's axes: x and the model's y negated, y and z swapped.
maths::Vec4 from_model(const render::HierarchyNode& node)
{
    const maths::Vec4& translation = core::at(node.matrix().row, 3);
    return {.x = -translation.x, .y = translation.z, .z = -translation.y, .w = 1.0F};
}

} // namespace

// 0x00433690
gsl::owner<ObjectModule*> ParkedVehicleModule::fabricate() { return new ParkedVehicleModule; }

// (the constructor: inlined in fabricate in the original)
ParkedVehicleModule::ParkedVehicleModule() { set_kind_tag(kType); }

// 0x00433650 (0x00433660 the deleting one)
ParkedVehicleModule::~ParkedVehicleModule() = default;

// 0x004336F0
void ParkedVehicleModule::seal()
{
    const std::span<const render::HierarchyNode> hierarchy = hierarchy_of(entity()->fetch_module(kModel));
    // The wheels: the hierarchy's nodes 2-5 (their matrices' translations).
    std::array<maths::Vec4, 4> wheels{};
    for (int k = 0; k < 4; ++k) {
        core::at(wheels, k) = from_model(core::at(hierarchy, 2 + k));
    }
    // The body: node 1's translation.
    const maths::Vec4 body = from_model(core::at(hierarchy, 1));

    ObjectModule* skeleton_component = entity()->fetch_module(kSkeleton);
    const std::span<maths::Mat4> bones = skeleton_of(skeleton_component)->matrices();
    for (int i = 0; i < skeleton_of(skeleton_component)->get_num_bones(); ++i) {
        maths::Mat4& bone = core::at(bones, i);
        std::memset(&bone, 0, sizeof(bone));
        if (i == 0) {
            continue;
        }
        if (i == 1) {
            core::at(bone.row, 0).x = 1.0F;
            core::at(bone.row, 1).z = -1.0F;
            core::at(bone.row, 2).y = 1.0F;
            core::at(bone.row, 3) = body;
        } else {
            core::at(bone.row, 0).x = -1.0F;
            core::at(bone.row, 1).z = 1.0F;
            core::at(bone.row, 2).y = 1.0F;
            // Not original - a seventh bone or more would be a fifth wheel: the original reads it from past the end of
            // its four (its own stack - garbage); ours stands it at the origin.
            core::at(bone.row, 3) =
                i - 2 < 4 ? core::at(wheels, i - 2) : maths::Vec4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        }
    }
}

} // namespace objects
