// Self-tests for the platform layer's Direct3D and system sides (src/game/sys/platform_d3d.cpp,
// platform_system.cpp): the original and ours with recording stand-ins for the Direct3D device, its buffers and
// queries, DirectSound and the Windows calls (drives, the window class and window) - the palette, the buffers, the
// dynamic vertices' locks, occlusion queries over random sequences, the release; the CD search, the game window,
// DirectSound, the ADPCM decoder and the unswizzle.

#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <dsound.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include "game/sys/platform_d3d.h"
#include "game/sys/platform_system.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AppMainTests;

// IDirect3DQuery9 and IDirectSound8: argument counts (after `this`) in vtable order.
const uint8_t kQueryArgs[8] = {2, 0, 0, 1, 0, 0, 1, 3};
const uint8_t kDirectSoundArgs[12] = {2, 0, 0, 3, 1, 2, 2, 0, 1, 1, 1, 1};
enum : uint32_t { kDeviceTag = 0xD3D00000, kVertexTag = 0xB0100000, kIndexTag = 0xB0200000, kQueryTag = 0xC0000000,
                  kSoundTag = 0xD5000000 };
enum { kQueryIssue = 6, kQueryGetData = 7, kSoundSetCooperativeLevel = 6, kCreateQuery = 118 };

int g_mismatches;
bool CompareRuns(const char* what, const std::function<void(bool)>& body)
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_trace.clear();
        body(run == 0);
        traces[run] = g_trace;
    }
    if (traces[0] == traces[1])
        return true;
    if (++g_mismatches <= 6) {
        Log("  %s: trace sizes original %u ours %u", what, static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
        for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i) {
            if (traces[0][i] != traces[1][i]) {
                Log("  %s entry %u: original %08X ours %08X", what, static_cast<unsigned>(i), traces[0][i],
                    traces[1][i]);
                ++shown;
            }
        }
    }
    return false;
}

std::deque<uint32_t> g_results; // what the stand-ins answer, in order (0 once it runs out)
uint32_t NextResult()
{
    if (g_results.empty())
        return 0;
    uint32_t result = g_results.front();
    g_results.pop_front();
    return result;
}

uint32_t HashText(const char* text)
{
    uint32_t hash = 2166136261u;
    for (; text && *text; ++text)
        hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619u;
    return text ? hash : 0;
}

