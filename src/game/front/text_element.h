#pragma once

#include <cstddef>
#include <cstdint>

#include "game/front/screen_element.h"

namespace Script {
class CArray;
}

// The text element (TextElement) and its blur effect. The text itself is drawn by the engine's text objects
// (0x0048D1D0 makes one, 0x0048D5F0 frees it; their setters 0x0048D0C0-0x0048D530 are still game code) in a
// font (0x0048D760 finds one by checksum).
namespace Front {

// The blur effect (0x47C bytes, made by blur_effect): 16 copies of the text behind it, each further along a
// displacement, more scaled and fainter, morphing between settings over a time.
struct CBlurEffect {
    struct Copy {
        float alpha;        // +0x00 the copy's alpha factor (1/16 of the alpha pair's value at its place)
        float unknown04[4];
        float dx, dy;       // +0x14 its displacement
        float unknown1C[3];
        float scaleX, scaleY; // +0x28
        float unknown30[5];
    };
    // The settings, in order: blur_alpha_pair (x, y), blur_bottom_scales (x, y), blur_max_displacement (x, y).
    struct Settings {
        float alpha[2];
        float bottomScales[2];
        float maxDisplacement[2];
    };

    // 0x004A5770 (a function Ghidra hadn't found): copy `i`.
    Copy* GetCopy(int i);
    // 0x004A5780: a morph to the target over `ms` (0: the target at once), from now.
    void StartMorph(int ms);
    // 0x004A57D0 (each frame): the settings morphed on, then the copies placed from them (copy n at n/16).
    void Update();
    // 0x004A5CF0 (not found by Ghidra either): new target settings, morphed to over `ms`.
    void SetTarget(const Settings* target, int ms);
    // 0x004A5D30: the target settings from the parameters (each pair if given), morphed to over `time` seconds
    // (its milliseconds into `outMs` when given). Returns the target.
    Settings* SetFromParams(Script::CStruct* params, int* outMs);

    Copy m_copies[16];      // +0x000
    Settings m_target;      // +0x440
    Settings m_current;     // +0x458
    int m_lastTime;         // +0x470 (ms)
    int m_remaining;        // +0x474 the morph's time left (ms)
    uint32_t m_rgba;        // +0x478 blur_rgba (the copies' colour)
};
static_assert(sizeof(CBlurEffect::Copy) == 0x44 && offsetof(CBlurEffect, m_target) == 0x440 &&
                  offsetof(CBlurEffect, m_rgba) == 0x478 && sizeof(CBlurEffect) == 0x47C,
              "layout must match the original");

// TextElement (vtable 0x0064A290, 0x1B0 bytes): a line of text in a font, optionally with a shadow (a second
// set of text objects behind it) and the blur effect (16 more text objects).
class CTextElement : public CScreenElement {
public:
    // 0x004B0920: the constructor (returns this): no text / font / blur / shadow, shadow colour black at half
    // alpha, one text object; type TextElement.
    CTextElement* Construct();
    // 0x004B1830: the destructor body: the blur, the text objects and the string freed, then CScreenElement's.
    void DestroyBody();
    // 0x004B2030 (vtable slot 0)
    CTextElement* Destroy(uint8_t flags);
    // 0x004B18E0 (slot 1): CScreenElement's, then font, rot_angle (at once), internal_just, text, blur_effect /
    // no_blur_effect, blur_rgba, shadow_offs, Shadow / no_shadow, shadow_rgba, the encoded colour overrides,
    // the per-character properties; the text objects go in the window's viewport.
    void SetProperties(Script::CStruct* params);
    // 0x004B09D0 (slot 9): the blur's settings, CScreenElement's, rot_angle (morphed to).
    void SetMorph(Script::CStruct* params);
    // 0x004AFE70 (slot 10): the text objects (the text, the shadow, the blur copies) set from the element.
    void Update();

    // 0x004AF650: the text's length.
    int GetLength() const;
    // 0x004AF670: the first text object (and shadow) in the window's viewport.
    void SetViewports();
    // 0x004AF8C0: a copy of `text` (\\ kept, \_ a space, \m<c> a button glyph), the size from the font (unless
    // locked); kVisibleNow.
    void SetText(const char* text);
    // 0x004AFA00: the rotation (degrees, plus a constant) morphed to and with `now` shown, the text objects
    // turned about their internal_just point.
    void SetTextRotation(float degrees, int now);
    // 0x004AFBB0: the per-character properties (an array of { index, rot_angle, offset, scale }) on the first
    // text object.
    void SetCharProperties(Script::CArray* chars);
    // 0x004AFD00 / 0x004AFDD0: text added at the end (under 0x60 characters in all) / the last character taken
    // off (an escaped backslash both). False when nothing changed.
    bool Concatenate(const char* text);
    bool Backspace();
    // 0x004B0620: `count` new text objects (and as many shadow objects with a shadow; `shadowOnly`: just those).
    void MakeTextObjects(int count, bool shadowOnly);
    // 0x004B0760: the shadow objects freed (and the text objects unless `shadowOnly`).
    void FreeTextObjects(bool shadowOnly);
    // 0x004B0A20 / 0x004B0AE0: the blur effect on (17 text objects) / off (one).
    void EnableBlur();
    void DisableBlur();

    char* m_string;            // +0x178
    uint32_t m_fontName;       // +0x17C
    void* mp_font;             // +0x180
    void** mp_texts;           // +0x184 the text objects (the first the text, the others the blur copies)
    void** mp_shadows;         // +0x188
    int m_count;               // +0x18C
    CBlurEffect* mp_blur;      // +0x190
    bool m_shadow;             // +0x194
    uint8_t m_pad195[3];
    float m_shadowX, m_shadowY; // +0x198 shadow_offs
    uint8_t m_shadowRgba[4];   // +0x1A0
    bool m_overrideRgba;       // +0x1A4 override_encoded_rgba
    bool m_rememberedOverride; // +0x1A5
    bool m_overrideAlpha;      // +0x1A6 override_encoded_alpha
    uint8_t m_pad1A7;
    float m_internalJustX, m_internalJustY; // +0x1A8
};
static_assert(offsetof(CTextElement, mp_blur) == 0x190 && offsetof(CTextElement, m_shadowRgba) == 0x1A0 &&
                  offsetof(CTextElement, m_internalJustX) == 0x1A8 && sizeof(CTextElement) == 0x1B0,
              "layout must match the original");

} // namespace Front
