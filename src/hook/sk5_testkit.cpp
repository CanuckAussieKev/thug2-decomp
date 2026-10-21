// The skater / camera test kit (sk5_testkit.h).

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "hook/log.h"
#include "hook/sk5_testkit.h"

namespace Sk5 {

namespace {

constexpr uint32_t kArenaSize = 0x8000;
alignas(16) uint8_t g_arena[kArenaSize];
alignas(16) uint8_t g_start[kArenaSize];
uint32_t g_rng = 1;
std::vector<uint32_t> g_trace;

struct CodeBytes {
    uint8_t* at;
    std::vector<uint8_t> saved;
};

struct StandIn {
    uint16_t pop;
    Handler handler;
};

Kit* g_kit = nullptr;
std::vector<StandIn> g_standIns;
std::vector<CodeBytes> g_patches;
uint8_t* g_thunks = nullptr;

void WriteCode(uint8_t* at, const std::vector<uint8_t>& bytes)
{
    DWORD old;
    VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
    g_patches.push_back({at, std::vector<uint8_t>(at, at + bytes.size())});
    std::memcpy(at, bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
}

std::vector<uint8_t> JumpBytes(uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes(5, 0xE9);
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}

// Through incremental-linking jumps to the function itself.
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

uint32_t g_entryPop, g_entryEax, g_entryReturn, g_entryIsFloat;
double g_entryValue, g_entryArgument;
uint32_t g_stackLow;

extern "C" void __cdecl Sk5Dispatch(Frame* frame)
{
    StandIn& s = g_standIns[frame->index];
    g_stackLow = reinterpret_cast<uint32_t>(frame);
    Result r = s.handler(*frame);
    g_entryPop = s.pop;
    g_entryEax = r.eax;
    g_entryIsFloat = r.isFloat ? 1 : 0;
    g_entryValue = r.value;
}

__declspec(naked) void Sk5Entry()
{
    __asm {
        pushad
        mov eax, esp
        push eax
        call Sk5Dispatch
        add esp, 4
        popad
        mov eax, [esp + 4]
        mov g_entryReturn, eax
        mov eax, g_entryPop
        lea esp, [esp + eax + 8]
        mov eax, g_entryEax
        cmp g_entryIsFloat, 0
        je done
        fld g_entryValue
    done:
        jmp g_entryReturn
    }
}

// The entry for callees taking their argument in ST0: it is popped into g_entryArgument first.
__declspec(naked) void Sk5EntrySt0()
{
    __asm {
        fstp g_entryArgument
        jmp Sk5Entry
    }
}

} // namespace

double St0Argument() { return g_entryArgument; }

uint8_t* Arena() { return g_arena; }
uint32_t ArenaSize() { return kArenaSize; }
void Pointer(uint32_t at, uint32_t to) { At<uint8_t*>(at) = g_arena + to; }

uint32_t Next()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
void Seed(uint32_t seed) { g_rng = seed ? seed : 1; }
float RandomFloat(float range) { return (static_cast<int>(Next() % 2001) - 1000) * (range / 1000.0f); }
float PickFloat()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 30.0f, 60.0f, 89.0f, 200.0f, 0.001f, -0.25f, 7.0f,
                                   0.75f, 0.016666668f, 3.0f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}
int PickInt()
{
    static const int values[] = {-100, 0, 0, 50, 150, 300, 1000, 1, 500, 250, 2, 3};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}
bool Chance(uint32_t percent) { return Next() % 100 < percent; }

void FillArena()
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        At<float>(i) = RandomFloat(3.0f);
}
void KeepStart() { std::memcpy(g_start, g_arena, kArenaSize); }

uint32_t Relative(uint32_t word)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_arena);
    if (word >= base && word <= base + kArenaSize)
        return 0xA0000000u + (word - base);
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    uint32_t low = g_stackLow ? g_stackLow : reinterpret_cast<uint32_t>(&word);
    if (word >= low - 0x10000 && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x5AC00000u;
    return word;
}
std::vector<uint32_t>& Trace() { return g_trace; }
void Record(uint32_t value) { g_trace.push_back(value); }
void RecordFloat(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    g_trace.push_back(bits);
}

Kit::Kit()
{
    Cleanup();
    g_kit = this;
    g_thunks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
}

Kit::~Kit()
{
    for (auto it = g_patches.rbegin(); it != g_patches.rend(); ++it) {
        std::memcpy(it->at, it->saved.data(), it->saved.size());
        FlushInstructionCache(GetCurrentProcess(), it->at, it->saved.size());
    }
    g_patches.clear();
    g_standIns.clear();
    if (g_thunks)
        VirtualFree(g_thunks, 0, MEM_RELEASE);
    g_thunks = nullptr;
    g_kit = nullptr;
}

