#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frontend/new_frontend_model.h"

// NOT ORIGINAL - the new front end's drawing (ours; see new_frontend.h): its fonts in its own ImGui context's atlas and
// its screens through ImGui's background draw list - polygons for the slanted cuts, textured quads for the art, glyphs
// placed one by one (condensed, tracked, turned with the selected row). The current ImGui context must be the new
// front end's, between NewFrame and Render.
namespace frontend::new_frontend::paint {

// The font files (full paths) and how the display face is drawn: `condense` its width (1: as designed - Anton),
// `bold` extra weight (0: none; else drawn twice that fraction of an em apart).
struct FontFiles {
    std::string display, mono, body;
    float condense = 1.0f;
    float bold = 0.0f;
};

// The design's CSS font size (px at 1280 x 720) a font is baked for.
enum class Face : uint8_t { kDisplay, kMono, kBody };
struct Bake {
    Face face;
    float css; // its CSS font-size (design px)
};

// The fonts for `scale` (window px per design px) built into the current context's atlas (the atlas cleared first).
// False (logged) if a file can't be read - that face is left out and falls back to another.
bool build_fonts(const FontFiles& files, float scale);
float built_scale(); // (0: none built in the current context's atlas)
// Forget the fonts built (their atlas is going: a context destroyed).
void forget_fonts();

// A loaded picture (an ImGui texture id and its size in texels; 0: none).
struct Art {
    uint64_t texture = 0;
    float width = 0.0f, height = 0.0f;
    bool bottom_up_ = false; // (its rows bottom first: the game's loading screens - drawn with v from 1 at the top)
};
using ArtLookup = Art (*)(const std::string& image);

// What the career keeps for a level (-1: not known - not shown).
struct Highscores {
    int goals = -1;
    int gaps_got_ = -1, gaps_total_ = -1;
};
using RecordsLookup = Highscores (*)(const StageMap& level);

// A place the mouse can point at, in design pixels (the last frame's).
struct Hit {
    // (Item: a list's item / an option row; Tab: a game tab / an option group; Skate: the level select's Skate, the
    // Game options row's Open; Choice: an option's choice - index row * 16 + choice)
    enum class Kind : uint8_t { kItem, kStageMap, kTab, kBack, kSkate, kContinue, kChoice } kind;
    int index;
    float x0, y0, x1, y1;
};

[[nodiscard]] inline bool get_holds(const Hit& hit, float x, float y)
{
    return x >= hit.x0 && x < hit.x1 && y >= hit.y0 && y < hit.y1;
}

// The session in the level (the pause menu's panel; -1: not known - not shown).
struct Session {
    int best_combo_ = -1;
    int gaps = -1, gaps_total_ = -1;
    float seconds = -1.0f;
};

struct Extras {
    std::string skater_name_; // (empty: not shown)
    // The pause menu's: the level's name, its game's tab and colour (0: the house yellow), the session.
    std::string level_name_, level_kicker_;
    uint32_t level_colour_ = 0;
    Session session;
    // The options changed this run that are seen after a restart (one per Options::get_specs() entry), a note at the top.
    std::vector<bool> restart_pending_;
    std::string options_note_;
    ArtLookup art = nullptr;
    RecordsLookup records = nullptr;
    bool levels_missing_ = false; // (no levels.json: Free Skate can't open)
    // (a restyled front end menu - Kind::Menu: the levels' data for the items' art, the main menu's Continue level)
    const Catalogue* catalogue = nullptr;
    std::string continue_name_;
    bool show_ini_keys_ = false; // ([frontend] show_ini_keys=1: the options show their ini section and key)
};

// The whole window: the screen the menu shows, the wipe and the launch over it. `hits` gets what the mouse can point
// at; the list's rows that fit (for Menu::set_visible_rows) in `visible_rows`.
void get_screen(const Menu& menu, const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                std::vector<Hit>* hits, int* visible_rows);

} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::mirror {
class Tracker;
}

namespace frontend::new_frontend::paint {

// Phase 3: an original screen restyled (new_frontend_mirror.h) - a level's pause menu, a dialog box, the skater select
// (the 3D skater left in view), a plain list. `hits`: its rows (Item, the row's index), Back. `scroll`: the list's
// first row shown, kept by the caller (in and out). `extras`: the pause menu's level and session (null: none).
void restyled(const mirror::Tracker& tracker, const TickCycle& frame, float window_width, float window_height,
              std::vector<Hit>* hits, float* scroll, const Extras* extras = nullptr);
// The front end come up before any of its menus has: the screen's ground and the art with id `art_id` full-bleed (the
// main menu's), no rows - so the front end's 3D scene is never seen behind a menu that isn't there yet.
void get_waiting(const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                 const char* art_id);

// --- the layout's numbers (design px; exposed for the self-tests) ---

// A text's line box: the baseline's offset from the box's top for a font of ascent / descent (fractions of an em)
// at `css` px with line-height `lineHeight` (a multiple of the size), as CSS places it (the half-leading above).
float baseline_in_line(float ascent, float descent, float css, float line_height);
// The level select's list: its top for `tab_rows` rows of game tabs, and how many 42-px rows fit down to the bottom.
float level_list_top(int tab_rows);
int level_list_rows(int tab_rows);
// The main menu's row `index`: its top.
float main_row_top(int index);
// The column (the user's 2026-10-25 change: about a third of the screen, the art full-bleed behind it all): its scrim's
// edge, the right of its rules / tabs / rows, the feather past its edge, the left of the words on the right.
constexpr float kColumn = 430.0f;
constexpr float kColumnInner = 400.0f;
constexpr float kScrimFeather = 70.0f;
constexpr float kRight = 470.0f;
// The list type: the main menu's (the design's 78, smaller for the narrower column) and the level list's (40).
constexpr float kMainType = 64.0f;
constexpr float kLevelType = 36.0f;
constexpr float kMainRowHeight = kMainType * 0.92f + 4.0f; // (line-height 0.92, padding 2 + 2)
constexpr float kLevelRowHeight = kLevelType * 1.05f;      // (line-height 1.05)

// A TrueType file's vertical metrics as stb_truetype scales it: hhea's ascender / descender over head's units_per_em
// (descent negative). False if it isn't a font file it can read.
bool read_font_metrics(const uint8_t* data, size_t size, float* ascent, float* descent);

} // namespace frontend::new_frontend::paint
