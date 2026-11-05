// NOT ORIGINAL - what the level editor draws in the world: lines, boxes, the node gizmos and markers, the
// highlights, and the frame's overlay (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "base/core/enum_ops.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_arrange.h"
#include "base/maths/vec2.h"
#include "base/core/format.h"
#include "editor/level_editor_arcs.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_grind_preview.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_ruler.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/maths/rect.h"
#include "base/maths/trig.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr float kArrowHeadLength = 12.0F;   // pixels, at the reference view height
constexpr int kLadderPieceCount = 8;        // a ladder segment is drawn dashed: eight pieces, every other one
constexpr float kLadderPieceCountF = 8.0F;
constexpr float kRouteArrowMinLength =
    30.0F;                              // a route segment shorter than this (times the icon scale) has no arrowhead
constexpr float kRouteArrowWing = 0.6F; // the arrowhead is this wide for its length
constexpr float kPickDotRadius = 4.0F;
constexpr float kLabelOffset = 8.0F;
constexpr float kLabelDrop = 2.0F;
constexpr uint32_t kLabelColour = 0xFFFFFFFF; // ARGB
constexpr float kRailPointRange = 1500.0F;    // a rail shows its points within this distance
constexpr float kRailDotRadius = 2.5F;
constexpr float kSpawnArrowWidth = 2.5F;
constexpr float kMinArrowPixels = 4.0F;
constexpr float kSpawnArrowWing = 0.55F;
constexpr float kOutlineGrow = 1.5F; // the outline circle is this much bigger
constexpr float kHorseLabelOffset = 3.0F;
constexpr float kHorseLabelRise = 12.0F;
constexpr uint32_t kHorseColour = 0xFFF0943C; // ARGB
constexpr float kCrownHeight = 1.4F;          // the crown diamond is this many radii tall
constexpr std::size_t kOrderTextSize = 16;
constexpr float kCrownLabelOffset = 4.0F;
constexpr float kPoleHeight = 3.2F;          // the pennant pole, in radii
constexpr uint32_t kPoleColour = 0xFFE0E0E0; // ARGB
constexpr float kMarkerLineWidth = 2.0F;
constexpr float kPennantReach = 2.2F; // in radii
constexpr float kPennantMiddle = 2.6F;
constexpr float kPennantBottom = 2.0F;
constexpr float kSparkInner = 1.4F; // the spark strokes start and end this many radii out
constexpr float kSparkOuter = 2.1F;
constexpr float kSparkWidth = 1.5F;
constexpr uint32_t kHalfAlpha = 0x80000000;
constexpr float kSpeakerBack = 0.3F; // the speaker box, in radii
constexpr float kSpeakerConeSpread = 1.1F;
constexpr float kRingNearRange = 2000.0F; // a fire-off square shows its radius rings within this distance
constexpr float kDiamondRadius = 0.8F;    // an object marker is a diamond of this many radii
constexpr float kDiamondWidth = 1.5F;
constexpr float kCoreRadius = 0.55F;
constexpr float kRingGrow = 5.0F; // the selection ring is this much bigger
constexpr float kDashLength = 6.0F;
constexpr float kDashGap = 4.0F;

constexpr uint32_t kGroupBoxColour = 0xFFFFFF00;  // ARGB
constexpr uint32_t kMemberBoxColour = 0xFF00FFFF; // ARGB
constexpr std::size_t kMaxBoxesDrawn = 64;

constexpr float kArrowGrab = 8.0F;     // (pixels from a move arrow that grab it)

