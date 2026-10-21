// The harness of the skater core physics self-tests (see the header).

#include "hook/selftests_skater_physics_harness.h"

#include <windows.h>

#include <float.h>

#include <cstring>

#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace SkaterPhysicsTest {

CodePatch::CodePatch(uint32_t address, const std::vector<uint8_t>& bytes)
    : at(reinterpret_cast<uint8_t*>(address)), saved(bytes.size())
{
    DWORD old;
    VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(saved.data(), at, bytes.size());
    std::memcpy(at, bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
}

CodePatch::~CodePatch()
{
    std::memcpy(at, saved.data(), saved.size());
    FlushInstructionCache(GetCurrentProcess(), at, saved.size());
}

std::vector<uint8_t> JumpTo(uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes(5);
    bytes[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}

namespace {

alignas(16) uint8_t g_arena[kArenaSize];
alignas(16) uint8_t g_start[kArenaSize];

std::vector<StandIn> g_standIns;
std::vector<uint32_t> g_calls;
uint32_t g_stubEax, g_stubPop, g_stubReturn, g_stubFloatKind;
float g_stubFloat;
double g_stubDouble;

uint32_t Relative(uint32_t word, uint32_t stackLow)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_arena);
    if (word >= base && word < base + kArenaSize)
        return 0xA0000000u + (word - base);
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= stackLow && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x5AC00000u;
    return word;
}

void __cdecl RecordStandInCall(uint32_t* frame)
{
    // frame: EBX ECX EDX ESI EDI EBP, the stand-in's index, the return address, the arguments.
    const StandIn& s = g_standIns[frame[6]];
    uint32_t* args = frame + 8;
    uint32_t stackLow = reinterpret_cast<uint32_t>(frame);
    g_calls.push_back(s.address);
    if (s.thiscall)
        g_calls.push_back(Relative(frame[1], stackLow));
    for (int i = 0; i < s.args; ++i) {
        uint32_t word = (s.byteArgs & (1 << i)) ? args[i] & 0xFF : (s.wordArgs & (1 << i)) ? args[i] & 0xFFFF : args[i];
        g_calls.push_back((s.floatArgs & (1 << i)) ? word : Relative(word, stackLow));
    }
    if (s.inArg >= 0) {
        const uint32_t* data = reinterpret_cast<const uint32_t*>(args[s.inArg]);
        for (int i = 0; i < s.words; ++i)
            g_calls.push_back(Relative(data[i], stackLow));
    }
    if (s.thisIn >= 0) {
        const uint32_t* data = reinterpret_cast<const uint32_t*>(frame[1] + s.thisIn);
        for (int i = 0; i < s.thisInWords; ++i)
            g_calls.push_back(Relative(data[i], stackLow));
    }
    static const float kFixed[4] = {1.0f, 2.0f, 3.0f, 1.0f};
    if (s.outArg >= 0) {
        float* data = reinterpret_cast<float*>(args[s.outArg]);
        for (int i = 0; i < s.words; ++i)
            data[i] = g_fixedOut ? kFixed[i % 4] : RandomFloat(s.outScale);
    }
    if (s.thisOut >= 0) {
        float* data = reinterpret_cast<float*>(frame[1] + s.thisOut);
        for (int i = 0; i < s.words; ++i)
            data[i] = g_fixedOut ? kFixed[i % 4] : RandomFloat(s.outScale);
    }
    g_stubFloatKind = 0;
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
        g_stubFloatKind = 1;
        g_stubEax = 0xEEEEEEEE;
        g_calls.push_back(*reinterpret_cast<uint32_t*>(&g_stubFloat));
        break;
    case Ret::Double: {
        g_stubDouble = static_cast<double>(PickFloat()) * (4.0 / 3.0);
        g_stubFloatKind = 2;
        g_stubEax = 0xEEEEEEEE;
        uint32_t halves[2];
        std::memcpy(halves, &g_stubDouble, 8);
        g_calls.push_back(halves[0]);
        g_calls.push_back(halves[1]);
        break;
    }
    case Ret::Fixed:
        g_stubEax = *s.value;
        g_calls.push_back(Relative(g_stubEax, stackLow));
        break;
    case Ret::FirstArg:
        g_stubEax = args[0];
        break;
    }
    g_stubPop = s.pop;
}

void (__cdecl* const g_record)(uint32_t*) = &RecordStandInCall;

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
        call dword ptr [g_record]
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
        cmp g_stubFloatKind, 1
        jne not_float
        fld g_stubFloat
        jmp g_stubReturn
    not_float:
        cmp g_stubFloatKind, 2
        jne done
        fld g_stubDouble
    done:
        jmp g_stubReturn
    }
}

} // namespace

uint8_t* const g_physicsArena = g_arena;
bool g_fixedOut = false;
uint32_t g_rng = 1;

