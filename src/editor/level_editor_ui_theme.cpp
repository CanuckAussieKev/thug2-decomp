// NOT ORIGINAL - the level editor's fonts and style (ours). See level_editor_ui_theme.h.

#include "editor/level_editor_ui_theme.h"

#include <array>
#include <span>

#include "editor/level_editor_ui_fonts.h"
#include "editor/level_editor_ui_layout.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace frontend::level_editor::ui_theme {

namespace {

// The theme's sizes (at scale 1; ScaleAllSizes applies the UI scale).
constexpr float kCornerRounding = 3.0f;
constexpr ImVec2 kWindowPadding{10.0f, 8.0f};
constexpr ImVec2 kFramePadding{7.0f, 4.0f};
constexpr ImVec2 kItemSpacing{7.0f, 5.0f};
constexpr float kIndentSpacing = 14.0f;
constexpr float kScrollbarSize = 12.0f;
constexpr ImVec2 kBarMinimumWindowSize{4.0f, 4.0f}; // the bars are thinner than ImGui's default minimum
// The theme's colours: a dark neutral (the accent colour is the user's).
constexpr ImVec4 kWindowBgColour{0.11f, 0.115f, 0.125f, 0.94f};
constexpr ImVec4 kPopupBgColour{0.12f, 0.125f, 0.135f, 0.98f};
constexpr ImVec4 kMenuBarBgColour{0.085f, 0.09f, 0.095f, 1.0f};
constexpr ImVec4 kBorderColour{0.25f, 0.26f, 0.28f, 1.0f};
constexpr ImVec4 kFrameBgColour{0.18f, 0.19f, 0.2f, 1.0f};
constexpr ImVec4 kFrameBgHoveredColour{0.24f, 0.25f, 0.27f, 1.0f};
constexpr ImVec4 kFrameBgActiveColour{0.28f, 0.29f, 0.31f, 1.0f};
constexpr ImVec4 kTitleBgColour{0.09f, 0.095f, 0.1f, 1.0f};
constexpr ImVec4 kButtonColour{0.2f, 0.21f, 0.225f, 1.0f};
constexpr ImVec4 kButtonHoveredColour{0.28f, 0.29f, 0.31f, 1.0f};
constexpr ImVec4 kTextColour{0.91f, 0.9f, 0.88f, 1.0f};
constexpr ImVec4 kTextDisabledColour{0.6f, 0.61f, 0.63f, 1.0f};
constexpr float kHeaderAlpha = 0.35f; // the accent colour's alpha on a header
constexpr float kHeaderHoveredAlpha = 0.45f; // and on a hovered header

// The characters the editor shows: Latin-1, dashes and bullets, arrows and triangles.
constexpr auto kRanges = std::to_array<ImWchar>({0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0x25B2, 0x25C0, 0});

ImFont* add_font(ui_fonts::CompressedFont font, float size)
{
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    text::format_to(config.Name, "%.0fpx", size);
    return ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(font.data, font.size, size, &config, kRanges.data());
}

} // namespace

Fonts build_fonts(float scale)
{
    const ui_layout::FontSizes sizes = ui_layout::font_sizes_for(scale);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    Fonts fonts;
    fonts.ui = add_font(ui_fonts::inter(), sizes.ui);
    fonts.small = add_font(ui_fonts::inter(), sizes.small);
    fonts.mono = add_font(ui_fonts::get_mono(), sizes.mono);
    fonts.heading = add_font(ui_fonts::inter_semi_bold(), sizes.heading);
    atlas->Build();
    return fonts;
}

void apply_style(float scale, std::span<const float, 3> accent_rgb)
{
    auto accent_at = [accent_rgb](float alpha) { return ImVec4(core::at(accent_rgb, 0), core::at(accent_rgb, 1), core::at(accent_rgb, 2), alpha); };
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0.0f;
    style.ChildRounding = kCornerRounding;
    style.FrameRounding = kCornerRounding;
    style.PopupRounding = kCornerRounding;
    style.GrabRounding = kCornerRounding;
    style.TabRounding = kCornerRounding;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = kWindowPadding;
    style.FramePadding = kFramePadding;
    style.ItemSpacing = kItemSpacing;
    style.IndentSpacing = kIndentSpacing;
    style.ScrollbarSize = kScrollbarSize;
    // A dark neutral with the accent.
    const std::span<ImVec4, ImGuiCol_COUNT> c(style.Colors);
    const ImVec4 accent = accent_at(1.0f);
    const ImVec4 accent_soft = accent_at(0.55f);
    core::at(c, ImGuiCol_WindowBg) = kWindowBgColour;
    core::at(c, ImGuiCol_ChildBg) = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    core::at(c, ImGuiCol_PopupBg) = kPopupBgColour;
    core::at(c, ImGuiCol_MenuBarBg) = kMenuBarBgColour;
    core::at(c, ImGuiCol_Border) = kBorderColour;
    core::at(c, ImGuiCol_FrameBg) = kFrameBgColour;
    core::at(c, ImGuiCol_FrameBgHovered) = kFrameBgHoveredColour;
    core::at(c, ImGuiCol_FrameBgActive) = kFrameBgActiveColour;
    core::at(c, ImGuiCol_TitleBg) = core::at(c, ImGuiCol_TitleBgActive) = kTitleBgColour;
    core::at(c, ImGuiCol_Button) = kButtonColour;
    core::at(c, ImGuiCol_ButtonHovered) = kButtonHoveredColour;
    core::at(c, ImGuiCol_ButtonActive) = accent_soft;
    core::at(c, ImGuiCol_Header) = accent_at(kHeaderAlpha);
    core::at(c, ImGuiCol_HeaderHovered) = accent_at(kHeaderHoveredAlpha);
    core::at(c, ImGuiCol_HeaderActive) = accent_soft;
    core::at(c, ImGuiCol_CheckMark) = accent;
    core::at(c, ImGuiCol_SliderGrab) = accent;
    core::at(c, ImGuiCol_SliderGrabActive) = accent;
    core::at(c, ImGuiCol_Separator) = core::at(c, ImGuiCol_Border);
    core::at(c, ImGuiCol_Text) = kTextColour;
    core::at(c, ImGuiCol_TextDisabled) = kTextDisabledColour;
    core::at(c, ImGuiCol_NavCursor) = accent;
    style.ScaleAllSizes(scale);
    // The bars are thinner than ImGui's default minimum window size (32 x 32, scaled): at it the menu bar's window grew
    // down over the tool strip.
    style.WindowMinSize = kBarMinimumWindowSize;
    ImGui::GetStyle() = style;
}

} // namespace frontend::level_editor::ui_theme
