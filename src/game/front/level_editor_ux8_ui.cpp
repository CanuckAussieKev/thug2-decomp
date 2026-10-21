// NOT ORIGINAL - the level editor's navigation and hints: the UI (ours). See level_editor_ux8.h. Only ImGui's own API.

#include "game/front/level_editor_ux8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_hints.h"
#include "game/front/level_editor_organize.h"
#include "game/front/level_editor_ui_strip.h"
#include "game/front/level_editor_ux3.h"
#include "game/front/level_editor_ux8_pie.h"
#include "imgui.h"

namespace Front::LevelEditor::Ux8 {

// (level_editor_ux8.cpp)
const Pie::State& PieNow();
void NoteViewCentre(float x, float y);

namespace {

const ImU32 kCap = IM_COL32(60, 63, 68, 255);
const ImU32 kCapEdge = IM_COL32(92, 96, 104, 255);
const ImU32 kInk = IM_COL32(230, 232, 236, 255);
const ImU32 kLit = IM_COL32(240, 140, 60, 255);

// A mouse: its body and the button `button` (0 left, 1 right, 2 middle / wheel; -1 none) lit.
void MouseGlyph(ImDrawList* draw, ImVec2 at, float h, int button, bool wheel, bool twice)
{
    float w = h * 0.68f;
    ImVec2 a(at.x, at.y), b(at.x + w, at.y + h);
    float r = w * 0.45f;
    draw->AddRectFilled(a, b, kCap, r);
    draw->AddRect(a, b, kCapEdge, r);
    float split = at.y + h * 0.42f;
    if (button == 0)
        draw->AddRectFilled(a, ImVec2(at.x + w * 0.5f, split), kLit, r, ImDrawFlags_RoundCornersTopLeft);
    if (button == 1)
        draw->AddRectFilled(ImVec2(at.x + w * 0.5f, at.y), ImVec2(b.x, split), kLit, r, ImDrawFlags_RoundCornersTopRight);
    draw->AddLine(ImVec2(at.x, split), ImVec2(b.x, split), kCapEdge);
    draw->AddLine(ImVec2(at.x + w * 0.5f, at.y), ImVec2(at.x + w * 0.5f, split), kCapEdge);
    if (button == 2 || wheel)
        draw->AddRectFilled(ImVec2(at.x + w * 0.38f, at.y + h * 0.1f), ImVec2(at.x + w * 0.62f, at.y + h * 0.34f),
                            wheel ? kLit : kInk, w * 0.1f);
    if (twice) {
        draw->AddText(ImVec2(b.x + 1.0f, at.y), kLit, "2");
    }
}

// One token's glyph, at the cursor; the cursor moved past it.
void TokenGlyph(const std::string& token, const Fonts& fonts)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    float s = fonts.scale;
    Hints::Glyph g = Hints::GlyphOf(token);
    float width = 0.0f;
    switch (g) {
    case Hints::Glyph::Mouse:
    case Hints::Glyph::MouseLeft:
    case Hints::Glyph::MouseRight:
    case Hints::Glyph::MouseMiddle:
    case Hints::Glyph::Wheel:
    case Hints::Glyph::DoubleClick: {
        int button = g == Hints::Glyph::MouseLeft || g == Hints::Glyph::DoubleClick ? 0
                     : g == Hints::Glyph::MouseRight                               ? 1
                     : g == Hints::Glyph::MouseMiddle                              ? 2
                                                                                    : -1;
        MouseGlyph(draw, ImVec2(pos.x, pos.y), h, button, g == Hints::Glyph::Wheel, g == Hints::Glyph::DoubleClick);
        width = h * 0.68f + (g == Hints::Glyph::DoubleClick ? 8.0f * s : 0.0f);
        break;
    }
    case Hints::Glyph::PadA:
    case Hints::Glyph::PadB:
    case Hints::Glyph::PadX:
    case Hints::Glyph::PadY: {
        // (Xbox's colours: A green, B red, X blue, Y yellow)
        ImU32 colour = g == Hints::Glyph::PadA   ? IM_COL32(80, 170, 70, 255)
                       : g == Hints::Glyph::PadB ? IM_COL32(200, 60, 50, 255)
                       : g == Hints::Glyph::PadX ? IM_COL32(50, 110, 210, 255)
                                                 : IM_COL32(220, 180, 40, 255);
        float r = h * 0.5f;
        draw->AddCircleFilled(ImVec2(pos.x + r, pos.y + r), r, colour);
        const char letter[2] = {token.back(), 0};
        ImVec2 size = ImGui::CalcTextSize(letter);
        draw->AddText(ImVec2(pos.x + r - size.x * 0.5f, pos.y + r - size.y * 0.5f), IM_COL32(255, 255, 255, 255), letter);
        width = 2.0f * r;
        break;
    }
    default: {
        // A key cap (a controller's stick / trigger / shoulder: a darker, rounder cap).
        if (fonts.mono)
            ImGui::PushFont(fonts.mono);
        ImVec2 size = ImGui::CalcTextSize(token.c_str());
        float padX = 4.0f * s;
        bool pad = g == Hints::Glyph::PadOther;
        draw->AddRectFilled(ImVec2(pos.x, pos.y - 1.0f * s), ImVec2(pos.x + size.x + 2.0f * padX, pos.y + size.y + 1.0f * s),
                            pad ? IM_COL32(40, 42, 46, 255) : kCap, (pad ? 8.0f : 3.0f) * s);
        draw->AddText(ImVec2(pos.x + padX, pos.y), kInk, token.c_str());
        if (fonts.mono)
            ImGui::PopFont();
        width = size.x + 2.0f * padX;
        break;
    }
    }
    ImGui::Dummy(ImVec2(width, h));
}

} // namespace