// NOT ORIGINAL grouping - the overlay the UI draws over the view (ui::Overlay), filled each frame (was in the pick
// cycle's record; only this piece uses it). Made on first use, never destroyed.
ui::Overlay& drawn_overlay()
{
    static core::NoDestroy<ui::Overlay> overlay;
    return overlay.get();
}

} // namespace
namespace internal {

// --- the gizmo's handles ---
//
// Move: three arrows from the selection's position along x (red), y (green) and z (blue) - the world's or, Local, the
// selection's own; Rotate: the heading ring about the pivot; Scale: a cube at each axis' end and one at the pivot. A
// fifth of the camera's distance long; the handle under the cursor (within 8 pixels) lights up, and dragging it moves
// the selection along that axis only, turns it or scales it.

// The handle under the back-buffer pixel (x, y): -1 for none.
int arrow_at(const TickCycle& frame, maths::Vec2 at)
{
    return xform::handle_at(
        gizmo_now(), [&](V3 point, float* px, float* py) { return get_to_pixel(frame, point, px, py); }, at,
        kArrowGrab);
}

} // namespace internal
namespace {

void add_pixel_line(maths::Rect line, uint32_t color, std::vector<LineVertex>* out)
{
    const auto ax = line.x0;
    const auto ay = line.y0;
    const auto bx = line.x1;
    const auto by = line.y1;
    out->push_back({.x = ax, .y = ay, .z = 0.0F, .rhw = 1.0F, .color = color, .u = 0.0F, .v = 0.0F});
    out->push_back({.x = bx, .y = by, .z = 0.0F, .rhw = 1.0F, .color = color, .u = 0.0F, .v = 0.0F});
}

} // namespace
namespace internal {

// A world line; `thickness` 3: drawn three times a pixel apart across it.
void add_line(V3 a, V3 b, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out, int thickness)
{
    if (!clip_to_near(fly_camera().pos_, frame, kNearZ + kNearPlaneMargin, &a, &b)) {
        return;
    }
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_to_pixel(frame, a, &ax, &ay) || !get_to_pixel(frame, b, &bx, &by)) {
        return;
    }
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = std::sqrt((dx * dx) + (dy * dy));
    const float nx = length > 0.0F ? -dy / length : 0.0F;
    const float ny = length > 0.0F ? dx / length : 0.0F;
    for (int k = -(thickness / 2); k <= thickness / 2; ++k) {
        add_pixel_line({.x0 = ax + (nx * static_cast<float>(k)),
                        .y0 = ay + (ny * static_cast<float>(k)),
                        .x1 = bx + (nx * static_cast<float>(k)),
                        .y1 = by + (ny * static_cast<float>(k))},
                       color, out);
    }
}

} // namespace internal
namespace {

void add_box(const OrientedBox& box, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out)
{
    std::array<V3, kBoxCornerCount> corners{};
    box_corners(box.origin, box.axes, box.min, box.max, corners);
    for (int i = 0; i < kBoxCorners; ++i) {
        for (int bit = 1; bit < kBoxCorners; bit <<= 1) {
            if ((i & bit) == 0) {
                add_line(core::at(corners, i), core::at(corners, i | bit), frame, color, out);
            }
        }
    }
}

// An arrow: its shaft (three pixels wide) and a head of two strokes 12 pixels long.
void add_arrow(V3 from, V3 to, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out)
{
    add_line(from, to, frame, color, out, 3);
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_to_pixel(frame, from, &ax, &ay) || !get_to_pixel(frame, to, &bx, &by)) {
        return;
    }
    float dx = bx - ax;
    float dy = by - ay;
    float const length = std::sqrt((dx * dx) + (dy * dy));
    if (length < 1.0F) {
        return;
    }
    dx /= length;
    dy /= length;
    for (const float side : {-1.0F, 1.0F}) {
        const float hx = (-dx * 0.9F) + (-dy * 0.45F * side);
        const float hy = (-dy * 0.9F) + (dx * 0.45F * side);
        add_pixel_line({.x0 = bx, .y0 = by, .x1 = bx + (hx * kArrowHeadLength), .y1 = by + (hy * kArrowHeadLength)},
                       color, out);
    }
}

} // namespace
namespace internal {

// --- the nodes' gizmos (level_editor_nodes.h): drawn through the UI's overlay, picked by screen distance ---
//
// A point (spawns, crowns, flags, sounds, particles, objects, others) is an icon in its colour - a spawn with its
// heading -, a chain (rails, ladders, paths) lines along its links, a volume (lights, proximity nodes) an icon with its
// radius as rings when it's near, hovered or selected. Only what's in front of the camera, within a distance (6000 for
// points, 4000 for chains, 2500 for objects and particles) unless selected.

// Back buffer pixels per pixel at 720 lines (the icons' size).
float pixel_scale()
{
    const float gizmo = static_cast<float>(prefs::get_pull().gizmo_pct_) / 100.0F; // ([editor] gizmo_pct)
    return (core::at(drawn_view().viewport_, 3) > 0
                ? static_cast<float>(core::at(drawn_view().viewport_, 3)) / kReferenceViewHeight
                : 1.0F) *
           gizmo;
}

} // namespace internal
namespace {

constexpr uint32_t kSelectedGizmo = 0xFFFFFFFF, kHoverGizmo = 0xFFFFE070;

// The view the gizmos are drawn in: the frame, the overlay, the pixel scale and the viewport grown by 40 pixels (a
// gizmo outside it isn't drawn).
struct GizmoView {
    const TickCycle* frame{};
    ui::Overlay* overlay{};
    float scale{};
    float left{};
    float top{};
    float right{};
    float bottom{};
};

// One node's gizmo: the node, its index, selected / hovered, its (apparent) distance.
struct NodeGizmo {
    const nodes::Elem* elem{};
    int index{};
    bool selected{};
    bool hovered{};
    float distance{};
};

// A line on the overlay (pixels).
auto line_drawer(ui::Overlay& overlay)
{
    return [&overlay](float ax, float ay, float bx, float by, uint32_t colour, float thickness) {
        overlay.lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = colour, .thickness = thickness});
    };
}

// A ring about `centre` in the plane of `u` and `v` (unit, at right angles): its sides as world lines (clipped to the
// near plane).
auto ring_drawer(const GizmoView& view)
{
    return [&view](V3 centre, float radius, V3 u, V3 v, uint32_t colour) {
        const TickCycle& frame = *view.frame;
        const auto line = line_drawer(*view.overlay);
        auto world_line = [&](V3 a, V3 b, uint32_t colour_of_line, float thickness) {
            if (!clip_to_near(fly_camera().pos_, frame, kNearZ + kNearPlaneMargin, &a, &b)) {
                return;
            }
            float ax = 0.0F;
            float ay = 0.0F;
            float bx = 0.0F;
            float by = 0.0F;
            if (get_to_pixel(frame, a, &ax, &ay) && get_to_pixel(frame, b, &bx, &by)) {
                line(ax, ay, bx, by, colour_of_line, thickness);
            }
        };
        constexpr int kSides = 32;
        V3 previous = centre + u * radius;
        for (int i = 1; i <= kSides; ++i) {
            const float angle = 2.0F * maths::kPi * static_cast<float>(i) / kSides;
            const V3 next = centre + u * (radius * std::cos(angle)) + v * (radius * std::sin(angle));
            world_line(previous, next, colour, 1.0F);
            previous = next;
        }
    };
}

