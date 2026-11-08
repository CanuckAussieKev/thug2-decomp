#pragma once

#include <cstdint>
#include <span>

#include "base/core/api.h"

namespace render {
class Geom;
}

// NOT ORIGINAL - outlines round geometry a tool marks in the 3D view (the level editor's selection and hover: a stroke
// round each marked piece's silhouette, as Unity, Unreal and Blender show a selection).
//
// A UI hands its marks over once a frame (set_outlines), in the frame they are drawn in (before the 3D passes - the
// level editor's frame runs then); the renderer outlines them in that frame's 3D view after the world (render_pc/
// outline_draw), in every view of a split view, with each view's camera: each marked geometry's shown meshes drawn as a
// flat colour into a mask the frame's size (where they are hidden behind something: its hidden colour; where they show:
// its colour), then the pixels just outside the mask - a stroke a few pixels wide - coloured from it over the frame.
// Nothing of the geometry changes. A geometry with an instance (an object's model) is drawn where its instance is, with
// its bones; one without (a level's sector) as the world pass draws it.
namespace render::outline_draws {

struct Outline {
    Geom* geom = nullptr;
    // ARGB: the stroke where the geometry shows, and where it is behind something (alpha 0: no stroke there). An alpha
    // below 0x40 is taken as 0x40.
    uint32_t colour = 0;
    uint32_t hidden_colour = 0;
};

// --- the UI's side (the plugin API) ---

// The outlines of this frame (in place of those handed over before; drawn in the order given - where two meet, the
// later's colour). The geometries must stay until the frame is drawn.
THUG2_API void set_outlines(std::span<const Outline> outlines);
// Whether the renderer drew the last outlines it was given (false: it can't here - the frame is multisampled, the
// mask couldn't be made - so the UI shows its marks another way).
THUG2_API bool outlines_drawn();

// --- the renderer's side ---

std::span<const Outline> pending();
void clear();
void note_drawn(bool drawn);

} // namespace render::outline_draws
