#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <utility>

#include "engine/gfx/draw2d.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/engine_text.h"

// Bitmap fonts and the screen text drawn with them (Nx engine, PC). Text can hold escapes: "\\" (a backslash),
// "\c<d>" colour d of the font's table (0 = the text's own), "\s<d>" special glyph d, "\b<d>" a button (glyph
// from the button font, or the key's name - PartyMod decides which), "\m<d>" a button through the meta button
// map, "\u<d>" (skipped), "\L" (skipped); d is a base-32 digit.
namespace render {

// One character's image in the font texture.
struct Char {
    float u0, v0, u1, v1; // texture coordinates
    uint16_t w, h;        // size in pixels
    uint16_t baseline;    // from the top of the image
    uint16_t pad;
};
static_assert(sizeof(Char) == 0x18, "layout must match the original");

struct TypefaceData {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t height_value() const { return height; }
    [[nodiscard]] uint32_t base_value() const { return base; }
    [[nodiscard]] const std::array<uint8_t, 256>& map_value() const { return map; }
    [[nodiscard]] const std::array<uint8_t, 0x20>& special_map() const { return special_map_; }
    [[nodiscard]] int16_t glyph_gap() const { return glyph_gap_; }
    void set_glyph_gap(int16_t value) { glyph_gap_ = value; }
    [[nodiscard]] int16_t blank_gap() const { return blank_gap_; }
    void set_blank_gap(int16_t value) { blank_gap_ = value; }
    std::array<uint32_t, 16>& colors_value() { return colors; }
    [[nodiscard]] const std::array<uint32_t, 16>& colors_value() const { return colors; }
    [[nodiscard]] void* texture_value() const { return texture; }

private:
    uint32_t height;
    uint32_t base;
    Char* chars;
    std::array<uint8_t, 256> map;           // character -> chars index
    std::array<uint8_t, 0x20> special_map_; // special glyph / button -> chars index; [0x1F] is also the default glyph (Load
                                // gives it to the characters and specials the file leaves unmapped)
    uint8_t format;             // the file's character map format (1 or 2, see Load)
    uint8_t pad12d_;
    uint16_t glyph_count_; // NOT ORIGINAL: how many glyphs `chars` holds (the font file's count), for the checked access
    TypefaceData* next;   // the list of loaded fonts (0x00780FD8)
    int16_t glyph_gap_;   // added after every character
    int16_t blank_gap_;   // the width of a space ('I' when loaded)
    std::array<uint32_t, 16> colors;  // "\c1" ... (RGBA as bytes)
    void* texture;        // IDirect3DTexture9 (A8R8G8B8, managed)
    uint32_t unknown17c_; // passed with the texture to the renderer's texture set (0x004DA730); 0 when loaded

public:

    // The glyph at `index` of `chars` (checked against the glyph count).
    [[nodiscard]] Char& glyph(std::size_t index) const
    {
        return core::at(std::span<Char>(chars, glyph_count_), index);
    }

    // The colour "\c<index>" picks: index 1 is colors[0]; index 0 the word before the table (the two gaps, as the
    // original read it). NOT ORIGINAL: past the table, 0 (the original read on into the texture's fields).
    [[nodiscard]] uint32_t get_color(int index) const
    {
        if (index == 0)
            return static_cast<uint16_t>(glyph_gap_) | static_cast<uint32_t>(static_cast<uint16_t>(blank_gap_)) << 16;
        return index > 0 && std::cmp_less_equal(index, colors.size()) ? core::at(colors, index - 1) : 0;
    }

    // 0x004CECB0: the size `text` would be drawn at (pixels at scale 1): the width of the whole text
    // (as one line) and the font's height.
    void read_string_param(const char* text, float* out_width, float* out_height);

