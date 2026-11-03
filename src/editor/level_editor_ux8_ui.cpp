// NOT ORIGINAL - the level editor's navigation and hints: the UI (ours). See level_editor_ux8.h. Only ImGui's own API.

#include "editor/level_editor_ux8.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor_hints.h"
#include "editor/level_editor_nav.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_ui_strip.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8_pie.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::ux8 {

namespace {

constexpr float kHalf = 0.5f; // half

const ImU32 kCap = IM_COL32(60, 63, 68, 255);
const ImU32 kCapEdge = IM_COL32(92, 96, 104, 255);
const ImU32 kInk = IM_COL32(230, 232, 236, 255);
const ImU32 kLit = IM_COL32(240, 140, 60, 255);

// A mouse glyph's height and the button shown.
struct GlyphSize {
    float h{};
    int button{};
};

// A mouse: its body and the button `button` (0 left, 1 right, 2 middle / wheel; -1 none) lit.
void mouse_glyph(ImDrawList* draw, ImVec2 at, GlyphSize glyph_size, bool wheel, bool twice)
{
    const auto h = glyph_size.h;
    const auto button = glyph_size.button;
    const float w = h * 0.68f;
    const ImVec2 a(at.x, at.y);
    const ImVec2 b(at.x + w, at.y + h);
    const float r = w * 0.45f;
    draw->AddRectFilled(a, b, kCap, r);
    draw->AddRect(a, b, kCapEdge, r);
    const float split = at.y + h * 0.42f;
    if (button == 0)
        draw->AddRectFilled(a, ImVec2(at.x + w * kHalf, split), kLit, r, ImDrawFlags_RoundCornersTopLeft);
    if (button == 1)
        draw->AddRectFilled(ImVec2(at.x + w * kHalf, at.y), ImVec2(b.x, split), kLit, r,
                            ImDrawFlags_RoundCornersTopRight);
    draw->AddLine(ImVec2(at.x, split), ImVec2(b.x, split), kCapEdge);
    draw->AddLine(ImVec2(at.x + w * kHalf, at.y), ImVec2(at.x + w * kHalf, split), kCapEdge);
    if (button == 2 || wheel)
        draw->AddRectFilled(ImVec2(at.x + w * 0.38f, at.y + h * 0.1f), ImVec2(at.x + w * 0.62f, at.y + h * 0.34f),
                            wheel ? kLit : kInk, w * 0.1f);
    if (twice) {
        draw->AddText(ImVec2(b.x + 1.0f, at.y), kLit, "2");
    }
}

// One token's glyph, at the cursor; the cursor moved past it.
void token_glyph(const std::string& token, const Fonts& fonts)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const float s = fonts.scale;
    const hints::Glyph g = hints::glyph_of(token);
    float width = 0.0f;
    switch (g) {
    case hints::Glyph::kMouse:
    case hints::Glyph::kMouseLeft:
    case hints::Glyph::kMouseRight:
    case hints::Glyph::kMouseMiddle:
    case hints::Glyph::kWheel:
    case hints::Glyph::kDoubleClick: {
        const int button = g == hints::Glyph::kMouseLeft || g == hints::Glyph::kDoubleClick ? 0
                           : g == hints::Glyph::kMouseRight                                 ? 1
                           : g == hints::Glyph::kMouseMiddle                                ? 2
                                                                                            : -1;
        mouse_glyph(draw, ImVec2(pos.x, pos.y), {.h = h, .button = button}, g == hints::Glyph::kWheel, g == hints::Glyph::kDoubleClick);
        width = h * 0.68f + (g == hints::Glyph::kDoubleClick ? 8.0f * s : 0.0f);
        break;
    }
    case hints::Glyph::kPadA:
    case hints::Glyph::kPadB:
    case hints::Glyph::kPadX:
    case hints::Glyph::kPadY: {
        // (Xbox's colours: A green, B red, X blue, Y yellow)
        const ImU32 colour = g == hints::Glyph::kPadA   ? IM_COL32(80, 170, 70, 255)
                             : g == hints::Glyph::kPadB ? IM_COL32(200, 60, 50, 255)
                             : g == hints::Glyph::kPadX ? IM_COL32(50, 110, 210, 255)
                                                        : IM_COL32(220, 180, 40, 255);
        const float r = h * 0.5f;
        draw->AddCircleFilled(ImVec2(pos.x + r, pos.y + r), r, colour);
        const std::array<char, 2> letter = {{token.back(), 0}};
        const ImVec2 size = ImGui::CalcTextSize(letter.data());
        draw->AddText(ImVec2(pos.x + r - size.x * kHalf, pos.y + r - size.y * kHalf), IM_COL32(255, 255, 255, 255),
                      letter.data());
        width = 2.0f * r;
        break;
    }
    default: {
        // A key cap (a controller's stick / trigger / shoulder: a darker, rounder cap).
        if (fonts.mono)
            ImGui::PushFont(fonts.mono);
        const ImVec2 size = ImGui::CalcTextSize(token.c_str());
        const float pad_x = 4.0f * s;
        const bool pad = g == hints::Glyph::kPadOther;
        draw->AddRectFilled(ImVec2(pos.x, pos.y - 1.0f * s),
                            ImVec2(pos.x + size.x + 2.0f * pad_x, pos.y + size.y + 1.0f * s),
                            pad ? IM_COL32(40, 42, 46, 255) : kCap, (pad ? 8.0f : 3.0f) * s);
        draw->AddText(ImVec2(pos.x + pad_x, pos.y), kInk, token.c_str());
        if (fonts.mono)
            ImGui::PopFont();
        width = size.x + 2.0f * pad_x;
        break;
    }
    }
    ImGui::Dummy(ImVec2(width, h));
}

} // namespace

