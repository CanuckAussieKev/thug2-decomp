// The game clock.

#include "game/gfx/engine.h"
#include "game/sys/timer.h"

#include <intrin.h>

#include <cstring>
#include <ctime>

#include "hook/game.h"
#include "hook/hook.h"

namespace Tmr {

namespace {

// The timing block (0x007D1E68), saved whole by Reset.
float& FrameLength() { return GameVariable<float>(0x007D1E68); }
double& UncappedFrameLength() { return GameVariable<double>(0x007D1E70); }
int (&History())[4] { return GameVariable<int[4]>(0x007D1E78); }
uint64_t& LastFrameVblank() { return GameVariable<uint64_t>(0x007D1E88); }
uint64_t& PreviousFrameVblank() { return GameVariable<uint64_t>(0x007D1E90); }
int& HistoryIndex() { return GameVariable<int>(0x007D1E98); }
constexpr uint32_t kTimingBlock = 0x007D1E68;
constexpr uint32_t kSavedTimingBlock = 0x007D1E30;
constexpr size_t kTimingBlockSize = 0x38;

uint64_t& StartTimeStamp() { return GameVariable<uint64_t>(0x007D1EA0); }
uint64_t& Unknown1EA8() { return GameVariable<uint64_t>(0x007D1EA8); }
uint64_t& StartClock() { return GameVariable<uint64_t>(0x007D1EB0); }
uint64_t& Unknown1EB8() { return GameVariable<uint64_t>(0x007D1EB8); }
uint64_t& Vblanks() { return GameVariable<uint64_t>(0x007D1EC8); } // since Reset
uint64_t& TotalVblanks() { return GameVariable<uint64_t>(0x007D1ED0); }
uint64_t& RenderFrame() { return GameVariable<uint64_t>(0x007D1ED8); }
uint64_t& Unknown1EE0() { return GameVariable<uint64_t>(0x007D1EE0); }
bool& Flag1EE8() { return GameVariable<bool>(0x007D1EE8); }
bool& Flag1EE9() { return GameVariable<bool>(0x007D1EE9); }
bool& RateSet() { return GameVariable<bool>(0x007D1EEA); }

int& RefreshRate() { return GameVariable<int>(0x00684FB4); } // 60
float& Slomo() { return GameVariable<float>(0x00684FC8); }
float& NextSlomo() { return GameVariable<float>(0x00684FCC); }
float& VblanksPerSecond() { return GameVariable<float>(0x00684FD0); }

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