StandIns::StandIns(const std::vector<StandIn>& list)
{
    g_standIns = list;
    thunks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    for (size_t i = 0; i < list.size(); ++i) {
        uint8_t* thunk = thunks + i * 10;
        thunk[0] = 0x68; // push i
        uint32_t index = static_cast<uint32_t>(i);
        std::memcpy(thunk + 1, &index, 4);
        thunk[5] = 0xE9; // jmp StandInEntry
        int32_t relative =
            static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StandInEntry) - (reinterpret_cast<uintptr_t>(thunk) + 10));
        std::memcpy(thunk + 6, &relative, 4);
        patches.push_back(new CodePatch(list[i].address, JumpTo(list[i].address, thunk)));
        if (list[i].also) {
            uint32_t ours = reinterpret_cast<uint32_t>(list[i].also);
            patches.push_back(new CodePatch(ours, JumpTo(ours, thunk)));
        }
    }
    FlushInstructionCache(GetCurrentProcess(), thunks, 0x1000);
}

StandIns::~StandIns()
{
    for (size_t i = patches.size(); i-- > 0;)
        delete patches[i];
    VirtualFree(thunks, 0, MEM_RELEASE);
}

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
    static const int values[] = {-100, 0, 0, 50, 150, 300, 1000, 1, 500, 250, -1};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}

float RandomFloat(float range) { return (static_cast<int>(Next() % 2001) - 1000) * (range / 1000.0f); }

uint8_t RandomBool(uint32_t percent) { return Next() % 100 < percent ? 1 : 0; }

void FillArena()
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
    pointer(kComponent + 0x1520, kMisc2);
    pointer(kComponent + 0x1524, kMisc2 + 0x10);
    pointer(kComponent + 0x1528, kFlags);
    pointer(kComponent + 0x152C, kRide);
    pointer(kComponent + 0x1530, kState);
    pointer(kComponent + 0x1534, kMisc2 + 0x20);
    pointer(kComponent + 0x1538, kProjectile);
    pointer(kRide + 0x18, kRider);
    pointer(kRider, kRideObject);
    pointer(kState + 0x2C, kWalk);
    std::memset(g_arena + kInput, 0, 0x200);
}

void SaveStart() { std::memcpy(g_start, g_arena, kArenaSize); }

std::vector<uint32_t> RunOnArenaImpl(uint32_t seed, uint32_t (*call)(uint8_t* component, void* context), void* context)
{
    std::memcpy(g_arena, g_start, kArenaSize);
    g_rng = seed;
    g_calls.clear();
    uint32_t result;
    {
        X87Double precision;
        result = call(g_arena + kComponent, context);
    }
    uint32_t stackLow = reinterpret_cast<uint32_t>(&result) - 0x10000;
    std::vector<uint32_t> trace = {Relative(result, stackLow)};
    trace.insert(trace.end(), g_calls.begin(), g_calls.end());
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
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("    entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
    return false;
}

void RecordValue(uint32_t value) { g_calls.push_back(value); }

namespace {

Mth::Matrix* __stdcall OurMatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}

} // namespace

OurMath::OurMath()
{
    auto redirect = [this](uint32_t address, const void* ours) {
        patches.push_back(new CodePatch(address, JumpTo(address, ours)));
    };
    redirect(0x004060F0, CodeAddress(&Mth::Vector::RotateToPlane));
    redirect(0x00406080, CodeAddress(&Mth::Vector::ProjectToPlane));
    redirect(0x00406200, CodeAddress(&Mth::Vector::ProjectOnto));
    redirect(0x004023F0, CodeAddress(&Mth::Vector::Normalize));
    redirect(0x00406630, CodeAddress(&Mth::GetAngleAbout));
    redirect(0x00404DD0, CodeAddress(&Mth::CreateRotateXMatrix));
    redirect(0x00404E20, CodeAddress(&Mth::CreateRotateYMatrix));
    redirect(0x00404E70, CodeAddress(&Mth::CreateRotateZMatrix));
    redirect(0x005BFA8C, reinterpret_cast<const void*>(&OurMatrixMultiply));
    redirect(0x00405670, CodeAddress(&Mth::Matrix::RotateXLocal));
    redirect(0x00405710, CodeAddress(&Mth::Matrix::RotateYLocal));
    redirect(0x004057B0, CodeAddress(&Mth::Matrix::RotateZLocal));
    redirect(0x00405950, CodeAddress(&Mth::Matrix::Rotate));
    redirect(0x00405170, CodeAddress(&Mth::Matrix::OrthoNormalizeAbout));
    redirect(0x00405990, CodeAddress(&Mth::SlerpInterpolator::SetMatrices));
    redirect(0x00405A20, CodeAddress(&Mth::SlerpInterpolator::GetMatrix));
    redirect(0x004058E0, CodeAddress(&Mth::SlerpInterpolator::InvertDirection));
}

OurMath::~OurMath()
{
    for (size_t i = patches.size(); i-- > 0;)
        delete patches[i];
}

bool Called(const std::vector<uint32_t>& trace, uint32_t address)
{
    size_t calls = trace.size() - kArenaSize / 4;
    for (size_t i = 1; i < calls; ++i) {
        if (trace[i] == address)
            return true;
    }
    return false;
}

X87Double::X87Double()
{
    _controlfp_s(&saved, 0, 0);
    unsigned int unused;
    _controlfp_s(&unused, _PC_53, _MCW_PC);
}

X87Double::~X87Double()
{
    unsigned int unused;
    _controlfp_s(&unused, saved & _MCW_PC, _MCW_PC);
}

} // namespace SkaterPhysicsTest
