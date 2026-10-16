// Self-test for the main loop (Mlp::Manager::MainLoop, 0x0044EEF0). The loop runs until the game ends, so the
// original (with PartyMod's call to the letterboxed render, Mlp::RenderFrameLetterboxed, written in) and ours run against recording stand-ins for
// everything they call - the Win32 timer, sleep and screen saver calls included - with a scripted clock, and a
// stand-in ends the loop the way the game does (m_done set by a task, or the quit flag). The two call traces,
// with the state at every call, are compared.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/gfx/screen.h"
#include "game/sio/device.h"
#include "game/sys/mlp.h"
#include "game/sys/platform.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"


namespace {

using namespace TestStandIns;

alignas(16) uint8_t g_mlp[0x100];
alignas(16) uint8_t g_skate[0x40];
alignas(16) uint8_t g_career[0x640];
void* g_device[1];
void* g_deviceVtable[20];

Mlp::Manager* TheManager() { return reinterpret_cast<Mlp::Manager*>(g_mlp); }
bool& SkipRender() { return GameVariable<bool>(0x007D1EC0); }
bool& Flag6F1158() { return GameVariable<bool>(0x006F1158); }
bool& AppActive() { return GameVariable<bool>(0x007CCBE4); }
bool& TextEntry() { return GameVariable<bool>(0x007CCDF8); }
bool& Quit() { return GameVariable<bool>(0x007D6A2C); }
bool& DeviceLost() { return GameVariable<bool>(0x00786AB4); }
int& SkippedInARow() { return GameVariable<int>(0x00696D74); }
int& SkippedTotal() { return GameVariable<int>(0x00696D70); }
uint8_t& Letterbox() { return GameVariable<uint8_t>(0x00786CBE); }

// The scripted world: a clock (QueryPerformanceCounter adds g_qpcAdvance per read, Sleep g_sleepAdvance), the
// frame number (counted at each frame's keyboard read) and a per-case script run at every stand-in call.
enum Tag : uint32_t {
    kKeyboard = 0x0D60, kEvents = 0x56E0, kNetwork = 0x6640, kRelease1 = 0x0320, kRelease2 = 0x67F0, kRelease3 = 0x7AF0,
    kRestore1 = 0x0440, kRestore2 = 0x68C0, kRestore3 = 0x8420, kVblank = 0xE700, kRendered = 0xE740,
    kFrameLength = 0xE7F0, kPrintf = 0xDB6, kTasks = 0x70A0, kBegin = 0xBF10, kRender = 0xC330, kAfter = 0x5DA0,
    kEnd = 0xBF20, kLetterbox = 0x1E77, kCoop = 0xC00B, kReset = 0x5E5E, kSpi = 0x5B1, kQpf = 0x9F, kQpc = 0x9C,
    kSleep = 0x51EE,
};
uint64_t g_clock;
uint64_t g_qpcAdvance;
uint64_t g_sleepAdvance;
int64_t g_frequency;
UINT g_screenSaverTimeout;
int g_frame;
int g_eventsCalls;
int g_sleeps;
HRESULT g_coop;
HRESULT g_reset;
void (*g_script)(uint32_t tag, Regs& r);
int g_stageCalls;
bool g_stageFirst;

// The state the loop keeps in globals and in the manager, packed.
uint32_t State()
{
    return (SkipRender() ? 1u : 0u) | (Flag6F1158() ? 2u : 0u) | (DeviceLost() ? 4u : 0u) |
           (static_cast<uint32_t>(Letterbox()) << 4) | (static_cast<uint32_t>(TheManager()->m_done) << 8) |
           (static_cast<uint32_t>(SkippedInARow() & 0xFF) << 16) | (static_cast<uint32_t>(SkippedTotal() & 0xFF) << 24);
}
uint32_t Event(uint32_t tag, Regs& r)
{
    if (g_script)
        g_script(tag, r);

    Record({tag, static_cast<uint32_t>(g_frame), State(), static_cast<uint32_t>(TheManager()->m_profileFrames),
            static_cast<uint32_t>(TheManager()->m_profiling)});
    return 0;
}
Handler On(uint32_t tag)
{
    return [tag](Regs& r) { return Event(tag, r); };
}

void SetUpStandIns(StandIns& s)
{
    s.At({Resolve(reinterpret_cast<const void*>(&RunStageOnce))}, 0, [](Regs& r) {
        ++g_stageCalls;
        g_stageFirst &= g_trace.empty() && r.args[0] == static_cast<uint32_t>(TestStage::EngineStarted);
        return 0u;
    });
    s.At({0x005BDD60, AddressOf(&Sio::ReadKeyboard)}, 0, [](Regs& r) {
        ++g_frame;
        if (g_frame > 3000) // (a runaway loop: end it)
            Quit() = true, TheManager()->m_done = 1;
        return Event(kKeyboard, r);
    });
    s.At({0x005F56E0, AddressOf(&Platform::ProcessWindowEvents)}, 0, [](Regs& r) {
        ++g_eventsCalls;
        return Event(kEvents, r);
    });
    s.At({0x00536640}, 0, On(kNetwork));
    s.At({0x004E0320}, 0, On(kRelease1));
    s.At({0x004D67F0}, 0, On(kRelease2));
    s.At({0x004D7AF0}, 0, On(kRelease3));
    s.At({0x004E0440}, 0, On(kRestore1));
    s.At({0x004D68C0}, 0, On(kRestore2));
    s.At({0x004D8420}, 0, On(kRestore3));
    s.At({0x005BE700, AddressOf(&Tmr::CountVblank)}, 0, On(kVblank));
    s.At({0x005BE740, AddressOf(&Tmr::OncePerRender)}, 0, On(kRendered));
    s.At({0x005BE7F0, AddressOf(&Tmr::UpdateFrameLength)}, 0, On(kFrameLength));
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({Hash(reinterpret_cast<const char*>(r.args[0]))});
        return Event(kPrintf, r);
    });
    s.At({0x004070A0}, 4, [](Regs& r) {
        Record({r.ecx - reinterpret_cast<uint32_t>(g_mlp), r.args[0] & 0xFF});
        return Event(kTasks, r);
    });
    s.At({0x0048BF10}, 0, On(kBegin));
    s.At({0x0048C330}, 0, On(kRender));
    s.At({0x00565DA0}, 0, On(kAfter));
    s.At({0x0048BF20}, 0, On(kEnd));
    s.At({Resolve(reinterpret_cast<const void*>(&Nx::SetLetterbox))}, 0, [](Regs& r) {
        Record({r.args[0]});
        Letterbox() = static_cast<uint8_t>(r.args[0]);
        return Event(kLetterbox, r);
    });
    g_deviceVtable[3] = s.Thunk(4, [](Regs& r) { // TestCooperativeLevel
        Record({r.args[0] == reinterpret_cast<uint32_t>(g_device)});
        Event(kCoop, r);
        return static_cast<uint32_t>(g_coop);
    });
    g_deviceVtable[16] = s.Thunk(8, [](Regs& r) { // Reset
        Record({r.args[0] == reinterpret_cast<uint32_t>(g_device), r.args[1] == 0x00786A7C});
        Event(kReset, r);
        return static_cast<uint32_t>(g_reset);
    });
    StandInApi(s, 0x00645214, reinterpret_cast<const void*>(&SystemParametersInfoA), 16, [](Regs& r) {
        Record({r.args[0], r.args[1], r.args[2] != 0, r.args[3]});
        if (r.args[0] == SPI_GETSCREENSAVETIMEOUT && r.args[2])
            *reinterpret_cast<UINT*>(r.args[2]) = g_screenSaverTimeout;
        Event(kSpi, r);
        return 1u;
    });
    StandInApi(s, 0x006450B0, reinterpret_cast<const void*>(&QueryPerformanceFrequency), 4, [](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.args[0]), &g_frequency, 8);
        Event(kQpf, r);
        return 1u;
    });
    StandInApi(s, 0x006450AC, reinterpret_cast<const void*>(&QueryPerformanceCounter), 4, [](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.args[0]), &g_clock, 8);
        Record({static_cast<uint32_t>(g_clock), static_cast<uint32_t>(g_clock >> 32)});
        g_clock += g_qpcAdvance;
        Event(kQpc, r);
        return 1u;
    });
    StandInApi(s, 0x006450A8, reinterpret_cast<const void*>(&Sleep), 4, [](Regs& r) {
        Record({r.args[0]});
        g_clock += g_sleepAdvance;
        if (++g_sleeps > 100000) { // (a runaway wait: end it)
            g_clock += 0x100000000000ull;
            AppActive() = true;
            Quit() = true;
            TheManager()->m_done = 1;
        }
        Event(kSleep, r);
        return 0u;
    });
}