    // 0x004CF0F0: load a font - the file "fonts/<name>.fnt.xbx", or (from_memory) the same data at `name` -
    // into a new font (its texture created on the device), put at the head of the font list. Null if the
    // texture can't be created or locked (what was allocated is then left). cdecl.
    static TypefaceData* read_in(const char* name);
    // 0x004CFA80: take `font` off the font list and free it (not its texture). cdecl.
    static void offload(TypefaceData* font);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(TypefaceData, map) == 0xC, "layout must match the original");
        static_assert(offsetof(TypefaceData, special_map_) == 0x10C, "layout must match the original");
        static_assert(offsetof(TypefaceData, format) == 0x12C, "layout must match the original");
        static_assert(offsetof(TypefaceData, next) == 0x130, "layout must match the original");
        static_assert(offsetof(TypefaceData, glyph_gap_) == 0x134, "layout must match the original");
        static_assert(offsetof(TypefaceData, colors) == 0x138, "layout must match the original");
        static_assert(offsetof(TypefaceData, texture) == 0x178 && sizeof(TypefaceData) == 0x180,
                      "layout must match the original");
    }
};

// Per-character effects (an array parallel to the text, one record per character).
// (a character's effect: engine_text.h's CharEffect - offset in pixels, scale 16384 = 1, rotation radians * 16384
// about the character's centre)
using TextEffect = CharEffect;

// A vertex of the text's quads (pre-transformed, coloured, textured).
struct TextVertex {
    float x, y, z, rhw;
    uint32_t color; // ARGB
    float u, v;
};
static_assert(sizeof(TextVertex) == 0x1C, "layout must match the original");

// A piece of screen text. Virtual (vtable 0x0064A89C): [0] destructor 0x4CFAF0, [1] open_sketch 0x4CEF00
// (vertices go to the start of the buffer), [2] Draw, [3] close_sketch 0x4CEF10 (draws what's in the buffer).
// Constructor 0x4CEED0. Built on the base of things drawn in priority order (vtable 0x0064AD18; constructor
// 0x004DFA70, destructor 0x004DFAA0 - not rewritten yet): +4 the next in the draw list (0x007CC248, by
// priority), +8 set: not put in the list, +0xC the priority; the base constructor also zeroes +0x10 (z).
class GlyphRun final : public OverlayDraw {
public:
    // 0x004CEED0: hidden (not in the draw list), with this priority.
    explicit GlyphRun(float priority) : OverlayDraw(priority, true) {}
    // 0x004CEEF0 (0x004CFAF0 the scalar deleting destructor): the base's (out of the draw list).
    ~GlyphRun() override = default;
    // 0x004CEF00 (vtable slot 1): new vertices go to the start of the buffer.
    void open_sketch() override;
    // 0x004CFD00 (slot 2): the text's quads into the vertex buffer, drawing it whenever it fills or the texture
    // changes (a button glyph).
    void draw() override;
    // 0x004CEF10 (slot 3): draw the vertices in the buffer (quads, as indexed triangles) with the texture font's
    // texture - the button font's after a button glyph - or this text's font's, and empty the buffer.
    void close_sketch() override;

    TypefaceData* font{};
    const TextEffect* effects{}; // or null
    const char* text{};
    float x{}, y{}; // position, in 640x480 screen units
    float xscale{}, yscale{};
    float pivot_x_{}, pivot_y_{}; // what it rotates about, from (x, y)
    float angle{};                // radians
    uint32_t color{};             // RGBA as bytes
    bool fixed_color_{};          // ignore "\c" colours

    // 0x004CFB20: one character of a key name at *pen_x (advanced past it), at line height pen_y - plain, no
    // effects.
    void draw_key_char(char c, float* pen_x, float pen_y);
};
static_assert(offsetof(GlyphRun, z) == 0x10, "layout must match the original");
static_assert(offsetof(GlyphRun, text) == 0x1C, "layout must match the original");
static_assert(offsetof(GlyphRun, angle) == 0x38, "layout must match the original");
static_assert(offsetof(GlyphRun, fixed_color_) == 0x40, "layout must match the original");

} // namespace render
