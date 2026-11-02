// NOT ORIGINAL - the level editor's skateability overlay (ours). See level_editor_skate_view.h.

#include "editor/level_editor_skate_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_view.h"
#include "engine/render/coll_static.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/param_struct.h"
#include "imgui.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/sector.h"
#include "engine/core/checked_index.h"
#include "editor/level_editor_gui_text.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::skate_view {

// --- the pure parts -----------------------------------------------------------------------------------------------

Kind get_classify(uint16_t flags, float normal_y, float threshold, int trigger)
{
    if (trigger == 1)
        return Kind::kKill;
    if (trigger == 2)
        return Kind::kWater;
    if (flags & 0x10)
        return Kind::kNone; // (passed through)
    bool skatable = false;
    const bool vert = (flags & 0x08) != 0;
    if (flags & 0x01)
        // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
        skatable = true;
    else if (flags & 0x02)
        return Kind::kNotSkatable;
    else if (vert)
        skatable = true;
    else if (flags & 0x04)
        return Kind::kWallride;
    else
        skatable = normal_y >= threshold;
    if (!skatable)
        return Kind::kNotSkatable;
    if (vert)
        return Kind::kVert;
    return normal_y >= 0.97f ? Kind::kFloor : Kind::kBank;
}

uint32_t get_colour(Kind kind)
{
    switch (kind) {
    case Kind::kFloor:
        return 0x6840D850;
    case Kind::kBank:
        return 0x7040C8C0;
    case Kind::kVert:
        return 0x88FF8A20;
    case Kind::kWallride:
        return 0x78FFD830;
    case Kind::kNotSkatable:
        return 0x70E03838;
    case Kind::kKill:
        return 0x90FF1050;
    case Kind::kWater:
        return 0x903070FF;
    default:
        return 0;
    }
}

const char* get_name(Kind kind)
{
    switch (kind) {
    case Kind::kFloor:
        return "Skatable floor";
    case Kind::kBank:
        return "Bank / ramp (skatable slope)";
    case Kind::kVert:
        return "Vert";
    case Kind::kWallride:
        return "Wallride wall";
    case Kind::kNotSkatable:
        return "Not skatable (bail / wall)";
    case Kind::kKill:
        return "Kill zone";
    case Kind::kWater:
        return "Water (kill zone)";
    default:
        return "";
    }
}

