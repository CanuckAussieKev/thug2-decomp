// Self-tests for the skater's spin and break-vert code (PartyMod's PS2 controls off and on) and the network
// keyboard task (PartyMod's F1): the originals - with PartyMod's bytes written in where it patches them - vs ours.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/chat.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/sdl_input.h"
#include "game/skater/rotation.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"


namespace {

// Bytes written over game code for the length of a test (restored when it goes).
struct Patch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    Patch(uint32_t address, const std::vector<uint8_t>& bytes)
        : at(reinterpret_cast<uint8_t*>(address)), saved(bytes.size())
    {
        DWORD old;
        VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved.data(), at, bytes.size());
        std::memcpy(at, bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
    }
    ~Patch()
    {
        std::memcpy(at, saved.data(), saved.size());
        FlushInstructionCache(GetCurrentProcess(), at, saved.size());
    }
    Patch(const Patch&) = delete;
    Patch& operator=(const Patch&) = delete;
};
std::vector<uint8_t> Jump(uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes(5);
    bytes[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}

// A small deterministic generator (the same sequence for both runs).
struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
};

// --- the skater: stand-ins for the callees, recording their arguments ----------------------------------------

alignas(16) uint8_t g_component[0x1600];
alignas(16) uint8_t g_object[0x100];
alignas(16) uint8_t g_input[0x200];
alignas(16) uint8_t g_flags[0x100];
alignas(16) uint8_t g_trick[0x1400];
alignas(16) uint8_t g_display_holder[0x20];
alignas(16) uint8_t g_display[0x10];
std::vector<uint32_t> g_log;
int g_time;
int g_time_step;         // 0 in some cases, so held times can hit the script's times exactly
double g_values[16];      // the script values for this case (by kind, see ValueFor)
double g_stat;            // StatValue's result
double g_off;             // 0x00406630's result
bool g_stuck;             // 0x00505F70's result

uint32_t Where(const void* p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    auto in = [&](const uint8_t* base, size_t size, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uintptr_t>(base);
        return a >= b && a < b + size ? tag + static_cast<uint32_t>(a - b) : 0;
    };
    if (!p)
        return 0;
    if (uint32_t r = in(g_component, sizeof(g_component), 0x10000000))
        return r;
    if (uint32_t r = in(g_object, sizeof(g_object), 0x20000000))
        return r;
    if (uint32_t r = in(g_display, sizeof(g_display), 0x30000000))
        return r;
    return 0xBAD00000;
}

double ValueFor(uint32_t name)
{
    switch (name) {
    case 0xA5A02449: return g_values[0];  // spin delay
    case 0xABDD3395: return g_values[1];  // face-ramp cut-off time
    case 0xA092B2BE: return g_values[2];  // ramp start
    case 0x5D756349: return g_values[3];  // ramp end
    case 0x61224F2E: return g_values[4];  // face-ramp tilt (degrees)
    case 0x9DB33213: return g_values[5];  // face-ramp speed
    case 0x50C5CC2F: return g_values[6];  // rotation snap
    case 0x9D2F8CC8: return g_values[7];  // jump time
    case 0x13F33B41: return g_values[8];
    case 0x848C5CD6: return g_values[9];
    case 0xD757F4BB: return g_values[10];
    default: return -12345.0;
    }
}
int __cdecl StandInTime()
{
    g_time += g_time_step;
    g_log.insert(g_log.end(), {0x7111u, static_cast<uint32_t>(g_time)});
    return g_time;
}
double __cdecl StandInPhysicsFloat(uint32_t name, int required, void* object)
{
    g_log.insert(g_log.end(), {0xF10Au, name, static_cast<uint32_t>(required), Where(object)});
    return static_cast<float>(ValueFor(name)); // (the original returns a float it loaded)
}
int __cdecl StandInPhysicsInt(uint32_t name, int required, void* object)
{
    g_log.insert(g_log.end(), {0x1A7u, name, static_cast<uint32_t>(required), Where(object)});
    return static_cast<int>(ValueFor(name));
}
double __fastcall StandInStatValue(void* object, void*, uint32_t name)
{
    g_log.insert(g_log.end(), {0x57A7u, Where(object), name});
    return g_stat; // (unrounded, as the original's x87 result)
}
void __fastcall StandInShowRotation(void* display, void*, int degrees)
{
    g_log.insert(g_log.end(), {0xD15Bu, Where(display), static_cast<uint32_t>(degrees)});
}
double __cdecl StandInRampOffset(void* a, void* b, void* c)
{
    g_log.insert(g_log.end(), {0x0FFu, Where(a), Where(b), Where(c)});
    return g_off;
}
bool __fastcall StandInStuck(void* component, void*)
{
    g_log.insert(g_log.end(), {0x57C4u, Where(component)});
    return g_stuck;
}

