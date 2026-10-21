// NOT ORIGINAL - the level editor's tool strip and status bar (ours). See level_editor_ui_strip.h. Only ImGui's own API.

#include "game/front/level_editor_ui_strip.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace Front::LevelEditor::UiStrip {

// --- the pure placement ----------------------------------------------------------------------------------------------

Plan Fit(const std::vector<Slot>& slots, float left, float right, float overflowWidth, float overflowGap)
{
    const size_t n = slots.size();
    Plan plan;
    plan.shown.assign(n, true);
    plan.x.assign(n, 0.0f);
    auto needed = [&](bool overflow) {
        float total = 0.0f;
        bool firstLeft = true, firstRight = true;
        for (size_t i = 0; i < n; ++i) {
            if (!plan.shown[i])
                continue;
            bool& first = slots[i].right ? firstRight : firstLeft;
            total += slots[i].width + (first ? 0.0f : slots[i].gap);
            first = false;
        }
        // (the two groups apart by the right-hand group's first gap)
        if (!firstLeft && !firstRight) {
            for (size_t i = 0; i < n; ++i) {
                if (plan.shown[i] && slots[i].right) {
                    total += slots[i].gap;
                    break;
                }
            }
        }
        if (overflow)
            total += overflowWidth + ((firstLeft && firstRight) ? 0.0f : overflowGap);
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
        plan.overflowX = right - overflowWidth;
    float end = hidden ? plan.overflowX - overflowGap : right;
    float nextGap = 0.0f;
    bool any = false;
    for (size_t k = n; k-- > 0;) {
        if (!plan.shown[k] || !slots[k].right)
            continue;
        if (any)
            end -= nextGap;
        plan.x[k] = end - slots[k].width;
        end = plan.x[k];
        nextGap = slots[k].gap;
        any = true;
    }
    return plan;
}

std::string Elide(const std::string& text, float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        return text;
    static const char kEllipsis[] = "\xE2\x80\xA6";
    if (ImGui::CalcTextSize(kEllipsis).x > maxWidth)
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
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
            return candidate;
    }
    return kEllipsis;
}

// --- the recorder ----------------------------------------------------------------------------------------------------

namespace {
std::vector<Box>* g_boxes;
} // namespace

void SetRecorder(std::vector<Box>* boxes) { g_boxes = boxes; }

void NoteRect(float x0, float y0, float x1, float y1, const char* what)
{
    if (g_boxes)
        g_boxes->push_back({x0, y0, x1, y1, what});
}

void Note(const char* what)
{
    if (!g_boxes)
        return;
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    NoteRect(a.x, a.y, b.x, b.y, what);
}

// --- the tool strip --------------------------------------------------------------------------------------------------

namespace {

const char kDown[] = " \xE2\x96\xBE"; // (" ▾": a button that opens a list)

float ButtonWidth(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

ImVec4 AccentButton()
{
    ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    accent.w = 0.85f;
    return accent;
}

// A button lit while `on`.
bool Toggle(const char* label, bool on)
{
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, AccentButton());
    bool pressed = ImGui::Button(label);
    if (on)
        ImGui::PopStyleColor();
    return pressed;
}

// A one-line tooltip: what it does, and its key when it has one.
void Tip(const char* what, const std::string& keys = std::string())
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;
    if (keys.empty())
        ImGui::SetTooltip("%s", what);
    else
        ImGui::SetTooltip("%s  (%s)", what, keys.c_str());
}

std::string KeyOf(const State& s, const char* id) { return s.keyOf ? s.keyOf(id) : std::string(); }

std::string GridText(float grid)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%g%s###stripgrid", grid, kDown);
    return text;
}

const char* PickText(uint32_t bits)
{
    switch (bits & 7) {
    case 7: return "Pick: All";
    case 0: return "Pick: None";
    case 1: return "Pick: Nodes";
    case 2: return "Pick: Objects";
    case 4: return "Pick: Scenery";
    default: return "Pick: Custom";
    }
}

std::string ProblemsText(const State& s)
{
    int count = s.errors + s.warnings;
    char text[48];
    if (count > 0)
        std::snprintf(text, sizeof(text), "Problems %d###stripproblems", count);
    else
        std::snprintf(text, sizeof(text), "Problems###stripproblems");
    return text;
}

std::string LevelText(const State& s, float scale)
{
    std::string text = s.level.empty() ? std::string("No level") : s.level + " (Edited)";
    if (s.unsaved)
        text += " *";
    return Elide(text, 240.0f * scale);
}

std::string ShowAllText(const State& s)
{
    char text[48];
    std::snprintf(text, sizeof(text), "Show All (%d)###stripshowall", s.hidden);
    return text;
}

// The controls, in the order they sit; their slots, and how each is drawn.
enum ItemId { kMode, kTools, kSpace, kSnapGrid, kPickItem, kShowAllItem, kStopItem, kLevel, kBudgetItem, kProblemsItem };

