#pragma once

#include <cstdint>

// The game clock (Neversoft's Tmr): counted in vblanks (60 a second on PC), with the render frame count, an
// average frame length over the last four frames, and slow motion.
namespace Tmr {

// 0x005BEA50: start the clock (also notes the C runtime clock and the CPU's time stamp counter).
void Init();
// 0x005BE9B0: back to frame 0 with a one-vblank frame length (the vblank rate is set the first time).
void Reset();

// 0x005BE700: a vblank has passed (counted since Reset, and in total).
void CountVblank();
// 0x005BE740: a frame has been rendered: its length in vblanks goes into the four-frame history.
void OncePerRender();
// 0x005BE7F0: once a frame: the frame length from the history (each entry 0-4 vblanks; outside 0-10 counts
// as 1; at least 1), the uncapped one, and the slow motion asked for since the last frame.
void UpdateFrameLength();
// 0x005BE920: wait for the next vblank, running the idle function (if any) meanwhile.
void WaitForVblank();

// 0x005BE8E0 / 0x005BE8D0: vblanks in total; frames rendered.
uint64_t GetVblanks();
uint64_t GetRenderFrame();
// 0x005BE970: milliseconds since Reset (from the low 32 bits of the vblank count).
uint32_t GetTime();
// 0x005BE910: the uncapped average frame length, in seconds.
double GetFrameSeconds();
// 0x005BEAA0: slow motion times a nominal 1/60 s frame.
double GetSlomoFrameSeconds();
// 0x005BE8F0 / 0x005BE900: slow motion (1 = normal speed); a new value takes effect at the next frame.
float GetSlomo();
void SetSlomo(float slomo);

// 0x005BE6F0: the CPU's time stamp counter.
uint64_t ReadCpuTimeStamp();
// 0x005BE7C0: sets the two flags at 0x007D1EE8 / 0x007D1EE9 (meaning not known yet).
void SetFlags();

} // namespace Tmr
