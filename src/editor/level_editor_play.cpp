// NOT ORIGINAL - play from here (ours). See level_editor_play.h.

#include "editor/level_editor_play.h"

#include <cstdint>
#include <SDL2/SDL.h>

#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "editor/level_editor_layout.h"
#include "frontend/mouse_menus.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"

namespace frontend::level_editor::perform {

Step on_key(bool pressed, bool in_level, bool paused, bool offered)
{
    if (!pressed || !in_level || paused || !offered)
        return Step::kNone;
    return Step::kPauseThenEnter;
}

bool get_key_down(const uint8_t* keys)
{
    if (!keys)
        return false;
    const bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    return keys[SDL_SCANCODE_F5] || (ctrl && keys[SDL_SCANCODE_P]);
}

namespace {

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kUnpauseFrames = 2;        // (after level_editor_exit's pause menu, before it's closed)
constexpr int kPauseWaitFrames = 120;    // (for the game to pause after Start)
constexpr int kPauseMenuFrames = 10;     // (the pause menu up this long before LEVEL EDITOR is chosen)

bool g_requested;
bool g_have_resume;
int g_resume_load = -1;
Resume g_resume;
// After leaving: the pause menu closed a couple of frames later. Back to the editor: waiting for the pause, then
// for the pause menu.
enum class Phase : uint8_t { kIdle, kUnpause, kWaitPause, kWaitMenu };
Phase g_phase = Phase::kIdle;
int g_frames;
bool g_key_up = true;

// The overlay's level_editor_enter is loaded (tools/pipeline/menu_level/make_menu_level.ts).
bool enter_script_loaded()
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(checksums::hash_from_text("level_editor_enter"));
    return symbol && symbol->type == script::kQbvalScriptBody;
}

} // namespace

void get_request() { g_requested = true; }

bool take_request()
{
    const bool requested = g_requested;
    g_requested = false;
    if (requested) {
        g_phase = Phase::kUnpause;
        g_frames = 0;
    }
    return requested;
}

void get_keep(const Resume& resume, int level_load)
{
    g_resume = resume;
    g_resume_load = level_load;
    g_have_resume = true;
}

bool take_resume(Resume* resume, int level_load)
{
    if (!g_have_resume || g_resume_load != level_load)
        return false;
    g_have_resume = false;
    *resume = g_resume;
    return true;
}

void tick_frame(modules::FrontEnd* front_end)
{
    if (!level_editor::is_switched_on() || frame_loop::frames_run() == 0)
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    const bool down = get_key_down(keys);
    const bool pressed = down && g_key_up;
    g_key_up = !down;
    const bool paused = front_end && front_end->is_halted_;
    const bool in_level = render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
    switch (g_phase) {
    case Phase::kUnpause:
        // (level_editor_exit brought the pause menu back: closed as its CONTINUE closes it)
        if (++g_frames < kUnpauseFrames)
            return;
        g_phase = Phase::kIdle;
        if (paused && in_level) {
            get_log("level editor: play from here - the pause menu closed");
            script::launch_script("exit_pause_menu", nullptr, nullptr, false);
        }
        return;
    case Phase::kWaitPause:
        if (!in_level) {
            g_phase = Phase::kIdle;
            return;
        }
        if (!paused) {
            if (++g_frames >= kPauseWaitFrames) {
                get_log("level editor: back to the editor - the game didn't pause (the pause menu's LEVEL EDITOR opens "
                        "it)");
                g_phase = Phase::kIdle;
            }
            return;
        }
        g_phase = Phase::kWaitMenu;
        g_frames = 0;
        return;
    case Phase::kWaitMenu:
        if (!in_level || !paused) {
            g_phase = Phase::kIdle;
            return;
        }
        if (++g_frames < kPauseMenuFrames)
            return;
        g_phase = Phase::kIdle;
        get_log("level editor: back to the editor (the play key) - running level_editor_enter");
        script::launch_script("level_editor_enter", nullptr, nullptr, false);
        return;
    case Phase::kIdle:
        break;
    }
    if (!pressed || level_editor::live_now())
        return;
    const bool offered = enter_script_loaded() && cfunc_available(nullptr, nullptr) && !layout::get_refused();
    if (on_key(pressed, in_level, paused, offered) != Step::kPauseThenEnter) {
        if (in_level && !paused && !offered)
            get_log("level editor: the play key - the editor isn't offered here");
        return;
    }
    // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
    get_log("level editor: back to the editor (the play key) - pausing");
    mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
    g_phase = Phase::kWaitPause;
    g_frames = 0;
}

} // namespace frontend::level_editor::perform
