// NOT ORIGINAL - the level editor's tool strip and status bar (ours). See level_editor_ui_strip.h. Only ImGui's own API.

#include "editor/level_editor_ui_strip.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace frontend::level_editor::ui_strip {

// --- the pure placement ----------------------------------------------------------------------------------------------

Plan get_fit(const std::vector<Bay>& slots, float left, float right, float overflow_width, float overflow_gap)
{
    const size_t n = slots.size();
    Plan plan;
    plan.shown.assign(n, true);
    plan.x.assign(n, 0.0f);
    auto needed = [&](bool overflow) {
        float total = 0.0f;
        bool first_left = true, first_right = true;
        for (size_t i = 0; i < n; ++i) {
            if (!plan.shown[i])
                continue;
            bool& first = slots[i].right ? first_right : first_left;
            total += slots[i].width + (first ? 0.0f : slots[i].gap);
            first = false;
        }
        // (the two groups apart by the right-hand group's first gap)
        if (!first_left && !first_right) {
            for (size_t i = 0; i < n; ++i) {
                if (plan.shown[i] && slots[i].right) {
                    total += slots[i].gap;
                    break;
                }
            }
        }
        if (overflow)
            total += overflow_width + ((first_left && first_right) ? 0.0f : overflow_gap);
        return total;
    };
    const float room = std::max(0.0f, right - left);
    bool hidden = false;
    while (needed(hidden) > room) {
        int drop = -1;
        for (size_t i = 0; i < n; ++i) {
            if (plan.shown[i] && (drop < 0 || slots[i].priority <= slots[drop].priority))
                drop = static_cast<int>(i);
        }
        if (drop < 0)
            break;
        plan.shown[drop] = false;
        hidden = true;
    }
    plan.overflow = hidden;
    // Left-hand slots from the left.
    float x = left;
    bool first = true;
    for (size_t i = 0; i < n; ++i) {
        if (!plan.shown[i] || slots[i].right)
            continue;
        if (!first)
            x += slots[i].gap;
        plan.x[i] = x;
        x += slots[i].width;
        first = false;
    }
    // The overflow button at the end, the right-hand slots before it (placed from the last; a slot's gap is the space
    // before it).
    if (hidden)
        plan.overflow_x_ = right - overflow_width;
    float end = hidden ? plan.overflow_x_ - overflow_gap : right;
    float next_gap = 0.0f;
    bool any = false;
    for (size_t k = n; k-- > 0;) {
        if (!plan.shown[k] || !slots[k].right)
            continue;
        if (any)
            end -= next_gap;
        plan.x[k] = end - slots[k].width;
        end = plan.x[k];
        next_gap = slots[k].gap;
        any = true;
    }
    return plan;
}

std::string elide(const std::string& text, float max_width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= max_width)
        return text;
    static const char kEllipsis[] = "\xE2\x80\xA6";
    if (ImGui::CalcTextSize(kEllipsis).x > max_width)
        return "";
    // The longest prefix that fits with the ellipsis, cut at a character's start (UTF-8 continuation bytes are
    // 10xxxxxx) and trimmed of trailing spaces. (Status and strip texts are short: a plain scan.)
    for (size_t length = text.size(); length > 0; --length) {
        if ((static_cast<unsigned char>(text[length]) & 0xC0) == 0x80 && length < text.size())
            continue;
        std::string cut = text.substr(0, length);
        while (!cut.empty() && cut.back() == ' ')
            cut.pop_back();
        std::string candidate = cut + kEllipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= max_width)
            return candidate;
    }
    return kEllipsis;
}

// --- the recorder ----------------------------------------------------------------------------------------------------

namespace {
std::vector<Cuboid>* g_boxes;
} // namespace

void set_recorder(std::vector<Cuboid>* boxes) { g_boxes = boxes; }

void note_rect(float x0, float y0, float x1, float y1, const char* what)
{
    if (g_boxes)
        g_boxes->push_back({x0, y0, x1, y1, what});
}

void get_note(const char* what)
{
    if (!g_boxes)
        return;
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    note_rect(a.x, a.y, b.x, b.y, what);
}

// --- the tool strip --------------------------------------------------------------------------------------------------

