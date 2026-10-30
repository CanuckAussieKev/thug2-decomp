// Script commands (CFunctions) 0x005B2420-0x005B2FC0: face textures, changing textures, HSV / RGB (see
// face_texture_cfuncs.h).

#include "game/gfx/cag_cfuncs.h"
#include "game/gfx/face_texture_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/gfx/face_texture.h"
#include "game/gfx/texture_dict.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CArray;
using Script::CStruct;

constexpr uint32_t kChecksum = 0x21902065;        // "checksum"
constexpr uint32_t kCapturedTexture = 0xD7C087AF; // "captured_texture"
constexpr uint32_t kXOffset = 0xD83D589E;         // (name unknown) OffsetTexture: the x offset
constexpr uint32_t kYOffset = 0x14975800;         // "y_offset"
constexpr uint32_t kColour0 = 0xB7A78C53;         // (names unknown) Offset / PushTextureToPoint: the colour's bytes
constexpr uint32_t kColour1 = 0xDA7A68B8;
constexpr uint32_t kColour2 = 0xAA109C37;
constexpr uint32_t kColour3 = 0x3319CD8D;
constexpr uint32_t kAngle = 0xFF7EBAF6;           // "angle"
constexpr uint32_t kPushPoint = 0x485A0CDB;       // (name unknown) PushTextureToPoint: the point
constexpr uint32_t kAxis = 0x7AF07905;            // "Axis"
constexpr uint32_t kPushAmount = 0x88CF948C;      // (name unknown) PushTextureToPoint: how far
constexpr uint32_t kR = 0x93F60062;               // "r"
constexpr uint32_t kG = 0xFE2BE489;               // "g"
constexpr uint32_t kB = 0x8E411006;               // "b"
constexpr uint32_t kA = 0x174841BC;               // "a"
constexpr uint32_t kTintMode = 0xC164842A;        // (name unknown) ApplyRGBAToTexture: 1 = the second mode
constexpr uint32_t kHsv = 0xFB0E4249;             // "hsv"
constexpr uint32_t kRgb = 0xDECB67C7;             // "rgb"

// The texture dictionary (0x00700F1C) and its texture by checksum (0x0049F540; null if none).
void* TextureDictionary() { return GameVariable<void*>(0x00700F1C); }
void* FindTexture(uint32_t name) { return static_cast<Nx::CTexDict*>(TextureDictionary())->GetTexture(name); }

// The current profile's face texture (its appearance's +0x54, through the getter 0x00576E60 folded with others) - the
// skate module's 0x00564F80 with no params is the current profile.
void* FaceTexture()
{
    auto* profile = Mdl::Skate::Instance()->GetProfile(static_cast<CStruct*>(nullptr));
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(profile) + 0x54);
}

// A texture's methods (each a jump through its vtable).
template <typename R, typename... A>
R TextureCall(uint32_t slot, void* texture, A... args)
{
    // (the texture's virtual slot `slot`: the original goes through a jump stub for each)
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(texture))[slot])(texture, args...);
}

// A colour as the textures take it: four bytes (the first in the low byte).
uint32_t PackColour(int c0, int c1, int c2, int c3)
{
    return static_cast<uint8_t>(c0) | static_cast<uint8_t>(c1) << 8 | static_cast<uint8_t>(c2) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(c3)) << 24;
}

// The four optional colour components (0x80 each by default) and whether any was given (every one is read).
uint32_t ReadColour(CStruct* params, bool* given)
{
    int c[4] = {0x80, 0x80, 0x80, 0x80};
    bool any = params->GetInteger(kColour0, &c[0], 0);
    if (params->GetInteger(kColour1, &c[1], 0))
        any = true;
    if (params->GetInteger(kColour2, &c[2], 0))
        any = true;
    if (params->GetInteger(kColour3, &c[3], 0))
        any = true;
    *given = any;
    return PackColour(c[0], c[1], c[2], c[3]);
}

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A new integer array for the converted colour: 3 elements, or 4 with the source's fourth copied.
CArray* NewColourArray(CArray* source)
{
    CArray* memory = Script::NewArray();
    CArray* colour = memory ? memory->Init() : nullptr;
    if (source->m_size == 4) {
        colour->SetSizeAndType(4, Script::ESYMBOLTYPE_INTEGER);
        colour->SetElement(3, source->GetElement(3));
    } else {
        colour->SetSizeAndType(3, Script::ESYMBOLTYPE_INTEGER);
    }
    return colour;
}

} // namespace