// A path's way: an arrowhead at the middle of its segment (pixels).
void add_route_arrow(const GizmoView& view, const ui::Seg& segment)
{
    const float ax = segment.ax;
    const float ay = segment.ay;
    const float bx = segment.bx;
    const float by = segment.by;
    const float scale = view.scale;
    float dx = bx - ax;
    float dy = by - ay;
    float const length = std::sqrt((dx * dx) + (dy * dy));
    if (length > kRouteArrowMinLength * scale) {
        dx /= length;
        dy /= length;
        const float mx = (ax + bx) * 0.5F;
        const float my = (ay + by) * 0.5F;
        const float half_size = 7.0F * scale;
        view.overlay->triangles.push_back({.ax = mx + (dx * half_size),
                                           .ay = my + (dy * half_size),
                                           .bx = mx - (dx * half_size) - (dy * half_size * kRouteArrowWing),
                                           .by = my - (dy * half_size) + (dx * half_size * kRouteArrowWing),
                                           .cx = mx - (dx * half_size) + (dy * half_size * kRouteArrowWing),
                                           .cy = my - (dy * half_size) - (dx * half_size * kRouteArrowWing),
                                           .argb = segment.argb});
    }
}

// A chain's segment to the linked node (`linked_elem`): drawn (a ladder's dashed, a path's with its arrow) and
// pickable - unless both ends are far, it is behind the near plane or off the view.
void add_chain_segment(const GizmoView& view, const NodeGizmo& gizmo, const nodes::Elem& linked_elem)
{
    const TickCycle& frame = *view.frame;
    const nodes::Elem& elem = *gizmo.elem;
    const auto line = line_drawer(*view.overlay);
    constexpr float kChainCutoff = 4000.0F;
    if (!linked_elem.has_pos_) {
        return;
    }
    const V3 linked_delta = linked_elem.pos - fly_camera().pos_;
    if (!gizmo.selected && !gizmo.hovered && gizmo.distance > kChainCutoff &&
        view_cam::apparent(std::sqrt(get_dot(linked_delta, linked_delta))) > kChainCutoff) {
        return;
    }
    V3 point_a = elem.pos;
    V3 point_b = linked_elem.pos;
    if (!clip_to_near(fly_camera().pos_, frame, kNearZ + kNearPlaneMargin, &point_a, &point_b)) {
        return;
    }
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_to_pixel(frame, point_a, &ax, &ay) || !get_to_pixel(frame, point_b, &bx, &by)) {
        return;
    }
    if ((ax < view.left && bx < view.left) || (ax > view.right && bx > view.right) ||
        (ay < view.top && by < view.top) || (ay > view.bottom && by > view.bottom)) {
        return;
    }
    uint32_t colour = elem.colour;
    if (gizmo.selected) {
        colour = kSelectedGizmo;
    } else if (gizmo.hovered) {
        colour = kHoverGizmo;
    }
    const float plain_thickness = elem.family == nodes::Family::kRail ? 2.0F : 1.5F;
    const float thickness = gizmo.selected || gizmo.hovered ? 3.0F : plain_thickness;
    if (elem.family == nodes::Family::kLadder) {
        for (int piece = 0; piece < kLadderPieceCount; piece += 2) { // (dashed)
            line(ax + ((bx - ax) * static_cast<float>(piece) / kLadderPieceCountF),
                 ay + ((by - ay) * static_cast<float>(piece) / kLadderPieceCountF),
                 ax + ((bx - ax) * static_cast<float>(piece + 1) / kLadderPieceCountF),
                 ay + ((by - ay) * static_cast<float>(piece + 1) / kLadderPieceCountF), colour, thickness);
        }
    } else {
        line(ax, ay, bx, by, colour, thickness);
    }
    if (elem.family == nodes::Family::kRouteWay) {
        // (the way it goes: an arrowhead at the middle)
        add_route_arrow(view, {.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = colour, .thickness = thickness});
    }
    const V3 middle = (point_a + point_b) * 0.5F;
    drawn_view().pickables_.push_back({.node = gizmo.index,
                                       .ax = ax,
                                       .ay = ay,
                                       .bx = bx,
                                       .by = by,
                                       .segment = true,
                                       .depth = -get_dot(middle - fly_camera().pos_, frame.back),
                                       .depth_a_ = -get_dot(point_a - fly_camera().pos_, frame.back),
                                       .depth_b_ = -get_dot(point_b - fly_camera().pos_, frame.back)});
}

// A chain's node: its segments to its links; selected or hovered, a dot and its name; a rail's point, near, a dot
// of its own (picked before its segments - a point moves by itself or with its whole rail).
void add_chain_gizmo(const GizmoView& view, const NodeGizmo& gizmo, const std::vector<nodes::Elem>& nodes)
{
    const TickCycle& frame = *view.frame;
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const nodes::Elem& elem = *gizmo.elem;
    for (const int link : elem.links) {
        add_chain_segment(view, gizmo, core::at(nodes, link));
    }
    if (gizmo.selected || gizmo.hovered) {
        float px = 0.0F;
        float py = 0.0F;
        if (get_to_pixel(frame, elem.pos, &px, &py)) {
            overlay.circles.push_back({.x = px,
                                       .y = py,
                                       .radius = kPickDotRadius * scale,
                                       .argb = gizmo.selected ? kSelectedGizmo : kHoverGizmo,
                                       .filled = true,
                                       .thickness = 1.0F});
            overlay.texts.push_back({.x = px + (kLabelOffset * scale),
                                     .y = py - (kLabelOffset * scale),
                                     .argb = kLabelColour,
                                     .text = nodes::caption(elem.name)});
        }
    }
    if (elem.family == nodes::Family::kRail && (gizmo.distance < kRailPointRange || gizmo.selected)) {
        float px = 0.0F;
        float py = 0.0F;
        const V3 front = elem.pos - fly_camera().pos_;
        if (-get_dot(front, frame.back) > kNearZ && get_to_pixel(frame, elem.pos, &px, &py) && px >= view.left &&
            px <= view.right && py >= view.top && py <= view.bottom) {
            if (!gizmo.selected && !gizmo.hovered) {
                overlay.circles.push_back({.x = px,
                                           .y = py,
                                           .radius = kRailDotRadius * scale,
                                           .argb = elem.colour,
                                           .filled = true,
                                           .thickness = 1.0F});
            }
            drawn_view().pickables_.push_back({.node = gizmo.index,
                                               .ax = px,
                                               .ay = py,
                                               .bx = px,
                                               .by = py,
                                               .segment = false,
                                               .depth = -get_dot(front, frame.back)});
        }
    }
}

