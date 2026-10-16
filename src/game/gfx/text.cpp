// Bitmap font text: measuring and drawing, with the PC port's button glyphs (game/gfx/glyphs.h) built in.
// Contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md (the PC port's changes in it).

#include "game/gfx/text.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/file/file.h"
#include "game/gfx/glyphs.h"
#include "hook/game.h"
#include "hook/hook.h"

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
    // PC port (its call at 0x004CED4B / 0x004CFF0C): with a controller (or PreferGlyphs) the PC's
    // explicit button tags are turned back into the controller's. The original: Str::DehexifyDigit(digit).
    return static_cast<int>(ButtonForTag(reinterpret_cast<uint8_t*>(const_cast<char*>(digit))));
}

// Whether a button is drawn as a glyph of the button font (otherwise its key's name is spelled out).
bool UseButtonGlyph(int button)
{
    // PC port (its test at 0x004CED6D / 0x004CFF36). The original, where 4-7 and 10-13 are the glyphs:
    //   uint8_t b = static_cast<uint8_t>(button);
    //   return (b >= 4 && b <= 7) || (b >= 10 && b <= 13);
    return ShouldUseGlyph(static_cast<uint8_t>(button)) == 1;
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

namespace {

// The D3D9 device, its methods by vtable offset.
void** Device() { return GameVariable<void**>(0x007D69E0); }
template <typename F>
F DeviceMethod(int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(Device()))[offset / 4]);
}
void SetTextureStageState(int stage, int type, int value)
{
    DeviceMethod<long(__stdcall*)(void*, int, int, int)>(0x10C)(Device(), stage, type, value);
}
template <typename F>
F TextureMethod(void* texture, int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(texture))[offset / 4]);
}

// The renderer's state calls (not rewritten yet; cdecl).
void SetVertexShader(int shader, int unknown) { GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(shader, unknown); }
void SetVertexFormat(uint32_t fvf) { GameFunction<void(__cdecl*)(uint32_t)>(0x004D9DB0)(fvf); }
void SetTexture(int stage, void* texture, uint32_t unknown)
{
    GameFunction<void(__cdecl*)(int, void*, uint32_t)>(0x004DA730)(stage, texture, unknown);
}

// The vertices' store (BeginDraw points the buffer at it) and the quads' triangles as indices into it (made the
// first time they're drawn).
TextVertex* VertexStore() { return reinterpret_cast<TextVertex*>(0x00779FD8); }
uint16_t* QuadIndices() { return reinterpret_cast<uint16_t*>(0x00780FF0); }
bool& QuadIndicesMade() { return GameVariable<bool>(0x00781BF0); }
constexpr int kQuads = 0x100;

// The loaded fonts (through SFont::next).
SFont*& FontList() { return GameVariable<SFont*>(0x00780FD8); }

uint16_t U16(const uint8_t* p)
{
    uint16_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}
uint32_t U32(const uint8_t* p)
{
    uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

} // namespace

// 0x004CEED0
SText* SText::Construct(float priority)
{
    GameFunction<void*(__thiscall*)(SText*, float, bool)>(0x004DFA70)(this, priority, true);
    vtable = reinterpret_cast<void**>(0x0064A89C);
    return this;
}

// 0x004CEEF0
void SText::DestroyBody()
{
    vtable = reinterpret_cast<void**>(0x0064A89C);
    GameFunction<void(__thiscall*)(SText*)>(0x004DFAA0)(this);
}

// 0x004CFAF0
SText* SText::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004CEF00
void SText::ResetVertices() { VertexBuffer() = VertexStore(); }

