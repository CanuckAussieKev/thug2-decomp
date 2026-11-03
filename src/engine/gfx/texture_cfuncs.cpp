// Script commands (CFunctions) and helpers 0x005A9D80-0x005AA92F: textures and a few neighbours (see
// texture_cfuncs.h).

#include "engine/gfx/texture_cfuncs.h"

#include "engine/core/str.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/gfx/engine_texture.h"
#include "engine/core/dbg.h"
#include "engine/gfx/texture_dict.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"

namespace commands {

constexpr std::size_t kParkNameBytes = 128;
constexpr int kMaxNameSearchDepth = 10;
constexpr double kFullCircleDegrees = 360.0;
constexpr int kPaletteDepthBits = 8;
// Text unknown for the two parameters (the 5th and 6th of the texture), the others are h, s and v.
constexpr uint32_t kValue5Key = 0xE679982EU;
constexpr uint32_t kValue6Key = 0x3D0F162AU;
constexpr uint32_t kHueKey = 0x6E94F918U;
constexpr uint32_t kSaturationKey = 0xE4F130F4U;
constexpr uint32_t kValueKey = 0x949BC47BU;

// Checksums used in this file (the text each hashes).
constexpr uint32_t kAxis = 0x7AF07905U;            // Axis
constexpr uint32_t kBRIGHTNESS = 0x2689291CU;      // BRIGHTNESS
constexpr uint32_t kBitdepth = 0xBA0F2AC5U;        // bitdepth
constexpr uint32_t kDontAssert = 0x3D92465EU;      // dont_assert
constexpr uint32_t kEndPoint = 0x39C54BDEU;        // end_point
constexpr uint32_t kHoriz = 0xA04027C9U;           // horiz
constexpr uint32_t kNAME = 0xA1DC81F9U;            // NAME
constexpr uint32_t kNoPaletteGen = 0x5905256BU;    // no_palette_gen
constexpr uint32_t kNumPixels = 0x88CF948CU;       // num_pixels
constexpr uint32_t kPOINT = 0x485A0CDBU;           // POINT
constexpr uint32_t kPaletteBitdepth = 0x51533ECBU; // palette_bitdepth
constexpr uint32_t kPaletteGen = 0xC313FDFFU;      // palette_gen
constexpr uint32_t kRenderable = 0xA5D7CFAAU;      // Renderable
constexpr uint32_t kSCALE = 0x13B9DA7BU;           // SCALE
constexpr uint32_t kStoreOriginal = 0xEEFAF080U;   // store_original
constexpr uint32_t kTOP = 0xE126E035U;             // TOP
constexpr uint32_t kVERT = 0x94927839U;            // VERT
constexpr uint32_t kXpos = 0x0FA8972EU;            // xpos
constexpr uint32_t kYpos = 0xB714F04BU;            // ypos

namespace {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed (texture_flags()).
struct TextureFlags {
    bool texture_flag_ce441_{};
    std::array<char, kParkNameBytes> park_name_{};
};

TextureFlags& texture_flags()
{
    static core::NoDestroy<TextureFlags> state;
    return state.get();
}

} // namespace

bool texture_flag_ce441() { return texture_flags().texture_flag_ce441_; }
std::array<char, kParkNameBytes>& park_name() { return texture_flags().park_name_; }

namespace {

using script::Component;
using script::Script;
using script::Struct;

// The names of the texture commands' parameters.
constexpr uint32_t kWidth = 0x73E5BAD0;           // "width"
constexpr uint32_t kHeight = 0x0AB21AF0;          // "height"
constexpr uint32_t kSrc = 0x9FBBDB72;             // "src"
constexpr uint32_t kNew = 0x941CBBBA;             // "new"
constexpr uint32_t kSetsPaletteFlag = 0xC164842A; // (name unknown) brightness / HSV: an integer, 1 = the flag on

// The texture dictionaries (Nx): the level's / scripts' (0x00700F1C) and the particles' (0x00700F20).

// A dictionary's calls (no source yet).
render::BitmapTexture* read_texture_file(void* dictionary, const char* file, bool flag1, bool allocate_vram, bool flag3)
{
    return static_cast<render::TextureSet*>(dictionary)->ingest_skin(file, flag1, allocate_vram, flag3);
}
render::BitmapTexture* fetch_skin(void* dictionary, uint32_t checksum)
{
    return static_cast<render::TextureSet*>(dictionary)->fetch_skin(checksum);
}
render::BitmapTexture* get_texture_by_name(void* dictionary, const char* file)
{
    return static_cast<render::TextureSet*>(dictionary)->get_texture_by_name(file);
}
void unload_texture_from(void* dictionary, void* texture)
{
    static_cast<render::TextureSet*>(dictionary)->unload_texture(static_cast<render::BitmapTexture*>(texture));
}

// A component's raw value.
uint32_t raw_value(const Component* component) { return component->value.word(); }

} // namespace

// 0x005A9D80
bool cfunc_sce_alt_pt(Struct* /*params*/, Script* /*script*/)
{
    texture_flags().texture_flag_ce441_ = true;
    return true;
}

// 0x005A9D90
uint32_t find_name_of_value(Struct* structure, uint32_t value, int depth)
{
    if (depth > kMaxNameSearchDepth) {
        return 0;
    }
    uint32_t found = 0;
    for (Component* component = structure->step_to_entry(nullptr); component;) {
        // (the global structure the component's value names, looked up for every component)
        Struct* named = script::read_sub_struct(raw_value(component));
        Component* next = structure->step_to_entry(component);
        if (!found) {
            if (value_kind(*component) == script::QbValueKind::kStruct) {
                found = find_name_of_value(component->value.structure(), value, depth + 1);
            } else if (named) {
                found = find_name_of_value(named, value, depth + 1);
            }
        }
        if (value_kind(*component) == script::QbValueKind::kName && component->value.word() == value) {
            found = component->name;
        }
        component = next;
    }
    return found;
}

// 0x005A9E30
uint32_t find_name_of_integer(Struct* structure, uint32_t value)
{
    uint32_t found = 0;
    for (Component* component = structure->step_to_entry(nullptr); component;) {
        Component* next = structure->step_to_entry(component);
        if (value_kind(*component) == script::QbValueKind::kInteger && component->value.word() == value) {
            debug::printf("s_find_cat_in_mapping found %i\n", value); // "s_find_cat_in_mapping found %i"
            found = component->name;
        }
        component = next;
    }
    return found;
}

// 0x005A9E80
float wrap_angle(float degrees)
{
    double angle = degrees; // (kept in an x87 register, double precision)
    while (angle < 0.0) {
        angle += kFullCircleDegrees;
    }
    while (angle > kFullCircleDegrees) {
        angle -= kFullCircleDegrees;
    }
    return static_cast<float>(angle);
}

// 0x005A9ED0
bool cfunc_composite_object_exists(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(kNAME, &id, 1); // name (asserted: the id is used as it is)
    return objects::find_object(id) != nullptr;
}

// 0x005A9F00
bool cfunc_load_texture(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    const bool allocate_vram = !params->has_bare_flag(0x3955FF2EU); // no_vram_alloc
    return read_texture_file(render::engine_parts().game_texture_dict(), file, true, allocate_vram, true) != nullptr;
}

bool texture_loaded(const char* file) { return get_texture_by_name(render::engine_parts().game_texture_dict(), file) != nullptr; }

// 0x005A9F60
bool cfunc_create_texture(Struct* params, Script* /*script*/)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    int width = 0;
    int height = 0;
    int bit_depth = 0;
    int clut_bits = 0; // (not initialised in the original: only read when needed)
    bool valid = params->fetch_int_value(kWidth, &width, 0);
    valid = params->fetch_int_value(kHeight, &height, 0) && valid;
    valid = params->fetch_int_value(kBitdepth, &bit_depth, 0) && valid;       // bitdepth
    if (bit_depth <= kPaletteDepthBits &&
        !params->fetch_int_value(kPaletteBitdepth, &clut_bits, 0)) { // palette_bitdepth
        return false;
    }
    if (!valid) {
        return false;
    }
    return static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())
               ->create_texture(name, width, height, bit_depth, clut_bits, 1, true) != nullptr;
}

