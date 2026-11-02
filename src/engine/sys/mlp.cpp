// The main loop (frame_loop::Hub), with PartyMod's per-level letterbox built in.
// (The letterbox per level contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md)

#include "engine/sys/mlp.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "editor/level_editor_ui.h"
#include "engine/core/task_list.h"
#include "frontend/new_frontend.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/screen.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/thugpro_compat.h"
#include "game/skate/skate_module.h"
#include "game/skate/thugpro_cas.h"
#include "engine/pad_input/device.h"
#include "platform/platform.h"
#include "platform/screenshot.h"
#include "platform/stopwatch.h"
#include "platform/winmain.h"
#include "engine/objects/cutscene.h"
#include "engine/assets/resource_registry.h"
#include "engine/gfx/draw_frame.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "engine/net/net_game_state.h"

namespace frame_loop {

uint32_t& frames_run()
{
    static uint32_t frames;
    return frames;
}

namespace {

// Set for a frame that isn't drawn (running late, the window inactive, the device just reset): the scene isn't
// begun or presented.

bool text_entry() { return online::net_game_state().text_entry(); } // keeps the loop running while the window is inactive

D3DPRESENT_PARAMETERS* present_parameters() { return &render::present_parameters(); }

// NOT ORIGINAL grouping - the main loop's references and its skipped renders. Made on first use, never destroyed
// (mlp_state()).
struct MlpState {
    uint32_t mlp_references_{};
    int skipped_total_{};
    int skipped_in_a_row_{};
};

MlpState& mlp_state()
{
    static core::NoDestroy<MlpState> state;
    return state.get();
}

// Frames in a row whose render was skipped (after four, one is drawn regardless), and in all.
int& skipped_in_a_row() { return mlp_state().skipped_in_a_row_; }
int& skipped_total() { return mlp_state().skipped_total_; }

void service_network() { online::Hub::service_game_spy(); }
// Around a device Reset: the device objects released first (default pool) and made again after.
void release_device_objects()
{
    render::release_render_target_textures();
    render::release_buffers();
    render::release_render_targets();
}
void restore_device_objects()
{
    render::restore_render_target_textures();
    render::recreate_buffers();
    render::create_render_targets();
}
// A task list's tasks, in order (0x004070A0; the profiling flag is passed but unused).
void run_tasks(tasks::PileUp& list, bool profiling)
{
    (void)profiling; // (the original passed it on as the pointer argument, which isn't read)
    list.work_on(nullptr);
}
void begin_scene() { render::begin_frame_draw(); }      // the device's BeginScene
void render_frame() { render::engine::render_frame(); } // the world, then the 2D
void after_render() { modules::Skate::debug_rails(); }  // (uses the skate module)
void end_scene() { render::engine::end_render(); }      // EndScene, Present

uint8_t cinema_bars_on() { return render::renderer().render_camera().letterbox_active_; }

} // namespace

// The frame's render with PartyMod's letterbox (PartyMod's renderWorldWrapper, at 0x0044F1C0): letterboxed
// (4:3) in the front end, not in a level (split screen breaks with it on: both screens drawn over each other,
// sharing a depth buffer). The original only rendered: render_frame();
void render_frame_letterboxed()
{
    render_frame();
    if (render::current_level() == 0) {
        if (cinema_bars_on() != 1)
            render::set_letterbox(true);
    } else if (cinema_bars_on() != 0) {
        render::set_letterbox(false);
    }
}

namespace {

memory::HeapUnique<Hub>& sole() { return assets::main_loop_manager_owner(); }
uint32_t& ref_tally() { return mlp_state().mlp_references_; }

} // namespace

// 0x0044EDC0
// (the three task lists - 0x3C bytes each in the original - made first by their constructors)
Hub::Hub() = default;

// 0x0044EE90
void Hub::reset_device_if_lost()
{
    if (!render::engine_parts().device_lost())
        return;
    if (render::renderer().d3d_device()->TestCooperativeLevel() == D3DERR_DEVICELOST) // (not yet: it can't be reset)
        return;
    // Not original - the level editor's UI (editor/level_editor_ui.h): ImGui's default-pool objects dropped before
    // the reset (made again on its next frame).
    // Original behaviour: only the game's own.
    frontend::level_editor::ui::device_lost();
    frontend::new_frontend::device_lost(); // (not original either: the new front end's - frontend/new_frontend.h)
    release_device_objects();
    if (render::renderer().d3d_device()->Reset(present_parameters()) < 0)
        return;
    restore_device_objects();
    render::engine_parts().set_device_lost(false);
}

// 0x0044F220
void Hub::profile_frames(int frames)
{
    profile_frames_ = frames;
    profiling_ = 0;
}

// 0x0044F240
void Hub::stop_loop() { is_finished_ = true; }

// 0x0044F260
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = memory::HeapUnique<Hub>(memory::construct_in<Hub>(memory));
            ++ref_tally();
            return sole().get();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole().get();
}

