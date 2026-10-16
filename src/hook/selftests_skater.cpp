// Self-tests for src/game/skater: the control code PartyMod's PS2 controls change, original (with PartyMod's
// patch bytes written in for the "on" runs) vs ours.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/script/struct.h"
#include "game/skater/controls.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

extern "C" struct PartyModInputSettings {
    uint8_t isPs2Controls;
    uint8_t uicontrols;
} inputsettings;

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
struct PartyModPs2Bytes {
    std::vector<CodePatch*> patches;
    explicit PartyModPs2Bytes(bool on)
    {
        if (!on)
            return;
        patches.push_back(new CodePatch(0x0051F4C8, {0x05}));
        patches.push_back(new CodePatch(0x00527546, std::vector<uint8_t>(6, 0x90)));
        patches.push_back(new CodePatch(0x00527636, std::vector<uint8_t>(6, 0x90)));
    }
    ~PartyModPs2Bytes()
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
    uint8_t savedSettings = inputsettings.isPs2Controls;
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
                    PartyModPs2Bytes bytes(ps2 != 0);
                    original = Original<void*(__thiscall*)(void*, void*)>(0x0051F4C0)(nullptr, params);
                }
                inputsettings.isPs2Controls = static_cast<uint8_t>(ps2);
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
    inputsettings.isPs2Controls = savedSettings;
    Check(same, "skater: platform structure (PS2 controls off and on)");
}

// The two acid drops over the buttons, the flags that stop them, the physics finding a drop or not, walking or
// not, PartyMod's PS2 controls off and on: the skater's fields and every callee's arguments.
void TestSkaterAcidDrops()
{
    uint8_t savedSettings = inputsettings.isPs2Controls;
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
                        PartyModPs2Bytes bytes(ps2 != 0);
                        result = Original<bool(__thiscall*)(void*)>(which ? 0x00527600 : 0x00527510)(skater);
                    } else {
                        inputsettings.isPs2Controls = static_cast<uint8_t>(ps2);
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
    inputsettings.isPs2Controls = savedSettings;
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

} // namespace

SELFTEST(Early, TestSkaterPlatformStructure);
SELFTEST(Early, TestSkaterAcidDrops);
