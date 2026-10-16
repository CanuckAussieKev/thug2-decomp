// Button glyphs in text on PC.
// Contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md

#include "game/gfx/glyphs.h"

#include "game/core/str.h"
#include "game/sio/sdl_input.h"
#include "game/sys/config.h"

namespace Nx {

namespace {

bool s_preferGlyphs = false;
uint8_t s_glyphStyle = 0;

// The keyboard is in use and glyphs aren't preferred: the key names.
bool KeyNames() { return Sio::UsingKeyboard() && !s_preferGlyphs; }

} // namespace

bool& PreferGlyphs() { return s_preferGlyphs; }
uint8_t& GlyphStyle() { return s_glyphStyle; }

void LoadGlyphSettings()
{
    s_preferGlyphs = Config::GetBool(Config::kMiscellaneous, "PreferGlyphs", false);
    s_glyphStyle = static_cast<uint8_t>(Config::GetInt(Config::kMiscellaneous, "GlyphStyle", 1));
}

uint32_t __cdecl ButtonForTag(uint8_t* digit)
{
    if (!KeyNames()) {
        switch (*digit) {
        case 'M': case 'm': return 3;
        case 'N': case 'n': return 2;
        case 'O': case 'o': return 1;
        case 'P': case 'p': return 0;
        case 'Q': case 'q': return 14;
        case 'R': case 'r': return 15;
        case 'S': case 's': return 16;
        case 'T': case 't': return 17;
        default: break;
        }
    }
    return static_cast<uint32_t>(Str::DehexifyDigit(reinterpret_cast<const char*>(digit)));
}

uint8_t __cdecl ShouldUseGlyph(uint8_t index)
{
    if (KeyNames())
        return index >= 0x04 && index <= 0x0D;
    return index <= 17;
}

} // namespace Nx