// 0x005AA040
bool cfunc_unload_texture(Struct* params, Script* /*script*/)
{
    void* texture = nullptr;
    const char* file = nullptr;
    uint32_t checksum = 0;
    if (params->fetch_text(0U, &file, 0)) {
        texture = get_texture_by_name(render::engine_parts().game_texture_dict(), file);
    } else if (params->read_hash_value(0U, &checksum, 0)) {
        texture = fetch_skin(render::engine_parts().game_texture_dict(), checksum);
    }
    if (texture) {
        unload_texture_from(render::engine_parts().game_texture_dict(), texture);
        return true;
    }
    params->has_bare_flag(kDontAssert); // dont_assert (the assert is compiled out)
    return true;
}

// 0x005AA0C0
bool cfunc_generate32_bit_image(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        bool renderable = false;
        bool store_original = false;
        int value = 0;
        if (params->fetch_int_value(kRenderable, &value, 0) && value == 1) { // renderable
            renderable = true;
        }
        if (params->fetch_int_value(kStoreOriginal, &value, 0) && value == 1) { // store_original
            store_original = true;
        }
        texture->begin_edit(renderable, store_original);
    }
    return true;
}

// 0x005AA160
bool cfunc_put32_bit_image_into_texture(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        bool palette_gen = false;
        int value = 0;
        params->fetch_int_value(kPaletteGen, &value, 0); // palette_gen
        if (value == 1) {
            palette_gen = true;
        }
        texture->write_back(palette_gen, nullptr);
    }
    return true;
}

