// NOT ORIGINAL - the level editor's two-pick gap wizard (ours). See level_editor_gap_wizard.h.

#include "editor/level_editor_gap_wizard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_triggers.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/rail_manager.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::gap_wizard {

// --- the pure parts -----------------------------------------------------------------------------------------------

const char* spot_name(SpotKind kind)
{
    switch (kind) {
    case SpotKind::kLip:
        return "a lip";
    case SpotKind::kWall:
        return "a wall";
    case SpotKind::kRail:
        return "a rail";
    default:
        return "an area";
    }
}

SpotKind kind_of_face(float normal_y, uint16_t face_flags)
{
    const bool vert = (face_flags & 0x08) != 0;
    if (!vert && normal_y >= 0.7f)
        return SpotKind::kArea;
    if (!vert && std::fabs(normal_y) < 0.2f)
        return SpotKind::kWall;
    return SpotKind::kLip;
}

const char* current_type_name(KindTag type)
{
    switch (type) {
    case KindTag::kGrind:
        return "Grind";
    case KindTag::kLip:
        return "Lip";
    case KindTag::kManual:
        return "Manual";
    case KindTag::kWallride:
        return "Wallride";
    default:
        return "Air";
    }
}

std::vector<std::string> flags_for(KindTag type)
{
    switch (type) {
    case KindTag::kGrind:
        return {"REQUIRE_RAIL", "CANCEL_GROUND"};
    case KindTag::kLip:
        return {"REQUIRE_LIP"};
    case KindTag::kManual:
        return {"PURE_MANUAL"};
    case KindTag::kWallride:
        return {"REQUIRE_WALLRIDE", "CANCEL_GROUND"};
    default:
        return {"CANCEL_GROUND"};
    }
}

KindTag suggest(const Spot& start, const Spot& end)
{
    if (start.kind == SpotKind::kRail && end.kind == SpotKind::kRail)
        return KindTag::kGrind;
    if (start.kind == SpotKind::kWall || end.kind == SpotKind::kWall)
        return KindTag::kWallride;
    return KindTag::kAir;
}

float get_heading_of(V3 d)
{
    if (std::fabs(d.x) < 1e-6f && std::fabs(d.z) < 1e-6f)
        return 0.0f;
    return std::atan2(d.x, d.z);
}

Gate gate_for(const Spot& spot, V3 toward)
{
    Gate gate;
    const V3 to = toward - spot.point;
    switch (spot.kind) {
    case SpotKind::kRail: {
        // (across the rail, the skater grinding through it: its thin side along the rail, pointing to the other end)
        V3 along = spot.along;
        if (get_dot(along, to) < 0.0f)
            along = along * -1.0f;
        gate.yaw = get_heading_of(get_dot(along, along) > 1e-6f ? along : to);
        gate.size = {.x = 120.0f, .y = 120.0f, .z = 60.0f};
        gate.pos = spot.point + V3{.x = 0.0f, .y = 30.0f, .z = 0.0f};
        break;
    }
    case SpotKind::kLip:
        gate.yaw = get_heading_of(to);
        gate.size = {.x = 300.0f, .y = 400.0f, .z = 60.0f};
        gate.pos = spot.point + V3{.x = 0.0f, .y = 200.0f, .z = 0.0f};
        break;
    case SpotKind::kWall: {
        V3 out = {.x = spot.normal.x, .y = 0.0f, .z = spot.normal.z};
        const float length = std::sqrt(get_dot(out, out));
        out = length > 1e-4f ? out * (1.0f / length) : V3{.x = 0.0f, .y = 0.0f, .z = 0.0f};
        gate.yaw = get_heading_of(to);
        gate.size = {.x = 300.0f, .y = 300.0f, .z = 60.0f};
        gate.pos = spot.point + out * 80.0f;
        break;
    }
    default:
        gate.yaw = get_heading_of(to);
        gate.size = {.x = 300.0f, .y = 300.0f, .z = 60.0f};
        gate.pos = spot.point + V3{.x = 0.0f, .y = 150.0f, .z = 0.0f};
        break;
    }
    return gate;
}

int clamp_score(int score) { return std::max(kMinScore, std::min(kMaxScore, score)); }

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

bool g_armed;
int g_picked; // (0, 1 or 2 ends)
std::array<Spot, 2> g_spots;
bool g_have_hover;
Spot g_hover;
// The form.
bool g_form_open;
std::array<char, 64> g_name = {"My Gap"};
int g_score = 250;
int g_type;
std::vector<std::function<void()>> g_queue;

struct Hit {
    V3 point, normal;
    uint16_t flags;
};

// The game's collision ray (a ProbeRay, as level_editor_ruler.cpp's): the point, the face's normal and flags.
bool get_cast(V3 from, V3 to, Hit* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    // (the feeler's hit record at +0x24: normal +0x00, point +0x10, face flags +0x2C - coll_line.h's CollisionHit)
    const auto& n = ray->hit_.normal;
    const auto& p = ray->hit_.point;
    hit->normal = {.x = n[0], .y = n[1], .z = n[2]};
    hit->point = {.x = p[0], .y = p[1], .z = p[2]};
    hit->flags = ray->hit_.face_flags_;
    return true;
}