uint32_t g_lcg;
uint32_t Next()
{
    g_lcg = g_lcg * 1103515245u + 12345u;
    return g_lcg >> 8;
}

struct Case {
    const char* name;
    void (*setup)();
    void (*script)(uint32_t tag, Regs& r);
};

// Frame times at 100 ticks a frame with jitter: early (the wait loop), on time, more than half a frame late
// (skipped renders, at most four in a row), a long pause (more than 20 frames: counting restarts). Profiling for
// three frames, the screen saver's timeout set again every 201 frames, the letterbox turned on in the front end,
// tasks changing the skip flag. Ends after 450 frames by a task setting m_done (0x5A on entry is put back).
void SetUpTiming()
{
    g_frequency = 6059; // 100 ticks a frame
    g_screenSaverTimeout = 600;
    g_clock = 0x00000000FFFFFF00ull; // (crosses 2^32)
    g_qpcAdvance = 1;
    g_sleepAdvance = 13;
    TheManager()->m_done = 0x5A;
    TheManager()->m_profileFrames = 3;
    GameVariable<uint8_t*>(0x007CE478) = nullptr; // (no skate module: the front end)
    Letterbox() = 2;
    SkippedInARow() = 0;
    SkippedTotal() = 250;
}
void ScriptTiming(uint32_t tag, Regs& r)
{
    if (tag == kKeyboard) {
        uint32_t roll = Next() % 16;
        if (roll < 4)
            g_clock += 40; // early: waits
        else if (roll < 9)
            g_clock += 100 + Next() % 40; // on time or slightly late
        else if (roll < 14)
            g_clock += 160 + Next() % 60; // late
        else if (roll == 14)
            g_clock += 149 + Next() % 3; // around 1.5 frames
        else
            g_clock += g_frame % 3 == 0 ? 2500 + Next() % 10 : 1990 + Next() % 30; // around / past 20 frames
    }
    if (tag == kTasks) {
        bool system = r.ecx == reinterpret_cast<uint32_t>(g_mlp) + 0x7C;
        if (system && g_frame % 7 == 0)
            SkipRender() = true;
        if (!system && g_frame % 11 == 0)
            SkipRender() = false;
        if (!system && g_frame == 450)
            TheManager()->m_done = 1;
        if (system && g_frame == 20)
            TheManager()->m_profileFrames = 1;
    }
    if (tag == kRender)
        Flag6F1158() = true; // (cleared again at the start of the next frame)
}

