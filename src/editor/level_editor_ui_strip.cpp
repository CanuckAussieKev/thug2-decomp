// NOT ORIGINAL - the level editor's tool strip and status bar (ours). See level_editor_ui_strip.h. Only ImGui's own API.

#include "editor/level_editor_ui_strip.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor_ux8.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "base/maths/vec2.h"
#include "base/core/str.h"
#include "editor/level_editor_ui_layout.h"

namespace frontend::level_editor::ui_strip {

namespace {
constexpr float kAccentButtonAlpha = 0.85F; // the accent-coloured button's alpha
constexpr std::size_t kGridTextSize = 24; // "<grid>v###stripgrid"
constexpr std::size_t kProblemsTextSize = 48; // "Problems <n>###stripproblems"
constexpr std::size_t kShowAllTextSize = 48; // "Show All (<n>)###stripshowall"
constexpr std::size_t kGridLabelSize = 16; // one entry of the grid list
constexpr uint32_t kPickAll = 7; // nodes, objects and scenery
constexpr float kLevelTextMaxWidth = 240.0F; // the level name's widest, at scale 1
constexpr float kBudgetBarWidth = 44.0F;     // the budget meter's width, at scale 1
constexpr float kBudgetBarRounding = 2.0F;   // its corners
constexpr float kItemGroupGap = 10.0F;       // the room kept between strip groups, at scale 1
// Which strip item is dropped first when the strip runs out of room.
constexpr int kLevelPriority = 1;
constexpr int kShowAllPriority = 2;
constexpr int kModePriority = 3;
constexpr int kPickPriority = 4;
constexpr int kBudgetPriority = 5;
constexpr int kSpacePriority = 6;
constexpr int kProblemsPriority = 7;
constexpr int kSnapGridPriority = 8;
constexpr int kToolsPriority = 9;
constexpr int kStopPriority = 10;
// The strip's button and text colours.
constexpr ImVec4 kShowAllButtonColour{0.45F, 0.28F, 0.52F, 0.9F};
constexpr ImVec4 kStopButtonColour{0.75F, 0.25F, 0.15F, 1.0F};
constexpr ImVec4 kErrorsButtonColour{0.70F, 0.18F, 0.20F, 0.85F};
constexpr ImVec4 kWarningsButtonColour{0.72F, 0.48F, 0.08F, 0.85F};
constexpr ImVec4 kLevelTextColour{0.55F, 0.84F, 1.0F, 1.0F};
constexpr ImVec4 kMessageTextColour{1.0F, 0.86F, 0.5F, 1.0F};

} // namespace

// --- the pure placement ----------------------------------------------------------------------------------------------

namespace {

// The width the shown slots take, the two groups apart by the right-hand group's first gap, and the overflow
// button's when asked.
float needed_width(const std::vector<Bay>& slots, const Plan& plan, float overflow_width, float overflow_gap,
                   bool overflow)
{
    const size_t count = slots.size();
    float total = 0.0F;
    bool first_left = true;
    bool first_right = true;
    for (size_t i = 0; i < count; ++i) {
        if (!core::at(plan.shown, i)) {
            continue;
        }
        bool& first = core::at(slots, i).right ? first_right : first_left;
        total += core::at(slots, i).width + (first ? 0.0F : core::at(slots, i).gap);
        first = false;
    }
    // (the two groups apart by the right-hand group's first gap)
    if (!first_left && !first_right) {
        for (size_t i = 0; i < count; ++i) {
            if (core::at(plan.shown, i) && core::at(slots, i).right) {
                total += core::at(slots, i).gap;
                break;
            }
        }
    }
    if (overflow) {
        total += overflow_width + ((first_left && first_right) ? 0.0F : overflow_gap);
    }
    return total;
}

} // namespace

Plan get_fit(const std::vector<Bay>& slots, float left, float right, float overflow_width, float overflow_gap)
{
    const size_t count = slots.size();
    Plan plan;
    plan.shown.assign(count, true);
    plan.x.assign(count, 0.0F);
    auto needed = [&](bool overflow) { return needed_width(slots, plan, overflow_width, overflow_gap, overflow); };
    const float room = std::max(0.0F, right - left);
    bool hidden = false;
    while (needed(hidden) > room) {
        int drop = -1;
        for (size_t i = 0; i < count; ++i) {
            if (core::at(plan.shown, i) &&
                (drop < 0 || core::at(slots, i).priority <= core::at(slots, drop).priority)) {
                drop = static_cast<int>(i);
            }
        }
        if (drop < 0) {
            break;
        }
        core::at(plan.shown, drop) = false;
        hidden = true;
    }
    plan.overflow = hidden;
    // Left-hand slots from the left.
    float x = left;
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
        if (!core::at(plan.shown, i) || core::at(slots, i).right) {
            continue;
        }
        if (!first) {
            x += core::at(slots, i).gap;
        }
        core::at(plan.x, i) = x;
        x += core::at(slots, i).width;
        first = false;
    }
    // The overflow button at the end, the right-hand slots before it (placed from the last; a slot's gap is the space
    // before it).
    if (hidden) {
        plan.overflow_x_ = right - overflow_width;
    }
    float end = hidden ? plan.overflow_x_ - overflow_gap : right;
    float next_gap = 0.0F;
    bool any = false;
    for (size_t k = count; k-- > 0;) {
        if (!core::at(plan.shown, k) || !core::at(slots, k).right) {
            continue;
        }
        if (any) {
            end -= next_gap;
        }
        core::at(plan.x, k) = end - core::at(slots, k).width;
        end = core::at(plan.x, k);
        next_gap = core::at(slots, k).gap;
        any = true;
    }
    return plan;
}

