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
#include "base/maths/vec2.h"

namespace frontend::new_frontend::paint::internal {

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

} // namespace frontend::new_frontend::paint::internal