namespace {

const char kDown[] = " \xE2\x96\xBC"; // (" ▼": a button that opens a list - Inter has no small ▾)

float button_width(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

ImVec4 accent_button()
{
    ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    accent.w = 0.85f;
    return accent;
}

// A button lit while `on`.
bool flip_over(const char* label, bool on)
{
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, accent_button());
    bool pressed = ImGui::Button(label);
    if (on)
        ImGui::PopStyleColor();
    return pressed;
}

// A one-line tooltip: what it does, and its key when it has one.
void get_tip(const char* what, const std::string& keys = std::string())
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;
    if (keys.empty())
        ImGui::SetTooltip("%s", what);
    else
        ImGui::SetTooltip("%s  (%s)", what, keys.c_str());
}

std::string get_key_of(const State& s, const char* id) { return s.key_of_ ? s.key_of_(id) : std::string(); }

std::string grid_text(float grid)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%g%s###stripgrid", grid, kDown);
    return text;
}

const char* pick_text(uint32_t bits)
{
    switch (bits & 7) {
    case 7:
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

std::string problems_text(const State& s)
{
    int count = s.errors + s.warnings;
    char text[48];
    if (count > 0)
        std::snprintf(text, sizeof(text), "Problems %d###stripproblems", count);
    else
        std::snprintf(text, sizeof(text), "Problems###stripproblems");
    return text;
}

std::string get_level_text(const State& s, float scale)
{
    std::string text = s.level.empty() ? std::string("No level") : s.level + " (Edited)";
    if (s.unsaved)
        text += " *";
    return elide(text, 240.0f * scale);
}

std::string show_all_text(const State& s)
{
    char text[48];
    std::snprintf(text, sizeof(text), "Show All (%d)###stripshowall", s.hidden);
    return text;
}

// The controls, in the order they sit; their slots, and how each is drawn.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): indexes into tables and arrays
enum ItemId {
    kMode,
    kTools,
    kSpace,
    kSnapGrid,
    kPickItem,
    kShowAllItem,
    kStopItem,
    kLevel,
    kBudgetItem,
    kProblemsItem
};

struct Item {
    ItemId id;
    Bay slot;
};

std::vector<Item> get_items(const State& s, float scale)
{
    const float inner = 2.0f * scale, group = 10.0f * scale;
    std::vector<Item> items;
    auto add = [&](ItemId id, float width, bool right, int priority) {
        items.push_back({id, {width, group, right, priority}});
    };
    add(kMode, button_width("Fly") + inner + button_width("Select"), false, 3);
    add(kTools, button_width("Move") + inner + button_width("Rotate") + inner + button_width("Scale"), false, 9);
    add(kSpace, std::max(button_width("World"), button_width("Local")), false, 6);
    add(kSnapGrid, button_width("Snap") + inner + button_width(grid_text(s.grid).c_str()), false, 8);
    add(kPickItem, button_width((std::string(pick_text(s.pick_filter_)) + kDown).c_str()), false, 4);
    if (s.hidden > 0)
        add(kShowAllItem, button_width(show_all_text(s).c_str()), false, 2);
    if (s.simulating)
        add(kStopItem, button_width("Stop Simulation"), false, 10);
    add(kLevel, ImGui::CalcTextSize(get_level_text(s, scale).c_str()).x, true, 1);
    if (s.budget)
        add(kBudgetItem, 44.0f * scale, true, 5);
    add(kProblemsItem, button_width(problems_text(s).c_str()), true, 7);
    return items;
}

void draw_item(ItemId id, const State& s, float scale, Output* out)
{
    const float inner = 2.0f * scale;
    switch (id) {
    case kMode:
        if (flip_over("Fly", !s.selection))
            out->events.push_back({Occurrence::Kind::kDeselect});
        get_tip("Fly the camera; nothing selected", get_key_of(s, "deselect"));
        get_note("Fly");
        ImGui::SameLine(0.0f, inner);
        flip_over("Select", s.selection);
        get_tip("Click an object, piece or gizmo to select it");
        get_note("Select");
        break;
    case kTools: {
        static const char* const kLabels[3] = {"Move", "Rotate", "Scale"};
        static const char* const kIds[3] = {"tool_move", "tool_rotate", "tool_scale"};
        static const char* const kTips[3] = {"Move tool", "Rotate tool", "Scale tool"};
        for (int t = 0; t < 3; ++t) {
            if (t)
                ImGui::SameLine(0.0f, inner);
            if (flip_over(kLabels[t], s.tool == t))
                out->events.push_back({Occurrence::Kind::kTool, static_cast<float>(t)});
            get_tip(kTips[t], get_key_of(s, kIds[t]));
            get_note(kLabels[t]);
        }
        break;
    }
    case kSpace: {
        // (as wide as the wider label, so it doesn't move when switched)
        float width = std::max(button_width("World"), button_width("Local"));
        if (ImGui::Button(s.local ? "Local###stripspace" : "World###stripspace", ImVec2(width, 0.0f)))
            out->events.push_back({Occurrence::Kind::kSpace});
        get_tip(s.local ? "Gizmo axes: local" : "Gizmo axes: world", get_key_of(s, "tool_space"));
        get_note("Space");
        break;
    }
    case kSnapGrid: {
        if (flip_over("Snap", s.snap))
            out->events.push_back({Occurrence::Kind::kSnap, s.snap ? 0.0f : 1.0f});
        get_tip("Snap while moving");
        get_note("Snap");
        ImGui::SameLine(0.0f, inner);
        std::string grid = grid_text(s.grid);
        if (ImGui::Button(grid.c_str()))
            ImGui::OpenPopup("##stripgridlist");
        get_tip("Grid size");
        get_note("Grid");
        if (ImGui::BeginPopup("##stripgridlist")) {
            for (float size : s.grids) {
                char label[16];
                std::snprintf(label, sizeof(label), "%g", size);
                if (ImGui::Selectable(label, size == s.grid))
                    out->events.push_back({Occurrence::Kind::kGrid, size});
            }
            ImGui::EndPopup();
        }
        break;
    }
    case kPickItem: {
        std::string label = std::string(pick_text(s.pick_filter_)) + kDown + "###strippick";
        if (ImGui::Button(label.c_str()))
            ImGui::OpenPopup("##strippicklist");
        get_tip("What a click in the view selects");
        get_note("Pick");
        if (ImGui::BeginPopup("##strippicklist")) {
            static const struct {
                const char* label;
                uint32_t bit;
            } kPick[] = {{"Nodes", 1}, {"Objects", 2}, {"Scenery", 4}};
            for (const auto& p : kPick) {
                if (ImGui::MenuItem(p.label, nullptr, (s.pick_filter_ & p.bit) != 0))
                    out->events.push_back({Occurrence::Kind::kPick, static_cast<float>(s.pick_filter_ ^ p.bit)});
            }
            ImGui::EndPopup();
        }
        break;
    }
    case kShowAllItem:
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.28f, 0.52f, 0.9f));
        if (ImGui::Button(show_all_text(s).c_str()))
            out->events.push_back({Occurrence::Kind::kShowAll});
        ImGui::PopStyleColor();
        get_tip("Show items hidden in the editor", get_key_of(s, "show_all"));
        get_note("ShowAll");
        break;
    case kStopItem:
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.15f, 1.0f));
        if (ImGui::Button("Stop Simulation"))
            out->events.push_back({Occurrence::Kind::kSimulate});
        ImGui::PopStyleColor();
        get_tip("Stop the simulation", get_key_of(s, "simulate"));
        get_note("Stop");
        break;
    case kLevel: {
        // (text, set on the buttons' baseline)
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ImGui::GetStyle().FramePadding.y));
        ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "%s", get_level_text(s, scale).c_str());
        out->level_hovered_ = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        get_note("Level");
        break;
    }
    case kBudgetItem: {
        float width = 44.0f * scale, frame = ImGui::GetFrameHeight();
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##stripbudget", ImVec2(width, frame));
        if (ImGui::IsItemClicked())
            out->events.push_back({Occurrence::Kind::kBudget});
        out->budget_hovered_ = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        get_note("Budget");
        float bar = ImGui::GetTextLineHeight() * 0.6f;
        float top = at.y + (frame - bar) * 0.5f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        uint32_t c = s.budget_argb_;
        ImU32 colour = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
        draw->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + bar), IM_COL32(60, 62, 66, 255), 2.0f);
        float fill = std::clamp(s.budget_fill_, 0.0f, 1.0f);
        draw->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width * fill, top + bar), colour, 2.0f);
        break;
    }
    case kProblemsItem: {
        int colours = 0;
        if (s.errors) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.18f, 0.20f, 0.85f));
            ++colours;
        } else if (s.warnings) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.48f, 0.08f, 0.85f));
            ++colours;
        }
        if (ImGui::Button(problems_text(s).c_str()))
            out->events.push_back({Occurrence::Kind::kProblems});
        ImGui::PopStyleColor(colours);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            std::string keys = get_key_of(s, "problems");
            ImGui::SetTooltip("%d errors, %d warnings%s%s%s", s.errors, s.warnings, keys.empty() ? "" : "  (",
                              keys.c_str(), keys.empty() ? "" : ")");
        }
        get_note("Problems");
        break;
    }
    }
}

} // namespace

