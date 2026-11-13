// The main loop (frame_loop::Hub), with PartyMod's per-level letterbox built in.
// (The letterbox per level contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md)

#include "engine/sys/mlp.h"

#include <windows.h>

#include <cstdint>
#include <utility>
#include <new>

#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/ext/extension.h"
#include "engine/memory/task_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/screen.h"
#include "engine/sys/game_hooks.h"
#include "engine/file/thugpro_cas.h"
#include "engine/pad_input/device.h"
#include "platform/sdl_events.h"
#include "engine/sys/stopwatch.h"
#include "platform/process_flags.h"
#include "engine/assets/resource_registry.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu_context.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"
#include "engine/pad_input/sdl_input.h"
#include "engine/objects/objects_shared.h"

namespace frame_loop {

uint32_t& frames_run()
{
    static uint32_t frames;
    return frames;
}

namespace {
constexpr uint32_t kScreenSaverResetFrames = 200; // the screen-saver timeout is set again this often
constexpr DWORD kInactiveSleepMilliseconds = 100;
constexpr uint64_t kFarBehindFrames = 20;         // a frame this many frames late counts from the next one

// Set for a frame that isn't drawn (running late, the window inactive, the device just reset): the scene isn't
// begun or presented.

bool text_entry() { return sio::text_entry_flag(); } // keeps the loop running while the window is inactive

// NOT ORIGINAL grouping - the main loop's references and its skipped renders. Made on first use, never destroyed
// (mlp_state()).
struct MlpState {
    uint32_t mlp_references_{};
    int skipped_total_{};
    int skipped_in_a_row_{};
    // NOT ORIGINAL: a back buffer size asked for (request_back_buffer_size; 0: none)
    int wanted_width_{};
    int wanted_height_{};
};

MlpState& mlp_state()
{
    static core::NoDestroy<MlpState> state;
    return state.get();
}

// Frames in a row whose render was skipped (after four, one is drawn regardless), and in all.
int& skipped_in_a_row() { return mlp_state().skipped_in_a_row_; }
int& skipped_total() { return mlp_state().skipped_total_; }

// (the online session's GameSpy service, the skate module's rail debugging: engine/sys/game_hooks.h)
void service_network() { sys::game_hooks().service_network(); }
// A task list's tasks, in order (0x004070A0; the profiling flag is passed but unused).
void run_tasks(tasks::PileUp& list, bool profiling)
{
    (void)profiling; // (the original passed it on as the pointer argument, which isn't read)
    list.work_on(nullptr);
}
void begin_scene() { render::device().begin_frame_draw(); } // the device's BeginScene
void render_frame() { render::engine::render_frame(); } // the world, then the 2D
void after_render() { sys::game_hooks().after_render(); } // (uses the skate module)
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
        if (cinema_bars_on() != 1) {
            render::set_letterbox(true);
        }
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
    if (!render::engine_parts().device_lost()) {
        return;
    }
    if (!render::device().can_reset()) { // (not yet: it is still lost)
        return;
    }
    // Not original - the extensions (engine/ext/extension.h; the level editor's UI: ImGui's default-pool objects
    // dropped before the reset, made again on its next frame).
    // Original behaviour: only the game's own.
    ext::device_lost(); // (then the new front end's)
    render::device().release_device_objects();
    // Not original - a new back buffer size (request_back_buffer_size): the reset makes the back buffer at it.
    MlpState& state = mlp_state();
    const bool resized = state.wanted_width_ > 0;
    if (resized) {
        render::gpu_context().set_back_buffer_size({.width = static_cast<uint32_t>(state.wanted_width_),
                                                    .height = static_cast<uint32_t>(state.wanted_height_)});
        sys::get_window_width() = state.wanted_width_;
        sys::get_window_height() = state.wanted_height_;
        state.wanted_width_ = state.wanted_height_ = 0;
    }
    if (!render::device().reset()) {
        return;
    }
    render::device().restore_device_objects();
    render::engine_parts().set_device_lost(false);
    if (resized) {
        // (the 640x480 layout's factors for the new size, the letterbox as it was)
        render::set_letterbox(render::renderer().render_camera().letterbox_active_ != 0U);
        get_log("window: the back buffer is now %ux%u", render::gpu_context().settings().width,
                render::gpu_context().settings().height);
    }
}

void request_back_buffer_size(BackBufferSize size)
{
    constexpr int kSmallest = 16;
    if (size.width < kSmallest || size.height < kSmallest) {
        return;
    }
    const render::GpuSettings now = render::gpu_context().settings();
    MlpState& state = mlp_state();
    if (std::cmp_equal(now.width, size.width) && std::cmp_equal(now.height, size.height) && state.wanted_width_ == 0) {
        return;
    }
    state.wanted_width_ = size.width;
    state.wanted_height_ = size.height;
    render::engine_parts().set_device_lost(true);
}

// 0x0044F220
void Hub::profile_frames(int frames)
{
    profile_frames_ = frames;
    profiling_ = 0;
}

// 0x0044F240
void Hub::stop_loop() { is_finished_ = 1U; }

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
    is_finished_ = 0U;
    for (bool repeat = true; repeat; repeat = (is_finished_ == 0U)) {
        ++frames_run(); // (not original - see frames_run)
        if ((screen_saver_timeout != 0U) && frames_since_screen_saver++ >= kScreenSaverResetFrames) {
            SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screen_saver_timeout, nullptr, 0);
            frames_since_screen_saver = 0;
        }
        render::engine_parts().set_suppress_draw(false);
        objects::objects_shared().set_cutscene_playing(false);
        sio::read_keyboard();
        os_layer::process_window_events();
        service_network();
        // Not original - THUGPRO's big-level positions in network games (TP-R6, game/online/thugpro_compat.h, through
        // engine/sys/game_hooks.h).
        sys::game_hooks().refresh_position_scale();
        // Not original - THUGPRO's own CAS parts from the user's install, once the CAS lists are loaded (TP-R10).
        objects::thugpro_cas::load_once();
        if (!sys::app_active()) {
            if (!text_entry()) {
                // Inactive: idle until the window is active again (nothing else runs meanwhile).
                for (bool again = true; again; again = !sys::app_active()) {
                    Sleep(kInactiveSleepMilliseconds);
                    os_layer::process_window_events();
                }
                continue;
            }
            render::engine_parts().set_suppress_draw(true);
        }
        if (render::engine_parts().quit_requested()) {
            break;
        }
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
        if (elapsed > frame_ticks * kFarBehindFrames) { // far behind (a pause): start counting again from one frame ago
            frame_start = now - frame_ticks;
            elapsed = frame_ticks;
        }
        // A frame more than half a frame late isn't drawn - but never more than four in a row, and never while
        // the window is inactive.
        // Not original: while an extension keeps every frame (a frame series, a demo rendered as pictures or a video -
        // engine/ext/extension.h) a late frame is drawn too: the time before the wait (window events, the network) can
        // make one late even though the last frame's capture time isn't counted.
        // Original behaviour: a late frame isn't drawn.
        const bool active = sys::app_active();
        if ((skipped_in_a_row() >= 4 || elapsed < frame_ticks + (frame_ticks >> 1) || ext::keeps_every_frame()) &&
            active) {
            skipped_in_a_row() = 0;
        } else {
            ++skipped_total();
            render::engine_parts().set_suppress_draw(true);
            ++skipped_in_a_row();
        }
        frame_start += frame_ticks;

