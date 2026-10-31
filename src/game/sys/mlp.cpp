// The main loop (FrameLoop::Hub), with PartyMod's per-level letterbox built in.
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
#include "game/gfx/sub_mesh.h"
#include "game/gfx/bitmap_texture.h"
#include "game/gfx/screen.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/net/thugpro_compat.h"
#include "game/skate/skate_module.h"
#include "game/skate/thugpro_cas.h"
#include "game/pad_input/device.h"
#include "game/sys/platform.h"
#include "game/sys/screenshot.h"
#include "game/sys/stopwatch.h"
#include "game/sys/winmain.h"
#include "game/obj/cutscene.h"
#include "game/gfx/movie.h"
#include "game/assets/resource_registry.h"
#include "game/gfx/draw_frame.h"

namespace FrameLoop {

uint32_t& FramesRun()
{
    static uint32_t frames;
    return frames;
}

namespace {

// Set for a frame that isn't drawn (running late, the window inactive, the device just reset): the scene isn't
// begun or presented.
bool& OmitFromDraw() { return Engine3D::g_suppressDraw; }
bool& Flag6F1158() { return GameObjects::g_cutscenePlaying; } // cleared every frame (read by the pad code)
bool AppActive() { return SystemCore::g_appActive; }
bool TextEntry() { return OnlinePlay::g_textEntry; } // keeps the loop running while the window is inactive
bool QuitRequested() { return Engine3D::g_quitRequested; }
bool& DeviceLost() { return Engine3D::g_deviceLost; }
IDirect3DDevice9* InputPort() { return Engine3D::g_d3dDevice; }
D3DPRESENT_PARAMETERS* PresentParameters() { return &Engine3D::g_presentParameters; }

uint32_t g_mlpReferences;
int g_skippedTotal;
int g_skippedInARow;

// Frames in a row whose render was skipped (after four, one is drawn regardless), and in all.
int& SkippedInARow() { return g_skippedInARow; }
int& SkippedTotal() { return g_skippedTotal; }

void ServiceNetwork() { OnlinePlay::Hub::ServiceGameSpy(); }
// Around a device Reset: the device objects released first (default pool) and made again after.
void ReleaseDeviceObjects()
{
    Engine3D::ReleaseRenderTargetTextures();
    Engine3D::ReleaseBuffers();
    Engine3D::ReleaseRenderTargets();
}
void RestoreDeviceObjects()
{
    Engine3D::RestoreRenderTargetTextures();
    Engine3D::RecreateBuffers();
    Engine3D::CreateRenderTargets();
}
// A task list's tasks, in order (0x004070A0; the profiling flag is passed but unused).
void RunTasks(Scheduling::PileUp& list, bool profiling)
{
    list.WorkOn(reinterpret_cast<void*>(static_cast<uintptr_t>(profiling)));
}
void BeginScene() { Engine3D::BeginFrameDraw(); }      // the device's BeginScene
void RenderFrame() { Engine3D::Engine::RenderFrame(); }     // the world, then the 2D
void AfterRender() { GameModules::Skate::DebugRails(); }     // (uses the skate module)
void EndScene() { Engine3D::Engine::EndRender(); }        // EndScene, Present

uint8_t CinemaBarsOn() { return Engine3D::g_camera.letterboxActive; }

} // namespace

// The frame's render with PartyMod's letterbox (PartyMod's renderWorldWrapper, at 0x0044F1C0): letterboxed
// (4:3) in the front end, not in a level (split screen breaks with it on: both screens drawn over each other,
// sharing a depth buffer). The original only rendered: RenderFrame();
void RenderFrameLetterboxed()
{
    RenderFrame();
    if (Engine3D::CurrentLevel() == 0) {
        if (CinemaBarsOn() != 1)
            Engine3D::SetLetterbox(true);
    } else if (CinemaBarsOn() != 0) {
        Engine3D::SetLetterbox(false);
    }
}

namespace {

// The task lists (Lst-style lists of tasks, 0x3C bytes each).
void ConstructTaskList(Scheduling::PileUp& list) { list.MakeNew(); }
void DestroyTaskList(Scheduling::PileUp& list) { list.Destruct(); }

Hub*& Sole() { return AssetStore::g_mainLoopManager; }
uint32_t& RefTally() { return g_mlpReferences; }

} // namespace

// 0x0044EDC0
Hub* Hub::MakeNew()
{
    ConstructTaskList(m_logicTasks);
    ConstructTaskList(m_displayTasks);
    ConstructTaskList(m_systemTasks);
    m_unknownB8 = 0;
    m_unknownBC = 0;
    m_isFinished = false;
    return this;
}

// 0x0044EE30
void Hub::DestroyBody()
{
    DestroyTaskList(m_systemTasks);
    DestroyTaskList(m_displayTasks);
    DestroyTaskList(m_logicTasks);
}

// 0x0044EE90
void Hub::ResetDeviceIfLost()
{
    if (!DeviceLost())
        return;
    if (InputPort()->TestCooperativeLevel() == D3DERR_DEVICELOST) // (not yet: it can't be reset)
        return;
    // Not original - the level editor's UI (game/front/level_editor_ui.h): ImGui's default-pool objects dropped before
    // the reset (made again on its next frame).
    // Original behaviour: only the game's own.
    MenuSystem::LevelEditor::Ui::DeviceLost();
    MenuSystem::NewFrontend::DeviceLost(); // (not original either: the new front end's - game/front/new_frontend.h)
    ReleaseDeviceObjects();
    if (InputPort()->Reset(PresentParameters()) < 0)
        return;
    RestoreDeviceObjects();
    DeviceLost() = false;
}

// 0x0044F220
void Hub::ProfileFrames(int frames)
{
    m_profileFrames = frames;
    m_profiling = 0;
}

// 0x0044F240
void Hub::StopLoop() { m_isFinished = true; }

// 0x0044F260
Hub* CreateManager(bool create)
{
    if (!Sole() && create) {
        if (void* memory = MemSys::AllocZeroed(sizeof(Hub), nullptr, 1)) {
            Sole() = static_cast<Hub*>(memory)->MakeNew();
            ++RefTally();
            return Sole();
        }
        Sole() = nullptr;
    }
    ++RefTally();
    return Sole();
}

// 0x0044F2B0
void ReleaseManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Hub* manager = Sole()) {
        manager->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(manager);
    }
    Sole() = nullptr;
    RefTally() = 0;
}