float get_threshold(float angle, float factor)
{
    if (!std::isfinite(angle) || !std::isfinite(factor) || !(factor > 0.0f))
        return std::sin(45.0f * 3.14159265f / 180.0f);
    return std::max(0.0f, std::min(1.0f, static_cast<float>(std::sin(static_cast<double>(angle) * factor))));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr float kRadius = 3000.0f;
constexpr int kMaxFaces = 12000;
constexpr uint32_t kRail = 0xF040F0FF;

struct Cached {
    const render::TriMeshSource* tri = nullptr;
    V3 min = {}, max = {}, first = {}, last = {};
    int faces = 0;
    float threshold = -1.0f;
    std::vector<V3> points;
    std::vector<uint8_t> kinds;
};
std::unordered_map<uint32_t, Cached>& cache()
{
    static std::unordered_map<uint32_t, Cached> value;
    return value;
}
// NOT ORIGINAL grouping - level_editor_skate_view.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_skate_view_state()).
struct LevelEditorSkateViewState {
    std::array<int, kKinds> counts_{};
    bool capped_{};
    int rail_segments_{};
    float threshold_ = -1.0f;
};

LevelEditorSkateViewState& level_editor_skate_view_state()
{
    static core::NoDestroy<LevelEditorSkateViewState> state;
    return state.get();
}

V3 to_v3(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

float game_threshold()
{
    if (level_editor_skate_view_state().threshold_ < 0.0f) {
        // (0x005003A0: the script float 0x3EEDE4D3 - 0x00478F00 with its assert on, as it asks - times 0x00645464)
        const float angle = script::fetch_float(0x3EEDE4D3u); // (the original also passes 1, unused)
        const float factor = ::commands::kDegreeToRadianRatio;
        level_editor_skate_view_state().threshold_ = get_threshold(angle, factor);
    }
    return level_editor_skate_view_state().threshold_;
}

const Cached& cache_of(uint32_t checksum, const render::TriMeshSource* tri, int trigger)
{
    const V3 min = to_v3(tri->bounds().min);
    const V3 max = to_v3(tri->bounds().max);
    V3 first = {};
    V3 last = {};
    if (tri->vertices() && tri->num_vertices()) {
        first = to_v3(tri->read_stored_vertex(0));
        last = to_v3(tri->read_stored_vertex(tri->num_vertices() - 1));
    }
    auto same = [](V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    const float threshold = game_threshold();
    Cached& c = cache()[checksum];
    if (c.tri == tri && std::cmp_equal(c.faces, tri->tri_count()) && c.threshold == threshold && same(c.min, min) &&
        same(c.max, max) && same(c.first, first) && same(c.last, last))
        return c;
    c = Cached{};
    c.tri = tri, c.min = min, c.max = max, c.first = first, c.last = last, c.faces = tri->tri_count();
    c.threshold = threshold;
    if (!tri->faces() || !tri->vertices())
        return c;
    for (int f = 0; f < c.faces; ++f) {
        std::array<V3, 3> p{};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const uint32_t index = tri->face_vertex(f, k);
            ok = index < tri->num_vertices();
            if (ok)
                core::at(p, k) = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
        }
        if (!ok)
            continue;
        const uint16_t flags = tri->face_flags(f);
        const V3 n = perpendicular(core::at(p, 1) - core::at(p, 0), core::at(p, 2) - core::at(p, 0));
        const float length = std::sqrt(get_dot(n, n));
        const float ny = length > 0.0f ? n.y / length : 0.0f;
        Kind kind = get_classify(flags, ny, threshold, (flags & 0x40) ? trigger : 0);
        if (kind == Kind::kNone)
            continue;
        c.points.insert(c.points.end(), p.begin(), p.end());
        c.kinds.push_back(static_cast<uint8_t>(kind));
    }
    return c;
}

} // namespace

bool get_shown() { return overlays::get_shown(overlays::kSkateability); }
void set_shown(bool on) { overlays::assign_value(overlays::kSkateability, on); }
void halt() { cache().clear(); }

void add_overlay(const overlays::Painter& painter)
{
    std::memset(level_editor_skate_view_state().counts_.data(), 0, sizeof(level_editor_skate_view_state().counts_));
    level_editor_skate_view_state().capped_ = false;
    level_editor_skate_view_state().rail_segments_ = 0;
    if (!get_shown() || !painter.overlay())
        return;
    // The level's kill / water triggers by sector (1 kill, 2 water).
    std::unordered_map<uint32_t, int> triggers;
    for (const triggers::LevelTrigger& t : triggers::level_triggers()) {
        if (t.kind == triggers::Kind::kKill && !t.off)
            triggers[t.sector] = t.calls.water ? 2 : 1;
    }
    struct Candidate {
        uint32_t checksum;
        const render::TriMeshSource* tri;
        float distance;
    };
    struct Walk {
        V3 camera;
        std::vector<Candidate> found;
    } walk = {.camera = painter.camera(), .found = {}};
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            auto* w = static_cast<Walk*>(context);
            const render::HitMesh* coll = sector->hit_body();
            if (!render::is_static_collider(coll) || (coll->flag_bits() & 4))
                return; // (no collision, or switched off)
            const auto* tri = coll->geometry();
            if (!tri || !tri->tri_count() || view::hidden_sector(checksum))
                return;
            const float distance = view::distance_to_box(w->camera, to_v3(tri->bounds().min), to_v3(tri->bounds().max));
            if (distance <= kRadius)
                w->found.push_back({.checksum = checksum, .tri = tri, .distance = distance});
        },
        &walk);
    std::ranges::sort(walk.found, [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    struct Polygon {
        float depth;
        uint32_t argb;
        int count;
        std::array<float, 4> x;
        std::array<float, 4> y;
    };
    std::vector<Polygon> polygons;
    for (const Candidate& candidate : walk.found) {
        auto trigger = triggers.find(candidate.checksum);
        const Cached& c = cache_of(candidate.checksum, candidate.tri, trigger == triggers.end() ? 0 : trigger->second);
        for (size_t f = 0; f < c.kinds.size(); ++f) {
            if (static_cast<int>(polygons.size()) >= kMaxFaces) {
                level_editor_skate_view_state().capped_ = true;
                break;
            }
            std::array<V3, 4> clipped{};
            const int count =
                view::clip_triangle_to_near(painter.camera(), painter.frame(), painter.near_z(), std::span<const V3>(c.points).subspan(f * 3).first<3>(), clipped);
            if (!count)
                continue;
            Polygon poly = {};
            poly.count = count;
            bool left = true, right = true, below = true, above = true;
            float depth = 0.0f;
            for (int k = 0; k < count; ++k) {
                float nx = 0.0f, ny = 0.0f;
                cast_onto(painter.camera(), painter.frame(), painter.lens(), core::at(clipped, k), painter.near_z() * 0.5f, &nx, &ny);
                left = left && nx < -1.0f, right = right && nx > 1.0f, below = below && ny < -1.0f;
                above = above && ny > 1.0f;
                ndc_to_pixel(painter.viewport(), nx, ny, &core::at(poly.x, k), &core::at(poly.y, k));
                depth += -get_dot(core::at(clipped, k) - painter.camera(), painter.frame().back);
            }
            if (left || right || below || above)
                continue;
            const Kind kind = static_cast<Kind>(core::at(c.kinds, f));
            poly.depth = depth / static_cast<float>(count);
            poly.argb = get_colour(kind);
            polygons.push_back(poly);
            ++core::at(level_editor_skate_view_state().counts_, core::at(c.kinds, f));
        }
        if (level_editor_skate_view_state().capped_)
            break;
    }
    std::ranges::sort(polygons, [](const Polygon& a, const Polygon& b) { return a.depth > b.depth; });
    auto& under = painter.overlay()->under;
    for (const Polygon& p : polygons) {
        under.push_back({.ax = core::at(p.x, 0), .ay = core::at(p.y, 0), .bx = core::at(p.x, 1), .by = core::at(p.y, 1), .cx = core::at(p.x, 2), .cy = core::at(p.y, 2), .argb = p.argb});
        if (p.count == 4)
            under.push_back({.ax = core::at(p.x, 0), .ay = core::at(p.y, 0), .bx = core::at(p.x, 2), .by = core::at(p.y, 2), .cx = core::at(p.x, 3), .cy = core::at(p.y, 3), .argb = p.argb});
    }
    // The editor's own kill / water triggers: their boxes outlined.
    for (const triggers::Added& a : triggers::added_triggers()) {
        if (!a.alive || a.spec.kind != triggers::Kind::kKill)
            continue;
        OrientedBox box = triggers::box_of(a.pos, a.yaw, a.spec.size);
        std::array<V3, 8> corner{};
        box_corners(box.origin, box.axes, box.min, box.max, corner);
        const std::array<std::array<int, 2>, 12> edges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                  {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
        const uint32_t colour = (get_colour(a.spec.water ? Kind::kWater : Kind::kKill) & 0x00FFFFFF) | 0xFF000000;
        for (const auto& e : edges)
            (void)painter.get_seg(core::at(corner, core::at(e, 0)), core::at(corner, core::at(e, 1)), colour, 2.0f);
    }
    // Grind rails: the rail manager's active segments near the camera.
    const modules::Skate* skate = modules::Skate::sole();
    auto* rails = skate ? skate->rail_manager().get() : nullptr;
    if (rails && !rails->on_object() && rails->nodes()) {
        int drawn = 0;
        for (int i = 0; i < rails->num(); ++i) {
            const objects::GrindPoint& node = core::at(rails->node_run(), i);
            if (!node.succ_ || (node.flag_bits_ & objects::kRailFlagClimbing) ||
                !(node.flag_bits_ & objects::kRailFlagActive))
                continue;
            const V3 a = {.x = node.at_point_.x, .y = node.at_point_.y, .z = node.at_point_.z};
            const V3 b = {.x = node.succ_->at_point_.x, .y = node.succ_->at_point_.y, .z = node.succ_->at_point_.z};
            if (painter.span_to(a) > kRadius && painter.span_to(b) > kRadius)
                continue;
            if (painter.get_seg(a, b, kRail, 3.0f))
                ++drawn;
        }
        level_editor_skate_view_state().rail_segments_ = drawn;
    }
}

void get_legend(float view_x, float view_y, float view_width, float view_height, float scale)
{
    if (!get_shown())
        return;
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width - 12.0f * scale, view_y + view_height - 12.0f * scale),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##skateLegend", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Skateability");
    float box = ImGui::GetTextLineHeight();
    auto row = [&](uint32_t argb, const char* name, int count) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const uint32_t abgr = (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFF) | ((argb & 0xFF) << 16);
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + box, at.y + box), abgr | 0xFF000000u);
        ImGui::Dummy(ImVec2(box, box));
        ImGui::SameLine();
        if (count >= 0)
            gui::text("%s  %d", name, count);
        else
            ImGui::TextUnformatted(name);
    };
    for (int k = 1; k < kKinds; ++k)
        row(get_colour(static_cast<Kind>(k)), get_name(static_cast<Kind>(k)), core::at(level_editor_skate_view_state().counts_, k));
    row(kRail, "Grind rail (the rail manager's)", level_editor_skate_view_state().rail_segments_);
    gui::text_disabled("Slope limit: normal y %.2f", game_threshold());
    if (level_editor_skate_view_state().capped_)
        gui::text_disabled("(nearest %d faces)", kMaxFaces);
    ImGui::End();
}

} // namespace frontend::level_editor::skate_view