// 0x004CEF10
void SText::DrawVertices()
{
    if (!QuadIndicesMade()) {
        for (int quad = 0; quad < kQuads; ++quad) {
            uint16_t first = static_cast<uint16_t>(quad * 4);
            uint16_t* triangles = QuadIndices() + quad * 6;
            triangles[0] = first;
            triangles[1] = static_cast<uint16_t>(first + 1);
            triangles[2] = static_cast<uint16_t>(first + 2);
            triangles[3] = first;
            triangles[4] = static_cast<uint16_t>(first + 2);
            triangles[5] = static_cast<uint16_t>(first + 3);
        }
        QuadIndicesMade() = true;
    }
    int count = static_cast<int>(VertexCount());
    if (count <= 0)
        return;
    SFont* textureFont = TextureFont() ? TextureFont() : font;
    SetVertexShader(0, 0);
    SetTextureStageState(0, 1, 5); // colour: texture x diffuse, doubled
    SetTextureStageState(0, 2, 2);
    SetTextureStageState(0, 3, 1);
    SetTextureStageState(0, 4, 5); // alpha: the same
    SetTextureStageState(0, 5, 2);
    SetTextureStageState(0, 6, 1);
    SetTextureStageState(1, 1, 1); // stage 1 off
    SetVertexFormat(0x144);        // position (pre-transformed), colour, one texture coordinate
    SetTexture(0, textureFont->texture, textureFont->unknown17C);
    DeviceMethod<long(__stdcall*)(void*, int, int)>(0xE4)(Device(), 22, 1); // D3DRS_CULLMODE: none
    if (!GameVariable<bool>(0x007D1EC0)) { // (rendering not skipped)
        ++GameVariable<int>(0x00786EBC);            // draw calls
        GameVariable<int>(0x00786EC0) += count / 2; // "triangles"
        using DrawIndexedPrimitiveUP = long(__stdcall*)(void*, int, unsigned int, unsigned int, unsigned int, const void*,
                                                        int, const void*, unsigned int);
        DeviceMethod<DrawIndexedPrimitiveUP>(0x150)(Device(), 4 /* triangle list */, 0, count, count / 2, QuadIndices(),
                                                    0x65 /* 16-bit indices */, VertexStore(), sizeof(TextVertex));
    }
    SetTextureStageState(0, 1, 4); // colour and alpha back to texture x diffuse
    SetTextureStageState(0, 4, 4);
    VertexCount() = 0;
    TextureFont() = nullptr;
}

