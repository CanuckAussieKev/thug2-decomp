#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "base/core/api.h"

// NOT ORIGINAL - lines a tool draws in the 3D view, tested against the depth the world left (the level editor's rails
// and gizmos: a line behind a wall can be drawn faint, or not at all, where - and only where - it is hidden).
//
// A UI hands its lines over once a frame (set_lines); the renderer draws them in the next frame's 3D view, after the
// world's passes and before the full-screen effects and the 2D, with that frame's camera (render_pc/depth_lines_draw):
// each line a strip `width` pixels wide facing the camera, drawn without writing depth, then the lines are dropped. A
// UI that stops handing lines over has none drawn. Lines are pulled a little toward the camera, so a line lying on a
// surface (a rail on a ledge's edge) isn't hidden by that surface.
namespace render::depth_lines {

// How a line meets what the world drew (in the order they are drawn).
enum class Depth : uint8_t {
    kBehind,  // drawn where something nearer was drawn (the part of it that is hidden)
    kInFront, // drawn where nothing nearer was drawn (the part of it that shows)
    kAlways,  // drawn over everything
};

struct Line {
    std::array<float, 3> from{};
    std::array<float, 3> to{};
    uint32_t argb{}; // its colour and opacity
    float width = 1.0F; // pixels
    Depth depth = Depth::kInFront;
};

// --- the UI's side (the plugin API) ---

// The lines of the next frame (in place of those handed over before).
THUG2_API void set_lines(std::span<const Line> lines);

// --- the renderer's side ---

// The lines to draw now (the behind lines first, then those in front, then the ones over everything).
std::span<const Line> pending();
// Drawn (or a frame without the 3D view): dropped.
void clear();

} // namespace render::depth_lines
