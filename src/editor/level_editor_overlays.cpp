// NOT ORIGINAL - the level editor's View and Overlays menus (ours). See level_editor_overlays.h; the menus themselves
// are level_editor_overlays_ui.cpp's.

#include "editor/level_editor_overlays.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_view.h"
#include "engine/gfx/view_mode.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::overlays {

namespace {

constexpr float kMaxGridLines = 200.0F; // a grid of more lines than this per side isn't drawn
constexpr long kMajorLineEvery = 5; // every fifth grid line is a major line
constexpr float kCircleThickness = 1.5F;
constexpr float kFarAway = 1e30F;             // a position that is never close to a real one
constexpr float kGridRefreshDistance = 25.0F; // the grid's ground is found again when the origin moves this far
constexpr std::uint32_t kGridMajorColour = 0x70E0E8F0U;
constexpr std::uint32_t kGridMinorColour = 0x38C0C8D0U;
constexpr float kGridMajorThickness = 1.25F;
constexpr float kCarriedThickness = 1.25F;
constexpr float kCarriedDotRadius = 5.0F;

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

std::vector<GridLine> grid_lines(V3 centre, float y, float radius, float spacing)
{
    std::vector<GridLine> lines;
    if (!(spacing > 0.0F) || !(radius > 0.0F) || radius / spacing > kMaxGridLines)
        return lines;
    // (the lines on multiples of the spacing, within the square round the centre)
    const std::array<long, 2> first = {{static_cast<long>(std::ceil((centre.x - radius) / spacing)),
                           static_cast<long>(std::ceil((centre.z - radius) / spacing))}};
    const std::array<long, 2> last = {{static_cast<long>(std::floor((centre.x + radius) / spacing)),
                          static_cast<long>(std::floor((centre.z + radius) / spacing))}};
    for (long i = core::at(first, 0); i <= core::at(last, 0); ++i) {
        const float x = static_cast<float>(i) * spacing;
        lines.push_back({.a = {.x = x, .y = y, .z = centre.z - radius}, .b = {.x = x, .y = y, .z = centre.z + radius}, .major = i % kMajorLineEvery == 0});
    }
    for (long i = core::at(first, 1); i <= core::at(last, 1); ++i) {
        const float z = static_cast<float>(i) * spacing;
        lines.push_back({.a = {.x = centre.x - radius, .y = y, .z = z}, .b = {.x = centre.x + radius, .y = y, .z = z}, .major = i % kMajorLineEvery == 0});
    }
    return lines;
}

uint32_t parse_bits(int value, bool present)
{
    if (!present || value < 0)
        return kDefaultBits;
    return static_cast<uint32_t>(value) & kAllBits;
}

const std::vector<BitInfo>& get_bits()
{
    static const std::vector<BitInfo> kBits = {
        {.bit = kLabels, .label = "Labels", .key = "", .tip = "Names and values over the view"},
        {.bit = kFeatureMap, .label = "Feature Map", .key = "", .tip = "Speed-ups, boosts, proximity shapes and breakables"},
        {.bit = kGrindPreview, .label = "Grind Preview", .key = "", .tip = "Direction, terrain and connections of the rail under the cursor"},
        {.bit = kJumpArcs, .label = "Jump Arcs", .key = "Y", .tip = "Launch arcs from the selected ramp"},
        {.bit = kRuler, .label = "Ruler", .key = "M", .tip = "The ruler's measurement"},
        {.bit = kSkateability, .label = "Skateability", .key = "Alt+K", .tip = "Surfaces coloured by skateability"},
        {.bit = kGrid, .label = "Grid", .key = "", .tip = "100-unit ground grid"},
        {.bit = kCarryHints, .label = "Carried Objects", .key = "", .tip = "Outline objects carried by moving scenery"},
        {.bit = kLegend, .label = "Collision Legend", .key = "", .tip = "Collision colour key"},
        {.bit = kOutlinerMarks, .label = "Outliner Markers", .key = "", .tip = "Change letters on Outliner rows"},
    };
    return kBits;
}

// --- the setting ------------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - level_editor_overlays.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_overlays_state()).
struct LevelEditorOverlaysState {
    bool read_{};
    uint32_t bits_ = kDefaultBits;
    int view_mode_{};
    bool forced_collision_{}; // (Collision only turned the collision view on: off again when it ends)
};

LevelEditorOverlaysState& level_editor_overlays_state()
{
    static core::NoDestroy<LevelEditorOverlaysState> state;
    return state.get();
}

void get_take_in()
{
    if (level_editor_overlays_state().read_)
        return;
    level_editor_overlays_state().read_ = true;
    const int value = config_int("editor", "overlays", -1);
    level_editor_overlays_state().bits_ = parse_bits(value, value >= 0);
}

} // namespace