        frame_clock::note_refresh();
        frame_clock::once_every_draw();
        if (profile_frames_ != 0) {
            debug::printf("\nProfiling.....Start of main loop\n\n"); // "\nProfiling.....Start of main loop\n\n"
            --profile_frames_;
            profiling_ = 1;
        }
        run_tasks(system_tasks_, profiling_ != 0);
        run_tasks(logic_tasks_, profiling_ != 0);
        if (!render::engine_parts().suppress_draw()) {
            begin_scene();
        }
        render_frame_letterboxed();
        after_render();
        // Not original - the extensions (engine/ext/extension.h; the screenshot key: the frame read just before it's
        // presented).
        // Original behaviour: the scene is ended and presented.
        ext::before_present(!render::engine_parts().suppress_draw());
        if (!render::engine_parts().suppress_draw()) {
            end_scene();
        }
        // Not original - an extension that keeps every frame (engine/ext/extension.h; the screenshot key's frame
        // series): the capture's time isn't counted, so no frame is skipped to catch up - every logic frame is drawn
        // and taken.
        // Original behaviour: the next frame is due a frame after this one was (late ones skip drawing).
        if (ext::keeps_every_frame()) {
            QueryPerformanceCounter(&counter);
            frame_start = static_cast<uint64_t>(counter.QuadPart);
        }
        frame_clock::update_frame_length();
        profiling_ = 0;
    }
    is_finished_ = done_on_entry;
}

} // namespace frame_loop
