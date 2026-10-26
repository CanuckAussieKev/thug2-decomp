#pragma once

#include <cstddef>
#include <cstdint>

#include "game/front/screen_element.h"

namespace Front {

struct CBlurEffect;

// TextBlockElement (vtable 0x0064A264, 0x1C4 bytes): text wrapped into lines that fit the element's width, each
// line a text element child. Its blur, shadow and colour overrides are passed on to the lines.
class CTextBlockElement : public CScreenElement {
public:
    // 0x004B0810: the constructor (returns this): no font, internal_scale and line_spacing 1, shadow colour black at
    // half alpha, no blur, no lines; type TextBlockElement.
    CTextBlockElement* Construct();
    // 0x004B08B0: the destructor body: the blur freed, then CScreenElement's.
    void DestroyBody();
    // 0x004B0B30 (vtable slot 0)
    CTextBlockElement* Destroy(uint8_t flags);
    // 0x004B1B60 (slot 1): CScreenElement's, then internal_scale, line_spacing, font, internal_just,
    // allow_expansion, text (a string or an array of them), blur_effect / no_blur_effect, blur_rgba, shadow_offs,
    // Shadow / no_shadow, shadow_rgba, the encoded colour overrides; then the lines take them.
    void SetProperties(Script::CStruct* params);
    // 0x004B0B60 (slot 9): with the blur, the block's update, the blur's settings and each line's blur aimed at
    // the block's middle; then CScreenElement's.
    void SetMorph(Script::CStruct* params);
    // 0x004B1EC0 (slot 10): after a change, the lines placed one under another (internal_just), in its colour;
    // then they take the block's settings.
    void Update();

    // 0x004AF6C0: the font (by checksum) and how many lines fit the height.
    void SetFont(uint32_t font);
    // 0x004AF720: `text`'s words added to the lines, a new line when a word doesn't fit the width (or after a
    // line end, or at 0x60 characters); the lines wrap round at the most that fit.
    void AddWords(const char* text);
    // 0x004B0CD0: the lines made again from `texts` (run together): the old line elements destroyed, a text
    // element per line (the last ones that fit; with allow_expansion the block grows to fit them all).
    void SetText(const char** texts, int count);
    // 0x004B11C0: the lines' text, joined with spaces, into `out` (false if it doesn't fit `size`).
    bool GetText(char* out, int size);
    // 0x004B1390: the length of that text.
    int GetLength();
    // 0x004B14A0: the last character taken off (an escaped backslash both); false if there's no text.
    bool Backspace();
    // 0x004B16D0: `text` added at the end; with `enforceMaxWidth` (unless it is a space) or `lastLine`, refused
    // when the last word is wider than the block.
    bool Concatenate(const char* text, bool enforceMaxWidth, bool lastLine);
    // 0x004B1540: each line takes the block's blur, shadow (offset, colour) and encoded colour override.
    void UpdateLines();

    uint32_t m_fontName;      // +0x178
    char** mp_lines;          // +0x17C (while the lines are made: 30 of 0x60 characters)
    int m_numLines;           // +0x180 lines made
    int m_line;               // +0x184 the line being filled
    int m_lineLength;         // +0x188
    float m_lineWidth;        // +0x18C
    int m_maxLines;           // +0x190 how many fit
    float m_internalJustX, m_internalJustY; // +0x194
    float m_internalScale;    // +0x19C
    float m_lineSpacing;      // +0x1A0
    float m_linesHeight;      // +0x1A4 the lines' height in all
    int m_numElements;        // +0x1A8 line elements
    CBlurEffect* mp_blur;     // +0x1AC
    bool m_shadow;            // +0x1B0
    bool m_allowExpansion;    // +0x1B1
    uint8_t m_pad1B2[2];
    float m_shadowX, m_shadowY; // +0x1B4
    uint8_t m_shadowRgba[4];  // +0x1BC
    bool m_overrideRgba;      // +0x1C0
    bool m_rememberedOverride; // +0x1C1
    bool m_overrideAlpha;     // +0x1C2
    uint8_t m_pad1C3;
};
static_assert(offsetof(CTextBlockElement, m_maxLines) == 0x190 && offsetof(CTextBlockElement, mp_blur) == 0x1AC &&
                  offsetof(CTextBlockElement, m_shadowRgba) == 0x1BC && sizeof(CTextBlockElement) == 0x1C4,
              "layout must match the original");

} // namespace Front
