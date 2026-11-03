// The FlipRotate component (GameObjects::FlipRotateComponent): construction, destruction and its parameters.

#include "engine/objects/flip_rotate.h"
#include "engine/gfx/anim_controller.h"
#include "engine/gfx/rig.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/vehicle_component.h"
#include "engine/script/param_struct.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/car_physics.h"
#include "game/rider/flip_and_rotate.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <algorithm>

namespace objects {

namespace {
constexpr std::size_t kPoseCapacity = 64;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // SkaterFlipAndRotate

constexpr uint32_t kFlipped = 0x0C7A712C; // "flipped"
constexpr uint32_t kRotated = 0x6EA3704A; // "rotated"

} // namespace

// 0x004E2C50
FlipRotateComponent::FlipRotateComponent(gfx::AnimTimeline* timeline) : AnimDriver(timeline) {}

// 0x004E2C90
void FlipRotateComponent::configure_from_data(script::Struct* params)
{
    AnimDriver::configure_from_data(params);
    int value = 0;
    if (params->fetch_int_value(kFlipped, &value, 0))
        mirrored_ = value != 0;
    if (params->fetch_int_value(kRotated, &value, 0))
        has_pivoted_ = value != 0;
}

// 0x004E2D20
bool FlipRotateComponent::fetch_pose(gfx::Stance* pose)
{
    // The controller's skeleton and object (the base controller's; not rewritten yet).
    gfx::Rig* skeleton = fetch_rig();
    gfx::RigData* bones = skeleton->payload();
    auto* object = fetch_subject();
    ObjectModule* flip_and_rotate = object->fetch_module(kSkaterFlipAndRotate);
    if (flip_and_rotate && core::down_cast<RiderSpinAndFlip>(flip_and_rotate)->deck_turned()) {
        const int bone = skeleton->fetch_bone_slot(0x98971FAF);
        if (bone != -1) {
            maths::QuatRotation& rotation = core::at(pose->rotations, bone);
            const maths::Vec4 turn = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
            const maths::Vec4 as_vector = {.x = rotation.x, .y = rotation.y, .z = rotation.z, .w = rotation.w};
            maths::Vec4 product{};
            const maths::Vec4* result = quat_product(&product, &turn, &as_vector);
            rotation = {.x = result->x, .y = result->y, .z = result->z, .w = result->w};
            rotation.make_unit(1.0f); // (to length 1)
        }
    }
    if (!mirrored_)
        return true;
    // The pose as it was (rotations past the bone count: no turn), then each bone from its mirror image.
    std::array<maths::QuatRotation, kPoseCapacity> rotations{};
    std::array<maths::Vec4, kPoseCapacity> translations{};
    for (auto& r : rotations)
        r = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    const int count = skeleton->get_num_bones();
    std::copy_n(pose->rotations.begin(), count > 0 ? count : 0, rotations.begin());
    std::copy_n(pose->translations.begin(), count > 0 ? count : 0, translations.begin());
    for (int i = 0; i < count; ++i) {
        const int mirror = static_cast<gfx::RigData*>(bones)->fetch_mirror_slot(i);
        const int from = mirror == -1 ? i : mirror;
        maths::QuatRotation& rotation = core::at(pose->rotations, i);
        maths::Vec4& translation = core::at(pose->translations, i);
        rotation = core::at(rotations, from);
        translation = core::at(translations, from);
        rotation.x = -rotation.x;
        rotation.w = -rotation.w;
        translation.x = -translation.x;
    }
    return true;
}

} // namespace objects