struct SkaterStandIns {
    Patch time{0x0044ECE0, Jump(0x0044ECE0, reinterpret_cast<void*>(&StandInTime))};
    Patch physicsFloat{0x00580400, Jump(0x00580400, reinterpret_cast<void*>(&StandInPhysicsFloat))};
    Patch physicsInt{0x00580440, Jump(0x00580440, reinterpret_cast<void*>(&StandInPhysicsInt))};
    Patch stat{0x00580790, Jump(0x00580790, reinterpret_cast<void*>(&StandInStatValue))};
    Patch show{0x00564BE0, Jump(0x00564BE0, reinterpret_cast<void*>(&StandInShowRotation))};
    Patch offset{0x00406630, Jump(0x00406630, reinterpret_cast<void*>(&StandInRampOffset))};
    Patch stuck{0x00505F70, Jump(0x00505F70, reinterpret_cast<void*>(&StandInStuck))};
};

// The math helpers break vert uses, in the original redirected to ours: they're tested on their own (with a
// tolerance - x87 vs SSE, sin/cos/sqrt differ in the last bit), so this test can compare exactly.
Mth::Matrix* __stdcall OurMatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}
template <typename Method>
const void* MethodAddress(Method method)
{
    const void* address;
    static_assert(sizeof(method) == sizeof(address));
    std::memcpy(&address, &method, sizeof(address));
    return address;
}
struct OurMathHelpers {
    Patch rotateX{0x00404DD0, Jump(0x00404DD0, reinterpret_cast<void*>(&Mth::CreateRotateXMatrix))};
    Patch multiply{0x005BFA8C, Jump(0x005BFA8C, reinterpret_cast<void*>(&OurMatrixMultiply))};
    Patch normalize{0x004023F0, Jump(0x004023F0, MethodAddress(&Mth::Vector::Normalize))};
    Patch ortho{0x00405170, Jump(0x00405170, MethodAddress(&Mth::Matrix::OrthoNormalizeAbout))};
};

// A rotation matrix (rows) from three angles, with a position.
void SetMatrix(float* m, float a, float b, float c)
{
    float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
    const float rows[16] = {cb * cc, cb * sc, -sb, 0.0f,
                            sa * sb * cc - ca * sc, sa * sb * sc + ca * cc, sa * cb, 0.0f,
                            ca * sb * cc + sa * sc, ca * sb * sc - sa * cc, ca * cb, 0.0f,
                            10.5f, -3.25f, 7.0f, 1.0f};
    std::memcpy(m, rows, sizeof(rows));
}

