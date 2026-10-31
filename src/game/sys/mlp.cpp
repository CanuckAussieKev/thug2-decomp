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
#include "game/front/new_frontend.h"
#include "game/gfx/device.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/mesh.h"
#include "game/gfx/texture.h"
#include "game/gfx/screen.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/net/thugpro_compat.h"
#include "game/skate/skate_module.h"
#include "game/skate/thugpro_cas.h"
#include "game/sio/device.h"
#include "game/sys/platform.h"
#include "game/sys/screenshot.h"
#include "game/sys/timer.h"
#include "game/sys/winmain.h"
#include "game/obj/cutscene.h"
#include "game/gfx/movie.h"
#include "game/ass/assman.h"
#include "game/gfx/render.h"

namespace Mlp {

uint32_t& FramesRun()
{
    static uint32_t frames;
    return frames;
}

namespace {

// Set for a frame that isn't drawn (running late, the window inactive, the device just reset): the scene isn't
// begun or presented.
bool& SkipRender() { return Nx::g_skipRender; }
bool& Flag6F1158() { return Obj::g_cutscenePlaying; } // cleared every frame (read by the pad code)
bool AppActive() { return Sys::g_appActive; }
bool TextEntry() { return GameNet::g_textEntry; } // keeps the loop running while the window is inactive
bool QuitRequested() { return Nx::g_quitRequested; }
bool& DeviceLost() { return Nx::g_deviceLost; }
IDirect3DDevice9* Device() { return Nx::g_d3dDevice; }
D3DPRESENT_PARAMETERS* PresentParameters() { return &Nx::g_presentParameters; }

uint32_t g_mlpReferences;
int g_skippedTotal;
int g_skippedInARow;

// Frames in a row whose render was skipped (after four, one is drawn regardless), and in all.
int& SkippedInARow() { return g_skippedInARow; }
int& SkippedTotal() { return g_skippedTotal; }

void ServiceNetwork() { GameNet::Manager::ServiceGameSpy(); }
// Around a device Reset: the device objects released first (default pool) and made again after.
void ReleaseDeviceObjects()
{
    Nx::ReleaseRenderTargetTextures();
    Nx::ReleaseBuffers();
    Nx::ReleaseRenderTargets();
}
void RestoreDeviceObjects()
{
    Nx::RestoreRenderTargetTextures();
    Nx::RecreateBuffers();
    Nx::CreateRenderTargets();
}
// A task list's tasks, in order (0x004070A0; the profiling flag is passed but unused).
void RunTasks(Tsk::Stack& list, bool profiling)
{
    list.Process(reinterpret_cast<void*>(static_cast<uintptr_t>(profiling)));
}
void BeginScene() { Nx::StartRender(); }      // the device's BeginScene
void RenderFrame() { Nx::Engine::RenderFrame(); }     // the world, then the 2D
void AfterRender() { Mdl::Skate::DebugRails(); }     // (uses the skate module)
void EndScene() { Nx::Engine::EndRender(); }        // EndScene, Present

uint8_t LetterboxActive() { return Nx::g_camera.letterboxActive; }

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
void ConstructTaskList(Tsk::Stack& list) { list.Construct(); }
void DestroyTaskList(Tsk::Stack& list) { list.Destruct(); }

Manager*& Instance() { return Ass::g_mainLoopManager; }
uint32_t& References() { return g_mlpReferences; }

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
    Front::NewFrontend::DeviceLost(); // (not original either: the new front end's - game/front/new_frontend.h)
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
        // Not original - THUGPRO's big-level positions in network games (TP-R6, game/net/thugpro_compat.h).
        GameNet::Thugpro::RefreshPositionScale();
        // Not original - THUGPRO's own CAS parts from the user's install, once the CAS lists are loaded (TP-R10).
        Obj::ThugproCas::LoadOnce();
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
            Dbg::Printf("\nProfiling.....Start of main loop\n\n"); // "\nProfiling.....Start of main loop\n\n"
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
        // Not original - a frame series (game/sys/screenshot.h): the capture's time isn't counted, so no frame is
        // skipped to catch up - every logic frame is drawn and taken.
        // Original behaviour: the next frame is due a frame after this one was (late ones skip drawing).
        if (Sys::Screenshot::SeriesRunning()) {
            QueryPerformanceCounter(&counter);
            frameStart = static_cast<uint64_t>(counter.QuadPart);
        }
        Tmr::UpdateFrameLength();
        m_profiling = 0;
    } while (!m_done);
    m_done = doneOnEntry;
}

} // namespace Mlp
