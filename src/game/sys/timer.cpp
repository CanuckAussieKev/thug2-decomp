// The game clock.

#include "game/gfx/engine.h"
#include "game/sys/timer.h"

#include <intrin.h>

#include <cstring>
#include <ctime>

#include "game/sys/game_config.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Tmr {

namespace {

GAME_GLOBAL(float, g_slomo, 0x00684FC8, 1.0f);
GAME_GLOBAL(float, g_nextSlomo, 0x00684FCC, 1.0f);
GAME_GLOBAL(float, g_vblanksPerSecond, 0x00684FD0, 60.0f);
GAME_GLOBAL(float, g_frameLength, 0x007D1E68);
GAME_GLOBAL(double, g_uncappedFrameLength, 0x007D1E70);
GAME_GLOBAL(uint64_t, g_lastFrameVblank, 0x007D1E88);
GAME_GLOBAL(uint64_t, g_previousFrameVblank, 0x007D1E90);
GAME_GLOBAL(int, g_historyIndex, 0x007D1E98);
GAME_GLOBAL(uint64_t, g_startTimeStamp, 0x007D1EA0);
GAME_GLOBAL(uint64_t, g_unknown1EA8, 0x007D1EA8);
GAME_GLOBAL(uint64_t, g_startClock, 0x007D1EB0);
GAME_GLOBAL(uint64_t, g_unknown1EB8, 0x007D1EB8);
GAME_GLOBAL(uint64_t, g_vblanks, 0x007D1EC8);
GAME_GLOBAL(uint64_t, g_totalVblanks, 0x007D1ED0);
GAME_GLOBAL(uint64_t, g_renderFrame, 0x007D1ED8);
GAME_GLOBAL(uint64_t, g_unknown1EE0, 0x007D1EE0);
GAME_GLOBAL(bool, g_flag1EE8, 0x007D1EE8);
GAME_GLOBAL(bool, g_flag1EE9, 0x007D1EE9);
GAME_GLOBAL(bool, g_rateSet, 0x007D1EEA);

// The timing block (0x007D1E68), saved whole by Reset.
float& FrameLength() { return g_frameLength; }
double& UncappedFrameLength() { return g_uncappedFrameLength; }

GAME_GLOBAL(int[4], g_history, 0x007D1E78);

int (&History())[4] { return g_history; }
uint64_t& LastFrameVblank() { return g_lastFrameVblank; }
uint64_t& PreviousFrameVblank() { return g_previousFrameVblank; }
int& HistoryIndex() { return g_historyIndex; }
constexpr uint32_t kTimingBlock = 0x007D1E68;
constexpr uint32_t kSavedTimingBlock = 0x007D1E30;
constexpr size_t kTimingBlockSize = 0x38;

uint64_t& StartTimeStamp() { return g_startTimeStamp; }
uint64_t& Unknown1EA8() { return g_unknown1EA8; }
uint64_t& StartClock() { return g_startClock; }
uint64_t& Unknown1EB8() { return g_unknown1EB8; }
uint64_t& Vblanks() { return g_vblanks; } // since Reset
uint64_t& TotalVblanks() { return g_totalVblanks; }
uint64_t& RenderFrame() { return g_renderFrame; }
uint64_t& Unknown1EE0() { return g_unknown1EE0; }
bool& Flag1EE8() { return g_flag1EE8; }
bool& Flag1EE9() { return g_flag1EE9; }
bool& RateSet() { return g_rateSet; }

int& RefreshRate() { return GameConfig::g_frameRate; } // 60
float& Slomo() { return g_slomo; }
float& NextSlomo() { return g_nextSlomo; }
float& VblanksPerSecond() { return g_vblanksPerSecond; }

// A history entry's weight: 0-4 vblanks; anything outside 0-10 (a stall) counts as 1.
int Clamped(int vblanks)
{
    if (vblanks > 10 || vblanks < 0)
        return 1;
    return vblanks > 4 ? 4 : vblanks;
}

} // namespace

// 0x005BE6F0
uint64_t ReadCpuTimeStamp() { return __rdtsc(); }

// 0x005BE9B0
void Reset()
{
    if (!RateSet()) {
        VblanksPerSecond() = static_cast<float>(RefreshRate());
        RateSet() = true;
    }
    UncappedFrameLength() = 1.0;
    for (int& h : History())
        h = 1;
    FrameLength() = 1.0f;
    LastFrameVblank() = 0;
    PreviousFrameVblank() = 0;
    HistoryIndex() = 0;
    std::memcpy(reinterpret_cast<void*>(kSavedTimingBlock), reinterpret_cast<void*>(kTimingBlock), kTimingBlockSize);
    Vblanks() = 0;
    Unknown1EE0() = 0;
}

