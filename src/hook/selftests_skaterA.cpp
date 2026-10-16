// Self-tests for the skater's core physics functions PartyMod changes (its PS2-style controls): original - with
// PartyMod's bytes written in for the "on" runs - vs ours, on stand-in components, every callee into game code
// replaced by a recording stand-in for the length of the test.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/script/struct.h"
#include "game/sio/sdl_input.h"
#include "game/skater/core_physics.h"
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
    CodePatch(const CodePatch&) = delete;
    CodePatch& operator=(const CodePatch&) = delete;
};
std::vector<uint8_t> JumpTo(uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes(5);
    bytes[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}

// --- recording stand-ins ----------------------------------------------------------------------------------
// Each callee gets a thunk (push its index; jump to StandInEntry) written over its first bytes. The entry
// records the call - the callee, `this`, the stack arguments (pointers into the test's arena made relative,
// pointers into the stack just marked), data behind an input pointer - fills an output buffer, and returns a
// value from a seeded sequence (the same in both runs, as long as the calls are the same), popping what the
// real callee pops and keeping every register but EAX.

enum class Ret : uint8_t { None, Int, Bool, Float };
struct StandIn {
    uint32_t address;
    uint8_t pop;      // bytes of arguments the callee pops (RET n)
    uint8_t args;     // stack arguments to record
    Ret ret;
    bool thiscall;
    uint8_t byteArgs; // bit i: argument i is a byte (only its low 8 bits mean anything)
    int8_t inArg;     // an argument pointing at `words` words to record, or -1
    int8_t outArg;    // an argument pointing at `words` words to fill, or -1
    uint8_t words;
    const void* also = nullptr; // our rewrite of the callee, called directly by ours: stood in the same way
};

std::vector<StandIn> g_standIns;
std::vector<uint32_t> g_calls;
uint32_t g_rng = 1;
uint8_t* g_arena;
uint32_t g_arenaSize;
uint32_t g_stubEax, g_stubPop, g_stubReturn, g_stubIsFloat;
float g_stubFloat;

uint32_t Next()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
float PickFloat()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 30.0f, 60.0f, 89.0f, 200.0f, 0.001f, -0.25f, 7.0f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}
int PickInt()
{
    static const int values[] = {-100, 0, 0, 50, 150, 300, 1000, 1, 500, 250};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}
// A random float in [-range, range], a few bits only (so sums and products stay exact-ish but varied).
float RandomFloat(float range) { return (static_cast<int>(Next() % 2001) - 1000) * (range / 1000.0f); }

uint32_t Relative(uint32_t word, uint32_t stackLow)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_arena);
    if (word >= base && word < base + g_arenaSize)
        return 0xA0000000u + (word - base);
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= stackLow && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x5AC00000u;
    return word;
}

extern "C" void __cdecl RecordStandInCall(uint32_t* frame)
{
    // frame: EBX ECX EDX ESI EDI EBP, the stand-in's index, the return address, the arguments.
    const StandIn& s = g_standIns[frame[6]];
    uint32_t* args = frame + 8;
    uint32_t stackLow = reinterpret_cast<uint32_t>(frame);
    g_calls.push_back(s.address);
    if (s.thiscall)
        g_calls.push_back(Relative(frame[1], stackLow));
    for (int i = 0; i < s.args; ++i)
        g_calls.push_back(Relative((s.byteArgs & (1 << i)) ? args[i] & 0xFF : args[i], stackLow));
    if (s.inArg >= 0) {
        const uint32_t* data = reinterpret_cast<const uint32_t*>(args[s.inArg]);
        for (int i = 0; i < s.words; ++i)
            g_calls.push_back(Relative(data[i], stackLow));
    }
    if (s.outArg >= 0) {
        float* data = reinterpret_cast<float*>(args[s.outArg]);
        for (int i = 0; i < s.words; ++i)
            data[i] = RandomFloat(4.0f);
    }
    g_stubIsFloat = 0;
    switch (s.ret) {
    case Ret::None:
        g_stubEax = 0xEEEEEEEE;
        break;
    case Ret::Int:
        g_stubEax = static_cast<uint32_t>(PickInt());
        g_calls.push_back(g_stubEax);
        break;
    case Ret::Bool:
        g_stubEax = Next() & 1;
        g_calls.push_back(g_stubEax);
        break;
    case Ret::Float:
        g_stubFloat = PickFloat();
        g_stubIsFloat = 1;
        g_stubEax = 0xEEEEEEEE;
        g_calls.push_back(*reinterpret_cast<uint32_t*>(&g_stubFloat));
        break;
    }
    g_stubPop = s.pop;
}