void* Kit::Add(std::initializer_list<uint32_t> addresses, uint16_t pop, Handler handler,
               std::initializer_list<const void*> also, bool takesSt0)
{
    uint32_t index = static_cast<uint32_t>(g_standIns.size());
    g_standIns.push_back({pop, std::move(handler)});
    uint8_t* thunk = g_thunks + index * 16;
    thunk[0] = 0x68; // push index
    std::memcpy(thunk + 1, &index, 4);
    const void* entry = takesSt0 ? reinterpret_cast<const void*>(&Sk5EntrySt0) : reinterpret_cast<const void*>(&Sk5Entry);
    std::vector<uint8_t> jump = JumpBytes(reinterpret_cast<uint32_t>(thunk + 5), reinterpret_cast<const void*>(Resolve(entry)));
    std::memcpy(thunk + 5, jump.data(), 5);
    FlushInstructionCache(GetCurrentProcess(), thunk, 16);
    for (uint32_t address : addresses)
        WriteCode(reinterpret_cast<uint8_t*>(address), JumpBytes(address, thunk));
    for (const void* ours : also) {
        uint32_t at = Resolve(ours);
        WriteCode(reinterpret_cast<uint8_t*>(at), JumpBytes(at, thunk));
    }
    return thunk;
}

void Kit::Recorder(uint32_t address, Rec rec, std::initializer_list<const void*> also)
{
    Handler handler = [address, rec](Frame& f) {
        Record(address);
        if (rec.thiscall)
            Record(Relative(f.ecx));
        for (int i = 0; i < rec.args; ++i)
            Record(Relative((rec.byteArgs & (1 << i)) ? f.args[i] & 0xFF : f.args[i]));
        if (rec.inArg >= 0) {
            const uint32_t* data = reinterpret_cast<const uint32_t*>(f.args[rec.inArg]);
            for (int i = 0; i < rec.words; ++i)
                Record(Relative(data[i]));
        }
        if (rec.outArg >= 0) {
            float* data = reinterpret_cast<float*>(f.args[rec.outArg]);
            for (int i = 0; i < rec.words; ++i)
                data[i] = RandomFloat(4.0f);
        }
        Result r;
        switch (rec.ret) {
        case Ret::None:
            break;
        case Ret::Int:
            r.eax = static_cast<uint32_t>(PickInt());
            Record(r.eax);
            break;
        case Ret::Bool:
            r.eax = Next() & 1;
            Record(r.eax);
            break;
        case Ret::Float: {
            float value = PickFloat();
            RecordFloat(value);
            r.isFloat = true;
            r.value = value;
            break;
        }
        }
        return r;
    };
    Add({address}, rec.pop, handler, also);
}

void Kit::Bytes(uint32_t address, const std::vector<uint8_t>& bytes) { WriteCode(reinterpret_cast<uint8_t*>(address), bytes); }

void Kit::Jump(uint32_t address, const void* to) { WriteCode(reinterpret_cast<uint8_t*>(address), JumpBytes(address, reinterpret_cast<const void*>(Resolve(to)))); }

void Cleanup()
{
    if (g_kit) {
        Kit* kit = g_kit;
        kit->~Kit();
    }
}

std::vector<uint32_t> Run(uint32_t seed, const std::function<uint32_t()>& call)
{
    std::memcpy(g_arena, g_start, kArenaSize);
    Seed(seed);
    g_trace.clear();
    g_stackLow = 0;
    uint32_t result = call();
    std::vector<uint32_t> trace = {Relative(result)};
    trace.insert(trace.end(), g_trace.begin(), g_trace.end());
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        trace.push_back(Relative(At<uint32_t>(i)));
    // NaNs compared as NaNs: their sign and payload aren't (x87 and SSE propagate them differently).
    for (uint32_t& word : trace) {
        if ((word & 0x7F800000u) == 0x7F800000u && (word & 0x007FFFFFu))
            word = 0x7FC00000u;
    }
    return trace;
}

bool Same(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what, int variant)
{
    if (expected == actual)
        return true;
    Log("  %s: variant %d differs (trace sizes: original %u ours %u)", what, variant,
        static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
    size_t first = SIZE_MAX;
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("    entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            if (first == SIZE_MAX)
                first = i;
            ++shown;
        }
    }
    // The original's trace before the first difference (the calls that led there).
    if (first != SIZE_MAX) {
        for (size_t i = first > 24 ? first - 24 : 0; i < first; i += 8) {
            char line[160];
            int n = 0;
            for (size_t j = i; j < i + 8 && j < first; ++j)
                n += std::snprintf(line + n, sizeof(line) - n, " %08X", expected[j]);
            Log("    before %u:%s", static_cast<unsigned>(i), line);
        }
    }
    return false;
}

} // namespace Sk5