// 0x005B2420: LoadFaceTextureFromProfile checksum=<name>: the texture dictionary given the current profile's face
// texture image (0x0049FA20) as `name`. Whether a texture was made.
bool __cdecl CFunc_LoadFaceTextureFromProfile(Script::CStruct* params, Script::CScript*)
{
    uint32_t name;
    params->GetChecksum(kChecksum, &name, 1);
    void* face = FaceTexture();
    int size = static_cast<Gfx::CFaceTexture*>(face)->GetImageSize();         // (0x4420)
    void* data = static_cast<Gfx::CFaceTexture*>(face)->GetImage();           // (its +0x3C)
    void* texture = static_cast<Nx::CTexDict*>(TextureDictionary())
                        ->LoadTextureFromBuffer(static_cast<const uint8_t*>(data), size, name, 1, 1, true, 1);
    return texture != nullptr;
}

// 0x005B2480: SetFaceTextureFromVideoCapture captured_texture=<name>: the current profile's face texture made from
// the captured texture: a 0x4420-byte image (zeroed) - the texture's header (+0x78), its palette (+0x74: header size
// / bytes per pixel entries, after 0x20 bytes) and its pixels (+0x64, +0x68 bytes) after it - copied in (0x00487420).
// The texture isn't checked for null. Always true.
bool __cdecl CFunc_SetFaceTextureFromVideoCapture(Script::CStruct* params, Script::CScript*)
{
    uint32_t name;
    params->GetChecksum(kCapturedTexture, &name, 1);
    void* face = FaceTexture();
    void* texture = FindTexture(name);
    uint8_t image[0x4420];
    std::memset(image, 0, sizeof(image));
    TextureCall<void>(30, texture, static_cast<void*>(image));
    int paletteSize = TextureCall<int>(28, texture);
    uint8_t bitsPerPixel = TextureCall<uint8_t>(38, texture);
    TextureCall<void>(29, texture, static_cast<void*>(image + 0x20), paletteSize / (bitsPerPixel >> 3));
    const void* pixels = TextureCall<const void*>(25, texture);
    int pixelSize = TextureCall<int>(26, texture);
    std::memcpy(image + 0x20 + paletteSize, pixels, pixelSize);
    static_cast<Gfx::CFaceTexture*>(face)->SetImage(image, pixelSize + paletteSize + 0x20);
    return true;
}

// 0x005B2570: OffsetTexture <texture> [0xD83D589E=x] [y_offset=y] [colour bytes]: the texture's image shifted
// (+0x48: x, y, whether a colour was given, the colour - 0x80 for each byte not given). Always true.
bool __cdecl CFunc_OffsetTexture(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(0u, &name, 0);
    void* texture = FindTexture(name);
    if (!texture)
        return true;
    int x = 0;
    int y = 0;
    params->GetInteger(kXOffset, &x, 0);
    params->GetInteger(kYOffset, &y, 0);
    bool given;
    uint32_t colour = ReadColour(params, &given);
    TextureCall<void>(18, texture, x, y, given, colour);
    return true;
}

// 0x005B26B0: RotateTexture <texture> angle=degrees: the texture's image rotated (+0x44: radians, -1, -1). Always
// true.
bool __cdecl CFunc_RotateTexture(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (both uninitialised in the original when missing)
    params->GetChecksum(0u, &name, 0);
    float angle = 0.0f;
    params->GetFloat(kAngle, &angle, 0);
    void* texture = FindTexture(name);
    if (texture) {
        // x87: degrees * (pi / 180, 0x00645464) in double, rounded once
        float radians = static_cast<float>(static_cast<double>(angle) * g_degreesToRadians);
        TextureCall<void>(17, texture, radians, -1.0f, -1.0f);
    }
    return true;
}