objects::GrindRailHub* get_rails()
{
    const modules::Skate* skate = modules::g_skate_module.get();
    auto* manager = skate ? skate->rail_manager_.get() : nullptr;
    return manager && !manager->on_object_ && manager->nodes_ ? manager : nullptr;
}

// The rail segment nearest the cursor within 14 pixels (and not behind what the ray hits first): the point on it.
bool rail_near(V3 origin, V3 unit_dir, float x, float y, float max_t, Spot* spot)
{
    const objects::GrindRailHub* rails = get_rails();
    if (!rails)
        return false;
    float best = 14.0f;
    bool found = false;
    for (int i = 0; i < rails->num_; ++i) {
        const objects::GrindPoint& node = core::at(rails->node_run(), i);
        if (!node.succ_ || (node.flag_bits_ & objects::kRailFlagClimbing) ||
            !(node.flag_bits_ & objects::kRailFlagActive))
            continue;
        const V3 a = {.x = node.at_point_.x, .y = node.at_point_.y, .z = node.at_point_.z};
        const V3 b = {.x = node.succ_->at_point_.x, .y = node.succ_->at_point_.y, .z = node.succ_->at_point_.z};
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (!project_to_back(a, &ax, &ay) || !project_to_back(b, &bx, &by))
            continue;
        const float d = distance_to_segment(x, y, ax, ay, bx, by);
        if (d >= best)
            continue;
        // (where along it: the screen's nearest point, carried to the world segment)
        const float sx = bx - ax;
        const float sy = by - ay;
        const float length2 = sx * sx + sy * sy;
        const float t = length2 > 1e-6f ? std::clamp(((x - ax) * sx + (y - ay) * sy) / length2, 0.0f, 1.0f) : 0.0f;
        const V3 p = a + (b - a) * t;
        const V3 to = p - origin;
        if (get_dot(to, unit_dir) > max_t + 60.0f)
            continue; // (behind a wall)
        best = d;
        found = true;
        spot->kind = SpotKind::kRail;
        spot->point = p;
        spot->normal = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
        spot->along = make_unit(b - a);
    }
    return found;
}

bool spot_at(V3 origin, V3 unit_dir, float x, float y, Spot* spot)
{
    Hit hit{};
    const bool have_hit = get_cast(origin, origin + unit_dir * 20000.0f, &hit);
    const float max_t = have_hit ? get_dot(hit.point - origin, unit_dir) : 20000.0f;
    if (rail_near(origin, unit_dir, x, y, max_t, spot))
        return true;
    if (!have_hit)
        return false;
    spot->kind = kind_of_face(hit.normal.y, hit.flags);
    spot->point = hit.point;
    spot->normal = hit.normal;
    spot->along = {};
    return true;
}

void draw_gate(const overlays::Painter& painter, const Gate& gate, uint32_t argb)
{
    OrientedBox box = triggers::box_of(gate.pos, gate.yaw, gate.size);
    std::array<V3, 8> c{};
    box_corners(box.origin, box.axes, box.min, box.max, c);
    const std::array<std::array<int, 2>, 12> edges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    for (const auto& e : edges)
        (void)painter.get_seg(core::at(c, core::at(e, 0)), core::at(c, core::at(e, 1)), argb, 2.0f);
}

void get_make()
{
    triggers::Placed start, end;
    const Gate a = gate_for(core::at(g_spots, 0), core::at(g_spots, 1).point);
    const Gate b = gate_for(core::at(g_spots, 1), core::at(g_spots, 0).point);
    // (a box is the same turned half round: the end's gate facing back to the start lies across the same line)
    start.pos = a.pos, start.yaw = a.yaw, start.spec.size = a.size;
    end.pos = b.pos, end.yaw = b.yaw, end.spec.size = b.size;
    start.spec.text = g_name.data();
    start.spec.score = clamp_score(g_score);
    start.spec.flags = flags_for(static_cast<KindTag>(g_type));
    get_log("level editor: gap wizard - \"%s\" (%d, %s) from %s to %s", g_name.data(), start.spec.score,
            current_type_name(static_cast<KindTag>(g_type)), spot_name(core::at(g_spots, 0).kind), spot_name(core::at(g_spots, 1).kind));
    triggers::get_queue([start, end] { triggers::add_gap_pair(start, end); });
}

} // namespace

bool get_armed() { return g_armed || g_form_open; }
bool get_picking() { return g_armed && g_picked < 2; }

void set_armed(bool on)
{
    g_armed = on;
    g_picked = 0;
    g_form_open = false;
    g_have_hover = false;
    if (on)
        show_message("Gap Wizard: click the gap start", 6.0);
}

bool get_cancel()
{
    if (!get_armed())
        return false;
    set_armed(false);
    show_message("Gap Wizard cancelled");
    return true;
}

