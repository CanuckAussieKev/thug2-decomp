// Self-tests for the gaps filled next to batches 63-66: screen text's constructor, destructors and vertex buffer
// draw (SText), loading and unloading bitmap fonts (SFont), the device's SetRenderState / Clear wrappers, and the
// music's streams, user soundtracks, next track in order and per-frame update (Pcm). Each runs the original and
// ours against recording stand-ins (standins.h) for what they call - the Direct3D device and texture through
// stand-in vtables - and compares the call traces and the memory they write.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/manager.h"
#include "game/file/file.h"
#include "game/gfx/device.h"
#include "game/gfx/text.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

enum Tag : uint32_t {
    kBaseCtor = 0xBA5E, kBaseDtor = 0xBADD, kAlloc = 0xA110, kFree = 0xF4EE, kShader = 0x5ADE, kFvf = 0xF5F,
    kSetTexture = 0x7E8, kStageState = 0x5757, kRenderState = 0x4E5, kDraw = 0xD4A, kOpen = 0x0BE, kRead = 0x4EAD,
    kClose = 0xC105, kEnsure = 0xE45, kCreateTexture = 0xC7E, kLock = 0x10C, kUnlock = 0x11C, kClear = 0xC1EA,
};

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// A stand-in object whose vtable holds the given recording thunks.
struct FakeComObject {
    void** vtable;
    uint32_t pad[3];
};
void* g_deviceVtable[120];
FakeComObject g_device = {g_deviceVtable, {}};
void* g_textureVtable[24];
FakeComObject g_texture = {g_textureVtable, {}};

// Pointers into a test's own memory recorded relative to it (both runs use the same memory).
alignas(16) uint8_t g_arena[0x8000];
uint32_t g_arenaUsed;
uint32_t Rel(uint32_t p)
{
    auto base = reinterpret_cast<uint32_t>(g_arena);
    return p >= base && p < base + sizeof(g_arena) ? 0xA0000000u + (p - base) : p;
}

