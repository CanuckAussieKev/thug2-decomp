// Self-tests for functions PartyMod changes (skater controls, online stats, the screen flash, movies, the music
// shuffle): original, with PartyMod's patch bytes written in for the runs that need them, vs ours.
// (PartyMod's screen flash fix is copied below as the original run's reference: contains code from PartyMod
// (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md)

#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/gfx/flash.h"
#include "game/core/dbg.h"
#include "game/gfx/movie.h"
#include "game/pcm/music.h"
#include "game/sio/device.h"
#include "game/sys/mlp.h"
#include "game/sys/platform.h"
#include "game/net/online_hosts.h"
#include "game/net/online_identity.h"
#include "game/net/stats.h"
#include "game/sio/sdl_input.h"
#include "game/skater/controls.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"


namespace {

// Bytes written over game code for the length of a test (restored when it goes).
struct CodePatch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    CodePatch(uint32_t address, const std::vector<uint8_t>& bytes)
        : at(reinterpret_cast<uint8_t*>(address)), saved(bytes.size())
    {
        DWORD old;
        VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved.data(), at, bytes.size());
        std::memcpy(at, bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
    }
    ~CodePatch()
    {
        std::memcpy(at, saved.data(), saved.size());
        FlushInstructionCache(GetCurrentProcess(), at, saved.size());
    }
};
std::vector<uint8_t> JumpTo(uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes(5);
    bytes[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}
// PartyMod's PS2-controls bytes in the original, while `on`.
struct Ps2ControlBytes {
    std::vector<CodePatch*> patches;
    explicit Ps2ControlBytes(bool on)
    {
        if (!on)
            return;
        patches.push_back(new CodePatch(0x0051F4C8, {0x05}));
        patches.push_back(new CodePatch(0x00527546, std::vector<uint8_t>(6, 0x90)));
        patches.push_back(new CodePatch(0x00527636, std::vector<uint8_t>(6, 0x90)));
    }
    ~Ps2ControlBytes()
    {
        for (CodePatch* p : patches)
            delete p;
    }
};

// The acid drops' callees, recorded.
std::vector<uint32_t> g_skater_log;
bool g_skater_found;
uint32_t Rel(const void* p, const void* base) { return static_cast<uint32_t>(static_cast<const uint8_t*>(p) - static_cast<const uint8_t*>(base)); }
uint8_t* g_skater;
bool __fastcall StandInFindAcidDrop(void* physics, void*, int zero, void* a, void* b, float* velocity, void* c)
{
    g_skater_log.insert(g_skater_log.end(), {0xF1Du, Rel(physics, g_skater), static_cast<uint32_t>(zero), Rel(a, g_skater),
                                             Rel(b, g_skater), Rel(c, g_skater)});
    for (int i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &velocity[i], 4);
        g_skater_log.push_back(bits);
        velocity[i] = velocity[i] * 2.0f + static_cast<float>(i); // (the drop's velocity)
    }
    return g_skater_found;
}
void __fastcall StandInTrick(void* object, void*, void* name)
{
    g_skater_log.insert(g_skater_log.end(), {0x7C1Cu, Rel(object, g_skater), Rel(name, g_skater)});
}
float __cdecl StandInPhysicsFloat(uint32_t name)
{
    g_skater_log.insert(g_skater_log.end(), {0xF10Au, name});
    return 3.25f;
}
void __fastcall StandInSound(void* object, void*, const void* sound, void* name)
{
    g_skater_log.insert(g_skater_log.end(), {0x50Du, Rel(object, g_skater), reinterpret_cast<uint32_t>(sound), Rel(name, g_skater)});
}
void __fastcall StandInSetState(void* object, void*, int value)
{
    g_skater_log.insert(g_skater_log.end(), {0x57Au, Rel(object, g_skater), static_cast<uint32_t>(value)});
}

// The platform structure lookup over stand-in parameter structures, on platforms 5, 7 and another, PartyMod's
// PS2 controls off and on.
void TestSkaterPlatformStructure()
{
    int savedPlatform = GameVariable<int>(0x007CEFA8);
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    // Components: {type << 1, ?, ?, name, value, next} - structures (type 10).
    alignas(16) static uint8_t components[3][16];
    alignas(16) static uint8_t structures[3][8];
    const uint32_t names[3] = {0xCE42EE1A, 0xCB0E600C, 0xE594F0A2};
    bool same = true;
    for (int present = 0; present < 8; ++present) {
        alignas(8) uint8_t params[8] = {};
        uint8_t* last = nullptr;
        std::memset(components, 0, sizeof(components));
        for (int i = 0; i < 3; ++i) {
            if (!(present & (1 << i)))
                continue;
            components[i][0] = 10 << 1; // (the type: byte 0, shifted - the low bit is a flag)
            std::memcpy(&components[i][4], &names[i], 4);
            uint32_t value = reinterpret_cast<uint32_t>(structures[i]);
            std::memcpy(&components[i][8], &value, 4);
            uint8_t** link = last ? reinterpret_cast<uint8_t**>(last + 12) : reinterpret_cast<uint8_t**>(params + 4);
            *link = components[i];
            last = components[i];
        }
        for (int platform : {5, 7, 1}) {
            for (int ps2 = 0; ps2 < 2; ++ps2) {
                GameVariable<int>(0x007CEFA8) = platform;
                void* original;
                {
                    Ps2ControlBytes bytes(ps2 != 0);
                    original = Original<void*(__thiscall*)(void*, void*)>(0x0051F4C0)(nullptr, params);
                }
                Sio::TheInputSettings().ps2Controls = static_cast<uint8_t>(ps2);
                void* ours = reinterpret_cast<Skater::Component*>(0x1234)->PlatformStructure(
                    reinterpret_cast<Script::CStruct*>(params));
                if (original != ours) {
                    Log("  platform structure: present %d platform %d ps2 %d: original %p ours %p", present, platform, ps2,
                        original, ours);
                    same = false;
                }
            }
        }
    }
    GameVariable<int>(0x007CEFA8) = savedPlatform;
    Sio::TheInputSettings().ps2Controls = savedSettings;
    Check(same, "skater: platform structure (PS2 controls off and on)");
}