void get_hover(V3 origin, V3 unit_dir, float x, float y)
{
    g_have_hover = g_armed && g_picked < 2 && spot_at(origin, unit_dir, x, y, &g_hover);
}

bool get_click(V3 origin, V3 unit_dir, float x, float y)
{
    if (!g_armed || g_picked >= 2)
        return false;
    Spot spot;
    if (!spot_at(origin, unit_dir, x, y, &spot)) {
        show_message("Gap Wizard: click a rail, lip, wall or ground");
        return true;
    }
    core::at(g_spots, g_picked++) = spot;
    if (g_picked == 1) {
        show_message(std::string("Gap starts at ") + spot_name(spot.kind) + " - click the gap end", 6.0);
    } else {
        g_armed = false;
        g_form_open = true;
        g_type = static_cast<int>(suggest(core::at(g_spots, 0), core::at(g_spots, 1)));
        show_message(std::string("Gap ends at ") + spot_name(spot.kind) + "", 6.0);
    }
    return true;
}

void add_overlay(const overlays::Painter& painter)
{
    if (!get_armed())
        return;
    constexpr uint32_t kStart = 0xFF62E26A, kEnd = 0xFFF2D24A, kHover = 0xC0FFFFFF;
    if (g_picked >= 1) {
        const V3 other = g_picked >= 2  ? core::at(g_spots, 1).point
                         : g_have_hover ? g_hover.point
                                        : core::at(g_spots, 0).point + V3{.x = 0, .y = 0, .z = 1};
        draw_gate(painter, gate_for(core::at(g_spots, 0), other), kStart);
        painter.draw_label(core::at(g_spots, 0).point, kStart, std::string("start: ") + spot_name(core::at(g_spots, 0).kind));
    }
    if (g_picked >= 2) {
        const Gate b = gate_for(core::at(g_spots, 1), core::at(g_spots, 0).point);
        draw_gate(painter, b, kEnd);
        painter.draw_label(core::at(g_spots, 1).point, kEnd, std::string("end: ") + spot_name(core::at(g_spots, 1).kind));
        (void)painter.get_seg(core::at(g_spots, 0).point, core::at(g_spots, 1).point, 0xFFFFFFFF, 2.0f);
        const V3 d = core::at(g_spots, 1).point - core::at(g_spots, 0).point;
        std::array<char, 64> text{};
        text::format_to(text, "%.0f across, %.0f %s", std::sqrt(d.x * d.x + d.z * d.z), std::fabs(d.y),
                        d.y >= 0.0f ? "up" : "down");
        painter.draw_label((core::at(g_spots, 0).point + core::at(g_spots, 1).point) * 0.5f, 0xFFFFFFFF, text.data());
    } else if (g_have_hover) {
        painter.get_dot(g_hover.point, 5.0f, kHover);
        painter.draw_label(g_hover.point, kHover, spot_name(g_hover.kind));
        if (g_picked == 1)
            (void)painter.get_seg(core::at(g_spots, 0).point, g_hover.point, 0x90FFFFFF, 1.0f);
    }
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

void pane(float view_x, float view_y, float view_width, float view_height, float scale)
{
    (void)view_height;
    if (!g_form_open)
        return;
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width * 0.5f, view_y + 80.0f * scale), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f * scale, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Gap Wizard###gapWizard", &open,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open)
            g_queue.emplace_back([] { set_armed(false); });
        return;
    }
    gui::text_disabled("From %s to %s", spot_name(core::at(g_spots, 0).kind), spot_name(core::at(g_spots, 1).kind));
    ImGui::InputText("Name", g_name.data(), sizeof(g_name));
    ImGui::SliderInt("Score", &g_score, kMinScore, kMaxScore, "%d", ImGuiSliderFlags_AlwaysClamp);
    for (int t = 0; t < kTypes; ++t) {
        if (t)
            ImGui::SameLine();
        ImGui::RadioButton(current_type_name(static_cast<KindTag>(t)), &g_type, t);
    }
    std::string flags;
    for (const std::string& f : flags_for(static_cast<KindTag>(g_type)))
        flags += (flags.empty() ? "" : ", ") + f;
    gui::text_disabled("StartGap flags: %s", flags.c_str());
    ImGui::BeginDisabled();
    ImGui::RadioButton("Skitch", false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        gui::tooltip("Not supported in THUG2");
    ImGui::Separator();
    const bool named = std::ranges::any_of(std::string_view(g_name.data()), [](char c) { return c != ' '; });
    ImGui::BeginDisabled(!named);
    if (ImGui::Button("Create Gap"))
        g_queue.emplace_back([] {
            get_make();
            g_form_open = false;
            g_picked = 0;
        });
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Pick again"))
        g_queue.emplace_back([] { set_armed(true); });
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        g_queue.emplace_back([] { set_armed(false); });
    ImGui::End();
    if (!open)
        g_queue.emplace_back([] { set_armed(false); });
}

} // namespace frontend::level_editor::gap_wizard
