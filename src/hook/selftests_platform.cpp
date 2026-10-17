// Self-tests for the platform layer and the memory card's space CFunc - the functions PartyMod changes, each
// compared with the original as PartyMod leaves it (its patch bytes written into the original for the test).

#include <cstdint>
#include <cstring>
#include <vector>

#include <windows.h>

#include "game/gfx/settings.h"
#include "game/mc/memcard.h"
#include "game/mc/memcard_cfuncs.h"
#include "game/script/struct.h"
#include "game/sio/sdl_input.h"
#include "game/sys/platform.h"
#include "game/sys/platform_cfuncs.h"
#include "game/sys/sdl_events.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"


namespace {

// Our own functions may be reached through an incremental-linking jump: detour the function itself.
uint32_t Resolve(const void* code)
{
    auto* p = static_cast<const uint8_t*>(code);
    while (p[0] == 0xE9) {
        int32_t relative;
        std::memcpy(&relative, p + 1, 4);
        p += 5 + relative;
    }
    return reinterpret_cast<uint32_t>(p);
}

// Bytes of the exe overwritten for the length of a test (restored when it goes).
struct TestPatch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    TestPatch(uint32_t address, const std::vector<uint8_t>& bytes)
        : at(reinterpret_cast<uint8_t*>(address)), saved(bytes.size())
    {
        DWORD old;
        VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved.data(), at, bytes.size());
        std::memcpy(at, bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
    }
    ~TestPatch()
    {
        std::memcpy(at, saved.data(), saved.size());
        FlushInstructionCache(GetCurrentProcess(), at, saved.size());
    }
};