__declspec(naked) void StandInEntry()
{
    __asm {
        push ebp
        push edi
        push esi
        push edx
        push ecx
        push ebx
        mov eax, esp
        push eax
        call RecordStandInCall
        add esp, 4
        pop ebx
        pop ecx
        pop edx
        pop esi
        pop edi
        pop ebp
        mov eax, [esp + 4]
        mov g_stubReturn, eax
        mov eax, g_stubPop
        lea esp, [esp + eax + 8]
        mov eax, g_stubEax
        cmp g_stubIsFloat, 0
        je done
        fld g_stubFloat
    done:
        jmp g_stubReturn
    }
}

// The stand-ins written over their callees while this exists.
struct StandIns {
    uint8_t* thunks;
    std::vector<CodePatch*> patches;
    explicit StandIns(const std::vector<StandIn>& list)
    {
        g_standIns = list;
        thunks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        for (size_t i = 0; i < list.size(); ++i) {
            uint8_t* thunk = thunks + i * 10;
            thunk[0] = 0x68; // push i
            uint32_t index = static_cast<uint32_t>(i);
            std::memcpy(thunk + 1, &index, 4);
            thunk[5] = 0xE9; // jmp StandInEntry
            int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StandInEntry) -
                                                    (reinterpret_cast<uintptr_t>(thunk) + 10));
            std::memcpy(thunk + 6, &relative, 4);
            patches.push_back(new CodePatch(list[i].address, JumpTo(list[i].address, thunk)));
            if (list[i].also) {
                uint32_t ours = reinterpret_cast<uint32_t>(list[i].also);
                patches.push_back(new CodePatch(ours, JumpTo(ours, thunk)));
            }
        }
        FlushInstructionCache(GetCurrentProcess(), thunks, 0x1000);
    }
    ~StandIns()
    {
        for (CodePatch* p : patches)
            delete p;
        VirtualFree(thunks, 0, MEM_RELEASE);
    }
};

// Ours and the original use different (but separately tested) vector code; the original is pointed at ours
// where results aren't bit-identical (x87 vs SSE), so this compares the code under test only.
struct OurVectorCode {
    CodePatch rotate{0x004060F0, JumpTo(0x004060F0, CodeAddress(&Mth::Vector::RotateToPlane))};
};

// A stand-in symbol table (for Script::GetInteger / GetFloat) while this exists.
struct StandInSymbols {
    Script::CSymbolTableEntry** saved;
    Script::CSymbolTableEntry* table[0x1000] = {};
    std::vector<Script::CSymbolTableEntry*> entries;
    StandInSymbols() : saved(GameVariable<Script::CSymbolTableEntry**>(0x006F3624))
    {
        GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
    }
    ~StandInSymbols()
    {
        GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = saved;
        for (Script::CSymbolTableEntry* e : entries)
            delete e;
    }
    Script::CSymbolTableEntry* Add(uint32_t name)
    {
        auto* e = new Script::CSymbolTableEntry();
        std::memset(e, 0, sizeof(*e));
        e->name = name;
        e->next = table[name & 0xFFF];
        table[name & 0xFFF] = e;
        entries.push_back(e);
        return e;
    }
};