void TestPlatformD3D()
{
    g_mismatches = 0;
    GameMemory memory({{0x007D69DC, 0x20}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDeviceTag);
    FakeCom* direct3D = com.Direct3D();
    FakeCom* vertices = com.Buffer(kVertexTag);
    FakeCom* indices = com.Buffer(kIndexTag);
    FakeCom* queries[64];
    for (int i = 0; i < 64; ++i)
        queries[i] = com.Make(kQueryTag + 0x10000 * i, kQueryArgs, 8);
    int nextQuery = 0;
    alignas(16) static uint8_t lockArena[0x10000];
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) {
        auto base = reinterpret_cast<uint32_t>(lockArena);
        if (word >= base && word <= base + sizeof(lockArena))
            return 0xA0000000u + (word - base);
        return standard(word);
    };
    // The original's hash_map (0x007D69FC) allocates through the game's new / delete: malloc / free here.
    s.At({0x004015D0}, 0, [](Regs& r) { return reinterpret_cast<uint32_t>(std::malloc(r.args[0])); });
    s.At({0x00401000}, 0, [](Regs& r) {
        std::free(reinterpret_cast<void*>(r.args[0]));
        return 0u;
    });
    com.On(kDeviceTag, kCreateVertexBuffer, [&](Regs& r) {
        *reinterpret_cast<void**>(r.args[5]) = vertices;
        return NextResult();
    });
    com.On(kDeviceTag, kCreateIndexBuffer, [&](Regs& r) {
        *reinterpret_cast<void**>(r.args[5]) = indices;
        return NextResult();
    });
    for (uint32_t tag : {kVertexTag, kIndexTag}) {
        com.On(tag, 11, [](Regs& r) { // Lock
            *reinterpret_cast<void**>(r.args[3]) = lockArena;
            return 0u;
        });
        com.On(tag, 12, [](Regs&) { return NextResult(); }); // Unlock
    }
    com.On(kDeviceTag, kCreateQuery, [&](Regs& r) {
        uint32_t result = NextResult();
        if (static_cast<int32_t>(result) >= 0) // (a query for every result that isn't a failure)
            *reinterpret_cast<void**>(r.args[2]) = queries[nextQuery++ % 64];
        return result;
    });
    for (int i = 0; i < 64; ++i) {
        com.On(kQueryTag + 0x10000 * i, kQueryIssue, [](Regs&) { return NextResult(); });
        com.On(kQueryTag + 0x10000 * i, kQueryGetData, [](Regs& r) {
            *reinterpret_cast<DWORD*>(r.args[1]) = 0x1000 + r.args[0] % 997;
            return NextResult();
        });
    }
    auto& deviceSlot = GameVariable<FakeCom*>(0x007D69E0);
    bool ok = true;
    Lcg random(0);

    // The palette.
    ok &= CompareRuns("ExpandPalette", [&](bool original) {
        random = Lcg(0x45E0);
        uint32_t palette[256], out[300];
        uint8_t in[300];
        for (uint32_t& c : palette)
            c = random.Next();
        for (uint8_t& b : in)
            b = static_cast<uint8_t>(random.Next());
        for (int count : {0, 1, 7, 300, -5}) {
            std::memset(out, 0xEE, sizeof(out));
            if (original)
                Original<void(__cdecl*)(uint32_t*, const uint8_t*, int, const uint32_t*)>(0x005F45E0)(out, in, count,
                                                                                                  palette);
            else
                Platform::ExpandPalette(out, in, count, palette);
            for (uint32_t c : out)
                Record({c});
        }
    });

    // The buffers: made (failing or not), the quads' indices, released; the dynamic vertices' locks over random
    // sizes and strides (wrapping round, room asked for or not).
    for (int c = 0; c < 40; ++c) {
        ok &= CompareRuns("dynamic buffers", [&](bool original) {
            random = Lcg(0x4620 + c);
            g_results.clear();
            memory.Fill(0x4620 + c);
            deviceSlot = device;
            std::memset(lockArena, 0xCD, sizeof(lockArena));
            uint32_t size = 0x400 + random.Below(0x100) * 32; // (32-byte multiples: whole-buffer fits happen)
            g_results = {c % 7 == 6 ? 0x8876086Cu : 0u};
            HRESULT made = original ? Original<HRESULT(__cdecl*)(uint32_t)>(0x005F4620)(size)
                                    : Platform::CreateDynamicVertexBuffer(size);
            uint32_t quads = random.Below(600);
            g_results = {0, c % 5 == 4 ? 0x88760827u : 0u};
            HRESULT quadsMade = original ? Original<HRESULT(__cdecl*)(uint32_t)>(0x005F4690)(quads)
                                         : Platform::CreateQuadIndexBuffer(quads);
            Record({static_cast<uint32_t>(made), static_cast<uint32_t>(quadsMade)});
            for (uint32_t i = 0; i < quads * 3 && i < sizeof(lockArena) / 4; ++i)
                Record({reinterpret_cast<uint32_t*>(lockArena)[i]});
            for (int step = 0; step < 30; ++step) {
                uint32_t stride = 1 + random.Below(48), count = random.Below(200);
                if (step % 7 == 6) { // exactly the room left (the write offset is 32-byte aligned after an unlock)
                    stride = 32;
                    count = (size - GameVariable<uint32_t>(0x007D69F0)) / 32;
                }
                IDirect3DVertexBuffer9* buffer = nullptr;
                uint32_t first = 0xEEEE, room = 0xEEEE;
                uint32_t* roomOut = random.Below(3) ? &room : nullptr;
                void* where = original ? Original<void*(__cdecl*)(uint32_t, uint32_t, IDirect3DVertexBuffer9**,
                                                                     uint32_t*, uint32_t*)>(0x005F4740)(
                                             count, stride, &buffer, &first, roomOut)
                                       : Platform::LockDynamicVertices(count, stride, &buffer, &first, roomOut);
                Record({Relocate(reinterpret_cast<uint32_t>(where)), Relocate(reinterpret_cast<uint32_t>(buffer)),
                        first, room});
                memory.Dump(g_trace);
                if (original)
                    Original<void(__cdecl*)(uint32_t, uint32_t)>(0x005F4800)(count, stride);
                else
                    Platform::UnlockDynamicVertices(count, stride);
                memory.Dump(g_trace);
            }
            for (int release = 0; release < 2; ++release) {
                if (original)
                    Original<void(__cdecl*)()>(release ? 0x005F4670 : 0x005F4650)();
                else if (release)
                    Platform::ReleaseQuadIndexBuffer();
                else
                    Platform::ReleaseDynamicVertexBuffer();
            }
            memory.Dump(g_trace);
        });
    }

    // Occlusion queries: random begins (made or not, issued or not), ends under keys (taken or not, issued or not),
    // results (ready, not yet, failing, no such key), releases of them all, and the whole Direct3D release - the
    // queries' releases compared as a set where the original's hash_map and ours go through them in their orders.
    for (int c = 0; c < 60; ++c) {
        ok &= CompareRuns("occlusion queries", [&](bool original) {
            random = Lcg(0x4840 + c);
            g_results.clear();
            memory.Fill(0x4840 + c);
            memory.Zero();
            deviceSlot = device;
            nextQuery = 0;
            if (original)
                Original<void*(__thiscall*)(void*)>(0x005F52B0)(reinterpret_cast<void*>(0x007D69FC)); // its map made
            const uint32_t kErrors[] = {0, 0, 0, 1, 0x88760868, 0x80004005};
            for (int step = 0; step < 400; ++step) {
                uint32_t op = random.Below(c % 4 == 3 ? 40 : 12);
                uint32_t key = random.Below(c % 2 ? 6 : 40) * 0x1001;
                HRESULT result;
                if (op < 3) {
                    g_results = {kErrors[random.Below(6)], kErrors[random.Below(6)]};
                    result = original ? Original<HRESULT(__cdecl*)()>(0x005F4840)() : Platform::BeginOcclusionQuery();
                } else if (op < 6) {
                    g_results = {kErrors[random.Below(6)]};
                    result = original ? Original<HRESULT(__cdecl*)(uint32_t)>(0x005F5650)(key)
                                      : Platform::EndOcclusionQuery(key);
                } else if (op < 11) {
                    g_results = {kErrors[random.Below(6)]};
                    DWORD pixels = 0xEEEE;
                    result = original ? Original<HRESULT(__cdecl*)(uint32_t, DWORD*)>(0x005F4C80)(key, &pixels)
                                      : Platform::GetOcclusionResult(key, &pixels);
                    Record({pixels});
                } else {
                    size_t mark = g_trace.size();
                    if (op == 11) {
                        if (original)
                            Original<void(__cdecl*)()>(0x005F52D0)();
                        else
                            Platform::ReleaseOcclusionQueries();
                    } else {
                        GameVariable<FakeCom*>(0x007D69DC) = random.Below(2) ? direct3D : nullptr;
                        GameVariable<FakeCom*>(0x007D69E4) = random.Below(2) ? vertices : nullptr;
                        GameVariable<FakeCom*>(0x007D69E8) = random.Below(2) ? indices : nullptr;
                        GameVariable<uint32_t>(0x007D69F4) = random.Next(); // (the size, cleared)
                        if (original)
                            Original<void(__cdecl*)()>(0x005F55E0)();
                        else
                            Platform::ReleaseDirect3D();
                        deviceSlot = device;
                    }
                    std::sort(g_trace.begin() + static_cast<ptrdiff_t>(mark), g_trace.end());
                    result = 0;
                }
                Record({op, static_cast<uint32_t>(result)});
                memory.Dump(g_trace);
            }
            // (everything let go before the next case)
            size_t mark = g_trace.size();
            if (original) {
                Original<void(__cdecl*)()>(0x005F52D0)();
                Original<void(__thiscall*)(void*)>(0x005F5180)(reinterpret_cast<void*>(0x007D69FC)); // its map gone
            } else {
                Platform::ReleaseOcclusionQueries();
            }
            std::sort(g_trace.begin() + static_cast<ptrdiff_t>(mark), g_trace.end());
        });
    }
    g_relocate = nullptr;
    delete &s;
    Check(ok, "platform: the Direct3D side - palette, buffers, dynamic vertices, occlusion queries, release");
}

