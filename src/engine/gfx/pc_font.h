#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"

// The engine's fonts and texts on PC (Nx engine): the PC font (the engine's Typeface over a bitmap font, TypefaceData) and
// the PC text (the engine's GlyphString over a piece of screen text, GlyphRun), the texts' free list, and the platform calls
// that load and unload fonts.
namespace render {

struct TypefaceData;
class GlyphRun;

// The PC font (0xC bytes, vtable 0x0064A328; the engine's Typeface with the bitmap font at +8).
class PcFont final : public Typeface {
public:
    // 0x004B3BB0: no font yet.
    PcFont() { font = nullptr; }
    // 0x004B3E00 (0x004B4060 the scalar deleting destructor): the font unloaded if there is one, then the engine's.
    ~PcFont() override;
    // 0x004B3BD0 (vtable slot 1): load "fonts/<name>.fnt.xbx" (TypefaceData::ReadIn). Whether it loaded.
    bool read_in(const char* name) override;
    // 0x004B3C00 (slot 2): the character spacing, and the space's width when above 0.
    void change_spacings(int glyph_gap, int blank_gap) override;
    // 0x004B3C30 (slot 3): the 16 colours "\c1".."\cg" pick (RGBA as bytes).
    void change_colors(const uint32_t* colors) override;
    // 0x004B3CB0 (slot 4): this is the button glyph font (0x00780FDC), or no font is.
    void flag_button_font(bool buttons) override;
    // 0x004B3CD0 (slot 5): the font unloaded (TypefaceData::offload).
    void unload_platform() override;
    // 0x004B3CF0 / 0x004B3D00 / 0x004B3D10 (slots 6-8): jumps to what the bitmap font has - its height and base
    // (0x00469490 / 0x005480C0, the shared "first / second field" getters) and the size of a text.
    uint32_t fetch_tallness() override;
    uint32_t get_base() override;
    void read_string_param(const char* text, float* width, float* height) override;

    TypefaceData* font; // +0x08
};
static_assert(sizeof(PcFont) == 0xC, "layout must match the original");

// The PC text (0x4C bytes, vtable 0x0064A34C; the engine's GlyphString, whose properties go to its screen text at
// +0x48).
class PcText final : public GlyphString {
public:
    // 0x004B3E70: the engine's (no window), a screen text of its own (priority 0), then everything sent to it.
    PcText();
    // 0x004B3D20 (0x004B3F20 the scalar deleting destructor): the screen text destroyed, then the engine's.
    ~PcText() override;
    // 0x004B3D80 (vtable slot 1): every property sent to the screen text (slots 3, 4, 2).
    void refresh_all() override;
    // 0x004B3DA0 (slot 2): shown or hidden.
    void update_hidden() override;
    // 0x004B3F50 (slot 3): font, string, effects, position, pivot, scales, angle, colour.
    void update_settings() override;
    // 0x004B3DB0 (slot 4): the priority, and the depth (0 without z_enabled_).
    void update_priority() override;

    GlyphRun* text; // +0x48
};
static_assert(sizeof(PcText) == 0x4C, "layout must match the original");

// 0x004B3FD0: `count` new texts put on the free list (0x006FA804).
void __cdecl allocate_text_pool(uint32_t count);
// 0x004B4090: the font's engine Unload (0x0048D040: unload_platform, then +4 cleared).
void __cdecl unload_pc_font(PcFont* font);
// 0x004B40A0: a new PC font, loaded from `name`.
PcFont* __cdecl create_pc_font(const char* name);

} // namespace render
