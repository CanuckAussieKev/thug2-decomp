#pragma once

// NOT ORIGINAL - round W2: the light store round the camera for the things the level's vertices don't carry - the grass's
// blades, the clutter's items (docs/design/vulkan.md "Round W2: grass and clutter lit by the one light"). The light
// field (rt_light_field.h - Round O's probes: what a surface facing n at a point receives from the sky, the bounce and the
// lamps, linear, in the global unit) sampled at its own cells' centres in a box round the camera, kLightWindowCells cells
// a side across the ground and kLightWindowLayers up, each kLightFieldCell units - one first-order probe a cell, the
// whole indirect light (the sky's, the bounce's and the lamps', all of it: the lamps pass lights only the surfaces drawn
// before it). A backend puts it on the device (gpu_vulkan/vulkan_light_window.h - its GLSL twin) and its passes read a
// point's probe trilinearly between the cells' centres, as light_at does between the field's own cells. The direct light
// (the sun's or the moon's) is not in it: each thing traces its own visibility of it.
//
// Made on a worker from the published field when the camera has moved an eighth of the window from its centre or the
// field changed (each time the lighting changes). Texels: kLightWindowPlanes planes of RGBA half floats, plane c holding
// channel c's (c0, c1.x, c1.y, c1.z) - texel (x, (plane x layers + layer) x cells + z) of one 2D image. API-neutral.

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_light_field.h"

namespace render::rt {

inline constexpr int kLightWindowCells = 96;  // (x 128 units: 312 m across - the grass's farthest distance, 200 m, inside)
inline constexpr int kLightWindowLayers = 16; // (52 m up and down)
inline constexpr float kLightWindowCell = kLightFieldCell;
inline constexpr int kLightWindowPlanes = 3;
inline constexpr int kLightWindowRecentre = kLightWindowCells / 8;

struct LightWindow {
    std::array<int, 3> first{};   // the first cell (absolute, in cells)
    const LightField* field = nullptr; // the field it was made from (identity only - never read through)
    std::vector<uint16_t> texels; // half floats, RGBA, the planes stacked (cells x (planes x layers x cells) texels)
    float least = 0.0F;           // (the log's) the smallest and largest c0 met
    float most = 0.0F;
};

// The window's first cell for a centre (the centre in its middle).
[[nodiscard]] std::array<int, 3> light_window_first(const FieldPoint& centre);
// The window round `centre` from `field` (each cell's probe: light_at at its centre, the indirect light with every lamp).
[[nodiscard]] LightWindow light_window(const LightField& field, const FieldPoint& centre);
// A point's probe as the device reads the window (the twin of the GLSL's window_probe): trilinear between the cells'
// centres, the half floats as stored; outside the window its nearest edge cell's.
[[nodiscard]] ProbeL1 light_window_probe(const LightWindow& window, const FieldPoint& point);

} // namespace render::rt