uint32_t at_hand()
{
    get_take_in();
    return level_editor_overlays_state().bits_;
}

bool get_shown(uint32_t bit) { return (at_hand() & bit) != 0; }

void assign_value(uint32_t bit, bool on)
{
    get_take_in();
    const uint32_t bits = on ? level_editor_overlays_state().bits_ | bit : level_editor_overlays_state().bits_ & ~bit;
    if (bits == level_editor_overlays_state().bits_)
        return;
    level_editor_overlays_state().bits_ = bits & kAllBits;
    config_set_int("editor", "overlays", static_cast<int>(level_editor_overlays_state().bits_));
}

void kickoff()
{
    get_take_in();
    level_editor_overlays_state().view_mode_ = 0;
    level_editor_overlays_state().forced_collision_ = false;
    render::view_mode::assign_value(render::view_mode::Mode::kLit);
}

void halt() { change_view_mode(0); }

int view_mode() { return level_editor_overlays_state().view_mode_; }

void change_view_mode(int mode)
{
    if (mode < 0 || mode >= kViewModes || mode == level_editor_overlays_state().view_mode_)
        return;
    constexpr int kCollisionOnly = static_cast<int>(render::view_mode::Mode::kCollisionOnly);
    if (mode == kCollisionOnly && !view::show_collision()) {
        view::set_show_collision(true);
        level_editor_overlays_state().forced_collision_ = true;
    } else if (level_editor_overlays_state().view_mode_ == kCollisionOnly && level_editor_overlays_state().forced_collision_) {
        if (view::show_collision())
            view::set_show_collision(false);
        level_editor_overlays_state().forced_collision_ = false;
    }
    level_editor_overlays_state().view_mode_ = mode;
    render::view_mode::assign_value(static_cast<render::view_mode::Mode>(mode));
}

const char* view_mode_name(int mode) { return render::view_mode::get_name(static_cast<render::view_mode::Mode>(mode)); }

// --- drawing ----------------------------------------------------------------------------------------------------

bool Painter::get_pixel(V3 p, float* x, float* y) const
{
    float nx = 0.0F;
    float ny = 0.0F;
    if (!cast_onto(camera_, frame_, lens_, p, near_z_, &nx, &ny))
        return false;
    ndc_to_pixel(viewport_, nx, ny, x, y);
    return true;
}

bool Painter::get_seg(V3 a, V3 b, uint32_t argb, float thickness) const
{
    if (!clip_to_near(camera_, frame_, near_z_ + kNearPlaneMargin, &a, &b))
        return false;
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_pixel(a, &ax, &ay) || !get_pixel(b, &bx, &by))
        return false;
    overlay_->lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    return true;
}

void Painter::get_dot(V3 p, float radius, uint32_t argb, bool filled) const
{
    float x = 0.0F;
    float y = 0.0F;
    if (get_pixel(p, &x, &y))
        overlay_->circles.push_back({.x = x, .y = y, .radius = radius, .argb = argb, .filled = filled, .thickness = kCircleThickness});
}

void Painter::draw_label(V3 p, uint32_t argb, const std::string& text, float dx, float dy) const
{
    float x = 0.0F;
    float y = 0.0F;
    if (get_pixel(p, &x, &y))
        overlay_->texts.push_back({.x = x + dx, .y = y + dy, .argb = argb, .text = text});
}