// Where a marker is drawn: its pixel, its depth, its radius and colour.
struct MarkerAt {
    float px{};
    float py{};
    float depth{};
    float radius{};
    uint32_t colour{};
};

constexpr uint32_t kMarkerOutline = 0xC0101012;

// A spawn point: a disc and its heading (an arrow along the ground); "H" for a horse spot.
void add_spawn_marker(const GizmoView& view, const nodes::Elem& elem, const MarkerAt& marker)
{
    const TickCycle& frame = *view.frame;
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const auto line = line_drawer(overlay);
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    const float length = std::max(30.0F, std::min(200.0F, marker.depth * 0.08F));
    const V3 tip = elem.pos + elem.forward * length;
    float tx = 0.0F;
    float ty = 0.0F;
    if (get_to_pixel(frame, tip, &tx, &ty)) {
        line(px, py, tx, ty, colour, kSpawnArrowWidth);
        float dx = tx - px;
        float dy = ty - py;
        float const segment_length = std::sqrt((dx * dx) + (dy * dy));
        if (segment_length > kMinArrowPixels) {
            dx /= segment_length;
            dy /= segment_length;
            const float half_size = 8.0F * scale;
            overlay.triangles.push_back({.ax = tx + (dx * half_size * kHalf),
                                         .ay = ty + (dy * half_size * kHalf),
                                         .bx = tx - (dx * half_size) - (dy * half_size * kSpawnArrowWing),
                                         .by = ty - (dy * half_size) + (dx * half_size * kSpawnArrowWing),
                                         .cx = tx - (dx * half_size) + (dy * half_size * kSpawnArrowWing),
                                         .cy = ty - (dy * half_size) - (dx * half_size * kSpawnArrowWing),
                                         .argb = colour});
        }
    }
    overlay.circles.push_back({.x = px,
                               .y = py,
                               .radius = radius + (kOutlineGrow * scale),
                               .argb = kMarkerOutline,
                               .filled = true,
                               .thickness = 1.0F});
    overlay.circles.push_back({.x = px, .y = py, .radius = radius, .argb = colour, .filled = true, .thickness = 1.0F});
    if (core::any(elem.restart_types_ &
                  nodes::SpawnPointKind::kHorse)) { // (a HORSE spot: restart_types has Horse - the feature map)
        overlay.texts.push_back({.x = px + radius + (kHorseLabelOffset * scale),
                                 .y = py - radius - (kHorseLabelRise * scale),
                                 .argb = kHorseColour,
                                 .text = "H"});
    }
}

// A crown: a gold diamond and its index.
void add_crown_marker(const GizmoView& view, const nodes::Elem& elem, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    overlay.triangles.push_back({.ax = px,
                                 .ay = py - (radius * kCrownHeight),
                                 .bx = px + radius,
                                 .by = py,
                                 .cx = px - radius,
                                 .cy = py,
                                 .argb = colour});
    overlay.triangles.push_back({.ax = px - radius,
                                 .ay = py,
                                 .bx = px + radius,
                                 .by = py,
                                 .cx = px,
                                 .cy = py + (radius * kCrownHeight),
                                 .argb = colour});
    std::array<char, kOrderTextSize> text{};
    if (elem.order >= 0) {
        text::format_to(text, "#%d", elem.order);
    } else {
        text::format_to(text, "hidden");
    }
    overlay.texts.push_back({.x = px + radius + (kCrownLabelOffset * view.scale),
                             .y = py - (radius * kCrownHeight),
                             .argb = colour,
                             .text = text.data()});
}

// A light: a bulb in its colour; rings at its radii when selected or hovered.
void add_light_marker(const GizmoView& view, const NodeGizmo& gizmo, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const auto line = line_drawer(overlay);
    const auto ring = ring_drawer(view);
    const nodes::Elem& elem = *gizmo.elem;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    const V3 x_axis = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const V3 z_axis = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    overlay.circles.push_back({.x = px,
                               .y = py,
                               .radius = radius + (kOutlineGrow * scale),
                               .argb = kMarkerOutline,
                               .filled = true,
                               .thickness = 1.0F});
    overlay.circles.push_back({.x = px, .y = py, .radius = radius, .argb = colour, .filled = true, .thickness = 1.0F});
    for (int ray = 0; ray < 4; ++ray) {
        const float angle = (maths::kPi * 0.25F) + (maths::kPi * 0.5F * static_cast<float>(ray));
        const float cx = std::cos(angle);
        const float cy = std::sin(angle);
        line(px + (cx * radius * kSparkInner), py + (cy * radius * kSparkInner), px + (cx * radius * kSparkOuter),
             py + (cy * radius * kSparkOuter), colour, kSparkWidth);
    }
    if ((gizmo.selected || gizmo.hovered) && elem.radius > 0.0F) {
        ring(elem.pos, elem.radius, x_axis, z_axis, colour);
        if (elem.inner_radius_ > 0.0F && elem.inner_radius_ < elem.radius) {
            ring(elem.pos, elem.inner_radius_, x_axis, z_axis, (colour & kRgbMask) | kHalfAlpha);
        }
    }
}

