#pragma once

#include <cstdint>
#include <span>

struct ImFont;

// NOT ORIGINAL - the level editor's preferences (ours; see level_editor.h): a window (the toolbar's Prefs, Ctrl+,) and
// what it keeps in thug2-decomp.ini's [editor] section, read when the editor starts and written when a value changes.
// The ini keeps integers, so fractions are kept in hundredths (percent):
//   fly_speed=800          the camera's flying speed when the editor opens (units a second; - / = and the right
//                          button + wheel change it while flying)
//   fly_accel_ms=0         the time flying takes to reach that speed (0: at once, as before)
//   look_pct=100           mouse look sensitivity (100: 0.0035 radians a pixel)
//   invert_y=0             mouse look's up / down inverted
//   gizmo_pct=100          the move arrows' length and the gizmos' size
//   ui_scale_pct=0         the UI's scale (0: automatic - the window's size, level_editor_ui_layout.h)
//   autosave_s=60          the autosave's interval (level_editor_layout.h)
//   snap_move=25           the move snap (Ctrl / the toolbar's Snap) in units
//   snap_turn=15           Z / X's turn for objects and nodes, in degrees
//   accent=0               the UI's accent colour (kAccentCount colours: orange, blue, green, purple, red, teal)
//   wasd_flies=0           W A S D always fly the camera, even with a selection (the transform work's setting -
//                          UX2's; this window shows it)
//   speed_by_selection=0   F (frame the selection) sets the flying speed from the selection's size (Unreal's)
//   tips_seen=0            the first-run tips were dismissed (the help's "Show tips" shows them again)
namespace frontend::level_editor::prefs {

struct Values {
    int fly_speed_ = 800;
    int fly_accel_ms_ = 0;
    int look_pct_ = 100;
    bool invert_y_ = false;
    int gizmo_pct_ = 100;
    int ui_scale_pct_ = 0;
    int autosave_seconds_ = 60;
    int snap_move_ = 25;
    int snap_turn_ = 15;
    int accent = 0;
    bool wasd_flies_ = false;
    bool speed_by_selection_ = false;
    bool tips_seen_ = false;
};

// The limits (pure): every value brought within its range (fly speed 50..20000, accel 0..3000 ms, look 10..500%,
// gizmo 25..400%, UI scale 0 or 75..400%, autosave 10..3600 s, snap move 1..1000, snap turn 1..90, accent
// 0..kAccentCount-1).
Values get_clamped(Values v);
constexpr int kAccentCount = 6;
// An accent's colour (pure): r, g, b in 0..1; its name.
void get_accent_rgb(int accent, std::span<float, 3> rgb);
const char* accent_name(int accent);

// The values now (read from the ini once, the first time).
const Values& get_pull();
// Changed: kept (and written to the ini unless `save` is false - self-tests).
void assign_value(const Values& values, bool save = true);
// Counts each change of the UI's look (scale, accent): the UI rebuilds its fonts / style when it moves.
int style_generation();

// The window (the toolbar's Prefs, Ctrl+,): open / closed, and drawn by the UI's frame (level_editor_ui.cpp) with
// its scale and fonts.
bool& unseal();
void pane(float scale, ImFont* heading, ImFont* small);

// The first-run tips: a short overlay the first time the editor opens (until dismissed: tips_seen=1), and again from
// the help's "Show tips".
void show_tips();
void tips_window(float scale, ImFont* heading, ImFont* mono);

} // namespace frontend::level_editor::prefs
