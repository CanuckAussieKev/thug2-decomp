// NOT ORIGINAL - play from here (ours). See level_editor_play.h.

#include "editor/level_editor_play.h"
#include "engine/pad_input/input_source.h"

#include <cstdint>
#include <SDL3/SDL.h>

#include "editor/level_editor.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "game/front_end/front_end.h"
#include "editor/level_editor_layout.h"
#include "game/front_end/menu_pad_event.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::perform {

Step on_key(bool pressed, bool in_level, bool paused, bool offered)
{
    if (!pressed || !in_level || paused || !offered) {
        return Step::kNone;
    }
    return Step::kPauseThenEnter;
}

bool get_key_down(input::KeyState keys)
{
    if (keys.empty()) {
        return false;
    }
    const bool ctrl = (core::at(keys, SDL_SCANCODE_LCTRL) != 0U) || (core::at(keys, SDL_SCANCODE_RCTRL) != 0U);
    return (core::at(keys, SDL_SCANCODE_F5) != 0U) || (ctrl && (core::at(keys, SDL_SCANCODE_P) != 0U));
}

namespace {

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kUnpauseFrames = 2;        // (after level_editor_exit's pause menu, before it's closed)
constexpr int kPauseWaitFrames = 120;    // (for the game to pause after Start)
constexpr int kPauseMenuFrames = 10;     // (the pause menu up this long before LEVEL EDITOR is chosen)

// NOT ORIGINAL grouping - level_editor_play.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_play_state()).
struct LevelEditorPlayState {
    bool requested_{};
    bool return_asked_{}; // (the Play menu's Stop Playing)
    bool have_resume_{};
    int resume_load_ = -1;
    Resume resume_{};
};

LevelEditorPlayState& level_editor_play_state()
{
    static core::NoDestroy<LevelEditorPlayState> state;
    return state.get();
}

// After leaving: the pause menu closed a couple of frames later. Back to the editor: waiting for the pause, then
// for the pause menu.
enum class Phase : uint8_t { kIdle, kUnpause, kWaitPause, kWaitMenu };
// NOT ORIGINAL grouping - the play start's phase, frames and key (were globals). Made on first use, never destroyed
// (play_start()).
struct PlayStart {
    Phase phase_ = Phase::kIdle;
    int frames_{};
    bool key_up_ = true;
    bool escape_up_ = true;      // (Esc while playing from here: back to the editor - the key's last frame)
    bool was_paused_ = false;    // (the game paused last frame: an Esc then is the pause menu's own)
};

PlayStart& play_start()
{
    static core::NoDestroy<PlayStart> state;
    return state.get();
}

// The overlay's level_editor_enter is loaded (tools/pipeline/menu_level/make_menu_level.ts).
bool enter_script_loaded()
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(checksums::hash_from_text("level_editor_enter"));
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

} // namespace

void get_request() { level_editor_play_state().requested_ = true; }

bool take_request()
{
    const bool requested = level_editor_play_state().requested_;
    level_editor_play_state().requested_ = false;
    if (requested) {
        play_start().phase_ = Phase::kUnpause;
        play_start().frames_ = 0;
    }
    return requested;
}

void get_keep(const Resume& resume, int level_load)
{
    level_editor_play_state().resume_ = resume;
    level_editor_play_state().resume_load_ = level_load;
    level_editor_play_state().have_resume_ = true;
}

bool take_resume(Resume* resume, int level_load)
{
    if (!level_editor_play_state().have_resume_ || level_editor_play_state().resume_load_ != level_load) {
        return false;
    }
    level_editor_play_state().have_resume_ = false;
    *resume = level_editor_play_state().resume_;
    return true;
}

bool playing(int level_load)
{
    const LevelEditorPlayState& state = level_editor_play_state();
    return state.have_resume_ && state.resume_load_ == level_load && !level_editor::live_now() &&
           render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
}

void ask_return() { level_editor_play_state().return_asked_ = true; }