// A sound: a speaker - a box and its cone.
void add_sound_marker(const GizmoView& view, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const auto line = line_drawer(overlay);
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    line(px - radius, py - (radius * kHalf), px - (radius * kSpeakerBack), py - (radius * kHalf), colour,
         kMarkerLineWidth);
    line(px - radius, py + (radius * kHalf), px - (radius * kSpeakerBack), py + (radius * kHalf), colour,
         kMarkerLineWidth);
    line(px - radius, py - (radius * kHalf), px - radius, py + (radius * kHalf), colour, kMarkerLineWidth);
    overlay.triangles.push_back({.ax = px - (radius * kSpeakerBack),
                                 .ay = py - (radius * kHalf),
                                 .bx = px + radius,
                                 .by = py - (radius * kSpeakerConeSpread),
                                 .cx = px + radius,
                                 .cy = py + (radius * kSpeakerConeSpread),
                                 .argb = colour});
    overlay.triangles.push_back({.ax = px - (radius * kSpeakerBack),
                                 .ay = py - (radius * kHalf),
                                 .bx = px + radius,
                                 .by = py + (radius * kSpeakerConeSpread),
                                 .cx = px - (radius * kSpeakerBack),
                                 .cy = py + (radius * kHalf),
                                 .argb = colour});
}

// A fire-off point: a square; its radius as three rings when near.
void add_fire_off_marker(const GizmoView& view, const NodeGizmo& gizmo, const MarkerAt& marker)
{
    const auto line = line_drawer(*view.overlay);
    const auto ring = ring_drawer(view);
    const nodes::Elem& elem = *gizmo.elem;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    const V3 x_axis = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const V3 y_axis = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const V3 z_axis = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    line(px - radius, py - radius, px + radius, py - radius, colour, kMarkerLineWidth);
    line(px + radius, py - radius, px + radius, py + radius, colour, kMarkerLineWidth);
    line(px + radius, py + radius, px - radius, py + radius, colour, kMarkerLineWidth);
    line(px - radius, py + radius, px - radius, py - radius, colour, kMarkerLineWidth);
    if (elem.radius > 0.0F && (gizmo.selected || gizmo.hovered || gizmo.distance < kRingNearRange)) {
        const uint32_t faint = (colour & 0x00FFFFFF) | 0x90000000;
        ring(elem.pos, elem.radius, x_axis, z_axis, faint);
        ring(elem.pos, elem.radius, x_axis, y_axis, faint);
        ring(elem.pos, elem.radius, z_axis, y_axis, faint);
    }
}

// The marker of a node of its family.
void add_family_marker(const GizmoView& view, const NodeGizmo& gizmo, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const auto line = line_drawer(overlay);
    const nodes::Elem& elem = *gizmo.elem;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    switch (elem.family) {
    case nodes::Family::kSpawn:
        add_spawn_marker(view, elem, marker);
        break;
    case nodes::Family::kCrown:
        add_crown_marker(view, elem, marker);
        break;
    case nodes::Family::kBitMark:
        // A pole and a pennant in its team's colour.
        line(px, py, px, py - (radius * kPoleHeight), kPoleColour, kMarkerLineWidth);
        overlay.triangles.push_back({.ax = px,
                                     .ay = py - (radius * kPoleHeight),
                                     .bx = px + (radius * kPennantReach),
                                     .by = py - (radius * kPennantMiddle),
                                     .cx = px,
                                     .cy = py - (radius * kPennantBottom),
                                     .argb = colour});
        overlay.circles.push_back(
            {.x = px, .y = py, .radius = radius * kHalf, .argb = colour, .filled = true, .thickness = 1.0F});
        break;
    case nodes::Family::kLight:
        add_light_marker(view, gizmo, marker);
        break;
    case nodes::Family::kSound:
        add_sound_marker(view, marker);
        break;
    case nodes::Family::kParticle:
        // A spark: three strokes.
        for (int stroke = 0; stroke < 3; ++stroke) {
            const float angle = maths::kPi * static_cast<float>(stroke) / 3.0F;
            const float cx = std::cos(angle) * radius;
            const float cy = std::sin(angle) * radius;
            line(px - cx, py - cy, px + cx, py + cy, colour, kMarkerLineWidth);
        }
        break;
    case nodes::Family::kFireOff:
        add_fire_off_marker(view, gizmo, marker);
        break;
    case nodes::Family::kObject:
        // A small open diamond (the object itself shows).
        line(px, py - (radius * kDiamondRadius), px + (radius * kDiamondRadius), py, colour, kDiamondWidth);
        line(px + (radius * kDiamondRadius), py, px, py + (radius * kDiamondRadius), colour, kDiamondWidth);
        line(px, py + (radius * kDiamondRadius), px - (radius * kDiamondRadius), py, colour, kDiamondWidth);
        line(px - (radius * kDiamondRadius), py, px, py - (radius * kDiamondRadius), colour, kDiamondWidth);
        break;
    default:
        overlay.circles.push_back(
            {.x = px, .y = py, .radius = radius * kCoreRadius, .argb = colour, .filled = true, .thickness = 1.0F});
        break;
    }
}

