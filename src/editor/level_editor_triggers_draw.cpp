// NOT ORIGINAL - what the level editor draws for triggers (labels, boxes, speed-ups, breakables, proximity nodes)
// and their resize handles (ours). See level_editor_triggers.h.

#include "editor/level_editor_triggers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "base/maths/vec2.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "engine/objects/proximity.h"
#include "engine/script/nodes.h"
#include <gsl/pointers>
#include <array>
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "editor/level_editor_triggers_internal.h"

namespace frontend::level_editor::triggers {
namespace {

constexpr std::size_t kBoostTextSize = 48;
constexpr std::size_t kSpeedUpTextSize = 64;

constexpr uint32_t kHandleOutlineColour = 0xFF101010U;

constexpr float kDefaultBoostMultiplier = 7.5F; // shown when the call has none
// The overlay.
constexpr float kDefaultSegmentThickness = 1.5F;
constexpr float kOverlayLineThickness = 2.0F;
constexpr float kMinArrowLength = 4.0F; // shorter arrows are not drawn
constexpr uint32_t kLinkArrowAlpha = 0xE0;
constexpr uint32_t kKillArrowAlpha = 0x80;
constexpr uint32_t kGapArrowAlpha = 0xB0;
constexpr uint32_t kLevelBoostArrowAlpha = 0xA0;
constexpr float kMinSideLengthSquared = 1e-4F;
constexpr float kArrowHeadLength = 9.0F;
constexpr float kArrowHeadWidth = 7.0F;
constexpr float kSpeedUpLabelDistance = 1500.0F; // how close the camera is for a speed-up label
constexpr float kSpeedUpLabelLift = 14.0F;
constexpr float kProximDrawDistance = 6000.0F; // how far away an unselected proximity node is still drawn
constexpr float kProximBoxThickness = 1.5F;
constexpr float kProximLabelLift = 10.0F;
constexpr float kBreakDrawDistance = 4000.0F;
constexpr int kCrackRays = 5;
constexpr float kCrackThickness = 1.5F;
constexpr float kBreakLabelLift = 12.0F;
constexpr float kBoostArrowScale = 0.15F; // arrow length per unit of boost strength
constexpr float kBoostDrawDistance = 3000.0F;
constexpr float kDiameterFactor = 2.0F;
constexpr int kHandleCount = 6; // a box resize handles: one for each face
constexpr int kLastHandle = 5;
constexpr float kLitHandleRadius = 7.0F;
constexpr float kHandleRadius = 5.5F;
constexpr float kResizeSnapGrid = 25.0F;

struct Painter {
    V3 camera{};
    const TickCycle* frame{};
    const Lens* lens{};
    std::span<const uint32_t, 4> viewport;
    float near_z_{};
    std::vector<ui::Seg>* lines{};

