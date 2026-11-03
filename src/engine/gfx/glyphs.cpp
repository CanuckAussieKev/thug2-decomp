// Button glyphs in text on PC.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/gfx/glyphs.h"

#include "engine/core/str.h"
#include "engine/pad_input/sdl_input.h"
#include "platform/platform_config.h"
#include "engine/core/no_destroy.h"

#include <cstdint>

namespace render {

constexpr int kGlyphQ = 14; // the glyph index of the button named by the letter
constexpr int kGlyphR = 15;
constexpr int kGlyphS = 16;
constexpr int kGlyphT = 17;
constexpr int kLastKeyboardGlyph = 0x0D; // the keyboard layout has the glyphs 4-13 (the first four are the pad's)
constexpr int kLastPadGlyph = 17;

namespace {

// NOT ORIGINAL grouping - glyphs.cpp's state (were globals). Made on first use, never destroyed (glyphs_state()).
struct GlyphsState {
    bool prefer_glyphs_ = false;
    uint8_t glyph_style_ = 0;
};

GlyphsState& glyphs_state()
{
    static core::NoDestroy<GlyphsState> state;
    return state.get();
}

// The keyboard is in use and glyphs aren't preferred: the key names.
bool key_names() { return sio::using_keyboard() && !glyphs_state().prefer_glyphs_; }

} // namespace

bool& prefer_glyphs() { return glyphs_state().prefer_glyphs_; }
uint8_t& glyph_style() { return glyphs_state().glyph_style_; }

void load_glyph_settings()
{
    glyphs_state().prefer_glyphs_ = build_info::fetch_flag(build_info::kMiscellaneous, "PreferGlyphs", false);
    glyphs_state().glyph_style_ = static_cast<uint8_t>(build_info::get_int(build_info::kMiscellaneous, "GlyphStyle", 1));
}

uint32_t button_for_tag(char digit)
{
    if (!key_names()) {
        switch (digit) {
        case 'M':
        case 'm':
            return 3;
        case 'N':
        case 'n':
            return 2;
        case 'O':
        case 'o':
            return 1;
        case 'P':
        case 'p':
            return 0;
        case 'Q':
        case 'q':
            return kGlyphQ;
        case 'R':
        case 'r':
            return kGlyphR;
        case 'S':
        case 's':
            return kGlyphS;
        case 'T':
        case 't':
            return kGlyphT;
        default:
            break;
        }
    }
    return static_cast<uint32_t>(text::hex_digit_value(digit));
}

uint8_t should_use_glyph(uint8_t index)
{
    if (key_names()) {
        return index >= 0x04 && index <= kLastKeyboardGlyph;
    }
    return index <= kLastPadGlyph;
}

} // namespace render
