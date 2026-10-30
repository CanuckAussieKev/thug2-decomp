// The pixel shaders: see pixel_shaders.h.

#include "game/gfx/pixel_shaders.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/mem.h"
#include "game/gfx/material.h"
#include "game/gfx/view_mode.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
void*& Handle(uint32_t address) { return GameVariable<void*>(address); }
// The cache of generated shaders (a hash table kept in place at 0x007CA9C0), by configuration key.
Lst::HashTable& Cache() { return GameVariable<Lst::HashTable>(0x007CA9C0); }

// An ID3DXBuffer (COM, through its vtable): [2] Release, [3] GetBufferPointer.
void* BufferPointer(void* buffer) { return reinterpret_cast<void*(__stdcall*)(void*)>((*static_cast<void***>(buffer))[3])(buffer); }
void Release(void* object) { reinterpret_cast<unsigned long(__stdcall*)(void*)>((*static_cast<void***>(object))[2])(object); }

// D3DXAssembleShader (linked library code; stdcall). Returns the HRESULT.
long AssembleShader(const char* source, uint32_t length, void** shader, void** errors)
{
    return GameFunction<long(__stdcall*)(const char*, uint32_t, const void*, void*, uint32_t, void**, void**)>(
        0x005C2FBD)(source, length, nullptr, nullptr, 0, shader, errors);
}

// Per-pass flags of a material (not rewritten yet; thiscall): bit n set for passes that take their alpha from the
// texture alone.
uint32_t PassAlphaFlags(const void* material) { return static_cast<const sMaterial*>(material)->TextureAlphaPasses(); }
uint32_t PassCount(const void* material) { return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(material) + 8); }
uint32_t BlendMode(const void* material, uint32_t pass)
{
    return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(material) + 0x80)[pass] & 0xFFFFFF;
}

// The fixed shaders' sources.
const char kTextureVertexX4[] =
    "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4\tr0.rgb,v0,r0\n"; // 0x0067EBF0
const char kVertexFactor[] = "ps.1.1\nmul_x2\tr0.rgb,v0,c0\n+mov r0.a, v0.a\n";       // 0x0067EC3C
const char kBloomCutoff[] =                                                                  // 0x0067ED60
    "ps.1.1\n"
    "; On entry, C0 contains the 'cutoff' color for the bloom effect (and 0.5 in the alpha component).\n"
    "; Only texels above this color value will be considered.\n"
    "def c1, 1, 1, 1, 1\n"
    "def c2, 0, 0, 0, 0\n"
    "tex t0\n"
    "sub\tr0,t0,c0\t\t\t; r0 now contains the difference - we want to know if this is non zero\n"
    "dp3\tr1,r0,c1\t\t\t; r1.a = ( r0.r * 1 ) + ( r0.g * 1 ) + ( r0.b * 1 )\n"
    "add\tr0.a,r1.a,c0.a\t\t; r0.a = 0.5 + sum of r0.rgb\n"
    "cnd\tr0,r0.a,t0,c2\t\t; sets r0 = t0 if r1.a > 0.5 (so r0.rgb > 0.0), 0.0 otherwise\n";
const char kVertexAlphaTextureAlpha[] = "ps.1.1\ntex t0\nmul_x2\tr0,v0.a,t0.a\n";   // 0x0067ECC8
const char kConstantAlphaTextureAlpha[] = "ps.1.1\ntex t0\nmul_x2 r0,c4.a,t0.a\n"; // 0x0067ECEC
const char kTextureConstantAlpha[] =
    "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\nmul_x4 r0.rgb,v0,r0\n+mul_x2\tr0.a,c4.a,t0.a\n"; // 0x0067ED10

// The shader text being written (the original sprintfs into a 1 KB buffer).
struct Text {
    char buffer[1024];
    int length = 0;
    template <typename... Args>
    void Add(const char* format, Args... args)
    {
        length += std::snprintf(buffer + length, sizeof(buffer) - length, format, args...);
    }
};

} // namespace