// The quit flag, set by the network service in frame 5, ends the loop before the frame's timing (m_done goes
// back to 0x33). A timer frequency over 2^32 (the frame length divides unsigned, 64-bit). In a level (the letterbox, 2,
// turned off); profiling left on from before (one frame).
void SetUpQuit()
{
    g_frequency = 0x100001770ll; // 71582888 ticks a frame
    g_screenSaverTimeout = 0;
    g_clock = 1000000000;
    g_qpcAdvance = 3;
    g_sleepAdvance = 1 << 24;
    TheManager()->m_done = 0x33;
    TheManager()->m_profiling = 1;
    GameVariable<uint8_t*>(0x007CE478) = g_skate;
    *reinterpret_cast<uint8_t**>(g_skate + 0x20) = g_career;
    g_career[0x630] = 5;
    Letterbox() = 2;
    SkippedInARow() = 3;
}
void ScriptQuit(uint32_t tag, Regs&)
{
    if (tag == kKeyboard)
        g_clock += 37000000ull * g_frame;
    if (tag == kNetwork && g_frame == 5)
        Quit() = true;
}

// The device lost (frames 1-3: still lost; reset fails; reset works), the window inactive (frame 8: idle in Sleep(100)
// until the third message pump makes it active; frames 10-11: inactive but text entry on - the frame runs, its
// render skipped, on time, even with six skipped in a row). Four skipped in a row to start with. The letterbox already on in the
// front end (level 0). Ends in frame 14 with m_done set by the system tasks.
void SetUpDevice()
{
    g_frequency = 12000; // 200 ticks a frame
    g_screenSaverTimeout = 1;
    g_clock = 777;
    g_qpcAdvance = 0;
    g_sleepAdvance = 31;
    TheManager()->m_done = 0;
    GameVariable<uint8_t*>(0x007CE478) = g_skate;
    *reinterpret_cast<uint8_t**>(g_skate + 0x20) = g_career;
    g_career[0x630] = 0;
    Letterbox() = 1;
    SkippedInARow() = 4;
}
void ScriptDevice(uint32_t tag, Regs& r)
{
    if (tag == kKeyboard) {
        // (frame 8 doesn't count: it idles and starts no frame time; frame 3 runs late)
        uint64_t frames = static_cast<uint64_t>(g_frame <= 8 ? g_frame - 1 : g_frame - 2);
        g_clock = 777 + frames * 200 + (g_frame == 3 ? 360 : 230);
        if (g_frame == 1)
            DeviceLost() = true;
        g_coop = g_frame == 1 ? D3DERR_DEVICELOST : g_frame == 2 ? D3DERR_DEVICENOTRESET : 0;
        g_reset = g_frame == 2 ? D3DERR_INVALIDCALL : 0;
    }
    if (tag == kNetwork && g_frame == 8) {
        AppActive() = false;
        g_eventsCalls = 0;
    }
    if (tag == kEvents && g_frame == 8 && g_eventsCalls == 4)
        AppActive() = true;
    if (tag == kNetwork && g_frame == 10)
        AppActive() = false, TextEntry() = true, SkippedInARow() = 6;
    if (tag == kNetwork && g_frame == 12)
        AppActive() = true, TextEntry() = false;
    if (tag == kTasks && g_frame == 14 && r.ecx == reinterpret_cast<uint32_t>(g_mlp) + 0x7C)
        TheManager()->m_done = 1;
}

