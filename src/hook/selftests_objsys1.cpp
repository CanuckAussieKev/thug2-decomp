// Self-tests for the objsys agent's batch OS-1: the controller pad (src/game/obj/control_pad.cpp) - the original
// and ours on the same stand-in pads, with the clocks they read stood in for, every result and the pads compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/obj/control_pad.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t FloatBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float BitsFloat(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

alignas(16) uint8_t g_pad[0x240];

// The clocks the pad reads: a scripted sequence, the same in both runs (every read recorded).
Lcg g_clocks(1);
std::vector<uint32_t> g_times; // (directed times, used first)
uint32_t NextTime()
{
    if (!g_times.empty()) {
        uint32_t t = g_times.back();
        g_times.pop_back();
        return t;
    }
    switch (g_clocks.Below(6)) {
    case 0: return 0x90000000u + g_clocks.Below(3); // past 2^31: compared unsigned
    case 1: return 1500;                           // exactly a lock's time
    default: return 1000 + g_clocks.Below(1000);
    }
}

void RandomButton(uint8_t* button, Lcg& random)
{
    for (int i = 0; i < 0x20; ++i)
        button[i] = static_cast<uint8_t>(random.Next());
    button[0] = static_cast<uint8_t>(random.Below(3)); // up / down / a 2 stored as down
    static const uint32_t kLocks[] = {0, 0, 0, FloatBits(1500.0f), FloatBits(1200.5f), 0x7FC00000u /* NaN */,
                                      FloatBits(3.0e9f)};
    uint32_t lock = random.Pick(kLocks);
    std::memcpy(button + 0x1C, &lock, 4);
}

void RandomPad(Lcg& random)
{
    for (uint8_t& b : g_pad)
        b = static_cast<uint8_t>(random.Next());
    for (int i = 0; i < 16; ++i)
        RandomButton(g_pad + 0x20 * i, random);
    static const uint32_t kLocks[] = {0, 0, 1500, 1200, 0x90000001u, 0xFFFFFFFFu};
    for (int i = 0; i < 2; ++i) {
        uint32_t lock = random.Pick(kLocks);
        std::memcpy(g_pad + 0x230 + 4 * i, &lock, 4);
    }
}

void RandomData(uint8_t* data, Lcg& random)
{
    for (int i = 0; i < 0x20; ++i)
        data[i] = static_cast<uint8_t>(random.Next());
    static const uint8_t kStick[] = {0x80, 0x80, 0x00, 0x01, 0x7F, 0x81, 0xFF, 0xB2, 0x4E, 0xB3};
    for (int i = 0; i < 4; ++i)
        data[i] = random.Pick(kStick);
    for (int i = 4; i < 0x10; ++i)
        data[i] = random.Below(3) == 0 ? static_cast<uint8_t>(random.Next()) : 0;
    // the second stick: centred often (the d-pad stands in), else pointed up / down / anywhere
    switch (random.Below(5)) {
    case 0:
    case 1: data[0x14] = 0x80; data[0x15] = 0x80; break;
    case 2: data[0x14] = 0x80 + static_cast<uint8_t>(random.Below(20)); data[0x15] = 0x00; break;
    case 3: data[0x14] = 0x80 - static_cast<uint8_t>(random.Below(20)); data[0x15] = 0xFF; break;
    default: break;
    }
    if (random.Below(3) == 0) {
        data[0x12] = 0x80;
        data[0x13] = 0x80;
    }
}

void PushPad(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0; i < 0x238; i += 4)
        trace.push_back(*reinterpret_cast<uint32_t*>(g_pad + i));
}

