// The FlipRotate component (GameObjects::FlipRotateComponent): construction, destruction and its parameters.

#include "engine/objects/flip_rotate.h"
#include "engine/gfx/anim_controller.h"
#include "engine/gfx/rig.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/maths/quat_rotation.h"
#include "engine/objects/entity.h"
#include "engine/objects/vehicle_component.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/car_physics.h"
#include "game/rider/flip_and_rotate.h"

namespace objects {

namespace {

constexpr uint32_t kFlipped = 0x0C7A712C; // "flipped"
constexpr uint32_t kRotated = 0x6EA3704A; // "rotated"

} // namespace

// 0x004E2C50
FlipRotateComponent::FlipRotateComponent(gfx::AnimTimeline* timeline) : AnimDriver(timeline)
{
    mirrored_ = false;
    has_pivoted_ = false;
}

// 0x004E2C90
void FlipRotateComponent::configure_from_data(script::Struct* params)
{
    AnimDriver::configure_from_data(params);
    int value;
    if (params->fetch_int_value(kFlipped, &value, 0))
        mirrored_ = value != 0;
    if (params->fetch_int_value(kRotated, &value, 0))
        has_pivoted_ = value != 0;
}

// 0x004E2D20
bool FlipRotateComponent::fetch_pose(uint8_t* pose)
{
    // The controller's skeleton and object (the base controller's; not rewritten yet).
    uint8_t* skeleton = fetch_rig();
    void* bones = *reinterpret_cast<void**>(skeleton + 8);
    auto* object = fetch_subject();
    ObjectModule* flip_and_rotate = object->fetch_module(0x488AF07A);
    if (flip_and_rotate && static_cast<RiderSpinAndFlip*>(flip_and_rotate)->deck_turned_) {
        int bone = reinterpret_cast<gfx::Rig*>(skeleton)->fetch_bone_slot(0x98971FAF);
        if (bone != -1) {
            auto* rotation = reinterpret_cast<float*>(pose + 0x10 * bone);
            const float turn[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            float product[4];
            const float* result = reinterpret_cast<const float*>(
                quat_product(reinterpret_cast<maths::Vec4*>(product), reinterpret_cast<const maths::Vec4*>(turn),
                             reinterpret_cast<const maths::Vec4*>(rotation)));
            std::memcpy(rotation, result, 16);
            reinterpret_cast<maths::QuatRotation*>(rotation)->make_unit(1.0f); // (to length 1)
        }
    }
    if (!mirrored_)
        return true;
    // The pose as it was (rotations past the bone count: no turn), then each bone from its mirror image.
    float rotations[64][4], translations[64][4];
    for (auto& r : rotations) {
        r[0] = r[1] = r[2] = 0.0f;
        r[3] = 1.0f;
    }
    int count = reinterpret_cast<int(__thiscall*)(void*)>(code_address(&gfx::Rig::get_num_bones))(skeleton);
    std::memcpy(rotations, pose, 0x10 * (count > 0 ? count : 0));
    std::memcpy(translations, pose + 0x400, 0x10 * (count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) {
        int mirror = static_cast<gfx::RigData*>(bones)->fetch_mirror_slot(i);
        int from = mirror == -1 ? i : mirror;
        auto* rotation = reinterpret_cast<float*>(pose + 0x10 * i);
        auto* translation = reinterpret_cast<float*>(pose + 0x400 + 0x10 * i);
        std::memcpy(rotation, rotations[from], 16);
        std::memcpy(translation, translations[from], 16);
        rotation[0] = -rotation[0];
        rotation[3] = -rotation[3];
        translation[0] = -translation[0];
    }
    return true;
}

} // namespace objects
