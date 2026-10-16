// Bitmap font text: measuring and drawing, with PartyMod's button glyphs built in.

#include "game/gfx/text.h"

#include <bit>
#include <cstdint>

#include "game/core/str.h"
#include "hook/game.h"
#include "hook/hook.h"

// PartyMod (src/partymod/glyph.c): which glyph a button tag stands for, and whether it's drawn as a glyph or as
// the key's name - it depends on whether a keyboard is in use and its PreferGlyphs setting.
extern "C" uint32_t dehexifyDigitWrapper(uint8_t* button);
extern "C" uint8_t __cdecl shouldUseGlyph(uint8_t index);

namespace Nx {

namespace {

constexpr float Bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kEffectScale = Bits(0x38800000); // 0x006487C8, 1 / 16384
constexpr float kMinusHalf = Bits(0xBF000000);   // 0x0064642C

// The 640x480 -> back buffer conversion (set up by InitDirect3D).
float ScreenScaleX() { return GameVariable<float>(0x00786D80); }
float ScreenScaleY() { return GameVariable<float>(0x00786D84); }
int ScreenOffsetX() { return GameVariable<int>(0x00786D88); }
int ScreenOffsetY() { return GameVariable<int>(0x00786D8C); }

// The text vertex buffer (BeginDraw points it at 0x00779FD8) and how many vertices it holds.
TextVertex*& VertexBuffer() { return GameVariable<TextVertex*>(0x00780FE8); }
uint32_t& VertexCount() { return GameVariable<uint32_t>(0x00780FE4); }
constexpr uint32_t kVertexLimit = 0x400;
// The font button glyphs come from, and the font whose texture the buffer is drawn with.
SFont* ButtonFont() { return GameVariable<SFont*>(0x00780FDC); }
SFont*& TextureFont() { return GameVariable<SFont*>(0x00780FE0); }

// A button tag's digit through the meta button map (0x0048D6A0, also used by 0x004AF8C0; not rewritten yet).
char MetaButton(const char* digit) { return GameFunction<char(__cdecl*)(const char*)>(0x0048D6A0)(digit); }
// The name of the key a button is mapped to (not rewritten yet).
const char* ButtonKeyName(int button) { return GameFunction<const char*(__cdecl*)(int)>(0x005BCD50)(button); }

// "\b<d>" / "\m<d>" (the pointer at d): which button.
int ButtonIndex(char tag, const char* digit)
{
    if (tag == 'M' || tag == 'm') {
        char mapped = MetaButton(digit);
        return Str::DehexifyDigit(&mapped);
    }
    // PartyMod (its call at 0x004CED4B / 0x004CFF0C): with a controller (or PreferGlyphs) the PC's explicit
    // button tags are turned back into the controller's. The original: Str::DehexifyDigit(digit).
    return static_cast<int>(dehexifyDigitWrapper(reinterpret_cast<uint8_t*>(const_cast<char*>(digit))));
}

// Whether a button is drawn as a glyph of the button font (otherwise its key's name is spelled out).
bool UseButtonGlyph(int button)
{
    // PartyMod (its test at 0x004CED6D / 0x004CFF36). The original, where 4-7 and 10-13 are the glyphs:
    //   uint8_t b = static_cast<uint8_t>(button);
    //   return (b >= 4 && b <= 7) || (b >= 10 && b <= 13);
    return shouldUseGlyph(static_cast<uint8_t>(button)) == 1;
}

// RGBA bytes (as stored) -> the vertex's ARGB: red and blue swap.
uint32_t VertexColor(uint32_t c) { return (c & 0xFF) << 16 | (c & 0xFF00FF00) | (c >> 16 & 0xFF); }

// The CRT's _ftol (0x006236FC): truncation; the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The x87's own FSIN / FCOS, which the original uses (the CRT's sin/cos round differently). A result stored as
// a float is rounded once, from the x87's extended precision.
float X87Cos(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
float X87Sin(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
// sin(angle) * value, with the sine kept at extended precision as the original keeps it on the x87 stack.
double X87SinTimes(double angle, float value)
{
    double result;
    __asm {
        fld angle
        fsin
        fmul value
        fstp result
    }
    return result;
}

} // namespace

// 0x004CECB0
void SFont::QueryString(const char* text, float* outWidth, float* outHeight)
{
    int total = 0;
    const char* p = text;
    for (;;) {
        const SChar* glyph = nullptr; // set by an escape that draws a glyph in place of the next character
        const SFont* font = this;
        bool counted = true;
        bool done = false;
        while (*p == '\\' && !done) {
            char tag = *++p;
            if (tag == '\\')
                break; // "\\": the second backslash is a character
            switch (tag) {
            case 'S':
            case 's':
                ++p;
                glyph = &chars[specialMap[Str::DehexifyDigit(p)]];
                done = true;
                break;
            case 'B':
            case 'b':
            case 'M':
            case 'm': {
                ++p;
                int button = ButtonIndex(tag, p);
                if (UseButtonGlyph(button)) {
                    font = ButtonFont();
                    glyph = &font->chars[font->specialMap[button]];
                } else {
                    // the key's name, in this font (the map indexed by signed chars)
                    const uint8_t* nameMap = map;
                    for (const char* name = ButtonKeyName(button); *name; ++name)
                        total += chars[nameMap[static_cast<signed char>(*name)]].w + charSpacing;
                    counted = false; // not the digit
                }
                done = true;
                break;
            }
            case 'C':
            case 'c':
            case 'U':
            case 'u':
                p += 2;
                break;
            case 'L': // (only capital: "\l" is drawn as an l)
                ++p;
                break;
            default:
                break; // the letter is a character (the table's '\\' case can't be reached)
            }
        }
        if (*p == '\0')
            break;
        if (counted) {
            int w;
            if (glyph)
                w = glyph->w;
            else if (*p == ' ')
                w = spaceSpacing;
            else
                w = font->chars[font->map[static_cast<uint8_t>(*p)]].w;
            total += w + charSpacing;
        }
        ++p;
    }
    *outWidth = static_cast<float>(total);
    *outHeight = static_cast<float>(static_cast<double>(height));
}

// 0x004CFB20
void SText::DrawKeyChar(char c, float* penX, float penY)
{
    double scaleX = ScreenScaleX();
    double scaleY = ScreenScaleY();
    float spacing = static_cast<float>(font->charSpacing * static_cast<double>(xscale));
    if (c == ' ') {
        *penX = static_cast<float>((font->spaceSpacing * static_cast<double>(xscale) + spacing) * scaleX + *penX);
        return;
    }
    const SChar* glyph = &font->chars[font->map[static_cast<uint8_t>(c)]];
    // (x87: top and bottom stay at double precision until stored)
    double top = static_cast<double>(font->base - glyph->baseline) * yscale * scaleY + penY;
    float left = *penX;
    float right = static_cast<float>(glyph->w * scaleX * xscale + left);
    double bottom = glyph->h * scaleY * yscale + top;
    uint32_t vertexColor = VertexColor(color);
    TextVertex* v = VertexBuffer() + VertexCount();
    v[0] = {left, static_cast<float>(top), z, 1.0f, vertexColor, glyph->u0, glyph->v0};
    v[1] = {left, static_cast<float>(bottom), z, 1.0f, vertexColor, glyph->u0, glyph->v1};
    v[2] = {right, static_cast<float>(bottom), z, 1.0f, vertexColor, glyph->u1, glyph->v1};
    v[3] = {right, static_cast<float>(top), z, 1.0f, vertexColor, glyph->u1, glyph->v0};
    VertexCount() += 4;
    if (VertexCount() >= kVertexLimit) {
        EndDraw();
        BeginDraw();
    }
    *penX = static_cast<float>(scaleX * spacing + right);
}

// 0x004CFD00
void SText::Draw()
{
    double scaleX = ScreenScaleX();
    double scaleY = ScreenScaleY();
    double offsetX = ScreenOffsetX();
    double offsetY = ScreenOffsetY();
    // Screen positions (x87: each sum rounded to float once, when stored).
    float posX = static_cast<float>(scaleX * x + offsetX);
    float posY = static_cast<float>(scaleY * y + offsetY);
    float pivotScreenX = static_cast<float>((static_cast<double>(pivotX) + x) * scaleX + offsetX);
    float pivotScreenY = static_cast<float>((static_cast<double>(pivotY) + y) * scaleY + offsetY);
    float charSpacing = static_cast<float>(font->charSpacing * static_cast<double>(xscale));
    float spaceSpacing = static_cast<float>(font->spaceSpacing * static_cast<double>(xscale));
    if (xscale == 0.0f || yscale == 0.0f) // (0x0066D520)
        return;
    float cosA = X87Cos(angle);
    float sinA = X87Sin(angle);
    uint32_t vertexColor = VertexColor(color);
    int effect = 0; // byte offset of this character's effect record
    const char* p = text;
    for (;;) {
        TextVertex* v = VertexBuffer() + VertexCount();
        SFont* charFont = font;
        const SChar* glyph = nullptr;
        bool drawn = true;
        bool done = false;
        while (*p == '\\' && !done) {
            char tag = *++p;
            switch (tag) {
            case '\\':
                done = true; // the second backslash is drawn
                break;
            case 'C':
            case 'c': {
                ++p;
                int index = Str::DehexifyDigit(p);
                ++p;
                // colour 0, or any colour while the text's is fixed, is the text's own
                vertexColor = VertexColor(index != 0 && !fixedColor ? font->Color(index) : color);
                break;
            }
            case 'S':
            case 's':
                ++p;
                glyph = &font->chars[font->specialMap[Str::DehexifyDigit(p)]];
                done = true;
                break;
            case 'B':
            case 'b':
            case 'M':
            case 'm': {
                ++p;
                int button = ButtonIndex(tag, p);
                if (UseButtonGlyph(button)) {
                    // drawn with the button font's texture: draw what's buffered first
                    SFont* buttons = ButtonFont();
                    glyph = &buttons->chars[buttons->specialMap[button]];
                    charFont = buttons;
                    EndDraw();
                    BeginDraw();
                    v = VertexBuffer() + VertexCount();
                    TextureFont() = buttons;
                } else {
                    EndDraw();
                    BeginDraw();
                    v = VertexBuffer() + VertexCount();
                    drawn = false; // not the digit
                    for (const char* name = ButtonKeyName(button); *name; ++name)
                        DrawKeyChar(*name, &posX, posY);
                }
                done = true;
                break;
            }
            case 'L': // (only capital: "\l" is drawn as an l)
                ++p;
                break;
            case 'U':
            case 'u':
                p += 2;
                break;
            default:
                break; // the letter is drawn
            }
        }
        if (*p == '\0')
            return;
        if (drawn) {
            if (*p == ' ' && !glyph) {
                posX = static_cast<float>((static_cast<double>(spaceSpacing) + charSpacing) * scaleX + posX);
                ++p;
                effect += sizeof(STextEffect);
                continue;
            }
            if (!glyph)
                glyph = &charFont->chars[charFont->map[static_cast<uint8_t>(*p)]];
            float top = static_cast<float>(static_cast<double>(charFont->base - glyph->baseline) * yscale * scaleY + posY);
            const STextEffect* e =
                effects ? reinterpret_cast<const STextEffect*>(reinterpret_cast<const uint8_t*>(effects) + effect) : nullptr;
            float right, bottom;
            if (e) {
                int w = Ftol(e->xscale * static_cast<double>(kEffectScale) * glyph->w * xscale * scaleX);
                right = static_cast<float>(static_cast<double>(w) + e->x + posX);
                int h = Ftol(e->yscale * static_cast<double>(kEffectScale) * glyph->h * yscale * scaleY);
                bottom = static_cast<float>(static_cast<double>(h) + e->y + top);
            } else {
                int w = Ftol(static_cast<double>(glyph->w) * xscale * scaleX);
                right = static_cast<float>(static_cast<double>(w) + posX);
                int h = Ftol(static_cast<double>(glyph->h) * yscale * scaleY);
                bottom = static_cast<float>(static_cast<double>(h) + top);
            }

            // The corners, relative to the pivot (whole pixels), rotated by the text's angle. The left and top
            // offsets go through floats; the right and bottom ones don't (the same unless they're huge).
            int pivotYi = Ftol(pivotScreenY);
            int topRel = Ftol(static_cast<double>(top) - pivotYi);
            int pivotXi = Ftol(pivotScreenX);
            int leftRel = Ftol(static_cast<double>(posX) - pivotXi);
            double l = static_cast<float>(leftRel);
            double t = static_cast<float>(topRel);
            int x0 = Ftol(l * cosA - t * sinA) + pivotXi; // left, top
            int y0 = Ftol(t * cosA + l * sinA) + pivotYi;
            double r = Ftol(static_cast<double>(right) - pivotXi);
            int x1 = Ftol(r * cosA - t * sinA) + pivotXi; // right, top
            int y1 = Ftol(t * cosA + r * sinA) + pivotYi;
            double b = Ftol(static_cast<double>(bottom) - pivotYi);
            l = leftRel;
            int x2 = Ftol(l * cosA - b * sinA) + pivotXi; // left, bottom
            int y2 = Ftol(b * cosA + l * sinA) + pivotYi;
            int x3 = Ftol(r * cosA - b * sinA) + pivotXi; // right, bottom
            int y3 = Ftol(b * cosA + r * sinA) + pivotYi;

            if (e && e->rotation != 0) {
                // The character's own rotation about its centre - as the original computes it: the centre is
                // negated and offset by the pivot's x on both axes, then turned by the text's angle, and the
                // corners are turned by (angle - 90 degrees) mirrored. Kept as it is.
                double a = e->rotation * static_cast<double>(kEffectScale);
                float cosR = X87Cos(a);
                int cy = Ftol((static_cast<double>(bottom) + top) * kMinusHalf);
                double oy = static_cast<float>(-pivotXi - cy);
                int cx = Ftol((static_cast<double>(right) + posX) * kMinusHalf);
                double ox = static_cast<float>(-pivotXi - cx);
                int centreX = Ftol(ox * cosA - oy * sinA) + pivotXi;
                int centreY = Ftol(oy * cosA + ox * sinA) + pivotXi;
                auto turn = [&](int& px, int& py) {
                    float dx = static_cast<float>(px - centreX);
                    float dy = static_cast<float>(py - centreY);
                    px = Ftol(X87SinTimes(a, dx) - static_cast<double>(dy) * cosR) + centreX;
                    py = Ftol(X87SinTimes(a, dy) + static_cast<double>(dx) * cosR) + centreY;
                };
                turn(x0, y0);
                turn(x1, y1);
                turn(x2, y2);
                turn(x3, y3);
            }

            auto at = [](int i) { return static_cast<float>(i); };
            v[0] = {at(x0), at(y0), z, 1.0f, vertexColor, glyph->u0, glyph->v0};
            v[1] = {at(x2), at(y2), z, 1.0f, vertexColor, glyph->u0, glyph->v1};
            v[2] = {at(x3), at(y3), z, 1.0f, vertexColor, glyph->u1, glyph->v1};
            v[3] = {at(x1), at(y1), z, 1.0f, vertexColor, glyph->u1, glyph->v0};
            VertexCount() += 4;
            if (VertexCount() >= kVertexLimit) {
                EndDraw();
                BeginDraw();
            }
            posX = static_cast<float>(scaleX * charSpacing + right);
            if (charFont != font) { // back to this font's texture
                EndDraw();
                BeginDraw();
            }
        }
        ++p;
        effect += sizeof(STextEffect);
    }
}

} // namespace Nx

REPLACE(0x004CECB0, &Nx::SFont::QueryString);
REPLACE(0x004CFB20, &Nx::SText::DrawKeyChar);
REPLACE(0x004CFD00, &Nx::SText::Draw);