    bool get_to_pixel(V3 point, float* x, float* y) const
    {
        float nx = 0.0F;
        float ny = 0.0F;
        if (!cast_onto(camera, *frame, *lens, point, near_z_, &nx, &ny)) {
            return false;
        }
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    }
    void get_seg(V3 a, V3 b, uint32_t argb, float thickness = kDefaultSegmentThickness) const
    {
        if (!clip_to_near(camera, *frame, near_z_ + kNearPlaneMargin, &a, &b)) {
            return;
        }
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (get_to_pixel(a, &ax, &ay) && get_to_pixel(b, &bx, &by)) {
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
        }
    }
    void cuboid(const OrientedBox& box, uint32_t argb, float thickness) const
    {
        std::array<V3, kBoxCorners> corners{};
        box_corners(box.origin, box.axes, box.min, box.max, corners);
        for (int i = 0; i < kBoxCorners; ++i) {
            for (const int bit : {1, 2, 4}) {
                if ((i & bit) == 0) {
                    get_seg(core::at(corners, i), core::at(corners, i | bit), argb, thickness);
                }
            }
        }
    }
    // An arrow from a to b, its head drawn on the screen.
    void get_arrow(V3 a, V3 b, uint32_t argb) const
    {
        get_seg(a, b, argb, kOverlayLineThickness);
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (!get_to_pixel(a, &ax, &ay) || !get_to_pixel(b, &bx, &by)) {
            return;
        }
        float dx = bx - ax;
        float dy = by - ay;
        float const length = std::sqrt((dx * dx) + (dy * dy));
        if (length < kMinArrowLength) {
            return;
        }
        dx /= length;
        dy /= length;
        const float head = std::min(14.0F, length * 0.4F);
        for (const float side : {-1.0F, 1.0F}) {
            const float hx = (-dx * 0.9F) + (side * -dy * 0.45F);
            const float hy = (-dy * 0.9F) + (side * dx * 0.45F);
            lines->push_back({.ax = bx,
                              .ay = by,
                              .bx = bx + (hx * head),
                              .by = by + (hy * head),
                              .argb = argb,
                              .thickness = kOverlayLineThickness});
        }
    }
};

uint32_t get_dim(uint32_t argb, uint32_t alpha) { return (argb & kRgbMask) | (alpha << kAlphaShift); }

float distance_to(V3 point, V3 min, V3 max)
{
    const float dx = std::max({min.x - point.x, 0.0F, point.x - max.x});
    const float dy = std::max({min.y - point.y, 0.0F, point.y - max.y});
    const float dz = std::max({min.z - point.z, 0.0F, point.z - max.z});
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

} // namespace
namespace internal {

std::string describe(const Calls& calls, Kind kind)
{
    std::string text = kind_name(kind);
    switch (kind) {
    case Kind::kKill:
        if (calls.water) {
            text += " (water)";
        }
        if (calls.target != 0U) {
            text += " -> " + get_label_of(calls.target);
        }
        break;
    case Kind::kJumpTo:
        if (calls.target != 0U) {
            text += " -> " + get_label_of(calls.target);
        }
        break;
    case Kind::kGapEnd:
        text += ": " + (calls.gap_text_.empty() ? get_label_of(calls.gap_id_) : calls.gap_text_);
        if (calls.gap_score_ != 0) {
            text += " (" + std::to_string(calls.gap_score_) + ")";
        }
        break;
    case Kind::kGapStart:
        if (calls.gap_id_ != 0U) {
            text += ": " + get_label_of(calls.gap_id_);
        }
        break;
    case Kind::kBoost: {
        std::array<char, kBoostTextSize> value{};
        if (calls.boost_speed_ > 0.0F) {
            text::format_to(value, " (speed %.0f)", calls.boost_speed_);
        } else {
            text::format_to(value, " (x%.1f)",
                            calls.boost_multi_ > 0.0F ? calls.boost_multi_ : kDefaultBoostMultiplier);
        }
        text += value.data();
        break;
    }
    case Kind::kViewCam:
        if (calls.camera_node_ != 0U) {
            text += " -> " + get_label_of(calls.camera_node_);
        }
        break;
    default:
        break;
    }
    return text;
}

} // namespace internal
namespace {

bool get_node_pos(uint32_t name, V3* pos)
{
    maths::Vec4 point;
    if ((name == 0U) || !level_nodes::fetch_vertex(name, false) || !level_nodes::fetch_place(name, &point)) {
        return false;
    }
    *pos = {.x = point.x, .y = point.y, .z = point.z};
    return true;
}

// One of the editor's triggers: its box (grey when it has a problem), the arrow to what it links to (a gap's to
// its end) and its label.
template <class Label>
void draw_added_trigger(Painter& paint, const Added& added, Label& label)
{
    const OrientedBox box = box_of(added.pos, added.yaw, added.spec.size);
    const uint32_t colour = added.problem ? 0xC0A0A0A0U : kind_colour(added.spec.kind);
    paint.cuboid(box, colour, kOverlayLineThickness);
    V3 target{};
    if (!added.problem &&
        (added.spec.kind == Kind::kJumpTo || added.spec.kind == Kind::kKill || added.spec.kind == Kind::kViewCam) &&
        get_node_pos(checksum_of(added.spec.target), &target)) {
        paint.get_arrow(added.pos, target,
                        get_dim(colour, added.spec.kind == Kind::kKill ? kKillArrowAlpha : kLinkArrowAlpha));
    }
    if (added.spec.kind == Kind::kGapStart) {
        const int other = partner(level_editor_triggers_state().specs_, spec_index(added.spec.name));
        if (other >= 0) {
            for (const Added& partner_trigger : level_editor_triggers_state().added_) {
                if (partner_trigger.alive &&
                    same_text(partner_trigger.spec.name, core::at(level_editor_triggers_state().specs_, other).name)) {
                    paint.get_arrow(added.pos, partner_trigger.pos,
                                    get_dim(kind_colour(Kind::kGapEnd), kGapArrowAlpha));
                }
            }
        }
    }
    std::string text = added.spec.name + ": " + kind_name(added.spec.kind);
    if (added.spec.kind == Kind::kKill || added.spec.kind == Kind::kJumpTo) {
        text += " -> " + added.spec.target;
    } else if (added.spec.kind == Kind::kScripts) {
        text += " " + added.spec.script;
    } else if (added.spec.kind == Kind::kGapStart || added.spec.kind == Kind::kGapEnd) {
        text += " \"" + added.spec.text + "\"";
    } else if (added.spec.kind == Kind::kBoost) {
        text += " " + std::to_string(added.spec.strength);
    } else if (added.spec.kind == Kind::kBreak || added.spec.kind == Kind::kViewCam) {
        text += " " + added.spec.target;
    }
    if (added.problem) {
        text += " (not active)";
    }
    label({.x = added.pos.x, .y = added.pos.y + (added.spec.size.y * kHalf), .z = added.pos.z}, colour, text);
}

void remember_view(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z);

} // namespace

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    remember_view(camera, frame, lens, viewport, near_z); // (the resize handles are drawn and picked with it)
    if (!get_shown()) {
        return;
    }
    Painter paint = {.camera = camera, .frame = &frame, .lens = &lens, .viewport = viewport, .near_z_ = near_z, .lines = lines};
    int labels = 0;
    auto label = [&](V3 at, uint32_t argb, const std::string& text) {
        if (labels >= kMaxLabels) {
            return;
        }
        float x = 0.0F;
        float y = 0.0F;
        if (paint.get_to_pixel(at, &x, &y)) {
            texts->push_back({.x = x, .y = y, .argb = argb, .text = text});
            ++labels;
        }
    };
    // The level's: nearest first.
    std::vector<std::pair<float, const LevelTrigger*>> near;
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        const float distance = distance_to(camera, level_trigger.min, level_trigger.max);
        if (distance <= kDrawRadius) {
            near.emplace_back(distance, &level_trigger);
        }
    }
    std::ranges::sort(near, [](const auto& a, const auto& b) { return a.first < b.first; });
    if (near.size() > static_cast<size_t>(kMaxLevelDrawn)) {
        near.resize(kMaxLevelDrawn);
    }
    for (const auto& candidate : near) {
        const LevelTrigger& level_trigger = *candidate.second;
        const bool inspected = level_trigger.sector == level_editor_triggers_state().inspected_;
        const uint32_t colour =
            level_trigger.off ? 0x70909090U : get_dim(kind_colour(level_trigger.kind), inspected ? 0xFFU : 0xC0U);
        const float thickness = inspected ? 2.5F : 1.0F;
        if (level_trigger.face_count_ <= kMaxFacesKept) {
            for (size_t face_index = 0; face_index + 2 < level_trigger.faces.size(); face_index += 3) {
                paint.get_seg(core::at(level_trigger.faces, face_index), core::at(level_trigger.faces, face_index + 1),
                              colour, thickness);
                paint.get_seg(core::at(level_trigger.faces, face_index + 1),
                              core::at(level_trigger.faces, face_index + 2), colour, thickness);
                paint.get_seg(core::at(level_trigger.faces, face_index + 2), core::at(level_trigger.faces, face_index),
                              colour, thickness);
            }
        } else {
            const OrientedBox box = {.origin = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                     .axes = {{{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                               {.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                               {.x = 0.0F, .y = 0.0F, .z = 1.0F}}},
                                     .min = level_trigger.min,
                                     .max = level_trigger.max};
            paint.cuboid(box, colour, thickness);
        }
        const V3 centre = (level_trigger.min + level_trigger.max) * 0.5F;
        V3 target{};
        if (!level_trigger.off &&
            ((level_trigger.kind == Kind::kJumpTo && get_node_pos(level_trigger.calls.target, &target)) ||
             (level_trigger.kind == Kind::kViewCam &&
              get_node_pos(level_trigger.calls.camera_node_, &target)))) { // (map4: the camera's)
            paint.get_arrow(centre, target, get_dim(kind_colour(level_trigger.kind), kLinkArrowAlpha));
        }
        if (candidate.first <= kLabelRadius || inspected) {
            label({.x = centre.x, .y = level_trigger.max.y, .z = centre.z}, colour,
                  (level_trigger.off ? "(off) " : "") + describe(level_trigger.calls, level_trigger.kind) + "  " +
                      get_label_of(level_trigger.sector));
        }
    }
    // The editor's.
    for (const Added& added : level_editor_triggers_state().added_) {
        if (!added.alive) {
            continue;
        }
        draw_added_trigger(paint, added, label);
    }
}

namespace {

// A label at a world point, if it's on screen.
void add_label(Painter& paint, std::vector<ui::Overlay::OverlayText>* texts, V3 at, uint32_t argb,
               const std::string& text)
{
    float x = 0.0F;
    float y = 0.0F;
    if (paint.get_to_pixel(at, &x, &y)) {
        texts->push_back({.x = x, .y = y, .argb = argb, .text = text});
    }
}

// Rail speed-ups near the camera: chevrons along each segment pointing uphill (where they speed the skater up),
// 60 apart, and their rates.
void draw_speed_ups(Painter& paint, V3 camera, std::vector<ui::Overlay::OverlayText>* texts)
{
    auto label = [&](V3 at, uint32_t argb, const std::string& text) { add_label(paint, texts, at, argb, text); };
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    // Rail speed-ups: chevrons along each segment pointing uphill (where they speed the skater up), 60 apart.
    for (const chains::SpeedSegment& speed_segment : chains::speed_segments(camera, 3000.0F)) {
        V3 point_a = speed_segment.a;
        V3 point_b = speed_segment.b;
        if (point_b.y < point_a.y) {
            std::swap(point_a, point_b);
        }
        const V3 delta = point_b - point_a;
        const float length = std::sqrt(get_dot(delta, delta));
        if (length < 1.0F) {
            continue;
        }
        const V3 dir = delta * (1.0F / length);
        V3 side = perpendicular(dir, up);
        if (get_dot(side, side) < kMinSideLengthSquared) {
            side = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
        }
        side = make_unit(side);
        const uint32_t colour = speed_segment.level ? 0xC0B4B4B4U : 0xFFFF9F1AU;
        const int count = std::max(1, std::min(24, static_cast<int>(length / 60.0F)));
        for (int i = 1; i <= count; ++i) {
            const V3 tip = point_a + delta * (static_cast<float>(i) / static_cast<float>(count + 1)) + up * 4.0F;
            paint.get_seg(tip - dir * kArrowHeadLength + side * kArrowHeadWidth, tip, colour, kOverlayLineThickness);
            paint.get_seg(tip - dir * kArrowHeadLength - side * kArrowHeadWidth, tip, colour, kOverlayLineThickness);
        }
        const V3 middle = (point_a + point_b) * 0.5F;
        const V3 middle_offset = middle - camera;
        if (get_dot(middle_offset, middle_offset) < kSpeedUpLabelDistance * kSpeedUpLabelDistance) {
            std::array<char, kSpeedUpTextSize> text{};
            text::format_to(text, "%s+%.1f%%", speed_segment.level ? "level speed-up " : "speed-up ",
                            speed_segment.rate * 100.0F);
            label(middle + up * kSpeedUpLabelLift, colour, text.data());
        }
    }
}

// A proximity node (near the camera, or the selected one): a sphere's three rings or a box's edges, and what it runs.
void draw_proxim_node(Painter& paint, V3 camera, std::vector<ui::Overlay::OverlayText>* texts, uint32_t node,
                      bool is_selected)
{
    auto label = [&](V3 at, uint32_t argb, const std::string& text) { add_label(paint, texts, at, argb, text); };
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const objects::ProximNode* proxim_node = is_proxim(node) ? objects::proxim_get_node(node) : nullptr;
    if (!proxim_node) {
        return;
    }
    const V3 at = {.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z};
    const V3 delta = at - camera;
    if (!is_selected && get_dot(delta, delta) > kProximDrawDistance * kProximDrawDistance) {
        return;
    }
    const uint32_t colour = is_selected ? 0xFFFFB060U : 0xC0F08A3CU;
    V3 top = at;
    if (proxim_node->form() == objects::ProximShape::kSphere && proxim_node->radius() > 0.0F) {
        constexpr int kSides = 32;
        const std::array<std::array<V3, 2>, 3> axes = {{{{{.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 0, .z = 1}}},
                                                        {{{.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0}}},
                                                        {{{.x = 0, .y = 1, .z = 0}, {.x = 0, .y = 0, .z = 1}}}}};
        for (const auto& plane : axes) {
            V3 previous = at + core::at(plane, 0) * proxim_node->radius();
            for (int i = 1; i <= kSides; ++i) {
                const float angle = 2.0F * 3.14159265F * static_cast<float>(i) / kSides;
                const V3 next = at + core::at(plane, 0) * (proxim_node->radius() * std::cos(angle)) +
                                core::at(plane, 1) * (proxim_node->radius() * std::sin(angle));
                paint.get_seg(previous, next, colour, 1.0F);
                previous = next;
            }
        }
        top = at + up * proxim_node->radius();
    } else if (proxim_node->form() == objects::ProximShape::kBox) {
        const V3 min = {.x = proxim_node->box().min.x, .y = proxim_node->box().min.y, .z = proxim_node->box().min.z};
        const V3 max = {.x = proxim_node->box().max.x, .y = proxim_node->box().max.y, .z = proxim_node->box().max.z};
        const OrientedBox box = {.origin = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                 .axes = {{{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                           {.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                           {.x = 0.0F, .y = 0.0F, .z = 1.0F}}},
                                 .min = min,
                                 .max = max};
        paint.cuboid(box, colour, kProximBoxThickness);
        top = {.x = (min.x + max.x) * kHalf, .y = max.y, .z = (min.z + max.z) * kHalf};
    }
    if (const ProximEdit* proxim_edit = proxim_of(node)) {
        std::string text = "Proximity";
        if (!proxim_edit->enter.empty()) {
            text += "  in: " + proxim_edit->enter;
        }
        if (!proxim_edit->exit.empty()) {
            text += "  out: " + proxim_edit->exit;
        }
        label(top + up * kProximLabelLift, colour, text);
    }
}

// A breakable: cracks over the piece's box (each face's middle, five jagged rays) and its state.
void draw_breakable(Painter& paint, const Added& added, std::vector<ui::Overlay::OverlayText>* texts)
{
    auto label = [&](V3 at, uint32_t argb, const std::string& text) { add_label(paint, texts, at, argb, text); };
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const uint32_t colour = added.broken ? 0x90A0A0A0U : 0xFFE8F4FFU;
    const V3 half = added.spec.size * 0.5F;
    const std::array<V3, 4> normals = {
        {{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0}, {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}}};
    for (const V3& normal : normals) {
        const V3 across = {.x = normal.z, .y = 0.0F, .z = -normal.x};
        const float width = (std::fabs(across.x) * half.x) + (std::fabs(across.z) * half.z);
        const V3 centre = added.pos + V3{.x = normal.x * half.x, .y = 0.0F, .z = normal.z * half.z};
        for (int ray = 0; ray < kCrackRays; ++ray) {
            const float angle = 0.6F + (static_cast<float>(ray) * 1.26F);
            V3 previous = centre;
            for (int step = 1; step <= 3; ++step) {
                const float reach = static_cast<float>(step) / 3.0F;
                const float jag = ((step & 1) != 0) ? 0.18F : -0.12F;
                const V3 next = centre + across * (std::cos(angle + jag) * width * 0.8F * reach) +
                                up * (std::sin(angle + jag) * half.y * 0.8F * reach);
                paint.get_seg(previous, next, colour, kCrackThickness);
                previous = next;
            }
        }
    }
    label(added.pos + up * (half.y + kBreakLabelLift), colour,
          std::string(added.broken ? "broken (back in the editor)" : "breaks when hit") +
              (added.spec.sound.empty() ? "" : "  " + added.spec.sound));
}

} // namespace

void add_extras(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport,
                ExtrasPick extras_pick, std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    const auto near_z = extras_pick.near_z_;
    const auto selected_node = extras_pick.selected_node_;
    Painter paint = {
        .camera = camera, .frame = &frame, .lens = &lens, .viewport = viewport, .near_z_ = near_z, .lines = lines};
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    draw_speed_ups(paint, camera, texts);
    // Proximity nodes: the edited ones and the selected one - a sphere's three rings, a box's edges.
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const uint32_t selected =
        selected_node >= 0 && std::cmp_less(selected_node, nodes.size()) ? core::at(nodes, selected_node).name : 0;
    auto draw_proxim = [&](uint32_t node, bool is_selected) {
        draw_proxim_node(paint, camera, texts, node, is_selected);
    };
    for (const ProximEdit& stored_proxim : stored_proxims()) {
        if (stored_proxim.node != selected) {
            draw_proxim(stored_proxim.node, false);
        }
    }
    if ((selected != 0U) && is_proxim(selected)) {
        draw_proxim(selected, true);
    }
    // Breakables: cracks over the piece's box (each face's middle, five jagged rays); jump boosts: an arrow up as
    // strong as they are (the level's too).
    for (const Added& added : level_editor_triggers_state().added_) {
        if (!added.alive) {
            continue;
        }
        const V3 delta = added.pos - camera;
        if (get_dot(delta, delta) > kBreakDrawDistance * kBreakDrawDistance) {
            continue;
        }
        if (added.spec.kind == Kind::kBreak) {
            draw_breakable(paint, added, texts);
        } else if (added.spec.kind == Kind::kBoost) {
            const V3 base = added.pos + up * (added.spec.size.y * 0.5F);
            paint.get_arrow(base, base + up * (static_cast<float>(added.spec.strength) * kBoostArrowScale),
                            kind_colour(Kind::kBoost));
        }
    }
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        if (level_trigger.kind != Kind::kBoost || level_trigger.off) {
            continue;
        }
        const V3 centre = (level_trigger.min + level_trigger.max) * 0.5F;
        const V3 delta = centre - camera;
        if (get_dot(delta, delta) > kBoostDrawDistance * kBoostDrawDistance) {
            continue;
        }
        const float strength = level_trigger.calls.boost_speed_ > 0.0F ? level_trigger.calls.boost_speed_ : 600.0F;
        const V3 base = {.x = centre.x, .y = level_trigger.max.y, .z = centre.z};
        paint.get_arrow(base, base + up * (strength * kBoostArrowScale), get_dim(kind_colour(Kind::kBoost), kLevelBoostArrowAlpha));
    }
}

namespace {

constexpr std::array<uint32_t, 3> kHandleColours = {
    {0xFFFF4040U, 0xFF40E040U, 0xFF4080FFU}}; // (x, y, z: as the move arrows)

// The selected editor trigger's node, the view add_overlay last drew with, the handles where they were drawn, the one
// under the cursor, and a resize under way.
uint32_t& stored_selected_node()
{
    static uint32_t selected_node{};
    return selected_node;
}
struct LastView {
    bool have = false;
    V3 camera = {};
    TickCycle frame = {};
    Lens lens = {};
    std::array<uint32_t, 4> viewport = {};
    float near_z_ = 1.0F;
};

LastView& last_view()
{
    static core::NoDestroy<LastView> state;
    return state.get();
}
struct DrawnHandle {
    int handle;
    float x, y;
};
// NOT ORIGINAL grouping - the handles drawn and the one hovered (were globals). Made on first use, never destroyed
// (trigger_handles()).
struct TriggerHandles {
    std::vector<DrawnHandle> drawn_handles_;
    int hover_handle_ = -1;
};

TriggerHandles& trigger_handles()
{
    static core::NoDestroy<TriggerHandles> state;
    return state.get();
}

struct ResizeDrag {
    bool on = false;
    std::string name;
    uint32_t node = 0;
    int handle = -1;
    V3 pos0 = {}, size0 = {}, start = {}, direction = {};
    float yaw = 0.0F, s0 = 0.0F;
    bool proxim = false, sphere = false; // (a proximity node's shape)
};

ResizeDrag& resize_drag()
{
    static core::NoDestroy<ResizeDrag> state;
    return state.get();
}

void remember_view(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z)
{
    last_view().have = true;
    last_view().camera = camera;
    last_view().frame = frame;
    last_view().lens = lens;
    for (int i = 0; i < 4; ++i) {
        core::at(last_view().viewport, i) = core::at(viewport, i);
    }
    last_view().near_z_ = near_z;
}

const Added* alive_named(uint32_t node)
{
    for (const Added& added : level_editor_triggers_state().added_) {
        if (added.alive && hash_of_name(added.spec.name) == node) {
            return &added;
        }
    }
    return nullptr;
}

} // namespace

void set_selected(uint32_t node)
{
    if (node != stored_selected_node()) {
        trigger_handles().hover_handle_ = -1;
    }
    stored_selected_node() = node;
}

namespace {

// What the handles resize: an editor trigger's box (not a breakable's: its box is its piece's), or a proximity node's
// sphere / box (its edit's, else the shape the game has).
struct HandleShape {
    bool trigger = false, sphere = false;
    V3 pos = {};
    float yaw = 0.0F;
    V3 size = {};
};
bool shape_for(uint32_t node, HandleShape* handle_shape)
{
    *handle_shape = HandleShape{};
    if (const Added* added = alive_named(node)) {
        if (added->spec.kind == Kind::kBreak) {
            return false;
        }
        handle_shape->trigger = true;
        handle_shape->pos = added->pos;
        handle_shape->yaw = added->yaw;
        handle_shape->size = added->spec.size;
        return true;
    }
    const objects::ProximNode* proxim_node = is_proxim(node) ? objects::proxim_get_node(node) : nullptr;
    if (!proxim_node) {
        return false;
    }
    const V3 at = {.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z};
    const ProximEdit* proxim_edit = proxim_of(node);
    if (proxim_edit && proxim_edit->shape) {
        handle_shape->sphere = !proxim_edit->box;
        handle_shape->pos = proxim_edit->box ? at + proxim_edit->offset : at;
        handle_shape->size = proxim_edit->box ? proxim_edit->size
                                              : V3{.x = proxim_edit->radius * kDiameterFactor,
                                                   .y = proxim_edit->radius * kDiameterFactor,
                                                   .z = proxim_edit->radius * kDiameterFactor};
        return true;
    }
    if (proxim_node->form() == objects::ProximShape::kSphere) {
        handle_shape->sphere = true;
        handle_shape->pos = at;
        handle_shape->size = {.x = proxim_node->radius() * kDiameterFactor,
                              .y = proxim_node->radius() * kDiameterFactor,
                              .z = proxim_node->radius() * kDiameterFactor};
        return proxim_node->radius() > 0.0F;
    }
    if (proxim_node->form() == objects::ProximShape::kBox) {
        const V3 min = {.x = proxim_node->box().min.x, .y = proxim_node->box().min.y, .z = proxim_node->box().min.z};
        const V3 max = {.x = proxim_node->box().max.x, .y = proxim_node->box().max.y, .z = proxim_node->box().max.z};
        handle_shape->pos = (min + max) * kHalf;
        handle_shape->size = max - min;
        return handle_shape->size.x > 0.0F && handle_shape->size.y > 0.0F && handle_shape->size.z > 0.0F;
    }
    return false; // (a sector's own shape: not resized here)
}

} // namespace

void add_handles(std::vector<ui::Overlay::Circle>* circles)
{
    trigger_handles().drawn_handles_.clear();
    HandleShape shape;
    if (!last_view().have || !shape_for(stored_selected_node(), &shape)) {
        return;
    }
    for (int handle = 0; handle < kHandleCount; ++handle) {
        const V3 point = handle_point(shape.pos, shape.yaw, shape.size, handle);
        float nx = 0.0F;
        float ny = 0.0F;
        float x = 0.0F;
        float y = 0.0F;
        if (!cast_onto(last_view().camera, last_view().frame, last_view().lens, point, last_view().near_z_, &nx, &ny)) {
            continue;
        }
        ndc_to_pixel(last_view().viewport, nx, ny, &x, &y);
        const bool lit =
            handle == trigger_handles().hover_handle_ || (resize_drag().on && handle == resize_drag().handle);
        const uint32_t colour = lit ? 0xFFFFFF40U : core::at(kHandleColours, handle >> 1);
        circles->push_back({.x = x,
                            .y = y,
                            .radius = lit ? kLitHandleRadius : kHandleRadius,
                            .argb = colour,
                            .filled = true,
                            .thickness = 1.0F});
        circles->push_back({.x = x,
                            .y = y,
                            .radius = lit ? kLitHandleRadius : kHandleRadius,
                            .argb = kHandleOutlineColour,
                            .filled = false,
                            .thickness = 1.0F});
        trigger_handles().drawn_handles_.push_back({.handle = handle, .x = x, .y = y});
    }
}

int handle_at(maths::Vec2 at, float radius)
{
    const float x = at.x;
    const float y = at.y;
    int best = -1;
    float best_distance = radius;
    for (const DrawnHandle& drawn_handle : trigger_handles().drawn_handles_) {
        const float distance =
            std::sqrt(((drawn_handle.x - x) * (drawn_handle.x - x)) + ((drawn_handle.y - y) * (drawn_handle.y - y)));
        if (distance <= best_distance) {
            best_distance = distance;
            best = drawn_handle.handle;
        }
    }
    return best;
}

void set_hover_handle(int handle) { trigger_handles().hover_handle_ = handle; }

bool begin_resize(int handle, V3 origin, V3 dir)
{
    HandleShape shape;
    if (!shape_for(stored_selected_node(), &shape) || handle < 0 || handle > kLastHandle) {
        return false;
    }
    const Added* added = shape.trigger ? alive_named(stored_selected_node()) : nullptr;
    ResizeDrag drag;
    drag.name = added ? added->spec.name : std::string();
    drag.node = stored_selected_node();
    drag.handle = handle;
    drag.pos0 = shape.pos;
    drag.size0 = shape.size;
    drag.yaw = shape.yaw;
    drag.proxim = !shape.trigger;
    drag.sphere = shape.sphere;
    drag.start = handle_point(shape.pos, shape.yaw, shape.size, handle);
    drag.direction = handle_direction({.yaw = shape.yaw, .handle = handle});
    if (!closest_on_axis({.origin = drag.start, .dir = drag.direction}, {.origin = origin, .dir = dir}, &drag.s0)) {
        return false; // (looking along the axis: no grip on it)
    }
    drag.on = true;
    resize_drag() = drag;
    note_undo(); // (the state before: one undo step, taken by the editor next frame)
    return true;
}

void rescale(V3 origin, V3 dir, bool snap)
{
    if (!resize_drag().on) {
        return;
    }
    float new_scale = 0.0F;
    if (!closest_on_axis({.origin = resize_drag().start, .dir = resize_drag().direction},
                         {.origin = origin, .dir = dir}, &new_scale)) {
        return;
    }
    float along = new_scale - resize_drag().s0;
    if (!std::isfinite(along) || std::fabs(along) > kMaxTriggerSize) {
        return; // (the ray nearly along the axis: far off)
    }
    const int axis = resize_drag().handle >> 1;
    const float size0 = axis_value(resize_drag().size0, axis);
    if (snap) {
        along = snap_to_grid(size0 + along, kResizeSnapGrid) - size0;
    }
    V3 pos{};
    V3 size{};
    resize_face(resize_drag().pos0, resize_drag().yaw, resize_drag().size0, {.handle = resize_drag().handle, .along = along}, {.new_pos_ = &pos, .new_size_ = &size});
    if (resize_drag().proxim) {
        // (a proximity node: a sphere's radius grows by what the face moved, about its middle; a box as a trigger's -
        // its middle kept as an offset from the node)
        const objects::ProximNode* proxim_node =
            is_proxim(resize_drag().node) ? objects::proxim_get_node(resize_drag().node) : nullptr;
        if (!proxim_node) {
            resize_drag().on = false;
            return;
        }
        ProximEdit proxim_edit = proxim_of(resize_drag().node) ? *proxim_of(resize_drag().node) : ProximEdit{};
        proxim_edit.node = resize_drag().node;
        proxim_edit.shape = true;
        proxim_edit.box = !resize_drag().sphere;
        if (resize_drag().sphere) {
            proxim_edit.radius = std::max(kProximMin, std::min(kProximMax, (size0 * kHalf) + along));
        } else {
            proxim_edit.size = size;
            proxim_edit.offset =
                pos -
                V3{.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z};
        }
        set_proxim(proxim_edit, false);
        return;
    }
    const int index = spec_index(resize_drag().name);
    if (index < 0) {
        resize_drag().on = false;
        return;
    }
    Spec spec = core::at(level_editor_triggers_state().specs_, index);
    if (spec.size.x != size.x || spec.size.y != size.y || spec.size.z != size.z) {
        spec.size = size;
        set_spec(resize_drag().name, spec, false);
    }
    node_edit::Values values;
    if (node_edit::get_take_in(resize_drag().node, &values) &&
        (values.pos.x != pos.x || values.pos.y != pos.y || values.pos.z != pos.z)) {
        values.pos = pos;
        node_edit::assign_value(resize_drag().node, values);
    }
}

void end_resize() { resize_drag().on = false; }

bool resizing() { return resize_drag().on; }

} // namespace frontend::level_editor::triggers