// 0x004DD7F0
void __cdecl CreatePixelShaders()
{
    bool& done = GameVariable<bool>(0x007CAA30);
    if (done)
        return;
    done = true;
    for (uint32_t address = 0x007CA920; address <= 0x007CA964; address += 4)
        Handle(address) = nullptr;
    if (!GameVariable<bool>(0x00786AB8)) {
        // No pixel shaders: numbers the stand-in texture stages tell apart.
        Handle(0x007CA964) = reinterpret_cast<void*>(1);
        Handle(0x007CA960) = reinterpret_cast<void*>(2);
        Handle(0x007CA95C) = reinterpret_cast<void*>(3);
        Handle(0x007CA958) = reinterpret_cast<void*>(4);
        Handle(0x007CA954) = reinterpret_cast<void*>(5);
        Handle(0x007CA950) = reinterpret_cast<void*>(6);
        Handle(0x007CA94C) = reinterpret_cast<void*>(7);
        Handle(0x007CA930) = reinterpret_cast<void*>(8);
        Handle(0x007CA92C) = reinterpret_cast<void*>(9);
        Handle(0x007CA944) = reinterpret_cast<void*>(10);
    } else {
        void* shader = nullptr;
        void* errors = nullptr;
        struct Fixed {
            const char* source;
            uint32_t handle;
        };
        static const Fixed kFixed[] = {
            {kTextureVertexX4, 0x007CA964},          {kVertexFactor, 0x007CA95C},
            {kBloomCutoff, 0x007CA948},              {kVertexAlphaTextureAlpha, 0x007CA944},
            {kConstantAlphaTextureAlpha, 0x007CA940}, {kTextureConstantAlpha, 0x007CA960},
        };
        for (const Fixed& f : kFixed) {
            AssembleShader(f.source, static_cast<uint32_t>(std::strlen(f.source)), &shader, &errors);
            Device()->CreatePixelShader(static_cast<const DWORD*>(BufferPointer(shader)),
                                        reinterpret_cast<IDirect3DPixelShader9**>(&Handle(f.handle)));
            // Not original - the level editor's Unlit view (game/gfx/view_mode.h): the source kept for its twin.
            ViewMode::NoteShaderSource(Handle(f.handle), f.source);
            if (shader)
                Release(shader);
            if (errors)
                Release(errors);
        }
    }
    void* table = Mem::New(sizeof(Lst::HashTable));
    GameVariable<Lst::HashTable*>(0x007CA970) = table ? new (table) Lst::HashTable(8) : nullptr; // the shadow casters
}