// 0x005BEA50
void Init()
{
    Reset();
    Unknown1EB8() = 0;
    StartClock() = static_cast<uint32_t>(std::clock());
    StartTimeStamp() = ReadCpuTimeStamp();
    Unknown1EA8() = 0;
}

// 0x005BE700
void CountVblank()
{
    ++Vblanks();
    ++TotalVblanks();
}

// 0x005BE740
void OncePerRender()
{
    ++RenderFrame();
    PreviousFrameVblank() = LastFrameVblank();
    LastFrameVblank() = Vblanks();
    History()[HistoryIndex()] = static_cast<int>(static_cast<uint32_t>(Vblanks()) -
                                                 static_cast<uint32_t>(PreviousFrameVblank()));
    if (++HistoryIndex() == 4)
        HistoryIndex() = 0;
}

// 0x005BE7F0
void UpdateFrameLength()
{
    const int(&h)[4] = History();
    int clamped = Clamped(h[0]) + Clamped(h[1]) + Clamped(h[2]) + Clamped(h[3]);
    int raw = static_cast<int>(static_cast<uint32_t>(h[0]) + h[1] + h[2] + h[3]); // (wraps, as the original)
    Slomo() = NextSlomo();
    FrameLength() = static_cast<float>(clamped * 0.25f);
    if (clamped * 0.25 < 1.0)
        FrameLength() = 1.0f;
    UncappedFrameLength() = raw * 0.25;
}

// 0x005BE920
void WaitForVblank()
{
    uint64_t start = TotalVblanks();
    while (TotalVblanks() == start) {
        // the idle function (0x0048C1C0 reads it from 0x006FA7D8)
        if (Nx::Engine::GetIdleFunction())
            Nx::Engine::GetIdleFunction()();
    }
}

// 0x005BE8E0
uint64_t GetVblanks() { return TotalVblanks(); }

// 0x005BE8D0
uint64_t GetRenderFrame() { return RenderFrame(); }

// 0x005BE970 (x87: 1000 / rate * the low 32 bits, truncated through 64 bits)
uint32_t GetTime()
{
    double milliseconds = (1000.0 / VblanksPerSecond()) * static_cast<double>(static_cast<uint32_t>(Vblanks()));
    return static_cast<uint32_t>(static_cast<int64_t>(milliseconds));
}

// 0x005BE910
double GetFrameSeconds() { return UncappedFrameLength() / VblanksPerSecond(); }

// 0x005BEAA0 (the original leaves the unrounded product on the x87 stack)
double GetSlomoFrameSeconds() { return Slomo() * static_cast<double>(1.0f / 60.0f); }

// 0x005BE8F0
float GetSlomo() { return Slomo(); }

// 0x005BE900
void SetSlomo(float slomo) { NextSlomo() = slomo; }

// 0x005BE7C0
void SetFlags()
{
    if (!Flag1EE8())
        Flag1EE8() = true;
    if (!Flag1EE9())
        Flag1EE9() = true;
}

} // namespace Tmr

REPLACE_SOURCE_ONLY(0x005BE6F0, Tmr::ReadCpuTimeStamp); // 3 bytes
REPLACE(0x005BE9B0, Tmr::Reset);
REPLACE(0x005BEA50, Tmr::Init);
REPLACE(0x005BE700, Tmr::CountVblank);
REPLACE(0x005BE740, Tmr::OncePerRender);
REPLACE(0x005BE7F0, Tmr::UpdateFrameLength);
REPLACE(0x005BE920, Tmr::WaitForVblank);
REPLACE(0x005BE8E0, Tmr::GetVblanks);
REPLACE(0x005BE8D0, Tmr::GetRenderFrame);
REPLACE(0x005BE970, Tmr::GetTime);
REPLACE(0x005BE910, Tmr::GetFrameSeconds);
REPLACE(0x005BEAA0, Tmr::GetSlomoFrameSeconds);
REPLACE(0x005BE8F0, Tmr::GetSlomo);
REPLACE(0x005BE900, Tmr::SetSlomo);
REPLACE(0x005BE7C0, Tmr::SetFlags);
