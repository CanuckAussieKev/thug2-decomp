#pragma once

#include <cstddef>
#include <cstdint>

#include "frontend/screen_element.h"

namespace frontend {

struct SmearEffect;

// TextBlockElement (vtable 0x0064A264, 0x1C4 bytes): text wrapped into lines that fit the element's width, each
// line a text element child. Its blur, shadow and colour overrides are passed on to the lines.
class ParagraphWidget : public UiWidget {
public:
    // 0x004B0810: the constructor (returns this): no font, internal_scale and line_spacing 1, shadow colour black at
    // half alpha, no blur, no lines; type TextBlockElement.
    ParagraphWidget* make_new();
    // 0x004B08B0: the destructor body: the blur freed, then UiWidget's.
    void destroy_body();
    // 0x004B0B30 (vtable slot 0)
    ParagraphWidget* teardown(uint8_t flags);
    // 0x004B1B60 (slot 1): UiWidget's, then internal_scale, line_spacing, font, internal_just,
    // allow_expansion, text (a string or an array of them), blur_effect / no_blur_effect, blur_rgba, shadow_offs,
    // Shadow / no_shadow, shadow_rgba, the encoded colour overrides; then the lines take them.
    void change_traits(script::Struct* params);
    // 0x004B0B60 (slot 9): with the blur, the block's update, the blur's settings and each line's blur aimed at
    // the block's middle; then UiWidget's.
    void blend_towards(script::Struct* params);
    // 0x004B1EC0 (slot 10): after a change, the lines placed one under another (internal_just), in its colour;
    // then they take the block's settings.
    void tick_frame();

    // 0x004AF6C0: the font (by checksum) and how many lines fit the height.
    void change_typeface(uint32_t font);
    // 0x004AF720: `text`'s words added to the lines, a new line when a word doesn't fit the width (or after a
    // line end, or at 0x60 characters); the lines wrap round at the most that fit.
    void add_words(const char* text);
    // 0x004B0CD0: the lines made again from `texts` (run together): the old line elements destroyed, a text
    // element per line (the last ones that fit; with allow_expansion the block grows to fit them all).
    void change_caption(const char** texts, int count);
    // 0x004B11C0: the lines' text, joined with spaces, into `out` (false if it doesn't fit `size`).
    bool fetch_caption(char* out, int size);
    // 0x004B1390: the length of that text.
    int fetch_magnitude();
    // 0x004B14A0: the last character taken off (an escaped backslash both); false if there's no text.
    bool backspace();
    // 0x004B16D0: `text` added at the end; with `enforce_max_width` (unless it is a space) or `last_line`, refused
    // when the last word is wider than the block.
    bool join_onto(const char* text, bool enforce_max_width, bool last_line);
    // 0x004B1540: each line takes the block's blur, shadow (offset, colour) and encoded colour override.
    void update_lines();

    uint32_t font_name_;      // +0x178
    char** lines_;          // +0x17C (while the lines are made: 30 of 0x60 characters)
    int num_lines_;           // +0x180 lines made
    int line_;               // +0x184 the line being filled
    int line_length_;         // +0x188
    float line_width_;        // +0x18C
    int line_capacity_;           // +0x190 how many fit
    float inner_align_h_, inner_align_v_; // +0x194
    float internal_scale_;    // +0x19C
    float line_spacing_;      // +0x1A0
    float lines_height_;      // +0x1A4 the lines' height in all
    int rows_filled_;        // +0x1A8 line elements
    SmearEffect* blur_;     // +0x1AC
    bool shadow_;            // +0x1B0
    bool may_grow_;    // +0x1B1
    uint8_t pad1b2_[2];
    float shadow_x_, shadow_y_; // +0x1B4
    uint8_t shadow_rgba_[4];  // +0x1BC
    bool override_rgba_;      // +0x1C0
    bool remembered_override_; // +0x1C1
    bool override_alpha_;     // +0x1C2
    uint8_t pad1c3_;
};
static_assert(offsetof(ParagraphWidget, line_capacity_) == 0x190 && offsetof(ParagraphWidget, blur_) == 0x1AC &&
                  offsetof(ParagraphWidget, shadow_rgba_) == 0x1BC && sizeof(ParagraphWidget) == 0x1C4,
              "layout must match the original");

} // namespace Front