// One random case: the component, its object, pad, flags and trick.
void BuildSkater(Random& r)
{
    for (size_t i = 0; i < sizeof(g_component); ++i)
        g_component[i] = static_cast<uint8_t>(r.Next());
    std::memset(g_input, 0, sizeof(g_input));
    std::memset(g_flags, 0, sizeof(g_flags));
    std::memset(g_object, 0, sizeof(g_object));
    std::memset(g_trick, 0, sizeof(g_trick));
    std::memset(g_display, 0, sizeof(g_display));
    *reinterpret_cast<void**>(g_component + 0x0C) = g_object;
    *reinterpret_cast<void**>(g_component + 0x150C) = g_input;
    *reinterpret_cast<void**>(g_component + 0x1518) = g_trick;
    *reinterpret_cast<void**>(g_component + 0x1520) = g_display_holder;
    *reinterpret_cast<void**>(g_component + 0x1528) = g_flags;
    *reinterpret_cast<void**>(g_display_holder + 0x18) = g_display;
    SetMatrix(reinterpret_cast<float*>(g_object + 0x6C), 0.3f * r.Below(9), 0.7f * r.Below(5) - 1.2f, 0.4f * r.Below(7));
    SetMatrix(reinterpret_cast<float*>(g_component + 0x18), 0.2f * r.Below(9), 0.5f * r.Below(5), 0.9f * r.Below(4));
    const float velocities[][4] = {{3.0f, 12.5f, -4.0f, 0.0f}, {-0.1f, -7.0f, 0.3f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
                                   {25.0f, 1.5f, 13.25f, 0.5f}};
    std::memcpy(g_object + 0xAC, velocities[r.Below(4)], 16);
    const float normals[][2] = {{1.0f, 0.0f}, {0.6f, -0.8f}, {-0.70710677f, 0.70710677f}};
    std::memcpy(g_component + 0x58, &normals[r.Below(3)][0], 4);
    std::memcpy(g_component + 0x60, &normals[r.Below(3)][1], 4);
    const float frames[] = {1.0f / 60.0f, 0.0333333f, 0.05f, 0.0f, 0.0625f};
    std::memcpy(g_component + 0x8C, &frames[r.Below(5)], 4);
    const float queued[] = {0.0f, 0.0f, 3.14159274f, -6.2831855f, 0.01f, -0.002f, 1.0f, 0.25002f, -0.25f};
    std::memcpy(g_component + 0x30C, &queued[r.Below(9)], 4);
    g_component[0x370] = r.Below(8) == 0;
    g_component[0x2DA] = r.Below(3) == 0;
    g_component[0x2D0] = r.Below(4) == 0;
    const float tilts[] = {0.99f, 0.2f, -0.5f, 1.5f, -2.0f, 0.5f};
    std::memcpy(g_object + 0x90, &tilts[r.Below(6)], 4); // the forward vector's y
    const float totals[] = {0.0f, 179.5f, -350.25f, 359.95f, 720.0f, 3.0e9f, 340.05f, -340.05f};
    std::memcpy(g_trick + 0x13A4, &totals[r.Below(8)], 4);
    // Buttons: down, the time they went down (held = now + offset - that), pressed this frame.
    for (uint32_t button : {0x20u, 0x60u, 0x80u, 0xA0u, 0xC0u, 0x100u, 0x120u, 0x160u, 0x180u}) {
        g_input[button] = r.Below(3) == 0;
        g_input[button + 0x10] = r.Below(3) == 0;
        int start = static_cast<int>(r.Below(1200)) - 100;
        if (r.Below(8) == 0)
            start = 2000000000; // (a negative held time)
        std::memcpy(g_input + button + 4, &start, 4);
    }
    for (uint32_t flag = 0; flag < 0xD0; flag += 8)
        g_flags[flag] = r.Below(3) == 0;
    g_flags[0x80] = static_cast<uint8_t>(r.Below(3)); // (0, 1 or another true value)
    const double delays[] = {50.0, 400.0, 900.0};
    g_values[0] = delays[r.Below(3)];
    g_values[1] = delays[r.Below(3)];
    const double starts[] = {100.0, 300.0, 700.0};
    g_values[2] = starts[r.Below(3)];
    g_values[3] = starts[r.Below(3)] + 50.0 * r.Below(5) - 100.0;
    const double tiltLimits[] = {5.0, 60.0, 170.0, 60.3};
    g_values[4] = tiltLimits[r.Below(4)];
    const double rates[] = {2.5, -1.75, 0.0};
    g_values[5] = rates[r.Below(3)];
    const double snaps[] = {20.0, 400.0, 0.5};
    g_values[6] = snaps[r.Below(3)];
    g_values[7] = 100.0 * r.Below(8);
    g_values[8] = 0.0625 * r.Below(5);
    g_values[9] = 0.5 + 0.25 * r.Below(4);
    g_values[10] = 15.0 * r.Below(7) - 30.0;
    const double stats[] = {6.283185307179586, 4.71238898, 0.1, 11.0, 4.0};
    g_stat = stats[r.Below(5)];
    const double offs[] = {0.3, -0.05, 0.0001, -2.0, 0.0};
    g_off = offs[r.Below(5)];
    g_stuck = r.Bit();
    g_time = static_cast<int>(r.Below(1000));
    g_time_step = r.Below(4) == 0 ? 0 : 7;
    int offset = static_cast<int>(r.Below(3)) * 40;
    GameVariable<int>(0x00696D60) = offset;
    if (g_time_step == 0) {
        // Held times exactly at the jump time and the ramp start.
        int jump = g_time + offset - static_cast<int>(g_values[7]);
        int ramp = g_time + offset - static_cast<int>(g_values[2]);
        std::memcpy(g_input + 0x20 + 4, &jump, 4);
        std::memcpy(g_input + 0x60 + 4, &ramp, 4);
        std::memcpy(g_input + 0x80 + 4, &ramp, 4);
    }
}

void TraceSkater(std::vector<uint32_t>& trace)
{
    auto words = [&](const uint8_t* p, size_t n) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
        trace.insert(trace.end(), w, w + n / 4);
    };
    words(g_component, 0x400); // (the fields used are below 0x400, the pointers at 0x150C-0x1528 never change)
    words(g_object, sizeof(g_object));
    words(g_input, sizeof(g_input));
    words(g_flags, sizeof(g_flags));
    trace.push_back(*reinterpret_cast<uint32_t*>(g_trick + 0x13A4));
    trace.insert(trace.end(), g_log.begin(), g_log.end());
}