// 0x0044F2B0
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    memory::destroy_then_clear(sole()); // (its destructor, then its block given back)
    ref_tally() = 0;
}

// 0x0044EEF0
void Hub::get_main_loop()
{
    // The test runner's EngineStarted stage: every engine module exists, the first frame is next.

    const uint8_t done_on_entry = is_finished_;
    // The screen saver's timeout is set again every 201 frames, which restarts its idle time.
    UINT screen_saver_timeout = 0;
    SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &screen_saver_timeout, 0);
    uint32_t frames_since_screen_saver = 0;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const uint64_t frame_ticks = static_cast<uint64_t>(frequency.QuadPart) / 60; // (an unsigned division)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    auto frame_start = static_cast<uint64_t>(counter.QuadPart);
    is_finished_ = false;
    for (bool repeat = true; repeat; repeat = !is_finished_) {
        ++frames_run(); // (not original - see frames_run)
        if (screen_saver_timeout && frames_since_screen_saver++ >= 200) {
            SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screen_saver_timeout, nullptr, 0);
            frames_since_screen_saver = 0;
        }
        render::engine_parts().set_suppress_draw(false);
        objects::set_cutscene_playing(false);
        sio::read_keyboard();
        os_layer::process_window_events();
        service_network();
        // Not original - THUGPRO's big-level positions in network games (TP-R6, engine/net/thugpro_compat.h).
        online::thugpro::refresh_position_scale();
        // Not original - THUGPRO's own CAS parts from the user's install, once the CAS lists are loaded (TP-R10).
        objects::thugpro_cas::load_once();
        if (!sys::app_active()) {
            if (!text_entry()) {
                // Inactive: idle until the window is active again (nothing else runs meanwhile).
                for (bool again = true; again; again = !sys::app_active()) {
                    Sleep(100);
                    os_layer::process_window_events();
                }
                continue;
            }
            render::engine_parts().set_suppress_draw(true);
        }
        if (render::engine_parts().quit_requested())
            break;
        if (render::engine_parts().device_lost()) {
            reset_device_if_lost(); // (inline in the original)
            render::engine_parts().set_suppress_draw(true);
        }

        // Wait for the frame's time (60 a second).
        QueryPerformanceCounter(&counter);
        auto now = static_cast<uint64_t>(counter.QuadPart);
        while (now < frame_start + frame_ticks) {
            Sleep(0);
            QueryPerformanceCounter(&counter);
            now = static_cast<uint64_t>(counter.QuadPart);
        }
        uint64_t elapsed = now - frame_start;
        if (elapsed > frame_ticks * 20) { // far behind (a pause): start counting again from one frame ago
            frame_start = now - frame_ticks;
            elapsed = frame_ticks;
        }
        // A frame more than half a frame late isn't drawn - but never more than four in a row, and never while
        // the window is inactive.
        const bool active = sys::app_active();
        // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
        if (skipped_in_a_row() >= 4 && active) {
            skipped_in_a_row() = 0;
        } else if (elapsed < frame_ticks + (frame_ticks >> 1) && active) {
            skipped_in_a_row() = 0;
        } else {
            ++skipped_total();
            render::engine_parts().set_suppress_draw(true);
            ++skipped_in_a_row();
        }
        frame_start += frame_ticks;

        frame_clock::note_refresh();
        frame_clock::once_every_draw();
        if (profile_frames_) {
            debug::printf("\nProfiling.....Start of main loop\n\n"); // "\nProfiling.....Start of main loop\n\n"
            --profile_frames_;
            profiling_ = 1;
        }
        run_tasks(system_tasks_, profiling_ != 0);
        run_tasks(logic_tasks_, profiling_ != 0);
        if (!render::engine_parts().suppress_draw())
            begin_scene();
        render_frame_letterboxed();
        after_render();
        // Not original - the screenshot key (platform/screenshot.h): the frame read just before it's presented.
        // Original behaviour: the scene is ended and presented.
        sys::screenshot::before_present(!render::engine_parts().suppress_draw());
        if (!render::engine_parts().suppress_draw())
            end_scene();
        // Not original - a frame series (platform/screenshot.h): the capture's time isn't counted, so no frame is
        // skipped to catch up - every logic frame is drawn and taken.
        // Original behaviour: the next frame is due a frame after this one was (late ones skip drawing).
        if (sys::screenshot::series_running()) {
            QueryPerformanceCounter(&counter);
            frame_start = static_cast<uint64_t>(counter.QuadPart);
        }
        frame_clock::update_frame_length();
        profiling_ = 0;
    }
    is_finished_ = done_on_entry;
}

} // namespace frame_loop
