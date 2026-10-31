#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor_ui.h"

struct ImFont;

// NOT ORIGINAL - the Add palette's (B) thumbnails, Recently used and Favourites (ours; see level_editor.h and
// level_editor_preview.h):
//   - Grid / List (the palette's toggle; [editor] palette_grid= in thug2-decomp.ini): Grid shows each entry as a small
//     picture of the piece or object, drawn by the preview's own off-screen render (Preview::draw_view: the game's
//     meshes and instances, the default angle) into the cells of one atlas texture (ui_backend::begin_atlas_cell -
//     kAtlasPixels square, kCellPixels a cell: kCells pictures kept, the least recently shown given up first). Only
//     the entries the grid shows are drawn, kPerFrame a frame, in level_editor::Blit (after the game's frame, before
//     the UI's), as the preview is. The atlas is freed with the device objects (alt-tab, a new window size) and when
//     the editor closes; its pictures are drawn again as they're shown.
//   - The filter (the palette's search box) searches the names in both modes.
//   - Recently used: the last kRecent entries placed (Enter, Place, a double click), newest first; Favourites: the
//     entries starred (the star on a thumbnail or a row). Both per level (the scene's name), kept in
//     thug2-decomp.ini's [editor_palette] as <scene>_recent= / <scene>_favourites= (s:<sector checksum> /
//     o:<object id> in hex, comma separated). The chips All / Recent / Favourites choose what the palette lists.
namespace frontend::level_editor::thumbs {

constexpr int kAtlasPixels = 2048;
constexpr int kCellPixels = 128;
constexpr int kCells = (kAtlasPixels / kCellPixels) * (kAtlasPixels / kCellPixels);
constexpr int kPerFrame = 3;
constexpr size_t kRecent = 20;

// --- the pieces (pure) -------------------------------------------------------------------------------------------

// The atlas's cells: which entry (a key: Key(tab, id); 0 none) each holds, and when each was last shown. Take finds a
// key's cell or gives it one - a free cell, else the one shown longest ago (never one shown in `frame`: those are on
// screen now); -1 when every cell is on screen.
struct CellCache {
    std::vector<uint64_t> keys;
    std::vector<uint64_t> shown;
    std::vector<uint8_t> drawn; // (its picture is in the atlas)
    explicit CellCache(int cells = kCells) : keys(cells, 0), shown(cells, 0), drawn(cells, 0) {}
    int get_find(uint64_t key) const;
    int get_take(uint64_t key, uint64_t frame);
    void get_clear();
};
inline uint64_t get_key(int tab, uint32_t id) { return (static_cast<uint64_t>(tab + 1) << 32) | id; }
// A cell's place in the atlas (pixels) and its uv rectangle.
void cell_rect(int cell, int* x, int* y);
void cell_uv(int cell, float* u0, float* v0, float* u1, float* v1);

// The lists' entries: "s:0000ABCD" (scenery: a sector's checksum) / "o:0000ABCD" (an object's id).
std::string get_entry_name(int tab, uint32_t id);
bool parse_entry(const std::string& text, int* tab, uint32_t* id);
// Recently used: `entry` to the front (once), at most `cap` kept.
void push_recent(std::vector<std::string>* list, const std::string& entry, size_t cap);
// Favourites: in or out.
void toggle_in(std::vector<std::string>* list, const std::string& entry);
// The ini's value and back (comma separated; empty and malformed entries dropped, duplicates once).
std::string join_list(const std::vector<std::string>& list);
std::vector<std::string> split_list(const std::string& text);
// A scene's name as an ini key's start (letters, digits and _ kept, the rest _).
std::string get_key_of(const std::string& scene);

// --- the game side ----------------------------------------------------------------------------------------------

// Once a frame from level_editor::Blit (before Ui::TickCycle): the level's lists loaded when the level changed; while the
// palette is open in grid mode, the pictures the grid asked for last frame drawn (kPerFrame at most).
void tick_cycle(const ui::ShapeMesh& model);
// The editor closing: the atlas freed.
void halt();
// An entry placed (Recently used).
void note_used(int tab, uint32_t id);
bool favourite(int tab, uint32_t id);
void toggle_favourite(int tab, uint32_t id);
// The palette's grid mode.
bool grid_mode();
void set_grid_mode(bool on);

// The palette's item area (level_editor_ui.cpp's Browser): the Grid / List toggle, the All / Recent / Favourites chips
// and the items (the filter `filter` applied) in `height` pixels (negative: all but that much). `follow`: the
// highlighted entry moved by the keys (scrolled into view). Requests (select, place) added to `requests`. Returns the
// entry under the mouse (-1: none).
int items_panel(const ui::ShapeMesh& model, const char* filter, bool follow, float height, ImFont* mono,
                ImFont* small_font, float scale, std::vector<ui::Request>* requests);

} // namespace frontend::level_editor::thumbs
