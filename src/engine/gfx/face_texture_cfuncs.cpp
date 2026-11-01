// Script commands (CFunctions) 0x005B2420-0x005B2FC0: face textures, changing textures, HSV / RGB (see
// face_texture_cfuncs.h).

#include "engine/gfx/cag_cfuncs.h"
#include "engine/gfx/face_texture_cfuncs.h"
#include "engine/gfx/engine_texture.h"

#include <cstdint>
#include <cstring>

#include "engine/gfx/face_texture.h"
#include "engine/gfx/texture_dict.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/model_appearance.h"

namespace commands {

namespace {

using script::Array;
using script::Struct;

constexpr uint32_t kChecksum = 0x21902065;        // "checksum"
constexpr uint32_t kCapturedTexture = 0xD7C087AF; // "captured_texture"
constexpr uint32_t kXOffset = 0xD83D589E;         // (name unknown) OffsetTexture: the x offset
constexpr uint32_t kYOffset = 0x14975800;         // "y_offset"
constexpr uint32_t kColour0 = 0xB7A78C53;         // (names unknown) Offset / PushTextureToPoint: the colour's bytes
constexpr uint32_t kColour1 = 0xDA7A68B8;
constexpr uint32_t kColour2 = 0xAA109C37;
constexpr uint32_t kColour3 = 0x3319CD8D;
constexpr uint32_t kAngle = 0xFF7EBAF6;      // "angle"
constexpr uint32_t kPushPoint = 0x485A0CDB;  // (name unknown) PushTextureToPoint: the point
constexpr uint32_t kAxis = 0x7AF07905;       // "Axis"
constexpr uint32_t kPushAmount = 0x88CF948C; // (name unknown) PushTextureToPoint: how far
constexpr uint32_t kR = 0x93F60062;          // "r"
constexpr uint32_t kG = 0xFE2BE489;          // "g"
constexpr uint32_t kB = 0x8E411006;          // "b"
constexpr uint32_t kA = 0x174841BC;          // "a"
constexpr uint32_t kTintMode = 0xC164842A;   // (name unknown) ApplyRGBAToTexture: 1 = the second mode
constexpr uint32_t kHsv = 0xFB0E4249;        // "hsv"
constexpr uint32_t kRgb = 0xDECB67C7;        // "rgb"

// The texture dictionary (0x00700F1C) and its texture by checksum (0x0049F540; null if none).
void* texture_bank() { return reinterpret_cast<void*>(render::g_game_texture_dict); }
render::BitmapTexture* find_texture(uint32_t name)
{
    return static_cast<render::TextureSet*>(texture_bank())->fetch_skin(name);
}

// The current profile's face texture (its appearance's +0x54, through the getter 0x00576E60 folded with others) - the
// skate module's 0x00564F80 with no params is the current profile.
void* get_face_texture()
{
    auto* profile = modules::Skate::sole()->fetch_account(static_cast<Struct*>(nullptr));
    return profile->appearance()->face_skin_;
}

// A colour as the textures take it: four bytes (the first in the low byte).
uint32_t pack_colour(int c0, int c1, int c2, int c3)
{
    return static_cast<uint8_t>(c0) | static_cast<uint8_t>(c1) << 8 | static_cast<uint8_t>(c2) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(c3)) << 24;
}

// The four optional colour components (0x80 each by default) and whether any was given (every one is read).
uint32_t read_colour(Struct* params, bool* given)
{
    int c[4] = {0x80, 0x80, 0x80, 0x80};
    bool any = params->fetch_int_value(kColour0, &c[0], 0);
    if (params->fetch_int_value(kColour1, &c[1], 0))
        any = true;
    if (params->fetch_int_value(kColour2, &c[2], 0))
        any = true;
    if (params->fetch_int_value(kColour3, &c[3], 0))
        any = true;
    *given = any;
    return pack_colour(c[0], c[1], c[2], c[3]);
}

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A new integer array for the converted colour: 3 elements, or 4 with the source's fourth copied.
Array* new_colour_array(Array* source)
{
    Array* memory = script::alloc_script_array();
    Array* colour = memory ? new (memory) script::Array : nullptr;
    if (source->extent_ == 4) {
        colour->configure_size_kind(4, script::kQbvalInteger);
        colour->set_element(3, source->fetch_part(3));
    } else {
        colour->configure_size_kind(3, script::kQbvalInteger);
    }
    return colour;
}

} // namespace

