// NOT ORIGINAL - the level editor's fonts and style (ours). See level_editor_ui_theme.h.

#include "editor/level_editor_ui_theme.h"

#include <cstdio>
#include <array>
#include <iterator>
#include <span>

#include "editor/level_editor_ui_fonts.h"
#include "editor/level_editor_ui_layout.h"
#include "imgui.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::ui_theme {

namespace {

// The characters the editor shows: Latin-1, dashes and bullets, arrows and triangles.
constexpr auto kRanges = std::to_array<ImWchar>({0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0x25B2, 0x25C0, 0});

ImFont* add_font(ui_fonts::CompressedFont font, float size)
{
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::snprintf(std::data(config.Name), sizeof(config.Name), "%.0fpx", size);
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
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 12.0f;
    // A dark neutral with the accent.
    const std::span<ImVec4, ImGuiCol_COUNT> c(style.Colors);
    const ImVec4 accent = accent_at(1.0f), accent_soft = accent_at(0.55f);
    core::at(c, ImGuiCol_WindowBg) = ImVec4(0.11f, 0.115f, 0.125f, 0.94f);
    core::at(c, ImGuiCol_ChildBg) = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    core::at(c, ImGuiCol_PopupBg) = ImVec4(0.12f, 0.125f, 0.135f, 0.98f);
    core::at(c, ImGuiCol_MenuBarBg) = ImVec4(0.085f, 0.09f, 0.095f, 1.0f);
    core::at(c, ImGuiCol_Border) = ImVec4(0.25f, 0.26f, 0.28f, 1.0f);
    core::at(c, ImGuiCol_FrameBg) = ImVec4(0.18f, 0.19f, 0.2f, 1.0f);
    core::at(c, ImGuiCol_FrameBgHovered) = ImVec4(0.24f, 0.25f, 0.27f, 1.0f);
    core::at(c, ImGuiCol_FrameBgActive) = ImVec4(0.28f, 0.29f, 0.31f, 1.0f);
    core::at(c, ImGuiCol_TitleBg) = core::at(c, ImGuiCol_TitleBgActive) = ImVec4(0.09f, 0.095f, 0.1f, 1.0f);
    core::at(c, ImGuiCol_Button) = ImVec4(0.2f, 0.21f, 0.225f, 1.0f);
    core::at(c, ImGuiCol_ButtonHovered) = ImVec4(0.28f, 0.29f, 0.31f, 1.0f);
    core::at(c, ImGuiCol_ButtonActive) = accent_soft;
    core::at(c, ImGuiCol_Header) = accent_at(0.35f);
    core::at(c, ImGuiCol_HeaderHovered) = accent_at(0.45f);
    core::at(c, ImGuiCol_HeaderActive) = accent_soft;
    core::at(c, ImGuiCol_CheckMark) = accent;
    core::at(c, ImGuiCol_SliderGrab) = accent;
    core::at(c, ImGuiCol_SliderGrabActive) = accent;
    core::at(c, ImGuiCol_Separator) = core::at(c, ImGuiCol_Border);
    core::at(c, ImGuiCol_Text) = ImVec4(0.91f, 0.9f, 0.88f, 1.0f);
    core::at(c, ImGuiCol_TextDisabled) = ImVec4(0.6f, 0.61f, 0.63f, 1.0f);
    core::at(c, ImGuiCol_NavCursor) = accent;
    style.ScaleAllSizes(scale);
    // The bars are thinner than ImGui's default minimum window size (32 x 32, scaled): at it the menu bar's window grew
    // down over the tool strip.
    style.WindowMinSize = ImVec2(4.0f, 4.0f);
    ImGui::GetStyle() = style;
}

} // namespace frontend::level_editor::ui_theme
