// NOT ORIGINAL - the level editor's ruler (ours). See level_editor_ruler.h.

#include "editor/level_editor_ruler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_arcs.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "engine/maths/vec2.h"
#include "engine/render/coll_static.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/gfx/sector.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::ruler {

namespace {

constexpr float kRulerRayLength = 20000.0F; // how far the mouse ray is cast to find a surface
constexpr float kSnappedDotRadius = 6.0F;   // the dot at a snapped point
constexpr float kFreeDotRadius = 4.0F;      // the dot at a free point
constexpr float kEndDotRadius = 2.0F;       // the dot at the measured end
constexpr float kRulerLineWidth = 2.0F;     // the measuring line
constexpr float kComponentLineWidth = 1.5F; // the across / rise component lines
constexpr float kLabelOffsetX = 6.0F;       // the across label's offset from the line
constexpr float kLabelOffsetY = 6.0F;
constexpr float kMinShownRise = 0.5F; // a rise smaller than this isn't drawn

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

Measure measure_of(V3 a, V3 b)
{
    const V3 delta = b - a;
    Measure measure;
    measure.distance = std::sqrt(get_dot(delta, delta));
    measure.horizontal = std::sqrt((delta.x * delta.x) + (delta.z * delta.z));
    measure.height = delta.y;
    return measure;
}

int nearest_within(const std::vector<std::pair<float, float>>& pixels, maths::Vec2 at, float radius)
{
    const float x = at.x;
    const float y = at.y;
    int best = -1;
    float best_d2 = radius * radius;
    for (size_t i = 0; i < pixels.size(); ++i) {
        const float dx = core::at(pixels, i).first - x;
        const float dy = core::at(pixels, i).second - y;
        const float d2 = (dx * dx) + (dy * dy);
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string describe(const Measure& measure)
{
    std::string text = arcs::span_to(measure.distance) + " - across " + arcs::span_to(measure.horizontal) + ", ";
    text += measure.height < 0.0F ? "down " : "up ";
    return text + arcs::span_to(std::fabs(measure.height));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - level_editor_ruler.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_ruler_state()).
struct LevelEditorRulerState {
    bool armed_{};
    int ends_{}; // (0, 1 or 2 ends placed)
    V3 a_{};
    V3 b_{};
    bool have_hover_{};
    bool hover_snapped_{};
    V3 hover_{};
};

LevelEditorRulerState& level_editor_ruler_state()
{
    static core::NoDestroy<LevelEditorRulerState> state;
    return state.get();
}

// The game's collision ray (a ProbeRay as level_editor_arcs.cpp's): the point hit and the collision object's checksum.
bool cast_feeler(V3 from, V3 to, V3* hit, uint32_t* sector)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& hit_point = ray->hit().point;
    *hit = {.x = hit_point[0], .y = hit_point[1], .z = hit_point[2]};
    *sector = ray->hit().object ? ray->hit().object_checksum_ : 0;
    return true;
}

// The corners of a sector's collision near `near` (within `reach`), at most 20000 looked at.
std::vector<V3> corners_near(uint32_t checksum, V3 near, float reach)
{
    std::vector<V3> corners;
    const render::WorldCell* sector = (checksum != 0U) ? scenery::sector_of(checksum) : nullptr;
    if (!sector) {
        return corners;
    }
    const render::HitMesh* coll = sector->hit_body();
    if (!render::is_static_collider(coll)) {
        return corners;
    }
    const auto* tri = coll->geometry();
    if (!tri || !tri->vertices()) {
        return corners;
    }
    const int count = tri->num_vertices() < 20000 ? tri->num_vertices() : 20000;
    for (int i = 0; i < count; ++i) {
        const maths::Vec4 v = tri->read_stored_vertex(i);
        const V3 point = {.x = v.x, .y = v.y, .z = v.z};
        const V3 delta = point - near;
        if (get_dot(delta, delta) <= reach * reach) {
            corners.push_back(point);
        }
    }
    return corners;
}

} // namespace

bool get_armed() { return level_editor_ruler_state().armed_; }

void set_armed(bool on)
{
    level_editor_ruler_state().armed_ = on;
    level_editor_ruler_state().have_hover_ = false;
}

void get_clear()
{
    level_editor_ruler_state().ends_ = 0;
    level_editor_ruler_state().have_hover_ = false;
}

void get_hover(V3 origin, V3 unit_dir, float x, float y, bool no_snap)
{
    level_editor_ruler_state().have_hover_ = false;
    if (!level_editor_ruler_state().armed_) {
        return;
    }
    V3 hit{};
    uint32_t sector = 0;
    if (!cast_feeler(origin, origin + unit_dir * kRulerRayLength, &hit, &sector)) {
        return;
    }
    level_editor_ruler_state().have_hover_ = true;
    level_editor_ruler_state().hover_ = hit;
    level_editor_ruler_state().hover_snapped_ = false;
    ViewNow view{};
    if (no_snap || !current_view(&view)) {
        return;
    }
    // (the corners of the piece hit, near enough to the hit to be within the snap on screen)
    const V3 delta = hit - origin;
    const float reach = std::max(50.0F, std::sqrt(get_dot(delta, delta)) * 0.08F);
    const std::vector<V3> corners = corners_near(sector, hit, reach);
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    std::vector<std::pair<float, float>> pixels;
    std::vector<V3> shown;
    for (const V3 corner : corners) {
        float nx = 0.0F;
        float ny = 0.0F;
        float px = 0.0F;
        float py = 0.0F;
        if (!cast_onto(view.pos, frame, view.lens, corner, view.near_z_, &nx, &ny)) {
            continue;
        }
        ndc_to_pixel(view.viewport, nx, ny, &px, &py);
        pixels.emplace_back(px, py);
        shown.push_back(corner);
    }
    const int nearest = nearest_within(pixels, {.x = x, .y = y}, kSnapPixels);
    if (nearest >= 0) {
        level_editor_ruler_state().hover_ = core::at(shown, nearest);
        level_editor_ruler_state().hover_snapped_ = true;
    }
}

std::string get_click()
{
    if (!level_editor_ruler_state().armed_ || !level_editor_ruler_state().have_hover_) {
        return "Ruler: no surface under cursor";
    }
    if (level_editor_ruler_state().ends_ == 1) {
        level_editor_ruler_state().b_ = level_editor_ruler_state().hover_;
        level_editor_ruler_state().ends_ = 2;
        return "Ruler: " + describe(measure_of(level_editor_ruler_state().a_, level_editor_ruler_state().b_)) + " (Shift+M clears)";
    }
    level_editor_ruler_state().a_ = level_editor_ruler_state().hover_;
    level_editor_ruler_state().ends_ = 1;
    return "Ruler: click the second point";
}

void add_overlay(const overlays::Painter& painter)
{
    if (!overlays::get_shown(overlays::kRuler)) {
        return;
    }
    constexpr uint32_t kRuler = 0xFFFFE040;
    constexpr uint32_t kGap = 0xC040C0FF;
    constexpr uint32_t kRise = 0xC0FF8040;
    auto end = [&](V3 point, bool snapped) {
        painter.get_dot(point, snapped ? kSnappedDotRadius : kFreeDotRadius, kRuler, !snapped);
        if (snapped) {
            painter.get_dot(point, kEndDotRadius, kRuler, true);
        }
    };
    if (level_editor_ruler_state().ends_ >= 1) {
        end(level_editor_ruler_state().a_, false);
    }
    const V3 point_b =
        level_editor_ruler_state().ends_ == 2 ? level_editor_ruler_state().b_ : level_editor_ruler_state().hover_;
    const bool have_b = level_editor_ruler_state().ends_ == 2 || (level_editor_ruler_state().armed_ && level_editor_ruler_state().ends_ == 1 && level_editor_ruler_state().have_hover_);
    if (level_editor_ruler_state().armed_ && level_editor_ruler_state().have_hover_ &&
        level_editor_ruler_state().ends_ != 1) {
        end(level_editor_ruler_state().hover_, level_editor_ruler_state().hover_snapped_); // (where the next click puts an end)
    }
    if (level_editor_ruler_state().ends_ >= 1 && have_b) {
        if (level_editor_ruler_state().ends_ == 1) {
            end(point_b, level_editor_ruler_state().hover_snapped_);
        } else {
            end(point_b, false);
        }
        const Measure measure = measure_of(level_editor_ruler_state().a_, point_b);
        (void)painter.get_seg(level_editor_ruler_state().a_, point_b, kRuler, kRulerLineWidth);
        // (the horizontal gap at the lower end's height, the rise straight up from it)
        const V3 low = level_editor_ruler_state().a_.y <= point_b.y ? level_editor_ruler_state().a_ : point_b;
        const V3 high = level_editor_ruler_state().a_.y <= point_b.y ? point_b : level_editor_ruler_state().a_;
        const V3 corner = {.x = high.x, .y = low.y, .z = high.z};
        (void)painter.get_seg(low, corner, kGap, kComponentLineWidth);
        (void)painter.get_seg(corner, high, kRise, kComponentLineWidth);
        painter.draw_label((level_editor_ruler_state().a_ + point_b) * kHalf, kRuler, arcs::span_to(measure.distance));
        painter.draw_label((low + corner) * kHalf, kGap, "across " + arcs::span_to(measure.horizontal), kLabelOffsetX,
                           kLabelOffsetY);
        if (std::fabs(measure.height) > kMinShownRise) {
            painter.draw_label((corner + high) * kHalf, kRise,
                               (measure.height < 0.0F ? "down " : "up ") + arcs::span_to(std::fabs(measure.height)));
        }
    }
}

std::string describe()
{
    if (level_editor_ruler_state().ends_ < 2) {
        return level_editor_ruler_state().armed_ ? (level_editor_ruler_state().ends_ == 1 ? "ruler: click the second end" : "ruler: click the first end") : "";
    }
    return "ruler: " + describe(measure_of(level_editor_ruler_state().a_, level_editor_ruler_state().b_));
}

} // namespace frontend::level_editor::ruler