// 0x005B2720: PushTextureToPoint <texture> [0x485A0CDB=point] [Axis=n] [0x88CF948C=amount] [colour bytes]: the
// texture's image pushed towards a point (+0x58: point, axis, amount, whether a colour was given, the colour).
// Always true.
bool __cdecl CFunc_PushTextureToPoint(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(0u, &name, 0);
    void* texture = FindTexture(name);
    if (!texture)
        return true;
    int point = 0;
    int axis = 0;
    int amount = 0;
    params->GetInteger(kPushPoint, &point, 0);
    params->GetInteger(kAxis, &axis, 0);
    params->GetInteger(kPushAmount, &amount, 0);
    bool given;
    uint32_t colour = ReadColour(params, &given);
    TextureCall<void>(22, texture, point, axis, amount, given, colour);
    return true;
}

// 0x005B2880: ApplyRGBAToTexture <texture> r= g= b= [a=a (0x80)] [0xC164842A=mode]: the texture tinted (+0x30: the
// colour, whether the mode is 1). Always true.
bool __cdecl CFunc_ApplyRGBAToTexture(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(0u, &name, 0);
    void* texture = FindTexture(name);
    if (!texture)
        return true;
    int r = 0; // (r, g and b uninitialised in the original when missing)
    int g = 0;
    int b = 0;
    int a = 0x80;
    params->GetInteger(kR, &r, 0);
    params->GetInteger(kG, &g, 0);
    params->GetInteger(kB, &b, 0);
    params->GetInteger(kA, &a, 0);
    int mode = 0;
    params->GetInteger(kTintMode, &mode, 0);
    bool second = mode == 1;
    TextureCall<void>(12, texture, PackColour(r, g, b, a), second);
    return true;
}

// 0x005B2970: HSVtoRGB hsv=[h s v (alpha)]: rgb, the colour in RGB (0-128 each; the fourth element copied). Hue is
// taken in 60ths (wrapped into 0-6), saturation in 90ths (kept to 0.01-1), value in 60ths (kept to 0.01-1) - the
// value stays at the x87's precision until stored. Always true.
bool __cdecl CFunc_HSVtoRGB(Script::CStruct* params, Script::CScript* script)
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    CArray* hsv = nullptr;
    params->GetArray(kHsv, &hsv, 1);

    constexpr float kSixtieth = 0x1.111112p-6f; // 0x006463B8
    constexpr float kNinetieth = 0x1.6c16c2p-7f; // 0x00657CC4
    double hue = static_cast<int>(hsv->GetElement(0)) * static_cast<double>(kSixtieth);
    float h = static_cast<float>(hue);
    if (!(hue < 6.0)) {
        do
            hue -= 6.0;
        while (!(hue < 6.0));
        h = static_cast<float>(hue);
    }
    if (hue < 0.0) {
        do
            hue += 6.0;
        while (hue < 0.0);
        h = static_cast<float>(hue);
    }
    float s = static_cast<float>(static_cast<int>(hsv->GetElement(1)) * static_cast<double>(kNinetieth));
    if (!(s > 0.0f))
        s = 0.01f;
    else if (s > 1.0f)
        s = 1.0f;
    double v = static_cast<int>(hsv->GetElement(2)) * static_cast<double>(kSixtieth);
    if (!(v > 0.0))
        v = 0.01f;
    else if (v > 1.0)
        v = 1.0;

    int sector = Ftol(h);
    double f = h - static_cast<double>(sector);
    float p = static_cast<float>((1.0 - s) * v);
    float q = static_cast<float>((1.0 - s * f) * v);
    float t = static_cast<float>((1.0 - (1.0 - f) * s) * v);
    float value = static_cast<float>(v);
    switch (static_cast<uint32_t>(Ftol(static_cast<double>(sector)))) {
    case 0: r = value; g = t; b = p; break;
    case 1: r = q; g = value; b = p; break;
    case 2: r = p; g = value; b = t; break;
    case 3: r = p; g = q; b = value; break;
    case 4: r = t; g = p; b = value; break;
    case 5: r = value; g = p; b = q; break;
    default: break;
    }

    CArray* rgb = NewColourArray(hsv);
    rgb->SetElement(0, static_cast<uint32_t>(Ftol(static_cast<double>(r) * 128.0f)));
    rgb->SetElement(1, static_cast<uint32_t>(Ftol(static_cast<double>(g) * 128.0f)));
    rgb->SetElement(2, static_cast<uint32_t>(Ftol(static_cast<double>(b) * 128.0f)));
    script->m_params->AddArrayPointer(kRgb, rgb);
    return true;
}