// 0x004CF0F0. The file: a header (+4 the character map format, +8 the number of glyphs, +0xC the height, +0x10
// the base), the glyphs' character map entries (6 bytes each), the texture's header (+4 width, +6 height, 16-bit),
// its pixels (one byte each, rounded up to 4), its palette (256 RGBA, PS2 alpha: 0x80 = opaque), and the glyphs'
// rectangles in the texture (after 4 bytes: x, y, width, height, 16-bit).
SFont* SFont::Load(const char* name, bool fromMemory)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(name); // (from memory: the next bytes)
    File::Handle file = nullptr;
    if (!fromMemory) {
        char path[128]; // (the original strcpy/strcats into it: a longer name overflows; here it's cut short)
        std::snprintf(path, sizeof(path), "fonts/%s.fnt.xbx", name);
        file = File::Open(path, "rb");
    }
    // The next size * count bytes into `to` (as the original reads them: the header as 0x14 of 1 byte, the rest
    // as one block each; from memory, the glyph rectangles - read last - don't move on).
    auto read = [&](void* to, int size, int count, bool advance) {
        if (fromMemory) {
            std::memcpy(to, data, static_cast<size_t>(size) * count);
            if (advance)
                data += size * count;
        } else {
            File::Read(to, size, count, file);
        }
    };
    uint8_t buffer[0x808]; // (the original's: the tables of more than 256 glyphs overflow it)

    auto* font = static_cast<SFont*>(Mem::New(sizeof(SFont)));
    if (font)
        std::memset(font, 0, sizeof(SFont));
    read(buffer, 1, 0x14, true);
    font->format = buffer[4];
    int count = static_cast<int>(U32(buffer + 8));
    font->height = U32(buffer + 0xC);
    font->base = U32(buffer + 0x10);
    std::memset(font->map, 0, sizeof(font->map));
    std::memset(font->specialMap, 0, sizeof(font->specialMap));
    font->chars = static_cast<SChar*>(Mem::New(static_cast<uint32_t>(count) * sizeof(SChar)));

    // The character map: which glyph each character (and special glyph) is.
    read(buffer, count * 6, 1, true);
    uint8_t* fontBytes = reinterpret_cast<uint8_t*>(font);
    if (font->format == 1) {
        // Each glyph: its baseline, then a character (>= 0) or a special glyph (-1 - index; past the special map
        // for indexes over 0x1F, as in the original).
        for (int i = 0; i < count; ++i) {
            const uint8_t* entry = buffer + 6 * i;
            font->chars[i].baseline = U16(entry);
            int16_t code = static_cast<int16_t>(U16(entry + 2));
            if (code >= 0)
                font->map[code & 0xFF] = static_cast<uint8_t>(i);
            else
                fontBytes[offsetof(SFont, specialMap) + static_cast<uint8_t>(0xFF - (code & 0xFF))] = static_cast<uint8_t>(i);
        }
    } else if (font->format == 2) {
        // Each glyph: its baseline, then up to four characters (0xFF ends them early).
        for (int i = 0; i < count; ++i) {
            const uint8_t* entry = buffer + 6 * i;
            font->chars[i].baseline = U16(entry);
            const uint8_t* codes = entry + 2;
            font->map[codes[0]] = static_cast<uint8_t>(i);
            for (int c = 1; c < 4 && codes[c] != 0xFF; ++c)
                font->map[codes[c]] = static_cast<uint8_t>(i);
        }
    }
    // Characters and special glyphs without one get the default glyph (glyph 0 counts as none, but for 'A'/'a').
    if (font->specialMap[0x1F]) {
        for (int c = 0; c < 256; ++c) {
            if (!font->map[c] && c != 'A' && c != 'a')
                font->map[c] = font->specialMap[0x1F];
            if (c < 0x1F && !font->specialMap[c])
                font->specialMap[c] = font->specialMap[0x1F];
        }
    }

    // The texture: created on the device, its palette's colours (RGBA, PS2 alpha) made ARGB with alpha doubled
    // (0x80 and over: opaque), and the pixels written through it (the rows packed: the pitch isn't used).
    read(buffer, 0x10, 1, true);
    uint32_t width = U16(buffer + 4);
    uint32_t height = U16(buffer + 6);
    using CreateTexture = long(__stdcall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void**, void*);
    if (DeviceMethod<CreateTexture>(0x5C)(Device(), width, height, 1, 0, 21 /* A8R8G8B8 */, 1 /* managed */,
                                         &font->texture, nullptr) != 0)
        return nullptr;
    uint32_t size = (height * width + 3) & ~3u;
    auto* indices = static_cast<uint8_t*>(Mem::New(size));
    uint32_t palette[256];
    read(indices, static_cast<int>(size), 1, true);
    read(palette, 0x400, 1, true);
    for (uint32_t& color : palette) {
        uint32_t argb = (color >> 16 & 0xFF) | (color & 0xFF) << 16 | (color & 0xFF00FF00);
        uint32_t alpha = argb >> 24;
        alpha = alpha >= 0x80 ? 0xFF : alpha << 1;
        color = alpha << 24 | (argb & 0xFFFFFF);
    }
    auto* pixels = static_cast<uint32_t*>(Mem::New(size * 4));
    // palette indices -> colours (not rewritten yet)
    GameFunction<void(__cdecl*)(uint32_t*, const uint8_t*, uint32_t, const uint32_t*)>(0x005F45E0)(pixels, indices, size,
                                                                                                  palette);
    struct LockedRect {
        int pitch;
        void* bits;
    } locked;
    if (TextureMethod<long(__stdcall*)(void*, uint32_t, LockedRect*, const void*, uint32_t)>(font->texture, 0x4C)(
            font->texture, 0, &locked, nullptr, 0) != 0)
        return nullptr;
    std::memcpy(locked.bits, pixels, size * 4);
    TextureMethod<long(__stdcall*)(void*, uint32_t)>(font->texture, 0x50)(font->texture, 0);
    Mem::Free(indices);
    Mem::Free(pixels);

    // The glyphs' rectangles: texture coordinates (the right/bottom ones from the unrounded left/top) and sizes.
    read(buffer, count * 8 + 4, 1, false);
    double textureWidth = static_cast<int>(width);
    double textureHeight = static_cast<int>(height);
    for (int i = 0; i < count; ++i) {
        const uint8_t* rect = buffer + 4 + 8 * i;
        SChar& glyph = font->chars[i];
        glyph.w = U16(rect + 4);
        glyph.h = U16(rect + 6);
        double u0 = U16(rect) / textureWidth;
        double v0 = U16(rect + 2) / textureHeight;
        glyph.u0 = static_cast<float>(u0);
        glyph.v0 = static_cast<float>(v0);
        glyph.u1 = static_cast<float>(glyph.w / textureWidth + u0);
        glyph.v1 = static_cast<float>(glyph.h / textureHeight + v0);
    }

    font->next = FontList();
    FontList() = font;
    if (!fromMemory)
        File::Close(file);
    font->spaceSpacing = static_cast<int16_t>(font->chars[font->map['I']].w);
    return font;
}

// 0x004CFA80
void SFont::Unload(SFont* font)
{
    if (FontList() == font) {
        FontList() = font->next;
    } else {
        for (SFont* p = FontList(); p->next; p = p->next) {
            if (p->next == font) {
                p->next = font->next;
                break;
            }
        }
    }
    Mem::Free(font->chars);
    Mem::Free(font);
}

} // namespace Nx

REPLACE(0x004CECB0, &Nx::SFont::QueryString);
REPLACE(0x004CFB20, &Nx::SText::DrawKeyChar);
REPLACE(0x004CFD00, &Nx::SText::Draw);
REPLACE(0x004CEED0, &Nx::SText::Construct);
REPLACE(0x004CEEF0, &Nx::SText::DestroyBody);
REPLACE(0x004CFAF0, &Nx::SText::Destroy);
REPLACE(0x004CEF00, &Nx::SText::ResetVertices);
REPLACE(0x004CEF10, &Nx::SText::DrawVertices);
REPLACE(0x004CF0F0, Nx::SFont::Load);
REPLACE(0x004CFA80, Nx::SFont::Unload);
