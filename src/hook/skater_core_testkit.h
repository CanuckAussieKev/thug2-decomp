#pragma once

// Shared pieces of the self-tests for the skater core physics component's large functions (selftests_skater_*.cpp):
// original vs ours on one stand-in component (an arena holding the component and everything it points at), every
// callee into game code - and our rewrite of it, where ours calls that directly - replaced by a recording stand-in
// for the length of the test. A stand-in records the call (the callee, `this`, the stack arguments: pointers into the
// arena kept as they are - both runs use the same arena -, pointers into the stack marked), may write outputs, and
// returns a value from a seeded sequence (the same in both runs as long as the calls are the same); every register
// but EAX comes back as it was, and a float result is left on the x87 stack. A run's trace is its calls, then the
// whole arena.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace SkaterCoreKit {

// Our own functions may be reached through an incremental-linking jump: the function itself.
inline uint32_t Resolve(const void* code)
{
    auto* p = static_cast<const uint8_t*>(code);
    while (p[0] == 0xE9) {
        int32_t relative;
        std::memcpy(&relative, p + 1, 4);
        p += 5 + relative;
    }
    return reinterpret_cast<uint32_t>(p);
}
template <typename F>
uint32_t Ours(F function)
{
    return Resolve(CodeAddress(function));
}

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
inline std::vector<uint8_t> JumpTo(uint32_t from, uint32_t to)
{
    std::vector<uint8_t> bytes(5);
    bytes[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(to - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}

// --- the arena ---------------------------------------------------------------------------------------------
constexpr uint32_t kArenaSize = 0x4000;
alignas(16) inline uint8_t g_arena[kArenaSize];
alignas(16) inline uint8_t g_arenaStart[kArenaSize];
// Where things are in it.
constexpr uint32_t kComponent = 0x0000; // the physics component (0x1540 bytes)
constexpr uint32_t kObject = 0x1600;    // the skater's object (+0x4C position, +0x6C matrix, +0xAC velocity)
constexpr uint32_t kInput = 0x1700;     // the pad (0x1C0 bytes)
constexpr uint32_t kFlags = 0x18C0;     // the timed flags (+0x1528)
constexpr uint32_t kOther = 0x19C0;     // +0x1514 (the sound component)
constexpr uint32_t kTrigger = 0x1A00;   // +0x1510
constexpr uint32_t kTriggerState = 0x1A40; // +0x1530
constexpr uint32_t kSkater = 0x1A80;    // +0x1518 (+0x13B0)
constexpr uint32_t kMisc1 = 0x2E80;     // +0x151C
constexpr uint32_t kRide = 0x2F00;      // +0x152C (+0x18: the ride tracker)
constexpr uint32_t kTracker = 0x2F80;   // a ride tracker (0xB0 bytes)
constexpr uint32_t kFree = 0x3040;      // for the tests' own things (to the end)

template <typename T>
T& ArenaAt(uint32_t offset)
{
    return *reinterpret_cast<T*>(g_arena + offset);
}
inline uint8_t* ArenaPtr(uint32_t offset) { return g_arena + offset; }

// --- the seeded sequence --------------------------------------------------------------------------------------
inline uint32_t g_rng = 1;
inline uint32_t Next()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
inline float PickFloat()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 30.0f, 60.0f, 89.0f, 200.0f, 0.001f, -0.25f, 7.0f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}
inline int PickInt()
{
    static const int values[] = {-100, 0, 0, 50, 150, 300, 1000, 1, 500, 250};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}
// A random float in [-range, range] (few bits: sums and products stay varied but tame).
inline float RandomFloat(float range) { return (static_cast<int>(Next() % 2001) - 1000) * (range / 1000.0f); }
inline uint8_t RandomBool(uint32_t percent) { return Next() % 100 < percent ? 1 : 0; }

// Words that may be pointers: into the stack (the functions' locals differ between the runs) one value.
inline uint32_t Relative(uint32_t word, uint32_t stackLow)
{
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= stackLow - 0x10000 && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x5AC00000u;
    return word;
}

// --- the stand-ins ---------------------------------------------------------------------------------------------
enum class Ret : uint8_t { None, Int, Bool, Float };

// What a stand-in sees: the registers on entry (but EAX), its index, the return address, the stack arguments.
struct Frame {
    uint32_t ebx, ecx, edx, esi, edi, ebp;
    uint32_t index;
    uint32_t ret;
    uint32_t args[16];
};
// What it returns (set from the sequence first; an action may change it).
struct Result {
    uint32_t eax;
    float st0;
};

struct StandIn {
    uint32_t address;
    uint8_t pop;      // bytes of arguments the callee pops (RET n)
    uint8_t args;     // stack arguments to record
    Ret ret;
    bool thiscall;
    uint8_t byteArgs = 0; // bit i: argument i is a byte (only its low 8 bits mean anything)
    std::vector<uint32_t> ours = {};                    // our rewrites, stood in the same way
    std::function<void(Frame&, Result&)> action = {}; // outputs, a chosen result (record with Record)
};

inline std::vector<StandIn> g_standIns;
inline std::vector<uint32_t> g_calls;
inline uint32_t g_stubEax, g_stubPop, g_stubReturn, g_stubIsFloat;
inline float g_stubFloat;
inline uint32_t g_stackLow;

inline void Record(uint32_t value) { g_calls.push_back(value); }
inline void RecordFloat(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    g_calls.push_back(bits);
}
inline uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

extern "C" inline void __cdecl SkaterKitRecordCall(Frame* frame)
{
    const StandIn& s = g_standIns[frame->index];
    uint32_t stackLow = reinterpret_cast<uint32_t>(frame);
    g_stackLow = stackLow;
    g_calls.push_back(s.address);
    if (s.thiscall)
        g_calls.push_back(Relative(frame->ecx, stackLow));
    for (int i = 0; i < s.args; ++i)
        g_calls.push_back(Relative((s.byteArgs & (1 << i)) ? frame->args[i] & 0xFF : frame->args[i], stackLow));
    Result result = {0xEEEEEEEE, 0.0f};
    g_stubIsFloat = 0;
    switch (s.ret) {
    case Ret::None:
        break;
    case Ret::Int:
        result.eax = static_cast<uint32_t>(PickInt());
        break;
    case Ret::Bool:
        result.eax = Next() & 1;
        break;
    case Ret::Float:
        result.st0 = PickFloat();
        g_stubIsFloat = 1;
        break;
    }
    if (s.action)
        s.action(*frame, result);
    if (s.ret == Ret::Float)
        RecordFloat(result.st0);
    else if (s.ret != Ret::None)
        g_calls.push_back(Relative(result.eax, stackLow));
    g_stubEax = result.eax;
    g_stubFloat = result.st0;
    g_stubPop = s.pop;
}

__declspec(naked) inline void SkaterKitEntry()
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
        call SkaterKitRecordCall
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
        thunks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        for (size_t i = 0; i < list.size(); ++i) {
            uint8_t* thunk = thunks + i * 10;
            thunk[0] = 0x68; // push i
            uint32_t index = static_cast<uint32_t>(i);
            std::memcpy(thunk + 1, &index, 4);
            thunk[5] = 0xE9; // jmp SkaterKitEntry
            int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&SkaterKitEntry) -
                                                    (reinterpret_cast<uintptr_t>(thunk) + 10));
            std::memcpy(thunk + 6, &relative, 4);
            uint32_t target = reinterpret_cast<uint32_t>(thunk);
            patches.push_back(new CodePatch(list[i].address, JumpTo(list[i].address, target)));
            for (uint32_t ours : list[i].ours)
                patches.push_back(new CodePatch(ours, JumpTo(ours, target)));
        }
        FlushInstructionCache(GetCurrentProcess(), thunks, 0x2000);
    }
    ~StandIns()
    {
        for (auto it = patches.rbegin(); it != patches.rend(); ++it)
            delete *it;
        VirtualFree(thunks, 0, MEM_RELEASE);
    }
    StandIns(const StandIns&) = delete;
    StandIns& operator=(const StandIns&) = delete;
};