// Frame times on the edges, exactly as the frame's start is kept (100 ticks a frame): 1.5 frames late (skipped),
// just under and over; exactly 20 frames late (no restart), then just over (restart). Ends in frame 8.
const uint32_t kEdgeElapsed[] = {150, 149, 151, 100, 2000, 2001, 150, 150};
void SetUpEdges()
{
    g_frequency = 6000;
    g_screenSaverTimeout = 0;
    g_clock = 1000;
    g_qpcAdvance = 0;
    g_sleepAdvance = 1;
    Letterbox() = 1;
}
void ScriptEdges(uint32_t tag, Regs& r)
{
    if (tag == kKeyboard && g_frame <= 8)
        g_clock = 1000 + static_cast<uint64_t>(g_frame - 1) * 100 + kEdgeElapsed[g_frame - 1] +
                  (g_frame >= 7 ? 2001 - 100 : 0); // (after the restart the frame's start is 2001 - 100 later)
    if (tag == kTasks && g_frame == 8 && r.ecx == reinterpret_cast<uint32_t>(g_mlp) + 4)
        TheManager()->m_done = 1;
}

// Quit asked for already, the window inactive with text entry on: one frame's start, then out.
void SetUpQuitAtOnce()
{
    g_frequency = 600;
    g_screenSaverTimeout = 5;
    g_clock = 5;
    g_qpcAdvance = 1;
    g_sleepAdvance = 1;
    TheManager()->m_done = 1;
    Quit() = true;
    AppActive() = false;
    TextEntry() = true;
}

