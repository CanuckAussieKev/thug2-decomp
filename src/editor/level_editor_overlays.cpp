// NOT ORIGINAL - the level editor's View and Overlays menus (ours). See level_editor_overlays.h; the menus themselves
// are level_editor_overlays_ui.cpp's.

#include "editor/level_editor_overlays.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <array>

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
    for (long i = first[0]; i <= last[0]; ++i) {
        float x = static_cast<float>(i) * spacing;
        lines.push_back({{x, y, centre.z - radius}, {x, y, centre.z + radius}, i % 5 == 0});
    }
    for (long i = first[1]; i <= last[1]; ++i) {
        float z = static_cast<float>(i) * spacing;
        lines.push_back({{centre.x - radius, y, z}, {centre.x + radius, y, z}, i % 5 == 0});
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
        {kLabels, "Labels", "", "Names and values over the view"},
        {kFeatureMap, "Feature Map", "", "Speed-ups, boosts, proximity shapes and breakables"},
        {kGrindPreview, "Grind Preview", "", "Direction, terrain and connections of the rail under the cursor"},
        {kJumpArcs, "Jump Arcs", "Y", "Launch arcs from the selected ramp"},
        {kRuler, "Ruler", "M", "The ruler's measurement"},
        {kSkateability, "Skateability", "Alt+K", "Surfaces coloured by skateability"},
        {kGrid, "Grid", "", "100-unit ground grid"},
        {kCarryHints, "Carried Objects", "", "Outline objects carried by moving scenery"},
        {kLegend, "Collision Legend", "", "Collision colour key"},
        {kOutlinerMarks, "Outliner Markers", "", "Change letters on Outliner rows"},
    };
    return kBits;
}

// --- the setting ------------------------------------------------------------------------------------------------

namespace {

bool g_read;
uint32_t g_bits = kDefaultBits;
int g_view_mode;
bool g_forced_collision; // (Collision only turned the collision view on: off again when it ends)

void get_take_in()
{
    if (g_read)
        return;
    g_read = true;
    const int value = config_int("editor", "overlays", -1);
    g_bits = parse_bits(value, value >= 0);
}

} // namespace

uint32_t at_hand()
{
    get_take_in();
    return g_bits;
}

bool get_shown(uint32_t bit) { return (at_hand() & bit) != 0; }

void assign_value(uint32_t bit, bool on)
{
    get_take_in();
    const uint32_t bits = on ? g_bits | bit : g_bits & ~bit;
    if (bits == g_bits)
        return;
    g_bits = bits & kAllBits;
    config_set_int("editor", "overlays", static_cast<int>(g_bits));
}

void kickoff()
{
    get_take_in();
    g_view_mode = 0;
    g_forced_collision = false;
    render::view_mode::assign_value(render::view_mode::Mode::kLit);
}

void halt() { change_view_mode(0); }

int view_mode() { return g_view_mode; }

void change_view_mode(int mode)
{
    if (mode < 0 || mode >= kViewModes || mode == g_view_mode)
        return;
    constexpr int kCollisionOnly = static_cast<int>(render::view_mode::Mode::kCollisionOnly);
    if (mode == kCollisionOnly && !view::show_collision()) {
        view::set_show_collision(true);
        g_forced_collision = true;
    } else if (g_view_mode == kCollisionOnly && g_forced_collision) {
        if (view::show_collision())
            view::set_show_collision(false);
        g_forced_collision = false;
    }
    g_view_mode = mode;
    render::view_mode::assign_value(static_cast<render::view_mode::Mode>(mode));
}

const char* view_mode_name(int mode) { return render::view_mode::get_name(static_cast<render::view_mode::Mode>(mode)); }

// --- drawing ----------------------------------------------------------------------------------------------------

bool Painter::get_pixel(V3 p, float* x, float* y) const
{
    float nx = 0.0f, ny = 0.0f;
    if (!cast_onto(camera, frame, lens, p, near_z_, &nx, &ny))
        return false;
    ndc_to_pixel(viewport.data(), nx, ny, x, y);
    return true;
}

bool Painter::get_seg(V3 a, V3 b, uint32_t argb, float thickness) const
{
    if (!clip_to_near(camera, frame, near_z_ + 0.01f, &a, &b))
        return false;
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    if (!get_pixel(a, &ax, &ay) || !get_pixel(b, &bx, &by))
        return false;
    overlay->lines.push_back({ax, ay, bx, by, argb, thickness});
    return true;
}

void Painter::get_dot(V3 p, float radius, uint32_t argb, bool filled) const
{
    float x = 0.0f, y = 0.0f;
    if (get_pixel(p, &x, &y))
        overlay->circles.push_back({x, y, radius, argb, filled, 1.5f});
}

void Painter::draw_label(V3 p, uint32_t argb, const std::string& text, float dx, float dy) const
{
    float x = 0.0f, y = 0.0f;
    if (get_pixel(p, &x, &y))
        overlay->texts.push_back({x + dx, y + dy, argb, text});
}

void Painter::perpendicular(V3 p, float size, uint32_t argb, float thickness) const
{
    (void)get_seg(p - V3{size, 0.0f, 0.0f}, p + V3{size, 0.0f, 0.0f}, argb, thickness);
    (void)get_seg(p - V3{0.0f, 0.0f, size}, p + V3{0.0f, 0.0f, size}, argb, thickness);
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
        painter->viewport[i] = view.viewport[i];
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
    const maths::Vec4 start = {from.x, from.y, from.z, 1.0f};
    const maths::Vec4 end = {from.x, from.y - 5000.0f, from.z, 1.0f};
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
        static V3 g_last_from = {1e30f, 0.0f, 0.0f};
        static float g_ground_y = 0.0f;
        static bool g_have_ground = false;
        const V3 from = have_selection ? selection_base + V3{0.0f, 2.0f, 0.0f} : painter.camera;
        const V3 d = from - g_last_from;
        if (get_dot(d, d) > 25.0f * 25.0f) {
            g_last_from = from;
            g_have_ground = get_ground_y(from, &g_ground_y);
        }
        if (g_have_ground) {
            const V3 centre = {std::round(from.x / 100.0f) * 100.0f, g_ground_y, std::round(from.z / 100.0f) * 100.0f};
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
            box_corners(box.origin, box.axes.data(), box.min, box.max, corners.data());
            static const std::array<std::array<int, 2>, 12> kEdges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            for (const auto& e : kEdges)
                (void)painter.get_seg(corners[e[0]], corners[e[1]], kCarried, 1.25f);
        }
        const std::vector<nodes::Elem>& all = nodes::get_all();
        for (const uint32_t name : carry::carried_nodes()) {
            const int index = nodes::get_find(name);
            if (index < 0 || !all[index].has_pos_)
                continue;
            painter.get_dot(all[index].pos, 5.0f, kCarried, false);
        }
    }
}

} // namespace frontend::level_editor::overlays