// A point node: its family's marker (unless far or off the view), a ring and its name when selected or hovered;
// pickable.
void add_point_gizmo(const GizmoView& view, const NodeGizmo& gizmo, V3 delta)
{
    const TickCycle& frame = *view.frame;
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const nodes::Elem& elem = *gizmo.elem;
    const float cutoff =
        elem.family == nodes::Family::kObject || elem.family == nodes::Family::kParticle ? 2500.0F : 6000.0F;
    if (!gizmo.selected && !gizmo.hovered && gizmo.distance > cutoff) {
        return;
    }
    float px = 0.0F;
    float py = 0.0F;
    if (!get_to_pixel(frame, elem.pos, &px, &py) || px < view.left || px > view.right || py < view.top ||
        py > view.bottom) {
        return;
    }
    const float depth = -get_dot(delta, frame.back);
    const float radius = 6.0F * scale;
    const uint32_t colour = elem.hidden ? 0x90A0A4A8 : elem.colour; // (hidden: greyed)
    add_family_marker(view, gizmo, {.px = px, .py = py, .depth = depth, .radius = radius, .colour = colour});
    if (gizmo.selected || gizmo.hovered) {
        overlay.circles.push_back({.x = px,
                                   .y = py,
                                   .radius = radius + (kRingGrow * scale),
                                   .argb = gizmo.selected ? kSelectedGizmo : kHoverGizmo,
                                   .filled = false,
                                   .thickness = kMarkerLineWidth});
        overlay.texts.push_back({.x = px + radius + (kLabelOffset * scale),
                                 .y = py + (kLabelDrop * scale),
                                 .argb = kLabelColour,
                                 .text = nodes::caption(elem.name)});
    }
    drawn_view().pickables_.push_back(
        {.node = gizmo.index, .ax = px, .ay = py, .bx = px, .by = py, .segment = false, .depth = depth});
}

// A box being dragged (level_editor_organize.h): its outline, solid inside / dashed crossing.
void add_box_outline(ui::Overlay& overlay, float scale)
{
    organize::Rect2 box{};
    bool crossing = false;
    if (organize::box_outline(&box, &crossing)) {
        const uint32_t colour = crossing ? 0xFF8FD18FU : 0xFFFFC870U;
        const std::array<ui::Seg, 4> edges = {
            {{.ax = box.x0, .ay = box.y0, .bx = box.x1, .by = box.y0, .argb = colour, .thickness = 1.5F},
             {.ax = box.x1, .ay = box.y0, .bx = box.x1, .by = box.y1, .argb = colour, .thickness = 1.5F},
             {.ax = box.x1, .ay = box.y1, .bx = box.x0, .by = box.y1, .argb = colour, .thickness = 1.5F},
             {.ax = box.x0, .ay = box.y1, .bx = box.x0, .by = box.y0, .argb = colour, .thickness = 1.5F}}};
        for (const ui::Seg& edge : edges) {
            if (crossing) {
                view::add_dashed(&overlay.lines, edge, kDashLength * scale, kDashGap * scale);
            } else {
                overlay.lines.push_back(edge);
            }
        }
    }
}

void add_node_gizmos(const TickCycle& frame, int selected)
{
    drawn_view().pickables_.clear();
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const float scale = pixel_scale();
    ui::Overlay& overlay = drawn_overlay();
    const float left = static_cast<float>(core::at(drawn_view().viewport_, 0)) - 40.0F;
    const float top = static_cast<float>(core::at(drawn_view().viewport_, 1)) - 40.0F;
    const float right =
        static_cast<float>(core::at(drawn_view().viewport_, 0) + core::at(drawn_view().viewport_, 2)) +
        40.0F;
    const float bottom =
        static_cast<float>(core::at(drawn_view().viewport_, 1) + core::at(drawn_view().viewport_, 3)) +
        40.0F;
    const GizmoView view{.frame = &frame,
                         .overlay = &overlay,
                         .scale = scale,
                         .left = left,
                         .top = top,
                         .right = right,
                         .bottom = bottom};
    for (int i = 0; std::cmp_less(i, nodes.size()); ++i) {
        const nodes::Elem& elem = core::at(nodes, i);
        if (!elem.has_pos_ || ((tool_options().families_ & nodes::get_bit(elem.family)) == 0U) ||
            organize::gizmo_hidden(elem.name)) {
            continue;
        }
        const nodes::Shape shape = nodes::shape_of(elem.family);
        if (shape == nodes::Shape::kNone) {
            continue;
        }
        const V3 delta = elem.pos - fly_camera().pos_;
        const NodeGizmo gizmo{
            .elem = &elem,
            .index = i,
            .selected = i == selected,
            .hovered = i == picked_set().hover_node_,
            .distance = view_cam::apparent(std::sqrt(get_dot(delta, delta)))}; // (an ortho view: level_editor_camera.h)
        if (shape == nodes::Shape::kChain) {
            add_chain_gizmo(view, gizmo, nodes);
            continue;
        }
        add_point_gizmo(view, gizmo, delta);
    }
    // (the Outliner's selectable off: drawn, not picked - level_editor_organize.h)
    std::erase_if(drawn_view().pickables_, [&](const nodes::Pickable& pickable) {
        return pickable.node >= 0 && std::cmp_less(pickable.node, nodes.size()) &&
               !organize::gizmo_pickable(core::at(nodes, pickable.node).name);
    });
    add_box_outline(overlay, scale);
}