std::string elide(const std::string& text, float max_width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= max_width) {
        return text;
    }
    static const auto kEllipsis = std::to_array("\xE2\x80\xA6");
    if (ImGui::CalcTextSize(kEllipsis.data()).x > max_width) {
        return "";
    }
    // The longest prefix that fits with the ellipsis, cut at a character's start (UTF-8 continuation bytes are
    // 10xxxxxx) and trimmed of trailing spaces. (Status and strip texts are short: a plain scan.)
    for (size_t length = text.size(); length > 0; --length) {
        if ((static_cast<unsigned char>(core::at(text, length)) & text::kUtf8ContinuationMask) ==
                text::kUtf8Continuation &&
            length < text.size()) {
            continue;
        }
        std::string cut = text.substr(0, length);
        while (!cut.empty() && cut.back() == ' ') {
            cut.pop_back();
        }
        std::string candidate = cut + kEllipsis.data();
        if (ImGui::CalcTextSize(candidate.c_str()).x <= max_width) {
            return candidate;
        }
    }
    return kEllipsis.data();
}

// --- the recorder ----------------------------------------------------------------------------------------------------

namespace {
std::vector<Cuboid>*& stored_boxes()
{
    static std::vector<Cuboid>* boxes{};
    return boxes;
}
} // namespace

void set_recorder(std::vector<Cuboid>* boxes) { stored_boxes() = boxes; }

void note_rect(float x0, float y0, float x1, float y1, const char* what)
{
    if (stored_boxes()) {
        stored_boxes()->push_back({.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .what = what});
    }
}

void get_note(const char* what)
{
    if (!stored_boxes()) {
        return;
    }
    const ImVec2 point_a = ImGui::GetItemRectMin();
    const ImVec2 point_b = ImGui::GetItemRectMax();
    note_rect(point_a.x, point_a.y, point_b.x, point_b.y, what);
}

// --- the tool strip --------------------------------------------------------------------------------------------------

namespace {

constexpr auto kDown = std::to_array(" \xE2\x96\xBC"); // (" ▼": a button that opens a list - Inter has no small ▾)

float button_width(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + (ImGui::GetStyle().FramePadding.x * ui_layout::kBothSides);
}

ImVec4 accent_button()
{
    ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    accent.w = kAccentButtonAlpha;
    return accent;
}

// A button lit while `on`.
bool flip_over(const char* label, bool on)
{
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, accent_button());
    }
    const bool pressed = ImGui::Button(label);
    if (on) {
        ImGui::PopStyleColor();
    }
    return pressed;
}