namespace {

// One token's glyph width, as token_glyph draws it.
float token_width(const std::string& token, const Fonts& fonts)
{
    const float h = ImGui::GetTextLineHeight();
    const float s = fonts.scale;
    switch (hints::glyph_of(token)) {
    case hints::Glyph::kMouse:
    case hints::Glyph::kMouseLeft:
    case hints::Glyph::kMouseRight:
    case hints::Glyph::kMouseMiddle:
    case hints::Glyph::kWheel:
        return h * 0.68f;
    case hints::Glyph::kDoubleClick:
        return h * 0.68f + 8.0f * s;
    case hints::Glyph::kPadA:
    case hints::Glyph::kPadB:
    case hints::Glyph::kPadX:
    case hints::Glyph::kPadY:
        return h;
    default: {
        if (fonts.mono)
            ImGui::PushFont(fonts.mono);
        const float w = ImGui::CalcTextSize(token.c_str()).x;
        if (fonts.mono)
            ImGui::PopFont();
        return w + 8.0f * s;
    }
    }
}

// A hint's width as hint_row draws it: its glyphs with "+" between them, then what it does (the small font).
float hint_width(const std::pair<std::string, std::string>& hint, const Fonts& fonts)
{
    const float s = fonts.scale;
    float width = 0.0f;
    std::vector<std::string> tokens = hints::get_tokens(hint.first);
    for (size_t t = 0; t < tokens.size(); ++t) {
        if (t)
            width += 2.0f * s + ImGui::CalcTextSize("+").x;
        width += token_width(core::at(tokens, t), fonts);
    }
    if (fonts.small)
        ImGui::PushFont(fonts.small);
    width += 4.0f * s + ImGui::CalcTextSize(hint.second.c_str()).x;
    if (fonts.small)
        ImGui::PopFont();
    return width;
}

} // namespace

void hint_row(const std::vector<std::pair<std::string, std::string>>& hints, const Fonts& fonts, float max_width)
{
    const float s = fonts.scale;
    const char* ellipsis = "\xE2\x80\xA6";
    float used = 0.0f;
    for (size_t i = 0; i < hints.size(); ++i) {
        // (what doesn't fit is left out, an ellipsis in its place - never drawn over the right-hand text)
        if (max_width > 0.0f) {
            const float gap = i ? 12.0f * s : 0.0f;
            const float width = hint_width(core::at(hints, i), fonts);
            const bool last = i + 1 == hints.size();
            const float reserve = last ? 0.0f : 12.0f * s + ImGui::CalcTextSize(ellipsis).x;
            if (used + gap + width + reserve > max_width) {
                if (used + gap + ImGui::CalcTextSize(ellipsis).x <= max_width) {
                    if (i)
                        ImGui::SameLine(0.0f, gap);
                    gui::text_disabled("%s", ellipsis);
                    ui_strip::get_note("HintMore");
                }
                break;
            }
            used += gap + width;
        }
        if (i)
            ImGui::SameLine(0.0f, 12.0f * s);
        ImGui::BeginGroup();
        std::vector<std::string> tokens = hints::get_tokens(core::at(hints, i).first);
        for (size_t t = 0; t < tokens.size(); ++t) {
            if (t) {
                ImGui::SameLine(0.0f, 1.0f * s);
                gui::text_disabled("+");
                ImGui::SameLine(0.0f, 1.0f * s);
            }
            token_glyph(core::at(tokens, t), fonts);
        }
        ImGui::SameLine(0.0f, 4.0f * s);
        if (fonts.small)
            ImGui::PushFont(fonts.small);
        ImGui::TextUnformatted(core::at(hints, i).second.c_str());
        if (fonts.small)
            ImGui::PopFont();
        ImGui::EndGroup();
        ui_strip::get_note("Hint");
    }
}

