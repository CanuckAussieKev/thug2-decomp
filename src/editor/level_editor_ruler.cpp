// NOT ORIGINAL - the level editor's ruler (ours). See level_editor_ruler.h.

#include "editor/level_editor_ruler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "editor/level_editor_arcs.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "engine/render/coll_static.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/gfx/sector.h"

namespace frontend::level_editor::ruler {

// --- the pieces -------------------------------------------------------------------------------------------------

Measure measure_of(V3 a, V3 b)
{
    const V3 d = b - a;
    Measure m;
    m.distance = std::sqrt(get_dot(d, d));
    m.horizontal = std::sqrt(d.x * d.x + d.z * d.z);
    m.height = d.y;
    return m;
}

int nearest_within(const std::vector<std::pair<float, float>>& pixels, float x, float y, float radius)
{
    int best = -1;
    float best_d2 = radius * radius;
    for (size_t i = 0; i < pixels.size(); ++i) {
        const float dx = pixels[i].first - x;
        const float dy = pixels[i].second - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string describe(const Measure& m)
{
    std::string text = arcs::span_to(m.distance) + " - across " + arcs::span_to(m.horizontal) + ", ";
    text += m.height < 0.0f ? "down " : "up ";
    return text + arcs::span_to(std::fabs(m.height));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

bool g_armed;
int g_ends; // (0, 1 or 2 ends placed)
V3 g_a, g_b;
bool g_have_hover, g_hover_snapped;
V3 g_hover;

// The game's collision ray (a ProbeRay as level_editor_arcs.cpp's): the point hit and the collision object's checksum.
bool cast_feeler(V3 from, V3 to, V3* hit, uint32_t* sector)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {from.x, from.y, from.z, 1.0f};
    const maths::Vec4 end = {to.x, to.y, to.z, 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const float* p = &ray->hit_.point.x;
    *hit = {p[0], p[1], p[2]};
    *sector = ray->hit_.object ? ray->hit_.object_checksum_ : 0;
    return true;
}

// The corners of a sector's collision near `near` (within `reach`), at most 20000 looked at.
std::vector<V3> corners_near(uint32_t checksum, V3 near, float reach)
{
    std::vector<V3> corners;
    const render::WorldCell* sector = checksum ? scenery::sector_of(checksum) : nullptr;
    if (!sector)
        return corners;
    const render::HitMesh* coll = sector->hit_body_;
    if (!render::is_static_collider(coll))
        return corners;
    const auto* tri = coll->geometry_;
    if (!tri || !tri->vertices_)
        return corners;
    const int count = tri->num_vertices_ < 20000 ? tri->num_vertices_ : 20000;
    for (int i = 0; i < count; ++i) {
        const maths::Vec4 v = tri->read_stored_vertex(i);
        const V3 p = {v.x, v.y, v.z};
        const V3 d = p - near;
        if (get_dot(d, d) <= reach * reach)
            corners.push_back(p);
    }
    return corners;
}

} // namespace

bool get_armed() { return g_armed; }

void set_armed(bool on)
{
    g_armed = on;
    g_have_hover = false;
}

void get_clear()
{
    g_ends = 0;
    g_have_hover = false;
}

void get_hover(V3 origin, V3 unit_dir, float x, float y, bool no_snap)
{
    g_have_hover = false;
    if (!g_armed)
        return;
    V3 hit{};
    uint32_t sector = 0;
    if (!cast_feeler(origin, origin + unit_dir * 20000.0f, &hit, &sector))
        return;
    g_have_hover = true;
    g_hover = hit;
    g_hover_snapped = false;
    ViewNow view{};
    if (no_snap || !current_view(&view))
        return;
    // (the corners of the piece hit, near enough to the hit to be within the snap on screen)
    const V3 d = hit - origin;
    const float reach = std::max(50.0f, std::sqrt(get_dot(d, d)) * 0.08f);
    const std::vector<V3> corners = corners_near(sector, hit, reach);
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    std::vector<std::pair<float, float>> pixels;
    std::vector<V3> shown;
    for (const V3 c : corners) {
        float nx = 0.0f, ny = 0.0f, px = 0.0f, py = 0.0f;
        if (!cast_onto(view.pos, frame, view.lens, c, view.near_z_, &nx, &ny))
            continue;
        ndc_to_pixel(view.viewport.data(), nx, ny, &px, &py);
        pixels.emplace_back(px, py);
        shown.push_back(c);
    }
    const int nearest = nearest_within(pixels, x, y, kSnapPixels);
    if (nearest >= 0) {
        g_hover = shown[nearest];
        g_hover_snapped = true;
    }
}

std::string get_click()
{
    if (!g_armed || !g_have_hover)
        return "Ruler: no surface under cursor";
    if (g_ends == 1) {
        g_b = g_hover;
        g_ends = 2;
        return "Ruler: " + describe(measure_of(g_a, g_b)) + " (Shift+M clears)";
    }
    g_a = g_hover;
    g_ends = 1;
    return "Ruler: click the second point";
}

void add_overlay(const overlays::Painter& painter)
{
    if (!overlays::get_shown(overlays::kRuler))
        return;
    constexpr uint32_t kRuler = 0xFFFFE040, kGap = 0xC040C0FF, kRise = 0xC0FF8040;
    auto end = [&](V3 p, bool snapped) {
        painter.get_dot(p, snapped ? 6.0f : 4.0f, kRuler, !snapped);
        if (snapped)
            painter.get_dot(p, 2.0f, kRuler, true);
    };
    if (g_ends >= 1)
        end(g_a, false);
    const V3 b = g_ends == 2 ? g_b : g_hover;
    const bool have_b = g_ends == 2 || (g_armed && g_ends == 1 && g_have_hover);
    if (g_armed && g_have_hover && g_ends != 1)
        end(g_hover, g_hover_snapped); // (where the next click puts an end)
    if (g_ends >= 1 && have_b) {
        if (g_ends == 1)
            end(b, g_hover_snapped);
        else
            end(b, false);
        const Measure m = measure_of(g_a, b);
        (void)painter.get_seg(g_a, b, kRuler, 2.0f);
        // (the horizontal gap at the lower end's height, the rise straight up from it)
        const V3 low = g_a.y <= b.y ? g_a : b;
        const V3 high = g_a.y <= b.y ? b : g_a;
        const V3 corner = {high.x, low.y, high.z};
        (void)painter.get_seg(low, corner, kGap, 1.5f);
        (void)painter.get_seg(corner, high, kRise, 1.5f);
        painter.draw_label((g_a + b) * 0.5f, kRuler, arcs::span_to(m.distance));
        painter.draw_label((low + corner) * 0.5f, kGap, "across " + arcs::span_to(m.horizontal), 6.0f, 6.0f);
        if (std::fabs(m.height) > 0.5f)
            painter.draw_label((corner + high) * 0.5f, kRise,
                               (m.height < 0.0f ? "down " : "up ") + arcs::span_to(std::fabs(m.height)));
    }
}

std::string describe()
{
    if (g_ends < 2)
        return g_armed ? (g_ends == 1 ? "ruler: click the second end" : "ruler: click the first end") : "";
    return "ruler: " + describe(measure_of(g_a, g_b));
}

} // namespace frontend::level_editor::ruler
