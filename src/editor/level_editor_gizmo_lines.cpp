// NOT ORIGINAL - the gizmos' lines in the 3D view against the world's depth (ours). See level_editor_gizmo_lines.h.

#include "editor/level_editor_gizmo_lines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_prefs.h"
#include "engine/gfx/depth_lines.h"
#include "imgui.h"

namespace frontend::level_editor::gizmo_lines {

namespace {

constexpr float kBehindStrength = 0.25F; // Faded: what a hidden part keeps of its opacity
constexpr float kDashPixels = 6.0F;      // Faded: a hidden part's dashes and the gaps between them
constexpr float kGapPixels = 4.0F;
constexpr int kMostDashes = 160;         // a line's dashes at most (the rest of it solid)
constexpr float kFullStrengthShare = 0.6F; // of the draw distance: lines at full strength up to here
constexpr float kNearZ = 2.0F;             // the near plane (the editor's camera; render::choose_camera's)
constexpr float kViewMargin = 1.05F;       // the view's sides pushed out this much (a line's width at the edge)
constexpr float kAlphaMax = 255.0F;
constexpr float kScreenSpan = 2.0F; // the view is 2 * tan_x across at depth 1
constexpr float kDiameter = 2.0F;   // radii

struct GizmoLinesState {
    View view;
    float pixel{}; // what a pixel spans at depth 1
    std::vector<render::depth_lines::Line> lines;
};

GizmoLinesState& gizmo_lines_state()
{
    static core::NoDestroy<GizmoLinesState> state;
    return state.get();
}

// A point in view space: x right, y up, z the depth along the view.
V3 seen_from(const View& view, V3 point)
{
    const V3 delta = point - view.camera;
    return {.x = get_dot(delta, view.frame.right), .y = get_dot(delta, view.frame.up), .z = -get_dot(delta, view.frame.back)};
}

float depth_of(const View& view, V3 point) { return -get_dot(point - view.camera, view.frame.back); }

void push(V3 a, V3 b, uint32_t argb, float width, render::depth_lines::Depth depth)
{
    gizmo_lines_state().lines.push_back(
        {.from = {a.x, a.y, a.z}, .to = {b.x, b.y, b.z}, .argb = argb, .width = width, .depth = depth});
}

// A hidden part as Faded draws it: dashes kDashPixels long, kGapPixels apart, sized at each dash's own depth.
void push_dashes(V3 a, V3 b, uint32_t argb, float width)
{
    const GizmoLinesState& state = gizmo_lines_state();
    const V3 along = b - a;
    const float length = std::sqrt(get_dot(along, along));
    if (length <= 0.0F) {
        return;
    }
    const V3 unit = along * (1.0F / length);
    float at = 0.0F;
    for (int dash = 0; at < length; ++dash) {
        const V3 start = a + (unit * at);
        if (dash == kMostDashes) {
            push(start, b, argb, width, render::depth_lines::Depth::kBehind);
            return;
        }
        const float pixel = std::max(depth_of(state.view, start), kNearZ) * state.pixel;
        const float end = std::min(at + (kDashPixels * pixel), length);
        push(start, a + (unit * end), argb, width, render::depth_lines::Depth::kBehind);
        at = end + (kGapPixels * pixel);
    }
}

} // namespace

const char* behind_name(Behind behind)
{
    switch (behind) {
    case Behind::kXRay:
        return "X-Ray";
    case Behind::kFaded:
        return "Faded";
    case Behind::kHidden:
        break;
    }
    return "Hidden";
}

Behind behind_now()
{
    return static_cast<Behind>(std::clamp(prefs::get_pull().gizmos_behind_, 0, kBehindCount - 1));
}

void set_behind(Behind behind)
{
    prefs::Values values = prefs::get_pull();
    values.gizmos_behind_ = static_cast<int>(behind);
    prefs::assign_value(values);
}

void behind_menu()
{
    if (!ImGui::BeginMenu("Gizmos Behind Geometry")) {
        return;
    }
    const Behind now = behind_now();
    for (int i = 0; i < kBehindCount; ++i) {
        const auto behind = static_cast<Behind>(i);
        if (ImGui::MenuItem(behind_name(behind), nullptr, behind == now)) {
            set_behind(behind);
        }
    }
    ImGui::EndMenu();
}

float distance_fade(float distance, float range)
{
    if (range <= 0.0F) {
        return 1.0F;
    }
    const float full = range * kFullStrengthShare;
    if (distance <= full) {
        return 1.0F;
    }
    return std::clamp((range - distance) / (range - full), 0.0F, 1.0F);
}

uint32_t scale_alpha(uint32_t argb, float factor)
{
    const float alpha = static_cast<float>(argb >> kAlphaShift) * std::clamp(factor, 0.0F, 1.0F);
    return (argb & kRgbMask) | (static_cast<uint32_t>(std::lround(std::min(alpha, kAlphaMax))) << kAlphaShift);
}

bool in_view(V3 a, V3 b, const ViewSides& sides)
{
    if (a.z < sides.near_z && b.z < sides.near_z) {
        return false;
    }
    const float side_x = std::fabs(sides.tan_x) * kViewMargin;
    const float side_y = std::fabs(sides.tan_y) * kViewMargin;
    // (past one side of the view at both ends: x beyond +-z * tan)
    const bool right = a.x > a.z * side_x && b.x > b.z * side_x;
    const bool left = a.x < -a.z * side_x && b.x < -b.z * side_x;
    const bool above = a.y > a.z * side_y && b.y > b.z * side_y;
    const bool below = a.y < -a.z * side_y && b.y < -b.z * side_y;
    return !(right || left || above || below);
}

void begin_frame(const View& view)
{
    GizmoLinesState& state = gizmo_lines_state();
    state.view = view;
    state.pixel = view.viewport_width > 0.0F ? kScreenSpan * std::fabs(view.lens.tan_x_) / view.viewport_width : 0.0F;
    state.lines.clear();
}

void end_frame() { render::depth_lines::set_lines(gizmo_lines_state().lines); }

void add(const Stroke& stroke)
{
    const GizmoLinesState& state = gizmo_lines_state();
    if (!in_view(seen_from(state.view, stroke.a), seen_from(state.view, stroke.b),
                 {.tan_x = state.view.lens.tan_x_, .tan_y = state.view.lens.tan_y_, .near_z = kNearZ})) {
        return;
    }
    const Behind behind = behind_now();
    if (stroke.emphasised || behind == Behind::kXRay) {
        push(stroke.a, stroke.b, stroke.argb, stroke.width, render::depth_lines::Depth::kAlways);
        return;
    }
    push(stroke.a, stroke.b, stroke.argb, stroke.width, render::depth_lines::Depth::kInFront);
    if (behind != Behind::kFaded) {
        return;
    }
    const uint32_t faint = scale_alpha(stroke.argb, kBehindStrength);
    if (stroke.dash_behind) {
        push_dashes(stroke.a, stroke.b, faint, stroke.width);
    } else {
        push(stroke.a, stroke.b, faint, stroke.width, render::depth_lines::Depth::kBehind);
    }
}

void add_dot(V3 at, float radius, uint32_t argb)
{
    const GizmoLinesState& state = gizmo_lines_state();
    const float depth = depth_of(state.view, at);
    if (depth < kNearZ) {
        return;
    }
    const V3 half = state.view.frame.right * (radius * depth * state.pixel);
    add({.a = at - half, .b = at + half, .argb = argb, .width = radius * kDiameter, .dash_behind = false});
}

} // namespace frontend::level_editor::gizmo_lines
