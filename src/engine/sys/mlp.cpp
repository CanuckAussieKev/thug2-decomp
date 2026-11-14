// The main loop (frame_loop::Hub), with PartyMod's per-level letterbox built in.
// (The letterbox per level contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md)

#include "engine/sys/mlp.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <new>
#include <optional>

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
#include "engine/sys/editor_mode.h"
#include "engine/sys/frame_blend.h"
#include "engine/sys/frame_pacing.h"
#include "engine/sys/tick_rate.h"
#include "base/maths/random.h"
#include "platform/crt.h"
#include "platform/process_flags.h"
#include "engine/assets/resource_registry.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu_context.h"
#include "platform/run_log.h"
#include "platform/frame_profile.h"
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
constexpr uint64_t kMostSkippedInARow = 4;        // late frames not drawn in a row at most

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

// NOT ORIGINAL - the frame's wait mostly slept: until `spin` before `due` (performance-counter ticks) on a
// high-resolution waitable timer, when more than that is left. The main loop's Sleep(0) loop held a core busy for the
// whole wait (each game running - and the workspaces' copies beside it - took a core from the others and from the
// processor's boost); it now spins only the last moments, so the frame still starts at its time. Nothing when the
// timer can't be made (older Windows): the spin does it all, as before.
void sleep_until_near(uint64_t due, uint64_t now, int64_t frequency)
{
    constexpr int64_t kSpinMicroseconds = 2000; // (the timer wakes within a few hundred microseconds of its time)
    constexpr int64_t kMicrosecondsPerSecond = 1000000;
    constexpr int64_t kTicksPer100ns = 10; // (a waitable timer's relative time: 100 ns units, negative)
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr || due <= now) {
        return;
    }
    const auto left = static_cast<int64_t>((due - now) * kMicrosecondsPerSecond / static_cast<uint64_t>(frequency));
    if (left <= kSpinMicroseconds) {
        return;
    }
    LARGE_INTEGER wait;
    wait.QuadPart = -(left - kSpinMicroseconds) * kTicksPer100ns;
    if (SetWaitableTimer(timer, &wait, 0, nullptr, nullptr, FALSE) != 0) {
        WaitForSingleObject(timer, INFINITE);
    }
}

uint64_t counter_now()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
}

// NOT ORIGINAL - [Graphics] max_fps (engine/sys/frame_pacing.h): the render-only frames after a tick's frame, until the
// next tick is due - each the world drawn again, blended between the tick before and this one by how far into the tick
// it is (render_pc/frame_interp.h), then presented. No task runs, no vblank is counted, no input is read; the game's
// random sequences are put back as they were (anything the draws took from them - the weather's splashes, a sort key -
// is not the game's). Stops at anything that wouldn't draw (the window inactive, the device lost, a load, quitting).
// Frame generation (DLSS FG / FSR 3) would hook in here and at end_scene (the present).
struct ExtraFrames {
    double carry = 0.0;     // the frames-a-tick fraction carried (frame_pacing::frames_this_tick)
    uint64_t cost = 0;      // a render-only frame's time, smoothed (counter ticks)
    uint64_t drawn = 0;     // in all (the log's)
    uint64_t first_tick = 0; // the tick of the first (the log's)
};
constexpr uint64_t kExtraFramesLogged = 1000; // (the log's line on the rate, once)
ExtraFrames& extra_frames()
{
    static ExtraFrames frames;
    return frames;
}

bool can_draw_extra()
{
    return sys::app_active() && !render::engine_parts().device_lost() && !render::engine_parts().quit_requested() &&
           !render::engine_parts().loading_screen_up();
}