namespace {

// One token's glyph width, as TokenGlyph draws it.
float TokenWidth(const std::string& token, const Fonts& fonts)
{
    float h = ImGui::GetTextLineHeight(), s = fonts.scale;
    switch (Hints::GlyphOf(token)) {
    case Hints::Glyph::Mouse:
    case Hints::Glyph::MouseLeft:
    case Hints::Glyph::MouseRight:
    case Hints::Glyph::MouseMiddle:
    case Hints::Glyph::Wheel:
        return h * 0.68f;
    case Hints::Glyph::DoubleClick:
        return h * 0.68f + 8.0f * s;
    case Hints::Glyph::PadA:
    case Hints::Glyph::PadB:
    case Hints::Glyph::PadX:
    case Hints::Glyph::PadY:
        return h;
    default: {
        if (fonts.mono)
            ImGui::PushFont(fonts.mono);
        float w = ImGui::CalcTextSize(token.c_str()).x;
        if (fonts.mono)
            ImGui::PopFont();
        return w + 8.0f * s;
    }
    }
}

// A hint's width as HintRow draws it: its glyphs with "+" between them, then what it does (the small font).
float HintWidth(const std::pair<std::string, std::string>& hint, const Fonts& fonts)
{
    float s = fonts.scale, width = 0.0f;
    std::vector<std::string> tokens = Hints::Tokens(hint.first);
    for (size_t t = 0; t < tokens.size(); ++t) {
        if (t)
            width += 2.0f * s + ImGui::CalcTextSize("+").x;
        width += TokenWidth(tokens[t], fonts);
    }
    if (fonts.small)
        ImGui::PushFont(fonts.small);
    width += 4.0f * s + ImGui::CalcTextSize(hint.second.c_str()).x;
    if (fonts.small)
        ImGui::PopFont();
    return width;
}

} // namespace

void HintRow(const std::vector<std::pair<std::string, std::string>>& hints, const Fonts& fonts, float maxWidth)
{
    float s = fonts.scale;
    const char* ellipsis = "\xE2\x80\xA6";
    float used = 0.0f;
    for (size_t i = 0; i < hints.size(); ++i) {
        // (what doesn't fit is left out, an ellipsis in its place - never drawn over the right-hand text)
        if (maxWidth > 0.0f) {
            float gap = i ? 12.0f * s : 0.0f;
            float width = HintWidth(hints[i], fonts);
            bool last = i + 1 == hints.size();
            float reserve = last ? 0.0f : 12.0f * s + ImGui::CalcTextSize(ellipsis).x;
            if (used + gap + width + reserve > maxWidth) {
                if (used + gap + ImGui::CalcTextSize(ellipsis).x <= maxWidth) {
                    if (i)
                        ImGui::SameLine(0.0f, gap);
                    ImGui::TextDisabled("%s", ellipsis);
                    UiStrip::Note("HintMore");
                }
                break;
            }
            used += gap + width;
        }
        if (i)
            ImGui::SameLine(0.0f, 12.0f * s);
        ImGui::BeginGroup();
        std::vector<std::string> tokens = Hints::Tokens(hints[i].first);
        for (size_t t = 0; t < tokens.size(); ++t) {
            if (t) {
                ImGui::SameLine(0.0f, 1.0f * s);
                ImGui::TextDisabled("+");
                ImGui::SameLine(0.0f, 1.0f * s);
            }
            TokenGlyph(tokens[t], fonts);
        }
        ImGui::SameLine(0.0f, 4.0f * s);
        if (fonts.small)
            ImGui::PushFont(fonts.small);
        ImGui::TextUnformatted(hints[i].second.c_str());
        if (fonts.small)
            ImGui::PopFont();
        ImGui::EndGroup();
        UiStrip::Note("Hint");
    }
}

