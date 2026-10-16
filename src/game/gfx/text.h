#pragma once

#include <cstddef>
#include <cstdint>

// Bitmap fonts and the screen text drawn with them (Nx engine, PC). Text can hold escapes: "\\" (a backslash),
// "\c<d>" colour d of the font's table (0 = the text's own), "\s<d>" special glyph d, "\b<d>" a button (glyph
// from the button font, or the key's name - the PC port decides which), "\m<d>" a button through the meta button
// map, "\u<d>" (skipped), "\L" (skipped); d is a base-32 digit.
namespace Nx {

// One character's image in the font texture.
struct SChar {
    float u0, v0, u1, v1; // texture coordinates
    uint16_t w, h;        // size in pixels
    uint16_t baseline;    // from the top of the image
    uint16_t pad;
};
static_assert(sizeof(SChar) == 0x18, "layout must match the original");

struct SFont {
    uint32_t height;
    uint32_t base;
    SChar* chars;
    uint8_t map[256];         // character -> chars index
    uint8_t specialMap[0x20]; // special glyph / button -> chars index; [0x1F] is also the default glyph (Load
                              // gives it to the characters and specials the file leaves unmapped)
    uint8_t format;           // the file's character map format (1 or 2, see Load)
    uint8_t pad12D[3];
    SFont* next;              // the list of loaded fonts (0x00780FD8)
    int16_t charSpacing;      // added after every character
    int16_t spaceSpacing;     // the width of a space ('I' when loaded)
    uint32_t colors[16];      // "\c1" ... (RGBA as bytes)
    void* texture;            // IDirect3DTexture9 (A8R8G8B8, managed)
    uint32_t unknown17C;      // passed with the texture to the renderer's texture set (0x004DA730); 0 when loaded

    // The colour "\c<index>" picks: index 1 is colors[0]; the original reads past the table for larger ones.
    uint32_t Color(int index) const
    {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(this) + 0x134 + 4 * index);
    }

    // 0x004CECB0: the size `text` would be drawn at (pixels at scale 1): the width of the whole text
    // (as one line) and the font's height.
    void QueryString(const char* text, float* outWidth, float* outHeight);

    // 0x004CF0F0: load a font - the file "fonts/<name>.fnt.xbx", or (fromMemory) the same data at `name` -
    // into a new font (its texture created on the device), put at the head of the font list. Null if the
    // texture can't be created or locked (what was allocated is then left). cdecl.
    static SFont* Load(const char* name, bool fromMemory);
    // 0x004CFA80: take `font` off the font list and free it (not its texture). cdecl.
    static void Unload(SFont* font);
};
static_assert(offsetof(SFont, map) == 0xC, "layout must match the original");
static_assert(offsetof(SFont, specialMap) == 0x10C, "layout must match the original");
static_assert(offsetof(SFont, format) == 0x12C, "layout must match the original");
static_assert(offsetof(SFont, next) == 0x130, "layout must match the original");
static_assert(offsetof(SFont, charSpacing) == 0x134, "layout must match the original");
static_assert(offsetof(SFont, colors) == 0x138, "layout must match the original");
static_assert(offsetof(SFont, texture) == 0x178 && sizeof(SFont) == 0x180, "layout must match the original");

// Per-character effects (an array parallel to the text, one record per character).
struct STextEffect {
    int16_t x, y;           // offset in pixels
    int16_t xscale, yscale; // 16384 = 1
    int16_t rotation;       // radians * 16384, about the character's centre
};
static_assert(sizeof(STextEffect) == 10, "layout must match the original");

// A vertex of the text's quads (pre-transformed, coloured, textured).
struct TextVertex {
    float x, y, z, rhw;
    uint32_t color; // ARGB
    float u, v;
};
static_assert(sizeof(TextVertex) == 0x1C, "layout must match the original");

// A piece of screen text. Virtual (vtable 0x0064A89C): [0] destructor 0x4CFAF0, [1] BeginDraw 0x4CEF00
// (vertices go to the start of the buffer), [2] Draw, [3] EndDraw 0x4CEF10 (draws what's in the buffer).
// Constructor 0x4CEED0. Built on the base of things drawn in priority order (vtable 0x0064AD18; constructor
// 0x004DFA70, destructor 0x004DFAA0 - not rewritten yet): +4 the next in the draw list (0x007CC248, by
// priority), +8 set: not put in the list, +0xC the priority; the base constructor also zeroes +0x10 (z).
struct SText {
    void** vtable;
    uint8_t base[0xC]; // the base drawable's
    float z;
    SFont* font;
    const STextEffect* effects; // or null
    const char* text;
    float x, y;            // position, in 640x480 screen units
    float xscale, yscale;
    float pivotX, pivotY;  // what it rotates about, from (x, y)
    float angle;           // radians
    uint32_t color;        // RGBA as bytes
    bool fixedColor;       // ignore "\c" colours

    // 0x004CFD00 (virtual [2]): the text's quads into the vertex buffer, drawing it whenever it fills or the
    // texture changes (a button glyph).
    void Draw();
    // 0x004CFB20: one character of a key name at *penX (advanced past it), at line height penY - plain, no
    // effects.
    void DrawKeyChar(char c, float* penX, float penY);

    // 0x004CEED0: construct (the base unlisted, with this priority). Returns this.
    SText* Construct(float priority);
    // 0x004CEEF0: the destructor's body (back to the base, which takes itself off the draw list).
    void DestroyBody();
    // 0x004CFAF0 (virtual [0]): destroy, and free if flags & 1. Returns this.
    SText* Destroy(unsigned int flags);
    // 0x004CEF00 (virtual [1]): new vertices go to the start of the buffer.
    void ResetVertices();
    // 0x004CEF10 (virtual [3]): draw the vertices in the buffer (quads, as indexed triangles) with the texture
    // font's texture - the button font's after a button glyph - or this text's font's, and empty the buffer.
    void DrawVertices();

    void BeginDraw() { reinterpret_cast<void(__thiscall*)(SText*)>(vtable[1])(this); }
    void EndDraw() { reinterpret_cast<void(__thiscall*)(SText*)>(vtable[3])(this); }
};
static_assert(offsetof(SText, z) == 0x10, "layout must match the original");
static_assert(offsetof(SText, text) == 0x1C, "layout must match the original");
static_assert(offsetof(SText, angle) == 0x38, "layout must match the original");
static_assert(offsetof(SText, fixedColor) == 0x40, "layout must match the original");

} // namespace Nx