// 0x004DD2C0
void* __cdecl GeneratedPixelShader(const void* material)
{
    uint32_t passes = PassCount(material);
    uint32_t present = 0;
    uint32_t modes[4];
    for (uint32_t pass = 0; pass < passes; ++pass) {
        present |= 1u << pass;
        modes[pass] = BlendMode(material, pass);
    }
    // The key: the pass count less one, each pass's mode (5 bits from bit 2), the passes and the flags at the top.
    uint32_t key = passes - 1;
    for (uint32_t pass = 0; pass < passes; ++pass)
        key |= modes[pass] << (2 + 5 * pass);
    uint32_t alphaFlags = PassAlphaFlags(material);
    key |= (present * 8 | alphaFlags) << 22;
    // In the cache? (the first item with the key and a shader)
    Lst::HashTable& cache = Cache();
    for (Lst::HashTable::Item* item = cache.m_buckets + (key & ((1u << cache.m_bits) - 1)); item; item = item->next) {
        if (item->key == key && item->value)
            return item->value;
    }

    Text text;
    text.Add("ps.1.1\n");
    for (uint32_t pass = 0; pass < passes; ++pass)
        text.Add("tex t%d\n", pass);
    // The first pass into r0.
    if (present & 1) {
        if (!(alphaFlags & 1))
            text.Add("mul_x2 r0.a, v0.a, t0.a\n");
        else
            text.Add("mov r0.a, t0.a\n");
        text.Add("+mul_x2 r0.rgb, c0, t0\n");
    } else if (!(alphaFlags & 1)) {
        text.Add("mul_x2 r0.a, v0.a, t0.a\n");
        text.Add("+mov r0.rgb, r8\n");
    } else {
        text.Add("mov r0, t0\n");
    }
    // Each further pass into r1 (or straight from its texture), then blended into r0 by its mode. Registers by
    // number: r0, t1, t2, t3, r1.
    static const char kRegisters[5][3] = {"r0", "t1", "t2", "t3", "r1"};
    uint32_t reg = 0;
    for (int pass = 1; pass < static_cast<int>(passes); ++pass) {
        uint32_t mode = modes[pass];
        bool brighten = mode == 0xC || mode == 0xD;
        uint32_t previous = reg;
        reg = 4;
        uint32_t bit = 1u << pass;
        if (present & bit) {
            text.Add("mul_x2 r1.rgb, c%d, t%d\n", pass, pass);
            if (!brighten) {
                if (!(alphaFlags & bit))
                    text.Add(mode == 5 ? "+mul_x2_sat r1.a, v0.a, t%d.a\n"
                                                              : "+mul_x2 r1.a, v0.a, t%d.a\n",
                                     pass);
                else
                    text.Add("+mov r1.a, t%d\n", pass);
            }
        } else if (!(alphaFlags & bit)) {
            text.Add("mov r1.rgb, t%d\n", pass);
            if (!brighten)
                text.Add(mode == 5 ? "+mul_x2_sat r1.a, v0.a, t%d.a\n"
                                                          : "+mul_x2 r1.a, v0.a, t%d.a\n",
                                 pass);
        } else {
            reg = pass; // the texture itself
        }
        const char* r = kRegisters[reg];
        const char* p = kRegisters[previous];
        switch (mode) {
        case 1: text.Add("mad r0.rgb,%s,%s.a,r0\n", r, r); break;
        case 2: text.Add("mad r0.rgb,%s,c%d.a,r0\n", r, pass); break;
        case 3: text.Add("mad r0.rgb,%s,-%s.a,r0\n", r, r); break;
        case 4: text.Add("mad r0.rgb,-%s,c%d.a,r0\n", r, pass); break;
        case 5: text.Add("lrp r0.rgb,%s.a,%s,r0\n", r, r); break;
        case 6: text.Add("lrp r0.rgb,c%d.a,%s,r0\n", pass, r); break;
        case 7: text.Add("mul r0.rgb,r0,%s.a\n", r); break;
        case 8: text.Add("mul r0.rgb,r0,c%d.a\n", pass); break;
        case 9: text.Add("mad r0.rgb,r0,%s.a,r0\n", r); break;
        case 0xA: text.Add("mad r0.rgb,r0,c%d.a,r0\n", pass); break;
        case 0xB: text.Add("mul v1.rgb,v1,%s.a\n", r); break;
        case 0xC: text.Add("lrp r0.rgb, %s.a, %s, r0\n", p, r); break;
        case 0xD: text.Add("lrp r0.rgb, %s.a, r0, %s\n", p, r); break;
        default: break;
        }
    }
    text.Add("mul_x2 r0.rgb, r0, v0\n");

    void* shader = nullptr;
    void* errors = nullptr;
    void* handle = nullptr;
    if (AssembleShader(text.buffer, static_cast<uint32_t>(std::strlen(text.buffer)), &shader, &errors) != 0)
        BufferPointer(errors); // (the error text, not shown)
    Device()->CreatePixelShader(static_cast<const DWORD*>(BufferPointer(shader)),
                                reinterpret_cast<IDirect3DPixelShader9**>(&handle));
    // Not original - the Unlit view (view_mode.h): the source kept for its twin.
    ViewMode::NoteShaderSource(handle, text.buffer);
    cache.PutItem(key, handle);
    if (shader)
        Release(shader);
    if (errors)
        Release(errors);
    return handle;
}

// 0x004DDC00
void __cdecl MaterialPixelShader(const void* material, void** shader)
{
    if (PassCount(material) != 1) {
        *shader = GeneratedPixelShader(material);
        return;
    }
    if (!*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(material) + 0x30)) { // no texture
        *shader = Handle(0x007CA95C);
        return;
    }
    uint32_t alphaFlags = PassAlphaFlags(material);
    if (BlendMode(material, 0) == 9)
        *shader = Handle(alphaFlags ? 0x007CA940 : 0x007CA944);
    else
        *shader = Handle(alphaFlags ? 0x007CA960 : 0x007CA964);
}

} // namespace Nx

REPLACE(0x004DD7F0, Nx::CreatePixelShaders);
REPLACE(0x004DD2C0, Nx::GeneratedPixelShader);
REPLACE(0x004DDC00, Nx::MaterialPixelShader);
