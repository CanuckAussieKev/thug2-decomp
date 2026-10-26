#pragma once

#include <cstddef>
#include <cstdint>

// The engine's fonts and texts on PC (Nx engine): the PC font (the engine's CFont over a bitmap font, SFont) and
// the PC text (the engine's CText over a piece of screen text, SText), the texts' free list, and the platform calls
// that load and unload fonts.
namespace Nx {

struct SFont;
struct SText;

// The PC font (0xC bytes, vtable 0x0064A328; the engine's CFont - constructor 0x0048D020, vtable 0x00648A6C, its
// destructor body 0x0048D030 - with the bitmap font at +8).
struct CPcFont {
    void** vtable;
    uint32_t unknown4; // the engine's (cleared by its Unload, 0x0048D040)
    SFont* font;

    // 0x004B3BB0: construct (no font yet). Returns this.
    CPcFont* Construct();
    // 0x004B3E00: the destructor's body (the font unloaded if there is one).
    void DestroyBody();
    // 0x004B4060 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcFont* Destroy(unsigned int flags);
    // 0x004B3BD0 (virtual [1]): load "fonts/<name>.fnt.xbx" (SFont::Load). Whether it loaded.
    bool Load(const char* name);
    // 0x004B3C00 (virtual [2]): the character spacing, and the space's width when above 0.
    void SetSpacings(int charSpacing, int spaceSpacing);
    // 0x004B3C30 (virtual [3]): the 16 colours "\c1".."\cg" pick (RGBA as bytes).
    void SetColors(const uint32_t* colors);
    // 0x004B3CB0 (virtual [4]): this is the button glyph font (0x00780FDC), or no font is.
    void MarkAsButtonFont(bool buttons);
    // 0x004B3CD0 (virtual [5]): the font unloaded (SFont::Unload).
    void Unload();
    // 0x004B3CF0 / 0x004B3D00 / 0x004B3D10 (virtual [6]-[8]): jumps to what the bitmap font has - its height and
    // base (0x00469490 / 0x005480C0, the shared "first / second field" getters) and the size of a text.
    uint32_t GetHeight();
    uint32_t GetBase();
    void QueryString(const char* text, float* width, float* height);
};
static_assert(sizeof(CPcFont) == 0xC, "layout must match the original");

// The PC text (0x4C bytes, vtable 0x0064A34C; the engine's CText - constructor 0x0048D220, vtable 0x00648AA0,
// destructor body 0x0048D620 - whose properties go to its screen text at +0x48).
struct CPcText {
    void** vtable;
    CPcFont* font;
    const void* effects;  // per-character effects (STextEffect), or null
    const char* string;
    float x, y;
    float pivotX, pivotY;
    float xscale, yscale;
    float angle;
    float priority;
    uint32_t unknown30;
    uint32_t z;           // the depth, in 1/65536ths (used with zEnabled)
    uint32_t color;       // RGBA as bytes
    bool fixedColor;
    uint8_t unknown3D;
    bool hidden;
    bool zEnabled;
    uint32_t unknown40;   // the engine constructor's argument
    CPcText* nextFree;    // +0x44: the free list (0x006FA804)
    SText* text;          // +0x48

    // 0x004B3E70: construct (the engine's with 0): a screen text of its own (priority 0), then everything sent to
    // it. Returns this.
    CPcText* Construct();
    // 0x004B3D20: the destructor's body (the screen text destroyed).
    void DestroyBody();
    // 0x004B3F20 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcText* Destroy(unsigned int flags);
    // 0x004B3D80 (virtual [1]): every property sent to the screen text (virtual [3], [4], [2]).
    void Update();
    // 0x004B3DA0 (virtual [2]): shown or hidden.
    void UpdateHidden();
    // 0x004B3F50 (virtual [3]): font, string, effects, position, pivot, scales, angle, colour.
    void UpdateProperties();
    // 0x004B3DB0 (virtual [4]): the priority, and the z (0 without zEnabled).
    void UpdatePriority();
};
static_assert(offsetof(CPcText, priority) == 0x2C && offsetof(CPcText, z) == 0x34 && offsetof(CPcText, hidden) == 0x3E &&
                  offsetof(CPcText, nextFree) == 0x44 && sizeof(CPcText) == 0x4C,
              "layout must match the original");

// 0x004B3FD0: `count` new texts put on the free list (0x006FA804).
void __cdecl AllocateTextPool(uint32_t count);
// 0x004B4090: the font's engine Unload (0x0048D040: its virtual [5], then +4 cleared).
void __cdecl UnloadPcFont(CPcFont* font);
// 0x004B40A0: a new PC font, loaded (virtually) from `name`.
CPcFont* __cdecl CreatePcFont(const char* name);

} // namespace Nx
