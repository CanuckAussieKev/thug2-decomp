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
#include "engine/maths/vec2.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/rail_manager.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"

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

SpotKind kind_of_face(FaceKindOf face)
{
    const auto normal_y = face.normal_y_;
    const auto face_flags = face.face_flags_;
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

// NOT ORIGINAL grouping - level_editor_gap_wizard.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_gap_wizard_state()).
struct LevelEditorGapWizardState {
    bool armed_{};
    int picked_{}; // (0, 1 or 2 ends)
    std::array<Spot, 2> spots_{};
    bool have_hover_{};
    Spot hover_{};
    // The form.
    bool form_open_{};
    std::array<char, 64> name_ = {"My Gap"};
    int score_ = 250;
    int type_{};
    std::vector<std::function<void()>> queue_{};
};

LevelEditorGapWizardState& level_editor_gap_wizard_state()
{
    static core::NoDestroy<LevelEditorGapWizardState> state;
    return state.get();
}

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
    const auto& n = ray->hit().normal;
    const auto& p = ray->hit().point;
    hit->normal = {.x = n[0], .y = n[1], .z = n[2]};
    hit->point = {.x = p[0], .y = p[1], .z = p[2]};
    hit->flags = ray->hit().face_flags_;
    return true;
}

objects::GrindRailHub* get_rails()
{
    const modules::Skate* skate = modules::Skate::sole();
    auto* manager = skate ? skate->rail_manager().get() : nullptr;
    return manager && !manager->on_object() && manager->nodes() ? manager : nullptr;
}

// The rail segment nearest the cursor within 14 pixels (and not behind what the ray hits first): the point on it.
bool rail_near(Ray ray, maths::Vec2 at, float max_t, Spot* spot)
{
    const auto origin = ray.origin;
    const auto unit_dir = ray.dir;
    const float x = at.x, y = at.y;
    const objects::GrindRailHub* rails = get_rails();
    if (!rails)
        return false;
    float best = 14.0f;
    bool found = false;
    for (int i = 0; i < rails->num(); ++i) {
        const objects::GrindPoint& node = core::at(rails->node_run(), i);
        if (!node.succ() || (node.flag_bits() & objects::kRailFlagClimbing) ||
            !(node.flag_bits() & objects::kRailFlagActive))
            continue;
        const V3 a = {.x = node.at_point().x, .y = node.at_point().y, .z = node.at_point().z};
        const V3 b = {.x = node.succ()->at_point().x, .y = node.succ()->at_point().y, .z = node.succ()->at_point().z};
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (!project_to_back(a, &ax, &ay) || !project_to_back(b, &bx, &by))
            continue;
        const float d = distance_to_segment({.x = x, .y = y}, {.x = ax, .y = ay}, {.x = bx, .y = by});
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
    if (rail_near({.origin = origin, .dir = unit_dir}, {.x = x, .y = y}, max_t, spot))
        return true;
    if (!have_hit)
        return false;
    spot->kind = kind_of_face({.normal_y_ = hit.normal.y, .face_flags_ = hit.flags});
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
    const Gate a = gate_for(core::at(level_editor_gap_wizard_state().spots_, 0), core::at(level_editor_gap_wizard_state().spots_, 1).point);
    const Gate b = gate_for(core::at(level_editor_gap_wizard_state().spots_, 1), core::at(level_editor_gap_wizard_state().spots_, 0).point);
    // (a box is the same turned half round: the end's gate facing back to the start lies across the same line)
    start.pos = a.pos, start.yaw = a.yaw, start.spec.size = a.size;
    end.pos = b.pos, end.yaw = b.yaw, end.spec.size = b.size;
    start.spec.text = level_editor_gap_wizard_state().name_.data();
    start.spec.score = clamp_score(level_editor_gap_wizard_state().score_);
    start.spec.flags = flags_for(static_cast<KindTag>(level_editor_gap_wizard_state().type_));
    get_log("level editor: gap wizard - \"%s\" (%d, %s) from %s to %s", level_editor_gap_wizard_state().name_.data(), start.spec.score,
            current_type_name(static_cast<KindTag>(level_editor_gap_wizard_state().type_)), spot_name(core::at(level_editor_gap_wizard_state().spots_, 0).kind), spot_name(core::at(level_editor_gap_wizard_state().spots_, 1).kind));
    triggers::get_queue([start, end] { triggers::add_gap_pair(start, end); });
}

} // namespace