// 0x005AA1E0
bool cfunc_flip_texture(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    int horizontal = 0;
    int vertical = 0;
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        params->fetch_int_value(kHoriz, &horizontal, 0); // horiz
        params->fetch_int_value(kVERT, &vertical, 0);   // vert
        if (horizontal) {
            texture->get_mirror();
        }
        if (vertical) {
            render::BitmapTexture::flip_vertically();
        }
    }
    return true;
}

// 0x005AA270
bool cfunc_resize_texture_into(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    uint32_t source = 0; // (not initialised without src=, as in the original)
    params->read_hash_value(kSrc, &source, 0);
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        if (const render::BitmapTexture* source_texture =
                fetch_skin(render::engine_parts().game_texture_dict(), source)) {
            texture->resize_from(source_texture, 0);
        }
    }
    return true;
}

// 0x005AA2E0
bool cfunc_scale_texture(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    float scale = 0.0F; // (not initialised without scale=, as in the original)
    params->fetch_float(kSCALE, &scale, 0);
    if (render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id)) {
        texture->get_zoom(scale, scale);
    }
    return true;
}

// 0x005AA340
bool cfunc_adjust_texture_region(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    const render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        int x = 0;
        int y = 0;
        // (its width and height, through the jumps 0x0049F3C0 / 0x0049F3D0)
        int width = texture->fetch_breadth();
        int height = texture->fetch_tallness();
        int value5 = 0;
        int value6 = 0;
        int value7 = 0;
        params->fetch_int_value(kXpos, &x, 0); // xpos
        params->fetch_int_value(kYpos, &y, 0); // ypos
        params->fetch_int_value(kWidth, &width, 0);
        params->fetch_int_value(kHeight, &height, 0);
        params->fetch_int_value(kValue5Key, &value5, 0); // (names unknown)
        params->fetch_int_value(kValue6Key, &value6, 0);
        params->fetch_int_value(kEndPoint, &value7, 0);
        render::BitmapTexture::adjust_region(x, y, width, height, value5, value6, value7);
    }
    return true;
}

// 0x005AA460
bool cfunc_pull_texture_to_edge(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        int edge = 0;
        int axis = 0;
        int pixels = 0;
        params->fetch_int_value(kPOINT, &edge, 0);   // (name unknown)
        params->fetch_int_value(kAxis, &axis, 0);   // Axis
        params->fetch_int_value(kNumPixels, &pixels, 0); // num_pixels
        texture->get_stretch({.position = static_cast<uint16_t>(edge), .vertical = axis, .amount = pixels});
    }
    return true;
}

// 0x005AA500
bool cfunc_adjust_texture_brightness(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    const render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        float brightness = 1.0F;
        int value = 0;
        bool flag = false;
        params->fetch_float(kBRIGHTNESS, &brightness, 1); // Brightness
        params->fetch_int_value(kSetsPaletteFlag, &value, 0);
        if (value == 1) {
            flag = true;
        }
        render::BitmapTexture::adjust_brightness(brightness, flag);
    }
    return true;
}