// The two acid drops over the buttons, the flags that stop them, the physics finding a drop or not, walking or
// not, PartyMod's PS2 controls off and on: the skater's fields and every callee's arguments.
void TestSkaterAcidDrops()
{
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    CodePatch find(0x00502240, JumpTo(0x00502240, reinterpret_cast<void*>(&StandInFindAcidDrop)));
    CodePatch trick(0x00427CD0, JumpTo(0x00427CD0, reinterpret_cast<void*>(&StandInTrick)));
    CodePatch physicsFloat(0x0043DA70, JumpTo(0x0043DA70, reinterpret_cast<void*>(&StandInPhysicsFloat)));
    CodePatch sound(0x00434BF0, JumpTo(0x00434BF0, reinterpret_cast<void*>(&StandInSound)));
    CodePatch setState(0x005260E0, JumpTo(0x005260E0, reinterpret_cast<void*>(&StandInSetState)));
    alignas(16) static uint8_t skater[0x1900];
    alignas(16) static uint8_t input[0x140];
    alignas(16) static uint8_t flags[0x40];
    g_skater = skater;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];
        int variant = 0;
        for (int which = 0; which < 2; ++which) {
            for (int state = 0; state < 128; ++state) {
                for (int ps2 = 0; ps2 < 2; ++ps2) {
                    ++variant;
                    for (int i = 0; i < 0x1900; ++i)
                        skater[i] = static_cast<uint8_t>(i * 13 + variant);
                    std::memset(input, 0, sizeof(input));
                    std::memset(flags, 0, sizeof(flags));
                    float v[5] = {1.5f, -2.0f, 3.0f, 0.5f, 7.0f};
                    std::memcpy(skater + 0x34, v, sizeof(v));
                    skater[0x9B] = (state & 1) ? 1 : 0;
                    flags[0x24] = (state & 2) ? 1 : 0;
                    input[0xC0] = (state & 4) ? 1 : 0;
                    input[0x120] = (state & 8) ? 1 : 0;
                    g_skater_found = (state & 16) != 0;
                    skater[0x1864] = (state & 32) ? 1 : 0;
                    flags[0x26] = (state & 64) ? 1 : 0;
                    *reinterpret_cast<void**>(skater + 0x1770) = input;
                    *reinterpret_cast<void**>(skater + 0x1780) = flags;
                    *reinterpret_cast<void**>(skater + 0x177C) = skater + 0x10;
                    *reinterpret_cast<void**>(skater + 0x1784) = skater + 0x20;
                    *reinterpret_cast<void**>(skater + 0x178C) = skater + 0x30;
                    g_skater_log.clear();
                    bool result;
                    if (original) {
                        Ps2ControlBytes bytes(ps2 != 0);
                        result = Original<bool(__thiscall*)(void*)>(which ? 0x00527600 : 0x00527510)(skater);
                    } else {
                        Sio::TheInputSettings().ps2Controls = static_cast<uint8_t>(ps2);
                        auto* component = reinterpret_cast<Skater::Component*>(skater);
                        result = which ? component->TryAcidDrop() : component->TryWalkAcidDrop();
                    }
                    trace.push_back(result);
                    trace.insert(trace.end(), reinterpret_cast<uint32_t*>(skater + 0x30), reinterpret_cast<uint32_t*>(skater + 0x80));
                    trace.push_back(*reinterpret_cast<uint32_t*>(skater + 0x748));
                    trace.insert(trace.end(), g_skater_log.begin(), g_skater_log.end());
                }
            }
        }
    }
    Sio::TheInputSettings().ps2Controls = savedSettings;
    Check(traces[0] == traces[1], "skater: acid drops (PS2 controls off and on)");
    if (traces[0].size() != traces[1].size())
        Log("  acid drop trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  acid drop entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}


// --- online top stats (not skater code; shares this file's helpers) ---------------------------------------

std::vector<std::string> g_stats_log;
void __fastcall StandInSendStatsRequest(void* manager, void*, const uint8_t* request)
{
    char line[400];
    uint32_t words[5];
    std::memcpy(words, request + 0x104, sizeof(words));
    std::snprintf(line, sizeof(line), "%p %08X %s %08X %08X %08X %08X %08X", manager, *reinterpret_cast<const uint32_t*>(request),
                  reinterpret_cast<const char*>(request + 4), words[0], words[1], words[2], words[3], words[4]);
    g_stats_log.push_back(line);
}
int __cdecl StandInSprintf(char* out, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(out, 400, format, args); // (the URLs fit)
    va_end(args);
    return n;
}

// RetrieveTopStats: an index given or not, every file (ratings: another reply), PartyMod's host or the
// original's - the original run with PartyMod's DWord written in (and its C runtime sprintf stood in).
void TestRetrieveTopStats()
{
    CodePatch send(0x0054A910, JumpTo(0x0054A910, reinterpret_cast<void*>(&StandInSendStatsRequest)));
    CodePatch print(0x006234FE, JumpTo(0x006234FE, reinterpret_cast<void*>(&StandInSprintf)));
    void* savedManager = GameVariable<void*>(0x007CCE14);
    const char* savedHost = Net::GamestatsHost();
    static char host[] = "gamestats.example.org";
    alignas(16) static uint8_t manager[0x20];
    alignas(16) static uint8_t stats[0x200];
    *reinterpret_cast<void**>(manager + 0x14) = manager + 4;
    *reinterpret_cast<void**>(manager + 0x1C) = stats;
    GameVariable<void*>(0x007CCE14) = manager;
    std::vector<std::string> logs[2];
    std::vector<uint32_t> fields[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_stats_log.clear();
        for (int partyModHost = 0; partyModHost < 2; ++partyModHost) {
            for (int given : {-1, 0, 2, 3, 4}) {
                std::memset(stats, 0x77, sizeof(stats));
                *reinterpret_cast<int*>(stats + 0x90) = given == 3 ? 3 : 1; // (no index: the next one)
                // the parameters: {type << 1, ?, ?, name, value, next}
                alignas(16) uint8_t component[16] = {};
                alignas(8) uint8_t params[8] = {};
                if (given >= 0 && given != 3) {
                    component[0] = static_cast<uint8_t>(Script::ESYMBOLTYPE_INTEGER << 1);
                    uint32_t name = 0x7360C9EF;
                    std::memcpy(component + 4, &name, 4);
                    std::memcpy(component + 8, &given, 4);
                    *reinterpret_cast<uint8_t**>(params + 4) = component;
                }
                bool result;
                if (original) {
                    char* hostAddress = host;
                    uint32_t value = partyModHost ? reinterpret_cast<uint32_t>(hostAddress) : 0x0064D670;
                    std::vector<uint8_t> bytes(4);
                    std::memcpy(bytes.data(), &value, 4);
                    CodePatch dword(0x0054DD14, bytes);
                    result = Original<bool(__cdecl*)(void*, void*)>(0x0054DCB0)(params, nullptr);
                } else {
                    Net::GamestatsHost() = partyModHost ? host : nullptr;
                    result = Net::RetrieveTopStats(reinterpret_cast<Script::CStruct*>(params), nullptr);
                }
                fields[run].push_back(result);
                fields[run].push_back(*reinterpret_cast<uint32_t*>(stats + 0x90));
                fields[run].push_back(stats[0x1FC]);
            }
        }
        logs[run] = g_stats_log;
    }
    GameVariable<void*>(0x007CCE14) = savedManager;
    Net::GamestatsHost() = savedHost;
    for (size_t i = 0; i < logs[0].size() && i < logs[1].size(); ++i)
        if (logs[0][i] != logs[1][i])
            Log("  top stats %u: original %s / ours %s", static_cast<unsigned>(i), logs[0][i].c_str(), logs[1][i].c_str());
    Check(logs[0] == logs[1] && fields[0] == fields[1] && logs[1].size() == 10, "net: RetrieveTopStats (PartyMod's host and not)");
}


// --- the online identity (game/net/online_identity.cpp) -----------------------------------------------------

// Where the game name and key are, in the exe as it is (the Early stage: nothing written yet) - each write site
// checked against the instruction it's in by its own bytes - then a name and a key written and read back (the raw
// bytes too, so the NOT-ed sites are proved NOT-ed), a bad key refused, and everything put back.
void TestOnlineIdentity()
{
    // the sites' instructions: push imm32 (0x68) of the name's address, or the stored key bytes
    const uint32_t nameSites[Net::kGameNameSites] = {0x00542F8B, 0x00542FA0, 0x0054CAD9};
    bool layout = true;
    for (int i = 0; i < Net::kGameNameSites; ++i) {
        const auto* at = reinterpret_cast<const uint8_t*>(nameSites[i]);
        uint32_t operand;
        std::memcpy(&operand, at + 1, 4);
        layout = layout && at[0] == 0x68 && operand == 0x0064B1CC &&
                 Net::GameNameAt(i) == reinterpret_cast<const char*>(0x0064B1CC);
    }
    layout = layout && std::strcmp(reinterpret_cast<const char*>(0x0064B1CC), "thps6pc") == 0;
    const uint8_t inverted[6] = {0xBE, 0x9B, 0xB3, 0xA8, 0x9E, 0xA5}; // ~"AdLWaZ"
    layout = layout && std::memcmp(reinterpret_cast<const void*>(0x0067F120), inverted, 6) == 0 &&
             std::memcmp(reinterpret_cast<const void*>(0x0067F154), inverted, 6) == 0 &&
             std::strcmp(reinterpret_cast<const char*>(0x0064D58C), "AdLWaZ") == 0;
    for (int i = 0; i < Net::kGameKeySites; ++i) {
        char key[7];
        Net::GameKeyAt(i, key);
        layout = layout && std::strcmp(key, "AdLWaZ") == 0;
    }
    Check(layout, "net: the online identity's sites (thps6pc, AdLWaZ)");

    // save everything a write may touch (and a byte either side of each key)
    std::vector<std::vector<uint8_t>> saved;
    const uint32_t keySites[Net::kGameKeySites] = {0x0067F120, 0x0067F154, 0x0064D58C};
    for (uint32_t site : nameSites)
        saved.emplace_back(reinterpret_cast<const uint8_t*>(site), reinterpret_cast<const uint8_t*>(site) + 5);
    for (uint32_t site : keySites)
        saved.emplace_back(reinterpret_cast<const uint8_t*>(site - 1), reinterpret_cast<const uint8_t*>(site) + 8);
    auto unchanged = [&]() {
        size_t k = 0;
        bool same = true;
        for (uint32_t site : nameSites)
            same = same && std::memcmp(reinterpret_cast<const void*>(site), saved[k++].data(), 5) == 0;
        for (uint32_t site : keySites)
            same = same && std::memcmp(reinterpret_cast<const void*>(site - 1), saved[k++].data(), 8) == 0;
        return same;
    };

    static const char name[] = "testname12";
    bool refused = !Net::WriteOnlineIdentity(name, "short") && !Net::WriteOnlineIdentity(nullptr, "toolong") && unchanged();
    bool none = Net::WriteOnlineIdentity(nullptr, nullptr) && unchanged();

    bool written = Net::WriteOnlineIdentity(name, "Xy12#z");
    for (int i = 0; i < Net::kGameNameSites; ++i) {
        const auto* at = reinterpret_cast<const uint8_t*>(nameSites[i]);
        uint32_t operand;
        std::memcpy(&operand, at + 1, 4);
        written = written && at[0] == 0x68 && operand == reinterpret_cast<uint32_t>(name) && Net::GameNameAt(i) == name;
    }
    const uint8_t newInverted[6] = {0xA7, 0x86, 0xCE, 0xCD, 0xDC, 0x85}; // ~"Xy12#z"
    written = written && std::memcmp(reinterpret_cast<const void*>(0x0067F120), newInverted, 6) == 0 &&
              std::memcmp(reinterpret_cast<const void*>(0x0067F154), newInverted, 6) == 0 &&
              std::strcmp(reinterpret_cast<const char*>(0x0064D58C), "Xy12#z") == 0;
    for (int i = 0; i < Net::kGameKeySites; ++i) {
        char key[7];
        Net::GameKeyAt(i, key);
        written = written && std::strcmp(key, "Xy12#z") == 0;
        // nothing either side of the key's 6 bytes
        const auto* at = reinterpret_cast<const uint8_t*>(keySites[i]);
        written = written && at[-1] == saved[Net::kGameNameSites + i][0] && at[6] == saved[Net::kGameNameSites + i][7];
    }
    // only the key: the name stays
    static const char other[] = "othername";
    Net::WriteOnlineIdentity(other, nullptr);
    bool keyKept = Net::GameNameAt(1) == other && std::strcmp(reinterpret_cast<const char*>(0x0064D58C), "Xy12#z") == 0;
    Net::WriteOnlineIdentity(nullptr, "AdLWaZ");
    keyKept = keyKept && Net::GameNameAt(0) == other && std::memcmp(reinterpret_cast<const void*>(0x0067F120), inverted, 6) == 0;

    size_t k = 0;
    for (uint32_t site : nameSites)
        WriteGameCode(site, saved[k++].data(), 5);
    for (uint32_t site : keySites)
        WriteGameCode(site - 1, saved[k++].data(), 8);
    Check(refused && none && written && keyKept && unchanged(), "net: the online identity written (name, key, a bad key)");
}


// --- the screen flash ---------------------------------------------------------------------------------------

// PartyMod's reorder_flash_vertices, called at 0x004B91E7 in place of the device's DrawPrimitiveUP (the flash's
// vertices come in the wrong order): the first three rotated, then drawn through the device's vtable (slot 83).
// Nx::DrawFlash has this built in; here it's the reference written into the original.
struct FlashVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
void __fastcall PartyModReorderFlashVertices(uint32_t* device, uint32_t* deviceAgain, uint32_t* alsoDevice, uint32_t prim,
                                             uint32_t count, FlashVertex* vertices, uint32_t stride)
{
    auto draw = reinterpret_cast<void(__fastcall*)(void*, void*, void*, uint32_t, uint32_t, FlashVertex*, uint32_t)>(
        deviceAgain[83]);
    FlashVertex first = vertices[0];
    vertices[0] = vertices[1];
    vertices[1] = vertices[2];
    vertices[2] = first;
    draw(device, deviceAgain, alsoDevice, prim, count, vertices, stride);
}

std::vector<uint32_t> g_flash_log;
bool g_flash_textured;
long __stdcall StandInDrawPrimitiveUP(void* device, int type, unsigned int count, const uint8_t* vertices, unsigned int stride)
{
    g_flash_log.insert(g_flash_log.end(), {0xD4A3u, reinterpret_cast<uint32_t>(device) & 0xFFF, static_cast<uint32_t>(type), count, stride});
    for (int i = 0; i < 4; ++i) {
        const uint32_t* v = reinterpret_cast<const uint32_t*>(vertices + 0x1C * i);
        g_flash_log.insert(g_flash_log.end(), v, v + (g_flash_textured ? 7 : 5)); // (u/v unset untextured)
    }
    return 0;
}
const float* __cdecl StandInViewport(int index)
{
    // (the last: a product and sum that round differently once or twice - found by search)
    static float views[4][5] = {{0, 0.0f, 0.0f, 1.0f, 1.0f}, {0, 0.125f, 0.3f, 0.5f, 0.3333333f}, {0, 0.1f, 0.2f, 0.7f, 0.37f},
                                {0, 0.125f, 0.125f, 0.0065f, 0.017f}};
    g_flash_log.insert(g_flash_log.end(), {0x7E3u, static_cast<uint32_t>(index)});
    return views[index % 4];
}
void __cdecl StandInState3(int a, int b, int c) { g_flash_log.insert(g_flash_log.end(), {0x7A3u, static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)}); }
void __cdecl StandInState2(int a, int b) { g_flash_log.insert(g_flash_log.end(), {0x5A2u, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); }
void __cdecl StandInBlend(int a) { g_flash_log.insert(g_flash_log.end(), {0xB1Du, static_cast<uint32_t>(a)}); }
void __cdecl StandInState2b(int a, int b) { g_flash_log.insert(g_flash_log.end(), {0xAA8u, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); }
void __cdecl StandInFormat(int a) { g_flash_log.insert(g_flash_log.end(), {0xF7Fu, static_cast<uint32_t>(a)}); }

// The flash: two viewports x two screen sizes x colours x untextured / a texture (direct or through its +0x10)
// - the original with PartyMod's call to reorder_flash_vertices written in - the draw recorded through a
// stand-in D3D device.
void TestDrawFlash()
{
    CodePatch viewport(0x004A06D0, JumpTo(0x004A06D0, reinterpret_cast<void*>(&StandInViewport)));
    CodePatch texture(0x004DA730, JumpTo(0x004DA730, reinterpret_cast<void*>(&StandInState3)));
    CodePatch sampler(0x004DAB60, JumpTo(0x004DAB60, reinterpret_cast<void*>(&StandInState2)));
    CodePatch blend(0x004DA760, JumpTo(0x004DA760, reinterpret_cast<void*>(&StandInBlend)));
    CodePatch other(0x004DAA80, JumpTo(0x004DAA80, reinterpret_cast<void*>(&StandInState2b)));
    CodePatch format(0x004D9DB0, JumpTo(0x004D9DB0, reinterpret_cast<void*>(&StandInFormat)));
    struct Saved {
        uint32_t address;
        uint32_t value;
    };
    std::vector<Saved> saved;
    for (uint32_t a : {0x00786D6Cu, 0x00786D70u, 0x00786EBCu, 0x00786EC0u, 0x00786D0Cu})
        saved.push_back({a, GameVariable<uint32_t>(a)});
    static void* vtable[100];
    for (void*& slot : vtable)
        slot = nullptr;
    vtable[83] = reinterpret_cast<void*>(&StandInDrawPrimitiveUP);
    static void* device[2] = {vtable, nullptr};
    GameVariable<void*>(0x00786D0C) = device;
    // the texture: flags +8, +0x10 -> (an image, or a pointer to one) whose +0x10 is the D3D texture
    alignas(16) static uint8_t image[0x20], holder[0x20], tex[2][0x20];
    *reinterpret_cast<uint32_t*>(image + 0x10) = 0xD3D7E4;
    *reinterpret_cast<void**>(holder) = image;
    tex[0][8] = 0;
    *reinterpret_cast<void**>(tex[0] + 0x10) = image;
    tex[1][8] = 1;
    *reinterpret_cast<void**>(tex[1] + 0x10) = holder;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_flash_log.clear();
        GameVariable<int>(0x00786EBC) = 10;
        GameVariable<int>(0x00786EC0) = 20;
        for (int view = 0; view < 4; ++view) {
            for (int screen = 0; screen < 4; ++screen) {
                static const int widths[4] = {641, 1280, 1023, 1919};
                static const int heights[4] = {479, 720, 767, 1079};
                GameVariable<int>(0x00786D6C) = widths[screen];
                GameVariable<int>(0x00786D70) = heights[screen];
                for (int textured = 0; textured < 3; ++textured) {
                    alignas(16) uint8_t flash[0x24] = {};
                    *reinterpret_cast<int*>(flash) = view;
                    flash[0xC] = 0x12;
                    flash[0xD] = static_cast<uint8_t>(0x34 + view);
                    flash[0xE] = 0x56;
                    flash[0xF] = static_cast<uint8_t>(0x78 + screen);
                    *reinterpret_cast<float*>(flash + 0x1C) = 0.25f + view;
                    *reinterpret_cast<void**>(flash + 0x20) = textured ? tex[textured - 1] : nullptr;
                    g_flash_textured = textured != 0;
                    if (original) {
                        std::vector<uint8_t> call = JumpTo(0x004B91E7, reinterpret_cast<void*>(&PartyModReorderFlashVertices));
                        call[0] = 0xE8;
                        call.push_back(0x90);
                        CodePatch partyMod(0x004B91E7, call);
                        Original<void(__cdecl*)(void*)>(0x004B9030)(flash);
                    } else {
                        Nx::DrawFlash(reinterpret_cast<Nx::Flash*>(flash));
                    }
                }
            }
        }
        traces[run] = g_flash_log;
        traces[run].push_back(GameVariable<uint32_t>(0x00786EBC));
        traces[run].push_back(GameVariable<uint32_t>(0x00786EC0));
    }
    for (Saved& s : saved)
        GameVariable<uint32_t>(s.address) = s.value;
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  flash entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    Check(traces[0] == traces[1] && traces[1].size() > 100, "gfx: screen flash (with PartyMod's vertex order)");
}


// --- the movie frame ----------------------------------------------------------------------------------------

std::vector<uint32_t> g_movie_log;
long g_movie_lost;
long __stdcall MovieTestCooperativeLevel(void*) { g_movie_log.push_back(0x7C1u); return g_movie_lost; }
long __stdcall MovieSetRenderState(void*, int a, int b) { g_movie_log.insert(g_movie_log.end(), {0x5E5u, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); return 0; }
long __stdcall MovieSetFvf(void*, uint32_t f) { g_movie_log.insert(g_movie_log.end(), {0xF7Fu, f}); return 0; }
long __stdcall MovieStageState(void*, int a, int b, int c) { g_movie_log.insert(g_movie_log.end(), {0x557u, static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)}); return 0; }
long __stdcall MovieDraw(void*, int type, unsigned int count, const uint32_t* v, unsigned int stride)
{
    g_movie_log.insert(g_movie_log.end(), {0xD4Au, static_cast<uint32_t>(type), count, stride});
    g_movie_log.insert(g_movie_log.end(), v, v + 28);
    return 0;
}
long __stdcall MovieGetLevelDesc(void*, unsigned int level, uint32_t* desc)
{
    g_movie_log.insert(g_movie_log.end(), {0x1DEu, level});
    desc[6] = 512; // Width
    desc[7] = 256; // Height
    return 0;
}
void __cdecl MovieState1(int a) { g_movie_log.insert(g_movie_log.end(), {0x51u, static_cast<uint32_t>(a)}); }
void __cdecl MovieState2(int a, int b) { g_movie_log.insert(g_movie_log.end(), {0x52u, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); }
void __cdecl MovieState2b(int a, int b) { g_movie_log.insert(g_movie_log.end(), {0x62u, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); }
void __cdecl MovieFormat(int a) { g_movie_log.insert(g_movie_log.end(), {0x7Fu, static_cast<uint32_t>(a)}); }
void __cdecl MovieSetTexture(int a, void* t, int c) { g_movie_log.insert(g_movie_log.end(), {0x7E7u, static_cast<uint32_t>(a), reinterpret_cast<uint32_t>(t) & 0xFFF, static_cast<uint32_t>(c)}); }

// The movie frame: no texture, positions/sizes/fades, back buffers wider, narrower and 4:3 (PartyMod's fit),
// the device lost or not - the original with PartyMod's two calls written in.
void TestDrawMovieFrame()
{
    CodePatch blend(0x004DA760, JumpTo(0x004DA760, reinterpret_cast<void*>(&MovieState1)));
    CodePatch sampler(0x004DAB60, JumpTo(0x004DAB60, reinterpret_cast<void*>(&MovieState2)));
    CodePatch format(0x004D9DB0, JumpTo(0x004D9DB0, reinterpret_cast<void*>(&MovieFormat)));
    CodePatch other(0x004DAA80, JumpTo(0x004DAA80, reinterpret_cast<void*>(&MovieState2b)));
    CodePatch texture(0x004DA730, JumpTo(0x004DA730, reinterpret_cast<void*>(&MovieSetTexture)));
    CodePatch print(0x006234FE, JumpTo(0x006234FE, reinterpret_cast<void*>(&StandInSprintf)));
    std::vector<std::pair<uint32_t, uint32_t>> saved;
    for (uint32_t a : {0x007D69E0u, 0x00786D6Cu, 0x00786D70u, 0x00696D9Cu})
        saved.push_back({a, GameVariable<uint32_t>(a)});
    static void* vtable[100] = {};
    vtable[3] = reinterpret_cast<void*>(&MovieTestCooperativeLevel);
    vtable[0xE4 / 4] = reinterpret_cast<void*>(&MovieSetRenderState);
    vtable[0x164 / 4] = reinterpret_cast<void*>(&MovieSetFvf);
    vtable[0x10C / 4] = reinterpret_cast<void*>(&MovieStageState);
    vtable[0x14C / 4] = reinterpret_cast<void*>(&MovieDraw);
    static void* device[2] = {vtable, nullptr};
    static void* textureVtable[20] = {};
    textureVtable[17] = reinterpret_cast<void*>(&MovieGetLevelDesc);
    static void* movieTexture[2] = {textureVtable, nullptr};
    GameVariable<void*>(0x007D69E0) = device;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_movie_log.clear();
        GameVariable<int>(0x00696D9C) = 7;
        static const int sizes[3][2] = {{1920, 1080}, {640, 480}, {1024, 1280}};
        for (const auto& size : sizes) {
            GameVariable<int>(0x00786D6C) = size[0];
            GameVariable<int>(0x00786D70) = size[1];
            for (int lost = 0; lost < 2; ++lost) {
                g_movie_lost = lost ? static_cast<long>(0x88760868) : 0;
                for (int shape = 0; shape < 3; ++shape) {
                    void* tex = shape == 2 && lost ? nullptr : movieTexture;
                    float x = shape * 0.1f, y = 0.05f * shape, w = 1.0f - x, h = 0.7f + 0.01f * shape;
                    float alpha = 0.333f * (shape + 1);
                    if (original) {
                        std::vector<uint8_t> call1 = JumpTo(0x0044FB8C, reinterpret_cast<void*>(&Nx::SetMovieTexture));
                        call1[0] = 0xE8;
                        std::vector<uint8_t> call2 = JumpTo(0x0044FC88, reinterpret_cast<void*>(&Nx::DrawMovieQuad));
                        call2[0] = 0xE8;
                        call2.push_back(0x90);
                        CodePatch a(0x0044FB8C, call1);
                        CodePatch b(0x0044FC88, call2);
                        Original<void(__cdecl*)(void*, float, float, float, float, float)>(0x0044FA80)(tex, x, y, w, h, alpha);
                    } else {
                        Nx::DrawMovieFrame(tex, x, y, w, h, alpha);
                    }
                }
            }
        }
        traces[run] = g_movie_log;
        traces[run].push_back(GameVariable<uint32_t>(0x00696D9C));
    }
    for (auto& s : saved)
        GameVariable<uint32_t>(s.first) = s.second;
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  movie entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    Check(traces[0] == traces[1] && traces[1].size() > 300, "gfx: movie frame (with PartyMod's 4:3 fit)");
}


// --- PlayMovie ----------------------------------------------------------------------------------------------

std::vector<std::string> g_play_log;
void __cdecl StandInQueueMovie(const char* name) { g_play_log.push_back(name); }

// PlayMovie with the flag off: a name, a path, no name - original vs ours, the queue recorded. Then ours with the
// flag on: the intro movie is skipped (returning true), the other still queued.
void TestPlayMovie()
{
    bool savedSkip = Nx::SkipIntroMovies();
    Nx::SkipIntroMovies() = false;
    CodePatch queue(0x0044F830, JumpTo(0x0044F830, reinterpret_cast<void*>(&StandInQueueMovie)));
    auto* playFile = reinterpret_cast<void*>(&Nx::PlayMovieFile); // (ours calls it directly)
    CodePatch queueOurs(reinterpret_cast<uint32_t>(playFile),
                        JumpTo(reinterpret_cast<uint32_t>(playFile), reinterpret_cast<void*>(&StandInQueueMovie)));
    std::vector<std::string> logs[2];
    std::vector<int> results[2];
    for (int run = 0; run < 2; ++run) {
        g_play_log.clear();
        for (const char* name : {"movies/bik/NSlogo", "credits", static_cast<const char*>(nullptr)}) {
            alignas(16) uint8_t component[16] = {};
            alignas(8) uint8_t params[8] = {};
            if (name) {
                component[0] = static_cast<uint8_t>(Script::ESYMBOLTYPE_STRING << 1); // unnamed (name 0)
                std::memcpy(component + 8, &name, 4);
                *reinterpret_cast<uint8_t**>(params + 4) = component;
            }
            bool result = run == 0 ? Original<bool(__cdecl*)(void*, void*)>(0x0059F0C0)(params, nullptr)
                                   : Nx::PlayMovie(reinterpret_cast<Script::CStruct*>(params), nullptr);
            results[run].push_back(result);
        }
        logs[run] = g_play_log;
    }
    Check(logs[0] == logs[1] && results[0] == results[1] && logs[1].size() == 2, "gfx: PlayMovie (flag off)");

    Nx::SkipIntroMovies() = true;
    g_play_log.clear();
    bool skippedAll = true;
    for (const char* name : {"movies/bik/NSlogo", "credits"}) {
        alignas(16) uint8_t component[16] = {};
        alignas(8) uint8_t params[8] = {};
        component[0] = static_cast<uint8_t>(Script::ESYMBOLTYPE_STRING << 1);
        std::memcpy(component + 8, &name, 4);
        *reinterpret_cast<uint8_t**>(params + 4) = component;
        skippedAll &= Nx::PlayMovie(reinterpret_cast<Script::CStruct*>(params), nullptr);
    }
    bool introOnly = g_play_log == std::vector<std::string>{"credits"};
    // A movie played by the start-up script (the Anteara Mod's Shorty's movie) is skipped too; the same movie from
    // another script plays. (A stand-in script: only its m_scriptName is read.)
    g_play_log.clear();
    for (const char* scriptName : {"startup_loading_screen", "play_movie_from_menu"}) {
        const char* name = "movies\\shortys";
        alignas(16) uint8_t component[16] = {};
        alignas(8) uint8_t params[8] = {};
        component[0] = static_cast<uint8_t>(Script::ESYMBOLTYPE_STRING << 1);
        std::memcpy(component + 8, &name, 4);
        *reinterpret_cast<uint8_t**>(params + 4) = component;
        alignas(16) uint8_t script[sizeof(Script::CScript)] = {};
        uint32_t checksum = Crc::GenerateCRCFromString(scriptName);
        std::memcpy(script + offsetof(Script::CScript, m_scriptName), &checksum, 4);
        skippedAll &= Nx::PlayMovie(reinterpret_cast<Script::CStruct*>(params), script);
    }
    bool startUp = g_play_log == std::vector<std::string>{"movies\\shortys"};
    Nx::SkipIntroMovies() = savedSkip;
    Check(skippedAll && introOnly && startUp, "gfx: PlayMovie (flag on)");
}



// --- playing a movie (0x0044FDC0 and its helpers) -----------------------------------------------------------

std::vector<uint32_t> g_pm_log;
void PmNote(std::initializer_list<uint32_t> words) { g_pm_log.insert(g_pm_log.end(), words); }
uint32_t PmBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// What a run's stand-ins do.
struct MovieRunScript {
    const char* name;
    int soundSystem;   // BinkSetSoundSystem's result
    bool open;         // BinkOpen gives a movie
    long createResult; // CreateTexture's
    uint32_t frames;
    UINT screenSaver; // the screen saver's timeout
    float music, effects;
    int waitEvery;   // BinkWait says wait on every n-th call (0: never)
    int inactiveAt;  // the BinkWait call after which the app is inactive (0: never)
    int lostAt;      // the BinkNextFrame after which the device is lost (0: never)
    int lostResets;  // reset calls until it's back
    int quitAt;      // the BinkNextFrame after which quit is requested (0: never)
    int lockFailEvery;
    int skipPort, skipButton, skipAt; // a button (0 start, 1 X) going down on that read (0: never)
    bool heldFromStart;               // port 0's start and X down from the first read
    int fog;
};
MovieRunScript g_mr;
struct {
    int wait, nextFrame, reset, lock, read, events, inactivePumps;
    bool lostWait; // (ours: a reset left the device lost - the next window pump is our wait, not the original's)
} g_mc;
bool g_pm_ours; // (the run of Nx::PlayMovieFile, not the original)
alignas(16) uint8_t g_pm_bink[64];
alignas(16) uint8_t g_pm_bits[64];
alignas(16) uint8_t g_pm_devices[4][0x100];
alignas(16) uint8_t g_pm_manager[0x20];
void* g_pm_texture_vtable[24];
void* g_pm_texture[2] = {g_pm_texture_vtable, nullptr};
Nx::Bink* PmBink() { return reinterpret_cast<Nx::Bink*>(g_pm_bink); }
uint32_t PmRelative(const void* p, const void* base) { return p ? static_cast<uint32_t>(static_cast<const uint8_t*>(p) - static_cast<const uint8_t*>(base)) : 0xDEADu; }

int __stdcall PmBinkSetSoundSystem(void* open, void* param)
{
    PmNote({0xB55u, reinterpret_cast<uint32_t>(open), reinterpret_cast<uint32_t>(param)});
    return g_mr.soundSystem;
}
void* __stdcall PmBinkOpen(const char* path, uint32_t flags)
{
    PmNote({0xB0u, flags, static_cast<uint32_t>(std::strlen(path))});
    for (const char* c = path; *c; ++c)
        g_pm_log.push_back(static_cast<uint8_t>(*c));
    return g_mr.open ? g_pm_bink : nullptr;
}
int __stdcall PmBinkSetSoundOnOff(void* bink, int on) { PmNote({0xB50u, PmRelative(bink, g_pm_bink), static_cast<uint32_t>(on)}); return 0; }
void __stdcall PmBinkSetVolume(void* bink, uint32_t track, int volume) { PmNote({0xB57u, PmRelative(bink, g_pm_bink), track, static_cast<uint32_t>(volume)}); }
int __stdcall PmBinkPause(void* bink, int pause) { PmNote({0xBA5u, PmRelative(bink, g_pm_bink), static_cast<uint32_t>(pause)}); return 0; }
int __stdcall PmBinkWait(void* bink)
{
    ++g_mc.wait;
    PmNote({0xBA1u, PmRelative(bink, g_pm_bink), GameVariable<uint8_t>(0x00696D95)}); // (the movie-playing flag)
    if (g_mc.wait == g_mr.inactiveAt)
        GameVariable<bool>(0x007CCBE4) = false;
    return g_mr.waitEvery && g_mc.wait % g_mr.waitEvery == 0;
}
int __stdcall PmBinkDoFrame(void* bink) { PmNote({0xBD0u, PmRelative(bink, g_pm_bink)}); return 0; }
int __stdcall PmBinkCopyToBuffer(void* bink, void* bits, int pitch, uint32_t height, uint32_t x, uint32_t y, uint32_t flags)
{
    PmNote({0xBC7u, PmRelative(bink, g_pm_bink), PmRelative(bits, g_pm_bits), static_cast<uint32_t>(pitch), height, x, y, flags});
    return 0;
}
void __stdcall PmBinkNextFrame(void* bink)
{
    ++g_mc.nextFrame;
    ++PmBink()->frameNum;
    PmNote({0xB4Fu, PmRelative(bink, g_pm_bink)});
    if (g_mc.nextFrame == g_mr.lostAt)
        GameVariable<bool>(0x00786AB4) = true;
    if (g_mc.nextFrame == g_mr.quitAt)
        GameVariable<bool>(0x007D6A2C) = true;
}
void __stdcall PmBinkClose(void* bink) { PmNote({0xBC1u, PmRelative(bink, g_pm_bink)}); }
BOOL __stdcall PmSystemParametersInfo(UINT action, UINT param, PVOID value, UINT flags)
{
    PmNote({0x5B1u, action, param, value ? 1u : 0u, flags});
    if (action == SPI_GETSCREENSAVETIMEOUT)
        *static_cast<UINT*>(value) = g_mr.screenSaver;
    return TRUE;
}
void* __cdecl PmCreateSound(bool create) { PmNote({0x5C0u, create}); return reinterpret_cast<void*>(0x5D05D0); }
void __cdecl PmReleaseSound() { PmNote({0x5DEu}); }
float __fastcall PmSoundVolume(void* self) { PmNote({0x5F0u, reinterpret_cast<uint32_t>(self)}); return g_mr.effects; }
float __cdecl PmMusicVolume() { PmNote({0x3F0u}); return g_mr.music; }
void __cdecl PmPauseMusic() { PmNote({0xA05u}); }
void __cdecl PmStopStreams(int which) { PmNote({0x575u, static_cast<uint32_t>(which)}); }
void __cdecl PmSetFog(int on) { PmNote({0xF06u, static_cast<uint32_t>(on) & 0xFF}); } // (a bool: the original's upper bytes are stale)
void __cdecl PmResetDevice()
{
    PmNote({0x7E5u});
    if (++g_mc.reset >= g_mr.lostResets)
        GameVariable<bool>(0x00786AB4) = false;
    // Ours waits and pumps the window's messages while the device stays lost (the original spins without - hung
    // for good with another full-screen program up): that pump is the one deliberate difference, left out.
    if (g_mc.lostWait)
        PmNote({0xBADu}); // (ours didn't pump after the last reset that left it lost)
    g_mc.lostWait = g_pm_ours && GameVariable<bool>(0x00786AB4);
}
void __cdecl PmEvents()
{
    if (g_mc.lostWait) {
        g_mc.lostWait = false;
        return;
    }
    ++g_mc.events;
    PmNote({0xE7u});
    // Inactive for three pumps (the first may be the every-other-frame one, before the loop sees it).
    if (!GameVariable<bool>(0x007CCBE4) && ++g_mc.inactivePumps == 3)
        GameVariable<bool>(0x007CCBE4) = true;
}
void __cdecl PmFrame(void* texture, float x, float y, float width, float height, float alpha)
{
    PmNote({0xF4Au, PmRelative(texture, g_pm_texture), PmBits(x), PmBits(y), PmBits(width), PmBits(height), PmBits(alpha)});
}
void* __cdecl PmSioCreate(bool create) { PmNote({0x51Cu, create}); return g_pm_manager; }
void __cdecl PmSioRelease() { PmNote({0x51Du}); }
void __fastcall PmSioProcess(void* self, void*)
{
    PmNote({0x51Eu, PmRelative(self, g_pm_manager)});
    ++g_mc.read;
}
void* __fastcall PmSioDevice(void* self, void*, int index)
{
    PmNote({0x51Fu, PmRelative(self, g_pm_manager), static_cast<uint32_t>(index)});
    if (index == 1 || index < 0 || index > 3)
        return nullptr;
    uint8_t* data = g_pm_devices[index] + 0x1C;
    int read = g_mc.read;
    data[2] = static_cast<uint8_t>(read & 1 ? 0xEF : 0xFF); // other buttons going up and down
    data[3] = static_cast<uint8_t>(read & 2 ? 0xDF : 0xFF);
    if (g_mr.heldFromStart && index == 0) {
        data[2] &= ~0x08;
        data[3] &= ~0x40;
    }
    if (g_mr.skipAt && index == g_mr.skipPort && read >= g_mr.skipAt) {
        if (g_mr.skipButton == 0)
            data[2] &= ~0x08;
        else
            data[3] &= ~0x40;
    }
    return g_pm_devices[index];
}
void __cdecl PmSampler(int a, int b) { PmNote({0x5A3u, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); }
void __cdecl PmRendererTexture(int a, void* t, int c) { PmNote({0x7E8u, static_cast<uint32_t>(a), PmRelative(t, g_pm_texture), static_cast<uint32_t>(c)}); }
int __cdecl PmPrintf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int first = reinterpret_cast<uint32_t>(format) == 0x00647318 ? va_arg(args, int) : 0; // (the shuffle's count)
    va_end(args);
    PmNote({0xDB6u, reinterpret_cast<uint32_t>(format), static_cast<uint32_t>(first)});
    return -1;
}
// The D3D device and texture.
long __stdcall PmGetRenderState(void*, int state, uint32_t* value) { PmNote({0x6E5u, static_cast<uint32_t>(state)}); *value = 0x100u + state; return 0; }
long __stdcall PmSetRenderState(void*, int state, uint32_t value) { PmNote({0x5E5u, static_cast<uint32_t>(state), value}); return 0; }
long __stdcall PmGetSampler(void*, int s, int t, uint32_t* value) { PmNote({0x6A3u, static_cast<uint32_t>(s), static_cast<uint32_t>(t)}); *value = 0x200u + t; return 0; }
long __stdcall PmSetSampler(void*, int s, int t, uint32_t value) { PmNote({0x5A4u, static_cast<uint32_t>(s), static_cast<uint32_t>(t), value}); return 0; }
long __stdcall PmStageState(void*, int s, int t, uint32_t value) { PmNote({0x557u, static_cast<uint32_t>(s), static_cast<uint32_t>(t), value}); return 0; }
long __stdcall PmCreateTexture(void*, uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, int format, int pool, void** texture, void* shared)
{
    PmNote({0xC7Eu, w, h, levels, usage, static_cast<uint32_t>(format), static_cast<uint32_t>(pool), shared ? 1u : 0u});
    if (g_mr.createResult >= 0)
        *texture = g_pm_texture;
    return g_mr.createResult;
}
long __stdcall PmBeginScene(void*) { PmNote({0xBE9u}); return 0; }
long __stdcall PmEndScene(void*) { PmNote({0xE9Du}); return 0; }
long __stdcall PmClear(void*, uint32_t count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    PmNote({0xC1Eu, count, rects ? 1u : 0u, flags, color, PmBits(z), stencil});
    return 0;
}
long __stdcall PmPresent(void*, const void* a, const void* b, void* c, const void* d)
{
    PmNote({0x93Eu, a ? 1u : 0u, b ? 1u : 0u, c ? 1u : 0u, d ? 1u : 0u});
    return 0;
}
unsigned long __stdcall PmTextureRelease(void* t) { PmNote({0x7E1u, PmRelative(t, g_pm_texture)}); return 0; }
long __stdcall PmLockRect(void* t, uint32_t level, int* locked, const void* rect, uint32_t flags)
{
    ++g_mc.lock;
    PmNote({0x10Cu, PmRelative(t, g_pm_texture), level, rect ? 1u : 0u, flags});
    if (g_mr.lockFailEvery && g_mc.lock % g_mr.lockFailEvery == 0)
        return static_cast<long>(0x80004005);
    locked[0] = 0x800 + g_mc.lock;
    reinterpret_cast<void**>(locked)[1] = g_pm_bits + (g_mc.lock & 7);
    return 0;
}
long __stdcall PmUnlockRect(void* t, uint32_t level) { PmNote({0x11Cu, PmRelative(t, g_pm_texture), level}); return 0; }

// A 4-byte pointer written over an import table slot for the length of a test.
std::vector<uint8_t> PointerBytes(const void* p)
{
    std::vector<uint8_t> bytes(4);
    std::memcpy(bytes.data(), &p, 4);
    return bytes;
}
std::vector<CodePatch*> JumpBoth(uint32_t game, const void* ours, const void* to)
{
    std::vector<CodePatch*> patches;
    patches.push_back(new CodePatch(game, JumpTo(game, to)));
    if (ours) {
        auto at = reinterpret_cast<uint32_t>(ours);
        patches.push_back(new CodePatch(at, JumpTo(at, to)));
    }
    return patches;
}

// PlayMovieFile (with BeginMovie, EndMovie, DrawMovie and the button check) - the original with PartyMod's NOP
// (pads read every frame) vs ours: Bink, the D3D device and texture, the sound and input managers, the window
// pump and the screen saver call stood in; runs where Bink's sound set-up fails, the movie doesn't open, the
// texture can't be made, the movie plays out (waits, failed locks), start or X pressed (or held from the
// start: no skip), the app inactive for a while, the device lost and reset, quit requested, the screen saver
// timer reset (450 frames), a movie of 0 frames.
void TestPlayMovieFile()
{
    std::vector<CodePatch*> patches;
    auto add = [&](std::vector<CodePatch*> more) { patches.insert(patches.end(), more.begin(), more.end()); };
    add({new CodePatch(0x00450121, std::vector<uint8_t>(6, 0x90))}); // PartyMod's (ours has no such check)
    struct Slot {
        uint32_t address;
        const void* to;
    };
    for (Slot slot : {Slot{0x00645304, reinterpret_cast<const void*>(&PmBinkSetSoundSystem)},
                      Slot{0x006452E8, reinterpret_cast<const void*>(&PmBinkOpen)},
                      Slot{0x0064530C, reinterpret_cast<const void*>(&PmBinkSetSoundOnOff)},
                      Slot{0x00645310, reinterpret_cast<const void*>(&PmBinkSetVolume)},
                      Slot{0x00645314, reinterpret_cast<const void*>(&PmBinkPause)},
                      Slot{0x00645318, reinterpret_cast<const void*>(&PmBinkWait)},
                      Slot{0x006452EC, reinterpret_cast<const void*>(&PmBinkDoFrame)},
                      Slot{0x00645308, reinterpret_cast<const void*>(&PmBinkCopyToBuffer)},
                      Slot{0x0064531C, reinterpret_cast<const void*>(&PmBinkClose)},
                      Slot{0x00645320, reinterpret_cast<const void*>(&PmBinkNextFrame)},
                      Slot{0x00645214, reinterpret_cast<const void*>(&PmSystemParametersInfo)}})
        add({new CodePatch(slot.address, PointerBytes(slot.to))});
    // Ours calls SystemParametersInfoA through its own imports: the function itself, stood in.
    auto spi = reinterpret_cast<uint32_t>(GetProcAddress(GetModuleHandleA("user32.dll"), "SystemParametersInfoA"));
    add({new CodePatch(spi, JumpTo(spi, reinterpret_cast<const void*>(&PmSystemParametersInfo)))});
    add(JumpBoth(0x0047BA60, nullptr, reinterpret_cast<const void*>(&PmCreateSound)));
    add(JumpBoth(0x0047C6C0, nullptr, reinterpret_cast<const void*>(&PmReleaseSound)));
    add(JumpBoth(0x0047AFD0, nullptr, reinterpret_cast<const void*>(&PmSoundVolume)));
    add(JumpBoth(0x00450440, nullptr, reinterpret_cast<const void*>(&PmMusicVolume)));
    add(JumpBoth(0x00450650, nullptr, reinterpret_cast<const void*>(&PmPauseMusic)));
    add(JumpBoth(0x00450690, nullptr, reinterpret_cast<const void*>(&PmStopStreams)));
    add(JumpBoth(0x00490DE0, nullptr, reinterpret_cast<const void*>(&PmSetFog)));
    add(JumpBoth(0x0044F250, reinterpret_cast<const void*>(&Mlp::Manager::ResetDeviceIfLost),
                 reinterpret_cast<const void*>(&PmResetDevice)));
    add(JumpBoth(0x004DAB60, nullptr, reinterpret_cast<const void*>(&PmSampler)));
    add(JumpBoth(0x004DA730, nullptr, reinterpret_cast<const void*>(&PmRendererTexture)));
    add(JumpBoth(0x005F56E0, reinterpret_cast<const void*>(&Platform::ProcessWindowEvents), reinterpret_cast<const void*>(&PmEvents)));
    add(JumpBoth(0x0044FA80, reinterpret_cast<const void*>(&Nx::DrawMovieFrame), reinterpret_cast<const void*>(&PmFrame)));
    add(JumpBoth(0x00401C30, reinterpret_cast<const void*>(&Dbg::Printf), reinterpret_cast<const void*>(&PmPrintf)));
    add(JumpBoth(0x005BE670, reinterpret_cast<const void*>(&Sio::CreateManager), reinterpret_cast<const void*>(&PmSioCreate)));
    add(JumpBoth(0x005BDCF0, reinterpret_cast<const void*>(&Sio::ReleaseManager), reinterpret_cast<const void*>(&PmSioRelease)));
    add(JumpBoth(0x005BE3F0, CodeAddress(&Sio::Manager::ProcessDevices), reinterpret_cast<const void*>(&PmSioProcess)));
    add(JumpBoth(0x005BE4F0, CodeAddress(&Sio::Manager::GetDeviceByIndex), reinterpret_cast<const void*>(&PmSioDevice)));

    static void* vtable[100] = {};
    vtable[0x44 / 4] = reinterpret_cast<void*>(&PmPresent);
    vtable[0x5C / 4] = reinterpret_cast<void*>(&PmCreateTexture);
    vtable[0xA4 / 4] = reinterpret_cast<void*>(&PmBeginScene);
    vtable[0xA8 / 4] = reinterpret_cast<void*>(&PmEndScene);
    vtable[0xAC / 4] = reinterpret_cast<void*>(&PmClear);
    vtable[0xE4 / 4] = reinterpret_cast<void*>(&PmSetRenderState);
    vtable[0xE8 / 4] = reinterpret_cast<void*>(&PmGetRenderState);
    vtable[0x10C / 4] = reinterpret_cast<void*>(&PmStageState);
    vtable[0x110 / 4] = reinterpret_cast<void*>(&PmGetSampler);
    vtable[0x114 / 4] = reinterpret_cast<void*>(&PmSetSampler);
    static void* device[2] = {vtable, nullptr};
    g_pm_texture_vtable[2] = reinterpret_cast<void*>(&PmTextureRelease);
    g_pm_texture_vtable[0x4C / 4] = reinterpret_cast<void*>(&PmLockRect);
    g_pm_texture_vtable[0x50 / 4] = reinterpret_cast<void*>(&PmUnlockRect);

    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x007D69E0, 4, {}}, {0x00696D80, 0x18, {}}, {0x0067D7C4, 0x1C, {}},
                                {0x00787338, 4, {}}, {0x007CCBE4, 1, {}},   {0x007D6A2C, 1, {}},
                                {0x00786AB4, 1, {}}, {0x007D6A38, 4, {}},   {0x00786D6C, 8, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    GameVariable<void*>(0x007D69E0) = device;

    const MovieRunScript scripts[] = {
        // name, sound, open, create, frames, saver, music, effects, wait, inactive, lost, resets, quit, lockFail,
        // skip port/button/at, held, fog
        {"credits", 0, false, 0, 10, 0, 50.0f, 100.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 1},
        {"movies\\bik\\intro", 1, true, static_cast<long>(0x8876086C), 10, 0, 33.3f, 12.5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 0},
        {"a/b", 1, true, 0, 12, 0, 50.0f, 100.0f, 3, 0, 0, 0, 0, 4, 0, 0, 0, true, 1},
        {"x\\y\\skip_start", 1, true, 0, 40, 0, 80.0f, 30.0f, 0, 0, 0, 0, 0, 0, 2, 0, 3, false, 0},
        {"", 0, true, 0, 40, 0, 0.0f, 0.0f, 5, 4, 3, 2, 0, 0, 3, 1, 6, false, 1},
        {"\\lead", 1, true, 0, 450, 600, 100.0f, 99.9f, 0, 0, 0, 0, 430, 7, 0, 0, 0, true, 0},
        {"zero", 1, true, 0, 0, 5, 1.0f, 2.0f, 2, 0, 0, 0, 9, 0, 0, 0, 0, false, 0},
        {"one", 1, true, 0, 1, 0, 70.0f, 70.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 1},
    };
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_pm_ours = !original;
        for (const MovieRunScript& script : scripts) {
            g_mr = script;
            g_mc = {};
            g_pm_log.clear();
            std::memset(g_pm_bink, 0, sizeof(g_pm_bink));
            PmBink()->width = 640;
            PmBink()->height = 352;
            PmBink()->frames = script.frames;
            PmBink()->frameNum = 1;
            std::memset(g_pm_devices, 0x5A, sizeof(g_pm_devices));
            std::memset(reinterpret_cast<void*>(0x00696D80), 0x77, 0x18);
            GameVariable<int>(0x00787338) = script.fog;
            GameVariable<bool>(0x007CCBE4) = true;
            GameVariable<bool>(0x007D6A2C) = false;
            GameVariable<bool>(0x00786AB4) = false;
            GameVariable<void*>(0x007D6A38) = reinterpret_cast<void*>(0xD50D50);
            GameVariable<int>(0x00786D6C) = 1280;
            GameVariable<int>(0x00786D70) = 720;
            GameVariable<uint32_t>(0x0067D7C4) = 3;
            for (int i = 0; i < 4; ++i)
                GameVariable<float>(0x0067D7C8 + 4 * i) = -1.0f - i; // (BeginMovie's sizes)
            GameVariable<float>(0x0067D7D8) = 0.75f; // the frame's size (1.0 in the exe)
            GameVariable<float>(0x0067D7DC) = 0.5f;
            if (original)
                Original<void(__cdecl*)(const char*)>(0x0044FDC0)(script.name);
            else
                Nx::PlayMovieFile(script.name);
            traces[run].insert(traces[run].end(), g_pm_log.begin(), g_pm_log.end());
            traces[run].insert(traces[run].end(), reinterpret_cast<uint32_t*>(0x00696D80), reinterpret_cast<uint32_t*>(0x00696D98));
            traces[run].insert(traces[run].end(), reinterpret_cast<uint32_t*>(0x0067D7C8), reinterpret_cast<uint32_t*>(0x0067D7D8));
            traces[run].insert(traces[run].end(), {PmBink()->frameNum, static_cast<uint32_t>(g_mc.read), static_cast<uint32_t>(g_mc.events),
                                                   GameVariable<uint32_t>(0x00787338), static_cast<uint32_t>(0xFEEDu)});
        }
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    for (auto it = patches.rbegin(); it != patches.rend(); ++it)
        delete *it;
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  play movie entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    if (traces[0].size() != traces[1].size())
        Log("  play movie trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    Check(traces[0] == traces[1] && traces[1].size() > 5000, "gfx: playing a movie (PartyMod: pads read every frame)");
}

// The button check's register stub (control data in EAX, the mask in ECX, the seen-up bits in ESI) vs the
// original's.
bool CallButtonCheck(void* code, const uint8_t* data, uint16_t* seen, uint32_t mask)
{
    bool result;
    __asm {
        push esi
        mov eax, data
        mov ecx, mask
        mov esi, seen
        call code
        pop esi
        mov result, al
    }
    return result;
}
void TestButtonPressedAgain()
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        void* code = run == 0 ? OriginalFunction(0x0044F840) : reinterpret_cast<void*>(&Nx::ButtonPressedAgainStub);
        for (uint32_t mask : {0x40u, 0x800u, 0x41u, 0x10040u})
            for (int bytes : {0x0000, 0xFFFF, 0x00BF, 0xF7FF, 0x0840, 0x4008, -1})
                for (uint16_t seen : {uint16_t{0}, uint16_t{0x40}, uint16_t{0x800}, uint16_t{0x1234}, uint16_t{0xFFBF}}) {
                    uint8_t data[4] = {0x11, 0x22, static_cast<uint8_t>(bytes >> 8), static_cast<uint8_t>(bytes)};
                    uint16_t s = seen;
                    bool r = CallButtonCheck(code, bytes < 0 ? nullptr : data, &s, mask);
                    traces[run].insert(traces[run].end(), {static_cast<uint32_t>(r), s});
                }
    }
    Check(traces[0] == traces[1], "gfx: movie skip button check (register stub)");
}


// --- the playlist shuffle (0x00452200) -----------------------------------------------------------------------

int g_pl_stream_state;
int g_pl_script_track;
alignas(16) uint8_t g_pl_struct[16];
int __cdecl PlStreamStateResult() { PmNote({0x5757u}); return g_pl_stream_state; }
void __cdecl PlVolume() { PmNote({0x7011u}); }
void __cdecl PlPlayTrack(void* track, int special)
{
    PmNote({0x91A7u, static_cast<uint32_t>(reinterpret_cast<uint8_t*>(track) - reinterpret_cast<uint8_t*>(0x0069806C)), static_cast<uint32_t>(special)});
}
int __cdecl PlGetInteger(uint32_t name) { PmNote({0x6E71u, name}); return g_pl_script_track; }
void* __cdecl PlNewStruct() { PmNote({0x4E5u}); return g_pl_struct; }
void* __fastcall PlStructInit(void* self, void*) { PmNote({0x1417u, PmRelative(self, g_pl_struct)}); return self; }
void __fastcall PlAddInteger(void* self, void*, uint32_t name, int value)
{
    PmNote({0xADD1u, PmRelative(self, g_pl_struct), name, static_cast<uint32_t>(value)});
}
void __fastcall PlStructClear(void* self, void*) { PmNote({0xC1EAu, PmRelative(self, g_pl_struct)}); }
void __cdecl PlDeleteStruct(void* s) { PmNote({0xDE15u, PmRelative(s, g_pl_struct)}); }
void __cdecl PlRunScript(uint32_t name, void* params, int a, int b, int c)
{
    PmNote({0x5C41u, name, PmRelative(params, g_pl_struct), static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)});
}

