#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/task_list.h"

// The main loop's task manager (frame_loop::Hub, the singleton 0x0044F260 creates). sys::Main hands it the engine's
// tasks and then runs its loop (MainLoop), which returns only when the game is quit or the loop is told to stop.
namespace frame_loop {

struct Hub {
    uint8_t is_finished_;             // +0x00: (a bool) ends the loop after the current frame
    uint8_t pad01[3];
    tasks::PileUp logic_tasks_;     // +0x04: a task list (0x00406C90 adds, 0x004070A0 runs)
    tasks::PileUp display_tasks_;   // +0x40 (not run by the loop)
    tasks::PileUp system_tasks_;    // +0x7C: the module manager's and the input devices' tasks
    uint32_t unknownb8_;       // +0xB8 (cleared by the constructor, otherwise unused here)
    uint32_t unknownbc_;       // +0xBC (the same)
    uint8_t padc0_[0x04];
    int profile_frames_;        // +0xC4: frames still to profile (announced, counted down)
    int profiling_;            // +0xC8: this frame is being profiled (passed to the task lists)

    // 0x0044EDC0: the three task lists (0x00406E80 each), the +0xB8/+0xBC words and is_finished_ cleared. Returns this.
    Hub* make_new();
    // 0x0044EE30: the task lists destroyed (0x004070E0), the last first.
    void destroy_body();
    // 0x0044EE90 (also reached by the jump at 0x0044F250): after the device was lost (0x00786AB4), once
    // TestCooperativeLevel no longer says D3DERR_DEVICELOST: the default-pool objects released, the device Reset
    // and, if that worked, the objects made again and the flag cleared. (Doesn't use a manager.)
    static void reset_device_if_lost();
    // 0x0044F220 (ProfileTasks): profile the next `frames` frames.
    void profile_frames(int frames);
    // 0x0044F240 (ResetEngine): end the main loop after this frame (sys::Main then shuts down and starts again).
    void stop_loop();

    // 0x0044EEF0: the game's main loop - one frame per pass, at most 60 a second (QueryPerformanceCounter),
    // skipping the render of frames that run late, idling while the window is inactive and resetting the
    // device after it's lost. Returns when the game is quit (0x007D6A2C) or is_finished_ is set; is_finished_ is then put
    // back as it was on entry. PartyMod's letterbox per level is built in (render_frame_letterboxed).
    void get_main_loop();
};
static_assert(offsetof(Hub, logic_tasks_) == 0x04);
static_assert(offsetof(Hub, display_tasks_) == 0x40);
static_assert(offsetof(Hub, system_tasks_) == 0x7C);
static_assert(offsetof(Hub, unknownb8_) == 0xB8);
static_assert(offsetof(Hub, profile_frames_) == 0xC4);
static_assert(offsetof(Hub, profiling_) == 0xC8);
static_assert(sizeof(Hub) == 0xCC);

// NOT ORIGINAL: the main loop's passes so far (ours: features that need the game up - the front end's input logic
// also runs in the early self-tests, on stand-in state - wait for the first).
uint32_t& frames_run();

// The singleton (0x00696D68, references 0x00696D6C). 0x0044F260: the first call with `create` makes it (0xCC
// bytes, zeroed, on the current heap), every call takes a reference and returns it (null if there is none).
Hub* create_manager(bool create);
// 0x0044F2B0: drops a reference; the last one destroys and frees the manager.
void release_manager();

// The frame's render (0x0048C330), then PartyMod's letterbox: on in the front end, off in a level (in place of
// the main loop's plain render call at 0x0044F1C0).
void render_frame_letterboxed();

} // namespace Mlp