void blit(const State& state, float x, float y, float width, float scale, Output* out)
{
    std::vector<Item> items = get_items(state, scale);
    std::vector<Bay> slots;
    slots.reserve(items.size());
    for (const Item& item : items)
        slots.push_back(item.slot);
    const char* more = "\xC2\xBB###stripmore"; // ("»")
    float more_width = button_width(more);
    Plan plan = get_fit(slots, x, x + width, more_width, 10.0f * scale);
    for (size_t i = 0; i < items.size(); ++i) {
        if (!plan.shown[i])
            continue;
        ImGui::SetCursorScreenPos(ImVec2(plan.x[i], y));
        ImGui::PushID(static_cast<int>(items[i].id));
        draw_item(items[i].id, state, scale, out);
        ImGui::PopID();
    }
    if (!plan.overflow)
        return;
    ImGui::SetCursorScreenPos(ImVec2(plan.overflow_x_, y));
    if (ImGui::Button(more))
        ImGui::OpenPopup("##stripoverflow");
    get_tip("More controls");
    get_note("More");
    if (ImGui::BeginPopup("##stripoverflow")) {
        // (the hidden controls, one a line; not recorded - they're in a popup of their own)
        std::vector<Cuboid>* boxes = g_boxes;
        g_boxes = nullptr;
        for (size_t i = 0; i < items.size(); ++i) {
            if (plan.shown[i])
                continue;
            ImGui::PushID(static_cast<int>(items[i].id));
            draw_item(items[i].id, state, scale, out);
            ImGui::PopID();
        }
        g_boxes = boxes;
        ImGui::EndPopup();
    }
}