void Windows(float vx, float vy, float vw, float vh, const Fonts& fonts)
{
    NoteViewCentre(vx + vw * 0.5f, vy + vh * 0.5f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float s = fonts.scale;
    ImVec2 centre(vx + vw * 0.5f, vy + vh * 0.5f);
    // The crosshair: the controller's cursor, or walking's middle.
    if (PadOwnsCursor() || Walking()) {
        float r = 7.0f * s;
        draw->AddCircle(centre, r, IM_COL32(255, 255, 255, 200), 0, 1.5f * s);
        draw->AddLine(ImVec2(centre.x - 2.0f * r, centre.y), ImVec2(centre.x - r, centre.y), IM_COL32(255, 255, 255, 200), 1.5f * s);
        draw->AddLine(ImVec2(centre.x + r, centre.y), ImVec2(centre.x + 2.0f * r, centre.y), IM_COL32(255, 255, 255, 200), 1.5f * s);
        draw->AddLine(ImVec2(centre.x, centre.y - 2.0f * r), ImVec2(centre.x, centre.y - r), IM_COL32(255, 255, 255, 200), 1.5f * s);
        draw->AddLine(ImVec2(centre.x, centre.y + r), ImVec2(centre.x, centre.y + 2.0f * r), IM_COL32(255, 255, 255, 200), 1.5f * s);
    }
    if (Walking() && fonts.ui) {
        const char* text = "WALKING  -  W A S D walk, Space jumps, Shift runs  -  Shift+` or Esc stops";
        ImVec2 size = fonts.ui->CalcTextSizeA(fonts.ui->FontSize, 1e9f, 0.0f, text);
        ImVec2 at(centre.x - size.x * 0.5f, vy + 14.0f * s);
        draw->AddRectFilled(ImVec2(at.x - 8.0f * s, at.y - 4.0f * s), ImVec2(at.x + size.x + 8.0f * s, at.y + size.y + 4.0f * s),
                            IM_COL32(16, 17, 19, 200), 6.0f * s);
        draw->AddText(fonts.ui, fonts.ui->FontSize, at, kInk, text);
    }
    // The pie.
    const Pie::State& pie = PieNow();
    if (!pie.open || pie.items.empty())
        return;
    ImFont* font = fonts.ui ? fonts.ui : ImGui::GetFont();
    ImFont* small = fonts.small ? fonts.small : font;
    ImVec2 c(pie.x, pie.y);
    float radius = 120.0f * s;
    int n = static_cast<int>(pie.items.size());
    draw->AddCircleFilled(c, Pie::kDeadzone * s, IM_COL32(16, 17, 19, 170));
    draw->AddCircle(c, Pie::kDeadzone * s, IM_COL32(255, 255, 255, 90), 0, 1.5f * s);
    // (the pointing line)
    if (pie.hovered >= 0) {
        float dx, dy;
        Nav::SliceDirection(pie.hovered, n, &dx, &dy);
        draw->AddLine(c, ImVec2(c.x + dx * Pie::kDeadzone * s, c.y + dy * Pie::kDeadzone * s), kLit, 3.0f * s);
    }
    for (int i = 0; i < n; ++i) {
        const Pie::Item& item = pie.items[i];
        float dx, dy;
        Nav::SliceDirection(i, n, &dx, &dy);
        ImVec2 at(c.x + dx * radius, c.y + dy * radius);
        bool enabled = !item.enabled || item.enabled();
        int checked = item.checked ? item.checked() : -1;
        std::string label = (checked == 1 ? "\xE2\x97\x8F " : "") + item.label; // (a dot: on)
        ImVec2 size = font->CalcTextSizeA(font->FontSize, 1e9f, 0.0f, label.c_str());
        ImVec2 keySize = item.keys.empty() ? ImVec2(0, 0) : small->CalcTextSizeA(small->FontSize, 1e9f, 0.0f, item.keys.c_str());
        float w = std::max(size.x, keySize.x) + 18.0f * s, h = size.y + (item.keys.empty() ? 0.0f : keySize.y) + 8.0f * s;
        // (left / right of the centre: the box grows away from it)
        float left = dx > 0.3f ? at.x - 10.0f * s : dx < -0.3f ? at.x - w + 10.0f * s : at.x - w * 0.5f;
        ImVec2 a(left, at.y - h * 0.5f), b(left + w, at.y + h * 0.5f);
        bool hot = i == pie.hovered;
        draw->AddRectFilled(a, b, hot ? IM_COL32(200, 100, 40, 235) : IM_COL32(30, 32, 36, 230), 6.0f * s);
        draw->AddRect(a, b, hot ? kLit : IM_COL32(90, 94, 100, 255), 6.0f * s);
        ImU32 ink = enabled ? kInk : IM_COL32(130, 132, 136, 255);
        draw->AddText(font, font->FontSize, ImVec2(a.x + 9.0f * s, a.y + 4.0f * s), ink, label.c_str());
        if (!item.keys.empty())
            draw->AddText(small, small->FontSize, ImVec2(a.x + 9.0f * s, a.y + 4.0f * s + size.y),
                          IM_COL32(255, 200, 120, enabled ? 255 : 140), item.keys.c_str());
    }
}

void SetsMenu(const Fonts& fonts)
{
    float s = fonts.scale;
    if (!ImGui::BeginMenu("Selection Sets"))
        return;
    const Organize::State& state = Organize::Current();
    ImGui::TextDisabled("Quick Sets");
    for (int i = 1; i <= Nav::kQuickSets; ++i) {
        std::string name = Nav::QuickSetName(i);
        int at = Nav::FindGroup(state, name);
        ImGui::PushID(i);
        char label[64];
        std::snprintf(label, sizeof(label), "%s  %s", name.c_str(),
                      at >= 0 ? ("(" + std::to_string(state.groups[at].members.size()) + ")").c_str() : "-");
        ImGui::TextUnformatted(label);
        ImGui::SameLine(110.0f * s);
        if (ImGui::SmallButton("Store"))
            Ux3::Queue([name] { StoreSet(name); });
        if (at >= 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
                Ux3::Queue([name] { RecallSet(name); });
            ImGui::SameLine();
            if (ImGui::SmallButton("+"))
                Ux3::Queue([at] { AddToSet(at); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Add selection to set");
            ImGui::SameLine();
            if (ImGui::SmallButton("-"))
                Ux3::Queue([at] { TakeFromSet(at); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove selection from set");
        }
        ImGui::PopID();
    }
    // The other groups (named in the Outliner: Ctrl+G). (Clicks queued: done in the next Update, as UX3's panels.)
    bool any = false;
    for (int g = 0; g < static_cast<int>(state.groups.size()); ++g) {
        const Organize::Group& group = state.groups[g];
        if (group.name.rfind("Set ", 0) == 0 && group.name.size() == 5 && group.name[4] >= '1' && group.name[4] <= '9')
            continue;
        if (!any) {
            ImGui::Separator();
            ImGui::TextDisabled("Groups");
            any = true;
        }
        ImGui::PushID(100 + g);
        ImGui::TextUnformatted(group.name.c_str());
        ImGui::SameLine(110.0f * s);
        if (ImGui::SmallButton("Select"))
            Ux3::Queue([g] { Organize::SelectGroup(g); });
        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
            Ux3::Queue([g] { AddToSet(g); });
        ImGui::SameLine();
        if (ImGui::SmallButton("-"))
            Ux3::Queue([g] { TakeFromSet(g); });
        ImGui::PopID();
    }
    ImGui::EndMenu();
}

void PrefsSection()
{
    ImGui::Spacing();
    ImGui::TextUnformatted("Controller");
    ImGui::Separator();
    bool pad = PadMode();
    if (ImGui::Checkbox("Controller Mode", &pad))
        SetPadMode(pad);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use a game controller in the editor");
}

} // namespace Front::LevelEditor::Ux8