// 0x005AA590
bool cfunc_adjust_texture_hsv(Struct* params, Script* /*script*/)
{
    uint32_t id = 0;
    params->read_hash_value(0U, &id, 0);
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), id);
    if (texture) {
        float hue = 0.0F;
        float saturation = 1.0F;
        float value = 1.0F;
        int bit_value = 0;
        bool flag = false;
        params->fetch_float(kHueKey, &hue, 0);        // h
        params->fetch_float(kSaturationKey, &saturation, 0); // s
        params->fetch_float(kValueKey, &value, 0);      // v
        params->fetch_int_value(kSetsPaletteFlag, &bit_value, 0);
        if (bit_value == 1) {
            flag = true;
        }
        texture->shift_hue_sat_value({.hue = hue, .saturation = saturation, .value = value}, flag);
        texture->write_back(false, nullptr);
    }
    return true;
}

// 0x005AA670
bool cfunc_copy_texture(Struct* params, Script* /*script*/)
{
    uint32_t source = 0;
    params->read_hash_value(kSrc, &source, 0);
    render::BitmapTexture* texture = fetch_skin(render::engine_parts().game_texture_dict(), source);
    uint32_t copy = 0;
    if (texture && params->read_hash_value(kNew, &copy, 0)) {
        static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())->copy_texture(copy, texture);
    }
    return true;
}

// 0x005AA6E0
bool cfunc_combine_textures(Struct* params, Script* /*script*/)
{
    uint32_t source = 0; // (src and top are not initialised when missing, as in the original)
    uint32_t top = 0;
    uint32_t combined = 0;
    bool palette_gen = true;
    params->read_hash_value(kSrc, &source, 0);
    params->read_hash_value(kTOP, &top, 0); // top
    params->read_hash_value(kNew, &combined, 0);
    if (params->has_bare_flag(kNoPaletteGen)) { // no_palette_gen
        palette_gen = false;
    }
    render::BitmapTexture* source_texture = fetch_skin(render::engine_parts().game_texture_dict(), source);
    if (!source_texture) {
        return true;
    }
    render::BitmapTexture* top_texture = fetch_skin(render::engine_parts().game_texture_dict(), top);
    if (!top_texture) {
        return true;
    }
    if (combined) {
        // a new texture of the two
        static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())
            ->copy_texture_with_overlay(combined, source_texture, top_texture, palette_gen);
        return true;
    }
    source_texture->get_overlay(top_texture, palette_gen);
    return true;
}

// 0x005AA7B0
bool cfunc_load_particle_texture(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    if (*file == '\0') {
        return false;
    }
    const render::BitmapTexture* texture = get_texture_by_name(render::engine_parts().particle_texture_dict(), file);
    const bool permanent = params->has_bare_flag(0xD5928F25U); // perm
    if (!texture) {
        texture = read_texture_file(render::engine_parts().particle_texture_dict(), file, true, true, permanent);
    }
    return texture != nullptr;
}

// 0x005AA830
bool cfunc_unload_particle_texture(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    if (render::BitmapTexture* texture = get_texture_by_name(render::engine_parts().particle_texture_dict(), file)) {
        render::BitmapTexture::release_pixels(); // (its vtable +0x80 first)
        unload_texture_from(render::engine_parts().particle_texture_dict(), texture);
    }
    return true;
}

// 0x005AA880
bool cfunc_load_sfp_texture(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    return read_texture_file(render::engine_parts().particle_texture_dict(), file, false, true, true) != nullptr;
}

// 0x005AA8B0
bool cfunc_unload_sfp_texture(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    if (render::BitmapTexture* texture = get_texture_by_name(render::engine_parts().particle_texture_dict(), file)) {
        unload_texture_from(render::engine_parts().particle_texture_dict(), texture);
    }
    return true;
}

// 0x005AA8F0
bool cfunc_set_park_name(Struct* params, Script* /*script*/)
{
    const char* name = ""; // ""
    params->fetch_text(0U, &name, 0);
    text::copy_unbounded(texture_flags().park_name_.data(), name); // (no length limit, as in the original)
    return false;
}

} // namespace commands