// A one-line tooltip: what it does, and its key when it has one.
void get_tip(const char* what, const std::string& keys = std::string())
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        return;
    }
    if (keys.empty()) {
        gui::tooltip("%s", what);
    } else {
        gui::tooltip("%s  (%s)", what, keys.c_str());
    }
}

std::string get_key_of(const State& state, const char* id) { return state.key_of_ ? state.key_of_(id) : std::string(); }

std::string grid_text(float grid)
{
    std::array<char, kGridTextSize> text{};
    text::format_to(text, "%g%s###stripgrid", grid, kDown.data());
    return text.data();
}

const char* pick_text(uint32_t bits)
{
    switch (bits & kPickAll) {
    case kPickAll:
        return "Pick: All";
    case 0:
        return "Pick: None";
    case 1:
        return "Pick: Nodes";
    case 2:
        return "Pick: Objects";
    case 4:
        return "Pick: Scenery";
    default:
        return "Pick: Custom";
    }
}

std::string problems_text(const State& state)
{
    const int count = state.errors + state.warnings;
    std::array<char, kProblemsTextSize> text{};
    if (count > 0) {
        text::format_to(text, "Problems %d###stripproblems", count);
    } else {
        text::format_to(text, "Problems###stripproblems");
    }
    return text.data();
}

std::string get_level_text(const State& state, float scale)
{
    std::string text = state.level.empty() ? std::string("No level") : state.level + " (Edited)";
    if (state.unsaved) {
        text += " *";
    }
    return elide(text, kLevelTextMaxWidth * scale);
}

// The rotation snap's button ("15°", `button`: with the button's id) and a list entry.
std::string angle_text(float degrees, bool button = true)
{
    std::array<char, kGridTextSize> text{};
    text::format_to(text, "%g\xC2\xB0", degrees);
    return std::string(text.data()) + (button ? "###stripangle" : "");
}

std::string show_all_text(const State& state)
{
    std::array<char, kShowAllTextSize> text{};
    text::format_to(text, "Show All (%d)###stripshowall", state.hidden);
    return text.data();
}

// The controls, in the order they sit; their slots, and how each is drawn.
using ItemId = uint8_t;
constexpr int kMode = 0;
constexpr int kTools = 1;
constexpr int kSpace = 2;
constexpr int kSnapGrid = 3;
constexpr int kPickItem = 4;
constexpr int kShowAllItem = 5;
constexpr int kStopItem = 6;
constexpr int kLevel = 7;
constexpr int kBudgetItem = 8;
constexpr int kProblemsItem = 9;
constexpr int kPickUnitsItem = 10;

struct Item {
    ItemId id;
    Bay slot;
};

std::vector<Item> get_items(const State& state, float scale)
{
    const float inner = 2.0F * scale;
    const float group = 10.0F * scale;
    std::vector<Item> items;
    auto add = [&](ItemId id, float width, bool right, int priority) {
        items.push_back({.id = id, .slot = {.width = width, .gap = group, .right = right, .priority = priority}});
    };
    add(kMode, button_width("Fly") + inner + button_width("Select"), false, kModePriority);
    add(kTools, button_width("Move") + inner + button_width("Rotate") + inner + button_width("Scale"), false, kToolsPriority);
    add(kSpace, std::max(button_width("World"), button_width("Local")), false, kSpacePriority);
    add(kSnapGrid,
        button_width("Snap") + inner + button_width(grid_text(state.grid).c_str()) + inner +
            button_width(angle_text(state.angle).c_str()),
        false, kSnapGridPriority);
    add(kPickItem, button_width((std::string(pick_text(state.pick_filter_)) + kDown.data()).c_str()), false,
        kPickPriority);
    add(kPickUnitsItem, button_width("Groups") + inner + button_width("Pieces"), false, kPickPriority);
    if (state.hidden > 0) {
        add(kShowAllItem, button_width(show_all_text(state).c_str()), false, kShowAllPriority);
    }
    if (state.simulating) {
        add(kStopItem, button_width("Stop Simulation"), false, kStopPriority);
    }
    add(kLevel, ImGui::CalcTextSize(get_level_text(state, scale).c_str()).x, true, kLevelPriority);
    if (state.budget) {
        add(kBudgetItem, kBudgetBarWidth * scale, true, kBudgetPriority);
    }
    add(kProblemsItem, button_width(problems_text(state).c_str()), true, kProblemsPriority);
    return items;
}