void CompareTraces(const std::vector<uint32_t>* traces, const char* what, const char* name)
{
    Check(traces[0] == traces[1], what);
    if (traces[0].size() != traces[1].size())
        Log("  %s trace sizes: original %u ours %u", name, static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  %s entry %u: original %08X ours %08X", name, static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}

// Spin: random cases over every input that decides a branch (tap mode or not, the buttons and their held
// times against the script's times, the flags, the queued turn, the face-the-ramp values, the rotation total),
// PartyMod's PS2 controls off and on - the original with its NOPs at 0x00504239 / 0x0050430B for "on".
void TestSkaterSpin()
{
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    int savedOffset = GameVariable<int>(0x00696D60);
    std::vector<uint32_t> traces[2];
    {
        SkaterStandIns standIns;
        for (int run = 0; run < 2; ++run) {
            Random r{12345};
            for (int n = 0; n < 6000; ++n) {
                BuildSkater(r);
                bool ps2 = r.Bit();
                g_log.clear();
                if (run == 0) {
                    std::vector<Patch*> bytes;
                    if (ps2) {
                        bytes.push_back(new Patch(0x00504239, {0x90, 0x90}));
                        bytes.push_back(new Patch(0x0050430B, {0x90, 0x90}));
                    }
                    Original<void(__thiscall*)(void*)>(0x00504060)(g_component);
                    for (Patch* p : bytes)
                        delete p;
                } else {
                    Sio::TheInputSettings().ps2Controls = ps2 ? 1 : 0;
                    reinterpret_cast<Skater::CorePhysicsComponent*>(g_component)->Spin();
                }
                TraceSkater(traces[run]);
            }
        }
    }
    Sio::TheInputSettings().ps2Controls = savedSettings;
    GameVariable<int>(0x00696D60) = savedOffset;
    CompareTraces(traces, "skater: spin (PS2 controls off and on)", "spin");
}

// Break vert: the same over its inputs (the jump button's held time and the others, the triggers, the flag,
// `force`, the stuck check), PS2 controls off and on - the original with its JNZs at 0x00507184 / 0x005071AE.
void TestSkaterBreakVert()
{
    OurMathHelpers helpers;
    uint8_t savedSettings = Sio::TheInputSettings().ps2Controls;
    int savedOffset = GameVariable<int>(0x00696D60);
    std::vector<uint32_t> traces[2];
    {
        SkaterStandIns standIns;
        for (int run = 0; run < 2; ++run) {
            Random r{777};
            for (int n = 0; n < 4000; ++n) {
                BuildSkater(r);
                bool ps2 = r.Bit();
                bool force = r.Below(4) == 0;
                g_log.clear();
                if (run == 0) {
                    std::vector<Patch*> bytes;
                    if (ps2) {
                        bytes.push_back(new Patch(0x00507184, {0x75, 0x20}));
                        bytes.push_back(new Patch(0x005071AE, {0x75, 0x08}));
                    }
                    Original<void(__thiscall*)(void*, bool)>(0x00507120)(g_component, force);
                    for (Patch* p : bytes)
                        delete p;
                } else {
                    Sio::TheInputSettings().ps2Controls = ps2 ? 1 : 0;
                    reinterpret_cast<Skater::CorePhysicsComponent*>(g_component)->BreakVert(force);
                }
                TraceSkater(traces[run]);
                const uint32_t* display = reinterpret_cast<const uint32_t*>(g_component + 0x18);
                traces[run].insert(traces[run].end(), display, display + 16);
            }
        }
    }
    Sio::TheInputSettings().ps2Controls = savedSettings;
    GameVariable<int>(0x00696D60) = savedOffset;
    CompareTraces(traces, "skater: break vert (PS2 controls off and on)", "break vert");
}

// --- the network keyboard task ---------------------------------------------------------------------------------

bool g_exists[4];
int g_exists_calls;
std::vector<uint32_t> g_key_log;
uint32_t IdOf(Script::CStruct* params, uint32_t name)
{
    uint32_t value = 0xFFFFFFFF;
    if (params && !params->GetChecksum(name, &value, 0))
        value = 0xEEEEEEEE;
    return value;
}
bool __cdecl StandInObjectExists(Script::CStruct* params, void* script)
{
    g_key_log.insert(g_key_log.end(), {0x0B1Eu, IdOf(params, 0x40C698AF), reinterpret_cast<uint32_t>(script)});
    return g_exists[g_exists_calls++ & 3];
}
void __cdecl StandInRunScript(uint32_t name, Script::CStruct* params, int a, int b, int c)
{
    g_key_log.insert(g_key_log.end(), {0x5C21u, name, params ? 1u : 0u, IdOf(params, 0xB53D0E0F),
                                        static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)});
}

// (64K each, aligned to 64K, and both mapped in either run - ArenaWord / ArenaWordOrHigh, selftest.h: a value that
// only happens to fall inside one of them maps the same way in each)
constexpr uint32_t kKeyArenaSize = 0x10000;
ArenaPair<kKeyArenaSize> g_key_storage;
uint8_t (*const g_key_arenas)[kKeyArenaSize] = g_key_storage.Get();

// The keyboard task: no key, Enter with each dialog present or none, F1 (0x1F with PartyMod, the original
// with its byte at 0x00533652), F2-F4, Escape (0x1B) and another key, each at 2000 and 2001 ms since the last
// taunt - over a stand-in memory manager (the parameters it allocates, and leaks for Enter, compared).
void TestKeyboardTask()
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestKeyboardTask skipped (the memory manager is already set up)");
        return;
    }
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {
        {0x007CC979, 1, {}},  {0x006F2338, 16, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}},
        {0x006F3644, 16, {}}, {0x006F3654, 16, {}}, {0x006F362C, 16, {}}, {0x006F3570, 24, {}},
        {0x007D1E28, 4, {}},  {0x007D1BE0, 64, {}}, {0x007CCE20, 4, {}},  {0x007D1EC8, 8, {}},
        {0x00684FD0, 4, {}},
    };
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    Patch exists(0x00462340, Jump(0x00462340, reinterpret_cast<void*>(&StandInObjectExists)));
    auto ourObjectExists = reinterpret_cast<uint32_t>(CodeAddress(&Obj::CFunc_ObjectExists));
    Patch existsOurs(ourObjectExists, Jump(ourObjectExists, reinterpret_cast<void*>(&StandInObjectExists)));
    Patch run(0x00475790, Jump(0x00475790, reinterpret_cast<void*>(&StandInRunScript)));
    // (the chat code calls ours directly)
    auto ourRunScript = reinterpret_cast<uint32_t>(
        CodeAddress(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript)));
    Patch runOurs(ourRunScript, Jump(ourRunScript, reinterpret_cast<void*>(&StandInRunScript)));

    auto runAll = [&](bool original) {
        std::vector<uint32_t> trace;
        for (Saved& s : saved)
            std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        GameVariable<bool>(0x007CC979) = true;
        uint8_t* arena = g_key_arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_key_arenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_key_arenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + sizeof(g_key_arenas[0]));
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->m_scriptHeap = heap;
        manager->PushContext(heap);
        auto relocate = [](uint32_t word) { return ArenaWord(g_key_arenas, kKeyArenaSize, word); };
        GameVariable<float>(0x00684FD0) = 60.0f;
        GameVariable<uint64_t>(0x007D1EC8) = 600; // 10000 ms
        const int keys[] = {-1, 0x19, 0x1F, 0x1C, 0x1D, 0x1E, 0x1B, 0x41};
        int variant = 0;
        for (int key : keys) {
            for (int sinceLast : {2000, 2001}) {
                for (int dialog = 0; dialog < (key == 0x19 ? 5 : 1); ++dialog) {
                    ++variant;
                    g_key_log.clear();
                    g_exists_calls = 0;
                    for (int i = 0; i < 4; ++i)
                        g_exists[i] = i == dialog;
                    GameVariable<uint32_t>(0x007CCE20) = 10000u - static_cast<uint32_t>(sinceLast);
                    GameVariable<uint32_t>(0x007D1E28) = key < 0 ? 0 : 1 + (variant & 1);
                    GameVariable<int[16]>(0x007D1BE0)[0] = key;
                    GameVariable<int[16]>(0x007D1BE0)[1] = 0x1C;
                    if (original) {
                        Patch f1(0x00533652, {0x1F}); // PartyMod's
                        Original<void(__cdecl*)(void*)>(0x005334D0)(nullptr);
                    } else {
                        GameNet::KeyboardTask(nullptr);
                    }
                    trace.insert(trace.end(), g_key_log.begin(), g_key_log.end());
                    trace.push_back(GameVariable<uint32_t>(0x007D1E28));
                    trace.push_back(GameVariable<uint32_t>(0x007CCE20));
                }
            }
        }
        std::vector<uint32_t> image(sizeof(g_key_arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_key_arenas[0]));
        for (uint32_t& word : image) {
            word = ArenaWordOrHigh(g_key_arenas, kKeyArenaSize, word);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> traces[2] = {runAll(true), runAll(false)};
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    CompareTraces(traces, "net: keyboard task (Enter, F1-F4 with PartyMod's F1)", "keyboard task");
}

} // namespace

SELFTEST(Early, TestSkaterSpin);
SELFTEST(Early, TestSkaterBreakVert);
SELFTEST(Early, TestKeyboardTask);