// Callees the original reaches where ours runs our own (separately tested) code, not bit-identical (x87 against
// SSE): the original pointed at ours for the test.
struct UseOurs {
    std::vector<CodePatch*> patches;
    UseOurs(std::initializer_list<std::pair<uint32_t, uint32_t>> pairs)
    {
        for (auto [game, ours] : pairs)
            patches.push_back(new CodePatch(game, JumpTo(game, ours)));
    }
    ~UseOurs()
    {
        for (auto it = patches.rbegin(); it != patches.rend(); ++it)
            delete *it;
    }
    UseOurs(const UseOurs&) = delete;
    UseOurs& operator=(const UseOurs&) = delete;
};

// Fill the arena: floats everywhere, then the component's pointers; the pad's buttons up.
inline void FillArena()
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        ArenaAt<float>(i) = RandomFloat(3.0f);
    auto pointer = [](uint32_t at, uint32_t to) { ArenaAt<uint8_t*>(at) = g_arena + to; };
    pointer(kComponent + 0x0C, kObject);
    pointer(kComponent + 0x150C, kInput);
    pointer(kComponent + 0x1510, kTrigger);
    pointer(kComponent + 0x1514, kOther);
    pointer(kComponent + 0x1518, kSkater);
    pointer(kComponent + 0x151C, kMisc1);
    pointer(kComponent + 0x1528, kFlags);
    pointer(kComponent + 0x152C, kRide);
    pointer(kComponent + 0x1530, kTriggerState);
    std::memset(g_arena + kInput, 0, 0x1C0);
}

