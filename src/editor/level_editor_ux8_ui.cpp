// NOT ORIGINAL - the level editor's navigation and hints: the UI (ours). See level_editor_ux8.h. Only ImGui's own API.

#include "editor/level_editor_ux8.h"

#include <algorithm>
#include <cstddef>
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
#include "editor/level_editor_ui_layout.h"

namespace frontend::level_editor::ux8 {

namespace {

constexpr float kHalf = 0.5F; // half
// The glyphs and hint boxes, at scale 1 unless fractions of the glyph height (s multiplies them).
constexpr float kMouseGlyphAspect = 0.68F; // the mouse glyph width, as a fraction of its height
constexpr std::size_t kSetNameLength = 5; // "Set <n>"
constexpr int kSetIdBase = 100; // keeps the set rows ids apart
constexpr float kWheelLeft = 0.38F; // the mouse wheel inside the mouse glyph, as fractions of its size
constexpr float kWheelTop = 0.1F;
constexpr float kWheelRight = 0.62F;
constexpr float kWheelBottom = 0.34F;
constexpr float kWheelRounding = 0.1F;
constexpr float kDoubleClickExtra = 8.0F; // the double-click glyph is this much wider
constexpr float kKeyCapExtra = 8.0F;      // a key cap is this much wider than its text
constexpr float kPadCapRounding = 8.0F;
constexpr float kKeyCapRounding = 3.0F;
constexpr float kHintTextGap = 4.0F; // between a hint glyphs and its text
constexpr float kHintGap = 12.0F;    // between hints
constexpr float kPieLineWidth = 1.5F;
constexpr float kPieSpokeWidth = 3.0F;
constexpr float kHintBoxPaddingX = 8.0F;
constexpr float kHintBoxPaddingY = 4.0F;
constexpr float kHintBoxRounding = 6.0F;
constexpr float kSliceRounding = 6.0F; // the pie menu slices
constexpr float kSliceTextInsetX = 9.0F;
constexpr float kSliceTextInsetY = 4.0F;
constexpr std::size_t kSetLabelSize = 64;
constexpr float kSetNameColumn = 110.0F;

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
    const float w = h * kMouseGlyphAspect;
    const ImVec2 a(at.x, at.y);
    const ImVec2 b(at.x + w, at.y + h);
    const float r = w * 0.45F;
    draw->AddRectFilled(a, b, kCap, r);
    draw->AddRect(a, b, kCapEdge, r);
    const float split = at.y + (h * 0.42F);
    if (button == 0)
        draw->AddRectFilled(a, ImVec2(at.x + (w * kHalf), split), kLit, r, ImDrawFlags_RoundCornersTopLeft);
    if (button == 1)
        draw->AddRectFilled(ImVec2(at.x + (w * kHalf), at.y), ImVec2(b.x, split), kLit, r,
                            ImDrawFlags_RoundCornersTopRight);
    draw->AddLine(ImVec2(at.x, split), ImVec2(b.x, split), kCapEdge);
    draw->AddLine(ImVec2(at.x + (w * kHalf), at.y), ImVec2(at.x + (w * kHalf), split), kCapEdge);
    if (button == 2 || wheel)
        draw->AddRectFilled(ImVec2(at.x + (w * kWheelLeft), at.y + (h * kWheelTop)),
                            ImVec2(at.x + (w * kWheelRight), at.y + (h * kWheelBottom)), wheel ? kLit : kInk,
                            w * kWheelRounding);
    if (twice) {
        draw->AddText(ImVec2(b.x + 1.0F, at.y), kLit, "2");
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
    float width = 0.0F;
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
        width = (h * kMouseGlyphAspect) + (g == hints::Glyph::kDoubleClick ? kDoubleClickExtra * s : 0.0F);
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
        const float r = h * 0.5F;
        draw->AddCircleFilled(ImVec2(pos.x + r, pos.y + r), r, colour);
        const std::array<char, 2> letter = {{token.back(), 0}};
        const ImVec2 size = ImGui::CalcTextSize(letter.data());
        draw->AddText(ImVec2(pos.x + r - (size.x * kHalf), pos.y + r - (size.y * kHalf)), IM_COL32(255, 255, 255, 255),
                      letter.data());
        width = ui_layout::kBothSides * r;
        break;
    }
    default: {
        // A key cap (a controller's stick / trigger / shoulder: a darker, rounder cap).
        if (fonts.mono)
            ImGui::PushFont(fonts.mono);
        const ImVec2 size = ImGui::CalcTextSize(token.c_str());
        const float pad_x = 4.0F * s;
        const bool pad = g == hints::Glyph::kPadOther;
        draw->AddRectFilled(ImVec2(pos.x, pos.y - (1.0F * s)),
                            ImVec2(pos.x + size.x + (ui_layout::kBothSides * pad_x), pos.y + size.y + (1.0F * s)),
                            pad ? IM_COL32(40, 42, 46, 255) : kCap, (pad ? kPadCapRounding : kKeyCapRounding) * s);
        draw->AddText(ImVec2(pos.x + pad_x, pos.y), kInk, token.c_str());
        if (fonts.mono)
            ImGui::PopFont();
        width = size.x + (ui_layout::kBothSides * pad_x);
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
        return h * kMouseGlyphAspect;
    case hints::Glyph::kDoubleClick:
        return (h * kMouseGlyphAspect) + (kDoubleClickExtra * s);
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
        return w + (kKeyCapExtra * s);
    }
    }
}