// PartyMod's PS2-controls bytes (its patchPs2Buttons) in the core physics functions, while `on`.
struct PartyModCorePhysicsBytes {
    std::vector<CodePatch*> patches;
    explicit PartyModCorePhysicsBytes(bool on)
    {
        if (!on)
            return;
        patches.push_back(new CodePatch(0x004FCC59, {0x75, 0x0A})); // lip
        patches.push_back(new CodePatch(0x00509E47, {0x75, 0x0A})); // ground-to-air acid drop
        patches.push_back(new CodePatch(0x0050A257, {0x75, 0x08})); // ground-to-air
    }
    ~PartyModCorePhysicsBytes()
    {
        for (CodePatch* p : patches)
            delete p;
    }
};

// The test arena: the component and everything it points at.
constexpr uint32_t kArenaSize = 0x3400;
alignas(16) uint8_t g_physicsArena[kArenaSize];
alignas(16) uint8_t g_physicsStart[kArenaSize];
constexpr uint32_t kComponent = 0x0000; // (0x1540 bytes)
constexpr uint32_t kObject = 0x1600;
constexpr uint32_t kInput = 0x1700;
constexpr uint32_t kFlags = 0x1880;
constexpr uint32_t kOther = 0x1980;
constexpr uint32_t kTrigger = 0x19C0;
constexpr uint32_t kTriggerState = 0x19E0;
constexpr uint32_t kSkater = 0x1A00;    // (+0x13B0)
constexpr uint32_t kMisc1 = 0x2E00;     // (for +0x151C)
constexpr uint32_t kMisc2 = 0x2E80;     // (for +0x152C)
constexpr uint32_t kMisc3 = 0x2F00;     // (what +0x152C's +0x18 points at)

template <typename T>
T& ArenaAt(uint32_t offset)
{
    return *reinterpret_cast<T*>(g_physicsArena + offset);
}

// Fill the arena: floats everywhere, then the component's pointers.
void FillArena()
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        ArenaAt<float>(i) = RandomFloat(3.0f);
    auto pointer = [](uint32_t at, uint32_t to) { ArenaAt<uint8_t*>(at) = g_physicsArena + to; };
    pointer(kComponent + 0x0C, kObject);
    pointer(kComponent + 0x150C, kInput);
    pointer(kComponent + 0x1510, kTrigger);
    pointer(kComponent + 0x1514, kOther);
    pointer(kComponent + 0x1518, kSkater);
    pointer(kComponent + 0x151C, kMisc1);
    pointer(kComponent + 0x1528, kFlags);
    pointer(kComponent + 0x152C, kMisc2);
    pointer(kComponent + 0x1530, kTriggerState);
    std::memset(g_physicsArena + kInput, 0, 0x140);
}
uint8_t RandomBool(uint32_t percent) { return Next() % 100 < percent ? 1 : 0; }

// Run the original or ours over the arena as filled (reset to the same start, same stand-in values) and
// record everything: the result, the calls, the whole arena (pointers made relative).
template <typename Call>
std::vector<uint32_t> RunOnArena(uint32_t seed, Call call)
{
    std::memcpy(g_physicsArena, g_physicsStart, kArenaSize);
    g_rng = seed;
    g_calls.clear();
    uint32_t result = call(g_physicsArena + kComponent);
    std::vector<uint32_t> trace = {result};
    trace.insert(trace.end(), g_calls.begin(), g_calls.end());
    uint32_t stackLow = reinterpret_cast<uint32_t>(&trace);
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        trace.push_back(Relative(ArenaAt<uint32_t>(i), stackLow));
    return trace;
}

