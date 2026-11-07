#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_layout.h"

struct ImFont;

// NOT ORIGINAL - the level editor as an application window (ours; see level_editor_ui.h), after Unity's and Unreal's
// editors: the game's frame is a panel among the others, not the background they float over.
//   - The Scene panel shows the game's frame, drawn into a picture of the panel's size (engine/gfx/game_view.h - the
//     engine's back buffer, viewports, layout and aspect follow the panel; nothing is stretched). The editor's
//     projections stay in the window's back-buffer pixels: the view the editor notes (level_editor::note_view) is moved
//     to where the picture is shown, so the mouse, the picking, the gizmos, the drags, the ruler and the overlays map
//     through the panel as they did through the window. The overlay is drawn in the panel (clipped to it).
//   - Docking (Dear ImGui's docking branch, on for the editor's context only - the new front end's has none): the menu
//     bar, the tool strip under it and the status bar across the bottom are fixed; the rest is a dock space - by
//     default the Outliner on the left, the Inspector on the right, the Scene in the middle and a strip under it where
//     the Problems, Trails, History, Changes and Snapshots windows dock when they open. Panels can be docked, tabbed,
//     split and floated; the layout is kept in thug2-decomp-editor.ini beside thug2-decomp.ini (ImGui's own text, and
//     the Window menu's settings), Window > Reset Layout builds the default again.
//   - The mouse: over the Scene panel (nothing over it, no popup open) the editor's camera, picking and drags have it,
//     as the viewport had; over any other panel ImGui has it. The keys go to the editor unless ImGui types.
//   - Play from here (F5) keeps the window: the game plays in the Scene panel - the other panels show that it plays
//     (or they stand aside: Window > Maximise on Play), the menu bar's Play menu (or F5) goes back to the editor - and
//     the game reads its keys and controller only while the Scene panel has the focus.
//   - The window's title names the editor and the level while the editor is open (or plays from it).
// While the editor is closed (and not playing from it) nothing here runs: the game draws to the window as before.
namespace frontend::level_editor::ui::app {

// The editor's ImGui context made (once): docking on, the layout file read.
void prepare_context();
// The layout file written when ImGui has changes for it (every frame), or now (the editor closing).
void save_layout(bool now);

// The panels the Window menu shows or hides (their tabs' close buttons too).
enum class Panel : uint8_t { kOutliner, kInspector };
bool* panel_open(Panel panel);
// The Window menu's parts - its panels, Reset Layout - and the Play menu's Maximise on Play (inside the menus);
// window_menu: both of the Window menu's.
void window_menu_panels();
void window_menu_layout();
void window_menu();
void play_menu();
bool maximised_on_play();

// The frame's dock space (the default layout first, or after Reset Layout), under the menu bar and the tool strip.
void dock_space(float scale);
// Before a dockable floating window's Begin (`name` as Begin is given it): whether the caller places it
// (SetNextWindowPos / Size) - false while it is docked (a position would undock it), or when it is docked now: the
// log-type windows (`bottom`) go to the bottom strip the first time they appear (none: they float as before).
bool place_floating(const char* name, bool bottom);

// The Scene panel: the last frame's picture where it was asked to be shown, `overlay` over it (back-buffer pixels: the
// editor's projections; none in play), and the next frame's picture asked for at the panel's size. Its content rect (in
// the window's pixels) is where the floating tools place themselves (input_routing's layout viewport).
struct SceneOverlayFonts {
    ImFont* mono = nullptr;
    float scale = 1.0F;
};
void scene_panel(const Overlay* overlay, SceneOverlayFonts fonts);
// The Scene panel's content rect in the window's pixels (zero size: not shown this frame).
ui_layout::Rect2 scene_rect();
// Whether the Scene panel had the mouse last frame (hovered, nothing over it, no item held) and the focus.
bool scene_hovered();
bool scene_focused();
// While a frame is drawn into the picture: where the picture is in the back buffer's pixels (the editor's noted view
// moves there - level_editor::note_view). False: the frame is drawn to the window (no offset).
struct PictureOrigin {
    float x{};
    float y{};
};
bool picture_origin(PictureOrigin* origin);

// The window's title: the editor and the level (`level` empty: the window's own title back). In the editor mode
// "THUG2 Editor - <level>".
void set_window_title(const std::string& level);

// The editor mode's recent levels (level_editor_shell.h), kept in the layout file: the latest first, at most 8.
struct RecentLevel {
    uint32_t load = 0; // its load script
    bool edited = false; // its edited copy
};
const std::vector<RecentLevel>& recent_levels();
void note_recent(RecentLevel level);

// --- play from here in the Scene panel (level_editor_play.h; the frame: ui::play_frame) ---
// Whether the game plays in the panel now, noted each frame; true once as it starts (the Scene panel then takes the
// focus).
bool note_playing(bool playing);
// Whether the game's keys and controller rest now: playing in the panel, the Scene panel without the focus.
bool blocks_game_input();

} // namespace frontend::level_editor::ui::app
