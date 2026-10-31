// Script commands (CFunctions) and helpers 0x005A9D80-0x005AA92F: textures and a few neighbours (see
// texture_cfuncs.h).

#include "engine/gfx/texture_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/gfx/texture_dict.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"

namespace commands {

bool g_texture_flag_ce441;
char g_park_name[128];

namespace {

using script::Component;
using script::Script;
using script::Struct;

// The names of the texture commands' parameters.
constexpr uint32_t kWidth = 0x73E5BAD0;  // "width"
constexpr uint32_t kHeight = 0x0AB21AF0; // "height"
constexpr uint32_t kSrc = 0x9FBBDB72;    // "src"
constexpr uint32_t kNew = 0x941CBBBA;    // "new"
constexpr uint32_t kSetsPaletteFlag = 0xC164842A; // (name unknown) brightness / HSV: an integer, 1 = the flag on

// The texture dictionaries (Nx): the level's / scripts' (0x00700F1C) and the particles' (0x00700F20).
void* get_textures() { return reinterpret_cast<void*>(render::g_game_texture_dict); }
void* particle_textures() { return reinterpret_cast<void*>(render::g_particle_texture_dict); }

// A dictionary's calls (no source yet).
void* read_texture_file(void* dictionary, const char* file, bool flag1, bool allocate_vram, bool flag3)
{
    return static_cast<render::TextureSet*>(dictionary)->ingest_skin(file, flag1, allocate_vram, flag3);
}
void* fetch_skin(void* dictionary, uint32_t checksum)
{
    return static_cast<render::TextureSet*>(dictionary)->fetch_skin(checksum);
}
void* get_texture_by_name(void* dictionary, const char* file)
{
    return static_cast<render::TextureSet*>(dictionary)->get_texture_by_name(file);
}
void unload_texture_from(void* dictionary, void* texture)
{
    static_cast<render::TextureSet*>(dictionary)->unload_texture(static_cast<render::BitmapTexture*>(texture));
}

// A texture's own calls (thunks into its vtable).
template <typename Fn, typename... Args>
void texture_call(uint32_t slot, void* texture, Args... args)
{
    // (the texture's virtual slot `slot`: the original goes through a jump stub for each)
    reinterpret_cast<Fn>((*static_cast<void***>(texture))[slot])(texture, args...);
}

// A component's raw value.
uint32_t raw_value(const Component* component) { return component->raw; }

} // namespace

// 0x005A9D80
bool __cdecl cfunc_sce_alt_pt(Struct*, Script*)
{
    g_texture_flag_ce441 = true;
    return true;
}

// 0x005A9D90
uint32_t __cdecl find_name_of_value(Struct* structure, uint32_t value, int depth)
{
    if (depth > 10)
        return 0;
    uint32_t found = 0;
    for (Component* component = structure->step_to_entry(nullptr); component;) {
        // (the global structure the component's value names, looked up for every component)
        Struct* named = script::read_sub_struct(raw_value(component));
        Component* next = structure->step_to_entry(component);
        if (!found) {
            if (component->kind_tag() == script::kQbvalStruct)
                found = find_name_of_value(component->structure, value, depth + 1);
            else if (named)
                found = find_name_of_value(named, value, depth + 1);
        }
        if (component->kind_tag() == script::kQbvalName && component->checksum == value)
            found = component->name;
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
        if (component->kind_tag() == script::kQbvalInteger && component->raw == value) {
            debug::printf("s_find_cat_in_mapping found %i\n", value); // "s_find_cat_in_mapping found %i"
            found = component->name;
        }
        component = next;
    }
    return found;
}

namespace {
uint32_t __cdecl find_name_of_integer_registers(Struct* structure, uint32_t value)
{
    return find_name_of_integer(structure, value);
}
} // namespace

// 0x005A9E30: the structure in EBX, the value on the stack (the caller pops it).
__declspec(naked) void find_name_of_integer_stub()
{
    __asm {
        push dword ptr [esp + 4]
        push ebx
        call find_name_of_integer_registers
        add esp, 8
        ret
    }
}

// 0x005A9E80
float __cdecl wrap_angle(float degrees)
{
    double angle = degrees; // (kept in an x87 register, double precision)
    while (angle < 0.0)
        angle += 360.0;
    while (angle > 360.0)
        angle -= 360.0;
    return static_cast<float>(angle);
}

// 0x005A9ED0
bool __cdecl cfunc_composite_object_exists(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0xA1DC81F9u, &id, 1); // name (asserted: the id is used as it is)
    return objects::find_object(id) != nullptr;
}

