// The pixel shaders: see pixel_shaders.h.

#include "engine/gfx/pixel_shaders.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <array>

#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/view_mode.h"
#include "engine/lists/key_map.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "engine/sys/startup_init.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/shadow_base.h"

namespace render {

lists::ObjectSet<ShadowCaster> g_shadow_casters;
namespace {
bool g_pixel_shaders_made;
} // namespace
std::array<void*, kPixelShaderCount> g_pixel_shaders;

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
void*& hdl(int slot) { return core::at(g_pixel_shaders, slot); }
std::array<uint8_t, 10> g_shader_stand_ins{};
// The cache of generated shaders (a hash table kept in place at 0x007CA9C0), by configuration key.
lists::KeyMap g_shader_cache{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
lists::KeyMap& get_cache() { return g_shader_cache; }

// An ID3DXBuffer (COM, through its vtable): [2] Release, [3] GetBufferPointer.
void* buffer_pointer(void* buffer) { return d3dx::buffer_pointer(buffer); }
void get_release(void* object) { d3dx::release_buffer(object); }

// D3DXAssembleShader (linked library code; stdcall). Returns the HRESULT.
long assemble_shader(const char* source, uint32_t length, void** shader, void** errors)
{
    return d3dx::assemble_shader(source, length, shader, errors);
}

// Per-pass flags of a material (not rewritten yet; thiscall): bit n set for passes that take their alpha from the
// texture alone.
uint32_t pass_alpha_flags(const void* material)
{
    return static_cast<const MaterialRef*>(material)->texture_alpha_passes();
}
uint32_t pass_count(const void* material) { return static_cast<const MaterialRef*>(material)->pass_count_; }
uint32_t get_blend_mode(const void* material, uint32_t pass)
{
    return core::at(static_cast<const MaterialRef*>(material)->blend_modes_, pass) & 0xFFFFFF;
}

// The fixed shaders' sources.
constexpr auto kTextureVertexX4 = std::to_array("ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4\tr0.rgb,v0,r0\n"); // 0x0067EBF0
constexpr auto kVertexFactor = std::to_array("ps.1.1\nmul_x2\tr0.rgb,v0,c0\n+mov r0.a, v0.a\n");         // 0x0067EC3C
constexpr auto kBloomCutoff = std::to_array(                                                             // 0x0067ED60
    "ps.1.1\n"
    "; On entry, C0 contains the 'cutoff' color for the bloom effect (and 0.5 in the alpha component).\n"
    "; Only texels above this color value will be considered.\n"
    "def c1, 1, 1, 1, 1\n"
    "def c2, 0, 0, 0, 0\n"
    "tex t0\n"
    "sub\tr0,t0,c0\t\t\t; r0 now contains the difference - we want to know if this is non zero\n"
    "dp3\tr1,r0,c1\t\t\t; r1.a = ( r0.r * 1 ) + ( r0.g * 1 ) + ( r0.b * 1 )\n"
    "add\tr0.a,r1.a,c0.a\t\t; r0.a = 0.5 + sum of r0.rgb\n"
    "cnd\tr0,r0.a,t0,c2\t\t; sets r0 = t0 if r1.a > 0.5 (so r0.rgb > 0.0), 0.0 otherwise\n");
constexpr auto kVertexAlphaTextureAlpha = std::to_array("ps.1.1\ntex t0\nmul_x2\tr0,v0.a,t0.a\n");  // 0x0067ECC8
constexpr auto kConstantAlphaTextureAlpha = std::to_array("ps.1.1\ntex t0\nmul_x2 r0,c4.a,t0.a\n"); // 0x0067ECEC
constexpr auto kTextureConstantAlpha = std::to_array("ps.1.1\ntex t0\nmul r0.rgb,t0,c0\nmul_x4 r0.rgb,v0,r0\n+mul_x2\tr0.a,c4.a,t0.a\n"); // 0x0067ED10

// The shader text being written (the original sprintfs into a 1 KB buffer).
struct ShaderText {
    std::array<char, 1024> buffer{};
    int length = 0;
    template <typename... Args>
    void append_one(const char* format, Args... args)
    {
        length += std::snprintf(std::span(buffer).subspan(length).data(), sizeof(buffer) - length, format, args...);
    }
};

} // namespace

// 0x004DD7F0
void compile_pixel_shaders()
{
    bool& done = g_pixel_shaders_made;
    if (done)
        return;
    done = true;
    for (int slot = kPixelShader920; slot < kPixelShaderCount; ++slot)
        hdl(slot) = nullptr;
    if (!g_shaders_available) {
        // No pixel shaders: stand-ins the fixed-function stages tell apart (the original's numbers 1-10; here the
        // addresses of markers of their own - only compared, never given to the device).
        static constexpr auto kStandIns = std::to_array<int>({kPixelShader964, kPixelShader960, kPixelShader95c,
                                                              kPixelShader958, kPixelShader954, kPixelShader950,
                                                              kPixelShader94c, kPixelShader930, kPixelShader92c,
                                                              kPixelShader944});
        for (size_t i = 0; i < kStandIns.size(); ++i)
            hdl(core::at(kStandIns, i)) = &core::at(g_shader_stand_ins, i);
    } else {
        void* shader = nullptr;
        void* errors = nullptr;
        struct Fixed {
            const char* source;
            int handle;
        };
        static const auto kFixed = std::to_array<Fixed>({
            {kTextureVertexX4.data(), kPixelShader964},
            {kVertexFactor.data(), kPixelShader95c},
            {kBloomCutoff.data(), kPixelShader948},
            {kVertexAlphaTextureAlpha.data(), kPixelShader944},
            {kConstantAlphaTextureAlpha.data(), kPixelShader940},
            {kTextureConstantAlpha.data(), kPixelShader960},
        });
        for (const Fixed& f : kFixed) {
            assemble_shader(f.source, static_cast<uint32_t>(std::strlen(f.source)), &shader, &errors);
            IDirect3DPixelShader9* made = nullptr;
            input_port()->CreatePixelShader(static_cast<const DWORD*>(buffer_pointer(shader)), &made);
            hdl(f.handle) = made;
            // Not original - the level editor's Unlit view (engine/gfx/view_mode.h): the source kept for its twin.
            view_mode::note_shader_source(hdl(f.handle), f.source);
            if (shader)
                get_release(shader);
            if (errors)
                get_release(errors);
        }
    }
    // (the shadow casters' table was made here: lists::ObjectSet, made with the program)
}

// 0x004DD2C0
void* generated_pixel_shader(const void* material)
{
    const uint32_t passes = pass_count(material);
    uint32_t present = 0;
    std::array<uint32_t, 4> modes{};
    for (uint32_t pass = 0; pass < passes; ++pass) {
        present |= 1u << pass;
        core::at(modes, pass) = get_blend_mode(material, pass);
    }
    // The key: the pass count less one, each pass's mode (5 bits from bit 2), the passes and the flags at the top.
    uint32_t key = passes - 1;
    for (uint32_t pass = 0; pass < passes; ++pass)
        key |= core::at(modes, pass) << (2 + 5 * pass);
    const uint32_t alpha_flags = pass_alpha_flags(material);
    key |= (present * 8 | alpha_flags) << 22;
    // In the cache? (the first item with the key and a shader)
    lists::KeyMap& cache = get_cache();
    for (const lists::KeyMap::Item* item = &core::at(cache.bucket_run(), key & ((1u << cache.bits_) - 1)); item;
         item = item->next) {
        if (item->key == key && item->value)
            return item->value;
    }

    ShaderText text;
    text.append_one("ps.1.1\n");
    for (uint32_t pass = 0; pass < passes; ++pass)
        text.append_one("tex t%d\n", pass);
    // The first pass into r0.
    if (present & 1) {
        if (!(alpha_flags & 1))
            text.append_one("mul_x2 r0.a, v0.a, t0.a\n");
        else
            text.append_one("mov r0.a, t0.a\n");
        text.append_one("+mul_x2 r0.rgb, c0, t0\n");
    } else if (!(alpha_flags & 1)) {
        text.append_one("mul_x2 r0.a, v0.a, t0.a\n");
        text.append_one("+mov r0.rgb, r8\n");
    } else {
        text.append_one("mov r0, t0\n");
    }
    // Each further pass into r1 (or straight from its texture), then blended into r0 by its mode. Registers by
    // number: r0, t1, t2, t3, r1.
    static const std::array<std::array<char, 3>, 5> kRegisters = {{"r0", "t1", "t2", "t3", "r1"}};
    uint32_t reg = 0;
    for (int pass = 1; pass < ints::as_signed(passes); ++pass) {
        const uint32_t mode = core::at(modes, pass);
        const bool brighten = mode == 0xC || mode == 0xD;
        const uint32_t previous = reg;
        reg = 4;
        const uint32_t bit = 1u << pass;
        if (present & bit) {
            text.append_one("mul_x2 r1.rgb, c%d, t%d\n", pass, pass);
            if (!brighten) {
                if (!(alpha_flags & bit))
                    text.append_one(mode == 5 ? "+mul_x2_sat r1.a, v0.a, t%d.a\n" : "+mul_x2 r1.a, v0.a, t%d.a\n",
                                    pass);
                else
                    text.append_one("+mov r1.a, t%d\n", pass);
            }
        } else if (!(alpha_flags & bit)) {
            text.append_one("mov r1.rgb, t%d\n", pass);
            if (!brighten)
                text.append_one(mode == 5 ? "+mul_x2_sat r1.a, v0.a, t%d.a\n" : "+mul_x2 r1.a, v0.a, t%d.a\n", pass);
        } else {
            reg = pass; // the texture itself
        }
        const char* r = core::at(kRegisters, reg).data();
        const char* p = core::at(kRegisters, previous).data();
        switch (mode) {
        case 1:
            text.append_one("mad r0.rgb,%s,%s.a,r0\n", r, r);
            break;
        case 2:
            text.append_one("mad r0.rgb,%s,c%d.a,r0\n", r, pass);
            break;
        case 3:
            text.append_one("mad r0.rgb,%s,-%s.a,r0\n", r, r);
            break;
        case 4:
            text.append_one("mad r0.rgb,-%s,c%d.a,r0\n", r, pass);
            break;
        case 5:
            text.append_one("lrp r0.rgb,%s.a,%s,r0\n", r, r);
            break;
        case 6:
            text.append_one("lrp r0.rgb,c%d.a,%s,r0\n", pass, r);
            break;
        case 7:
            text.append_one("mul r0.rgb,r0,%s.a\n", r);
            break;
        case 8:
            text.append_one("mul r0.rgb,r0,c%d.a\n", pass);
            break;
        case 9:
            text.append_one("mad r0.rgb,r0,%s.a,r0\n", r);
            break;
        case 0xA:
            text.append_one("mad r0.rgb,r0,c%d.a,r0\n", pass);
            break;
        case 0xB:
            text.append_one("mul v1.rgb,v1,%s.a\n", r);
            break;
        case 0xC:
            text.append_one("lrp r0.rgb, %s.a, %s, r0\n", p, r);
            break;
        case 0xD:
            text.append_one("lrp r0.rgb, %s.a, r0, %s\n", p, r);
            break;
        default:
            break;
        }
    }
    text.append_one("mul_x2 r0.rgb, r0, v0\n");

    void* shader = nullptr;
    void* errors = nullptr;
    IDirect3DPixelShader9* handle = nullptr;
    if (assemble_shader(text.buffer.data(), static_cast<uint32_t>(std::strlen(text.buffer.data())), &shader, &errors) != 0)
        buffer_pointer(errors); // (the error text, not shown)
    input_port()->CreatePixelShader(static_cast<const DWORD*>(buffer_pointer(shader)), &handle);
    // Not original - the Unlit view (view_mode.h): the source kept for its twin.
    view_mode::note_shader_source(handle, text.buffer.data());
    cache.store_entry(key, handle);
    if (shader)
        get_release(shader);
    if (errors)
        get_release(errors);
    return handle;
}

// 0x004DDC00
void material_pixel_shader(const void* material, void** shader)
{
    if (pass_count(material) != 1) {
        *shader = generated_pixel_shader(material);
        return;
    }
    if (!core::at(static_cast<const MaterialRef*>(material)->textures, 0)) { // no texture
        *shader = hdl(kPixelShader95c);
        return;
    }
    const uint32_t alpha_flags = pass_alpha_flags(material);
    if (get_blend_mode(material, 0) == 9)
        *shader = hdl(alpha_flags ? kPixelShader940 : kPixelShader944);
    else
        *shader = hdl(alpha_flags ? kPixelShader960 : kPixelShader964);
}

} // namespace render

namespace render {

// 0x00640DF0 (a static initialiser): the generated pixel shaders' cache, 256 buckets.
void construct_shader_cache() { new (&g_shader_cache) lists::KeyMap(8); }

} // namespace render
