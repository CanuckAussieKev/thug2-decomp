// NOT ORIGINAL - the level editor's two-pick gap wizard (ours). See level_editor_gap_wizard.h.

#include "editor/level_editor_gap_wizard.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <array>

#include "engine/render/coll_obj.h"
#include "base/core/enum_ops.h"
#include "editor/level_editor.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_triggers.h"
#include "base/maths/vec2.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/rail_manager.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "game/skate/skate_module.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::gap_wizard {

namespace {

constexpr float kFloorNormalY = 0.7F; // a face whose normal is this upright is floor
constexpr float kWallNormalY = 0.2F;  // a face whose normal is this level is a wall
constexpr float kTinyLength = 1e-6F;
constexpr float kRailGateRise = 30.0F; // a rail's gate sits this far above the spot
constexpr float kLipGateRise = 200.0F;
constexpr float kTinyOutLength = 1e-4F;  // a wall normal flatter than this has no outward direction
constexpr float kWallGateOffset = 80.0F; // a wall's gate stands this far out from the wall
constexpr float kAreaGateRise = 150.0F;
constexpr std::size_t kGapNameSize = 64;
constexpr float kRailPickReach = 14.0F;    // a rail is picked within this many pixels
constexpr float kBehindWallMargin = 60.0F; // a rail point this far past the wall is behind it
constexpr float kGateLineWidth = 2.0F;
constexpr double kHintSeconds = 6.0;
constexpr uint32_t kRubberBandColour = 0xFFFFFFFF; // ARGB
constexpr float kRubberBandWidth = 2.0F;
constexpr std::size_t kMeasureTextSize = 64;
constexpr float kHoverDotRadius = 5.0F;
constexpr uint32_t kHoverBandColour = 0x90FFFFFF; // ARGB
constexpr float kWindowTopOffset = 80.0F;         // the window's first position: below the view's top edge
constexpr float kWindowPivotX = 0.5F;             // centred on its position
constexpr float kWindowWidth = 320.0F;
constexpr V3 kRailGateSize = V3{.x = 120.0F, .y = 120.0F, .z = 60.0F};
constexpr V3 kLipGateSize = V3{.x = 300.0F, .y = 400.0F, .z = 60.0F};
constexpr V3 kWallGateSize = V3{.x = 300.0F, .y = 300.0F, .z = 60.0F};
constexpr V3 kAreaGateSize = V3{.x = 300.0F, .y = 300.0F, .z = 60.0F};

constexpr float kHalf = 0.5F; // half

} // namespace

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
    const bool vert = core::any(face_flags & render::FaceFlag::kVert);
    if (!vert && normal_y >= kFloorNormalY) {
        return SpotKind::kArea;
    }
    if (!vert && std::fabs(normal_y) < kWallNormalY) {
        return SpotKind::kWall;
    }
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
    if (start.kind == SpotKind::kRail && end.kind == SpotKind::kRail) {
        return KindTag::kGrind;
    }
    if (start.kind == SpotKind::kWall || end.kind == SpotKind::kWall) {
        return KindTag::kWallride;
    }
    return KindTag::kAir;
}

float get_heading_of(V3 direction)
{
    if (std::fabs(direction.x) < kTinyLength && std::fabs(direction.z) < kTinyLength) {
        return 0.0F;
    }
    return std::atan2(direction.x, direction.z);
}

