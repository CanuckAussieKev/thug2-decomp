#pragma once

// NOT ORIGINAL - what new_frontend_draw.cpp's pieces share (the design's colours and sizes, the text styles, the screen
// parts and the helpers more than one piece uses). They were the one file's anonymous namespace; here they are in
// `internal`, and each piece that includes this has a using-directive for them (in the piece, not here: SF.7), so it
// sees them as it saw them there.

#include "frontend/new_frontend_draw.h"
#include <cstdint>
#include <string>
#include <vector>
#include "frontend/new_frontend_model.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/maths/rect.h"
#include "base/maths/vec2.h"

namespace frontend::new_frontend::paint::internal {

constexpr float kVisibleThreshold = 0.001F; // an ease or fade below this counts as not started (or gone)

// --- the colours (docs/design/frontend/README.md) ---
constexpr uint32_t kGround = 0xFF0A0A0A;

constexpr uint32_t kPaper = 0xFFF2EFE8;

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

} // namespace frontend::new_frontend::paint::internal
