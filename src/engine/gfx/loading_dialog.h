#pragma once

#include <cstdint>
#include <string>

#include "base/core/api.h"

struct ImDrawList;
struct ImFont;

// NOT ORIGINAL - the editor mode's loading dialog (ours, at the user's request: "things like the loading screens dont
// need to show anymore. We could just show a 'Loading' dialog or something").
//
// In the editor mode (engine/sys/editor_mode.h) a level's loading screen - the level select picture, the loading bar,
// the loading console's panel - is never shown: the window keeps the editor's dark page with a box in its middle, as
// the editor's own windows look (Dear ImGui's draw lists, the editor's fonts, colours and sizes):
//   +- Open Level ----------------------------------+
//   |  Loading Boston...                            |
//   |  [=============                        ]      |
//   |  load bo_scn.pre  2.1 MB              4.2 s   |
//   +-----------------------------------------------+
// The bar is the loading bar's (it fills with the time the level's script gave it), or a sweep when there is none;
// the line under it is the loading console's newest (engine/gfx/loading_console.h: what is loading now - nothing while
// [features] loading_console=0), in the warning colour after 10 s without any. The same box is drawn by the editor
// before and after the load (level_editor_shell.h: closing the editor, opening it in the level), so it stays in place
// from the level chosen to the editor up.
//
// The loading screen is drawn by a timer while the game loads on the main thread (render_pc/loading_screen.cpp); so
// is the box (render_pc/loading_dialog_panel.h): built in a draw list of its own without an ImGui context, with fonts
// of its own made before the load. Outside the editor mode nothing here runs: the loading screens are the game's.
namespace gfx::loading_dialog {

// The editor's look at scale 1 (until the editor gives its own: set_look).
namespace look_defaults {
constexpr uint32_t kPage = 0xFF171413U;
constexpr uint32_t kWindow = 0xFF242120U;
constexpr uint32_t kBorder = 0xFF474240U;
constexpr uint32_t kTitleBar = 0xFF1A1817U;
constexpr uint32_t kText = 0xFFE0E6E8U;
constexpr uint32_t kTextDim = 0xFFA19C99U;
constexpr uint32_t kFrame = 0xFF33302EU;
constexpr uint32_t kAccent = 0xFF2E9AFFU;
constexpr uint32_t kWarning = 0xFF73C7FFU;
constexpr float kUiPixels = 14.0F;
constexpr float kHeadingPixels = 18.0F;
constexpr float kWidth = 440.0F;
constexpr float kPaddingX = 10.0F;
constexpr float kPaddingY = 8.0F;
constexpr float kFramePaddingX = 7.0F;
constexpr float kFramePaddingY = 4.0F;
constexpr float kSpacingY = 5.0F;
constexpr float kRounding = 3.0F;
} // namespace look_defaults

// The editor's look (its style and fonts at its UI scale - level_editor_ui_theme.h). Colours are ImGui's (IM_COL32:
// red in the low byte).
struct Look {
    uint32_t page = look_defaults::kPage;           // the window's background around the box
    uint32_t window = look_defaults::kWindow;       // the box
    uint32_t border = look_defaults::kBorder;
    uint32_t title_bar = look_defaults::kTitleBar;
    uint32_t text = look_defaults::kText;
    uint32_t text_dim = look_defaults::kTextDim;
    uint32_t frame = look_defaults::kFrame;         // the bar's track
    uint32_t accent = look_defaults::kAccent;       // the bar's fill
    uint32_t warning = look_defaults::kWarning;     // the line after 10 s without activity
    float ui_pixels = look_defaults::kUiPixels;     // Inter, the UI's size
    float heading_pixels = look_defaults::kHeadingPixels; // Inter SemiBold, the headings'
    float width = look_defaults::kWidth;            // the box's width
    float padding_x = look_defaults::kPaddingX;     // the window's padding
    float padding_y = look_defaults::kPaddingY;
    float frame_padding_x = look_defaults::kFramePaddingX;
    float frame_padding_y = look_defaults::kFramePaddingY;
    float spacing_y = look_defaults::kSpacingY;     // between lines
    float rounding = look_defaults::kRounding;      // the bar's corners
};

// What the box says.
struct BoxText {
    std::string caption; // the title bar's ("Open Level")
    std::string heading; // "Loading Boston..."
    std::string status;  // the line under the bar (left)
    std::string time;    // its right end ("4.2 s"; "" none)
    float fraction = 0.0F; // the bar: 0..1, or below 0 a sweep at that phase (-seconds, as ImGui's ProgressBar)
    bool stalled = false;  // the status line in the warning colour
};

struct BoxFonts {
    ImFont* ui = nullptr;
    ImFont* heading = nullptr;
};

// The box drawn into `list` centred on a display of `display_width` x `display_height` pixels (from `origin_x` /
// `origin_y`). The list's texture must be the fonts' atlas. Uses no ImGui context: the editor draws it in its window,
// the loading screen's timer in a list of its own.
struct Display {
    float origin_x = 0.0F;
    float origin_y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};
THUG2_API void draw_box(ImDrawList* list, const Look& look, const BoxFonts& fonts, const BoxText& text,
                        const Display& display);

// The editor's look and the level being opened (the editor sets them before a load: level_editor_shell.cpp).
THUG2_API void set_look(const Look& look);
THUG2_API void set_title(const std::string& title);
Look look();
std::string title();

// Whether a loading screen shows the box instead (the editor mode).
bool replaces_loading_screens();

} // namespace gfx::loading_dialog
