#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor.h"

namespace frontend::level_editor::ui {
struct ShapeMesh;
}
namespace frontend::ui_backend {
struct Picture;
}

// NOT ORIGINAL - the level editor's Content Browser (ours, at the user's request: one asset browser, as Unreal's
// Content Browser / Unity's Project window / Godot's FileSystem dock).
//
// A dockable panel (docked along the bottom by default; Window / View > Content Browser, Ctrl+Shift+B; B opens it on
// This Level). Left: a folder tree - This Level (the level's own pieces and objects: the old Add Piece palette - its
// thumbnails, Recent / Favourites and keys, Enter places a copy at the cursor; a click in the Scene
// selects, as anywhere else - it doesn't place), Levels > <level> > Scenery / Rails (every
// THUG2 level's pieces from the catalogue), Prop Packs (the packs already cut out), Recent, Favorites. Top: the path,
// a search box (Ctrl+F) with type and level filters, the tile size and Grid / List. Right: the tiles (a thumbnail -
// drawn once by the pipeline, a placeholder until it is there -, the name, a type badge) or a table (name, level,
// triangles, textures, rails). Hover: the details; click selects; double click previews; right click: Place at Cursor,
// Add to Favorites, Show Source Level, Copy Name. Dragging a tile into the Scene places it where it is dropped (a box
// of its size follows the ground under the cursor; Esc cancels).
//
// Another level's piece is placed as a prop pack (level_editor_props.h): cut out of its level by the content pipeline
// (prop_packs/prop_packs.ts extract, run with the editor's Node runtime as Import Level runs the converters -
// level_editor_import.h) the first time, then loaded and placed; the pack is kept, so the next drop is at once.
// The catalogue (prop_packs.ts catalogue: every level's pieces) is made the first time the browser opens and again
// when a level's scene changed; the pictures (prop_packs.ts thumbs) when a level's folder is first shown. Both are in
// <output>\thug2-decomp-props (import::output_folder), the pictures also in the overlay (images\props\<level>\).
namespace frontend::level_editor::content {

// --- the pure parts -------------------------------------------------------------------------------------------------

struct Piece {
    std::string level; // its level's folder (LA)
    std::string name;  // its sector's name (Fire_Tender)
    uint32_t checksum = 0;
    int triangles = 0;
    int textures = 0;
    int rails = 0;
    V3 min{}, max{};
};
// The catalogue's text (prop_packs.ts catalogue): its pieces, by level then name. False: not a catalogue.
bool read_catalogue(const std::string& text, std::vector<Piece>* pieces);
// A piece's key in Recent / Favorites ("LA/Fire_Tender") and its pack's name (as prop_packs/pack.ts packId).
std::string piece_key(const Piece& piece);
std::string pack_id(const std::string& level, const std::string& name);
// Whether a piece matches the search (any case, in its name or its level) and the type filter (0 all, 1 without
// rails, 2 with rails).
bool matches(const Piece& piece, const std::string& search, int type);

// --- the game side ------------------------------------------------------------------------------------------------

// The keys (Ctrl+Shift+B, Ctrl+F); once.
void prepare_all();
// Once a frame from the editor's Update: the keys, the pipeline's runs, a drop waiting for its pack.
void tick_frame();
// B: the browser open on This Level, focused.
void show_this_level();
// Whether the browser shows This Level now (the Add Piece palette is inside it, not a window of its own).
bool hosts_this_level();
// Whether the browser's window (or one of its children) has the keyboard focus - drawn last frame.
bool has_focus();
// Whether the panel is open (Window / View > Content Browser).
bool* open_flag();
// The automation channel's `fe editor prop <level> <piece>`: that piece placed at the editor's cursor point (cut out
// first if need be). "ok ..." or "error: ...".
std::string automation_place(const std::string& level, const std::string& name);
// The switch flipped by a menu: kept in the ini, the panel focused when it opened.
void note_toggled();
// The panel (and its drag's feedback, the preview window), from the UI's frame.
void panel(const ui::ShapeMesh& model, float scale);
// For the Prefabs folder (level_editor_prefabs.h): a run of the content tools queued (`kind` prop_packs.ts's command,
// `label` for the status line), a picture of the overlay's images by name (nullptr: not there yet - looked for again
// after the next run), one forgotten (loaded again: drawn again).
// A pack cut out again now, waiting for the content tools (at most `kCutAgainSeconds`) - for a layout's pack cut by an
// older pipeline (props::kManifestVersion), as its level loads. False (with `why`) when the tools aren't there or the
// run failed; the old pack then stays.
inline constexpr int kCutAgainSeconds = 120;
bool cut_again_now(const std::string& level, const std::string& piece, std::string* why);
void run_tool(const std::string& kind, const std::vector<std::string>& arguments, const std::string& label);
const ui_backend::Picture* picture(const std::string& name);
void forget_picture(const std::string& name);

} // namespace frontend::level_editor::content