// A hint's width as hint_row draws it: its glyphs with "+" between them, then what it does (the small font).
float hint_width(const std::pair<std::string, std::string>& hint, const Fonts& fonts)
{
    const float s = fonts.scale;
    float width = 0.0F;
    std::vector<std::string> tokens = hints::get_tokens(hint.first);
    for (size_t t = 0; t < tokens.size(); ++t) {
        if (t)
            width += (ui_layout::kBothSides * s) + ImGui::CalcTextSize("+").x;
        width += token_width(core::at(tokens, t), fonts);
    }
    if (fonts.small)
        ImGui::PushFont(fonts.small);
    width += (kHintTextGap * s) + ImGui::CalcTextSize(hint.second.c_str()).x;
    if (fonts.small)
        ImGui::PopFont();
    return width;
}

} // namespace

void hint_row(const std::vector<std::pair<std::string, std::string>>& hints, const Fonts& fonts, float max_width)
{
    const float s = fonts.scale;
    const char* ellipsis = "\xE2\x80\xA6";
    float used = 0.0F;
    for (size_t i = 0; i < hints.size(); ++i) {
        // (what doesn't fit is left out, an ellipsis in its place - never drawn over the right-hand text)
        if (max_width > 0.0F) {
            const float gap = i ? 12.0F * s : 0.0F;
            const float width = hint_width(core::at(hints, i), fonts);
            const bool last = i + 1 == hints.size();
            const float reserve = last ? 0.0F : (12.0F * s) + ImGui::CalcTextSize(ellipsis).x;
            if (used + gap + width + reserve > max_width) {
                if (used + gap + ImGui::CalcTextSize(ellipsis).x <= max_width) {
                    if (i)
                        ImGui::SameLine(0.0F, gap);
                    gui::text_disabled("%s", ellipsis);
                    ui_strip::get_note("HintMore");
                }
                break;
            }
            used += gap + width;
        }
        if (i)
            ImGui::SameLine(0.0F, kHintGap * s);
        ImGui::BeginGroup();
        std::vector<std::string> tokens = hints::get_tokens(core::at(hints, i).first);
        for (size_t t = 0; t < tokens.size(); ++t) {
            if (t) {
                ImGui::SameLine(0.0F, 1.0F * s);
                gui::text_disabled("+");
                ImGui::SameLine(0.0F, 1.0F * s);
            }
            token_glyph(core::at(tokens, t), fonts);
        }
        ImGui::SameLine(0.0F, kHintTextGap * s);
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
    note_view_centre({.x = vx + (vw * kHalf), .y = vy + (vh * kHalf)});
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float s = fonts.scale;
    const ImVec2 centre(vx + (vw * 0.5F), vy + (vh * 0.5F));
    // The crosshair: the controller's cursor, or walking's middle.
    if (pad_owns_cursor() || on_foot()) {
        const float r = 7.0F * s;
        draw->AddCircle(centre, r, IM_COL32(255, 255, 255, 200), 0, kPieLineWidth * s);
        draw->AddLine(ImVec2(centre.x - (ui_layout::kBothSides * r), centre.y), ImVec2(centre.x - r, centre.y),
                      IM_COL32(255, 255, 255, 200), kPieLineWidth * s);
        draw->AddLine(ImVec2(centre.x + r, centre.y), ImVec2(centre.x + (ui_layout::kBothSides * r), centre.y),
                      IM_COL32(255, 255, 255, 200), kPieLineWidth * s);
        draw->AddLine(ImVec2(centre.x, centre.y - (ui_layout::kBothSides * r)), ImVec2(centre.x, centre.y - r),
                      IM_COL32(255, 255, 255, 200), kPieLineWidth * s);
        draw->AddLine(ImVec2(centre.x, centre.y + r), ImVec2(centre.x, centre.y + (ui_layout::kBothSides * r)),
                      IM_COL32(255, 255, 255, 200), kPieLineWidth * s);
    }
    if (on_foot() && fonts.ui) {
        const char* text = "WALK MODE  -  WASD walk, Space jump, Shift run, Esc exit";
        const ImVec2 size = fonts.ui->CalcTextSizeA(fonts.ui->FontSize, 1e9F, 0.0F, text);
        const ImVec2 at(centre.x - (size.x * 0.5F), vy + (14.0F * s));
        draw->AddRectFilled(ImVec2(at.x - (kHintBoxPaddingX * s), at.y - (kHintBoxPaddingY * s)),
                            ImVec2(at.x + size.x + (kHintBoxPaddingX * s), at.y + size.y + (kHintBoxPaddingY * s)),
                            IM_COL32(16, 17, 19, 200), kHintBoxRounding * s);
        draw->AddText(fonts.ui, fonts.ui->FontSize, at, kInk, text);
    }
    // The pie.
    const pie::State& pie = pie_now();
    if (!pie.open || pie.items.empty())
        return;
    ImFont* font = fonts.ui ? fonts.ui : ImGui::GetFont();
    ImFont* small = fonts.small ? fonts.small : font;
    const ImVec2 c(pie.x, pie.y);
    const float radius = 120.0F * s;
    const int n = static_cast<int>(pie.items.size());
    draw->AddCircleFilled(c, pie::kDeadzone * s, IM_COL32(16, 17, 19, 170));
    draw->AddCircle(c, pie::kDeadzone * s, IM_COL32(255, 255, 255, 90), 0, kPieLineWidth * s);
    // (the pointing line)
    if (pie.hovered >= 0) {
        float dx = 0.0F;
        float dy = 0.0F;
        nav::slice_direction(pie.hovered, n, &dx, &dy);
        draw->AddLine(c, ImVec2(c.x + (dx * pie::kDeadzone * s), c.y + (dy * pie::kDeadzone * s)), kLit,
                      kPieSpokeWidth * s);
    }
    for (int i = 0; i < n; ++i) {
        const pie::Item& item = core::at(pie.items, i);
        float dx = 0.0F;
        float dy = 0.0F;
        nav::slice_direction(i, n, &dx, &dy);
        const ImVec2 at(c.x + (dx * radius), c.y + (dy * radius));
        const bool enabled = !item.enabled || item.enabled();
        const int checked = item.checked ? item.checked() : -1;
        const std::string label = (checked == 1 ? "\xE2\x97\x8F " : "") + item.label; // (a dot: on)
        const ImVec2 size = font->CalcTextSizeA(font->FontSize, 1e9F, 0.0F, label.c_str());
        const ImVec2 key_size =
            item.keys.empty() ? ImVec2(0, 0) : small->CalcTextSizeA(small->FontSize, 1e9F, 0.0F, item.keys.c_str());
        const float w = std::max(size.x, key_size.x) + (18.0F * s);
        const float h = size.y + (item.keys.empty() ? 0.0F : key_size.y) + (8.0F * s);
        // (left / right of the centre: the box grows away from it)
        const float left = dx > 0.3F ? at.x - (10.0F * s) : dx < -0.3F ? at.x - w + (10.0F * s) : at.x - (w * 0.5F);
        const ImVec2 a(left, at.y - (h * 0.5F));
        const ImVec2 b(left + w, at.y + (h * 0.5F));
        const bool hot = i == pie.hovered;
        draw->AddRectFilled(a, b, hot ? IM_COL32(200, 100, 40, 235) : IM_COL32(30, 32, 36, 230), kSliceRounding * s);
        draw->AddRect(a, b, hot ? kLit : IM_COL32(90, 94, 100, 255), kSliceRounding * s);
        const ImU32 ink = enabled ? kInk : IM_COL32(130, 132, 136, 255);
        draw->AddText(font, font->FontSize, ImVec2(a.x + (kSliceTextInsetX * s), a.y + (kSliceTextInsetY * s)), ink,
                      label.c_str());
        if (!item.keys.empty())
            draw->AddText(small, small->FontSize,
                          ImVec2(a.x + (kSliceTextInsetX * s), a.y + (kSliceTextInsetY * s) + size.y),
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
        std::array<char, kSetLabelSize> label{};
        text::format_to(label, "%s  %s", name.c_str(),
                        at >= 0 ? ("(" + std::to_string(core::at(state.groups, at).members.size()) + ")").c_str()
                                : "-");
        ImGui::TextUnformatted(label.data());
        ImGui::SameLine(kSetNameColumn * s);
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
        if (group.name.starts_with("Set ") && group.name.size() == kSetNameLength && core::at(group.name, 4) >= '1' && core::at(group.name, 4) <= '9')
            continue;
        if (!any) {
            ImGui::Separator();
            gui::text_disabled("Groups");
            any = true;
        }
        ImGui::PushID(kSetIdBase + g);
        ImGui::TextUnformatted(group.name.c_str());
        ImGui::SameLine(kSetNameColumn * s);
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