void get_windows(float vx, float vy, float vw, float vh, const Fonts& fonts)
{
    note_view_centre({.x = vx + vw * kHalf, .y = vy + vh * kHalf});
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float s = fonts.scale;
    const ImVec2 centre(vx + vw * 0.5f, vy + vh * 0.5f);
    // The crosshair: the controller's cursor, or walking's middle.
    if (pad_owns_cursor() || on_foot()) {
        const float r = 7.0f * s;
        draw->AddCircle(centre, r, IM_COL32(255, 255, 255, 200), 0, 1.5f * s);
        draw->AddLine(ImVec2(centre.x - 2.0f * r, centre.y), ImVec2(centre.x - r, centre.y),
                      IM_COL32(255, 255, 255, 200), 1.5f * s);
        draw->AddLine(ImVec2(centre.x + r, centre.y), ImVec2(centre.x + 2.0f * r, centre.y),
                      IM_COL32(255, 255, 255, 200), 1.5f * s);
        draw->AddLine(ImVec2(centre.x, centre.y - 2.0f * r), ImVec2(centre.x, centre.y - r),
                      IM_COL32(255, 255, 255, 200), 1.5f * s);
        draw->AddLine(ImVec2(centre.x, centre.y + r), ImVec2(centre.x, centre.y + 2.0f * r),
                      IM_COL32(255, 255, 255, 200), 1.5f * s);
    }
    if (on_foot() && fonts.ui) {
        const char* text = "WALK MODE  -  WASD walk, Space jump, Shift run, Esc exit";
        const ImVec2 size = fonts.ui->CalcTextSizeA(fonts.ui->FontSize, 1e9f, 0.0f, text);
        const ImVec2 at(centre.x - size.x * 0.5f, vy + 14.0f * s);
        draw->AddRectFilled(ImVec2(at.x - 8.0f * s, at.y - 4.0f * s),
                            ImVec2(at.x + size.x + 8.0f * s, at.y + size.y + 4.0f * s), IM_COL32(16, 17, 19, 200),
                            6.0f * s);
        draw->AddText(fonts.ui, fonts.ui->FontSize, at, kInk, text);
    }
    // The pie.
    const pie::State& pie = pie_now();
    if (!pie.open || pie.items.empty())
        return;
    ImFont* font = fonts.ui ? fonts.ui : ImGui::GetFont();
    ImFont* small = fonts.small ? fonts.small : font;
    const ImVec2 c(pie.x, pie.y);
    const float radius = 120.0f * s;
    const int n = static_cast<int>(pie.items.size());
    draw->AddCircleFilled(c, pie::kDeadzone * s, IM_COL32(16, 17, 19, 170));
    draw->AddCircle(c, pie::kDeadzone * s, IM_COL32(255, 255, 255, 90), 0, 1.5f * s);
    // (the pointing line)
    if (pie.hovered >= 0) {
        float dx = 0.0f, dy = 0.0f;
        nav::slice_direction(pie.hovered, n, &dx, &dy);
        draw->AddLine(c, ImVec2(c.x + dx * pie::kDeadzone * s, c.y + dy * pie::kDeadzone * s), kLit, 3.0f * s);
    }
    for (int i = 0; i < n; ++i) {
        const pie::Item& item = core::at(pie.items, i);
        float dx = 0.0f, dy = 0.0f;
        nav::slice_direction(i, n, &dx, &dy);
        const ImVec2 at(c.x + dx * radius, c.y + dy * radius);
        const bool enabled = !item.enabled || item.enabled();
        const int checked = item.checked ? item.checked() : -1;
        const std::string label = (checked == 1 ? "\xE2\x97\x8F " : "") + item.label; // (a dot: on)
        const ImVec2 size = font->CalcTextSizeA(font->FontSize, 1e9f, 0.0f, label.c_str());
        const ImVec2 key_size =
            item.keys.empty() ? ImVec2(0, 0) : small->CalcTextSizeA(small->FontSize, 1e9f, 0.0f, item.keys.c_str());
        const float w = std::max(size.x, key_size.x) + 18.0f * s;
        const float h = size.y + (item.keys.empty() ? 0.0f : key_size.y) + 8.0f * s;
        // (left / right of the centre: the box grows away from it)
        const float left = dx > 0.3f ? at.x - 10.0f * s : dx < -0.3f ? at.x - w + 10.0f * s : at.x - w * 0.5f;
        const ImVec2 a(left, at.y - h * 0.5f);
        const ImVec2 b(left + w, at.y + h * 0.5f);
        const bool hot = i == pie.hovered;
        draw->AddRectFilled(a, b, hot ? IM_COL32(200, 100, 40, 235) : IM_COL32(30, 32, 36, 230), 6.0f * s);
        draw->AddRect(a, b, hot ? kLit : IM_COL32(90, 94, 100, 255), 6.0f * s);
        const ImU32 ink = enabled ? kInk : IM_COL32(130, 132, 136, 255);
        draw->AddText(font, font->FontSize, ImVec2(a.x + 9.0f * s, a.y + 4.0f * s), ink, label.c_str());
        if (!item.keys.empty())
            draw->AddText(small, small->FontSize, ImVec2(a.x + 9.0f * s, a.y + 4.0f * s + size.y),
                          IM_COL32(255, 200, 120, enabled ? 255 : 140), item.keys.c_str());
    }
}