// An E8 (call) or E9 (jump) at `address` to `to`, as PartyMod's patchCall / patchJmp write them.
std::vector<uint8_t> Branch(uint8_t opcode, uint32_t address, const void* to)
{
    std::vector<uint8_t> bytes(5, opcode);
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (address + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}

// A jump written over a function's start for the length of a test.
struct TestDetour : TestPatch {
    TestDetour(uint32_t address, const void* to) : TestPatch(address, Branch(0xE9, address, to)) {}
};

struct Saved {
    uint32_t address;
    uint32_t size;
    std::vector<uint8_t> bytes;
};
void SaveAll(std::vector<Saved>& saved)
{
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
}
void RestoreAll(const std::vector<Saved>& saved)
{
    for (const Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
}

void Compare(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what)
{
    Check(expected == actual, what);
    if (expected.size() != actual.size())
        Log("  %s trace sizes: original %u ours %u", what, static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  %s entry %u: original %08X ours %08X", what, static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// --- PartyMod's functions, stood in for (calls recorded) ------------------------------------------------

std::vector<uint32_t> g_platform_log;
void __cdecl StandInSetActuators(int port, uint16_t high, uint16_t low)
{
    g_platform_log.insert(g_platform_log.end(), {0x5E7A0u, static_cast<uint32_t>(port), high, low});
}
void __cdecl StandInHandleEvents() { g_platform_log.push_back(0xE7E27u); }
void __stdcall StandInInitManager()
{
    // (what's been set up by then: the bindings, the graphics options' first word, the DirectInput object)
    g_platform_log.insert(g_platform_log.end(), {0x1A17u, GameVariable<uint32_t>(0x007D6770),
                                                 GameVariable<uint32_t>(0x007D6434),
                                                 GameVariable<uint32_t>(0x007D69D0)});
}
void __cdecl StandInWriteConfigValues()
{
    g_platform_log.insert(g_platform_log.end(), {0xC0F1u, GameVariable<uint32_t>(0x007D6444)});
    GameVariable<uint8_t>(0x007D6434) = 0x11; // (as Nx::WriteGraphicsSettings writes the options)
    GameVariable<uint32_t>(0x007D6444) = 0x222;
}
void __cdecl StandInKeyNames()
{
    g_platform_log.insert(g_platform_log.end(), {0x4E75u, GameVariable<uint32_t>(0x007D6444)});
}

// Rumble and the event pump: the original's start is PartyMod's jump to its function; ours calls it.
void TestPlatformWholeReplacements()
{
    TestDetour setActuators(Resolve(reinterpret_cast<const void*>(&Sio::SetSdlActuators)),
                            reinterpret_cast<const void*>(&StandInSetActuators));
    TestDetour events(Resolve(reinterpret_cast<const void*>(&Platform::PumpSdlEvents)),
                      reinterpret_cast<const void*>(&StandInHandleEvents));
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_platform_log.clear();
        {
            TestPatch jumpA(0x005F3A50, Branch(0xE9, 0x005F3A50, reinterpret_cast<const void*>(&Sio::SetSdlActuators)));
            TestPatch jumpB(0x005F56E0, Branch(0xE9, 0x005F56E0, reinterpret_cast<const void*>(&Platform::PumpSdlEvents)));
            struct Case {
                int port, left, right;
            };
            for (Case c : {Case{0, 0, 0}, Case{1, 0xFFFF, 0}, Case{2, 0x1234, 0x8765}, Case{3, 0x12345, 0x7FFF0001},
                           Case{1, -1, 0xABCD}}) {
                if (original)
                    Original<void(__cdecl*)(int, int, int)>(0x005F3A50)(c.port, c.left, c.right);
                else
                    Platform::SetActuators(c.port, c.left, c.right);
                if (original)
                    Original<void(__cdecl*)()>(0x005F56E0)();
                else
                    Platform::ProcessWindowEvents();
            }
        }
        traces[run] = g_platform_log;
    }
    Check(traces[0].size() == 5 * 5, "platform: rumble and the event pump reached PartyMod's functions");
    Compare(traces[0], traces[1], "platform: rumble and the event pump");
}

// Input set-up: no window, already set up, and the set-up over dirty globals; PartyMod's three patches in
// the original (its settings and input manager called, DirectInput NOPed).
void TestPlatformInitInput()
{
    std::vector<Saved> saved = {{0x007D60F8, 0x578, {}}, {0x007D42C8, 0x18, {}}, {0x007D6770, 0x260, {}},
                                {0x007D69D0, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        TestDetour manager(Resolve(reinterpret_cast<const void*>(&Sio::InitSdlInput)),
                           reinterpret_cast<const void*>(&StandInInitManager));
        TestDetour config(Resolve(reinterpret_cast<const void*>(&Nx::WriteGraphicsSettings)),
                          reinterpret_cast<const void*>(&StandInWriteConfigValues));
        TestDetour keyNames(0x005F3C10, reinterpret_cast<const void*>(&StandInKeyNames));
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            std::vector<uint32_t>& trace = traces[run];
            // PartyMod: call its settings writer (Nx::WriteGraphicsSettings) at 0x5F4591; 45 NOPs at 0x5F459D,
            // then call its input set-up (Sio::InitSdlInput) at 0x5F45A2.
            std::vector<uint8_t> nops(45, 0x90);
            std::vector<uint8_t> call = Branch(0xE8, 0x005F45A2, reinterpret_cast<const void*>(&Sio::InitSdlInput));
            std::memcpy(&nops[5], call.data(), 5);
            TestPatch settings(0x005F4591, Branch(0xE8, 0x005F4591, reinterpret_cast<const void*>(&Nx::WriteGraphicsSettings)));
            TestPatch input(0x005F459D, nops);
            int variant = 0;
            for (uintptr_t window : {uintptr_t(0), uintptr_t(0x1234)}) {
                for (uint32_t directInput : {0u, 0xD1D1D1D0u}) {
                    ++variant;
                    for (const Saved& s : saved)
                        for (uint32_t i = 0; i < s.size; ++i)
                            reinterpret_cast<uint8_t*>(s.address)[i] = static_cast<uint8_t>(i * 13 + variant);
                    GameVariable<uint32_t>(0x007D69D0) = directInput;
                    g_platform_log.clear();
                    auto* hwnd = reinterpret_cast<HWND>(window);
                    auto* instance = reinterpret_cast<HINSTANCE>(0x400000);
                    bool result = original
                                      ? Original<bool(__cdecl*)(HINSTANCE, HWND, int)>(0x005F44F0)(instance, hwnd, 77)
                                      : Platform::InitInput(instance, hwnd, 77);
                    trace.push_back(result);
                    trace.insert(trace.end(), g_platform_log.begin(), g_platform_log.end());
                    for (const Saved& s : saved)
                        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(s.address),
                                     reinterpret_cast<uint32_t*>(s.address + s.size));
                }
            }
        }
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "platform: input set-up");
}

// --- GetMemCardSpaceAvailable --------------------------------------------------------------------------------

Mc::Card* g_standin_card;
struct StandInStruct {
    void AddInteger(uint32_t name, int value)
    {
        g_platform_log.insert(g_platform_log.end(),
                              {0xADD1u, reinterpret_cast<uint32_t>(this), name, static_cast<uint32_t>(value)});
    }
};
struct StandInManager {
    Mc::Card* GetCard(int port, int slot)
    {
        g_platform_log.insert(g_platform_log.end(), {0x6E7Cu, reinterpret_cast<uint32_t>(this),
                                                     static_cast<uint32_t>(port), static_cast<uint32_t>(slot)});
        return g_standin_card;
    }
};
int32_t __fastcall StandInFreeSpace(uint8_t* card)
{
    g_platform_log.insert(g_platform_log.end(), {0xF5EEu, reinterpret_cast<uint32_t>(card)});
    return 0x7FFE0001;
}

// With and without a card; PartyMod's call to its free space count (Mc::FreeDiskBlocks) in the original. The struct's adds,
// the card lookup and the space count are stood in for (in the original and in our code alike).
void TestMemCardSpaceAvailable()
{
    using AddFn = void (Script::CStruct::*)(uint32_t, int);
    TestDetour addOriginal(0x00477B80, CodeAddress(&StandInStruct::AddInteger));
    TestDetour addOurs(Resolve(CodeAddress(static_cast<AddFn>(&Script::CStruct::AddInteger))),
                       CodeAddress(&StandInStruct::AddInteger));
    TestDetour cardOriginal(0x005BC730, CodeAddress(&StandInManager::GetCard));
    TestDetour cardOurs(Resolve(CodeAddress(&Mc::Manager::GetCard)), CodeAddress(&StandInManager::GetCard));
    TestDetour space(Resolve(reinterpret_cast<const void*>(&Mc::FreeDiskBlocks)),
                     reinterpret_cast<const void*>(&StandInFreeSpace));
    TestPatch partyMod(0x005A6E7E, Branch(0xE8, 0x005A6E7E, reinterpret_cast<const void*>(&Mc::FreeDiskBlocks)));
    void* savedManager = GameVariable<void*>(0x007D1890);
    alignas(16) static uint8_t manager[0x90], card[0x10], script[0x20], results[8];
    GameVariable<void*>(0x007D1890) = manager;
    reinterpret_cast<void**>(script)[0x14 / 4] = results;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        for (Mc::Card* which : {static_cast<Mc::Card*>(nullptr), reinterpret_cast<Mc::Card*>(card)}) {
            g_standin_card = which;
            g_platform_log.clear();
            auto* params = reinterpret_cast<Script::CStruct*>(0x5A5A5A50);
            auto* calling = reinterpret_cast<Script::CScript*>(script);
            bool result = original
                              ? Original<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x005A6E40)(params, calling)
                              : Mc::GetMemCardSpaceAvailable(params, calling);
            traces[run].push_back(result);
            traces[run].insert(traces[run].end(), g_platform_log.begin(), g_platform_log.end());
        }
    }
    GameVariable<void*>(0x007D1890) = savedManager;
    Check(traces[0].size() > 20, "memory card space: the original reached its calls");
    Compare(traces[0], traces[1], "memory card space");
}