// --- the status bar ----------------------------------------------------------------------------------------------------

void status_row(const std::string& message, const std::vector<std::pair<std::string, std::string>>& hints,
                const std::string& right, float x, float y, float width, const ux8::Fonts& fonts)
{
    float s = fonts.scale;
    const float gap = 16.0f * s;
    // The right-hand text first: at most 45% of the row, elided.
    if (fonts.mono)
        ImGui::PushFont(fonts.mono);
    std::string right_text = elide(right, width * 0.45f);
    float right_width = right_text.empty() ? 0.0f : ImGui::CalcTextSize(right_text.c_str()).x;
    if (!right_text.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(x + width - right_width, y));
        ImGui::TextDisabled("%s", right_text.c_str());
        get_note("StatusRight");
    }
    if (fonts.mono)
        ImGui::PopFont();
    float left_width = std::max(0.0f, width - right_width - (right_text.empty() ? 0.0f : gap));
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (fonts.small)
        ImGui::PushFont(fonts.small);
    if (!message.empty()) {
        std::string text = elide(message, left_width);
        if (!text.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.5f, 1.0f), "%s", text.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && text != message)
                ImGui::SetTooltip("%s", message.c_str());
            get_note("StatusMessage");
        }
    } else {
        ux8::hint_row(hints, fonts, left_width);
    }
    if (fonts.small)
        ImGui::PopFont();
}

} // namespace frontend::level_editor::ui_strip