// 0x005A9F00
bool __cdecl cfunc_load_texture(Struct* params, Script*)
{
    const char* file;
    params->fetch_text(0u, &file, 0);
    bool allocate_vram = !params->has_bare_flag(0x3955FF2Eu); // no_vram_alloc
    return read_texture_file(get_textures(), file, true, allocate_vram, true) != nullptr;
}

bool texture_loaded(const char* file) { return get_texture_by_name(get_textures(), file) != nullptr; }

// 0x005A9F60
bool __cdecl cfunc_create_texture(Struct* params, Script*)
{
    uint32_t name;
    params->read_hash_value(0u, &name, 0);
    int width;
    int height;
    int bit_depth;
    int clut_bits = 0; // (not initialised in the original: only read when needed)
    bool valid = params->fetch_int_value(kWidth, &width, 0);
    valid = params->fetch_int_value(kHeight, &height, 0) && valid;
    valid = params->fetch_int_value(0xBA0F2AC5u, &bit_depth, 0) && valid; // bitdepth
    if (bit_depth <= 8 && !params->fetch_int_value(0x51533ECBu, &clut_bits, 0)) // palette_bitdepth
        return false;
    if (!valid)
        return false;
    return static_cast<render::TextureSet*>(get_textures())->create_texture(name, width, height, bit_depth, clut_bits, 1, true) !=
           nullptr;
}

// 0x005AA040
bool __cdecl cfunc_unload_texture(Struct* params, Script*)
{
    void* texture = nullptr;
    const char* file;
    uint32_t checksum;
    if (params->fetch_text(0u, &file, 0))
        texture = get_texture_by_name(get_textures(), file);
    else if (params->read_hash_value(0u, &checksum, 0))
        texture = fetch_skin(get_textures(), checksum);
    if (texture) {
        unload_texture_from(get_textures(), texture);
        return true;
    }
    params->has_bare_flag(0x3D92465Eu); // dont_assert (the assert is compiled out)
    return true;
}

// 0x005AA0C0
bool __cdecl cfunc_generate32_bit_image(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        bool renderable = false;
        bool store_original = false;
        int value;
        if (params->fetch_int_value(0xA5D7CFAAu, &value, 0) && value == 1) // renderable
            renderable = true;
        if (params->fetch_int_value(0xEEFAF080u, &value, 0) && value == 1) // store_original
            store_original = true;
        texture_call<void(__thiscall*)(void*, bool, bool)>(8, texture, renderable, store_original);
    }
    return true;
}

// 0x005AA160
bool __cdecl cfunc_put32_bit_image_into_texture(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        bool palette_gen = false;
        int value = 0;
        params->fetch_int_value(0xC313FDFFu, &value, 0); // palette_gen
        if (value == 1)
            palette_gen = true;
        texture_call<void(__thiscall*)(void*, bool, bool)>(9, texture, palette_gen, false);
    }
    return true;
}

// 0x005AA1E0
bool __cdecl cfunc_flip_texture(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    int horizontal = 0;
    int vertical = 0;
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        params->fetch_int_value(0xA04027C9u, &horizontal, 0); // horiz
        params->fetch_int_value(0x94927839u, &vertical, 0);   // vert
        if (horizontal)
            texture_call<void(__thiscall*)(void*)>(13, texture);
        if (vertical)
            texture_call<void(__thiscall*)(void*)>(14, texture);
    }
    return true;
}

