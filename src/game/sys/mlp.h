#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/task.h"

// The main loop's task manager (Mlp::Manager, the singleton 0x0044F260 creates). Sys::Main hands it the engine's
// tasks and then runs its loop (MainLoop), which returns only when the game is quit or the loop is told to stop.
namespace Mlp {

struct Manager {
    uint8_t m_done;             // +0x00: (a bool) ends the loop after the current frame
    uint8_t pad01[3];
    Tsk::Stack m_logicTasks;     // +0x04: a task list (0x00406C90 adds, 0x004070A0 runs)
    Tsk::Stack m_displayTasks;   // +0x40 (not run by the loop)
    Tsk::Stack m_systemTasks;    // +0x7C: the module manager's and the input devices' tasks
    uint32_t m_unknownB8;       // +0xB8 (cleared by the constructor, otherwise unused here)
    uint32_t m_unknownBC;       // +0xBC (the same)
    uint8_t padC0[0x04];
    int m_profileFrames;        // +0xC4: frames still to profile (announced, counted down)
    int m_profiling;            // +0xC8: this frame is being profiled (passed to the task lists)

    // 0x0044EDC0: the three task lists (0x00406E80 each), the +0xB8/+0xBC words and m_done cleared. Returns this.
    Manager* Construct();
    // 0x0044EE30: the task lists destroyed (0x004070E0), the last first.
    void DestroyBody();
    // 0x0044EE90 (also reached by the jump at 0x0044F250): after the device was lost (0x00786AB4), once
    // TestCooperativeLevel no longer says D3DERR_DEVICELOST: the default-pool objects released, the device Reset
    // and, if that worked, the objects made again and the flag cleared. (Doesn't use a manager.)
    static void ResetDeviceIfLost();
    // 0x0044F220 (ProfileTasks): profile the next `frames` frames.
    void ProfileFrames(int frames);
    // 0x0044F240 (ResetEngine): end the main loop after this frame (Sys::Main then shuts down and starts again).
    void StopLoop();

    // 0x0044EEF0: the game's main loop - one frame per pass, at most 60 a second (QueryPerformanceCounter),
    // skipping the render of frames that run late, idling while the window is inactive and resetting the
    // device after it's lost. Returns when the game is quit (0x007D6A2C) or m_done is set; m_done is then put
    // back as it was on entry. PartyMod's letterbox per level is built in (RenderFrameLetterboxed).
    void MainLoop();
};
static_assert(offsetof(Manager, m_logicTasks) == 0x04);
static_assert(offsetof(Manager, m_displayTasks) == 0x40);
static_assert(offsetof(Manager, m_systemTasks) == 0x7C);
static_assert(offsetof(Manager, m_unknownB8) == 0xB8);
static_assert(offsetof(Manager, m_profileFrames) == 0xC4);
static_assert(offsetof(Manager, m_profiling) == 0xC8);
static_assert(sizeof(Manager) == 0xCC);

// NOT ORIGINAL: the main loop's passes so far (ours: features that need the game up - the front end's input logic
// also runs in the early self-tests, on stand-in state - wait for the first).
uint32_t& FramesRun();

// The singleton (0x00696D68, references 0x00696D6C). 0x0044F260: the first call with `create` makes it (0xCC
// bytes, zeroed, on the current heap), every call takes a reference and returns it (null if there is none).
Manager* CreateManager(bool create);
// 0x0044F2B0: drops a reference; the last one destroys and frees the manager.
void ReleaseManager();

// The frame's render (0x0048C330), then PartyMod's letterbox: on in the front end, off in a level (in place of
// the main loop's plain render call at 0x0044F1C0).
void RenderFrameLetterboxed();

} // namespace Mlp