bool takes_pause_key(int scancode)
{
    return scancode == SDL_SCANCODE_ESCAPE && play_start().phase_ == Phase::kIdle && enter_script_loaded() &&
           playing(layout::level_loads());
}

void tick_frame(modules::FrontEnd* front_end)
{
    if (!level_editor::is_switched_on() || frame_loop::frames_run() == 0) {
        return;
    }
    const input::KeyState keys = input::keys();
    const bool down = get_key_down(keys);
    const bool pressed = (down && play_start().key_up_) || level_editor_play_state().return_asked_;
    play_start().key_up_ = !down;
    level_editor_play_state().return_asked_ = false;
    const bool paused = (front_end != nullptr) && front_end->is_paused();
    const bool in_level = render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
    // Esc while playing from here goes back to the editor, as editors' play modes do (Unity / Unreal: Esc stops
    // playing), rather than to the pause menu: the game doesn't get the key (takes_pause_key), the game is paused
    // without its menu (as Simulate's stop pauses it) and level_editor_enter runs at once. An Esc while the game was
    // already paused is the pause menu's own (closing it).
    const bool escape_down = !keys.empty() && core::at(keys, SDL_SCANCODE_ESCAPE) != 0U;
    const bool escape_pressed = escape_down && play_start().escape_up_ && !play_start().was_paused_;
    play_start().escape_up_ = !escape_down;
    play_start().was_paused_ = paused;
    if (escape_pressed && play_start().phase_ == Phase::kIdle && in_level && !paused && front_end != nullptr &&
        playing(layout::level_loads()) && enter_script_loaded()) {
        get_log("level editor: back to the editor (Esc while playing) - paused without the pause menu");
        front_end->get_pause_game(true);
        script::launch_script("level_editor_enter", nullptr, nullptr, false);
        return;
    }
    switch (play_start().phase_) {
    case Phase::kUnpause:
        // (level_editor_exit brought the pause menu back: closed as its CONTINUE closes it)
        if (++play_start().frames_ < kUnpauseFrames) {
            return;
        }
        play_start().phase_ = Phase::kIdle;
        if (paused && in_level) {
            get_log("level editor: play from here - the pause menu closed");
            script::launch_script("exit_pause_menu", nullptr, nullptr, false);
        }
        return;
    case Phase::kWaitPause:
        if (!in_level) {
            play_start().phase_ = Phase::kIdle;
            return;
        }
        if (!paused) {
            if (++play_start().frames_ >= kPauseWaitFrames) {
                get_log("level editor: back to the editor - the game didn't pause (the pause menu's LEVEL EDITOR opens "
                        "it)");
                play_start().phase_ = Phase::kIdle;
            }
            return;
        }
        play_start().phase_ = Phase::kWaitMenu;
        play_start().frames_ = 0;
        return;
    case Phase::kWaitMenu:
        if (!in_level || !paused) {
            play_start().phase_ = Phase::kIdle;
            return;
        }
        if (++play_start().frames_ < kPauseMenuFrames) {
            return;
        }
        play_start().phase_ = Phase::kIdle;
        get_log("level editor: back to the editor (the play key) - running level_editor_enter");
        script::launch_script("level_editor_enter", nullptr, nullptr, false);
        return;
    case Phase::kIdle:
        break;
    }
    if (!pressed || level_editor::live_now()) {
        return;
    }
    const bool offered = enter_script_loaded() && cfunc_available(nullptr, script::Context()) && !layout::get_refused();
    if (on_key(pressed, in_level, paused, offered) != Step::kPauseThenEnter) {
        if (in_level && !paused && !offered) {
            get_log("level editor: the play key - the editor isn't offered here");
        }
        return;
    }
    // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
    get_log("level editor: back to the editor (the play key) - pausing");
    modules::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
    play_start().phase_ = Phase::kWaitPause;
    play_start().frames_ = 0;
}

} // namespace frontend::level_editor::perform