void draw_extra_frames(uint64_t tick_time, uint64_t tick_length, int64_t frequency)
{
    ExtraFrames& own = extra_frames();
    const int frames = frame_pacing::frames_this_tick(frame_blend::max_fps(), &own.carry);
    const uint64_t next_tick = tick_time + tick_length;
    frame_blend::Frame& blend = frame_blend::frame();
    for (int i = 1; i < frames; ++i) {
        const uint64_t due = frame_pacing::frame_due(tick_time, tick_length, i, frames);
        uint64_t now = counter_now();
        if (due >= next_tick) {
            break;
        }
        if (now < due) {
            sleep_until_near(due, now, frequency);
            while ((now = counter_now()) < due) {
                Sleep(0);
            }
        }
        if (!can_draw_extra()) {
            break;
        }
        if (!frame_pacing::frame_fits(now, own.cost, next_tick)) {
            own.cost = frame_pacing::cost_after_refusal(own.cost); // (tried again: the estimate can't learn otherwise)
            break;
        }
        const frame_profile::Scope phase("extra_frame"); // ([debug] frame_profile)
        const uint32_t crt_random = crt::game_rand_state();
        const maths::RandomStreams streams = maths::random_streams();
        blend.extra = true;
        blend.alpha = frame_pacing::blend_alpha(now, tick_time, tick_length);
        render::engine_parts().set_suppress_draw(false);
        begin_scene();
        render::device().draw_world();
        ext::before_present(true);
        end_scene();
        blend.extra = false;
        crt::game_srand(crt_random);
        maths::set_random_streams(streams);
        if (own.drawn++ == 0) {
            own.first_tick = blend.tick;
        } else if (own.drawn == kExtraFramesLogged) {
            const uint64_t ticks = std::max<uint64_t>(1, blend.tick - own.first_tick);
            get_log("frame pacing: %llu render-only frames over %llu ticks (%.2f a tick, about %.0f frames a second)",
                    static_cast<unsigned long long>(own.drawn), static_cast<unsigned long long>(ticks),
                    static_cast<double>(own.drawn) / static_cast<double>(ticks),
                    (1.0 + (static_cast<double>(own.drawn) / static_cast<double>(ticks))) * tick_rate::rate());
        }
        const uint64_t took = counter_now() - now;
        own.cost = frame_pacing::cost_after_frame(own.cost, took, tick_length);
    }
}

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
    // Not original - the tick rate's tick (engine/sys/tick_rate.h: frequency / 60 at the original rate), and the counts
    // of ticks below that were time at 60 a second scaled to it (the same counts at 60).
    // Original behaviour: frequency / 60, and the counts as written.
    const uint64_t frame_ticks = tick_rate::counter_ticks(static_cast<uint64_t>(frequency.QuadPart)); // (unsigned)
    const uint64_t screen_saver_frames = tick_rate::ticks_for(kScreenSaverResetFrames);
    const uint64_t far_behind_frames = tick_rate::ticks_for(kFarBehindFrames);
    const auto most_skipped_in_a_row = static_cast<int>(tick_rate::ticks_for(kMostSkippedInARow));
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    auto frame_start = static_cast<uint64_t>(counter.QuadPart);
    is_finished_ = 0U;
    for (bool repeat = true; repeat; repeat = (is_finished_ == 0U)) {
        ++frames_run(); // (not original - see frames_run)
        frame_profile::loop_begin(); // (not original: [debug] frame_profile - platform/frame_profile.h)
        std::optional<frame_profile::Scope> events_phase(std::in_place, "events");
        if ((screen_saver_timeout != 0U) && frames_since_screen_saver++ >= screen_saver_frames) {
            SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screen_saver_timeout, nullptr, 0);
            frames_since_screen_saver = 0;
        }
        // Not original - the extensions' pace for this loop (engine/ext/extension.h LoopPace; the demo viewer: paused,
        // slow motion, seeking). Original behaviour: every loop a logic frame at the frame's time, drawn unless late.
        const ext::LoopPace pace = ext::loop_pace();
        render::engine_parts().set_suppress_draw(false);
        if (!pace.hold) {
            objects::objects_shared().set_cutscene_playing(false);
            sio::read_keyboard();
        }
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

        events_phase.reset();
        // Wait for the frame's time (60 a second). Not original: not while an extension runs flat out (pace.flat_out).
        QueryPerformanceCounter(&counter);
        auto now = static_cast<uint64_t>(counter.QuadPart);
        if (!pace.flat_out) {
            const frame_profile::Scope pace_phase("pace");
            // Not original - most of a long wait slept (sleep_until_near): the loop below then spins only the last
            // moments, so the frame starts when it did, without a core held busy the whole time.
            // Original behaviour: Sleep(0) until the frame's time.
            sleep_until_near(frame_start + frame_ticks, now, frequency.QuadPart);
            QueryPerformanceCounter(&counter);
            now = static_cast<uint64_t>(counter.QuadPart);
            while (now < frame_start + frame_ticks) {
                Sleep(0);
                QueryPerformanceCounter(&counter);
                now = static_cast<uint64_t>(counter.QuadPart);
            }
        }
        uint64_t elapsed = now - frame_start;
        if (elapsed > frame_ticks * far_behind_frames) { // far behind (a pause): start counting again from one frame ago
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
        if ((skipped_in_a_row() >= most_skipped_in_a_row || elapsed < frame_ticks + (frame_ticks >> 1) || ext::keeps_every_frame() ||
             pace.flat_out) &&
            active) {
            skipped_in_a_row() = 0;
        } else {
            ++skipped_total();
            render::engine_parts().set_suppress_draw(true);
            ++skipped_in_a_row();
        }
        frame_start += frame_ticks;
        // Not original - a loop run flat out counts the next one's time from now (it isn't late); one the extension
        // doesn't draw isn't drawn. Original behaviour: neither.
        if (pace.flat_out) {
            frame_start = now;
        }
        if (pace.skip_draw) {
            render::engine_parts().set_suppress_draw(true);
        }

        const bool drawn = !render::engine_parts().suppress_draw(); // (not original: for frame_profile)
        // Not original - [Graphics] max_fps (engine/sys/frame_pacing.h): this tick's frame and the render-only frames
        // after it drawn blended between the tick before and this one - not while an extension paces the loop or keeps
        // every frame (the demo viewer, a frame series, a video), nor in the editor mode. Original behaviour (max_fps=0,
        // the default): every frame drawn as its tick left it - nothing below changes anything then.
        frame_blend::Frame& blend = frame_blend::frame();
        const uint64_t tick_time = frame_start;
        blend.on = frame_blend::max_fps() != frame_pacing::kOriginal && !pace.hold && !pace.flat_out && !pace.skip_draw &&
                   !ext::keeps_every_frame() && !sys::editor_mode::on();
        blend.extra = false;
        if (!pace.hold) {
            ++blend.tick;
        }
        // Not original - a held simulation (pace.hold: the demo viewer paused or slowed) counts no clock and runs no
        // task: the frame is drawn again as the last logic frame left it. Original behaviour: always a logic frame.
        if (!pace.hold) {
            frame_clock::note_refresh();
            frame_clock::once_every_draw();
            if (profile_frames_ != 0) {
                debug::printf("\nProfiling.....Start of main loop\n\n"); // "\nProfiling.....Start of main loop\n\n"
                --profile_frames_;
                profiling_ = 1;
            }
            {
                const frame_profile::Scope phase("system");
                run_tasks(system_tasks_, profiling_ != 0);
            }
            const frame_profile::Scope phase("logic");
            run_tasks(logic_tasks_, profiling_ != 0);
        } else {
            ext::held_frame();
        }
        // (not original: the tick's frame blended by how far into the tick it is drawn - max_fps above)
        blend.alpha = blend.on ? frame_pacing::blend_alpha(counter_now(), tick_time, frame_ticks) : 1.0F;
        {
            const frame_profile::Scope phase("render");
            if (!render::engine_parts().suppress_draw()) {
                begin_scene();
            }
            render_frame_letterboxed();
            after_render();
        }
        // Not original - the extensions (engine/ext/extension.h; the screenshot key: the frame read just before it's
        // presented).
        // Original behaviour: the scene is ended and presented.
        {
            const frame_profile::Scope phase("before_present"); // (not original: [debug] frame_profile)
            ext::before_present(!render::engine_parts().suppress_draw());
        }
        if (!render::engine_parts().suppress_draw()) {
            const frame_profile::Scope phase("present");
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
        if (!pace.hold) {
            frame_clock::update_frame_length();
        }
        // Not original - [Graphics] max_fps: the render-only frames until the next tick is due (draw_extra_frames).
        if (blend.on && drawn && is_finished_ == 0U) {
            draw_extra_frames(tick_time, frame_ticks, frequency.QuadPart);
        }
        profiling_ = 0;
        frame_profile::loop_end(drawn, render::engine_parts().loading_screen_up());
    }
    is_finished_ = done_on_entry;
}

} // namespace frame_loop
