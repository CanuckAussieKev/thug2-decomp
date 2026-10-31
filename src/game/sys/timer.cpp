// The game clock.

#include "game/gfx/engine.h"
#include "game/sys/timer.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <ctime>

#include "game/sys/game_config.h"

namespace Tmr {

namespace {

float g_slomo{1.0f};
float g_nextSlomo{1.0f};
float g_vblanksPerSecond{60.0f};
float g_frameLength;
double g_uncappedFrameLength;
uint64_t g_lastFrameVblank;
uint64_t g_previousFrameVblank;
int g_historyIndex;
uint64_t g_startTimeStamp;
uint64_t g_unknown1EA8;
uint64_t g_startClock;
uint64_t g_unknown1EB8;
uint64_t g_vblanks;
uint64_t g_totalVblanks;
uint64_t g_renderFrame;
uint64_t g_unknown1EE0;
bool g_flag1EE8;
bool g_flag1EE9;
bool g_rateSet;

// The timing block (0x007D1E68), saved whole by Reset.
float& FrameLength() { return g_frameLength; }
double& UncappedFrameLength() { return g_uncappedFrameLength; }

int g_history[4];

int (&History())[4] { return g_history; }
uint64_t& LastFrameVblank() { return g_lastFrameVblank; }
uint64_t& PreviousFrameVblank() { return g_previousFrameVblank; }
int& HistoryIndex() { return g_historyIndex; }
// The timing block's copy Reset keeps (0x38 bytes, only ever written).
struct TimingBlock {
    float frameLength;
    uint32_t pad;
    double uncappedFrameLength;
    int history[4];
    uint64_t lastFrameVblank;
    uint64_t previousFrameVblank;
    int historyIndex;
};
static_assert(sizeof(TimingBlock) == 0x38, "the timing block's size");
TimingBlock g_savedTimingBlock;

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
    g_savedTimingBlock.frameLength = FrameLength();
    g_savedTimingBlock.uncappedFrameLength = UncappedFrameLength();
    std::memcpy(g_savedTimingBlock.history, History(), sizeof(g_savedTimingBlock.history));
    g_savedTimingBlock.lastFrameVblank = LastFrameVblank();
    g_savedTimingBlock.previousFrameVblank = PreviousFrameVblank();
    g_savedTimingBlock.historyIndex = HistoryIndex();
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