namespace {

// The tool buttons: Move, Rotate, Scale.
void draw_tools(const State& state, float inner, Output* out)
{
    static const std::array<const char*, 3> kLabels = {{"Move", "Rotate", "Scale"}};
    static const std::array<const char*, 3> kIds = {{"tool_move", "tool_rotate", "tool_scale"}};
    static const std::array<const char*, 3> kTips = {{"Move tool", "Rotate tool", "Scale tool"}};
    for (int slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            ImGui::SameLine(0.0F, inner);
        }
        if (flip_over(core::at(kLabels, slot), state.tool == slot)) {
            out->events.push_back({.kind = Occurrence::Kind::kTool, .value = static_cast<float>(slot)});
        }
        get_tip(core::at(kTips, slot), get_key_of(state, core::at(kIds, slot)));
        get_note(core::at(kLabels, slot));
    }
}

// Snap, and the grid size's list.
void draw_snap_grid(const State& state, float inner, Output* out)
{
    if (flip_over("Snap", state.snap)) {
        out->events.push_back({.kind = Occurrence::Kind::kSnap, .value = state.snap ? 0.0F : 1.0F});
    }
    get_tip("Snap while moving");
    get_note("Snap");
    ImGui::SameLine(0.0F, inner);
    const std::string grid = grid_text(state.grid);
    if (ImGui::Button(grid.c_str())) {
        ImGui::OpenPopup("##stripgridlist");
    }
    get_tip("Grid size");
    get_note("Grid");
    if (ImGui::BeginPopup("##stripgridlist")) {
        for (const float size : state.grids) {
            std::array<char, kGridLabelSize> label{};
            text::format_to(label, "%g", size);
            if (ImGui::Selectable(label.data(), size == state.grid)) {
                out->events.push_back({.kind = Occurrence::Kind::kGrid, .value = size});
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0F, inner);
    if (ImGui::Button(angle_text(state.angle).c_str())) {
        ImGui::OpenPopup("##stripanglelist");
    }
    get_tip("Rotation snap");
    get_note("Angle");
    if (ImGui::BeginPopup("##stripanglelist")) {
        for (const float step : state.angles) {
            if (ImGui::Selectable(angle_text(step, false).c_str(), step == state.angle)) {
                out->events.push_back({.kind = Occurrence::Kind::kAngle, .value = step});
            }
        }
        ImGui::EndPopup();
    }
}

// What a click in the view selects: nodes, objects, scenery.
void draw_pick(const State& state, Output* out)
{
    const std::string label = std::string(pick_text(state.pick_filter_)) + kDown.data() + "###strippick";
    if (ImGui::Button(label.c_str())) {
        ImGui::OpenPopup("##strippicklist");
    }
    get_tip("What a click in the view selects");
    get_note("Pick");
    if (ImGui::BeginPopup("##strippicklist")) {
        struct PickBit {
            const char* label;
            uint32_t bit;
        };
        static constexpr auto kPick = std::to_array<PickBit>(
            {{.label = "Nodes", .bit = 1}, {.label = "Objects", .bit = 2}, {.label = "Scenery", .bit = 4}});
        for (const auto& pick : kPick) {
            if (ImGui::MenuItem(pick.label, nullptr, (state.pick_filter_ & pick.bit) != 0)) {
                out->events.push_back(
                    {.kind = Occurrence::Kind::kPick, .value = static_cast<float>(state.pick_filter_ ^ pick.bit)});
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace

void draw_item(ItemId id, const State& state, float scale, Output* out)
{
    const float inner = 2.0F * scale;
    switch (id) {
    case kMode:
        if (flip_over("Fly", !state.selection)) {
            out->events.push_back({.kind = Occurrence::Kind::kDeselect});
        }
        get_tip("Fly the camera; nothing selected", get_key_of(state, "deselect"));
        get_note("Fly");
        ImGui::SameLine(0.0F, inner);
        flip_over("Select", state.selection);
        get_tip("Click an object, piece or gizmo to select it");
        get_note("Select");
        break;
    case kTools: {
        draw_tools(state, inner, out);
        break;
    }
    case kSpace: {
        // (as wide as the wider label, so it doesn't move when switched)
        const float width = std::max(button_width("World"), button_width("Local"));
        if (ImGui::Button(state.local ? "Local###stripspace" : "World###stripspace", ImVec2(width, 0.0F))) {
            out->events.push_back({.kind = Occurrence::Kind::kSpace});
        }
        get_tip(state.local ? "Gizmo axes: local" : "Gizmo axes: world", get_key_of(state, "tool_space"));
        get_note("Space");
        break;
    }
    case kSnapGrid: {
        draw_snap_grid(state, inner, out);
        break;
    }
    case kPickItem: {
        draw_pick(state, out);
        break;
    }
    case kPickUnitsItem:
        // (Hammer's selection mode: a click takes the group, or always the piece - level_editor_scenery.h)
        if (flip_over("Groups", !state.pick_pieces_) && state.pick_pieces_) {
            out->events.push_back({.kind = Occurrence::Kind::kPickUnits, .value = 0.0F});
        }
        get_tip("A click selects the whole group; click again for one piece", get_key_of(state, "pick_units"));
        get_note("PickGroups");
        ImGui::SameLine(0.0F, inner);
        if (flip_over("Pieces", state.pick_pieces_) && !state.pick_pieces_) {
            out->events.push_back({.kind = Occurrence::Kind::kPickUnits, .value = 1.0F});
        }
        get_tip("A click selects one piece", get_key_of(state, "pick_units"));
        get_note("PickPieces");
        break;
    case kShowAllItem:
        ImGui::PushStyleColor(ImGuiCol_Button, kShowAllButtonColour);
        if (ImGui::Button(show_all_text(state).c_str())) {
            out->events.push_back({.kind = Occurrence::Kind::kShowAll});
        }
        ImGui::PopStyleColor();
        get_tip("Show items hidden in the editor", get_key_of(state, "show_all"));
        get_note("ShowAll");
        break;
    case kStopItem:
        ImGui::PushStyleColor(ImGuiCol_Button, kStopButtonColour);
        if (ImGui::Button("Stop Simulation")) {
            out->events.push_back({.kind = Occurrence::Kind::kSimulate});
        }
        ImGui::PopStyleColor();
        get_tip("Stop the simulation", get_key_of(state, "simulate"));
        get_note("Stop");
        break;
    case kLevel: {
        // (text, set on the buttons' baseline)
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ImGui::GetStyle().FramePadding.y));
        gui::text_colored(kLevelTextColour, "%s", get_level_text(state, scale).c_str());
        out->level_hovered_ = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        get_note("Level");
        break;
    }
    case kBudgetItem: {
        const float width = 44.0F * scale;
        const float frame = ImGui::GetFrameHeight();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##stripbudget", ImVec2(width, frame));
        if (ImGui::IsItemClicked()) {
            out->events.push_back({.kind = Occurrence::Kind::kBudget});
        }
        out->budget_hovered_ = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        get_note("Budget");
        const float bar = ImGui::GetTextLineHeight() * 0.6F;
        const float top = at.y + ((frame - bar) * 0.5F);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const uint32_t budget_argb = state.budget_argb_;
        const ImU32 colour = IM_COL32((budget_argb >> 16) & 0xFF, (budget_argb >> 8) & 0xFF, budget_argb & 0xFF, 255);
        draw->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + bar), IM_COL32(60, 62, 66, 255), kBudgetBarRounding);
        const float fill = std::clamp(state.budget_fill_, 0.0F, 1.0F);
        draw->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + (width * fill), top + bar), colour, kBudgetBarRounding);
        break;
    }
    case kProblemsItem: {
        int colours = 0;
        if (state.errors != 0) {
            ImGui::PushStyleColor(ImGuiCol_Button, kErrorsButtonColour);
            ++colours;
        } else if (state.warnings != 0) {
            ImGui::PushStyleColor(ImGuiCol_Button, kWarningsButtonColour);
            ++colours;
        }
        if (ImGui::Button(problems_text(state).c_str())) {
            out->events.push_back({.kind = Occurrence::Kind::kProblems});
        }
        ImGui::PopStyleColor(colours);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            const std::string keys = get_key_of(state, "problems");
            gui::tooltip("%d errors, %d warnings%s%s%s", state.errors, state.warnings, keys.empty() ? "" : "  (",
                         keys.c_str(), keys.empty() ? "" : ")");
        }
        get_note("Problems");
        break;
    }
    default:
        break;
    }
}

} // namespace