Gate gate_for(const Spot& spot, V3 toward)
{
    Gate gate;
    const V3 to = toward - spot.point;
    switch (spot.kind) {
    case SpotKind::kRail: {
        // (across the rail, the skater grinding through it: its thin side along the rail, pointing to the other end)
        V3 along = spot.along;
        if (get_dot(along, to) < 0.0F) {
            along = along * -1.0F;
        }
        gate.yaw = get_heading_of(get_dot(along, along) > kTinyLength ? along : to);
        gate.size = kRailGateSize;
        gate.pos = spot.point + V3{.x = 0.0F, .y = kRailGateRise, .z = 0.0F};
        break;
    }
    case SpotKind::kLip:
        gate.yaw = get_heading_of(to);
        gate.size = kLipGateSize;
        gate.pos = spot.point + V3{.x = 0.0F, .y = kLipGateRise, .z = 0.0F};
        break;
    case SpotKind::kWall: {
        V3 out = {.x = spot.normal.x, .y = 0.0F, .z = spot.normal.z};
        const float length = std::sqrt(get_dot(out, out));
        out = length > kTinyOutLength ? out * (1.0F / length) : V3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        gate.yaw = get_heading_of(to);
        gate.size = kWallGateSize;
        gate.pos = spot.point + out * kWallGateOffset;
        break;
    }
    default:
        gate.yaw = get_heading_of(to);
        gate.size = kAreaGateSize;
        gate.pos = spot.point + V3{.x = 0.0F, .y = kAreaGateRise, .z = 0.0F};
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
    std::array<char, kGapNameSize> name_ = {"My Gap"};
    int score_ = 250;
    int type_{};
    std::vector<std::function<void()>> queue_;
};

LevelEditorGapWizardState& level_editor_gap_wizard_state()
{
    static core::NoDestroy<LevelEditorGapWizardState> state;
    return state.get();
}

struct Hit {
    V3 point, normal;
    render::FaceFlag flags;
};

// The game's collision ray (a ProbeRay, as level_editor_ruler.cpp's): the point, the face's normal and flags.
bool get_cast(V3 from, V3 to, Hit* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    // (the feeler's hit record at +0x24: normal +0x00, point +0x10, face flags +0x2C - coll_line.h's CollisionHit)
    const auto& hit_normal = ray->hit().normal;
    const auto& hit_point = ray->hit().point;
    hit->normal = {.x = hit_normal[0], .y = hit_normal[1], .z = hit_normal[2]};
    hit->point = {.x = hit_point[0], .y = hit_point[1], .z = hit_point[2]};
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
    const float x = at.x;
    const float y = at.y;
    const objects::GrindRailHub* rails = get_rails();
    if (!rails) {
        return false;
    }
    float best = kRailPickReach;
    bool found = false;
    for (int i = 0; i < rails->num(); ++i) {
        const objects::GrindPoint& node = core::at(rails->node_run(), i);
        if (!node.succ() || ((node.flag_bits() & objects::kRailFlagClimbing) != 0U) ||
            ((node.flag_bits() & objects::kRailFlagActive) == 0U)) {
            continue;
        }
        const V3 point_a = {.x = node.at_point().x, .y = node.at_point().y, .z = node.at_point().z};
        const V3 point_b = {
            .x = node.succ()->at_point().x, .y = node.succ()->at_point().y, .z = node.succ()->at_point().z};
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (!project_to_back(point_a, &ax, &ay) || !project_to_back(point_b, &bx, &by)) {
            continue;
        }
        const float distance = distance_to_segment({.x = x, .y = y}, {.x = ax, .y = ay}, {.x = bx, .y = by});
        if (distance >= best) {
            continue;
        }
        // (where along it: the screen's nearest point, carried to the world segment)
        const float sx = bx - ax;
        const float sy = by - ay;
        const float length2 = (sx * sx) + (sy * sy);
        const float fraction =
            length2 > 1e-6F ? std::clamp((((x - ax) * sx) + ((y - ay) * sy)) / length2, 0.0F, 1.0F) : 0.0F;
        const V3 point = point_a + (point_b - point_a) * fraction;
        const V3 to = point - origin;
        if (get_dot(to, unit_dir) > max_t + kBehindWallMargin) {
            continue; // (behind a wall)
        }
        best = distance;
        found = true;
        spot->kind = SpotKind::kRail;
        spot->point = point;
        spot->normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
        spot->along = make_unit(point_b - point_a);
    }
    return found;
}

bool spot_at(V3 origin, V3 unit_dir, float x, float y, Spot* spot)
{
    Hit hit{};
    const bool have_hit = get_cast(origin, origin + unit_dir * 20000.0F, &hit);
    const float max_t = have_hit ? get_dot(hit.point - origin, unit_dir) : 20000.0F;
    if (rail_near({.origin = origin, .dir = unit_dir}, {.x = x, .y = y}, max_t, spot)) {
        return true;
    }
    if (!have_hit) {
        return false;
    }
    spot->kind = kind_of_face({.normal_y_ = hit.normal.y, .face_flags_ = hit.flags});
    spot->point = hit.point;
    spot->normal = hit.normal;
    spot->along = {};
    return true;
}

void draw_gate(const overlays::Painter& painter, const Gate& gate, uint32_t argb)
{
    OrientedBox box = triggers::box_of(gate.pos, gate.yaw, gate.size);
    std::array<V3, kBoxCornerCount> corners{};
    box_corners(box.origin, box.axes, box.min, box.max, corners);
    const std::array<std::array<int, 2>, 12> edges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    for (const auto& edge : edges) {
        (void)painter.get_seg(core::at(corners, core::at(edge, 0)), core::at(corners, core::at(edge, 1)), argb,
                              kGateLineWidth);
    }
}

void get_make()
{
    triggers::Placed start;
    triggers::Placed end;
    const Gate gate = gate_for(core::at(level_editor_gap_wizard_state().spots_, 0),
                               core::at(level_editor_gap_wizard_state().spots_, 1).point);
    const Gate gate_b = gate_for(core::at(level_editor_gap_wizard_state().spots_, 1),
                                 core::at(level_editor_gap_wizard_state().spots_, 0).point);
    // (a box is the same turned half round: the end's gate facing back to the start lies across the same line)
    start.pos = gate.pos, start.yaw = gate.yaw, start.spec.size = gate.size;
    end.pos = gate_b.pos, end.yaw = gate_b.yaw, end.spec.size = gate_b.size;
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
    if (on) {
        show_message("Gap Wizard: click the gap start", kHintSeconds);
    }
}

bool get_cancel()
{
    if (!get_armed()) {
        return false;
    }
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
    if (!level_editor_gap_wizard_state().armed_ || level_editor_gap_wizard_state().picked_ >= 2) {
        return false;
    }
    Spot spot;
    if (!spot_at(origin, unit_dir, x, y, &spot)) {
        show_message("Gap Wizard: click a rail, lip, wall or ground");
        return true;
    }
    core::at(level_editor_gap_wizard_state().spots_, level_editor_gap_wizard_state().picked_++) = spot;
    if (level_editor_gap_wizard_state().picked_ == 1) {
        show_message(std::string("Gap starts at ") + spot_name(spot.kind) + " - click the gap end", kHintSeconds);
    } else {
        level_editor_gap_wizard_state().armed_ = false;
        level_editor_gap_wizard_state().form_open_ = true;
        level_editor_gap_wizard_state().type_ = static_cast<int>(suggest(core::at(level_editor_gap_wizard_state().spots_, 0), core::at(level_editor_gap_wizard_state().spots_, 1)));
        show_message(std::string("Gap ends at ") + spot_name(spot.kind) + "", kHintSeconds);
    }
    return true;
}

void add_overlay(const overlays::Painter& painter)
{
    if (!get_armed()) {
        return;
    }
    constexpr uint32_t kStart = 0xFF62E26A;
    constexpr uint32_t kEnd = 0xFFF2D24A;
    constexpr uint32_t kHover = 0xC0FFFFFF;
    if (level_editor_gap_wizard_state().picked_ >= 1) {
        V3 other{};
        if (level_editor_gap_wizard_state().picked_ >= 2) {
            other = core::at(level_editor_gap_wizard_state().spots_, 1).point;
        } else if (level_editor_gap_wizard_state().have_hover_) {
            other = level_editor_gap_wizard_state().hover_.point;
        } else {
            other = core::at(level_editor_gap_wizard_state().spots_, 0).point + V3{.x = 0, .y = 0, .z = 1};
        }
        draw_gate(painter, gate_for(core::at(level_editor_gap_wizard_state().spots_, 0), other), kStart);
        painter.draw_label(core::at(level_editor_gap_wizard_state().spots_, 0).point, kStart, std::string("start: ") + spot_name(core::at(level_editor_gap_wizard_state().spots_, 0).kind));
    }
    if (level_editor_gap_wizard_state().picked_ >= 2) {
        const Gate gate = gate_for(core::at(level_editor_gap_wizard_state().spots_, 1),
                                   core::at(level_editor_gap_wizard_state().spots_, 0).point);
        draw_gate(painter, gate, kEnd);
        painter.draw_label(core::at(level_editor_gap_wizard_state().spots_, 1).point, kEnd, std::string("end: ") + spot_name(core::at(level_editor_gap_wizard_state().spots_, 1).kind));
        (void)painter.get_seg(core::at(level_editor_gap_wizard_state().spots_, 0).point, core::at(level_editor_gap_wizard_state().spots_, 1).point, kRubberBandColour, kRubberBandWidth);
        const V3 delta = core::at(level_editor_gap_wizard_state().spots_, 1).point -
                         core::at(level_editor_gap_wizard_state().spots_, 0).point;
        std::array<char, kMeasureTextSize> text{};
        text::format_to(text, "%.0f across, %.0f %s", std::sqrt((delta.x * delta.x) + (delta.z * delta.z)),
                        std::fabs(delta.y), delta.y >= 0.0F ? "up" : "down");
        painter.draw_label((core::at(level_editor_gap_wizard_state().spots_, 0).point + core::at(level_editor_gap_wizard_state().spots_, 1).point) * kHalf, kRubberBandColour, text.data());
    } else if (level_editor_gap_wizard_state().have_hover_) {
        painter.get_dot(level_editor_gap_wizard_state().hover_.point, kHoverDotRadius, kHover);
        painter.draw_label(level_editor_gap_wizard_state().hover_.point, kHover, spot_name(level_editor_gap_wizard_state().hover_.kind));
        if (level_editor_gap_wizard_state().picked_ == 1) {
            (void)painter.get_seg(core::at(level_editor_gap_wizard_state().spots_, 0).point,
                                  level_editor_gap_wizard_state().hover_.point, kHoverBandColour, 1.0F);
        }
    }
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_gap_wizard_state().queue_);
    for (auto& action : queue) {
        action();
    }
}

