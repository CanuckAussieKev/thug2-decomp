#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The skater's run timer component (SkaterRunTimer, type 0x8BCE7E66, vtable 0x0064C04C, 0x28 bytes): the timer of a
// combo run - started by a combo (Physics_RunTimer_Duration), counting down while running (faster while the walk
// component hangs, Hang_Run_Timer_Speed_Adjustment), sending RunTimerUp to the skater when it's up; paused / resumed
// by the score's code and the scripts (each resume - up to 8 - caps the time left at a shrinking share of the
// duration), shown and hidden by ShowRunTimer / HideRunTimer to the run timer's script (spawn id 0x83321DEA + the
// skater's index). From the disassembly of 0x00518240-0x00518626 (0x00518240, 0x005184A0-0x005185C6 aren't Ghidra
// functions).
//
// Vtable: 0 Destroy, 1 Update, 2 the shared 0x004FBDA0, 3 Finalize, 10 FetchDiagnostics, 11 the shared 0x0042BB20, 12
// the shared InvokeMethodByName 0x0051ADC0.
namespace GameObjects {

class CRiderRunClock : public CObjectModule {
public:
    enum State : int { CLOCK_IDLE = 0, CLOCK_TICKING = 1, ACTIVE_PAUSED = 2, ACTIVE_TIME_UP = 3 };

    // 0x005185D0 (registered by RegisterComponentTypes): a new one (0 if out of memory). Nothing else is set.
    static CObjectModule* __cdecl Create();
    // 0x00518240 (registered with Create): its script member functions (the table at 0x006849C0) registered.
    static void __cdecl PublishMethodTable();
    // 0x00518260: the destructor body (the base's).
    void DestroyBody();
    // 0x005182B0 (vtable slot 0)
    CRiderRunClock* Destroy(unsigned int flags);
    // 0x005182E0 (vtable +0x0C): the walk component found.
    void Finalize();
    // 0x00518300 (vtable +0x04): running, the time left goes down by the frame's time (scaled while the walk's state
    // is 2-4); below 0 it's up: RunTimerUp to the skater - and again every frame while it's up.
    void Update();
    // 0x00518270 (vtable +0x28): m_state (the state's name) and m_timer added to `params`, then the base's (empty).
    void FetchDiagnostics(Scripts::CStruct* params);

    // 0x00518390: stopped (inactive); HideRunTimer to the run timer's script if it's running.
    void Stop();
    // 0x005183F0: resumed: running if there's time left, else up; the first 8 resumes cap the time left at the
    // duration times (8 - resumes) / 8; ShowRunTimer.
    void Resume();

    // --- script member functions (the table at 0x006849C0; params, script; all true) ---
    bool Pause(Scripts::CStruct* params, Scripts::CScript* script);                       // 0x005184A0 RunTimer_Pause
    bool Unpause(Scripts::CStruct* params, Scripts::CScript* script);                     // 0x005184F0 RunTimer_unpause
    bool GetFactorComplete(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00518500 (FactorComplete)
    bool GetRunTimerControllerId(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00518540
    bool GetRunTimerId(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00518570
    bool ChainBegan(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005185A0

    int m_state;           // +0x18 State
    float m_secondsRemaining;      // +0x1C seconds
    int16_t m_resumes;     // +0x20
    uint8_t pad22[2];
    uint8_t* m_walk;       // +0x24 Walk (0x726E85AA)
};
static_assert(offsetof(CRiderRunClock, m_state) == 0x18 &&
                  offsetof(CRiderRunClock, m_secondsRemaining) == 0x1C &&
                  offsetof(CRiderRunClock, m_resumes) == 0x20 &&
                  offsetof(CRiderRunClock, m_walk) == 0x24 && sizeof(CRiderRunClock) == 0x28,
              "layout must match the original (0x005185D0 allocates 0x28 bytes)");

} // namespace Obj
