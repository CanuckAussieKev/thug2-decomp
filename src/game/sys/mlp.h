#pragma once

#include <cstddef>
#include <cstdint>

// The main loop's task manager (Mlp::Manager, the singleton 0x0044F260 creates). Sys::Main hands it the engine's
// tasks and then runs its loop (MainLoop), which returns only when the game is quit or the loop is told to stop.
namespace Mlp {

struct Manager {
    uint8_t m_done;             // +0x00: (a bool) ends the loop after the current frame
    uint8_t pad01[3];
    uint8_t m_logicTasks[0x3C];   // +0x04: a task list (0x00406C90 adds, 0x004070A0 runs)
    uint8_t m_displayTasks[0x3C]; // +0x40 (not run by the loop)
    uint8_t m_systemTasks[0x3C];  // +0x7C: the module manager's and the input devices' tasks
    uint8_t padB8[0x0C];
    int m_profileFrames;        // +0xC4: frames still to profile (announced, counted down)
    int m_profiling;            // +0xC8: this frame is being profiled (passed to the task lists)

    // 0x0044EEF0: the game's main loop - one frame per pass, at most 60 a second (QueryPerformanceCounter),
    // skipping the render of frames that run late, idling while the window is inactive and resetting the
    // device after it's lost. Returns when the game is quit (0x007D6A2C) or m_done is set; m_done is then put
    // back as it was on entry. PartyMod's renderWorldWrapper (the letterbox per level) is built in.
    void MainLoop();
};
static_assert(offsetof(Manager, m_logicTasks) == 0x04);
static_assert(offsetof(Manager, m_displayTasks) == 0x40);
static_assert(offsetof(Manager, m_systemTasks) == 0x7C);
static_assert(offsetof(Manager, m_profileFrames) == 0xC4);
static_assert(offsetof(Manager, m_profiling) == 0xC8);

} // namespace Mlp