// Each pad function on random pads and controller data: the button updates (locked by a time, NaN, past or not;
// pressed / released, the stored byte), the press / release times, the names, the lookup (each name, a missing
// one), the resets, the update from the data (the scaled sticks, the angles, the d-pad standing in, the two locks
// kept or ended).
void TestControlPad()
{
    GameMemory memory({{0x00696D60, 4}, {0x006F2270, 0x44}});
    StandIns& s = *new StandIns();
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        uint32_t t = NextTime();
        Record({0x71AE0000, t});
        return t;
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        uint32_t t = g_clocks.Next();
        Record({0xC10C0000, t});
        return t;
    });
    const char* const kNames[] = {"UP", "DOWN", "LEFT", "RIGHT", "SQUARE", "CIRCLE", "TRIANGLE", "X", "L1", "L2",
                                  "L3", "R1", "R2", "R3", "START", "SELECT", "Z"};
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0x0BAD0000 + seed);
            // (the d-pad angles' static table as the game starts: empty, to be filled on the first use)
            std::memset(reinterpret_cast<void*>(0x006F2270), 0, 0x44);
            g_clocks = Lcg(0xC10C0000 + seed);
            Lcg random(0x9AD00000 + seed);
            RandomPad(random);
            void* pad = g_pad;
            auto relocate = [&](uint32_t word) {
                uint32_t base = reinterpret_cast<uint32_t>(g_pad);
                return word - base < sizeof(g_pad) ? 0xA0000000u + (word - base) : word;
            };
            if (seed % 4 == 1)
                Record({relocate(reinterpret_cast<uint32_t>(
                    Fn<void*(__thiscall*)(void*)>(original, 0x0046B940)(pad)))});
            if (seed % 3 == 0) {
                // the locks at exactly the time: the second stick held up (one seed) or down (the next)
                uint8_t data[0x20];
                RandomData(data, random);
                data[0x14] = 0x80;
                data[0x15] = seed % 2 ? 0xFF : 0x00;
                uint32_t lock = 1500;
                std::memcpy(g_pad + 0x230, &lock, 4);
                std::memcpy(g_pad + 0x234, &lock, 4);
                g_times = {1500, 1500};
                Fn<void(__thiscall*)(void*, const uint8_t*)>(original, 0x0046BDA0)(pad, data);
                g_times.clear();
                PushPad(g_trace);
            }
            for (int step = 0; step < 24; ++step) {
                uint8_t* button = g_pad + 0x20 * random.Below(16);
                switch (random.Below(12)) {
                case 0:
                case 1:
                case 2: {
                    uint8_t data[0x20];
                    RandomData(data, random);
                    Fn<void(__thiscall*)(void*, const uint8_t*)>(original, 0x0046BDA0)(pad, data);
                    break;
                }
                case 3: {
                    static const int kPressures[] = {0, 0x40, 0x41, 0xFF, 0x10, -5, 0x80};
                    Fn<void(__thiscall*)(void*, int)>(original, 0x0046B820)(button, random.Pick(kPressures));
                    break;
                }
                case 4: {
                    static const uint32_t kDown[] = {0, 1, 2, 0x55, 0x100};
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0046B8B0)(button, random.Pick(kDown));
                    break;
                }
                case 5:
                    Record({static_cast<uint32_t>(Fn<int(__thiscall*)(void*)>(original, 0x0046B900)(button)),
                            static_cast<uint32_t>(Fn<int(__thiscall*)(void*)>(original, 0x0046B920)(button))});
                    break;
                case 6:
                    Fn<void(__thiscall*)(void*)>(original, 0x0046B7E0)(button);
                    Fn<void(__thiscall*)(void*)>(original, random.Below(2) ? 0x0046B7F0 : 0x0046B7E0)(
                        g_pad + 0x20 * random.Below(16));
                    break;
                case 7:
                    Fn<void(__thiscall*)(void*, const char*)>(original, 0x0046B800)(button, random.Pick(kNames));
                    break;
                case 8: {
                    uint32_t name = random.Below(4) == 0 ? 0x12345678u
                                                         : *reinterpret_cast<uint32_t*>(button + 0x18);
                    if (random.Below(2)) // (two buttons of one name: the search order decides)
                        *reinterpret_cast<uint32_t*>(g_pad + 0x20 * random.Below(16) + 0x18) = name;
                    Record({relocate(reinterpret_cast<uint32_t>(
                        Fn<void*(__thiscall*)(void*, uint32_t)>(original, 0x0046C270)(pad, name)))});
                    break;
                }
                case 9:
                    Fn<void(__thiscall*)(void*)>(original, 0x0046BB50)(pad);
                    break;
                case 10:
                    Fn<void(__thiscall*)(void*)>(original, 0x0046BC60)(pad);
                    break;
                default:
                    Fn<void(__thiscall*)(void*)>(original, 0x0046B7F0)(button);
                    break;
                }
                PushPad(g_trace);
            }
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "control pad: buttons, updates, lookups, resets");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    RemoveStandIns();
}
SELFTEST(Early, TestControlPad, RemoveStandIns);