// The next track: music off (three ways), no next track, a stream playing, playlist 1 (the script's track: in
// range, past the end, negative), the special track, an empty playlist, then many tracks of a 20-track and a
// 100-track playlist with tracks off in both halves (reshuffles, the last track not first again, a track
// switched off or already played when its turn comes), a playlist with only one track on, a negative count -
// the original with PartyMod's two shuffle calls written in (the C runtime reseeded alike for both runs).
void TestPlayNextTrack()
{
    std::vector<CodePatch*> patches;
    auto add = [&](std::vector<CodePatch*> more) { patches.insert(patches.end(), more.begin(), more.end()); };
    for (uint32_t call : {0x004523A7u, 0x004523B4u}) {
        std::vector<uint8_t> bytes = JumpTo(call, reinterpret_cast<const void*>(&Pcm::ShuffleRandom));
        bytes[0] = 0xE8;
        add({new CodePatch(call, bytes)});
    }
    add(JumpBoth(0x00453250, nullptr, reinterpret_cast<const void*>(&PlStreamStateResult)));
    add(JumpBoth(0x00451400, nullptr, reinterpret_cast<const void*>(&PlVolume)));
    add(JumpBoth(0x004520D0, nullptr, reinterpret_cast<const void*>(&PlPlayTrack)));
    add(JumpBoth(0x00475790, CodeAddress(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript)),
                 reinterpret_cast<const void*>(&PlRunScript)));
    add(JumpBoth(0x00478F60, CodeAddress(static_cast<int (*)(uint32_t)>(&Script::GetInteger)), reinterpret_cast<const void*>(&PlGetInteger)));
    add(JumpBoth(0x00409030, CodeAddress(&Script::NewStruct), reinterpret_cast<const void*>(&PlNewStruct)));
    add(JumpBoth(0x00476410, CodeAddress(&Script::CStruct::Init), reinterpret_cast<const void*>(&PlStructInit)));
    add(JumpBoth(0x00477B80, CodeAddress(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger)),
                 reinterpret_cast<const void*>(&PlAddInteger)));
    add(JumpBoth(0x00477130, CodeAddress(&Script::CStruct::Clear), reinterpret_cast<const void*>(&PlStructClear)));
    add(JumpBoth(0x00409070, CodeAddress(&Script::DeleteStruct), reinterpret_cast<const void*>(&PlDeleteStruct)));
    add(JumpBoth(0x00401C30, reinterpret_cast<const void*>(&Dbg::Printf), reinterpret_cast<const void*>(&PmPrintf)));

    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x0069806C, 0x2C + 3 * 0xCB0, {}}, {0x00696D9C, 4 + 4 * 130, {}}, {0x0067D7F4, 8, {}},
                                {0x0069BA98, 0x18, {}}, {0x0069BAC4, 4, {}}, {0x007D6A38, 4, {}}, {0x007CEFA8, 4, {}},
                                {0x0067D7E0, 1, {}}, {0x00690FC8, 24, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    auto* lists = reinterpret_cast<Pcm::Playlist*>(0x00698098);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];
        g_pm_log.clear();
        for (Saved& s : saved)
            std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        std::srand(1234);
        GameVariable<uint32_t>(0x00690FC8) = 0x0AA2FB3F; // both random streams: as InitialRand(77) leaves them
        GameVariable<uint32_t>(0x00690FCC) = 0x12B9B0A1;
        GameVariable<uint32_t>(0x00690FD0) = 77;
        GameVariable<uint32_t>(0x00690FD4) = 0x0AA2FB3F;
        GameVariable<uint32_t>(0x00690FD8) = 0x12B9B0A1;
        GameVariable<uint32_t>(0x00690FDC) = 77;
        GameVariable<uint32_t>(0x00696D9C) = 11; // (read as the last track before the first shuffle)
        for (int i = 0; i < 130; ++i)
            GameVariable<int>(0x00696DA0 + 4 * i) = 0x300 + i;
        lists[0].count = 20;
        lists[0].off[0] = (1ull << 3) | (1ull << 7) | (1ull << 19) | (1ull << 40);
        lists[0].off[1] = 1; // (track 64: past the count)
        lists[1].count = 10;
        lists[2].count = 100;
        lists[2].off[0] = (1ull << 5) | (1ull << 63);
        lists[2].off[1] = (1ull << 2) | (1ull << 30);
        GameVariable<bool>(0x0067D7F4) = true;
        auto next = [&]() {
            if (original)
                Original<void(__cdecl*)()>(0x00452200)();
            else
                Pcm::PlayNextTrack();
            trace.insert(trace.end(), g_pm_log.begin(), g_pm_log.end());
            g_pm_log.clear();
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x00696D9C), reinterpret_cast<uint32_t*>(0x00696D9C) + 104);
            trace.insert(trace.end(), {GameVariable<uint32_t>(0x0067D7F4), GameVariable<uint32_t>(0x0067D7F8),
                                       GameVariable<uint32_t>(0x0069BAC4), GameVariable<uint32_t>(0x0069BA98),
                                       GameVariable<uint32_t>(0x0069BAAC), GameVariable<uint32_t>(0x00690FDC)});
            for (int l = 0; l < 3; ++l)
                trace.insert(trace.end(), reinterpret_cast<uint32_t*>(&lists[l].played[0]), reinterpret_cast<uint32_t*>(&lists[l].played[0]) + 4);
        };
        GameVariable<uint32_t>(0x0069BAAC) = 0x55;
        g_pl_stream_state = 1;
        GameVariable<int>(0x007CEFA8) = 1;
        GameVariable<bool>(0x0067D7E0) = true;
        next(); // no sound
        GameVariable<void*>(0x007D6A38) = reinterpret_cast<void*>(0xD50D50);
        GameVariable<int>(0x007CEFA8) = 2;
        next(); // platform 2
        GameVariable<int>(0x007CEFA8) = 1;
        GameVariable<bool>(0x0067D7E0) = false;
        next(); // music off
        GameVariable<bool>(0x0067D7E0) = true;
        lists[0].unknownCA8 = 1;
        next();
        lists[0].unknownCA8 = 0;
        g_pl_stream_state = 2;
        next(); // a stream playing
        g_pl_stream_state = 0;
        next();
        g_pl_stream_state = 1;
        GameVariable<int>(0x0069BA9C) = 1;
        for (int track : {4, 9, 10, 11, -1}) {
            g_pl_script_track = track;
            next();
        }
        GameVariable<int>(0x0069BA9C) = 0;
        GameVariable<bool>(0x0069BAA0) = true;
        next(); // the special track
        GameVariable<bool>(0x0069BAA0) = false;
        GameVariable<int>(0x0069BA9C) = 2;
        int saveCount = lists[2].count;
        lists[2].count = 0;
        next(); // empty
        lists[2].count = saveCount;
        GameVariable<int>(0x0069BA9C) = 0;
        for (int i = 0; i < 45; ++i) {
            if (i == 6 || i == 25) { // the next track already played / switched off since the shuffle
                int position = GameVariable<int>(0x0069BAC4) + 1;
                int track = GameVariable<int>(0x00696DA0 + 4 * position);
                if (i == 6)
                    lists[0].played[0] |= 1ull << track;
                else
                    lists[0].off[0] |= 1ull << track;
            }
            next();
        }
        GameVariable<int>(0x0069BA9C) = 2;
        GameVariable<bool>(0x0067D7F4) = true;
        for (int i = 0; i < 210; ++i) {
            if (i == 120 || i == 150) {
                int position = GameVariable<int>(0x0069BAC4) + 1;
                int track = GameVariable<int>(0x00696DA0 + 4 * position);
                if (track >= 64)
                    lists[2].played[1] |= 1ull << (track - 64);
                else
                    lists[2].off[0] |= 1ull << track;
            }
            next();
        }
        // Only one track on: the shuffle can't avoid repeating it.
        GameVariable<int>(0x0069BA9C) = 0;
        lists[0].count = 3;
        lists[0].off[0] = 3;
        GameVariable<bool>(0x0067D7F4) = true;
        for (int i = 0; i < 4; ++i)
            next();
        lists[0].count = -1;
        GameVariable<bool>(0x0067D7F4) = false;
        next();
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    for (auto it = patches.rbegin(); it != patches.rend(); ++it)
        delete *it;
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  next track entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    Check(traces[0] == traces[1] && traces[1].size() > 30000, "music: next track (PartyMod's shuffle)");
}

} // namespace

SELFTEST(Early, TestSkaterPlatformStructure);
SELFTEST(Early, TestSkaterAcidDrops);
SELFTEST(Early, TestRetrieveTopStats);
SELFTEST(Early, TestOnlineIdentity);
SELFTEST(Early, TestDrawFlash);
SELFTEST(Early, TestDrawMovieFrame);
SELFTEST(Early, TestPlayMovie);
SELFTEST(Early, TestPlayMovieFile);
SELFTEST(Early, TestButtonPressedAgain);
SELFTEST(Early, TestPlayNextTrack);
