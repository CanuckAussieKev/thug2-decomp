// The main loop (Mlp::Manager), with PartyMod's per-level letterbox built in.
// (The letterbox per level contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md)

#include "game/sys/mlp.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/level_editor_ui.h"
#include "game/gfx/screen.h"
#include "game/sio/device.h"
#include "game/sys/platform.h"
#include "game/sys/screenshot.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"

namespace Mlp {

uint32_t& FramesRun()
{
    static uint32_t frames;
    return frames;
}

namespace {

// Set for a frame that isn't drawn (running late, the window inactive, the device just reset): the scene isn't
// begun or presented.
bool& SkipRender() { return GameVariable<bool>(0x007D1EC0); }
bool& Flag6F1158() { return GameVariable<bool>(0x006F1158); } // cleared every frame (read by the pad code)
bool AppActive() { return GameVariable<bool>(0x007CCBE4); }
bool TextEntry() { return GameVariable<bool>(0x007CCDF8); } // keeps the loop running while the window is inactive
bool QuitRequested() { return GameVariable<bool>(0x007D6A2C); }
bool& DeviceLost() { return GameVariable<bool>(0x00786AB4); }
IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
D3DPRESENT_PARAMETERS* PresentParameters() { return &GameVariable<D3DPRESENT_PARAMETERS>(0x00786A7C); }
// Frames in a row whose render was skipped (after four, one is drawn regardless), and in all.
int& SkippedInARow() { return GameVariable<int>(0x00696D74); }
int& SkippedTotal() { return GameVariable<int>(0x00696D70); }

void ServiceNetwork() { GameFunction<void(__cdecl*)()>(0x00536640)(); }
// Around a device Reset: the device objects released first (default pool) and made again after.
void ReleaseDeviceObjects()
{
    GameFunction<void(__cdecl*)()>(0x004E0320)();
    GameFunction<void(__cdecl*)()>(0x004D67F0)();
    GameFunction<void(__cdecl*)()>(0x004D7AF0)();
}
void RestoreDeviceObjects()
{
    GameFunction<void(__cdecl*)()>(0x004E0440)();
    GameFunction<void(__cdecl*)()>(0x004D68C0)();
    GameFunction<void(__cdecl*)()>(0x004D8420)();
}
// A task list's tasks, in order (0x004070A0).
void RunTasks(uint8_t* list, bool profiling)
{
    GameFunction<void(__thiscall*)(uint8_t*, bool)>(0x004070A0)(list, profiling);
}
void BeginScene() { GameFunction<void(__cdecl*)()>(0x0048BF10)(); }      // the device's BeginScene
void RenderFrame() { GameFunction<void(__cdecl*)()>(0x0048C330)(); }     // the world, then the 2D
void AfterRender() { GameFunction<void(__cdecl*)()>(0x00565DA0)(); }     // (uses the skate module)
void EndScene() { GameFunction<void(__cdecl*)()>(0x0048BF20)(); }        // EndScene, Present

uint8_t LetterboxActive() { return GameVariable<uint8_t>(0x00786CBE); }

} // namespace

// The frame's render with PartyMod's letterbox (PartyMod's renderWorldWrapper, at 0x0044F1C0): letterboxed
// (4:3) in the front end, not in a level (split screen breaks with it on: both screens drawn over each other,
// sharing a depth buffer). The original only rendered: RenderFrame();
void RenderFrameLetterboxed()
{
    RenderFrame();
    if (Nx::CurrentLevel() == 0) {
        if (LetterboxActive() != 1)
            Nx::SetLetterbox(true);
    } else if (LetterboxActive() != 0) {
        Nx::SetLetterbox(false);
    }
}

namespace {

// The task lists (Lst-style lists of tasks, 0x3C bytes each).
void ConstructTaskList(uint8_t* list) { GameFunction<void*(__thiscall*)(uint8_t*)>(0x00406E80)(list); }
void DestroyTaskList(uint8_t* list) { GameFunction<void(__thiscall*)(uint8_t*)>(0x004070E0)(list); }

Manager*& Instance() { return GameVariable<Manager*>(0x00696D68); }
uint32_t& References() { return GameVariable<uint32_t>(0x00696D6C); }

} // namespace

// 0x0044EDC0
Manager* Manager::Construct()
{
    ConstructTaskList(m_logicTasks);
    ConstructTaskList(m_displayTasks);
    ConstructTaskList(m_systemTasks);
    m_unknownB8 = 0;
    m_unknownBC = 0;
    m_done = false;
    return this;
}

// 0x0044EE30
void Manager::DestroyBody()
{
    DestroyTaskList(m_systemTasks);
    DestroyTaskList(m_displayTasks);
    DestroyTaskList(m_logicTasks);
}

// 0x0044EE90
void Manager::ResetDeviceIfLost()
{
    if (!DeviceLost())
        return;
    if (Device()->TestCooperativeLevel() == D3DERR_DEVICELOST) // (not yet: it can't be reset)
        return;
    // Not original - the level editor's UI (game/front/level_editor_ui.h): ImGui's default-pool objects dropped before
    // the reset (made again on its next frame).
    // Original behaviour: only the game's own.
    Front::LevelEditor::Ui::DeviceLost();
    ReleaseDeviceObjects();
    if (Device()->Reset(PresentParameters()) < 0)
        return;
    RestoreDeviceObjects();
    DeviceLost() = false;
}

// 0x0044F220
void Manager::ProfileFrames(int frames)
{
    m_profileFrames = frames;
    m_profiling = 0;
}

// 0x0044F240
void Manager::StopLoop() { m_done = true; }

// 0x0044F260
Manager* CreateManager(bool create)
{
    if (!Instance() && create) {
        if (void* memory = Mem::Calloc(sizeof(Manager), nullptr, 1)) {
            Instance() = static_cast<Manager*>(memory)->Construct();
            ++References();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++References();
    return Instance();
}

// 0x0044F2B0
void ReleaseManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (Manager* manager = Instance()) {
        manager->DestroyBody();
        Mem::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

// 0x0044EEF0
void Manager::MainLoop()
{
    // The test runner's EngineStarted stage: every engine module exists, the first frame is next.
    RunStageOnce(TestStage::EngineStarted);

    uint8_t doneOnEntry = m_done;
    // The screen saver's timeout is set again every 201 frames, which restarts its idle time.
    UINT screenSaverTimeout = 0;
    SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &screenSaverTimeout, 0);
    uint32_t framesSinceScreenSaver = 0;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    uint64_t frameTicks = static_cast<uint64_t>(frequency.QuadPart) / 60; // (an unsigned division)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    uint64_t frameStart = static_cast<uint64_t>(counter.QuadPart);
    m_done = false;
    do {
        ++FramesRun(); // (not original - see FramesRun)
        if (screenSaverTimeout && framesSinceScreenSaver++ >= 200) {
            SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screenSaverTimeout, nullptr, 0);
            framesSinceScreenSaver = 0;
        }
        SkipRender() = false;
        Flag6F1158() = false;
        Sio::ReadKeyboard();
        Platform::ProcessWindowEvents();
        ServiceNetwork();
        if (!AppActive()) {
            if (!TextEntry()) {
                // Inactive: idle until the window is active again (nothing else runs meanwhile).
                do {
                    Sleep(100);
                    Platform::ProcessWindowEvents();
                } while (!AppActive());
                continue;
            }
            SkipRender() = true;
        }
        if (QuitRequested())
            break;
        if (DeviceLost()) {
            ResetDeviceIfLost(); // (inline in the original)
            SkipRender() = true;
        }

        // Wait for the frame's time (60 a second).
        QueryPerformanceCounter(&counter);
        uint64_t now = static_cast<uint64_t>(counter.QuadPart);
        while (now < frameStart + frameTicks) {
            Sleep(0);
            QueryPerformanceCounter(&counter);
            now = static_cast<uint64_t>(counter.QuadPart);
        }
        uint64_t elapsed = now - frameStart;
        if (elapsed > frameTicks * 20) { // far behind (a pause): start counting again from one frame ago
            frameStart = now - frameTicks;
            elapsed = frameTicks;
        }
        // A frame more than half a frame late isn't drawn - but never more than four in a row, and never while
        // the window is inactive.
        bool active = AppActive();
        if (SkippedInARow() >= 4 && active) {
            SkippedInARow() = 0;
        } else if (elapsed < frameTicks + (frameTicks >> 1) && active) {
            SkippedInARow() = 0;
        } else {
            ++SkippedTotal();
            SkipRender() = true;
            ++SkippedInARow();
        }
        frameStart += frameTicks;

        Tmr::CountVblank();
        Tmr::OncePerRender();
        if (m_profileFrames) {
            Dbg::Printf(reinterpret_cast<const char*>(0x0064725C)); // "\nProfiling.....Start of main loop\n\n"
            --m_profileFrames;
            m_profiling = 1;
        }
        RunTasks(m_systemTasks, m_profiling != 0);
        RunTasks(m_logicTasks, m_profiling != 0);
        if (!SkipRender())
            BeginScene();
        RenderFrameLetterboxed();
        AfterRender();
        // Not original - the screenshot key (game/sys/screenshot.h): the frame read just before it's presented.
        // Original behaviour: the scene is ended and presented.
        Sys::Screenshot::BeforePresent(!SkipRender());
        if (!SkipRender())
            EndScene();
        Tmr::UpdateFrameLength();
        m_profiling = 0;
    } while (!m_done);
    m_done = doneOnEntry;
}

} // namespace Mlp

REPLACE(0x0044EDC0, &Mlp::Manager::Construct);
REPLACE(0x0044EE30, &Mlp::Manager::DestroyBody);
REPLACE(0x0044EE90, Mlp::Manager::ResetDeviceIfLost);
REPLACE_SOURCE_ONLY(0x0044F250, Mlp::Manager::ResetDeviceIfLost); // a jump to 0x0044EE90
REPLACE(0x0044EEF0, &Mlp::Manager::MainLoop);
REPLACE(0x0044F220, &Mlp::Manager::ProfileFrames);
REPLACE_SOURCE_ONLY(0x0044F240, &Mlp::Manager::StopLoop); // (4 bytes: too short to hook)
REPLACE(0x0044F260, Mlp::CreateManager);
REPLACE(0x0044F2B0, Mlp::ReleaseManager);