// 0x0044EEF0
void Hub::MainLoop()
{
    // The test runner's EngineStarted stage: every engine module exists, the first frame is next.

    uint8_t doneOnEntry = m_isFinished;
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
    m_isFinished = false;
    do {
        ++FramesRun(); // (not original - see FramesRun)
        if (screenSaverTimeout && framesSinceScreenSaver++ >= 200) {
            SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screenSaverTimeout, nullptr, 0);
            framesSinceScreenSaver = 0;
        }
        OmitFromDraw() = false;
        Flag6F1158() = false;
        Sio::ReadKeyboard();
        OsLayer::ProcessWindowEvents();
        ServiceNetwork();
        // Not original - THUGPRO's big-level positions in network games (TP-R6, game/net/thugpro_compat.h).
        OnlinePlay::Thugpro::RefreshPositionScale();
        // Not original - THUGPRO's own CAS parts from the user's install, once the CAS lists are loaded (TP-R10).
        GameObjects::ThugproCas::LoadOnce();
        if (!AppActive()) {
            if (!TextEntry()) {
                // Inactive: idle until the window is active again (nothing else runs meanwhile).
                do {
                    Sleep(100);
                    OsLayer::ProcessWindowEvents();
                } while (!AppActive());
                continue;
            }
            OmitFromDraw() = true;
        }
        if (QuitRequested())
            break;
        if (DeviceLost()) {
            ResetDeviceIfLost(); // (inline in the original)
            OmitFromDraw() = true;
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
            OmitFromDraw() = true;
            ++SkippedInARow();
        }
        frameStart += frameTicks;

        FrameClock::NoteRefresh();
        FrameClock::OnceEveryDraw();
        if (m_profileFrames) {
            DebugOut::Printf("\nProfiling.....Start of main loop\n\n"); // "\nProfiling.....Start of main loop\n\n"
            --m_profileFrames;
            m_profiling = 1;
        }
        RunTasks(m_systemTasks, m_profiling != 0);
        RunTasks(m_logicTasks, m_profiling != 0);
        if (!OmitFromDraw())
            BeginScene();
        RenderFrameLetterboxed();
        AfterRender();
        // Not original - the screenshot key (game/sys/screenshot.h): the frame read just before it's presented.
        // Original behaviour: the scene is ended and presented.
        SystemCore::Screenshot::BeforePresent(!OmitFromDraw());
        if (!OmitFromDraw())
            EndScene();
        // Not original - a frame series (game/sys/screenshot.h): the capture's time isn't counted, so no frame is
        // skipped to catch up - every logic frame is drawn and taken.
        // Original behaviour: the next frame is due a frame after this one was (late ones skip drawing).
        if (SystemCore::Screenshot::SeriesRunning()) {
            QueryPerformanceCounter(&counter);
            frameStart = static_cast<uint64_t>(counter.QuadPart);
        }
        FrameClock::UpdateFrameLength();
        m_profiling = 0;
    } while (!m_isFinished);
    m_isFinished = doneOnEntry;
}

} // namespace Mlp
