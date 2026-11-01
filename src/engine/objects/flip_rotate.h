#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/anim_controller.h"

namespace script {
class Struct;
}

namespace objects {

// The "FlipRotate" animation controller (made by the controller factory 0x00481FB0 for the type checksum 0xDF5C091A;
// vtable 0x0064B1D4): an object's flipped / rotated state applied to its pose.
class FlipRotateComponent final : public gfx::AnimDriver {
public:
    // 0x004E2C50: on `timeline`, neither flipped nor rotated.
    explicit FlipRotateComponent(gfx::AnimTimeline* timeline);
    // 0x004E2C80 / 0x004E2CF0: nothing of its own.
    ~FlipRotateComponent() override = default;
    // 0x004E2C90 (vtable slot 4): the base's parameters, then "flipped" and "rotated" (integers: non-zero = on;
    // each left as it was when not given).
    void configure_from_data(script::Struct* params) override;
    // 0x004E2D20 (vtable slot 1): the pose (64 rotations at +0, 64 translations at +0x400) turned and mirrored:
    // with the object's SkaterFlipAndRotate component (0x488AF07A) turned (its +0x18), the bone 0x98971FAF's
    // rotation turned half round (the quaternion product with (0, 0, 1, 0), then scaled to length 1); flipped,
    // each bone takes its mirror bone's (the skeleton's 0x004A2C60, none: its own) rotation and translation with
    // the rotation's x and w and the translation's x negated. True.
    bool fetch_pose(uint8_t* pose) override;

    bool mirrored_{false};    // +0x14
    bool has_pivoted_{false}; // +0x15
    uint8_t pad16[2]{};
};
static_assert(offsetof(FlipRotateComponent, mirrored_) == 0x14);
static_assert(sizeof(FlipRotateComponent) == 0x18);

} // namespace objects
