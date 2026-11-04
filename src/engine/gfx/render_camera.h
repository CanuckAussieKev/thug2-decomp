#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

// The frame's camera as the renderer keeps it (render::Renderer's: renderer.h) - its state, the view's shape for
// culling, the device's viewport and a camera's lens. The per-frame 3D render that sets them is the graphics backend's
// (draw_frame.h).
namespace render {

constexpr size_t kGapBytes = 0x0C;        // bytes between the camera position and its orientation, not understood
constexpr size_t kPositionOffset = 0x1C;
constexpr size_t kBackwardOffset = 0x34;

// The camera's state (globals, 0x00786CA8).
struct RenderCamera {
    float near_z_;                // 0x00786CA8
    float far_z_;                 // 0x00786CAC
    float angle;                  // 0x00786CB0: horizontal field of view, degrees
    float near_width_;            // 0x00786CB4: the view's size at the near plane
    float near_height_;           // 0x00786CB8
    uint8_t custom_transforms_;   // 0x00786CBC: the view / projection set by hand, not the camera's (shadow_base.cpp)
    bool clear_colour_and_depth_; // 0x00786CBD (a global the camera's lie around)
    uint8_t letterbox_active_;    // 0x00786CBE
    uint8_t unknown17;
    uint32_t background_colour_; // 0x00786CC0 (another)
    std::array<float, 3> position;           // 0x00786CC4
    std::array<uint8_t, kGapBytes> unknown28;
    std::array<float, 3> backward; // 0x00786CDC: the orientation's row 2
    std::array<float, 3> up;       // 0x00786CE8: row 1
};
static_assert(offsetof(RenderCamera, position) == kPositionOffset, "0x00786CC4");
static_assert(offsetof(RenderCamera, backward) == kBackwardOffset, "0x00786CDC");

// The view's half-angle shape, for culling (0x00786E18).
struct ViewShape {
    float tan_x_; // tan(half the horizontal angle)
    float tan_y_; // -tanX / (aspect / 1.1): the vertical one, negated
    float sin_x_; // sin and cos of the two half-angles
    float sin_y_;
    float cos_x_;
    float cos_y_;
};

// The device's viewport (D3DVIEWPORT9, 0x00786C90).
struct DeviceViewport {
    uint32_t x, y, width, height;
    float min_z_, max_z_;
};

// A camera's lens: its angle, its aspect and its zoom.
struct CameraLens {
    float angle{};
    float aspect{};
    float zoom{};
};

} // namespace render