bool Compare(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what, int variant)
{
    if (expected == actual)
        return true;
    Log("  %s: variant %d differs (trace sizes: original %u ours %u)", what, variant,
        static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 6; ++i) {
        if (expected[i] != actual[i]) {
            Log("    entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
    return false;
}

// LipPushOff 0x004FCBA0: the facing (flat or not), each button held or not (and the timers' values), PartyMod's
// PS2 controls off and on.
void TestLipPushOff()
{
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    g_arena = g_physicsArena;
    g_arenaSize = kArenaSize;
    StandIns standIns({
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0}, // PhysicsFloat
        {0x00580440, 0, 3, Ret::Int, false, 0, -1, -1, 0},   // PhysicsInteger
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0},    // TimeSincePress
    });
    bool same = true;
    for (int variant = 0; variant < 3000 && same; ++variant) {
        int ps2 = variant & 1;
        g_rng = 0x9E3779B9u * (variant + 1);
        FillArena();
        float* facing = &ArenaAt<float>(kObject + 0x8C);
        if (variant % 7 == 0)
            facing[0] = facing[2] = 0.0f; // (straight up or down: not normalised)
        else if (variant % 7 == 1)
            facing[0] = 0.0f;
        for (uint32_t button : {0x20u, 0x40u, 0x60u, 0xC0u, 0x120u})
            ArenaAt<uint8_t>(kInput + button) = RandomBool(40);
        ArenaAt<uint8_t>(kInput + 0x80) = RandomBool(10);
        std::memcpy(g_physicsStart, g_physicsArena, kArenaSize);
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected, actual;
        {
            PartyModCorePhysicsBytes bytes(ps2 != 0);
            expected = RunOnArena(seed, [](uint8_t* component) {
                return static_cast<uint32_t>(Original<int(__thiscall*)(void*, void*, void*)>(0x004FCBA0)(
                    component, reinterpret_cast<void*>(0x1234), reinterpret_cast<void*>(0x5678)));
            });
        }
        Sio::TheInputSettings().ps2Controls = static_cast<uint8_t>(ps2);
        actual = RunOnArena(seed, [](uint8_t* component) {
            return static_cast<uint32_t>(reinterpret_cast<Skater::CorePhysicsComponent*>(component)->LipPushOff(
                reinterpret_cast<Script::CStruct*>(0x1234), reinterpret_cast<Script::CScript*>(0x5678)));
        });
        Sio::TheInputSettings().ps2Controls = savedSettings;
        same = Compare(expected, actual, "lip push-off", variant);
    }
    Sio::TheInputSettings().ps2Controls = savedSettings;
    Check(same, "skater: LipPushOff (PS2 controls off and on)");
}

// The stand-ins for CheckForGround's callees.
std::vector<StandIn> GroundStandIns()
{
    return {
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0},  // PhysicsFloat
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0},     // TimeSincePress
        {0x0046B900, 0, 0, Ret::Int, true, 0, -1, -1, 0},     // TimeSinceRelease
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0},    // the clock
        {0x005003A0, 8, 2, Ret::Bool, true, 0, -1, -1, 0},    // the feeler
        {0x004FE860, 16, 4, Ret::None, true, 0, -1, -1, 0},   // (a vector by value)
        {0x005196A0, 4, 1, Ret::None, true, 1, -1, -1, 0},    // (a byte)
        {0x004FEA50, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x005021D0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x004FEBA0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00434BF0, 8, 2, Ret::None, true, 0, -1, -1, 0},    // a trigger check
        {0x005232A0, 4, 1, Ret::None, true, 1, -1, -1, 0},    // (a bool)
        {0x0043A930, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x004FDED0, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x00460A20, 12, 3, Ret::None, true, 0, -1, -1, 0},
        {0x004609F0, 8, 2, Ret::None, true, 0, -1, -1, 0},
        {0x00507120, 4, 1, Ret::None, true, 1, -1, -1, 0, CodeAddress(&Skater::CorePhysicsComponent::BreakVert)},
        {0x00502240, 0x14, 5, Ret::Bool, true, 0, -1, 4, 10}, // the acid drop search (fills its last argument)
        {0x00502F20, 4, 1, Ret::None, true, 0, 0, -1, 10},    // (reads it)
    };
}