// The system side.
struct ApiScript {
    DWORD drives = 0;
    uint32_t types = 0;      // a CD-ROM where bit n is set (the drive n)
    uint32_t named = 0;      // GetVolumeInformation succeeds (with the name) where bit n is set
    const char* name = "";
    int screen[2] = {1920, 1080};
};
ApiScript g_api;

void TestPlatformSystem()
{
    g_mismatches = 0;
    GameMemory memory({{0x007D6A24, 0x18}, {0x0068E0BC, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* sound = com.Make(kSoundTag, kDirectSoundArgs, 12);
    UseStandardRelocation(com);
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    HMODULE user = GetModuleHandleA("user32.dll");
    StandInApi(s, 0x00645168, reinterpret_cast<const void*>(GetProcAddress(kernel, "GetLogicalDrives")), 0,
               [](Regs&) {
                   Record({0x1D});
                   return g_api.drives;
               });
    StandInApi(s, 0x00645164, reinterpret_cast<const void*>(GetProcAddress(kernel, "GetDriveTypeA")), 4,
               [](Regs& r) {
                   char letter = reinterpret_cast<const char*>(r.args[0])[0];
                   Record({0x17, static_cast<uint32_t>(letter), HashText(reinterpret_cast<const char*>(r.args[0]))});
                   // (a CD-ROM where the bit is set, else removable / fixed / remote / RAM disk)
                   const uint32_t kOthers[] = {DRIVE_REMOVABLE, DRIVE_FIXED, DRIVE_REMOTE, DRIVE_RAMDISK};
                   bool cd = (g_api.types >> (letter - 'A')) & 1;
                   return cd ? static_cast<uint32_t>(DRIVE_CDROM) : kOthers[letter % 4];
               });
    StandInApi(s, 0x00645160, reinterpret_cast<const void*>(GetProcAddress(kernel, "GetVolumeInformationA")), 32,
               [](Regs& r) {
                   char letter = reinterpret_cast<const char*>(r.args[0])[0];
                   Record({0x101, static_cast<uint32_t>(letter), r.args[2], r.args[4], r.args[5], r.args[6], r.args[7],
                           r.args[3] != 0});
                   if (!((g_api.named >> (letter - 'A')) & 1))
                       return 0u;
                   const char* name = (letter & 1) ? g_api.name : "OTHER";
                   std::memcpy(reinterpret_cast<char*>(r.args[1]), name, std::strlen(name) + 1);
                   return 1u;
               });
    StandInApi(s, 0x00645238, reinterpret_cast<const void*>(GetProcAddress(user, "LoadIconA")), 8, [](Regs& r) {
        Record({0x1C0, r.args[0], r.args[1]});
        return 0x1C0Eu;
    });
    StandInApi(s, 0x0064523C, reinterpret_cast<const void*>(GetProcAddress(user, "RegisterClassExA")), 4,
               [](Regs& r) {
                   auto* c = reinterpret_cast<const WNDCLASSEXA*>(r.args[0]);
                   Record({0xC1A55, c->cbSize, c->style, reinterpret_cast<uint32_t>(c->lpfnWndProc),
                           static_cast<uint32_t>(c->cbClsExtra), static_cast<uint32_t>(c->cbWndExtra),
                           reinterpret_cast<uint32_t>(c->hInstance), reinterpret_cast<uint32_t>(c->hIcon),
                           reinterpret_cast<uint32_t>(c->hCursor), reinterpret_cast<uint32_t>(c->hbrBackground),
                           reinterpret_cast<uint32_t>(c->lpszMenuName), HashText(c->lpszClassName),
                           reinterpret_cast<uint32_t>(c->hIconSm)});
                   return 0xC001u;
               });
    StandInApi(s, 0x00645240, reinterpret_cast<const void*>(GetProcAddress(user, "AdjustWindowRect")), 12,
               [](Regs& r) {
                   auto* rect = reinterpret_cast<RECT*>(r.args[0]);
                   Record({0xAD1, static_cast<uint32_t>(rect->left), static_cast<uint32_t>(rect->top),
                           static_cast<uint32_t>(rect->right), static_cast<uint32_t>(rect->bottom), r.args[1],
                           r.args[2]});
                   rect->left -= 3;
                   rect->top -= 26;
                   rect->right += 4;
                   rect->bottom += 5;
                   return 1u;
               });
    StandInApi(s, 0x00645244, reinterpret_cast<const void*>(GetProcAddress(user, "GetSystemMetrics")), 4,
               [](Regs& r) {
                   Record({0x6E7, r.args[0]});
                   return static_cast<uint32_t>(g_api.screen[r.args[0] & 1]);
               });
    StandInApi(s, 0x00645248, reinterpret_cast<const void*>(GetProcAddress(user, "CreateWindowExA")), 48,
               [](Regs& r) {
                   Record({0xC3EA7E, r.args[0], HashText(reinterpret_cast<const char*>(r.args[1])),
                           HashText(reinterpret_cast<const char*>(r.args[2]))});
                   for (int i = 3; i < 12; ++i)
                       Record({r.args[i]});
                   return 0x5EEDu;
               });
    // DirectSoundCreate8 (the exe's import thunk 0x005F813C jumps through its slot; ours imports it).
    uint32_t soundSlot = *reinterpret_cast<const uint32_t*>(0x005F813C + 2);
    StandInApi(s, soundSlot, reinterpret_cast<const void*>(&DirectSoundCreate8), 12, [&](Regs& r) {
        Record({0xD5C8, r.args[0], r.args[2]});
        uint32_t result = NextResult();
        *reinterpret_cast<void**>(r.args[1]) = result == 0 ? sound : nullptr;
        return result;
    });
    bool ok = true;
    Lcg random(0);

    // The CD search: no drives, drives with and without CD-ROMs and names, the name found or not, drives past Z.
    const char* kNames[] = {"THUG2", "", "thug2", "THUG2 ", "OTHER"};
    for (int c = 0; c < 80; ++c) {
        ok &= CompareRuns("FindCdDrive", [&](bool original) {
            random = Lcg(0x5740 + c);
            memory.Fill(0x5740 + c);
            g_api.drives = c % 9 == 0 ? 0 : c % 9 == 8 ? 0xFFFFFFFFu : random.Next() | (random.Below(2) << 31);
            g_api.types = random.Next();
            g_api.named = random.Next();
            g_api.name = kNames[c % 5];
            const char* volume = kNames[(c / 5) % 5];
            char letter = original ? Original<char(__cdecl*)(const char*)>(0x005F5740)(volume)
                                   : Platform::FindCdDrive(volume);
            Record({static_cast<uint32_t>(static_cast<uint8_t>(letter))});
            memory.Dump(g_trace);
        });
    }

    // The window: windowed (placed or centred, odd screen sizes) and full screen, with and without an icon.
    for (int c = 0; c < 128; ++c) {
        ok &= CompareRuns("CreateGameWindow", [&](bool original) {
            random = Lcg(0x5860 + c);
            memory.Fill(0x5860 + c);
            // (screens smaller than the window too: a negative, odd centring)
            g_api.screen[0] = (c & 16 ? 320 : 640) + static_cast<int>(random.Below(3000));
            g_api.screen[1] = (c & 16 ? 200 : 480) + static_cast<int>(random.Below(2000));
            int x = c & 1 ? -1 : static_cast<int>(random.Below(500)) - 100;
            int y = c & 2 ? -1 : static_cast<int>(random.Below(500)) - 100;
            int width = 320 + static_cast<int>(random.Below(1600)), height = 200 + static_cast<int>(random.Below(1200));
            int icon = c & 4 ? 0 : 0x10066;
            bool full = (c & 8) != 0;
            auto proc = reinterpret_cast<WNDPROC>(0x00ABCDEF);
            auto instance = reinterpret_cast<HINSTANCE>(0x00400000);
            using Fn = HWND(__cdecl*)(const char*, const char*, int, int, int, int, WNDPROC, HINSTANCE, int, bool);
            HWND window = original ? Original<Fn>(0x005F5860)("Class", "Title", x, y, width, height, proc, instance,
                                                              icon, full)
                                   : Platform::CreateGameWindow("Class", "Title", x, y, width, height, proc, instance,
                                                                icon, full);
            Record({reinterpret_cast<uint32_t>(window)});
            memory.Dump(g_trace);
        });
    }

    // DirectSound: made or not, released.
    for (int c = 0; c < 4; ++c) {
        ok &= CompareRuns("DirectSound", [&](bool original) {
            memory.Fill(0x59D0 + c);
            g_results = {c & 1 ? 0x88780078u : 0u};
            GameVariable<void*>(0x007D6A38) = nullptr;
            auto window = reinterpret_cast<HWND>(0x1234);
            if (original)
                Original<void(__cdecl*)(HWND)>(0x005F59D0)(window);
            else
                Platform::InitDirectSound(window);
            memory.Dump(g_trace);
            Record({Relocate(GameVariable<uint32_t>(0x007D6A38))});
            if (c & 2)
                GameVariable<void*>(0x007D6A38) = nullptr;
            if (original)
                Original<void(__cdecl*)()>(0x005F5A00)();
            else
                Platform::ReleaseDirectSound();
            Record({Relocate(GameVariable<uint32_t>(0x007D6A38))});
        });
    }

    // The ADPCM decoder: random bytes, odd and even counts, loud runs that clip.
    for (int c = 0; c < 20; ++c) {
        ok &= CompareRuns("DecodeAdpcm", [&](bool original) {
            random = Lcg(0x5A20 + c);
            uint8_t in[600];
            for (uint8_t& b : in)
                b = static_cast<uint8_t>(c % 4 == 3 ? (random.Below(2) ? 0x77 : 0xFF) : random.Next());
            int16_t out[1200];
            std::memset(out, 0xEE, sizeof(out));
            int count = c == 0 ? 0 : c == 1 ? -3 : c == 2 ? 1 : static_cast<int>(random.Below(1200));
            if (original)
                Original<void(__cdecl*)(int16_t*, const uint8_t*, int)>(0x005F5A20)(out, in, count);
            else
                Platform::DecodeAdpcm(out, in, count);
            for (int16_t sample : out)
                Record({static_cast<uint32_t>(static_cast<uint16_t>(sample))});
        });
    }

    // The unswizzle: square and long textures, 1-8 bytes a texel.
    for (int c = 0; c < 40; ++c) {
        ok &= CompareRuns("Unswizzle", [&](bool original) {
            random = Lcg(0x5AF0 + c);
            int width = 1 << random.Below(7), height = 1 << random.Below(7);
            uint32_t bytes = 1 + random.Below(8);
            static uint8_t in[64 * 64 * 8], out[64 * 64 * 8 + 16];
            for (uint8_t& b : in)
                b = static_cast<uint8_t>(random.Next());
            std::memset(out, 0xEE, sizeof(out));
            void* result = original ? Original<void*(__cdecl*)(void*, const void*, int, int, uint32_t)>(0x005F5AF0)(
                                          out, in, width, height, bytes)
                                    : Platform::Unswizzle(out, in, width, height, bytes);
            Record({result == out});
            for (size_t i = 0; i < sizeof(out); i += 4)
                Record({*reinterpret_cast<uint32_t*>(out + i)});
        });
    }
    g_relocate = nullptr;
    delete &s;
    Check(ok, "platform: the system side - CD search, game window, DirectSound, ADPCM, unswizzle");
}

} // namespace

SELFTEST(Early, TestPlatformD3D);
SELFTEST(Early, TestPlatformSystem);
