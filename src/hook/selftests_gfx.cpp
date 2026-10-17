// Self-tests for src/game/gfx: the originals as PartyMod leaves them (its jumps written over their starts for the
// length of the test) against ours.

#include <cstdint>
#include <cstring>
#include <vector>

#include <windows.h>

#include "game/gfx/screen.h"
#include "game/gfx/settings.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "game/sys/sdl_window.h"


namespace {

// A jump written over a game function's start for the length of a test (restored when it goes) - as PartyMod's
// patchJmp does. (A copy of selftest.cpp's.)
struct GfxDetour {
    uint8_t* at;
    uint8_t saved[5];
    GfxDetour(uint32_t address, const void* to) : at(reinterpret_cast<uint8_t*>(address))
    {
        DWORD old;
        VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved, at, 5);
        at[0] = 0xE9;
        int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (address + 5));
        std::memcpy(at + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
    ~GfxDetour()
    {
        std::memcpy(at, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

struct GfxSaved {
    uint32_t address;
    uint32_t size;
    std::vector<uint8_t> bytes;
};

// Everything these functions read or write: the aspect, the angle-factor constant, the blur state, the letterbox
// state and screen conversion (0x00786D6C-0x00786D8C), the skater (PartyMod's front-end check) and PartyMod's own.
std::vector<GfxSaved> SaveGfxState()
{
    std::vector<GfxSaved> saved = {{0x00701340, 4, {}}, {0x0067E544, 4, {}}, {0x00786D40, 4, {}},
                                   {0x00787328, 4, {}}, {0x006FA72C, 4, {}}, {0x00786CBE, 1, {}},
                                   {0x00786D6C, 0x24, {}}, {0x007CE478, 4, {}}};
    for (GfxSaved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    return saved;
}

void RestoreGfxState(const std::vector<GfxSaved>& saved)
{
    for (const GfxSaved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
}

// The screen conversion and letterbox state, traced.
void TraceScreen(std::vector<uint32_t>& trace)
{
    trace.push_back(GameVariable<uint32_t>(0x00701340));
    trace.push_back(GameVariable<uint8_t>(0x00786CBE));
    for (uint32_t address = 0x00786D6C; address < 0x00786D90; address += 4)
        trace.push_back(GameVariable<uint32_t>(address));
    trace.push_back(GameVariable<uint32_t>(0x00787328));
    trace.push_back(GameVariable<uint32_t>(0x006FA72C));
}

void CompareTraces(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what)
{
    Check(expected == actual, what);
    if (expected.size() != actual.size())
        Log("  %s: trace sizes original %u ours %u", what, static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  %s entry %u: original %08X ours %08X", what, static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The aspect ratio, angle factor and letterbox over front end / level, window shapes (wide, tall) and back
// buffers (wider and narrower than wanted), with the letterbox flag on and off before; blur with PartyMod's
// DisableBlur on and off (off: the unpatched original), the device able to blur or not.
void TestGfxScreen()
{
    std::vector<GfxSaved> saved = SaveGfxState();
    int savedResX = Sys::WindowWidth(), savedResY = Sys::WindowHeight();
    bool savedBlurDisabled = Nx::BlurDisabled();

    // A stand-in skater: PartyMod reads its career's level (+0x20 -> +0x630) to tell the front end (0) from a level.
    alignas(16) static uint8_t career[0x640];
    alignas(16) static uint8_t skater[0x40];
    *reinterpret_cast<void**>(skater + 0x20) = career;

    struct Shape {
        int resX, resY;     // the window
        int width, height;  // the back buffer
    };
    const Shape shapes[] = {{1920, 1080, 1600, 900}, {1280, 1024, 1280, 1024}, {800, 1280, 1366, 768},
                            {2560, 1080, 1024, 768}};
    const float aspects[] = {1.25f, 1.7777778f};
    const uint32_t blurs[] = {0, 1, 200, 0xFFFFFFF0u};

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];
        RestoreGfxState(saved); // both runs from the same state
        GfxDetour* jumps[4] = {};
        if (original) {
            // PartyMod's jumps from the originals to its versions (now ours)
            jumps[0] = new GfxDetour(0x004A0780, reinterpret_cast<const void*>(&Nx::SetScreenAspectRatio));
            jumps[1] = new GfxDetour(0x004A07C0, reinterpret_cast<const void*>(&Nx::GetScreenAngleFactor));
            jumps[3] = new GfxDetour(0x004B2440, reinterpret_cast<const void*>(&Nx::SetLetterbox));
        }
        for (int level : {0, 7}) {
            career[0x630] = static_cast<uint8_t>(level);
            for (int withSkater = 0; withSkater < 2; ++withSkater) {
                GameVariable<void*>(0x007CE478) = withSkater ? skater : nullptr;
                for (const Shape& shape : shapes) {
                    Sys::WindowWidth() = shape.resX;
                    Sys::WindowHeight() = shape.resY;
                    GameVariable<int>(0x00786D6C) = shape.width;
                    GameVariable<int>(0x00786D70) = shape.height;
                    for (float aspect : aspects) {
                        GameVariable<float>(0x00701340) = -3.0f;
                        if (original)
                            Original<void(__cdecl*)(float)>(0x004A0780)(aspect);
                        else
                            Nx::SetScreenAspectRatio(aspect);
                        TraceScreen(trace);
                    }
                    for (float constant : {1.0f, 0.875f}) {
                        GameVariable<float>(0x0067E544) = constant;
                        GameVariable<float>(0x00701340) = -3.0f;
                        float factor = original ? Original<float(__cdecl*)()>(0x004A07C0)() : Nx::GetScreenAngleFactor();
                        trace.push_back(Bits(factor));
                        TraceScreen(trace);
                    }
                    GameVariable<float>(0x0067E544) = 1.0f;
                    for (int before : {0, 1})
                        for (bool letterbox : {false, true}) {
                            GameVariable<uint8_t>(0x00786CBE) = static_cast<uint8_t>(before);
                            for (uint32_t address = 0x00786D74; address < 0x00786D90; address += 4)
                                GameVariable<uint32_t>(address) = 0x11110000u + address;
                            if (original)
                                Original<void(__cdecl*)(bool)>(0x004B2440)(letterbox);
                            else
                                Nx::SetLetterbox(letterbox);
                            TraceScreen(trace);
                        }
                }
            }
        }
        for (int disabled : {1, 0}) {
            Nx::BlurDisabled() = disabled != 0;
            if (original && disabled)
                jumps[2] = new GfxDetour(0x004B2330, reinterpret_cast<const void*>(&Nx::SetScreenBlur));
            for (uint32_t available : {0u, 1u})
                for (uint32_t blur : blurs) {
                    GameVariable<uint32_t>(0x00786D40) = available;
                    GameVariable<uint32_t>(0x00787328) = 0x5A5A;
                    if (original)
                        Original<void(__cdecl*)(uint32_t)>(0x004B2330)(blur);
                    else
                        Nx::SetScreenBlur(blur);
                    trace.push_back(GameVariable<uint32_t>(0x00787328));
                }
            delete jumps[2];
            jumps[2] = nullptr;
        }
        for (GfxDetour* jump : jumps)
            delete jump;
    }
    RestoreGfxState(saved);
    Sys::WindowWidth() = savedResX;
    Sys::WindowHeight() = savedResY;
    Nx::BlurDisabled() = savedBlurDisabled;
    CompareTraces(traces[0], traces[1], "gfx screen: aspect, angle factor, letterbox, blur (as PartyMod leaves them)");
}

// The SetScreenBlur script command over parameters with the amount, without it, and with only a named integer,
// PartyMod's DisableBlur on and off, the device able to blur or not.
void TestGfxScriptSetScreenBlur()
{
    using Script::CComponent;
    using Script::CStruct;
    std::vector<GfxSaved> saved = SaveGfxState();
    bool savedBlurDisabled = Nx::BlurDisabled();

    // Stand-in parameter structures (nothing allocated: the getters only read).
    static CComponent named, amount, other;
    std::memset(&named, 0, sizeof(named));
    named.typeAndFlag = Script::ESYMBOLTYPE_INTEGER << 1;
    named.name = 0x1D5D3A2Bu;
    named.integer = 99;
    amount = named;
    amount.name = 0;
    amount.integer = 150;
    other = named;
    other.typeAndFlag = Script::ESYMBOLTYPE_FLOAT << 1;
    other.name = 0;
    other.floatValue = 3.0f;
    static CStruct params[4];
    std::memset(params, 0, sizeof(params));
    params[1].m_head = &named; // only a named integer
    params[2].m_head = &amount; // the amount
    named.next = nullptr;
    amount.next = &other;
    params[3].m_head = &other; // an unnamed float, not an integer

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        RestoreGfxState(saved);
        for (int disabled : {1, 0}) {
            Nx::BlurDisabled() = disabled != 0;
            GfxDetour* jump = original && disabled
                ? new GfxDetour(0x004B2330, reinterpret_cast<const void*>(&Nx::SetScreenBlur))
                : nullptr;
            for (uint32_t available : {0u, 1u})
                for (CStruct& p : params) {
                    GameVariable<uint32_t>(0x00786D40) = available;
                    GameVariable<uint32_t>(0x00787328) = 0x5A5A;
                    GameVariable<int>(0x006FA72C) = -7;
                    bool result = original
                        ? Original<bool(__cdecl*)(CStruct*, void*)>(0x0048C170)(&p, nullptr)
                        : Nx::CFunc_SetScreenBlur(&p, nullptr);
                    traces[run].push_back(result);
                    traces[run].push_back(GameVariable<uint32_t>(0x00787328));
                    traces[run].push_back(GameVariable<uint32_t>(0x006FA72C));
                }
            delete jump;
        }
    }
    RestoreGfxState(saved);
    Nx::BlurDisabled() = savedBlurDisabled;
    CompareTraces(traces[0], traces[1], "gfx: the SetScreenBlur script command (as PartyMod leaves it)");
}

} // namespace

SELFTEST(Early, TestGfxScreen);
SELFTEST(Early, TestGfxScriptSetScreenBlur);