void Painter::perpendicular(V3 p, float size, uint32_t argb, float thickness) const
{
    (void)get_seg(p - V3{.x = size, .y = 0.0F, .z = 0.0F}, p + V3{.x = size, .y = 0.0F, .z = 0.0F}, argb, thickness);
    (void)get_seg(p - V3{.x = 0.0F, .y = 0.0F, .z = size}, p + V3{.x = 0.0F, .y = 0.0F, .z = size}, argb, thickness);
}

float Painter::span_to(V3 p) const
{
    const V3 d = p - camera_;
    return std::sqrt(level_editor::get_dot(d, d));
}

bool painter_now(ui::Overlay* overlay, Painter* painter)
{
    ViewNow view{};
    if (!current_view(&view))
        return false;
    painter->set_camera(view.pos);
    painter->set_frame(frame_from_yaw_pitch(view.yaw, view.pitch));
    painter->set_lens(view.lens);
    for (int i = 0; i < 4; ++i)
        core::at(painter->viewport(), i) = core::at(view.viewport, i);
    painter->set_near_z(view.near_z_);
    painter->set_overlay(overlay);
    return true;
}

namespace {

// The ground's height below a point (the game's collision ray: a ProbeRay, constructor 0x00417760, cast 0x00417710 -
// static collision only; the hit's point at +0x34), as level_editor.cpp's ground_below.
bool get_ground_y(V3 from, float* y)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = from.x, .y = from.y - 5000.0F, .z = from.z, .w = 1.0F};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    *y = ray->hit().point.y;
    return true;
}

} // namespace

void add_overlay(const Painter& painter, bool moving, V3 selection_base, bool have_selection)
{
    if (get_shown(kGrid)) {
        // (on the ground below the selection, else below the camera; the ground found again only when it moved)
        static V3 g_last_from = {.x = kFarAway, .y = 0.0F, .z = 0.0F};
        static float g_ground_y = 0.0F;
        static bool g_have_ground = false;
        const V3 from = have_selection ? selection_base + V3{.x = 0.0F, .y = 2.0F, .z = 0.0F} : painter.camera();
        const V3 d = from - g_last_from;
        if (get_dot(d, d) > kGridRefreshDistance * kGridRefreshDistance) {
            g_last_from = from;
            g_have_ground = get_ground_y(from, &g_ground_y);
        }
        if (g_have_ground) {
            const V3 centre = {
                .x = std::round(from.x / 100.0F) * 100.0F, .y = g_ground_y, .z = std::round(from.z / 100.0F) * 100.0F};
            for (const GridLine& line : grid_lines(centre, g_ground_y + 0.5F, 1500.0F, 100.0F))
                (void)painter.get_seg(line.a, line.b, line.major ? kGridMajorColour : kGridMinorColour,
                                      line.major ? kGridMajorThickness : 1.0F);
        }
    }
    if (get_shown(kCarryHints) && moving) {
        constexpr uint32_t kCarried = 0xE060F080;
        for (const uint32_t id : carry::carried_objects()) {
            auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
            OrientedBox box{};
            if (!object || (object->object_flags() & objects::kObjectFlagDead) || !object_edit::model_box(object, &box))
                continue;
            std::array<V3, kBoxCornerCount> corners{};
            box_corners(box.origin, box.axes, box.min, box.max, corners);
            static const std::array<std::array<int, 2>, 12> kEdges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            for (const auto& e : kEdges)
                (void)painter.get_seg(core::at(corners, core::at(e, 0)), core::at(corners, core::at(e, 1)), kCarried, kCarriedThickness);
        }
        const std::vector<nodes::Elem>& all = nodes::get_all();
        for (const uint32_t name : carry::carried_nodes()) {
            const int index = nodes::get_find(name);
            if (index < 0 || !core::at(all, index).has_pos_)
                continue;
            painter.get_dot(core::at(all, index).pos, kCarriedDotRadius, kCarried, false);
        }
    }
}

} // namespace frontend::level_editor::overlays