// CheckForGround 0x00509BD0: hit or not, ground or not, the facing's angles, the flags, buttons and triggers,
// PartyMod's PS2 controls off and on.
void TestCheckForGround()
{
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    g_arena = g_physicsArena;
    g_arenaSize = kArenaSize;
    StandIns standIns(GroundStandIns());
    OurVectorCode vectors;
    StandInSymbols symbols;
    Script::CSymbolTableEntry* wait = symbols.Add(0x6BB5B751);
    wait->type = Script::ESYMBOLTYPE_INTEGER;
    bool same = true;
    int deep = 0;
    for (int variant = 0; variant < 6000 && same; ++variant) {
        int ps2 = variant & 1;
        g_rng = 0x9E3779B9u * (variant + 1);
        FillArena();
        wait->integer = PickInt();
        uint8_t* c = g_physicsArena + kComponent;
        c[0x228] = RandomBool(70);
        c[0x229] = static_cast<uint8_t>(Next() % 3);
        c[0xFA] = static_cast<uint8_t>(Next());
        c[0x1AA] = static_cast<uint8_t>(Next());
        ArenaAt<int>(kComponent + 0x1AC) = static_cast<int>(Next() % 2);
        ArenaAt<int>(kComponent + 0xFC) = static_cast<int>(Next() % 2);
        ArenaAt<int>(kFlags + 0x18) = RandomBool(30) ? 1 : 0;
        for (uint32_t flag : {0x30u, 0x40u, 0x48u, 0x98u})
            ArenaAt<uint8_t>(kFlags + flag) = static_cast<uint8_t>(Next() % 3);
        ArenaAt<uint8_t>(kTriggerState + 0x26) = RandomBool(25);
        ArenaAt<uint8_t>(kSkater + 0x13B0) = RandomBool(50);
        for (uint32_t button : {0x20u, 0xC0u, 0x120u})
            ArenaAt<uint8_t>(kInput + button) = RandomBool(50);
        // The hit point near the skater (sometimes on the up axis), normals and facings of varied angles.
        float* position = &ArenaAt<float>(kObject + 0x4C);
        float* point = &ArenaAt<float>(kComponent + 0x120);
        for (int i = 0; i < 4; ++i)
            point[i] = position[i] + RandomFloat(Next() % 2 ? 0.5f : 4.0f);
        if (variant % 5 == 0) {
            float* up = &ArenaAt<float>(kObject + 0x7C);
            up[0] = 0.0f, up[1] = 1.0f, up[2] = 0.0f;
        }
        float* old = &ArenaAt<float>(kObject + 0x5C);
        for (int i = 0; i < 4; ++i)
            old[i] = position[i] + RandomFloat(Next() % 2 ? 0.25f : 3.0f);
        std::memcpy(g_physicsStart, g_physicsArena, kArenaSize);
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected, actual;
        {
            PartyModCorePhysicsBytes bytes(ps2 != 0);
            expected = RunOnArena(seed, [](uint8_t* component) {
                Original<void(__thiscall*)(void*)>(0x00509BD0)(component);
                return 0u;
            });
        }
        Sio::TheInputSettings().ps2Controls = static_cast<uint8_t>(ps2);
        actual = RunOnArena(seed, [](uint8_t* component) {
            reinterpret_cast<Skater::CorePhysicsComponent*>(component)->CheckForGround();
            return 0u;
        });
        Sio::TheInputSettings().ps2Controls = savedSettings;
        for (size_t i = 1; i + 2 < actual.size(); ++i) {
            if (actual[i] == 0x005196A0 && actual[i + 1] == 0xA0000000u + kFlags) {
                ++deep; // (landed)
                break;
            }
        }
        same = Compare(expected, actual, "check for ground", variant);
    }
    Sio::TheInputSettings().ps2Controls = savedSettings;
    if (deep < 300)
        Log("  check for ground: only %d variants landed", deep);
    Check(same && deep >= 300, "skater: CheckForGround (PS2 controls off and on)");
}