// 0x005AA270
bool __cdecl cfunc_resize_texture_into(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    uint32_t source; // (not initialised without src=, as in the original)
    params->read_hash_value(kSrc, &source, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        if (void* source_texture = fetch_skin(get_textures(), source))
            texture_call<void(__thiscall*)(void*, void*, int)>(15, texture, source_texture, 0);
    }
    return true;
}

// 0x005AA2E0
bool __cdecl cfunc_scale_texture(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    float scale; // (not initialised without scale=, as in the original)
    params->fetch_float(0x13B9DA7Bu, &scale, 0);
    if (void* texture = fetch_skin(get_textures(), id))
        texture_call<void(__thiscall*)(void*, float, float)>(16, texture, scale, scale);
    return true;
}

// 0x005AA340
bool __cdecl cfunc_adjust_texture_region(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        int x = 0;
        int y = 0;
        // (its width and height: slots 33 and 34, through the jumps 0x0049F3C0 / 0x0049F3D0)
        auto** slots = *static_cast<void***>(texture);
        int width = reinterpret_cast<uint16_t(__thiscall*)(void*)>(slots[33])(texture);
        int height = reinterpret_cast<uint16_t(__thiscall*)(void*)>(slots[34])(texture);
        int value5 = 0;
        int value6 = 0;
        int value7 = 0;
        params->fetch_int_value(0x0FA8972Eu, &x, 0); // xpos
        params->fetch_int_value(0xB714F04Bu, &y, 0); // ypos
        params->fetch_int_value(kWidth, &width, 0);
        params->fetch_int_value(kHeight, &height, 0);
        params->fetch_int_value(0xE679982Eu, &value5, 0); // (names unknown)
        params->fetch_int_value(0x3D0F162Au, &value6, 0);
        params->fetch_int_value(0x39C54BDEu, &value7, 0);
        texture_call<void(__thiscall*)(void*, int, int, int, int, int, int, int)>(20, texture, x, y, width,
                                                                                 height, value5, value6, value7);
    }
    return true;
}

// 0x005AA460
bool __cdecl cfunc_pull_texture_to_edge(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        int edge = 0;
        int axis = 0;
        int pixels = 0;
        params->fetch_int_value(0x485A0CDBu, &edge, 0);   // (name unknown)
        params->fetch_int_value(0x7AF07905u, &axis, 0);   // Axis
        params->fetch_int_value(0x88CF948Cu, &pixels, 0); // num_pixels
        texture_call<void(__thiscall*)(void*, int, int, int)>(21, texture, edge, axis, pixels);
    }
    return true;
}

// 0x005AA500
bool __cdecl cfunc_adjust_texture_brightness(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        float brightness = 1.0f;
        int value = 0;
        bool flag = false;
        params->fetch_float(0x2689291Cu, &brightness, 1); // Brightness
        params->fetch_int_value(kSetsPaletteFlag, &value, 0);
        if (value == 1)
            flag = true;
        texture_call<void(__thiscall*)(void*, float, bool)>(23, texture, brightness, flag);
    }
    return true;
}

// 0x005AA590
bool __cdecl cfunc_adjust_texture_hsv(Struct* params, Script*)
{
    uint32_t id;
    params->read_hash_value(0u, &id, 0);
    void* texture = fetch_skin(get_textures(), id);
    if (texture) {
        float hue = 0.0f;
        float saturation = 1.0f;
        float value = 1.0f;
        int bit_value = 0;
        bool flag = false;
        params->fetch_float(0x6E94F918u, &hue, 0);        // h
        params->fetch_float(0xE4F130F4u, &saturation, 0); // s
        params->fetch_float(0x949BC47Bu, &value, 0);      // v
        params->fetch_int_value(kSetsPaletteFlag, &bit_value, 0);
        if (bit_value == 1)
            flag = true;
        texture_call<void(__thiscall*)(void*, float, float, float, bool)>(24, texture, hue, saturation, value,
                                                                         flag);
        texture_call<void(__thiscall*)(void*, bool, bool)>(9, texture, false, false);
    }
    return true;
}

