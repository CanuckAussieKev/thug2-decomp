#pragma once

#include <cstddef>
#include <cstdint>

// The engine's fonts and texts on PC (Nx engine): the PC font (the engine's Typeface over a bitmap font, TypefaceData) and
// the PC text (the engine's GlyphString over a piece of screen text, GlyphRun), the texts' free list, and the platform calls
// that load and unload fonts.
namespace render {

struct TypefaceData;
struct GlyphRun;

// The PC font (0xC bytes, vtable 0x0064A328; the engine's Typeface - constructor 0x0048D020, vtable 0x00648A6C, its
// destructor body 0x0048D030 - with the bitmap font at +8).
struct PcFont {
    void** vtable;
    uint32_t unknown4; // the engine's (cleared by its Unload, 0x0048D040)
    TypefaceData* font;

    // 0x004B3BB0: construct (no font yet). Returns this.
    PcFont* make_new();
    // 0x004B3E00: the destructor's body (the font unloaded if there is one).
    void destroy_body();
    // 0x004B4060 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    PcFont* teardown(unsigned int flags);
    // 0x004B3BD0 (virtual [1]): load "fonts/<name>.fnt.xbx" (TypefaceData::ReadIn). Whether it loaded.
    bool read_in(const char* name);
    // 0x004B3C00 (virtual [2]): the character spacing, and the space's width when above 0.
    void change_spacings(int glyph_gap, int blank_gap);
    // 0x004B3C30 (virtual [3]): the 16 colours "\c1".."\cg" pick (RGBA as bytes).
    void change_colors(const uint32_t* colors);
    // 0x004B3CB0 (virtual [4]): this is the button glyph font (0x00780FDC), or no font is.
    void flag_button_font(bool buttons);
    // 0x004B3CD0 (virtual [5]): the font unloaded (TypefaceData::offload).
    void offload();
    // 0x004B3CF0 / 0x004B3D00 / 0x004B3D10 (virtual [6]-[8]): jumps to what the bitmap font has - its height and
    // base (0x00469490 / 0x005480C0, the shared "first / second field" getters) and the size of a text.
    uint32_t fetch_tallness();
    uint32_t get_base();
    void read_string_param(const char* text, float* width, float* height);
};
static_assert(sizeof(PcFont) == 0xC, "layout must match the original");

// The PC text (0x4C bytes, vtable 0x0064A34C; the engine's GlyphString - constructor 0x0048D220, vtable 0x00648AA0,
// destructor body 0x0048D620 - whose properties go to its screen text at +0x48).
struct PcText {
    void** vtable;
    PcFont* font;
    const void* effects;  // per-character effects (TextEffect), or null
    const char* string;
    float x, y;
    float pivot_x_, pivot_y_;
    float xscale, yscale;
    float angle;
    float priority;
    uint32_t unknown30;
    uint32_t z;           // the depth, in 1/65536ths (used with z_enabled_)
    uint32_t color;       // RGBA as bytes
    bool fixed_color_;
    uint8_t unknown3d_;
    bool hidden;
    bool z_enabled_;
    uint32_t unknown40;   // the engine constructor's argument
    PcText* next_free_;    // +0x44: the free list (0x006FA804)
    GlyphRun* text;          // +0x48

    // 0x004B3E70: construct (the engine's with 0): a screen text of its own (priority 0), then everything sent to
    // it. Returns this.
    PcText* make_new();
    // 0x004B3D20: the destructor's body (the screen text destroyed).
    void destroy_body();
    // 0x004B3F20 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    PcText* teardown(unsigned int flags);
    // 0x004B3D80 (virtual [1]): every property sent to the screen text (virtual [3], [4], [2]).
    void tick_frame();
    // 0x004B3DA0 (virtual [2]): shown or hidden.
    void update_hidden();
    // 0x004B3F50 (virtual [3]): font, string, effects, position, pivot, scales, angle, colour.
    void tick_traits();
    // 0x004B3DB0 (virtual [4]): the priority, and the z (0 without z_enabled_).
    void update_priority();
};
static_assert(offsetof(PcText, priority) == 0x2C && offsetof(PcText, z) == 0x34 && offsetof(PcText, hidden) == 0x3E &&
                  offsetof(PcText, next_free_) == 0x44 && sizeof(PcText) == 0x4C,
              "layout must match the original");

// 0x004B3FD0: `count` new texts put on the free list (0x006FA804).
void __cdecl allocate_text_pool(uint32_t count);
// 0x004B4090: the font's engine Unload (0x0048D040: its virtual [5], then +4 cleared).
void __cdecl unload_pc_font(PcFont* font);
// 0x004B40A0: a new PC font, loaded (virtually) from `name`.
PcFont* __cdecl create_pc_font(const char* name);

} // namespace Nx