// The air update's vector and matrix code: the original pointed at ours (see OurVectorCode).
struct OurAirVectorCode {
    CodePatch rotate{0x004060F0, JumpTo(0x004060F0, CodeAddress(&Mth::Vector::RotateToPlane))};
    CodePatch project{0x00406080, JumpTo(0x00406080, CodeAddress(&Mth::Vector::ProjectToPlane))};
    CodePatch direction{0x004061A0, JumpTo(0x004061A0, CodeAddress(&Mth::Vector::SetDirection))};
    CodePatch normalize{0x004023F0, JumpTo(0x004023F0, CodeAddress(&Mth::Vector::Normalize))};
    CodePatch length{0x004023C0, JumpTo(0x004023C0, CodeAddress(&Mth::Vector::Length))};
    CodePatch lengthSqr{0x004023A0, JumpTo(0x004023A0, CodeAddress(&Mth::Vector::LengthSqr))};
    CodePatch ortho{0x00405170, JumpTo(0x00405170, CodeAddress(&Mth::Matrix::OrthoNormalizeAbout))};
};

// PartyMod's PS2-controls bytes in the air update, while `on`.
struct PartyModAirBytes {
    std::vector<CodePatch*> patches;
    explicit PartyModAirBytes(bool on)
    {
        if (!on)
            return;
        patches.push_back(new CodePatch(0x0050CAC4, {0x75, 0x0A})); // air recovery
        patches.push_back(new CodePatch(0x0050CF89, {0x75, 0x14})); // in air
        patches.push_back(new CodePatch(0x0050DA64, {0x75, 0x0A})); // acid drop
    }
    ~PartyModAirBytes()
    {
        for (CodePatch* p : patches)
            delete p;
    }
};

std::vector<StandIn> AirStandIns()
{
    return {
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0},  // PhysicsFloat
        {0x004FF470, 0, 0, Ret::Float, true, 0, -1, -1, 0},   // (gravity)
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, -1, -1, 0},    // the clock
        {0x0046B920, 0, 0, Ret::Int, true, 0, -1, -1, 0},     // TimeSincePress
        {0x0046B900, 0, 0, Ret::Int, true, 0, -1, -1, 0},     // TimeSinceRelease
        {0x00504910, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x004FFB90, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00504060, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Skater::CorePhysicsComponent::Spin)},
        {0x00504A60, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00504B40, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00504DF0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x004FE730, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00507500, 12, 3, Ret::Bool, true, 0, -1, -1, 0},
        {0x0042BBF0, 4, 1, Ret::None, true, 0, -1, -1, 0},     // (a vector's length, it seems)
        {0x00427C70, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00416F00, 4, 1, Ret::Bool, true, 0, -1, -1, 0},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, -1, -1, 0},     // the feeler
        {0x00507120, 4, 1, Ret::None, true, 1, -1, -1, 0, CodeAddress(&Skater::CorePhysicsComponent::BreakVert)},
        {0x004FE860, 16, 4, Ret::None, true, 0, -1, -1, 0},
        {0x004FEA50, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00508780, 4, 1, Ret::Bool, true, 0, 0, -1, 4},
        {0x004FFDB0, 4, 1, Ret::Bool, true, 0, 0, -1, 4},
        {0x004FC400, 0, 0, Ret::Int, true, 0, -1, -1, 0},
        {0x004FDED0, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x005021D0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x004FC470, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x00580790, 4, 1, Ret::Float, true, 0, -1, -1, 0},
        {0x00518CF0, 8, 2, Ret::None, true, 0, -1, -1, 0},
        {0x005232A0, 4, 1, Ret::None, true, 1, -1, -1, 0},
        {0x00506BA0, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x00434BF0, 8, 2, Ret::None, true, 0, -1, -1, 0},
        {0x004FF590, 4, 1, Ret::None, true, 0, 0, 0, 4},       // (reads and changes a vector)
        {0x004FC370, 4, 1, Ret::None, true, 0, -1, -1, 0},
        {0x005009C0, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x004609F0, 8, 2, Ret::None, true, 0, -1, -1, 0},
        {0x0050C930, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00504EE0, 0, 0, Ret::Bool, true, 0, -1, -1, 0},
        {0x004FFB10, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00503710, 0, 0, Ret::None, true, 0, -1, -1, 0},
        {0x00420250, 0, 1, Ret::Int, false, 0, -1, -1, 0},
        {0x00502240, 0x14, 5, Ret::Bool, true, 1, -1, 4, 10},  // the acid drop search (a bool, fills its last)
        {0x00502F20, 4, 1, Ret::None, true, 0, 0, -1, 10},
    };
}

