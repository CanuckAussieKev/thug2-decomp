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

// --- the pieces -------------------------------------------------------------------------------------------------

std::vector<GridLine> grid_lines(V3 centre, float y, float radius, float spacing)
{
    std::vector<GridLine> lines;
    if (!(spacing > 0.0f) || !(radius > 0.0f) || radius / spacing > 200.0f)
        return lines;
    // (the lines on multiples of the spacing, within the square round the centre)
    const std::array<long, 2> first = {{static_cast<long>(std::ceil((centre.x - radius) / spacing)),
                           static_cast<long>(std::ceil((centre.z - radius) / spacing))}};
    const std::array<long, 2> last = {{static_cast<long>(std::floor((centre.x + radius) / spacing)),
                          static_cast<long>(std::floor((centre.z + radius) / spacing))}};
    for (long i = core::at(first, 0); i <= core::at(last, 0); ++i) {
        const float x = static_cast<float>(i) * spacing;
        lines.push_back({.a = {.x = x, .y = y, .z = centre.z - radius}, .b = {.x = x, .y = y, .z = centre.z + radius}, .major = i % 5 == 0});
    }
    for (long i = core::at(first, 1); i <= core::at(last, 1); ++i) {
        const float z = static_cast<float>(i) * spacing;
        lines.push_back({.a = {.x = centre.x - radius, .y = y, .z = z}, .b = {.x = centre.x + radius, .y = y, .z = z}, .major = i % 5 == 0});
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
    float nx = 0.0f, ny = 0.0f;
    if (!cast_onto(camera, frame, lens, p, near_z_, &nx, &ny))
        return false;
    ndc_to_pixel(viewport, nx, ny, x, y);
    return true;
}

bool Painter::get_seg(V3 a, V3 b, uint32_t argb, float thickness) const
{
    if (!clip_to_near(camera, frame, near_z_ + 0.01f, &a, &b))
        return false;
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    if (!get_pixel(a, &ax, &ay) || !get_pixel(b, &bx, &by))
        return false;
    overlay->lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    return true;
}

void Painter::get_dot(V3 p, float radius, uint32_t argb, bool filled) const
{
    float x = 0.0f, y = 0.0f;
    if (get_pixel(p, &x, &y))
        overlay->circles.push_back({.x = x, .y = y, .radius = radius, .argb = argb, .filled = filled, .thickness = 1.5f});
}

void Painter::draw_label(V3 p, uint32_t argb, const std::string& text, float dx, float dy) const
{
    float x = 0.0f, y = 0.0f;
    if (get_pixel(p, &x, &y))
        overlay->texts.push_back({.x = x + dx, .y = y + dy, .argb = argb, .text = text});
}

void Painter::perpendicular(V3 p, float size, uint32_t argb, float thickness) const
{
    (void)get_seg(p - V3{.x = size, .y = 0.0f, .z = 0.0f}, p + V3{.x = size, .y = 0.0f, .z = 0.0f}, argb, thickness);
    (void)get_seg(p - V3{.x = 0.0f, .y = 0.0f, .z = size}, p + V3{.x = 0.0f, .y = 0.0f, .z = size}, argb, thickness);
}

float Painter::span_to(V3 p) const
{
    const V3 d = p - camera;
    return std::sqrt(level_editor::get_dot(d, d));
}

bool painter_now(ui::Overlay* overlay, Painter* painter)
{
    ViewNow view{};
    if (!current_view(&view))
        return false;
    painter->camera = view.pos;
    painter->frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    painter->lens = view.lens;
    for (int i = 0; i < 4; ++i)
        core::at(painter->viewport, i) = core::at(view.viewport, i);
    painter->near_z_ = view.near_z_;
    painter->overlay = overlay;
    return true;
}

namespace {

// The ground's height below a point (the game's collision ray: a ProbeRay, constructor 0x00417760, cast 0x00417710 -
// static collision only; the hit's point at +0x34), as level_editor.cpp's ground_below.
bool get_ground_y(V3 from, float* y)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = from.x, .y = from.y - 5000.0f, .z = from.z, .w = 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    *y = ray->hit_.point.y;
    return true;
}

} // namespace

void add_overlay(const Painter& painter, bool moving, V3 selection_base, bool have_selection)
{
    if (get_shown(kGrid)) {
        // (on the ground below the selection, else below the camera; the ground found again only when it moved)
        static V3 g_last_from = {.x = 1e30f, .y = 0.0f, .z = 0.0f};
        static float g_ground_y = 0.0f;
        static bool g_have_ground = false;
        const V3 from = have_selection ? selection_base + V3{.x = 0.0f, .y = 2.0f, .z = 0.0f} : painter.camera;
        const V3 d = from - g_last_from;
        if (get_dot(d, d) > 25.0f * 25.0f) {
            g_last_from = from;
            g_have_ground = get_ground_y(from, &g_ground_y);
        }
        if (g_have_ground) {
            const V3 centre = {.x = std::round(from.x / 100.0f) * 100.0f, .y = g_ground_y, .z = std::round(from.z / 100.0f) * 100.0f};
            for (const GridLine& line : grid_lines(centre, g_ground_y + 0.5f, 1500.0f, 100.0f))
                (void)painter.get_seg(line.a, line.b, line.major ? 0x70E0E8F0u : 0x38C0C8D0u,
                                      line.major ? 1.25f : 1.0f);
        }
    }
    if (get_shown(kCarryHints) && moving) {
        constexpr uint32_t kCarried = 0xE060F080;
        for (const uint32_t id : carry::carried_objects()) {
            auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
            OrientedBox box{};
            if (!object || (object->object_flags_ & objects::kObjectFlagDead) || !object_edit::model_box(object, &box))
                continue;
            std::array<V3, 8> corners{};
            box_corners(box.origin, box.axes, box.min, box.max, corners);
            static const std::array<std::array<int, 2>, 12> kEdges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            for (const auto& e : kEdges)
                (void)painter.get_seg(core::at(corners, core::at(e, 0)), core::at(corners, core::at(e, 1)), kCarried, 1.25f);
        }
        const std::vector<nodes::Elem>& all = nodes::get_all();
        for (const uint32_t name : carry::carried_nodes()) {
            const int index = nodes::get_find(name);
            if (index < 0 || !core::at(all, index).has_pos_)
                continue;
            painter.get_dot(core::at(all, index).pos, 5.0f, kCarried, false);
        }
    }
}

} // namespace frontend::level_editor::overlays