// --- highlighting (the things' own colour tinted: Scenery::get_tint) ---

constexpr uint32_t kHoverTint = 0x8098C8C8;       // RGBA: warmer and brighter
constexpr uint32_t kSelectedTintLow = 0x80D0A060; // a cyan pulse
constexpr uint32_t kSelectedTintHigh = 0x80FFD850;
constexpr uint32_t kBrowseTint = 0x8060E0E0; // the browser's entry: yellow
bool& hover_tinted() // (else their boxes are drawn)
{
    static bool hover_tinted{};
    return hover_tinted;
}
bool& selected_tinted()
{
    static bool selected_tinted{};
    return selected_tinted;
}

bool tint_sectors(const std::vector<uint32_t>& sectors, uint32_t rgba)
{
    bool all = true;
    for (const uint32_t checksum : sectors) {
        void* geom = scenery::sector_geometry(checksum);
        if (scenery::tintable(geom)) {
            scenery::get_tint({.id = checksum, .index = -1}, geom, rgba);
        } else {
            all = false;
        }
    }
    return all;
}

} // namespace
namespace internal {

void update_highlights()
{
    const uint32_t pulse = get_pulse(kSelectedTintLow, kSelectedTintHigh, SDL_GetTicks() / 1000.0);
    selected_tinted() = true;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    for (objects::GameEntity* object : objects) {
        selected_tinted() &= object_edit::get_tint(object, pulse);
    }
    if (objects.empty() && scenery::has_selection()) {
        selected_tinted() = tint_sectors(scenery::get_selection(), pulse);
    }
    hover_tinted() = true;
    objects::GameEntity* hover = drag_session().dragging_ ? nullptr : get_live(picked_set().hover_);
    if (hover && !is_picked(hover)) {
        hover_tinted() = object_edit::get_tint(hover, kHoverTint);
    } else if (!drag_session().dragging_ && (picked_set().hover_sector_ != 0U) &&
               !scenery::get_selected(picked_set().hover_sector_) &&
               !scenery::why_not_movable(picked_set().hover_sector_)) {
        hover_tinted() = tint_sectors((core::at(input::keys(), SDL_SCANCODE_LALT) != 0U) ||
                                              (core::at(input::keys(), SDL_SCANCODE_RALT) != 0U)
                                          ? std::vector<uint32_t>{picked_set().hover_sector_}
                                          : scenery::hover_group(picked_set().hover_sector_),
                                      kHoverTint);
    }
    // (the browser's entry where it is in the level)
    if (stored_browser().open && std::cmp_less(stored_browser().index, stored_browser().items.size())) {
        const uint32_t id = core::at(stored_browser().items, stored_browser().index).id;
        if (stored_browser().tab == 0) {
            tint_sectors({id}, kBrowseTint);
        } else if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id))) {
            object_edit::get_tint(object, kBrowseTint);
        }
    }
    scenery::end_tints(object_edit::resolve_tint, false);
}

// The hovered thing's box when it couldn't be tinted (with the UI always): the object, or the scenery's group (its
// one piece with Alt).
void add_hover_boxes(const TickCycle& frame, objects::GameEntity* hover, std::vector<LineVertex>& lines)
{
    OrientedBox box{};
    // (with the UI the hover's outline is always drawn too - what a click takes: the object, or the scenery's group -
    // its one piece with Alt)
    if (!hover_tinted() || ui::underway()) {
        if (hover && !is_picked(hover) && object_edit::model_box(hover, &box)) {
            add_box(box, frame, kGroupBoxColour, &lines);
        } else if (!drag_session().dragging_ && (picked_set().hover_sector_ != 0U) &&
                   !scenery::get_selected(picked_set().hover_sector_)) {
            const input::KeyState keys = input::keys();
            const bool one = (core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U) ||
                             (scenery::why_not_movable(picked_set().hover_sector_) != nullptr);
            const std::vector<uint32_t>& group = scenery::hover_group(picked_set().hover_sector_);
            if (one || group.empty()) {
                if (scenery::cuboid(picked_set().hover_sector_, &box)) {
                    add_box(box, frame, kGroupBoxColour, &lines);
                }
            } else {
                for (size_t i = 0; i < group.size() && i < kMaxBoxesDrawn; ++i) {
                    if (scenery::cuboid(core::at(group, i), &box)) {
                        add_box(box, frame, kGroupBoxColour, &lines);
                    }
                }
            }
        }
    }
}

// The selection's boxes when it couldn't be tinted: its objects', else its scenery pieces'.
void add_selected_boxes(const TickCycle& frame, const std::vector<objects::GameEntity*>& objects,
                        std::vector<LineVertex>& lines)
{
    OrientedBox box{};
    if (!selected_tinted()) {
        for (objects::GameEntity* object : objects) {
            if (object_edit::model_box(object, &box)) {
                add_box(box, frame, kMemberBoxColour, &lines);
            }
        }
        for (size_t i = 0; objects.empty() && i < scenery::get_selection().size() && i < kMaxBoxesDrawn; ++i) {
            if (scenery::cuboid(core::at(scenery::get_selection(), i), &box)) {
                add_box(box, frame, kMemberBoxColour, &lines);
            }
        }
    }
}