void blit(const State& state, maths::Vec2 at, float width, float scale, Output* out)
{
    const auto x = at.x;
    const auto y = at.y;
    std::vector<Item> items = get_items(state, scale);
    std::vector<Bay> slots;
    slots.reserve(items.size());
    for (const Item& item : items) {
        slots.push_back(item.slot);
    }
    const char* more = "\xC2\xBB###stripmore"; // ("»")
    const float more_width = button_width(more);
    Plan plan = get_fit(slots, x, x + width, more_width, kItemGroupGap * scale);
    for (size_t i = 0; i < items.size(); ++i) {
        if (!core::at(plan.shown, i)) {
            continue;
        }
        ImGui::SetCursorScreenPos(ImVec2(core::at(plan.x, i), y));
        ImGui::PushID(static_cast<int>(core::at(items, i).id));
        draw_item(core::at(items, i).id, state, scale, out);
        ImGui::PopID();
    }
    if (!plan.overflow) {
        return;
    }
    ImGui::SetCursorScreenPos(ImVec2(plan.overflow_x_, y));
    if (ImGui::Button(more)) {
        ImGui::OpenPopup("##stripoverflow");
    }
    get_tip("More controls");
    get_note("More");
    if (ImGui::BeginPopup("##stripoverflow")) {
        // (the hidden controls, one a line; not recorded - they're in a popup of their own)
        std::vector<Cuboid>* boxes = stored_boxes();
        stored_boxes() = nullptr;
        for (size_t i = 0; i < items.size(); ++i) {
            if (core::at(plan.shown, i)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(core::at(items, i).id));
            draw_item(core::at(items, i).id, state, scale, out);
            ImGui::PopID();
        }
        stored_boxes() = boxes;
        ImGui::EndPopup();
    }
}