bool get_armed() { return level_editor_gap_wizard_state().armed_ || level_editor_gap_wizard_state().form_open_; }
bool get_picking() { return level_editor_gap_wizard_state().armed_ && level_editor_gap_wizard_state().picked_ < 2; }

void set_armed(bool on)
{
    level_editor_gap_wizard_state().armed_ = on;
    level_editor_gap_wizard_state().picked_ = 0;
    level_editor_gap_wizard_state().form_open_ = false;
    level_editor_gap_wizard_state().have_hover_ = false;
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
    level_editor_gap_wizard_state().have_hover_ = level_editor_gap_wizard_state().armed_ && level_editor_gap_wizard_state().picked_ < 2 && spot_at(origin, unit_dir, x, y, &level_editor_gap_wizard_state().hover_);
}

bool get_click(V3 origin, V3 unit_dir, float x, float y)
{
    if (!level_editor_gap_wizard_state().armed_ || level_editor_gap_wizard_state().picked_ >= 2)
        return false;
    Spot spot;
    if (!spot_at(origin, unit_dir, x, y, &spot)) {
        show_message("Gap Wizard: click a rail, lip, wall or ground");
        return true;
    }
    core::at(level_editor_gap_wizard_state().spots_, level_editor_gap_wizard_state().picked_++) = spot;
    if (level_editor_gap_wizard_state().picked_ == 1) {
        show_message(std::string("Gap starts at ") + spot_name(spot.kind) + " - click the gap end", 6.0);
    } else {
        level_editor_gap_wizard_state().armed_ = false;
        level_editor_gap_wizard_state().form_open_ = true;
        level_editor_gap_wizard_state().type_ = static_cast<int>(suggest(core::at(level_editor_gap_wizard_state().spots_, 0), core::at(level_editor_gap_wizard_state().spots_, 1)));
        show_message(std::string("Gap ends at ") + spot_name(spot.kind) + "", 6.0);
    }
    return true;
}

