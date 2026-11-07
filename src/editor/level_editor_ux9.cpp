// NOT ORIGINAL - the level editor's UX9 tools: the glue and the Ground tool's form (ours). See level_editor_ux9.h.

#include "editor/level_editor_ux9.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_gap_wizard.h"
#include "editor/level_editor_ground.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_skate_view.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::ux9 {

namespace {

constexpr float kProbeHeightAbove = 300.0F;     // how far above the point the downward probe starts
constexpr float kProbeDepthBelow = 3000.0F;     // how far below the point it ends
constexpr double kFirstCornerHintSeconds = 6.0; // how long the "click the first corner" hint stays
constexpr double kOppositeCornerHintSeconds = 6.0; // how long the "click the opposite corner" hint stays
constexpr double kFormMessageSeconds = 8.0; // how long a message from the Ground form stays
constexpr double kGroundResultSeconds = 5.0; // how long the "Raised / Lowered ground" message stays
constexpr double kGroundErrorSeconds = 8.0; // how long a Ground Tool refusal stays
constexpr float kPanelRightInset = 12.0F;   // the Ground form's gap from the view's right edge, at scale 1
constexpr float kPanelTopOffset = 60.0F;    // its gap from the view's top, at scale 1
constexpr float kPanelWidth = 340.0F;       // its width, at scale 1
constexpr float kGridSpacingMax = 500.0F;   // the grid drag's upper limit
constexpr std::size_t kPieceRowSize = 160; // the text of a "Pieces in this level" row
constexpr float kFrameDistanceFactor = 1.5F;    // the camera stands this many piece-spans away...
constexpr float kFrameDistancePadding = 300.0F; // ...plus this much
constexpr float kGroundRayLength = 20000.0F;    // how far a mouse ray is cast to find the ground
constexpr float kSectorHitMargin = 1000.0F;     // how far outside a piece's box a hit still counts as on it
constexpr float kOutlineThickness = 2.0F;       // the drawn outline's / guide's line width
constexpr std::size_t kSizeLabelSize = 64; // the "w x d" label text
constexpr float kHoverGuideLength = 25.0F; // the guide line drawn from the hovered corner

constexpr float kHalf = 0.5F; // half

// NOT ORIGINAL grouping - level_editor_ux9.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_ux9_state()).
struct LevelEditorUx9State {
    int gap_action_ = -1;
    int skate_action_ = -1;
    int ground_action_ = -1;
};

LevelEditorUx9State& level_editor_ux9_state()
{
    static core::NoDestroy<LevelEditorUx9State> state;
    return state.get();
}

// --- the Ground tool ---

enum class Stage : uint8_t { kDisengaged, kFirst, kSecond, kForm };
// NOT ORIGINAL grouping - the goal form: its stage, draft, grid, hover, floor and queue (were globals). Made on first
// use, never destroyed (goal_form()).
struct GoalForm {
    Stage stage_ = Stage::kDisengaged;
    bool editing_{}; // (the form edits a piece that is there - goal_form().draft_.name)
    ground::Spec draft_{};
    float grid_ = 50.0F;
    bool have_hover_{};
    V3 hover_{};
    V3 first_{};
    uint32_t floor_{}; // (the piece the first corner was clicked on: the material by default)
    std::string form_message_;
    std::vector<std::function<void()>> queue_;
};

GoalForm& goal_form()
{
    static core::NoDestroy<GoalForm> state;
    return state.get();
}

V3 get_snapped(V3 point)
{
    return {.x = ground::snap_to(point.x, goal_form().grid_),
            .y = point.y,
            .z = ground::snap_to(point.z, goal_form().grid_)};
}

// The collision ray: the point and the piece hit (0: none / a movable).
bool get_cast(V3 from, V3 to, V3* hit, uint32_t* sector)
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

// The ground's height under a point (cast down from 300 above; `fallback` when nothing).
float get_ground_at(V3 point, float fallback)
{
    V3 hit{};
    uint32_t sector = 0;
    if (get_cast(point + V3{.x = 0.0F, .y = kProbeHeightAbove, .z = 0.0F},
                 point - V3{.x = 0.0F, .y = kProbeDepthBelow, .z = 0.0F}, &hit, &sector) &&
        !ground::is_piece(sector)) {
        return hit.y;
    }
    return fallback;
}

void change_stage(Stage stage)
{
    goal_form().stage_ = stage;
    goal_form().have_hover_ = false;
    if (stage == Stage::kFirst) {
        show_message("Ground Tool: click the first corner", kFirstCornerHintSeconds);
    }
}

void open_form(const ground::Spec& spec, bool editing)
{
    goal_form().draft_ = spec;
    goal_form().editing_ = editing;
    goal_form().form_message_.clear();
    goal_form().stage_ = Stage::kForm;
}

void get_apply()
{
    std::string why;
    ground::Spec spec = goal_form().draft_;
    if (!goal_form().editing_ && spec.name.empty()) {
        spec.name = ground::fresh_name(ground::get_all());
    }
    if (!ground::place_in(spec, &why)) {
        goal_form().form_message_ = "Not made: " + why;
        show_message(goal_form().form_message_, kFormMessageSeconds);
        return;
    }
    if (const ground::Spec* made = ground::get_find(spec.name)) {
        goal_form().draft_ = *made;
    }
    goal_form().editing_ = true;
    goal_form().form_message_ = "Applied";
    show_message(std::string(spec.lower ? "Lowered" : "Raised") + " ground: " + spec.name, kGroundResultSeconds);
}

const char* ramp_name(ground::Ramp ramp)
{
    switch (ramp) {
    case ground::Ramp::kKicker:
        return "Kicker";
    case ground::Ramp::kQuarter:
        return "Quarter pipe";
    default:
        return "None (wall)";
    }
}

// Each side's ramp: none (a wall), a kicker or a quarter pipe.
void ramp_combos(ground::Spec& draft)
{
    static const std::array<const char*, 4> kSideNames = {{"-X side", "+X side", "-Z side", "+Z side"}};
    for (int side = 0; side < 4; ++side) {
        ImGui::PushID(side);
        if (ImGui::BeginCombo(core::at(kSideNames, side), ramp_name(core::at(draft.ramps, side)))) {
            for (int row = 0; row < 3; ++row) {
                auto ramp = static_cast<ground::Ramp>(row);
                if (ImGui::Selectable(ramp_name(ramp), core::at(draft.ramps, side) == ramp)) {
                    core::at(draft.ramps, side) = ramp;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}

// Use Selected Material: the selected scenery piece's material, if it can give it.
void material_button()
{
    if (ImGui::Button("Use Selected Material")) {
        goal_form().queue_.emplace_back([] {
            const std::vector<uint32_t>& selection = scenery::get_selection();
            std::string why;
            ground::Spec spec = goal_form().draft_;
            if (selection.empty()) {
                goal_form().form_message_ = "Select a scenery piece to copy its material";
            } else if (const char* no = ground::why_not_donor(core::at(selection, 0))) {
                goal_form().form_message_ = std::string("Can't use this material: ") + no;
            } else if (ground::choose_material(core::at(selection, 0), &spec, &why)) {
                goal_form().draft_.material = spec.material, goal_form().draft_.uv = spec.uv,
                goal_form().draft_.colour = spec.colour, goal_form().draft_.terrain = spec.terrain,
                goal_form().form_message_.clear();
            }
        });
    }
}

void ground_form(float view_x, float view_y, float view_width, float scale)
{
    if (goal_form().stage_ != Stage::kForm) {
        return;
    }
    ImGui::SetNextWindowPos(
        ImVec2(view_x + view_width - (kPanelRightInset * scale), view_y + (kPanelTopOffset * scale)),
        ImGuiCond_Appearing, ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth * scale, 0.0F), ImGuiCond_Appearing);
    bool open = true;
    const std::string title =
        (goal_form().editing_ ? "Ground piece " + goal_form().draft_.name : std::string("Raise / lower ground")) + "###groundForm";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) {
            goal_form().queue_.emplace_back([] { change_stage(Stage::kDisengaged); });
        }
        return;
    }
    ground::Spec& draft = goal_form().draft_;
    int mode = draft.lower ? 1 : 0;
    ImGui::RadioButton("Raise", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Lower", &mode, 1);
    draft.lower = mode == 1;
    ImGui::DragFloat(draft.lower ? "Depth" : "Height", &draft.height, 1.0F, ground::kMinHeight, ground::kCeilingY,
                     "%.0f");
    std::array<float, 4> rect = {{draft.x0, draft.z0, draft.x1, draft.z1}};
    if (ImGui::DragFloat4("x0 z0 x1 z1", rect.data(), 1.0F, 0.0F, 0.0F, "%.0f")) {
        draft.x0 = core::at(rect, 0), draft.z0 = core::at(rect, 1), draft.x1 = core::at(rect, 2),
        draft.z1 = core::at(rect, 3);
    }
    ImGui::DragFloat("Ground at", &draft.base, 1.0F, 0.0F, 0.0F, "%.1f");
    ramp_combos(draft);
    ImGui::DragFloat("Ramp length", &draft.length, 1.0F, 0.0F, ground::kMaxLength,
                     draft.length > 0.0F ? "%.0f" : "auto");
    if (ImGui::IsItemHovered()) {
        gui::tooltip("0: automatic");
    }
    ImGui::DragFloat("Grid", &goal_form().grid_, 1.0F, 0.0F, kGridSpacingMax,
                     goal_form().grid_ > 0.0F ? "%.0f" : "off");
    gui::text_disabled("Material: %s",
                       (draft.material != 0U) ? object_edit::caption(draft.material).c_str() : "(automatic)");
    material_button();
    if (draft.lower) {
        gui::text_disabled("Pits need floor pieces inside the rectangle");
    }
    if (!goal_form().form_message_.empty()) {
        gui::text_wrapped("%s", goal_form().form_message_.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button(goal_form().editing_ ? "Apply" : "Make")) {
        goal_form().queue_.emplace_back([] { get_apply(); });
    }
    if (goal_form().editing_) {
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            goal_form().queue_.emplace_back([] {
                ground::drop_out(goal_form().draft_.name);
                show_message("Removed ground piece " + goal_form().draft_.name);
                change_stage(Stage::kDisengaged);
            });
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("New Rectangle")) {
        goal_form().queue_.emplace_back([] { change_stage(Stage::kFirst); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        goal_form().queue_.emplace_back([] { change_stage(Stage::kDisengaged); });
    }
    // (the pieces of this level)
    if (!ground::get_all().empty() && ImGui::CollapsingHeader("Pieces in this level")) {
        for (const ground::Spec& spec : ground::get_all()) {
            std::array<char, kPieceRowSize> row{};
            text::format_to(row, "%s  %s %.0f x %.0f, %.0f", spec.name.c_str(), spec.lower ? "pit" : "platform",
                            spec.x1 - spec.x0, spec.z1 - spec.z0, spec.height);
            if (ImGui::Selectable(row.data(), goal_form().editing_ && spec.name == draft.name)) {
                const std::string name = spec.name;
                goal_form().queue_.emplace_back([name] {
                    if (const ground::Spec* found = ground::get_find(name)) {
                        open_form(*found, true);
                        go_to_point({.x = (found->x0 + found->x1) * kHalf,
                                     .y = found->base,
                                     .z = (found->z0 + found->z1) * kHalf},
                                    (std::max(found->x1 - found->x0, found->z1 - found->z0) * kFrameDistanceFactor) +
                                        kFrameDistancePadding);
                    }
                });
            }
        }
    }
    ImGui::End();
    if (!open) {
        goal_form().queue_.emplace_back([] { change_stage(Stage::kDisengaged); });
    }
}

void ground_hover(V3 origin, V3 unit_dir)
{
    goal_form().have_hover_ = false;
    if (goal_form().stage_ != Stage::kFirst && goal_form().stage_ != Stage::kSecond) {
        return;
    }
    V3 hit{};
    uint32_t sector = 0;
    if (get_cast(origin, origin + unit_dir * kGroundRayLength, &hit, &sector)) {
        goal_form().hover_ = get_snapped(hit);
        goal_form().have_hover_ = true;
    }
}

bool ground_click(V3 origin, V3 unit_dir)
{
    if (goal_form().stage_ != Stage::kFirst && goal_form().stage_ != Stage::kSecond) {
        return false;
    }
    V3 hit{};
    uint32_t sector = 0;
    if (!get_cast(origin, origin + unit_dir * kGroundRayLength, &hit, &sector)) {
        show_message("Ground Tool: click on the ground");
        return true;
    }
    if (goal_form().stage_ == Stage::kFirst && ground::is_piece(sector)) {
        for (const ground::Spec& spec : ground::get_all()) {
            if (hit.x >= spec.x0 - kSectorHitMargin && hit.x <= spec.x1 + kSectorHitMargin &&
                hit.z >= spec.z0 - kSectorHitMargin && hit.z <= spec.z1 + kSectorHitMargin) {
                open_form(spec, true);
                return true;
            }
        }
    }
    const V3 corner = get_snapped(hit);
    if (goal_form().stage_ == Stage::kFirst) {
        goal_form().first_ = corner;
        goal_form().floor_ = ground::is_piece(sector) ? 0 : sector;
        goal_form().stage_ = Stage::kSecond;
        show_message("Ground Tool: click the opposite corner", kOppositeCornerHintSeconds);
        return true;
    }
    ground::Spec spec;
    spec.x0 = goal_form().first_.x, spec.z0 = goal_form().first_.z, spec.x1 = corner.x, spec.z1 = corner.z;
    ground::normalise(&spec);
    const V3 centre = {.x = (spec.x0 + spec.x1) * 0.5F,
                       .y = std::max(goal_form().first_.y, corner.y),
                       .z = (spec.z0 + spec.z1) * 0.5F};
    spec.base = get_ground_at(centre, goal_form().first_.y);
    std::string why;
    if (!ground::choose_material(goal_form().floor_, &spec, &why)) {
        show_message("Ground Tool: " + why, kGroundErrorSeconds);
        change_stage(Stage::kDisengaged);
        return true;
    }
    open_form(spec, false);
    return true;
}

} // namespace

void prepare_all()
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    auto make = [](const char* id, const char* name, const char* category, int scancode, const char* tip) {
        actions::ActStep act_step;
        act_step.id = id;
        act_step.name = name;
        act_step.category = category;
        act_step.tip = tip;
        act_step.kind = actions::Kind::kPress;
        act_step.defaults = act_step.keys = {actions::Chord{.scancode = scancode, .mods = actions::kAlt}};
        return actions::enrol(act_step);
    };
    level_editor_ux9_state().gap_action_ =
        make("gap_wizard", "Gap Wizard", "Triggers", SDL_SCANCODE_J, "Create a gap from a start and end point");
    level_editor_ux9_state().skate_action_ = make("skateability", "Skateability", "View", SDL_SCANCODE_K, "Colour surfaces by skateability");
    actions::set_checked(level_editor_ux9_state().skate_action_, [] { return skate_view::get_shown() ? 1 : 0; });
    level_editor_ux9_state().ground_action_ =
        make("ground_tool", "Ground Tool", "Edit", SDL_SCANCODE_U, "Raise or lower a rectangle of ground");
    layout::Extension extension;
    extension.keywords = {"ground"};
    extension.capture = [] { return ground::capture_records(); };
    extension.apply = [](const std::vector<std::string>& lines) { ground::apply_records(lines); };
    extension.reset = [] { ground::reset_all(); };
    layout::add_extension(extension);
}

void kickoff()
{
    ground::kickoff();
    gap_wizard::set_armed(false);
    goal_form().stage_ = Stage::kDisengaged;
}

void halt()
{
    gap_wizard::set_armed(false);
    goal_form().stage_ = Stage::kDisengaged;
    skate_view::halt();
    ground::halt();
    goal_form().queue_.clear();
}

void tick_frame()
{
    if (level_editor_ux9_state().gap_action_ >= 0 && actions::squeezed(level_editor_ux9_state().gap_action_)) {
        const bool on = !gap_wizard::get_armed();
        goal_form().stage_ = Stage::kDisengaged;
        gap_wizard::set_armed(on);
        if (!on) {
            show_message("Gap Wizard off");
        }
    }
    if (level_editor_ux9_state().skate_action_ >= 0 && actions::squeezed(level_editor_ux9_state().skate_action_)) {
        skate_view::set_shown(!skate_view::get_shown());
        show_message(skate_view::get_shown() ? "Skateability on" : "Skateability off");
    }
    if (level_editor_ux9_state().ground_action_ >= 0 && actions::squeezed(level_editor_ux9_state().ground_action_)) {
        gap_wizard::set_armed(false);
        if (goal_form().stage_ == Stage::kDisengaged) {
            change_stage(Stage::kFirst);
        } else {
            change_stage(Stage::kDisengaged);
            show_message("Ground Tool off");
        }
    }
    gap_wizard::tick_frame();
    std::vector<std::function<void()>> queue;
    queue.swap(goal_form().queue_);
    for (auto& action : queue) {
        action();
    }
}

bool occupied() { return gap_wizard::get_armed() || goal_form().stage_ != Stage::kDisengaged; }

int picking_tool()
{
    if (gap_wizard::get_picking()) {
        return 1;
    }
    return goal_form().stage_ == Stage::kFirst || goal_form().stage_ == Stage::kSecond ? 2 : 0;
}

bool get_cancel()
{
    if (gap_wizard::get_cancel()) {
        return true;
    }
    if (goal_form().stage_ == Stage::kDisengaged) {
        return false;
    }
    change_stage(Stage::kDisengaged);
    show_message("Ground Tool off");
    return true;
}

void get_hover(V3 origin, V3 unit_dir, float x, float y, bool have_ray)
{
    if (!have_ray) {
        goal_form().have_hover_ = false;
        return;
    }
    gap_wizard::get_hover(origin, unit_dir, x, y);
    ground_hover(origin, unit_dir);
}

bool get_click(V3 origin, V3 unit_dir, float x, float y)
{
    return gap_wizard::get_click(origin, unit_dir, x, y) || ground_click(origin, unit_dir);
}

void add_overlay(const overlays::Painter& painter)
{
    skate_view::add_overlay(painter);
    gap_wizard::add_overlay(painter);
    constexpr uint32_t kRect = 0xFFFFE070;
    constexpr uint32_t kPreview = 0xE0FFE070;
    if (goal_form().stage_ == Stage::kSecond) {
        const V3 point_b = goal_form().have_hover_ ? goal_form().hover_ : goal_form().first_;
        const float y = goal_form().first_.y + 2.0F;
        const std::array<V3, 4> corners = {{{.x = goal_form().first_.x, .y = y, .z = goal_form().first_.z},
                                            {.x = point_b.x, .y = y, .z = goal_form().first_.z},
                                            {.x = point_b.x, .y = y, .z = point_b.z},
                                            {.x = goal_form().first_.x, .y = y, .z = point_b.z}}};
        for (int i = 0; i < 4; ++i) {
            (void)painter.get_seg(core::at(corners, i), core::at(corners, (i + 1) % 4), kRect, kOutlineThickness);
        }
        std::array<char, kSizeLabelSize> text{};
        text::format_to(text, "%.0f x %.0f", std::fabs(point_b.x - goal_form().first_.x),
                        std::fabs(point_b.z - goal_form().first_.z));
        painter.draw_label(point_b, kRect, text.data());
    } else if (goal_form().stage_ == Stage::kFirst && goal_form().have_hover_) {
        painter.perpendicular(goal_form().hover_, kHoverGuideLength, kRect, kOutlineThickness);
    }
    if (goal_form().stage_ == Stage::kForm) {
        ground::add_preview(painter, goal_form().draft_, kPreview);
    }
}

void get_windows(float view_x, float view_y, float view_width, float view_height, float scale)
{
    gap_wizard::pane({.x = view_x, .y = view_y, .width = view_width, .height = view_height}, scale);
    ground_form(view_x, view_y, view_width, scale);
    skate_view::get_legend(view_x, view_y, view_width, view_height, scale);
}

std::string describe()
{
    std::string text;
    if (!ground::get_all().empty()) {
        text += std::to_string(ground::get_all().size()) + " ground pieces";
    }
    if (skate_view::get_shown()) {
        text += std::string(text.empty() ? "" : ", ") + "skateability overlay on";
    }
    if (gap_wizard::get_armed()) {
        text += std::string(text.empty() ? "" : ", ") + "gap wizard armed";
    }
    return text;
}

} // namespace frontend::level_editor::ux9