// The d-pad tables (every combination) and the stick length (each quarter of angles, both signs of x and y, the
// clamp at 1, NaN).
void TestControlPadMath()
{
    GameMemory memory({{0x006F2270, 0x44}});
    memory.Zero();
    bool same = true;
    for (int i = 0; i < 16; ++i) {
        uint8_t up = i & 1, down = (i >> 1) & 1, left = (i >> 2) & 1, right = (i >> 3) & 1;
        int a = Original<int(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x0046BA50)(up, down, left, right);
        int b = Obj::DPadDirection(up, down, left, right);
        float c = Original<float(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x0046BA80)(up, down, left, right);
        float d = Obj::DPadAngle(up, down, left, right);
        if (a != b || FloatBits(c) != FloatBits(d)) {
            Log("  d-pad %d: direction %d / %d, angle %08X / %08X", i, a, b, FloatBits(c), FloatBits(d));
            same = false;
        }
    }
    Check(same, "control pad: d-pad direction and angle");

    same = true;
    Lcg random(77);
    const float angles[] = {0.0f, 0.5f, -0.5f, 0.7853981f, 0.7853982f, -0.7853982f, 1.0f, 1.5707964f, 2.0f,
                            2.3561945f, 2.3561946f, 2.5f, 3.1415927f, -1.0f, -2.0f, -2.3561945f, -2.5f, -3.1415927f,
                            BitsFloat(0x7FC00000u), 4.0f, -4.0f};
    // (108 / 127 and -109 / 128 put the length just over 1 at the gate's corners: the clamp)
    const float values[] = {0.0f, 127.0f, -128.0f, 64.0f, -64.0f, 100.0f, -100.0f, 1.0f, -1.0f, 37.0f, -90.0f,
                            108.0f, -109.0f, 108.0f, -109.0f};
    for (float angle : angles) {
        for (int i = 0; i < 12; ++i) {
            float x = random.Pick(values), y = random.Pick(values);
            float a = Original<float(__stdcall*)(float, float, float)>(0x0046C370)(x, y, angle);
            float b = Obj::StickLength(x, y, angle);
            if (FloatBits(a) != FloatBits(b)) {
                Log("  stick length (%g, %g, %g): %08X / %08X", x, y, angle, FloatBits(a), FloatBits(b));
                same = false;
            }
        }
    }
    const float clamps[][3] = {{108.0f, 0.0f, 0.0f}, {-109.0f, 0.0f, 0.1f}, {0.0f, 108.0f, 3.0f}, {108.0f, 0.0f, 1.5f}};
    for (const float* c : clamps) {
        float a = Original<float(__stdcall*)(float, float, float)>(0x0046C370)(c[0], c[1], c[2]);
        float b = Obj::StickLength(c[0], c[1], c[2]);
        if (FloatBits(a) != FloatBits(b)) {
            Log("  stick length (%g, %g, %g): %08X / %08X", c[0], c[1], c[2], FloatBits(a), FloatBits(b));
            same = false;
        }
    }
    Check(same, "control pad: stick length");
}
SELFTEST(Early, TestControlPadMath);

} // namespace