// The memory manager's allocate / free (the game's and ours): a bump allocator over the arena, the memory dirtied.
void StandInMemory(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 0xC, [](Regs& r) {
        Record({kAlloc, r.args[0], r.args[1], r.args[2]});
        uint32_t at = (g_arenaUsed + 15) & ~15u;
        if (at + r.args[0] > sizeof(g_arena))
            return 0u;
        g_arenaUsed = at + r.args[0];
        std::memset(g_arena + at, 0xCD, r.args[0]);
        return reinterpret_cast<uint32_t>(g_arena + at);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({kFree, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, [](Regs&) {
        Record({kEnsure});
        return 0u;
    });
}

// --- SText: construction, destruction, the vertex buffer ----------------------------------------------------

alignas(16) uint8_t g_text[0x44];
alignas(16) Nx::SFont g_textFonts[2];
Nx::SText* Text() { return reinterpret_cast<Nx::SText*>(g_text); }

void StandInDrawing(StandIns& s)
{
    s.At({0x004DFA70}, 8, [](Regs& r) {
        Record({kBaseCtor, Rel(r.ecx), r.args[0], r.args[1] & 0xFF});
        return r.ecx;
    });
    s.At({0x004DFAA0}, 0, [](Regs& r) {
        Record({kBaseDtor, Rel(r.ecx), *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0x0BAD0BADu;
    });
    s.At({0x004DAA80}, 0, [](Regs& r) {
        Record({kShader, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004D9DB0}, 0, [](Regs& r) {
        Record({kFvf, r.args[0]});
        return 0u;
    });
    s.At({0x004DA730}, 0, [](Regs& r) {
        Record({kSetTexture, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    g_deviceVtable[0x10C / 4] = s.Thunk(16, [](Regs& r) {
        Record({kStageState, r.args[0] == reinterpret_cast<uint32_t>(&g_device), r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    g_deviceVtable[0xE4 / 4] = s.Thunk(12, [](Regs& r) {
        Record({kRenderState, r.args[0] == reinterpret_cast<uint32_t>(&g_device), r.args[1], r.args[2]});
        return 0x100u + r.args[1];
    });
    g_deviceVtable[0x150 / 4] = s.Thunk(36, [](Regs& r) {
        Record({kDraw, r.args[0] == reinterpret_cast<uint32_t>(&g_device)});
        for (int i = 1; i < 9; ++i)
            Record({r.args[i]});
        Record({HashBytes(reinterpret_cast<const uint8_t*>(0x00780FF0), 0xC00)});
        return 0u;
    });
}

std::vector<Saved> TextGlobals()
{
    return {{0x00780FD8, 0xC1C, {}}, {0x007D69E0, 4, {}}, {0x007D1EC0, 1, {}}, {0x00786EBC, 8, {}},
            {0x007CC979, 1, {}}, {0x007D1800, 4, {}}};
}

// The constructor (on dirty memory), BeginDraw, the destructor body and the deleting destructor (flags 0, 1, 3);
// EndDraw making the index buffer (once - a spoilt entry stays spoilt after), with 0, 6, 7, -4 and 0x400
// vertices, the texture font set or not, rendering skipped or not.
void TestSTextDrawing()
{
    std::vector<Saved> saved = TextGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInMemory(*s);
        StandInDrawing(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            for (Saved& sv : saved)
                std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<void*>(0x007D1800) = g_arena; // (the manager: the stand-ins don't look at it)
            GameVariable<void*>(0x007D69E0) = &g_device;
            std::memset(g_text, 0xEE, sizeof(g_text));
            std::memset(g_textFonts, 0, sizeof(g_textFonts));
            g_textFonts[0].texture = reinterpret_cast<void*>(0x7E570000);
            g_textFonts[0].unknown17C = 0x1234;
            g_textFonts[1].texture = reinterpret_cast<void*>(0x7E571111);
            g_textFonts[1].unknown17C = 0x5678;
            auto snapshot = [&]() {
                Record({0x5A5A, HashBytes(g_text, sizeof(g_text)), GameVariable<uint32_t>(0x00780FE4),
                        Rel(GameVariable<uint32_t>(0x00780FE0)), GameVariable<uint32_t>(0x00780FE8),
                        GameVariable<uint32_t>(0x00786EBC), GameVariable<uint32_t>(0x00786EC0),
                        GameVariable<uint8_t>(0x00781BF0), HashBytes(reinterpret_cast<const uint8_t*>(0x00780FF0), 0xC00)});
            };
            Nx::SText* result = original ? Original<Nx::SText*(__thiscall*)(void*, float)>(0x004CEED0)(g_text, 2.5f)
                                         : Text()->Construct(2.5f);
            Record({Rel(reinterpret_cast<uint32_t>(result))});
            snapshot();
            Text()->font = &g_textFonts[0];
            GameVariable<uint32_t>(0x00780FE8) = 0xDEAD;
            if (original)
                Original<void(__thiscall*)(void*)>(0x004CEF00)(g_text);
            else
                Text()->ResetVertices();
            snapshot();
            struct DrawCase {
                int count;
                int textureFont; // -1: none
                bool skipRender;
                bool spoil; // spoil an index (the buffer's made already)
            };
            const DrawCase draws[] = {{0, -1, false, false}, {6, -1, false, true}, {7, 1, true, false},
                                      {-4, 1, false, false}, {0x400, -1, false, false}, {2, 0, false, false}};
            for (const DrawCase& d : draws) {
                GameVariable<int>(0x00780FE4) = d.count;
                GameVariable<Nx::SFont*>(0x00780FE0) = d.textureFont < 0 ? nullptr : &g_textFonts[d.textureFont];
                GameVariable<bool>(0x007D1EC0) = d.skipRender;
                if (d.spoil)
                    GameVariable<uint16_t>(0x00780FF0 + 2 * 37) = 0x7777;
                if (original)
                    Original<void(__thiscall*)(void*)>(0x004CEF10)(g_text);
                else
                    Text()->DrawVertices();
                snapshot();
            }
            if (original)
                Original<void(__thiscall*)(void*)>(0x004CEEF0)(g_text);
            else
                Text()->DestroyBody();
            snapshot();
            for (unsigned flags : {0u, 1u, 3u, 2u}) {
                Text()->vtable = reinterpret_cast<void**>(0x11112222);
                result = original ? Original<Nx::SText*(__thiscall*)(void*, unsigned)>(0x004CFAF0)(g_text, flags)
                                  : Text()->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                snapshot();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1],
            "text: SText constructor, destructors, BeginDraw, EndDraw (index buffer, device states, the draw)");
    Check(traces[0].size() > 150, "text: SText drawing test ran its cases");
}

// --- SFont: loading and unloading ---------------------------------------------------------------------------

std::vector<uint8_t> g_fileData;
uint32_t g_fileCursor;
bool g_createFails;
bool g_lockFails;
alignas(16) uint32_t g_bits[0x200];

void StandInFontLoading(StandIns& s)
{
    s.At({0x005B9E60, AddressOf(&File::Open)}, 0, [](Regs& r) {
        Record({kOpen, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0xF11Eu;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        Record({kRead, r.args[1], r.args[2], r.args[3]});
        uint32_t bytes = r.args[1] * r.args[2];
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_fileData.data() + g_fileCursor, bytes);
        g_fileCursor += bytes;
        return r.args[2];
    });
    s.At({0x005B9EA0, AddressOf(&File::Close)}, 0, [](Regs& r) {
        Record({kClose, r.args[0]});
        return 1u;
    });
    g_deviceVtable[0x5C / 4] = s.Thunk(36, [](Regs& r) {
        Record({kCreateTexture, r.args[0] == reinterpret_cast<uint32_t>(&g_device), r.args[1], r.args[2], r.args[3],
                r.args[4], r.args[5], r.args[6], Rel(r.args[7]), r.args[8]});
        if (g_createFails)
            return 0x8876086Cu;
        *reinterpret_cast<void**>(r.args[7]) = &g_texture;
        return 0u;
    });
    g_textureVtable[0x4C / 4] = s.Thunk(20, [](Regs& r) {
        Record({kLock, r.args[0] == reinterpret_cast<uint32_t>(&g_texture), r.args[1], r.args[3], r.args[4]});
        if (g_lockFails)
            return 0x8876086Cu;
        auto* locked = reinterpret_cast<uint32_t*>(r.args[2]);
        locked[0] = 0x999;
        locked[1] = reinterpret_cast<uint32_t>(g_bits);
        return 0u;
    });
    g_textureVtable[0x50 / 4] = s.Thunk(8, [](Regs& r) {
        Record({kUnlock, r.args[0] == reinterpret_cast<uint32_t>(&g_texture), r.args[1]});
        return 0u;
    });
}

void Put16(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    Put16(v, x & 0xFFFF);
    Put16(v, x >> 16);
}

struct FontCase {
    bool fromMemory;
    uint32_t format;
    std::vector<std::vector<uint16_t>> glyphs; // baseline, then the two code words
    uint32_t width, height;
    bool createFails, lockFails;
};

std::vector<uint8_t> FontFile(const FontCase& c)
{
    std::vector<uint8_t> v;
    Put32(v, 0x12345678);
    Put32(v, c.format | 0xAB00); // (only the low byte is the format)
    Put32(v, static_cast<uint32_t>(c.glyphs.size()));
    Put32(v, 21);
    Put32(v, 17);
    for (const auto& g : c.glyphs)
        for (uint16_t word : g)
            Put16(v, word);
    Put32(v, 0x0BADF00D);
    Put16(v, c.width);
    Put16(v, c.height);
    Put32(v, 0x11);
    Put32(v, 0x22);
    uint32_t size = (c.width * c.height + 3) & ~3u;
    for (uint32_t i = 0; i < size; ++i)
        v.push_back(static_cast<uint8_t>(i * 37 + 5));
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t alpha = (i * 13) & 0xFF;
        if (i < 4)
            alpha = (0x7F + i) & 0xFF; // 0x7F 0x80 0x81 0x82
        Put32(v, (alpha << 24) | ((i * 7) & 0xFF) << 16 | ((i * 11 + 3) & 0xFF) << 8 | ((i * 5 + 1) & 0xFF));
    }
    Put32(v, 0xFEEDFACE);
    for (size_t i = 0; i < c.glyphs.size(); ++i) {
        Put16(v, static_cast<uint32_t>(3 + i * 9) % c.width);
        Put16(v, static_cast<uint32_t>(1 + i * 5) % c.height);
        Put16(v, static_cast<uint32_t>(2 + i * 3));
        Put16(v, static_cast<uint32_t>(5 + (i * 7) % 4));
    }
    return v;
}

std::vector<FontCase> FontCases()
{
    auto code = [](int c) { return static_cast<uint16_t>(static_cast<int16_t>(c)); };
    return {
        // Format 1: characters (one with a high byte), specials (one is the default glyph's, one past the map).
        {true, 1, {{4, 'A', 0}, {5, 'b', 0}, {6, code(-1), 0}, {7, code(-32), 0}, {8, code(-0x41), 0},
                   {9, 'I', 0}, {10, 0x12C, 0}, {11, code(-5), 0}, {12, 'a', 0}, {13, 0, 0}},
         13, 7, false, false},
        // Format 1 without a default glyph, from the file.
        {false, 1, {{1, 'I', 0}, {2, 'x', 0}, {3, code(-2), 0}, {4, code(-31), 0}}, 16, 4, false, false},
        // Format 2: chains of one to four characters.
        {false, 2, {{1, 0x4241, 0x4443}, {2, 0xFF45, 0x4746}, {3, 0x4948, 0x4AFF}, {4, 0x4C4B, 0xFF4D},
                    {5, 0x00FF, 0x0000}, {6, 0x7A79, 0x7877}, {7, 0x2120, 0xFFFF}},
         9, 9, false, false},
        // Format 2 from memory, three glyphs (fewer than the rectangles' 4-at-a-time loop takes).
        {true, 2, {{1, 0x4149, 0xFFFF}, {2, 0x6162, 0xFFFF}, {3, 0x3031, 0x3233}}, 5, 3, false, false},
        // Another format: no character map; four glyphs.
        {true, 7, {{1, 'I', 0}, {2, 'J', 0}, {3, 'K', 0}, {4, 'L', 0}}, 11, 2, false, false},
        // The texture can't be created / locked.
        {true, 1, {{1, 'I', 0}, {2, code(-32), 0}}, 4, 4, true, false},
        {false, 1, {{1, 'I', 0}, {2, code(-32), 0}}, 4, 4, false, true},
    };
}

std::vector<Saved> FontGlobals() { return TextGlobals(); }

// Every case above (the original and ours each from the same file data, in memory or "read" from the file): the
// calls, the font list, the whole arena (the font, its glyphs, what's freed), the texture's pixels. Then Unload
// at the head, in the middle, at the end, of a font not in the list (one font and two).
void TestLoadFont()
{
    std::vector<Saved> saved = FontGlobals();
    SaveAll(saved);
    std::vector<FontCase> cases = FontCases();
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInMemory(*s);
        StandInFontLoading(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            for (const FontCase& c : cases) {
                for (Saved& sv : saved)
                    std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
                GameVariable<bool>(0x007CC979) = true;
                GameVariable<void*>(0x007D1800) = g_arena;
                GameVariable<void*>(0x007D69E0) = &g_device;
                GameVariable<uint32_t>(0x00780FD8) = 0x0F0F0F0F; // the list before
                std::memset(g_arena, 0, sizeof(g_arena));
                g_arenaUsed = 0x100;
                std::memset(g_bits, 0x5A, sizeof(g_bits));
                g_fileData = FontFile(c);
                g_fileCursor = 0;
                g_createFails = c.createFails;
                g_lockFails = c.lockFails;
                const char* name = c.fromMemory ? reinterpret_cast<const char*>(g_fileData.data()) : "small_font";
                Nx::SFont* font = original ? Original<Nx::SFont*(__cdecl*)(const char*, bool)>(0x004CF0F0)(name, c.fromMemory)
                                           : Nx::SFont::Load(name, c.fromMemory);
                Record({0xF0, Rel(reinterpret_cast<uint32_t>(font)), Rel(GameVariable<uint32_t>(0x00780FD8)), g_fileCursor,
                        HashBytes(reinterpret_cast<const uint8_t*>(g_bits), sizeof(g_bits))});
                std::vector<uint32_t> image(sizeof(g_arena) / 4);
                std::memcpy(image.data(), g_arena, sizeof(g_arena));
                for (uint32_t& word : image)
                    word = Rel(word);
                g_trace.insert(g_trace.end(), image.begin(), image.end());
            }
            // Unload: fonts a, b, c in the arena, listed a -> b -> c.
            auto* fonts = reinterpret_cast<Nx::SFont*>(g_arena + 0x1000);
            auto unload = [&](Nx::SFont* font) {
                if (original)
                    Original<void(__cdecl*)(Nx::SFont*)>(0x004CFA80)(font);
                else
                    Nx::SFont::Unload(font);
                Record({0xFE, Rel(GameVariable<uint32_t>(0x00780FD8))});
                for (int i = 0; i < 4; ++i)
                    Record({Rel(reinterpret_cast<uint32_t>(fonts[i].next))});
            };
            auto list = [&](std::initializer_list<int> order) {
                std::memset(fonts, 0, 4 * sizeof(Nx::SFont));
                for (int i = 0; i < 4; ++i)
                    fonts[i].chars = reinterpret_cast<Nx::SChar*>(g_arena + 0x3000 + 0x100 * i);
                Nx::SFont* previous = nullptr;
                for (int i : order) {
                    if (previous)
                        previous->next = &fonts[i];
                    else
                        GameVariable<Nx::SFont*>(0x00780FD8) = &fonts[i];
                    previous = &fonts[i];
                }
            };
            list({0, 1, 2});
            unload(&fonts[1]); // middle
            unload(&fonts[0]); // head
            unload(&fonts[2]); // the only one
            list({0, 1, 2});
            unload(&fonts[2]); // the end
            unload(&fonts[3]); // not listed
            list({0});
            unload(&fonts[3]); // not listed, one font
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    g_fileData.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "text: SFont::Load (formats 1 and 2, defaults, texture, file or memory) and Unload");
    Check(traces[0].size() > 7 * 0x2000, "text: font loading test ran its cases");
}

// --- the device wrappers ------------------------------------------------------------------------------------

void TestDeviceWrappers()
{
    std::vector<Saved> saved = {{0x007D69E0, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        g_deviceVtable[0xE4 / 4] = s->Thunk(12, [](Regs& r) {
            Record({kRenderState, r.args[0] == reinterpret_cast<uint32_t>(&g_device), r.args[1], r.args[2]});
            return 0x100u + r.args[1] * 3 + r.args[2];
        });
        g_deviceVtable[0xAC / 4] = s->Thunk(28, [](Regs& r) {
            Record({kClear, r.args[0] == reinterpret_cast<uint32_t>(&g_device), r.args[1], r.args[2], r.args[3],
                    r.args[4], r.args[5], r.args[6]});
            return 0x88760000u + r.args[1] + r.args[3];
        });
        GameVariable<void*>(0x007D69E0) = &g_device;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            const uint32_t states[][2] = {{22, 1}, {137, 0}, {7, 0xFFFFFFFF}, {0x80000000u, 0x12345678}};
            for (const auto& st : states) {
                long r = original ? Original<long(__cdecl*)(int, uint32_t)>(0x0044F870)(static_cast<int>(st[0]), st[1])
                                  : Nx::SetRenderState(static_cast<int>(st[0]), st[1]);
                Record({static_cast<uint32_t>(r)});
            }
            const uint32_t rects[4] = {1, 2, 3, 4};
            struct ClearCase {
                uint32_t count;
                const void* rects;
                uint32_t flags, color;
                float z;
                uint32_t stencil;
            };
            const ClearCase clears[] = {{0, nullptr, 7, 0xFF102030, 1.0f, 0},
                                        {1, rects, 2, 0x00ABCDEF, 0.25f, 0x81},
                                        {5, rects, 0xFFFFFFFF, 0, -3.5e7f, 0xFFFFFFFF}};
            for (const ClearCase& c : clears) {
                long r = original ? Original<long(__cdecl*)(uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(
                                        0x0044F890)(c.count, c.rects, c.flags, c.color, c.z, c.stencil)
                                  : Nx::ClearDevice(c.count, c.rects, c.flags, c.color, c.z, c.stencil);
                Record({static_cast<uint32_t>(r)});
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "gfx: the device's SetRenderState / Clear wrappers");
    Check(traces[0].size() > 30, "gfx: device wrapper test ran its cases");
}

// --- Pcm: streams, user soundtracks, getters and setters ----------------------------------------------------

Pcm::Stream* Streams() { return reinterpret_cast<Pcm::Stream*>(0x0069B938); }
// Channel -> playing (indexed by channel + 1: -1 is all of them).
bool g_channelPlaying[8];
uint32_t g_soundtrackTracks[4];
const wchar_t* g_soundtrackNames[8];

std::vector<Saved> MusicGlobals()
{
    return {{0x0069806C, 0x2C + 3 * 0xCB0, {}}, {0x00698060, 12, {}}, {0x0069B938, 0x15C, {}},
            {0x0069BA94, 0x34, {}}, {0x0067D7E0, 0x1C, {}}, {0x00696D9C, 4 + 4 * 130, {}},
            {0x006976FC, 4 + 4 * 600, {}}, {0x00690FC8, 24, {}}, {0x007D6A38, 4, {}}, {0x007CEFA8, 4, {}},
            {0x00701590, 100, {}}};
}
void ResetMusicGlobals(std::vector<Saved>& saved)
{
    for (Saved& sv : saved)
        std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
    GameVariable<uint32_t>(0x00690FC8) = 0x0AA2FB3F; // both random streams: as InitialRand(77) leaves them
    GameVariable<uint32_t>(0x00690FCC) = 0x12B9B0A1;
    GameVariable<uint32_t>(0x00690FD0) = 77;
    GameVariable<uint32_t>(0x00690FD4) = 0x0AA2FB3F;
    GameVariable<uint32_t>(0x00690FD8) = 0x12B9B0A1;
    GameVariable<uint32_t>(0x00690FDC) = 77;
}

void StandInChannels(StandIns& s)
{
    s.At({0x00453270}, 0, [](Regs& r) {
        Record({0xC4A, r.args[0]});
        int channel = static_cast<int>(r.args[0]);
        return channel >= -1 && channel < 7 && g_channelPlaying[channel + 1] ? 2u : 1u;
    });
}

// The stream lookups over streams sharing ids and names, playing or not; FreeStreamId over runs of used ids; the
// getters and setters; the user soundtrack's shuffle (counts 0, 1, 2, 3, 5, 700 - capped at 600 - many times, so
// the last track comes first and is moved) and selection (shuffled or not); the soundtrack names (long, empty,
// none, characters that become '~'), also through the thunks 0x0048C150 / 0x0048C160.
void TestMusicStreams()
{
    std::vector<Saved> saved = MusicGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInChannels(*s);
        s->At({0x004531A0}, 0, [](Regs&) {
            Record({0x5709});
            return 0x77u;
        });
        s->At({0x004542D0}, 0, [](Regs&) {
            Record({0x4E4D});
            return 7u;
        });
        s->At({0x00454320}, 0, [](Regs& r) {
            Record({0x7C7, r.args[0]});
            return r.args[0] < 4 ? g_soundtrackTracks[r.args[0]] : 0u;
        });
        s->At({0x00454300}, 0, [](Regs& r) {
            Record({0x4A3E, r.args[0]});
            return r.args[0] < 8 ? reinterpret_cast<uint32_t>(g_soundtrackNames[r.args[0]]) : 0u;
        });
        s->At({0x00453400}, 0, [](Regs& r) {
            Record({0x701, r.args[0]});
            return 1u;
        });
        std::wstring longName;
        for (int i = 0; i < 130; ++i)
            longName += static_cast<wchar_t>(L'a' + i % 26);
        std::wstring odd = L"x";
        for (unsigned c : {0x00A0u, 0x00A1u, 0x00A5u, 0x00ACu, 0x00ADu, 0x00E9u, 0x4E2Du, 0x20ACu, 0x0041u})
            odd += static_cast<wchar_t>(c);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            ResetMusicGlobals(saved);
            // Streams: 0 id 5 name 9 channel 0; 1 id 9 name 5 channel 1; 2 id 5 name 3 channel 3.
            const uint32_t ids[3] = {5, 9, 5}, names[3] = {9, 5, 3};
            const int channels[3] = {0, 1, 3};
            for (int i = 0; i < 3; ++i) {
                Streams()[i].id = ids[i];
                Streams()[i].name = names[i];
                Streams()[i].channel = channels[i];
            }
            const bool playing[][3] = {{false, false, false}, {true, false, false}, {false, true, false},
                                       {false, false, true}, {true, true, true}, {false, true, true}};
            for (const auto& p : playing) {
                for (int i = 0; i < 3; ++i)
                    g_channelPlaying[channels[i] + 1] = p[i];
                for (uint32_t id : {3u, 5u, 9u, 4u}) {
                    bool finished = original ? Original<bool(__cdecl*)(uint32_t)>(0x00450220)(id) : Pcm::StreamIdFinished(id);
                    int found = original ? Original<int(__cdecl*)(uint32_t)>(0x00450260)(id) : Pcm::FindPlayingStream(id);
                    uint32_t free = original ? Original<uint32_t(__cdecl*)(uint32_t)>(0x004502D0)(id) : Pcm::FreeStreamId(id);
                    Record({0x1D, finished, static_cast<uint32_t>(found), free});
                }
            }
            // Ids 5 6 7 in use: from 5 the first free one is 8.
            for (int i = 0; i < 3; ++i) {
                Streams()[i].id = 5 + i;
                g_channelPlaying[channels[i] + 1] = true;
            }
            for (uint32_t id : {4u, 5u, 6u, 8u}) {
                uint32_t free = original ? Original<uint32_t(__cdecl*)(uint32_t)>(0x004502D0)(id) : Pcm::FreeStreamId(id);
                Record({0xF8EE, free});
            }

            // Getters and setters.
            for (int v = 0; v < 3; ++v) {
                GameVariable<uint8_t>(0x0069BAA8) = static_cast<uint8_t>(v * 3);
                GameVariable<uint32_t>(0x0067D7E4) = 0x42480000u + v;
                GameVariable<uint32_t>(0x0067D7E8) = 0x42C80000u - v;
                GameVariable<uint32_t>(0x0067D7EC) = 0xABCDEF00u + static_cast<uint32_t>(v * 0x81);
                bool paused = original ? Original<bool(__cdecl*)()>(0x00450430)() : Pcm::MusicIsPaused();
                float music = original ? Original<float(__cdecl*)()>(0x00450440)() : Pcm::GetMusicVolume();
                float sfx = original ? Original<float(__cdecl*)()>(0x00450450)() : Pcm::GetSfxVolume();
                int random = original ? Original<int(__cdecl*)()>(0x00450460)() : Pcm::RandomTrackOrder();
                Record({0x6E7, paused, Bits(music), Bits(sfx), static_cast<uint32_t>(random)});
                bool loop = v == 1;
                if (original)
                    Original<void(__cdecl*)(bool)>(0x00450470)(loop);
                else
                    Pcm::SetMusicLooping(loop);
                Record({GameVariable<uint32_t>(0x0069BAA0)});
                uint64_t low = 0x1122334455667788ull * (v + 1), high = 0x0F0E0D0C0B0A0908ull ^ (v * 0x101);
                if (original)
                    Original<void(__cdecl*)(uint64_t, uint64_t)>(0x004504B0)(low, high);
                else
                    Pcm::SetTracksOff(low, high);
                uint64_t got[2] = {0, 0};
                if (original)
                    Original<void(__cdecl*)(uint64_t*, uint64_t*)>(0x00450480)(&got[0], &got[1]);
                else
                    Pcm::GetTracksOff(&got[0], &got[1]);
                Record({static_cast<uint32_t>(got[0]), static_cast<uint32_t>(got[0] >> 32), static_cast<uint32_t>(got[1]),
                        static_cast<uint32_t>(got[1] >> 32)});
                GameVariable<int>(0x0069BA9C) = v;
                float volume = 12.5f * static_cast<float>(v + 1);
                if (original)
                    Original<void(__cdecl*)(float)>(0x004504E0)(volume);
                else
                    Pcm::SetUserSoundtrackVolume(volume);
                GameVariable<uint8_t>(0x0069BAB0) = 0xEE;
                if (original)
                    Original<void(__cdecl*)()>(0x00450410)();
                else
                    Pcm::UseStandardSoundtrack();
                Record({GameVariable<uint32_t>(0x0069BAB0)});
            }
            if (original)
                Original<void(__cdecl*)()>(0x00450420)();
            else
                Pcm::StopUserSoundtrack();
            for (uint32_t thunk : {0x004B2350u, 0x0048C150u}) {
                int n = original ? Original<int(__cdecl*)()>(thunk)() : Pcm::GetNumSoundtracks();
                Record({0x4E, static_cast<uint32_t>(n)});
            }

            // The user soundtrack's shuffle.
            g_soundtrackTracks[0] = 5;
            g_soundtrackTracks[1] = 700;
            g_soundtrackTracks[2] = 0;
            g_soundtrackTracks[3] = 1;
            auto order = [&]() {
                Record({0x0D, GameVariable<uint32_t>(0x00690FC8), GameVariable<uint32_t>(0x00690FCC),
                        GameVariable<uint32_t>(0x00690FD0), GameVariable<uint32_t>(0x0069BAB0),
                        GameVariable<uint32_t>(0x0069BAB4), GameVariable<uint32_t>(0x0069BAB8),
                        GameVariable<uint32_t>(0x0069BAC0),
                        HashBytes(reinterpret_cast<const uint8_t*>(0x006976FC), 4 + 4 * 600)});
            };
            for (int i = 0; i < 600; ++i)
                GameVariable<int>(0x00697700 + 4 * i) = 1000 + i;
            for (uint32_t soundtrack : {2u, 3u, 3u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}) {
                GameVariable<uint32_t>(0x0069BAB4) = soundtrack;
                if (original)
                    Original<void(__cdecl*)()>(0x00450310)();
                else
                    Pcm::ShuffleUserSoundtrack();
                order();
            }
            g_soundtrackTracks[0] = 2;
            for (int i = 0; i < 12; ++i) {
                GameVariable<uint32_t>(0x0069BAB4) = 0;
                if (original)
                    Original<void(__cdecl*)()>(0x00450310)();
                else
                    Pcm::ShuffleUserSoundtrack();
                order();
            }
            g_soundtrackTracks[0] = 3;
            for (int i = 0; i < 12; ++i) {
                GameVariable<int>(0x0069BAB4) = 0;
                if (original)
                    Original<void(__cdecl*)()>(0x00450310)();
                else
                    Pcm::ShuffleUserSoundtrack();
                order();
            }
            for (int shuffle = 0; shuffle < 2; ++shuffle) {
                for (int soundtrack : {0, 1, 2}) {
                    GameVariable<uint8_t>(0x0069BABC) = static_cast<uint8_t>(shuffle);
                    GameVariable<uint32_t>(0x0069BAB0) = 0;
                    GameVariable<uint32_t>(0x0069BAB8) = 0x55;
                    GameVariable<uint32_t>(0x0069BAC0) = 0x66;
                    if (original)
                        Original<void(__cdecl*)(int)>(0x004503D0)(soundtrack);
                    else
                        Pcm::UseUserSoundtrack(soundtrack);
                    order();
                }
            }

            // Soundtrack names.
            g_soundtrackNames[0] = L"Rock";
            g_soundtrackNames[1] = longName.c_str();
            g_soundtrackNames[2] = L"";
            g_soundtrackNames[3] = odd.c_str();
            g_soundtrackNames[4] = nullptr;
            for (uint32_t index : {0u, 1u, 2u, 3u, 4u, 9u, 0u}) {
                for (uint32_t entry : {0x004B2360u, 0x0048C160u}) {
                    std::memset(reinterpret_cast<void*>(0x00701590), 0x41 + static_cast<int>(index), 100);
                    const char* name = original ? Original<const char*(__cdecl*)(int)>(entry)(static_cast<int>(index))
                                                : Pcm::GetSoundtrackName(static_cast<int>(index));
                    Record({0x5A3E, reinterpret_cast<uint32_t>(name),
                            HashBytes(reinterpret_cast<const uint8_t*>(0x00701590), 100)});
                }
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1],
            "music: stream lookups, getters / setters, user soundtrack shuffle and selection, soundtrack names");
    Check(traces[0].size() > 300, "music: streams test ran its cases");
}

// --- Pcm: the next track in order, the per-frame update -----------------------------------------------------

int g_streamState;
int g_streamsResult;
bool g_streamStillOn[3];
alignas(16) uint8_t g_params[16];
alignas(16) uint32_t g_owners[3][12];
alignas(16) uint32_t g_listeners[3][2];

void StandInTrackStart(StandIns& s)
{
    s.At({0x00453250}, 0, [](Regs&) {
        Record({0x5757});
        return static_cast<uint32_t>(g_streamState);
    });
    s.At({0x00451400}, 0, [](Regs&) {
        Record({0x7011});
        return 0u;
    });
    s.At({0x004520D0}, 0, [](Regs& r) {
        Record({0x91A7, r.args[0] - 0x0069806C, r.args[1]});
        return 0u;
    });
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, [](Regs&) {
        Record({0x4E5});
        return reinterpret_cast<uint32_t>(g_params);
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1417, r.ecx == reinterpret_cast<uint32_t>(g_params)});
        return r.ecx;
    });
    s.At({0x00477B80, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Record({0xADD1, r.ecx == reinterpret_cast<uint32_t>(g_params), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5C41, r.args[0], r.args[1] == reinterpret_cast<uint32_t>(g_params), r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA, r.ecx == reinterpret_cast<uint32_t>(g_params)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0xDE15, r.args[0] == reinterpret_cast<uint32_t>(g_params)});
        return 0u;
    });
}

Pcm::Playlist* Lists() { return reinterpret_cast<Pcm::Playlist*>(0x00698098); }

void MusicState()
{
    Record({0x57A7, GameVariable<uint32_t>(0x0069BA98), GameVariable<uint32_t>(0x0069BAAC),
            GameVariable<uint32_t>(0x0067D7F0)});
    for (int l = 0; l < 3; ++l)
        g_trace.insert(g_trace.end(), reinterpret_cast<uint32_t*>(&Lists()[l].played[0]),
                       reinterpret_cast<uint32_t*>(&Lists()[l].played[0]) + 4);
}

// In order: sound off (three ways), no next track, a stream playing, looping, an empty and a negative playlist,
// then 25 tracks of a 20-track playlist (tracks off, some played already: the round restarts) and 110 of a
// 100-track one (tracks off in both halves; the round restarts).
void TestPlayNextTrackInOrder()
{
    std::vector<Saved> saved = MusicGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInTrackStart(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            ResetMusicGlobals(saved);
            auto next = [&]() {
                if (original)
                    Original<void(__cdecl*)()>(0x00452680)();
                else
                    Pcm::PlayNextTrackInOrder();
                MusicState();
            };
            Lists()[0].count = 20;
            Lists()[0].off[0] = (1ull << 0) | (1ull << 7) | (1ull << 19) | (1ull << 40);
            Lists()[0].off[1] = 1;
            Lists()[0].played[0] = (1ull << 1) | (1ull << 2);
            Lists()[1].count = -3;
            Lists()[2].count = 100;
            Lists()[2].off[0] = (1ull << 5) | (1ull << 63);
            Lists()[2].off[1] = (1ull << 2) | (1ull << 30);
            Lists()[2].played[1] = 1ull << 7;
            GameVariable<uint32_t>(0x0069BAAC) = 0x55;
            g_streamState = 1;
            GameVariable<int>(0x007CEFA8) = 1;
            GameVariable<bool>(0x0067D7E0) = true;
            next(); // no sound
            GameVariable<void*>(0x007D6A38) = reinterpret_cast<void*>(0xD50D50);
            GameVariable<int>(0x007CEFA8) = 2;
            next();
            GameVariable<int>(0x007CEFA8) = 1;
            GameVariable<bool>(0x0067D7E0) = false;
            next(); // music off
            GameVariable<bool>(0x0067D7E0) = true;
            Lists()[0].unknownCA8 = 1;
            next();
            Lists()[0].unknownCA8 = 0;
            g_streamState = 2;
            next();
            g_streamState = 0;
            next();
            g_streamState = 1;
            GameVariable<bool>(0x0069BAA0) = true;
            next(); // looping
            GameVariable<bool>(0x0069BAA0) = false;
            GameVariable<int>(0x0069BA9C) = 1;
            next(); // negative count
            Lists()[1].count = 0;
            next();
            GameVariable<int>(0x0069BA9C) = 0;
            for (int i = 0; i < 25; ++i)
                next();
            GameVariable<int>(0x0069BA9C) = 2;
            for (int i = 0; i < 110; ++i) {
                if (i == 50)
                    Lists()[2].played[1] |= 1ull << 3; // played since
                next();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "music: the next track in order (0x00452680)");
    Check(traces[0].size() > 1500, "music: next track in order test ran its cases");
}

struct UpdateCase {
    bool sound;         // 0x007D6A38 set
    int platform;       // 0x007CEFA8
    bool music, sfx;    // 0x0067D7E0 / E1
    int streamsResult;  // 0x00453940's
    int frame;          // the frame counter before
    uint32_t volume;    // the music volume's bits
    int playlist;
    bool paused;
    uint8_t randomOrder;
};

// The update: sound off (three ways), the stream system's result (-1 0 1) with streamed sound on or off, owners
// whose streams are over or not, listeners of streams that stopped (cleared or already clear) or play on, the
// 64th frame or not (the 32nd too), the volume 0 / -0 / NaN / 50 with playlist 0, 1 and 2, paused, the random order on or off.
// PlayNextTrack / PlayNextTrackInOrder are stood in for.
void TestMusicUpdate()
{
    std::vector<Saved> saved = MusicGlobals();
    SaveAll(saved);
    const uint32_t kZero = 0, kMinusZero = 0x80000000u, kNaN = 0x7FC00000u, kFifty = 0x42480000u;
    std::vector<UpdateCase> cases = {
        {false, 1, true, true, 1, 63, kFifty, 0, false, 1},
        {true, 2, true, true, 1, 63, kFifty, 0, false, 1},
        {true, 1, false, false, 1, 63, kFifty, 0, false, 1},
        {true, 1, false, true, 1, 63, kFifty, 0, false, 1},
        {true, 1, true, false, 1, 63, kFifty, 0, false, 1},
        {true, 1, true, false, -1, 63, kFifty, 0, false, 0},
        {true, 1, true, true, -1, 62, kFifty, 0, false, 1},
        {true, 1, true, true, 0, 127, kFifty, 2, false, 0},
        {true, 1, true, true, 5, 63, kZero, 0, false, 1},
        {true, 1, true, true, 1, 63, kZero, 1, false, 0},
        {true, 1, true, true, 1, 63, kMinusZero, 2, false, 1},
        {true, 1, true, true, 1, 63, kNaN, 0, false, 0},
        {true, 1, true, true, 1, 63, kFifty, 0, true, 0},
        {true, 1, true, true, 1, 63, kFifty, 0, false, 0},
        {true, 1, true, true, 1, 63, kFifty, 1, false, 0},
        {true, 1, true, true, 1, 63, kFifty, 2, false, 0},
        {true, 1, true, true, 1, 0x13F, kFifty, 2, false, 2},
        {true, 1, true, true, 1, 64, kFifty, 0, false, 0},
        {true, 1, true, true, 1, 31, kFifty, 0, false, 0},
        {true, 1, false, true, 0, -1, kFifty, 1, false, 0},
    };
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInChannels(*s);
        s->At({0x00453940}, 0, [](Regs&) {
            Record({0x5940});
            return static_cast<uint32_t>(g_streamsResult);
        });
        s->At({0x00453BD0}, 0, [](Regs&) {
            Record({0x5BD0});
            return 0u;
        });
        s->At({0x00450650}, 0, [](Regs&) {
            Record({0x0650});
            return 0u;
        });
        s->At({0x00450BE0}, 0, [](Regs& r) {
            Record({0x0BE0, r.args[0]});
            return r.args[0] < 3 && g_streamStillOn[r.args[0]] ? 0xFFFFFF01u : 0xFFFFFF00u;
        });
        s->At({0x00452680, AddressOf(&Pcm::PlayNextTrackInOrder)}, 0, [](Regs&) {
            Record({0x2680});
            return 0u;
        });
        s->At({0x00452200, AddressOf(&Pcm::PlayNextTrack)}, 0, [](Regs&) {
            Record({0x2200});
            return 0u;
        });
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            int n = 0;
            for (const UpdateCase& c : cases) {
                ResetMusicGlobals(saved);
                GameVariable<void*>(0x007D6A38) = c.sound ? reinterpret_cast<void*>(0xD50D50) : nullptr;
                GameVariable<int>(0x007CEFA8) = c.platform;
                GameVariable<bool>(0x0067D7E0) = c.music;
                GameVariable<bool>(0x0067D7E1) = c.sfx;
                g_streamsResult = c.streamsResult;
                GameVariable<int>(0x0067D7F0) = c.frame;
                GameVariable<uint32_t>(0x0067D7E4) = c.volume;
                GameVariable<int>(0x0069BA9C) = c.playlist;
                GameVariable<bool>(0x0069BAA8) = c.paused;
                GameVariable<uint8_t>(0x0067D7EC) = c.randomOrder;
                // Owners: stream 0's over, stream 1's on, stream 2 none (or, every other case, all three over).
                std::memset(g_owners, 0x33, sizeof(g_owners));
                for (int i = 0; i < 3; ++i) {
                    g_streamStillOn[i] = (n & 1) ? false : i == 1;
                    GameVariable<uint32_t*>(0x00698060 + 4 * i) = (i == 2 && !(n & 1)) ? nullptr : g_owners[i];
                }
                // Listeners: stream 0 stopped (words set), stream 1 stopped (words clear), stream 2 playing.
                for (int i = 0; i < 3; ++i) {
                    Streams()[i].channel = 2 * i + (n & 1);
                    g_listeners[i][0] = i == 1 ? 0 : 0x1111 * (i + 1);
                    g_listeners[i][1] = 0x2222 * (i + 1);
                    Streams()[i].listener = (n % 3 == 2 && i == 0) ? nullptr : g_listeners[i];
                }
                std::memset(g_channelPlaying, 0, sizeof(g_channelPlaying));
                g_channelPlaying[Streams()[2].channel + 1] = true;
                if (original)
                    Original<void(__cdecl*)()>(0x00452910)();
                else
                    Pcm::Update();
                Record({0x0F, GameVariable<uint32_t>(0x0067D7F0), HashBytes(reinterpret_cast<const uint8_t*>(g_owners), sizeof(g_owners)),
                        HashBytes(reinterpret_cast<const uint8_t*>(g_listeners), sizeof(g_listeners))});
                for (int i = 0; i < 3; ++i)
                    Record({GameVariable<uint32_t>(0x00698060 + 4 * i) ? 1u : 0u, Streams()[i].listener ? 1u : 0u});
                ++n;
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "music: the per-frame update (0x00452910)");
    Check(traces[0].size() > 200, "music: update test ran its cases");
}

} // namespace

SELFTEST(Early, TestSTextDrawing, RemoveStandIns);
SELFTEST(Early, TestLoadFont, RemoveStandIns);
SELFTEST(Early, TestDeviceWrappers, RemoveStandIns);
SELFTEST(Early, TestMusicStreams, RemoveStandIns);
SELFTEST(Early, TestPlayNextTrackInOrder, RemoveStandIns);
SELFTEST(Early, TestMusicUpdate, RemoveStandIns);