void pane(PaneArea area, float scale)
{
    const auto view_x = area.x;
    const auto view_y = area.y;
    const auto view_width = area.width;
    const auto view_height = area.height;
    (void)view_height;
    if (!level_editor_gap_wizard_state().form_open_) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(view_x + (view_width * kHalf), view_y + (kWindowTopOffset * scale)),
                            ImGuiCond_Appearing, ImVec2(kWindowPivotX, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth * scale, 0.0F), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Gap Wizard###gapWizard", &open,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) {
            level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(false); });
        }
        return;
    }
    gui::text_disabled("From %s to %s", spot_name(core::at(level_editor_gap_wizard_state().spots_, 0).kind), spot_name(core::at(level_editor_gap_wizard_state().spots_, 1).kind));
    ImGui::InputText("Name", level_editor_gap_wizard_state().name_.data(), sizeof(level_editor_gap_wizard_state().name_));
    ImGui::SliderInt("Score", &level_editor_gap_wizard_state().score_, kMinScore, kMaxScore, "%d", ImGuiSliderFlags_AlwaysClamp);
    for (int type_index = 0; type_index < kTypes; ++type_index) {
        if (type_index != 0) {
            ImGui::SameLine();
        }
        ImGui::RadioButton(current_type_name(static_cast<KindTag>(type_index)), &level_editor_gap_wizard_state().type_,
                           type_index);
    }
    std::string flags;
    for (const std::string& flag : flags_for(static_cast<KindTag>(level_editor_gap_wizard_state().type_))) {
        flags += (flags.empty() ? "" : ", ") + flag;
    }
    gui::text_disabled("StartGap flags: %s", flags.c_str());
    ImGui::BeginDisabled();
    ImGui::RadioButton("Skitch", false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("Not supported in THUG2");
    }
    ImGui::Separator();
    const bool named = std::ranges::any_of(std::string_view(level_editor_gap_wizard_state().name_.data()),
                                           [](char character) { return character != ' '; });
    ImGui::BeginDisabled(!named);
    if (ImGui::Button("Create Gap")) {
        level_editor_gap_wizard_state().queue_.emplace_back([] {
            get_make();
            level_editor_gap_wizard_state().form_open_ = false;
            level_editor_gap_wizard_state().picked_ = 0;
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Pick again")) {
        level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(true); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(false); });
    }
    ImGui::End();
    if (!open) {
        level_editor_gap_wizard_state().queue_.emplace_back([] { set_armed(false); });
    }
}

} // namespace frontend::level_editor::gap_wizard
