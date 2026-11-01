#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's run timer component (SkaterRunTimer, type 0x8BCE7E66, vtable 0x0064C04C, 0x28 bytes): the timer of a
// combo run - started by a combo (Physics_RunTimer_Duration), counting down while running (faster while the walk
// component hangs, Hang_Run_Timer_Speed_Adjustment), sending RunTimerUp to the skater when it's up; paused / resumed
// by the score's code and the scripts (each resume - up to 8 - caps the time left at a shrinking share of the
// duration), shown and hidden by ShowRunTimer / HideRunTimer to the run timer's script (spawn id 0x83321DEA + the
// skater's index). From the disassembly of 0x00518240-0x00518626 (0x00518240, 0x005184A0-0x005185C6 aren't Ghidra
// functions).
//
// Vtable: 0 Destroy, 1 Update, 2 the shared 0x004FBDA0, 3 Finalize, 10 fetch_diagnostics, 11 the shared 0x0042BB20, 12
// the shared invoke_method_by_name 0x0051ADC0.
namespace objects {

class RiderRunClock final : public ObjectModule {
public:
    RiderRunClock(const RiderRunClock&) = delete;
    RiderRunClock& operator=(const RiderRunClock&) = delete;
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
    enum State : uint8_t { kClockIdle = 0, kClockTicking = 1, kActivePaused = 2, kActiveTimeUp = 3 };

    // 0x005185D0 (registered by register_component_types): a new one (0 if out of memory). Nothing else is set.
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderRunClock();
    // 0x00518240 (registered with Create): its script member functions (the table at 0x006849C0) registered.
    static void publish_method_table();
    // 0x00518260: the destructor body (the base's).
    ~RiderRunClock() override;
    // 0x004FBDA0 (slot 2, the cleanup component's, folded: its +0x18 cleared): state 0.
    void configure_from_data(script::Struct* /*params*/) override { cur_phase_ = 0; }
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x005182E0 (vtable +0x0C): the walk component found.
    void seal() override;
    // 0x00518300 (vtable +0x04): running, the time left goes down by the frame's time (scaled while the walk's state
    // is 2-4); below 0 it's up: RunTimerUp to the skater - and again every frame while it's up.
    void tick_frame() override;
    // 0x00518270 (vtable +0x28): cur_phase_ (the state's name) and stopwatch_ added to `params`, then the base's (empty).
    void fetch_diagnostics(script::Struct* params) override;

    // 0x00518390: stopped (inactive); HideRunTimer to the run timer's script if it's running.
    void halt();
    // 0x005183F0: resumed: running if there's time left, else up; the first 8 resumes cap the time left at the
    // duration times (8 - resumes) / 8; ShowRunTimer.
    void get_resume();

    // --- script member functions (the table at 0x006849C0; params, script; all true) ---
    bool get_freeze(script::Struct* params, script::Script* script);             // 0x005184A0 RunTimer_Pause
    bool unpause(script::Struct* params, script::Script* script);                // 0x005184F0 RunTimer_unpause
    bool get_factor_complete(script::Struct* params, script::Script* script);    // 0x00518500 (FactorComplete)
    bool fetch_run_clock_pad_id(script::Struct* params, script::Script* script); // 0x00518540
    bool get_run_timer_id(script::Struct* params, script::Script* script);       // 0x00518570
    bool chain_began(script::Struct* params, script::Script* script);            // 0x005185A0

    int cur_phase_{};           // +0x18 State
    float seconds_remaining_{}; // +0x1C seconds
    int16_t resumes_{};         // +0x20
    std::array<uint8_t, 2> pad22{};
    uint8_t* walk_{}; // +0x24 Walk (0x726E85AA)
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<script::MemberFunctionEntry, 7> g_run_timer_member_functions;
} // namespace objects