// Run the original or ours over the arena as saved in g_arenaStart (same stand-in values) and record everything:
// the result, the calls, the whole arena.
template <typename Call>
std::vector<uint32_t> RunOnArena(uint32_t seed, Call call)
{
    std::memcpy(g_arena, g_arenaStart, kArenaSize);
    g_rng = seed;
    g_calls.clear();
    uint32_t result = call(g_arena + kComponent);
    std::vector<uint32_t> trace = {result};
    trace.insert(trace.end(), g_calls.begin(), g_calls.end());
    uint32_t stackLow = reinterpret_cast<uint32_t>(&trace);
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        trace.push_back(Relative(ArenaAt<uint32_t>(i), stackLow));
    return trace;
}
inline void SaveStart() { std::memcpy(g_arenaStart, g_arena, kArenaSize); }

inline bool Compare(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what,
                    int variant)
{
    if (expected == actual)
        return true;
    Log("  %s: variant %d differs (trace sizes: original %u ours %u)", what, variant,
        static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            if (!shown) {
                for (size_t k = i > 40 ? i - 40 : 0; k < i; ++k)
                    Log("    entry %u: %08X", static_cast<unsigned>(k), expected[k]);
            }
            Log("    entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
    return false;
}

// Whether `trace` has a call to `address` (anywhere in its calls).
inline bool Called(const std::vector<uint32_t>& trace, uint32_t address)
{
    for (size_t i = 1; i < trace.size() - kArenaSize / 4; ++i) {
        if (trace[i] == address)
            return true;
    }
    return false;
}
// ... whose first recorded word after the address (`this`, or the first argument of a cdecl one) is `value`.
inline bool CalledWith(const std::vector<uint32_t>& trace, uint32_t address, uint32_t value)
{
    for (size_t i = 1; i + 1 < trace.size() - kArenaSize / 4; ++i) {
        if (trace[i] == address && trace[i + 1] == value)
            return true;
    }
    return false;
}

// The clock's offset (0x00696D60) at 0 for a test, so times can meet exactly.
struct ClockOffsetZero {
    int saved = GameVariable<int>(0x00696D60);
    ClockOffsetZero() { GameVariable<int>(0x00696D60) = 0; }
    ~ClockOffsetZero() { GameVariable<int>(0x00696D60) = saved; }
};

} // namespace SkaterCoreKit
