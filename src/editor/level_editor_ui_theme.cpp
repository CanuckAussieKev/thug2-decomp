// NOT ORIGINAL - the level editor's fonts and style (ours). See level_editor_ui_theme.h.

#include "editor/level_editor_ui_theme.h"

#include <array>
#include <span>

#include "engine/ui/ui_fonts.h"
#include "editor/level_editor_ui_layout.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"

namespace frontend::level_editor::ui_theme {

namespace {

// The theme's sizes (at scale 1; ScaleAllSizes applies the UI scale).
constexpr float kCornerRounding = 3.0F;
constexpr ImVec2 kWindowPadding{10.0F, 8.0F};
constexpr ImVec2 kFramePadding{7.0F, 4.0F};
constexpr ImVec2 kItemSpacing{7.0F, 5.0F};
constexpr float kIndentSpacing = 14.0F;
constexpr float kScrollbarSize = 12.0F;
constexpr ImVec2 kBarMinimumWindowSize{4.0F, 4.0F}; // the bars are thinner than ImGui's default minimum
// The theme's colours: a dark neutral (the accent colour is the user's).
constexpr ImVec4 kWindowBgColour{0.125F, 0.13F, 0.14F, 1.0F}; // (opaque: panels of a window, not over the game)
constexpr ImVec4 kPopupBgColour{0.12F, 0.125F, 0.135F, 0.98F};
constexpr ImVec4 kMenuBarBgColour{0.085F, 0.09F, 0.095F, 1.0F};
constexpr ImVec4 kBorderColour{0.25F, 0.26F, 0.28F, 1.0F};
constexpr ImVec4 kFrameBgColour{0.18F, 0.19F, 0.2F, 1.0F};
constexpr ImVec4 kFrameBgHoveredColour{0.24F, 0.25F, 0.27F, 1.0F};
constexpr ImVec4 kFrameBgActiveColour{0.28F, 0.29F, 0.31F, 1.0F};
constexpr ImVec4 kTitleBgColour{0.09F, 0.095F, 0.1F, 1.0F};
constexpr ImVec4 kButtonColour{0.2F, 0.21F, 0.225F, 1.0F};
constexpr ImVec4 kButtonHoveredColour{0.28F, 0.29F, 0.31F, 1.0F};
constexpr ImVec4 kTextColour{0.91F, 0.9F, 0.88F, 1.0F};
constexpr ImVec4 kTextDisabledColour{0.6F, 0.61F, 0.63F, 1.0F};
constexpr ImVec4 kDockEmptyColour{0.07F, 0.075F, 0.08F, 1.0F};
constexpr float kDockPreviewAlpha = 0.4F; // the accent where a dragged panel would dock
constexpr float kTabOverline = 2.0F;      // the selected tab's accent line
constexpr float kDockSeparator = 3.0F;    // the splitters between docked panels
constexpr float kHeaderAlpha = 0.35F;        // the accent colour's alpha on a header
constexpr float kHeaderHoveredAlpha = 0.45F; // and on a hovered header

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
    style.WindowRounding = 0.0F;
    style.ChildRounding = kCornerRounding;
    style.FrameRounding = kCornerRounding;
    style.PopupRounding = kCornerRounding;
    style.GrabRounding = kCornerRounding;
    style.TabRounding = kCornerRounding;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.WindowPadding = kWindowPadding;
    style.FramePadding = kFramePadding;
    style.ItemSpacing = kItemSpacing;
    style.IndentSpacing = kIndentSpacing;
    style.ScrollbarSize = kScrollbarSize;
    // A dark neutral with the accent.
    const std::span<ImVec4, ImGuiCol_COUNT> colours(style.Colors);
    const ImVec4 accent = accent_at(1.0F);
    const ImVec4 accent_soft = accent_at(0.55F);
    core::at(colours, ImGuiCol_WindowBg) = kWindowBgColour;
    core::at(colours, ImGuiCol_ChildBg) = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    core::at(colours, ImGuiCol_PopupBg) = kPopupBgColour;
    core::at(colours, ImGuiCol_MenuBarBg) = kMenuBarBgColour;
    core::at(colours, ImGuiCol_Border) = kBorderColour;
    core::at(colours, ImGuiCol_FrameBg) = kFrameBgColour;
    core::at(colours, ImGuiCol_FrameBgHovered) = kFrameBgHoveredColour;
    core::at(colours, ImGuiCol_FrameBgActive) = kFrameBgActiveColour;
    core::at(colours, ImGuiCol_TitleBg) = core::at(colours, ImGuiCol_TitleBgActive) = kTitleBgColour;
    core::at(colours, ImGuiCol_Button) = kButtonColour;
    core::at(colours, ImGuiCol_ButtonHovered) = kButtonHoveredColour;
    core::at(colours, ImGuiCol_ButtonActive) = accent_soft;
    core::at(colours, ImGuiCol_Header) = accent_at(kHeaderAlpha);
    core::at(colours, ImGuiCol_HeaderHovered) = accent_at(kHeaderHoveredAlpha);
    core::at(colours, ImGuiCol_HeaderActive) = accent_soft;
    core::at(colours, ImGuiCol_CheckMark) = accent;
    core::at(colours, ImGuiCol_SliderGrab) = accent;
    core::at(colours, ImGuiCol_SliderGrabActive) = accent;
    core::at(colours, ImGuiCol_Separator) = core::at(colours, ImGuiCol_Border);
    core::at(colours, ImGuiCol_Text) = kTextColour;
    core::at(colours, ImGuiCol_TextDisabled) = kTextDisabledColour;
    core::at(colours, ImGuiCol_NavCursor) = accent;
    // An application's window (level_editor_ui_app.h): opaque panels, tabs as the panels' title bars, the dock
    // space's splitters and drop targets in the accent.
    core::at(colours, ImGuiCol_Tab) = kTitleBgColour;
    core::at(colours, ImGuiCol_TabHovered) = kButtonHoveredColour;
    core::at(colours, ImGuiCol_TabSelected) = kWindowBgColour;
    core::at(colours, ImGuiCol_TabSelectedOverline) = accent;
    core::at(colours, ImGuiCol_TabDimmed) = kTitleBgColour;
    core::at(colours, ImGuiCol_TabDimmedSelected) = kWindowBgColour;
    core::at(colours, ImGuiCol_TabDimmedSelectedOverline) = kBorderColour;
    core::at(colours, ImGuiCol_DockingPreview) = accent_at(kDockPreviewAlpha);
    core::at(colours, ImGuiCol_DockingEmptyBg) = kDockEmptyColour;
    core::at(colours, ImGuiCol_SeparatorHovered) = accent_soft;
    core::at(colours, ImGuiCol_SeparatorActive) = accent;
    core::at(colours, ImGuiCol_ResizeGrip) = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 1.0F;
    style.TabBarOverlineSize = kTabOverline;
    style.DockingSeparatorSize = kDockSeparator;
    style.WindowMenuButtonPosition = ImGuiDir_None; // (no collapse arrow on the tabs)
    style.ScaleAllSizes(scale);
    // The bars are thinner than ImGui's default minimum window size (32 x 32, scaled): at it the menu bar's window grew
    // down over the tool strip.
    style.WindowMinSize = kBarMinimumWindowSize;
    ImGui::GetStyle() = style;
}

} // namespace frontend::level_editor::ui_theme