// 0x005B2CD0: RGBtoHSV rgb=[r g b (alpha)]: hsv, the colour in HSV (hue 0-360, saturation 0-90, value 0-60; the
// fourth element copied). Always true.
bool __cdecl CFunc_RGBtoHSV(Script::CStruct* params, Script::CScript* script)
{
    float h = 0.0f;
    CArray* rgbArray = nullptr;
    params->GetArray(kRgb, &rgbArray, 1);
    constexpr float kScale = 1.0f / 128.0f; // 0x0064A2FC
    float r = static_cast<float>(static_cast<int>(rgbArray->GetElement(0)) * static_cast<double>(kScale));
    float g = static_cast<float>(static_cast<int>(rgbArray->GetElement(1)) * static_cast<double>(kScale));
    float b = static_cast<float>(static_cast<int>(rgbArray->GetElement(2)) * static_cast<double>(kScale));
    float max = r;
    float min = r;
    if (g < r)
        min = g;
    if (g > max)
        max = g;
    if (b < min)
        min = b;
    if (b > max)
        max = b;
    float delta = static_cast<float>(static_cast<double>(max) - min);

    float s;
    double hue;
    if (max == 0.0f) {
        hue = 0.0;
        s = 0.0f;
    } else {
        s = static_cast<float>(static_cast<double>(delta) / max);
        if (s == 0.0f)
            hue = 0.0;
        else if (r == max)
            hue = (static_cast<double>(g) - b) / delta;
        else if (g == max)
            hue = (static_cast<double>(b) - r) / delta + 2.0;
        else if (b == max)
            hue = (static_cast<double>(r) - g) / delta + 4.0;
        else
            hue = h;
    }
    hue *= 60.0f;
    h = static_cast<float>(hue);
    if (hue < 0.0)
        h = static_cast<float>(static_cast<double>(h) + 360.0f);
    if (h > 360.0f)
        h = static_cast<float>(static_cast<double>(h) - 360.0f);

    CArray* hsv = NewColourArray(rgbArray);
    hsv->SetElement(0, static_cast<uint32_t>(Ftol(h)));
    hsv->SetElement(1, static_cast<uint32_t>(Ftol(static_cast<double>(s) * 90.0f)));
    hsv->SetElement(2, static_cast<uint32_t>(Ftol(static_cast<double>(max) * 60.0f)));
    script->m_params->AddArrayPointer(kHsv, hsv);
    return true;
}

} // namespace CFuncs

REPLACE(0x005B2420, CFuncs::CFunc_LoadFaceTextureFromProfile);
REPLACE(0x005B2480, CFuncs::CFunc_SetFaceTextureFromVideoCapture);
REPLACE(0x005B2570, CFuncs::CFunc_OffsetTexture);
REPLACE(0x005B26B0, CFuncs::CFunc_RotateTexture);
REPLACE(0x005B2720, CFuncs::CFunc_PushTextureToPoint);
REPLACE(0x005B2880, CFuncs::CFunc_ApplyRGBAToTexture);
REPLACE(0x005B2970, CFuncs::CFunc_HSVtoRGB);
REPLACE(0x005B2CD0, CFuncs::CFunc_RGBtoHSV);