// The gizmo (level_editor_transform.h): Move's arrows, Rotate's ring, Scale's cubes - while dragging with one, that
// one only (a modal Rotate / Scale: the whole gizmo).
void add_gizmo_lines(const TickCycle& frame, std::vector<LineVertex>& lines)
{
    if (have_selection() &&
        (!drag_session().dragging_ || drag_session().axis_ >= 0 || transform_session().drag_mode_ != xform::Mode::kShiftBy)) {
        const xform::Gizmo gizmo = gizmo_now();
        for (const xform::Segment& segment : xform::gizmo_segments(gizmo)) {
            if (drag_session().dragging_ && drag_session().axis_ >= 0 && segment.handle != drag_session().axis_) {
                continue;
            }
            const uint32_t colour = xform::handle_colour(segment.handle, segment.handle == drag_session().hover_axis_ ||
                                                                             segment.handle == drag_session().axis_);
            if (segment.arrow) {
                add_arrow(segment.a, segment.b, frame, colour, &lines);
            } else {
                add_line(segment.a, segment.b, frame, colour, &lines,
                         segment.handle == 1 && gizmo.mode == xform::Mode::kSpinBy ? 3 : 1);
            }
        }
    }
}

// With the UI: the overlays of every tool and the lines through ImGui's draw list, with the panels
// (level_editor_ui.h).
void draw_with_ui(const TickCycle& frame, const std::vector<objects::GameEntity*>& objects,
                  const std::vector<LineVertex>& lines)
{
    get_clear(drawn_overlay());
    ui::ShapeMesh model = assemble_model();
    // The Add palette's preview drawn off-screen first (level_editor_preview.h): ImGui shows its picture.
    preview::tick_cycle(&model);
    add_node_gizmos(frame, model.nodes.selected);
    view::add_collision(fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
                        kNearZ, &drawn_overlay().under, &drawn_overlay().lines);
    // (map3: the selected collision faces, the camera nodes' frustums)
    faces::add_overlay(fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
                       kNearZ, &drawn_overlay().lines);
    cameras::add_overlay(
        fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
        {.near_z_ = kNearZ,
         .selected = model.nodes.selected >= 0 ? core::at(nodes::get_all(), model.nodes.selected).name : 0},
        &drawn_overlay().lines);
    // (UX5: note pins; the problem checker run again a moment after a change)
    notes::add_overlay(fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
                       kNearZ, &drawn_overlay().lines, &drawn_overlay().circles,
                       &drawn_overlay().texts);
    checks::note_model(model.changes, model.marks);
    triggers::add_overlay(fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
                          kNearZ, &drawn_overlay().lines, &drawn_overlay().texts);
    if (overlays::get_shown(overlays::kFeatureMap)) { // (the Overlays menu - level_editor_overlays.h)
        triggers::add_extras(
            fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
            {.near_z_ = kNearZ, .selected_node_ = model.nodes.selected}, &drawn_overlay().lines,
            &drawn_overlay().texts); // (speed-ups, proximity shapes, breakables, boosts - the feature map)
    }
    triggers::add_handles(&drawn_overlay().circles); // (the selected trigger volume's resize handles)
    entities::add_overlay(fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
                          kNearZ, &drawn_overlay().lines, &drawn_overlay().texts);
    play_test::add_overlay(
        fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_, kNearZ,
        &drawn_overlay().lines, &drawn_overlay().circles,
        &drawn_overlay().texts); // (the test runs' trails, Simulate's banner - level_editor_playtest.h)
    goal_edit::add_overlay(
        fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
        {.near_z_ = kNearZ,
         .selected = model.nodes.selected >= 0 ? core::at(nodes::get_all(), model.nodes.selected).name : 0},
        &drawn_overlay().lines,
        &drawn_overlay().texts); // (the goals' points' labels)
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        drawn_overlay().lines.push_back({.ax = core::at(lines, i).x,
                                               .ay = core::at(lines, i).y,
                                               .bx = core::at(lines, i + 1).x,
                                               .by = core::at(lines, i + 1).y,
                                               .argb = core::at(lines, i).color,
                                               .thickness = 1.0F});
    }
    // The view menus' overlays (level_editor_overlays.h): labels off - the texts so far dropped; the grid and the
    // carried things, the jump arcs, the grind preview, the ruler (their own texts kept: they are the readings).
    if (!overlays::get_shown(overlays::kLabels)) {
        drawn_overlay().texts.clear();
    }
    overlays::Painter painter;
    if (overlays::painter_now(&drawn_overlay(), &painter)) {
        overlays::add_overlay(painter, drag_session().dragging_ || fly_camera().key_moving_,
                              picked_set().mark_base_, have_selection());
        arcs::add_overlay(painter, objects.empty() ? scenery::get_selection() : std::vector<uint32_t>{});
        grind_preview::add_overlay(painter, drag_session().dragging_ ? -1 : model.nodes.hovered, model.nodes.selected);
        ruler::add_overlay(painter);
        ux9::add_overlay(painter); // (UX9: skateability colours, the gap wizard's gates, the ground tool's preview)
    }
    // (UX7: the placement ghost, the readout while dragging, the faces snapped to, the top edges for a rail -
    // level_editor_arrange.h)
    arrange::add_overlay(fly_camera().pos_, frame, drawn_view().lens_, drawn_view().viewport_,
                         kNearZ, &drawn_overlay());
    thumbs::tick_cycle(model); // (the Add palette's thumbnails drawn off-screen - level_editor_thumbs.h)
    ui::tick_cycle(model, drawn_overlay());
}

} // namespace internal
} // namespace frontend::level_editor