// 0x005AA670
bool __cdecl cfunc_copy_texture(Struct* params, Script*)
{
    uint32_t source;
    params->read_hash_value(kSrc, &source, 0);
    void* texture = fetch_skin(get_textures(), source);
    uint32_t copy;
    if (texture && params->read_hash_value(kNew, &copy, 0))
        static_cast<render::TextureSet*>(get_textures())->copy_texture(copy, static_cast<render::BitmapTexture*>(texture));
    return true;
}

// 0x005AA6E0
bool __cdecl cfunc_combine_textures(Struct* params, Script*)
{
    uint32_t source; // (src and top are not initialised when missing, as in the original)
    uint32_t top;
    uint32_t combined = 0;
    bool palette_gen = true;
    params->read_hash_value(kSrc, &source, 0);
    params->read_hash_value(0xE126E035u, &top, 0); // top
    params->read_hash_value(kNew, &combined, 0);
    if (params->has_bare_flag(0x5905256Bu)) // no_palette_gen
        palette_gen = false;
    void* source_texture = fetch_skin(get_textures(), source);
    if (!source_texture)
        return true;
    void* top_texture = fetch_skin(get_textures(), top);
    if (!top_texture)
        return true;
    if (combined) {
        // a new texture of the two
        static_cast<render::TextureSet*>(get_textures())->copy_texture_with_overlay(
            combined, static_cast<render::BitmapTexture*>(source_texture), static_cast<render::BitmapTexture*>(top_texture), palette_gen);
        return true;
    }
    texture_call<void(__thiscall*)(void*, void*, bool)>(41, source_texture, top_texture, palette_gen);
    return true;
}

// 0x005AA7B0
bool __cdecl cfunc_load_particle_texture(Struct* params, Script*)
{
    const char* file;
    params->fetch_text(0u, &file, 0);
    if (file[0] == '\0')
        return false;
    void* texture = get_texture_by_name(particle_textures(), file);
    bool permanent = params->has_bare_flag(0xD5928F25u); // perm
    if (!texture)
        texture = read_texture_file(particle_textures(), file, true, true, permanent);
    return texture != nullptr;
}

// 0x005AA830
bool __cdecl cfunc_unload_particle_texture(Struct* params, Script*)
{
    const char* file;
    params->fetch_text(0u, &file, 0);
    if (void* texture = get_texture_by_name(particle_textures(), file)) {
        texture_call<void(__thiscall*)(void*)>(32, texture); // (its vtable +0x80 first)
        unload_texture_from(particle_textures(), texture);
    }
    return true;
}

// 0x005AA880
bool __cdecl cfunc_load_sfp_texture(Struct* params, Script*)
{
    const char* file;
    params->fetch_text(0u, &file, 0);
    return read_texture_file(particle_textures(), file, false, true, true) != nullptr;
}

// 0x005AA8B0
bool __cdecl cfunc_unload_sfp_texture(Struct* params, Script*)
{
    const char* file;
    params->fetch_text(0u, &file, 0);
    if (void* texture = get_texture_by_name(particle_textures(), file))
        unload_texture_from(particle_textures(), texture);
    return true;
}

// 0x005AA8F0
bool __cdecl cfunc_set_park_name(Struct* params, Script*)
{
    const char* name = ""; // ""
    params->fetch_text(0u, &name, 0);
    char* park_name = g_park_name;
    while ((*park_name++ = *name++) != '\0') { // (no length limit, as in the original)
    }
    return false;
}

} // namespace commands
