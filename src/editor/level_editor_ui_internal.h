#pragma once

// NOT ORIGINAL - what level_editor_ui.cpp's pieces share (the panels' state, the colours, the widgets and the helpers
// more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, and each piece
// that includes this has a using-directive for them (in the piece, not here: SF.7), so it sees them as it saw them
// there.

#include "editor/level_editor_ui.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include "editor/level_editor.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_notes.h"
#include "base/core/format.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "imgui.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::ui::internal {

// Layout, at scale 1 unless said (the UI scale multiplies them): the Outliner, Inspector, status bar, help, browser and dialogs.
constexpr std::size_t kSearchTextSize = 96; // a search field's text

constexpr float kPanelInset = 8.0F;     // a floating panel's gap from the viewport's corner

constexpr float kSwatchRounding = 2.0F;         // colour swatches (legend, Outliner and Place rows)

constexpr float kBarPaddingX = 8.0F; // the menu and status bars' window padding

constexpr float kBarPaddingY = 4.0F;

constexpr ImVec4 kDangerButtonColour{0.29F, 0.14F, 0.13F, 1.0F};

// NOT ORIGINAL grouping - level_editor_ui.cpp's state (were globals), one record per part (the Outliner's filter and the
// Inspector's scale lock are their pieces' own). Each made on first use, never destroyed.

// ImGui and the style (imgui_setup()): the context, the run, the fonts and the scale they and the style were built at.
struct ImguiSetup {
    bool created_{}; // (the ImGui context, made once)
    bool running_{}; // (between Start and Stop)
    float scale_{}; // (the fonts' and the style's scale; 0: not built)
    ImFont* ui_{};
    ImFont* small_{};
    ImFont* mono_{};
    ImFont* heading_{};
    int style_generation_ = -1; // (the preferences' scale / accent the style was built for)
};

ImguiSetup& imgui_setup();

// The routing (level_editor_ui_layout.h - input_routing()): the last frame's layout and ImGui's wishes, the mouse, which
// mouse buttons a press gave to the UI / to the editor (their release goes the same way), and the UI's requests to the
// editor.
struct InputRouting {
    ui_layout::Layout layout_{};
    bool want_mouse_{};
    bool want_keys_{};
    float mouse_x_ = -1.0F;
    float mouse_y_ = -1.0F;
    uint32_t ui_buttons_{};
    uint32_t editor_buttons_{};
    std::vector<Request> requests_;
};

InputRouting& input_routing();

// The panels (panel_state()): the autosave offer closed; the browser's search and the row shown, and the preview's
// entry - the list's row under the mouse (-1: none) and whether the keys moved the highlight since the mouse last moved
// onto a row (then the highlighted one shows); the entry asked for last.
struct PanelState {
    bool offer_closed_{};
    std::array<char, kSearchTextSize> browser_search_{};
    int browser_shown_index_ = -1;
    int browser_hovered_ = -1;
    bool browser_keys_won_{};
    int preview_asked_ = -1;
};

PanelState& panel_state();

void get_ask(Request::Kind kind, int value = 0, uint32_t id = 0);

ImVec4 get_accent(float alpha);

ImU32 get_colour(uint32_t argb);

// --- small pieces ---

template <typename... Args>
void get_mono(text::FormatFor<Args...> format, Args... args)
{
    ImGui::PushFont(imgui_setup().mono_);
    ImGui::TextUnformatted(text::format(format, args...).c_str());
    ImGui::PopFont();
}

void get_muted(const char* text);

ux8::Fonts ux8_fonts();
// The UI's fonts and scale, as ux3 keeps them.
ux3::Fonts ux3_fonts();

bool get_matches(const std::string& text, const char* filter);

constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

void place_window(const ui_layout::Rect2& rect);

void panel_title(const char* title);

void view_section(const ShapeMesh& model);

void get_outliner(const ShapeMesh& model);

void get_inspector(const ShapeMesh& model);

void status_bar(const ShapeMesh& model);

void get_help(const ShapeMesh& model);

void get_browser(const ShapeMesh& model);

void autosave_offer(const ShapeMesh& model);

void online_warning(const ShapeMesh& model);

void get_place(const ShapeMesh& model);

void confirm_reset(const ShapeMesh& model);

void confirm_hide(const ShapeMesh& model);

} // namespace frontend::level_editor::ui::internal