struct Item {
    ItemId id;
    Slot slot;
};

std::vector<Item> Items(const State& s, float scale)
{
    const float inner = 2.0f * scale, group = 10.0f * scale;
    std::vector<Item> items;
    auto add = [&](ItemId id, float width, bool right, int priority) {
        items.push_back({id, {width, group, right, priority}});
    };
    add(kMode, ButtonWidth("Fly") + inner + ButtonWidth("Select"), false, 3);
    add(kTools, ButtonWidth("Move") + inner + ButtonWidth("Rotate") + inner + ButtonWidth("Scale"), false, 9);
    add(kSpace, std::max(ButtonWidth("World"), ButtonWidth("Local")), false, 6);
    add(kSnapGrid, ButtonWidth("Snap") + inner + ButtonWidth(GridText(s.grid).c_str()), false, 8);
    add(kPickItem, ButtonWidth((std::string(PickText(s.pickFilter)) + kDown).c_str()), false, 4);
    if (s.hidden > 0)
        add(kShowAllItem, ButtonWidth(ShowAllText(s).c_str()), false, 2);
    if (s.simulating)
        add(kStopItem, ButtonWidth("Stop Simulation"), false, 10);
    add(kLevel, ImGui::CalcTextSize(LevelText(s, scale).c_str()).x, true, 1);
    if (s.budget)
        add(kBudgetItem, 44.0f * scale, true, 5);
    add(kProblemsItem, ButtonWidth(ProblemsText(s).c_str()), true, 7);
    return items;
}

void DrawItem(ItemId id, const State& s, float scale, Output* out)
{
    const float inner = 2.0f * scale;
    switch (id) {
    case kMode:
        if (Toggle("Fly", !s.selection))
            out->events.push_back({Event::kDeselect});
        Tip("Fly the camera; nothing selected", KeyOf(s, "deselect"));
        Note("Fly");
        ImGui::SameLine(0.0f, inner);
        Toggle("Select", s.selection);
        Tip("Click an object, piece or gizmo to select it");
        Note("Select");
        break;
    case kTools: {
        static const char* const kLabels[3] = {"Move", "Rotate", "Scale"};
        static const char* const kIds[3] = {"tool_move", "tool_rotate", "tool_scale"};
        static const char* const kTips[3] = {"Move tool", "Rotate tool", "Scale tool"};
        for (int t = 0; t < 3; ++t) {
            if (t)
                ImGui::SameLine(0.0f, inner);
            if (Toggle(kLabels[t], s.tool == t))
                out->events.push_back({Event::kTool, static_cast<float>(t)});
            Tip(kTips[t], KeyOf(s, kIds[t]));
            Note(kLabels[t]);
        }
        break;
    }
    case kSpace: {
        // (as wide as the wider label, so it doesn't move when switched)
        float width = std::max(ButtonWidth("World"), ButtonWidth("Local"));
        if (ImGui::Button(s.local ? "Local###stripspace" : "World###stripspace", ImVec2(width, 0.0f)))
            out->events.push_back({Event::kSpace});
        Tip(s.local ? "Gizmo axes: local" : "Gizmo axes: world", KeyOf(s, "tool_space"));
        Note("Space");
        break;
    }
    case kSnapGrid: {
        if (Toggle("Snap", s.snap))
            out->events.push_back({Event::kSnap, s.snap ? 0.0f : 1.0f});
        Tip("Snap while moving");
        Note("Snap");
        ImGui::SameLine(0.0f, inner);
        std::string grid = GridText(s.grid);
        if (ImGui::Button(grid.c_str()))
            ImGui::OpenPopup("##stripgridlist");
        Tip("Grid size");
        Note("Grid");
        if (ImGui::BeginPopup("##stripgridlist")) {
            for (float size : s.grids) {
                char label[16];
                std::snprintf(label, sizeof(label), "%g", size);
                if (ImGui::Selectable(label, size == s.grid))
                    out->events.push_back({Event::kGrid, size});
            }
            ImGui::EndPopup();
        }
        break;
    }
    case kPickItem: {
        std::string label = std::string(PickText(s.pickFilter)) + kDown + "###strippick";
        if (ImGui::Button(label.c_str()))
            ImGui::OpenPopup("##strippicklist");
        Tip("What a click in the view selects");
        Note("Pick");
        if (ImGui::BeginPopup("##strippicklist")) {
            static const struct {
                const char* label;
                uint32_t bit;
            } kPick[] = {{"Nodes", 1}, {"Objects", 2}, {"Scenery", 4}};
            for (const auto& p : kPick) {
                if (ImGui::MenuItem(p.label, nullptr, (s.pickFilter & p.bit) != 0))
                    out->events.push_back({Event::kPick, static_cast<float>(s.pickFilter ^ p.bit)});
            }
            ImGui::EndPopup();
        }
        break;
    }
    case kShowAllItem:
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.28f, 0.52f, 0.9f));
        if (ImGui::Button(ShowAllText(s).c_str()))
            out->events.push_back({Event::kShowAll});
        ImGui::PopStyleColor();
        Tip("Show items hidden in the editor", KeyOf(s, "show_all"));
        Note("ShowAll");
        break;
    case kStopItem:
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.15f, 1.0f));
        if (ImGui::Button("Stop Simulation"))
            out->events.push_back({Event::kSimulate});
        ImGui::PopStyleColor();
        Tip("Stop the simulation", KeyOf(s, "simulate"));
        Note("Stop");
        break;
    case kLevel: {
        // (text, set on the buttons' baseline)
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ImGui::GetStyle().FramePadding.y));
        ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "%s", LevelText(s, scale).c_str());
        out->levelHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        Note("Level");
        break;
    }
    case kBudgetItem: {
        float width = 44.0f * scale, frame = ImGui::GetFrameHeight();
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##stripbudget", ImVec2(width, frame));
        if (ImGui::IsItemClicked())
            out->events.push_back({Event::kBudget});
        out->budgetHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        Note("Budget");
        float bar = ImGui::GetTextLineHeight() * 0.6f;
        float top = at.y + (frame - bar) * 0.5f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        uint32_t c = s.budgetArgb;
        ImU32 colour = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
        draw->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + bar), IM_COL32(60, 62, 66, 255), 2.0f);
        float fill = std::clamp(s.budgetFill, 0.0f, 1.0f);
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
        if (ImGui::Button(ProblemsText(s).c_str()))
            out->events.push_back({Event::kProblems});
        ImGui::PopStyleColor(colours);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            std::string keys = KeyOf(s, "problems");
            ImGui::SetTooltip("%d errors, %d warnings%s%s%s", s.errors, s.warnings, keys.empty() ? "" : "  (",
                              keys.c_str(), keys.empty() ? "" : ")");
        }
        Note("Problems");
        break;
    }
    }
}

} // namespace

