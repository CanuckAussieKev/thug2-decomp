#pragma once

#include <cstddef>
#include <cstdint>

// The engine's text (Nx::CText - the platform-independent part of the PC text, CPcText in pc_font.h, its first 0x48
// bytes): a string drawn in a font, its position, pivot, scale, rotation, priority or depth, colour and per-character
// effects, each change passed on to the platform (vtable slots 2-5); texts come from a free list (0x006FA804).
namespace Nx {

class CFont;

// A character's effect (10 bytes): an offset, a scale and a rotation, in 1/16384ths.
struct sCharEffect {
    int16_t x, y;            // +0 (as given)
    int16_t xscale, yscale;  // +4 (16384: 1)
    int16_t rotation;        // +8

    // 0x0048D0A0 (Ghidra never made it: the array constructor's callback): no offset, scale 1, no rotation.
    // Returns this.
    sCharEffect* Construct();
};
static_assert(sizeof(sCharEffect) == 10, "layout must match the original");

// The effects array: 96 characters (an array new: its count before it).
constexpr int kCharEffects = 96;
// The string's buffer: 0x60 characters.
constexpr int kTextLength = 0x60;

// (vtable 0x00648AA0: [0] the destructor, [1]-[5] empty - CPcText's update the screen text: [2] shown / hidden, [3]
// the properties, [4] the priority, [5] the window.)
struct CText {
    void** m_vtable;
    CFont* mp_font;          // +0x04
    sCharEffect* mp_effects; // +0x08 (null until one is set)
    char* mp_string;         // +0x0C (a buffer of its own, null until set)
    float m_x, m_y;          // +0x10
    float m_pivotX, m_pivotY; // +0x18
    float m_xscale, m_yscale; // +0x20
    float m_angle;           // +0x28
    float m_priority;        // +0x2C
    uint32_t m_unknown30;
    float m_depth;           // +0x34 (with m_zEnabled)
    uint32_t m_colour;       // +0x38: RGBA as bytes
    bool m_fixedColour;      // +0x3C: the string's colour codes ignored
    bool m_overrideAlpha;    // +0x3D
    bool m_hidden;           // +0x3E
    bool m_zEnabled;         // +0x3F
    void* mp_window;         // +0x40: the 2D window it's drawn in
    CText* mp_nextFree;      // +0x44

    // 0x0048D220: construct in `window` - no font, string or effects, at 0, unrotated, scale 1, grey, hidden.
    // Returns this.
    CText* Construct(void* window);
    // 0x0048D620: the destructor's body - its string and effects freed.
    void DestroyBody();
    // 0x0048D670 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CText* Destroy(unsigned int flags);
    // 0x0048D5A0: the string's buffer and the effects freed.
    void FreeBuffers();

    // The setters (each followed by the platform's update, virtual [2]-[5]):
    // 0x0048D0C0 (not a Ghidra function) shown / hidden ([2]); 0x0048D0D0 (not a Ghidra function) the font ([3]).
    void SetHidden(bool hidden);
    void SetFont(CFont* font);
    // 0x0048D0E0 / 0x0048D100 (not Ghidra functions) the position / the scale ([3]).
    void SetPos(float x, float y);
    void SetScale(float x, float y);
    // 0x0048D120: the rotation about a pivot ([3]).
    void SetRotation(float angle, float pivotX, float pivotY);
    // 0x0048D140 / 0x0048D160 (not Ghidra functions): drawn by priority / at a depth ([4]).
    void SetPriority(float priority);
    void SetDepth(float depth);
    // 0x0048D180 / 0x0048D1A0 (not Ghidra functions): the colour and whether the string's colours are ignored;
    // its alpha overridden ([3]).
    void SetColour(uint32_t rgba, bool fixed);
    void SetOverrideAlpha(bool on);
    // 0x0048D1B0 (folded with the sprite's: +0x40 and [5]): the window it's drawn in.
    void SetWindow(void* window);
    // 0x0048D530: the string copied into its buffer (made on the front end's heap the first time; no length
    // limit, as the original) ([3]).
    void SetString(const char* string);
    // 0x0048D280 / 0x0048D360 / 0x0048D450: character `index`'s offset, scale (times 16384, truncated) and
    // rotation (the same) - the effects made on the front end's heap the first time (none: a write through null,
    // as the original) ([3]).
    void SetCharOffset(int index, int16_t x, int16_t y);
    void SetCharScale(int index, float x, float y);
    void SetCharRotation(int index, float rotation);
};
static_assert(offsetof(CText, m_priority) == 0x2C && offsetof(CText, m_depth) == 0x34 &&
                  offsetof(CText, m_hidden) == 0x3E && offsetof(CText, mp_nextFree) == 0x44 && sizeof(CText) == 0x48,
              "layout must match the original");

// 0x0048D1D0: a text from the free list (null when it's empty).
CText* __cdecl NewText();
// 0x0048D5F0: a text hidden, its buffers freed, and put back on the free list.
void __cdecl FreeText(CText* text);

} // namespace Nx