// --- IsPS2 ---------------------------------------------------------------------------------------------------

// The IsPS2 script command, through the CFunction table's entry: the original's (the shared "return false" stub
// 0x005B9E50) vs ours (InstallIsPS2 pointing the entry at Sys::CFunc_IsPS2), PS2 controls off and on, over no
// script, the vehicle grind script, scripts whose names differ from it in one bit (low, high) and another script.
// PS2 controls off: ours must answer as the original; on: true only for the vehicle grind script (what PartyMod's
// wrapper of the entry, isPs2Wrapper_DD, answered). The entry's other fields and neighbours stay as they were.
void TestIsPS2()
{
    constexpr uint32_t kEntry = 0x00681468;
    using CFunctionFn = bool(__cdecl*)(void*, void*);
    std::vector<Saved> saved = {{kEntry - 8, 24, {}}};
    SaveAll(saved);
    uint8_t savedPs2 = Sio::TheInputSettings().ps2Controls;
    Check(std::strcmp(GameVariable<const char*>(kEntry), "IsPS2") == 0 && GameVariable<uint32_t>(kEntry + 4) == 0x005B9E50,
          "IsPS2: the table entry is IsPS2's, the shared stub");

    alignas(16) static uint8_t scripts[4][0xD0];
    const uint32_t names[4] = {0x47F15883, 0x47F15882, 0xC7F15883, 0x12345678};
    for (int i = 0; i < 4; ++i) {
        std::memset(scripts[i], 0xA5, sizeof(scripts[i]));
        std::memcpy(scripts[i] + 0xC8, &names[i], 4);
    }
    void* cases[5] = {nullptr, scripts[0], scripts[1], scripts[2], scripts[3]};

    std::vector<uint32_t> traces[2];
    std::vector<uint32_t> expected;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        for (int ps2 = 0; ps2 < 2; ++ps2) {
            RestoreAll(saved);
            Sio::TheInputSettings().ps2Controls = static_cast<uint8_t>(ps2);
            if (!original)
                Sys::InstallIsPS2();
            auto function = reinterpret_cast<CFunctionFn>(GameVariable<uint32_t>(kEntry + 4));
            for (void* script : cases) {
                bool answer = function(reinterpret_cast<void*>(0x1234), script);
                traces[run].push_back(answer);
                if (original)
                    expected.push_back(ps2 && script == scripts[0]);
            }
            for (uint32_t a = kEntry - 8; a < kEntry + 16; a += 4)
                if (a != kEntry + 4)
                    traces[run].push_back(GameVariable<uint32_t>(a));
        }
    }
    bool installed = GameVariable<uint32_t>(kEntry + 4) == reinterpret_cast<uint32_t>(&Sys::CFunc_IsPS2);
    RestoreAll(saved);
    Sio::TheInputSettings().ps2Controls = savedPs2;
    Check(installed, "IsPS2: InstallIsPS2 points the entry at ours");
    // PS2 controls off: as the original (the first half of each run's answers and fields).
    std::vector<uint32_t> originalOff(traces[0].begin(), traces[0].begin() + traces[0].size() / 2);
    std::vector<uint32_t> oursOff(traces[1].begin(), traces[1].begin() + traces[1].size() / 2);
    Compare(originalOff, oursOff, "IsPS2 (PS2 controls off)");
    // PS2 controls on: the vehicle grind script only (the fields as the original's).
    std::vector<uint32_t> oursOn(traces[1].begin() + traces[1].size() / 2, traces[1].end());
    std::vector<uint32_t> wanted(expected.begin() + 5, expected.end());
    std::vector<uint32_t> fields(traces[0].begin() + traces[0].size() / 2 + 5, traces[0].end());
    wanted.insert(wanted.end(), fields.begin(), fields.end());
    Compare(wanted, oursOn, "IsPS2 (PS2 controls on: the vehicle grind script)");
}

} // namespace

SELFTEST(Early, TestIsPS2);
SELFTEST(Early, TestPlatformWholeReplacements);
SELFTEST(Early, TestPlatformInitInput);
SELFTEST(Early, TestMemCardSpaceAvailable);