void sets_menu(const Fonts& fonts)
{
    const float s = fonts.scale;
    if (!ImGui::BeginMenu("Selection Sets"))
        return;
    const organize::State& state = organize::at_hand();
    gui::text_disabled("Quick Sets");
    for (int i = 1; i <= nav::kQuickSets; ++i) {
        const std::string name = nav::quick_set_name(i);
        const int at = nav::find_group(state, name);
        ImGui::PushID(i);
        std::array<char, 64> label{};
        text::format_to(label, "%s  %s", name.c_str(),
                        at >= 0 ? ("(" + std::to_string(core::at(state.groups, at).members.size()) + ")").c_str()
                                : "-");
        ImGui::TextUnformatted(label.data());
        ImGui::SameLine(110.0f * s);
        if (ImGui::SmallButton("Store"))
            ux3::get_queue([name] { store_set(name); });
        if (at >= 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
                ux3::get_queue([name] { recall_set(name); });
            ImGui::SameLine();
            if (ImGui::SmallButton("+"))
                ux3::get_queue([at] { add_to_set(at); });
            if (ImGui::IsItemHovered())
                gui::tooltip("Add selection to set");
            ImGui::SameLine();
            if (ImGui::SmallButton("-"))
                ux3::get_queue([at] { take_from_set(at); });
            if (ImGui::IsItemHovered())
                gui::tooltip("Remove selection from set");
        }
        ImGui::PopID();
    }
    // The other groups (named in the Outliner: Ctrl+G). (Clicks queued: done in the next Update, as UX3's panels.)
    bool any = false;
    for (int g = 0; std::cmp_less(g, state.groups.size()); ++g) {
        const organize::GroupSet& group = core::at(state.groups, g);
        if (group.name.starts_with("Set ") && group.name.size() == 5 && core::at(group.name, 4) >= '1' && core::at(group.name, 4) <= '9')
            continue;
        if (!any) {
            ImGui::Separator();
            gui::text_disabled("Groups");
            any = true;
        }
        ImGui::PushID(100 + g);
        ImGui::TextUnformatted(group.name.c_str());
        ImGui::SameLine(110.0f * s);
        if (ImGui::SmallButton("Select"))
            ux3::get_queue([g] { organize::select_group(g); });
        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
            ux3::get_queue([g] { add_to_set(g); });
        ImGui::SameLine();
        if (ImGui::SmallButton("-"))
            ux3::get_queue([g] { take_from_set(g); });
        ImGui::PopID();
    }
    ImGui::EndMenu();
}

void prefs_section()
{
    ImGui::Spacing();
    ImGui::TextUnformatted("Controller");
    ImGui::Separator();
    bool pad = pad_mode();
    if (ImGui::Checkbox("Controller Mode", &pad))
        set_pad_mode(pad);
    if (ImGui::IsItemHovered())
        gui::tooltip("Use a game controller in the editor");
}

} // namespace frontend::level_editor::ux8
