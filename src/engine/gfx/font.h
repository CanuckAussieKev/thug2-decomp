#pragma once

#include <cstdint>

#include "engine/core/mem.h"
#include "engine/lists/key_map.h"

namespace script {
class Struct;
class Script;
} // namespace script

// Fonts loaded by the scripts (LoadFont), by name, and the meta button map the button glyphs go through.
namespace render {

// The engine's font (the base of the PC font, PcFont in pc_font.h: its first 8 bytes; vtable 0x00648A6C - [0] the
// destructor, [1] load (0x0049F4B0: false), [2] - [4] the stubs below, [5] unload (empty), [6] / [7] the height and
// base (0), [8] a string's size (0x0048D090: nothing)).
class Typeface : public memory::HeapObject {
public:
    // 0x0048D020
    Typeface() { unknown4_ = 0; }
    // 0x0048D030 (0x0048D1F0 the scalar deleting destructor, vtable slot 0)
    virtual ~Typeface() = default;
    Typeface(const Typeface&) = delete;
    Typeface& operator=(const Typeface&) = delete;

    // Vtable slot 1: loaded from "fonts/<name>..." - whether it loaded. The engine's: false.
    virtual bool read_in(const char* /*name*/) { return false; }
    // 0x0048D070 (slot 2): the character and space spacings. The engine's: "STUB A DUB DUB".
    virtual void change_spacings(int glyph_gap, int blank_gap);
    // 0x0048D080 (slots 3 and 4): the colour table / whether it's the button glyph font. The engine's: "STUB A DUB
    // DUB".
    virtual void change_colors(const uint32_t* colors);
    virtual void flag_button_font(bool buttons);
    // Slot 5: the platform's unload. The engine's: nothing.
    virtual void unload_platform() {}
    // Slots 6-8: its height, base and a string's width and height. The engine's: 0, 0, nothing. (0x0048D060, a
    // string's size, is a jump to slot 8 folded with other classes' slot 8: engine_geom.h's virtual_slot8_jump.)
    virtual uint32_t fetch_tallness() { return 0; }
    virtual uint32_t get_base() { return 0; }
    virtual void read_string_param(const char* /*text*/, float* /*width*/, float* /*height*/) {}

    // 0x0048D040: the platform's unload, +4 cleared.
    void offload();

    uint32_t unknown4_;
};

// A loaded font: its name (the file name the scripts gave) and the font. 16 of them at 0x006FA830.
struct FontSlot {
    char name[24];
    Typeface* font;
};
static_assert(sizeof(FontSlot) == 0x1C, "layout must match the original");

constexpr int kFontSlots = 16;

// Load the font `name` (or, if it's loaded, just update it): spacings, colour table (`colors` - 16 RGBA
// entries - or null to keep the font's), and whether it's the button-glyph font. Loading a font also loads
// the meta button map (once).
void load_font(const char* name, int glyph_gap, int blank_gap, uint32_t* colors, bool button_font);

// LoadFont <name> [buttons_font] [char_spacing = n] [space_spacing = n] [color_tab = [[r g b a] ...]]
bool __cdecl cfunc_load_font(script::Struct* params, script::Script* script);

// The name of the button-glyph font and of the meta button map script array, as PartyMod's GlyphStyle
// setting picks them (1 PS2, 2 GameCube, otherwise the original Xbox ones).
const char* button_font_name();
const char* meta_button_map_name();

// 0x0048D760: the loaded font named by this checksum, or null.
Typeface* __cdecl get_font(uint32_t name);
// 0x0048D6C0: the font `name` unloaded (0x004B4090) and taken out of the table (its slot's name kept).
void __cdecl unload_font(const char* name);
// UnloadFont <name> (0x0048D7B0).
bool __cdecl cfunc_unload_font(script::Struct* params, script::Script* script);
// 0x0048D6A0: the button glyph's character for the meta button a hex digit names (through the meta button map).
char __cdecl button_glyph(const char* digit);

extern char g_glyph_alias_table[32];

extern FontSlot g_font_slots[16];
extern lists::KeyMap g_font_table; // 0x006FA9F0: the fonts by name

} // namespace render