// 0x005B2420: LoadFaceTextureFromProfile checksum=<name>: the texture dictionary given the current profile's face
// texture image (0x0049FA20) as `name`. Whether a texture was made.
bool cfunc_load_face_texture_from_profile(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kChecksum, &name, 1);
    void* face = get_face_texture();
    const int size = static_cast<gfx::FaceSkin*>(face)->fetch_skin_resolution(); // (0x4420)
    const void* data = static_cast<gfx::FaceSkin*>(face)->get_image();           // (its +0x3C)
    const void* texture = static_cast<render::TextureSet*>(texture_bank())
                              ->read_texture_buffer(static_cast<const uint8_t*>(data), size, name, 1, 1, true, 1);
    return texture != nullptr;
}

// 0x005B2480: SetFaceTextureFromVideoCapture captured_texture=<name>: the current profile's face texture made from
// the captured texture: a 0x4420-byte image (zeroed) - the texture's header (+0x78), its palette (+0x74: header size
// / bytes per pixel entries, after 0x20 bytes) and its pixels (+0x64, +0x68 bytes) after it - copied in (0x00487420).
// The texture isn't checked for null. Always true.
bool cfunc_set_face_texture_from_video_capture(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kCapturedTexture, &name, 1);
    void* face = get_face_texture();
    render::BitmapTexture* texture = find_texture(name);
    uint8_t image[0x4420];
    std::memset(image, 0, sizeof(image));
    texture->set_pixels(static_cast<void*>(image));
    const int palette_size = static_cast<int>(texture->get_palette_size());
    const uint8_t bits_per_pixel = texture->get_unknown_e();
    texture->set_palette(static_cast<void*>(image + 0x20), palette_size / (bits_per_pixel >> 3));
    const void* pixels = texture->get_pixels();
    const int pixel_size = static_cast<int>(texture->get_pixels_size());
    std::memcpy(image + 0x20 + palette_size, pixels, pixel_size);
    static_cast<gfx::FaceSkin*>(face)->set_image(image, pixel_size + palette_size + 0x20);
    return true;
}

// 0x005B2570: OffsetTexture <texture> [0xD83D589E=x] [y_offset=y] [colour bytes]: the texture's image shifted
// (+0x48: x, y, whether a colour was given, the colour - 0x80 for each byte not given). Always true.
bool cfunc_offset_texture(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(0u, &name, 0);
    render::BitmapTexture* texture = find_texture(name);
    if (!texture)
        return true;
    int x = 0;
    int y = 0;
    params->fetch_int_value(kXOffset, &x, 0);
    params->fetch_int_value(kYOffset, &y, 0);
    bool given = false;
    const uint32_t colour = read_colour(params, &given);
    texture->get_offset(x, y, given, static_cast<int>(colour));
    return true;
}

// 0x005B26B0: RotateTexture <texture> angle=degrees: the texture's image rotated (+0x44: radians, -1, -1). Always
// true.
bool cfunc_rotate_texture(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (both uninitialised in the original when missing)
    params->read_hash_value(0u, &name, 0);
    float angle = 0.0f;
    params->fetch_float(kAngle, &angle, 0);
    render::BitmapTexture* texture = find_texture(name);
    if (texture) {
        // x87: degrees * (pi / 180, 0x00645464) in double, rounded once
        auto radians = static_cast<float>(static_cast<double>(angle) * g_degree_to_radian_ratio);
        texture->spin_by(radians, -1.0f, -1.0f);
    }
    return true;
}

// 0x005B2720: PushTextureToPoint <texture> [0x485A0CDB=point] [Axis=n] [0x88CF948C=amount] [colour bytes]: the
// texture's image pushed towards a point (+0x58: point, axis, amount, whether a colour was given, the colour).
// Always true.
bool cfunc_push_texture_to_point(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(0u, &name, 0);
    render::BitmapTexture* texture = find_texture(name);
    if (!texture)
        return true;
    int point = 0;
    int axis = 0;
    int amount = 0;
    params->fetch_int_value(kPushPoint, &point, 0);
    params->fetch_int_value(kAxis, &axis, 0);
    params->fetch_int_value(kPushAmount, &amount, 0);
    bool given = false;
    const uint32_t colour = read_colour(params, &given);
    texture->squash(point, axis, amount, given, colour);
    return true;
}