// Whether `trace` has a call to `address` whose first argument (after `this`) is `argument`.
bool Called(const std::vector<uint32_t>& trace, uint32_t address, uint32_t argument)
{
    for (size_t i = 1; i + 2 < trace.size(); ++i) {
        if (trace[i] == address && trace[i + 2] == argument)
            return true;
    }
    return false;
}

// DoAirPhysics 0x0050CA00: the flags, the ride, buttons and triggers, the wall ride and its feelers, landing
// (skatable or not, from a wall ride or not) or not, PartyMod's PS2 controls off and on.
void TestDoAirPhysics()
{
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    g_arena = g_physicsArena;
    g_arenaSize = kArenaSize;
    StandIns standIns(AirStandIns());
    OurAirVectorCode vectors;
    StandInSymbols symbols;
    Script::CSymbolTableEntry* wait = symbols.Add(0x6BB5B751);
    wait->type = Script::ESYMBOLTYPE_INTEGER;
    Script::CSymbolTableEntry* wallTime = symbols.Add(0x26495947);
    wallTime->type = Script::ESYMBOLTYPE_INTEGER;
    Script::CSymbolTableEntry* wallPush = symbols.Add(0x78384871);
    wallPush->type = Script::ESYMBOLTYPE_FLOAT;
    Script::CSymbolTableEntry* least = symbols.Add(0x59484878);
    least->type = Script::ESYMBOLTYPE_FLOAT;
    int savedOffset = GameVariable<int>(0x00696D60); // (the clock's offset: 0, so times can meet exactly)
    GameVariable<int>(0x00696D60) = 0;
    bool same = true;
    int landed = 0, wallLanded = 0, wallFound = 0, pushedOff = 0, dropped = 0;
    for (int variant = 0; variant < 12000 && same; ++variant) {
        int ps2 = variant & 1;
        g_rng = 0x9E3779B9u * (variant + 1);
        FillArena();
        wait->integer = PickInt();
        wallTime->integer = PickInt();
        wallPush->floatValue = RandomFloat(4.0f);
        least->floatValue = RandomFloat(3.0f);
        uint8_t* c = g_physicsArena + kComponent;
        ArenaAt<float>(kComponent + 0x8C) = (Next() % 2) ? 1.0f / 60.0f : RandomFloat(0.1f);
        ArenaAt<int>(kComponent + 0x14CC) = static_cast<int>(Next() % 3);
        for (uint32_t b : {0x14C0u, 0x2D0u, 0x2DBu, 0x2ECu, 0x373u, 0x374u})
            c[b] = RandomBool(30);
        c[0x2ED] = RandomBool(50);
        c[0x228] = RandomBool(60);
        c[0xFA] = static_cast<uint8_t>(Next());
        if (Next() % 2)
            ArenaAt<float>(kComponent + 0x320) = 0.0f;
        ArenaAt<float>(kComponent + 0x298) = RandomFloat(1.5f);
        ArenaAt<uint32_t>(kComponent + 0x380) = Next() % 4;
        ArenaAt<uint32_t>(kComponent + 0x4EC) = Next() % 4;
        for (uint32_t f : {0x30u, 0x38u, 0x40u, 0x48u, 0x70u, 0x78u, 0x80u, 0xA8u, 0xB0u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(f == 0xB0 ? 20 : 40);
        ArenaAt<uint8_t>(kFlags + 0xE1) = static_cast<uint8_t>(Next());
        static const int states[] = {0, 0, 0, 1, 2, 5};
        ArenaAt<int>(kFlags + 0x18) = states[Next() % 6];
        ArenaAt<int>(kFlags + 0x4C) = (Next() % 2) ? -PickInt() : static_cast<int>(Next() % 2000) - 1000;
        ArenaAt<uint8_t>(kMisc1 + 0x28) = RandomBool(20);
        ArenaAt<uint8_t>(kMisc1 + 0x50) = RandomBool(20);
        uint32_t ride = Next() % 3;
        ArenaAt<uint8_t*>(kMisc2 + 0x18) = ride ? g_physicsArena + kMisc3 : nullptr;
        ArenaAt<uint32_t>(kMisc3) = ride == 2 ? 1 : 0;
        ArenaAt<uint8_t>(kTriggerState + 0x26) = RandomBool(20);
        ArenaAt<uint8_t>(kSkater + 0x13B0) = RandomBool(50);
        for (uint32_t button : {0x20u, 0x40u, 0xC0u, 0x120u})
            ArenaAt<uint8_t>(kInput + button) = RandomBool(50);
        float* velocity = &ArenaAt<float>(kObject + 0xAC);
        if (variant % 6 == 0)
            velocity[0] = velocity[2] = 0.0f;
        else if (variant % 6 == 3)
            velocity[0] = 0.0f;
        if (variant % 9 == 0)
            velocity[1] = 0.0f;
        std::memcpy(g_physicsStart, g_physicsArena, kArenaSize);
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected, actual;
        {
            PartyModAirBytes bytes(ps2 != 0);
            expected = RunOnArena(seed, [](uint8_t* component) {
                Original<void(__thiscall*)(void*)>(0x0050CA00)(component);
                return 0u;
            });
        }
        Sio::TheInputSettings().ps2Controls = static_cast<uint8_t>(ps2);
        actual = RunOnArena(seed, [](uint8_t* component) {
            reinterpret_cast<Skater::CorePhysicsComponent*>(component)->DoAirPhysics();
            return 0u;
        });
        Sio::TheInputSettings().ps2Controls = savedSettings;
        landed += Called(actual, 0x004FDED0, 0);
        wallLanded += Called(actual, 0x004FDED0, 0) && Called(actual, 0x004FF590, 0x5AC00000u);
        for (size_t i = 1; i + 1 < actual.size(); ++i) {
            if (actual[i] == 0x004FE860) {
                ++wallFound;
                break;
            }
        }
        for (size_t i = 1; i + 1 < actual.size(); ++i) {
            if (actual[i] == 0x00580400 && actual[i + 1] == 0x23410C14) { // (cdecl: no `this` in the trace)
                ++pushedOff;
                break;
            }
        }
        dropped += Called(actual, 0x00502F20, 0x5AC00000u);
        same = Compare(expected, actual, "air physics", variant);
    }
    Sio::TheInputSettings().ps2Controls = savedSettings;
    GameVariable<int>(0x00696D60) = savedOffset;
    bool covered = landed >= 300 && wallLanded >= 50 && wallFound >= 100 && pushedOff >= 100 && dropped >= 50;
    if (!covered)
        Log("  air physics coverage: landed %d (from a wall ride %d), wall hits %d, pushed off %d, acid drops %d",
            landed, wallLanded, wallFound, pushedOff, dropped);
    Check(same && covered, "skater: DoAirPhysics (PS2 controls off and on)");
}

} // namespace

SELFTEST(Early, TestLipPushOff);
SELFTEST(Early, TestCheckForGround);
SELFTEST(Early, TestDoAirPhysics);
