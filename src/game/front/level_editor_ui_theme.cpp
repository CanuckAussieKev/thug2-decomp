// NOT ORIGINAL - the level editor's fonts and style (ours). See level_editor_ui_theme.h.

#include "game/front/level_editor_ui_theme.h"

#include <cstdio>

#include "game/front/level_editor_ui_fonts.h"
#include "game/front/level_editor_ui_layout.h"
#include "imgui.h"

namespace Front::LevelEditor::UiTheme {

namespace {

// The characters the editor shows: Latin-1, dashes and bullets, arrows and triangles.
const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0x25B2, 0x25C0, 0};

ImFont* AddFont(UiFonts::CompressedFont font, float size)
{
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::snprintf(config.Name, sizeof(config.Name), "%.0fpx", size);
    return ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(font.data, font.size, size, &config, kRanges);
}

} // namespace

Fonts BuildFonts(float scale)
{
    UiLayout::FontSizes sizes = UiLayout::FontSizesFor(scale);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    Fonts fonts;
    fonts.ui = AddFont(UiFonts::Inter(), sizes.ui);
    fonts.small = AddFont(UiFonts::Inter(), sizes.small);
    fonts.mono = AddFont(UiFonts::Mono(), sizes.mono);
    fonts.heading = AddFont(UiFonts::InterSemiBold(), sizes.heading);
    atlas->Build();
    return fonts;
}

void ApplyStyle(float scale, const float accentRgb[3])
{
    auto accentAt = [accentRgb](float alpha) { return ImVec4(accentRgb[0], accentRgb[1], accentRgb[2], alpha); };
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
    ImVec4* c = style.Colors;
    const ImVec4 accent = accentAt(1.0f), accentSoft = accentAt(0.55f);
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.115f, 0.125f, 0.94f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.135f, 0.98f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.085f, 0.09f, 0.095f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.25f, 0.26f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.2f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.25f, 0.27f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.29f, 0.31f, 1.0f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.095f, 0.1f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.2f, 0.21f, 0.225f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.29f, 0.31f, 1.0f);
    c[ImGuiCol_ButtonActive] = accentSoft;
    c[ImGuiCol_Header] = accentAt(0.35f);
    c[ImGuiCol_HeaderHovered] = accentAt(0.45f);
    c[ImGuiCol_HeaderActive] = accentSoft;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_Text] = ImVec4(0.91f, 0.9f, 0.88f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.6f, 0.61f, 0.63f, 1.0f);
    c[ImGuiCol_NavCursor] = accent;
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}

} // namespace Front::LevelEditor::UiTheme