void Draw(const State& state, float x, float y, float width, float scale, Output* out)
{
    std::vector<Item> items = Items(state, scale);
    std::vector<Slot> slots;
    for (const Item& item : items)
        slots.push_back(item.slot);
    const char* more = "\xC2\xBB###stripmore"; // ("»")
    float moreWidth = ButtonWidth(more);
    Plan plan = Fit(slots, x, x + width, moreWidth, 10.0f * scale);
    for (size_t i = 0; i < items.size(); ++i) {
        if (!plan.shown[i])
            continue;
        ImGui::SetCursorScreenPos(ImVec2(plan.x[i], y));
        ImGui::PushID(static_cast<int>(items[i].id));
        DrawItem(items[i].id, state, scale, out);
        ImGui::PopID();
    }
    if (!plan.overflow)
        return;
    ImGui::SetCursorScreenPos(ImVec2(plan.overflowX, y));
    if (ImGui::Button(more))
        ImGui::OpenPopup("##stripoverflow");
    Tip("More controls");
    Note("More");
    if (ImGui::BeginPopup("##stripoverflow")) {
        // (the hidden controls, one a line; not recorded - they're in a popup of their own)
        std::vector<Box>* boxes = g_boxes;
        g_boxes = nullptr;
        for (size_t i = 0; i < items.size(); ++i) {
            if (plan.shown[i])
                continue;
            ImGui::PushID(static_cast<int>(items[i].id));
            DrawItem(items[i].id, state, scale, out);
            ImGui::PopID();
        }
        g_boxes = boxes;
        ImGui::EndPopup();
    }
}

// --- the status bar ----------------------------------------------------------------------------------------------------

void StatusRow(const std::string& message, const std::vector<std::pair<std::string, std::string>>& hints,
               const std::string& right, float x, float y, float width, const Ux8::Fonts& fonts)
{
    float s = fonts.scale;
    const float gap = 16.0f * s;
    // The right-hand text first: at most 45% of the row, elided.
    if (fonts.mono)
        ImGui::PushFont(fonts.mono);
    std::string rightText = Elide(right, width * 0.45f);
    float rightWidth = rightText.empty() ? 0.0f : ImGui::CalcTextSize(rightText.c_str()).x;
    if (!rightText.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(x + width - rightWidth, y));
        ImGui::TextDisabled("%s", rightText.c_str());
        Note("StatusRight");
    }
    if (fonts.mono)
        ImGui::PopFont();
    float leftWidth = std::max(0.0f, width - rightWidth - (rightText.empty() ? 0.0f : gap));
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (fonts.small)
        ImGui::PushFont(fonts.small);
    if (!message.empty()) {
        std::string text = Elide(message, leftWidth);
        if (!text.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.5f, 1.0f), "%s", text.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && text != message)
                ImGui::SetTooltip("%s", message.c_str());
            Note("StatusMessage");
        }
    } else {
        Ux8::HintRow(hints, fonts, leftWidth);
    }
    if (fonts.small)
        ImGui::PopFont();
}

} // namespace Front::LevelEditor::UiStrip
