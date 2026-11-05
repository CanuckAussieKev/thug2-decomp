#pragma once

// NOT ORIGINAL - what new_frontend_draw.cpp's pieces share (the design's colours and sizes, the text styles, the screen
// parts and the helpers more than one piece uses). They were the one file's anonymous namespace; here they are in
// `internal`, and each piece that includes this has a using-directive for them (in the piece, not here: SF.7), so it
// sees them as it saw them there.

#include "frontend/new_frontend_draw.h"
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "frontend/new_frontend_model.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/maths/rect.h"
#include "base/maths/vec2.h"

namespace frontend::new_frontend::paint::internal {

constexpr float kColumnLeft = 48.0F;    // the left edge of the menu column, in design pixels

constexpr float kDegreesPerHalfTurn = 180.0F;

constexpr float kHeadingTextSize = 30.0F;

constexpr float kLabelTextSize = 13.0F;

constexpr float kNoteTextSize = 12.0F;      // text sizes in design pixels

constexpr float kRowTiltDegrees = 3.0F;     // the tilt a row starts its entrance at

constexpr float kTabLineHeight = 1.05F;

constexpr float kTextLineHeight = 1.3F; // the line height (times the text size) of the small text

constexpr float kVisibleThreshold = 0.001F; // an ease or fade below this counts as not started (or gone)

// Row entrances and the numbered rows: the slide a row makes as it comes in; the main menu's rows and the level
// list's (and the dialog's, which uses them) differ in how far their text and cut sit from the number.
constexpr float kRowSlideDistance = 24.0F;

constexpr float kRowNumberWidth = 26.0F;

constexpr float kRowCutLeadIn = 6.0F; // how far the cut starts left of the text

constexpr float kLevelRowNumberGap = 14.0F;

constexpr float kLevelRowCutPad = 10.0F;

// The pause screens: how far the game shows through, the clock text, the side titles' lift.
constexpr float kPauseGroundDim = 0.55F;

// The slanted button (Skate, Open): its padding, the cut's inset, the gap before its arrow; its height is the heading
// text's line plus kButtonVerticalPad; the records start kButtonRecordsGap above it.
constexpr float kButtonCutInset = 10.0F;

constexpr float kButtonPadding = 22.0F;

constexpr float kButtonArrowGap = 10.0F;

// --- the colours (docs/design/frontend/README.md) ---
constexpr uint32_t kGround = 0xFF0A0A0A;

constexpr uint32_t kPaper = 0xFFF2EFE8;

constexpr uint32_t kMuted = 0xFFB9B5AC;

constexpr uint32_t kMuted2 = 0xFF8E8A82;

constexpr uint32_t kRule = 0xFF3A3A38;

ImU32 get_col(uint32_t argb, float alpha = 1.0F);

uint32_t get_mix(uint32_t a, uint32_t b, float blend);

// --- text ---

constexpr float kStyleDefaultSize = 14.0F; // a Style's default CSS size, in design px

struct Style {
    Face face = Face::kBody;
    float css = kStyleDefaultSize;     // its CSS size (design px)
    float tracking = 0.0F; // letter-spacing (design px)
    bool upper = false;
    uint32_t colour = kPaper;
    float alpha = 1.0F;
};

float get_measure(const Style& style, const TickCycle& frame, const char* text);

void draw_text(ImDrawList* dl, const Style& style, const TickCycle& frame, maths::Vec2 at, const char* text);

float datum(const Style& style, const TickCycle& frame, float top, float line_height);

std::vector<std::string> get_wrap(const Style& style, const TickCycle& frame, const std::string& text, float width);

// --- shapes ---

// A cut's slant: how far its top edge starts right of the box's left, and its bottom edge ends left of the box's right.
struct Slant {
    float top;
    float bottom;
};

void get_cut(ImDrawList* dl, const maths::Rect& box, Slant slant, ImU32 colour);

void xform(ImDrawList* dl, int start, maths::Vec2 pivot, float radians, maths::Vec2 offset);

void get_arrow(ImDrawList* dl, maths::Vec2 at, float size, bool left, ImU32 colour, float* advance);

// How a piece of art shows: its pan (0 - 1 across the part the box doesn't cover) and its opacity.
struct ArtLook {
    float pan;
    float alpha;
};

void art_box(ImDrawList* dl, const Art& art, const maths::Rect& box, ArtLook look);

void scrim(ImDrawList* dl, const TickCycle& frame, const Art& art, ArtLook look);

void backdrop(ImDrawList* dl, const TickCycle& frame, const Art& art, float pan);

std::string pad2(int value);

// --- one screen's parts ---

// A horizontal extent in design x: from `left` to `right`.
struct Band {
    float left;
    float right;
};

// A launch's slice through the big name: how far (0: none) and the cut's colour.
struct Slice {
    float amount;
    uint32_t colour;
};

// A colour and an opacity.
struct Ink {
    uint32_t colour{};
    float alpha = 1.0F;
};

struct Ctx {
    ImDrawList* dl;
    // (views of the caller's data, while the frame is drawn)
    const TickCycle* f;
    const Menu* menu;
    const Extras* extras;
    std::vector<Hit>* hits;
    void get_hit(Hit::Kind kind, int index, float x0, float y0, float x1, float y1) const
    {
        if (hits) {
            hits->push_back({.kind = kind, .index = index, .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1});
        }
    }
};

Style mono12(uint32_t colour, bool tracked = true);

float get_header(Ctx& ctx, maths::Vec2 top_right, const char* left_text, bool back, const char* right_text);

// A list row with the kinetic selection: number, name, the cut behind the name when selected - all turned and moved
// by the selection's amount `e`. Design numbers: the row's top `top`, its line box `line_h`, the name's left `text_x_`,
// the number's style, the cut's extents and insets.
struct RowStyle {
    Style name;
    Style number;
    float line_height_{}; // (a multiple of the name's size)
    float row_pad_top_{}; // (above the line box)
    float row_height_{};
    float text_x_{};                 // the name's left
    float cut_left_{};               // the cut's left (the label span's left - 6)
    float cut_right_pad_{};          // past the name's right (span padding + 22 / 26)
    float cut_top_{}, cut_bottom_{}; // (fractions of the line box)
    float top_inset_{}, bottom_inset_{};
};

// A row's state: how far it is selected (0 - 1: the cut and the turn), its cut's colour, dimmed (not focusable).
struct RowLook {
    float amount;
    uint32_t cut;
    bool dim;
};

void get_row(Ctx& ctx, const RowStyle& rs, float top, const char* number, const char* label, RowLook look);

RowStyle main_row_style();

RowStyle level_row_style();

float big_name(Ctx& ctx, const std::string& name, float css, Band band, float bottom, Slice cut);

float get_kicker(Ctx& ctx, const std::string& text, maths::Vec2 at, Ink ink);

float get_hints(Ctx& ctx, const std::vector<std::string>& hints, Band band, float bottom,
                std::vector<std::pair<float, float>>* spans = nullptr);

Art art_for(const Extras& extras, const std::string& image);

std::string art_by_id(const Catalogue* data, const std::string& id);

void pause_ground(Ctx& ctx, float window_width, float window_height, float dim = kPauseGroundDim);

// The pause menus' right side's titles: the kicker ("PAUSED - the game's tab"), the big name (the level), their colour;
// `top`: the session records' rule's y less a gap.
struct SideTitle {
    float top;
    std::string kicker;
    std::string name;
    uint32_t colour;
};

SideTitle pause_side(Ctx& ctx, Band band, float bottom);

// A row of tabs: each one's text and colour, and which is selected.
struct TabRow {
    std::span<const std::pair<std::string, uint32_t>> tabs;
    int selected{};
};

float get_tabs(Ctx& ctx, TabRow row, float y);

void get_button(Ctx& ctx, maths::Vec2 at, const char* text, uint32_t colour, bool arrow, Hit::Kind kind, float alpha);

} // namespace frontend::new_frontend::paint::internal