void add_overlay(const overlays::Painter& painter)
{
    if (!get_armed())
        return;
    constexpr uint32_t kStart = 0xFF62E26A, kEnd = 0xFFF2D24A, kHover = 0xC0FFFFFF;
    if (level_editor_gap_wizard_state().picked_ >= 1) {
        const V3 other = level_editor_gap_wizard_state().picked_ >= 2  ? core::at(level_editor_gap_wizard_state().spots_, 1).point
                         : level_editor_gap_wizard_state().have_hover_ ? level_editor_gap_wizard_state().hover_.point
                                        : core::at(level_editor_gap_wizard_state().spots_, 0).point + V3{.x = 0, .y = 0, .z = 1};
        draw_gate(painter, gate_for(core::at(level_editor_gap_wizard_state().spots_, 0), other), kStart);
        painter.draw_label(core::at(level_editor_gap_wizard_state().spots_, 0).point, kStart, std::string("start: ") + spot_name(core::at(level_editor_gap_wizard_state().spots_, 0).kind));
    }
    if (level_editor_gap_wizard_state().picked_ >= 2) {
        const Gate b = gate_for(core::at(level_editor_gap_wizard_state().spots_, 1), core::at(level_editor_gap_wizard_state().spots_, 0).point);
        draw_gate(painter, b, kEnd);
        painter.draw_label(core::at(level_editor_gap_wizard_state().spots_, 1).point, kEnd, std::string("end: ") + spot_name(core::at(level_editor_gap_wizard_state().spots_, 1).kind));
        (void)painter.get_seg(core::at(level_editor_gap_wizard_state().spots_, 0).point, core::at(level_editor_gap_wizard_state().spots_, 1).point, 0xFFFFFFFF, 2.0f);
        const V3 d = core::at(level_editor_gap_wizard_state().spots_, 1).point - core::at(level_editor_gap_wizard_state().spots_, 0).point;
        std::array<char, 64> text{};
        text::format_to(text, "%.0f across, %.0f %s", std::sqrt(d.x * d.x + d.z * d.z), std::fabs(d.y),
                        d.y >= 0.0f ? "up" : "down");
        painter.draw_label((core::at(level_editor_gap_wizard_state().spots_, 0).point + core::at(level_editor_gap_wizard_state().spots_, 1).point) * 0.5f, 0xFFFFFFFF, text.data());
    } else if (level_editor_gap_wizard_state().have_hover_) {
        painter.get_dot(level_editor_gap_wizard_state().hover_.point, 5.0f, kHover);
        painter.draw_label(level_editor_gap_wizard_state().hover_.point, kHover, spot_name(level_editor_gap_wizard_state().hover_.kind));
        if (level_editor_gap_wizard_state().picked_ == 1)
            (void)painter.get_seg(core::at(level_editor_gap_wizard_state().spots_, 0).point, level_editor_gap_wizard_state().hover_.point, 0x90FFFFFF, 1.0f);
    }
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_gap_wizard_state().queue_);
    for (auto& action : queue)
        action();
}

void pane(PaneArea area, float scale)
{
    const auto view_x = area.x;
    const auto view_y = area.y;
    const auto view_width = area.width;
    const auto view_height = area.height;
    (void)view_height;
    if (!level_editor_gap_wizard_state().form_open_)
        return;
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width * 0.5f, view_y + 80.0f * scale), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f * scale, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Gap Wizard###gapWizard", &open,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open)
            level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(false); });
        return;
    }
    gui::text_disabled("From %s to %s", spot_name(core::at(level_editor_gap_wizard_state().spots_, 0).kind), spot_name(core::at(level_editor_gap_wizard_state().spots_, 1).kind));
    ImGui::InputText("Name", level_editor_gap_wizard_state().name_.data(), sizeof(level_editor_gap_wizard_state().name_));
    ImGui::SliderInt("Score", &level_editor_gap_wizard_state().score_, kMinScore, kMaxScore, "%d", ImGuiSliderFlags_AlwaysClamp);
    for (int t = 0; t < kTypes; ++t) {
        if (t)
            ImGui::SameLine();
        ImGui::RadioButton(current_type_name(static_cast<KindTag>(t)), &level_editor_gap_wizard_state().type_, t);
    }
    std::string flags;
    for (const std::string& f : flags_for(static_cast<KindTag>(level_editor_gap_wizard_state().type_)))
        flags += (flags.empty() ? "" : ", ") + f;
    gui::text_disabled("StartGap flags: %s", flags.c_str());
    ImGui::BeginDisabled();
    ImGui::RadioButton("Skitch", false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        gui::tooltip("Not supported in THUG2");
    ImGui::Separator();
    const bool named = std::ranges::any_of(std::string_view(level_editor_gap_wizard_state().name_.data()), [](char c) { return c != ' '; });
    ImGui::BeginDisabled(!named);
    if (ImGui::Button("Create Gap"))
        level_editor_gap_wizard_state().queue_.emplace_back([] {
            get_make();
            level_editor_gap_wizard_state().form_open_ = false;
            level_editor_gap_wizard_state().picked_ = 0;
        });
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Pick again"))
        level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(true); });
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(false); });
    ImGui::End();
    if (!open)
        level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(false); });
}

} // namespace frontend::level_editor::gap_wizard