// --- the status bar ----------------------------------------------------------------------------------------------------

void status_row(const std::string& message, const std::vector<std::pair<std::string, std::string>>& hints,
                const std::string& right, float x, float y, float width, const ux8::Fonts& fonts)
{
    const float scale = fonts.scale;
    const float gap = 16.0F * scale;
    // The right-hand text first: at most 45% of the row, elided.
    if (fonts.mono) {
        ImGui::PushFont(fonts.mono);
    }
    const std::string right_text = elide(right, width * 0.45F);
    const float right_width = right_text.empty() ? 0.0F : ImGui::CalcTextSize(right_text.c_str()).x;
    if (!right_text.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(x + width - right_width, y));
        gui::text_disabled("%s", right_text.c_str());
        get_note("StatusRight");
    }
    if (fonts.mono) {
        ImGui::PopFont();
    }
    const float left_width = std::max(0.0F, width - right_width - (right_text.empty() ? 0.0F : gap));
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (fonts.small) {
        ImGui::PushFont(fonts.small);
    }
    if (!message.empty()) {
        const std::string text = elide(message, left_width);
        if (!text.empty()) {
            gui::text_colored(kMessageTextColour, "%s", text.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && text != message) {
                gui::tooltip("%s", message.c_str());
            }
            get_note("StatusMessage");
        }
    } else {
        ux8::hint_row(hints, fonts, left_width);
    }
    if (fonts.small) {
        ImGui::PopFont();
    }
}

} // namespace frontend::level_editor::ui_strip