// 0x005B2880: ApplyRGBAToTexture <texture> r= g= b= [a=a (0x80)] [0xC164842A=mode]: the texture tinted (+0x30: the
// colour, whether the mode is 1). Always true.
bool cfunc_apply_rgba_to_texture(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(0u, &name, 0);
    render::BitmapTexture* texture = find_texture(name);
    if (!texture)
        return true;
    int r = 0; // (r, g and b uninitialised in the original when missing)
    int g = 0;
    int b = 0;
    int a = 0x80;
    params->fetch_int_value(kR, &r, 0);
    params->fetch_int_value(kG, &g, 0);
    params->fetch_int_value(kB, &b, 0);
    params->fetch_int_value(kA, &a, 0);
    int mode = 0;
    params->fetch_int_value(kTintMode, &mode, 0);
    const bool second = mode == 1;
    texture->modulate(pack_colour(r, g, b, a), second);
    return true;
}

// 0x005B2970: HSVtoRGB hsv=[h s v (alpha)]: rgb, the colour in RGB (0-128 each; the fourth element copied). Hue is
// taken in 60ths (wrapped into 0-6), saturation in 90ths (kept to 0.01-1), value in 60ths (kept to 0.01-1) - the
// value stays at the x87's precision until stored. Always true.
bool cfunc_hs_vto_rgb(script::Struct* params, script::Script* script)
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    Array* hsv = nullptr;
    params->fetch_list(kHsv, &hsv, 1);

    constexpr float kSixtieth = 0x1.111112p-6f;  // 0x006463B8
    constexpr float kNinetieth = 0x1.6c16c2p-7f; // 0x00657CC4
    double hue = static_cast<int>(hsv->fetch_part(0)) * static_cast<double>(kSixtieth);
    auto h = static_cast<float>(hue);
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
    auto s = static_cast<float>(static_cast<int>(hsv->fetch_part(1)) * static_cast<double>(kNinetieth));
    if (!(s > 0.0f))
        s = 0.01f;
    else if (s > 1.0f)
        s = 1.0f;
    double v = static_cast<int>(hsv->fetch_part(2)) * static_cast<double>(kSixtieth);
    if (!(v > 0.0))
        v = 0.01f;
    else if (v > 1.0)
        v = 1.0;

    const int sector = ftol(h);
    const double f = h - static_cast<double>(sector);
    auto p = static_cast<float>((1.0 - s) * v);
    auto q = static_cast<float>((1.0 - s * f) * v);
    auto t = static_cast<float>((1.0 - (1.0 - f) * s) * v);
    auto value = static_cast<float>(v);
    switch (static_cast<uint32_t>(ftol(static_cast<double>(sector)))) {
    case 0:
        r = value;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = value;
        b = p;
        break;
    case 2:
        r = p;
        g = value;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = value;
        break;
    case 4:
        r = t;
        g = p;
        b = value;
        break;
    case 5:
        r = value;
        g = p;
        b = q;
        break;
    default:
        break;
    }

    Array* rgb = new_colour_array(hsv);
    rgb->set_element(0, static_cast<uint32_t>(ftol(static_cast<double>(r) * 128.0f)));
    rgb->set_element(1, static_cast<uint32_t>(ftol(static_cast<double>(g) * 128.0f)));
    rgb->set_element(2, static_cast<uint32_t>(ftol(static_cast<double>(b) * 128.0f)));
    script->args_->append_array_ref(kRgb, rgb);
    return true;
}

// 0x005B2CD0: RGBtoHSV rgb=[r g b (alpha)]: hsv, the colour in HSV (hue 0-360, saturation 0-90, value 0-60; the
// fourth element copied). Always true.
bool cfunc_rg_bto_hsv(script::Struct* params, script::Script* script)
{
    float h = 0.0f;
    Array* rgb_array = nullptr;
    params->fetch_list(kRgb, &rgb_array, 1);
    constexpr float kScale = 1.0f / 128.0f; // 0x0064A2FC
    auto r = static_cast<float>(static_cast<int>(rgb_array->fetch_part(0)) * static_cast<double>(kScale));
    auto g = static_cast<float>(static_cast<int>(rgb_array->fetch_part(1)) * static_cast<double>(kScale));
    auto b = static_cast<float>(static_cast<int>(rgb_array->fetch_part(2)) * static_cast<double>(kScale));
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
    auto delta = static_cast<float>(static_cast<double>(max) - min);

    float s = 0.0f;
    double hue = 0.0;
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

    Array* hsv = new_colour_array(rgb_array);
    hsv->set_element(0, static_cast<uint32_t>(ftol(h)));
    hsv->set_element(1, static_cast<uint32_t>(ftol(static_cast<double>(s) * 90.0f)));
    hsv->set_element(2, static_cast<uint32_t>(ftol(static_cast<double>(max) * 60.0f)));
    script->args_->append_array_ref(kHsv, hsv);
    return true;
}

} // namespace commands