void TestMainLoop()
{
    std::vector<Saved> saved = {{0x007D1EC0, 1, {}}, {0x006F1158, 1, {}}, {0x007CCBE4, 1, {}}, {0x007CCDF8, 1, {}},
                                {0x007D6A2C, 1, {}}, {0x00786AB4, 1, {}}, {0x007D69E0, 4, {}}, {0x00696D70, 4, {}},
                                {0x00696D74, 4, {}}, {0x007CE478, 4, {}}, {0x00786CBE, 1, {}}};
    SaveAll(saved);
    const Case cases[] = {{"timing", SetUpTiming, ScriptTiming},
                          {"quit", SetUpQuit, ScriptQuit},
                          {"device", SetUpDevice, ScriptDevice},
                          {"edges", SetUpEdges, ScriptEdges},
                          {"quit at once", SetUpQuitAtOnce, nullptr}};
    std::vector<uint32_t> traces[2];
    int stageCalls = 0;
    bool stageFirst = true;
    {
        auto* s = new StandIns;
        SetUpStandIns(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            std::vector<uint32_t> trace;
            // PartyMod's call at 0x0044F1C0: the render with its letterbox (Mlp::RenderFrameLetterboxed) in place
            // of the plain render
            if (original)
                RunPatch(0x0044F1C0, Branch(0xE8, 0x0044F1C0, reinterpret_cast<const void*>(Resolve(
                                                                  reinterpret_cast<const void*>(&Mlp::RenderFrameLetterboxed)))));
            for (const Case& c : cases) {
                std::memset(g_mlp, 0xEE, sizeof(g_mlp));
                std::memset(g_skate, 0, sizeof(g_skate));
                std::memset(g_career, 0, sizeof(g_career));
                TheManager()->m_profileFrames = 0;
                TheManager()->m_profiling = 0;
                g_device[0] = g_deviceVtable;
                GameVariable<void**>(0x007D69E0) = g_device;
                SkipRender() = true;
                Flag6F1158() = true;
                AppActive() = true;
                TextEntry() = false;
                Quit() = false;
                DeviceLost() = false;
                SkippedTotal() = 0;
                GameVariable<uint8_t*>(0x007CE478) = nullptr;
                g_frame = 0;
                g_eventsCalls = 0;
                g_sleeps = 0;
                g_coop = 0;
                g_reset = 0;
                g_lcg = 12345;
                g_script = nullptr;
                c.setup();
                g_script = c.script;
                g_stageCalls = 0;
                g_stageFirst = true;
                g_trace.clear();
                if (original)
                    Original<void(__thiscall*)(void*)>(0x0044EEF0)(g_mlp);
                else
                    TheManager()->MainLoop();
                g_script = nullptr;
                if (!original) {
                    stageCalls += g_stageCalls;
                    stageFirst &= g_stageFirst;
                }
                Record({0xE4D, State(), static_cast<uint32_t>(g_frame), static_cast<uint32_t>(g_clock),
                        static_cast<uint32_t>(g_clock >> 32), HashBytes(g_mlp, sizeof(g_mlp))});
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            EndRunPatches();
            traces[run] = trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1],
            "main loop: frame timing (waits, late frames, pauses), skipped renders, profiling, screen saver, device "
            "reset, inactive window, quit, letterbox");
    Check(stageCalls == 5 && stageFirst, "main loop: RunStageOnce(EngineStarted) first, once per call");
}

} // namespace

SELFTEST(Early, TestMainLoop, RemoveStandIns);
