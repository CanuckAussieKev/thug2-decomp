// Debug mode (engine/sys/debug_mode.h).

#include "engine/sys/debug_mode.h"
#include "platform/keyboard.h"

#include <cstdint>
#include <SDL2/SDL.h>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include "engine/script/cell.h"
#include "engine/script/names.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "engine/gfx/screen.h"
#include "engine/script/cscript.h"
#include "engine/sys/mlp.h"
#include "engine/script/script_tables.h"
#include "platform/run_log.h"
#include "engine/objects/movie_manager.h"

namespace sys::debug_mode {

namespace {

constexpr float kStickFull = 127.0F; // a key held: the stick all the way over

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// The start-up scripts, which keep the disc build's CD: false there makes default_system_startup take the
// developers' path - no UsePreFilesForLevelLoading (their loose files: the retail data's archives aren't loaded, the
// front end's peds lose their animations - a crash at 0x00482BA0), auto_launch_viewer, the script debugger -
// instead of the main menu; autolaunch and startup_loading_screen run with it. LoadPermSongs (called by LoadPermSounds
// at start-up) fills the music playlist only when CD is true (or TestMusicFromHost): with CD false there's no music.
bool is_startup_script(const script::Script* script)
{
    if (!script) {
        return false;
    }
    // (the script it's running now: CScript +0xC8)
    const uint32_t name = script->script_title();
    static const auto kStartup = std::to_array<uint32_t>({name_hash("default_system_startup"), name_hash("autolaunch"),
                                        name_hash("startup_loading_screen"), name_hash("LoadPermSongs")});
    for (const uint32_t startup : kStartup) {
        if (name == startup) {
            return true;
        }
    }
    return false;
}

// The table's CD entry is {"CD", function 0x005B9760 - return true}, and NotCD's {"NotCD", 0x005B9E50 - return false}.
void get_point(const char* name, script::NativeFunction function)
{
    if (script::NativeCommand* entry = script::find_c_function(name)) {
        script::bind_native_command(*entry, function);
    }
}

void get_run(const char* script) { script::launch_script(name_hash(script), nullptr, nullptr, false, script); }

// The view mode (the viewer's free camera): the script global view_mode, which change_view_mode copies to 0x007CE4A4.
bool in_view_mode() { return objects::view_mode() != 0; }

// A key's press (down now, up the frame before).
struct Key {
    SDL_Scancode code;
    bool was_down_;
};

inline bool squeezed(Key& key_record, platform::KeyState keys)
{
    const bool down = core::at(keys, key_record.code) != 0;
    const bool pressed = down && !key_record.was_down_;
    key_record.was_down_ = down;
    return pressed;
}

bool& started()
{
    static bool started{};
    return started;
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "debug_mode", 0) != 0;
    return on;
}

bool cfunc_cd(script::Struct* /*params*/, script::Script* script)
{
    return !is_switched_on() || is_startup_script(script); // (the original: true - 0x005B9760)
}

bool cfunc_not_cd(script::Struct* /*params*/, script::Script* script)
{
    return !cfunc_cd(nullptr, script); // (the original: false - 0x005B9E50)
}

void install()
{
    if (!is_switched_on()) {
        return;
    }
    get_point("CD", &cfunc_cd);
    get_point("NotCD", &cfunc_not_cd);
    get_log("debug mode: on (CD false / NotCD true after start-up: the developers' menus - DEBUG OPTIONS in the pause "
            "menu - and the viewer: F9 free camera)");
}

void tick_frame(modules::FrontEnd* front_end)
{
    if (!is_switched_on() ||
        frame_loop::frames_run() == 0) { // (not in the early self-tests, which run this on stand-in state)
        return;
    }
    const platform::KeyState keys = platform::keyboard_keys();
    if (keys.empty()) {
        return;
    }
    static Key f9{.code = SDL_SCANCODE_F9, .was_down_ = false};
    static Key f10{.code = SDL_SCANCODE_F10, .was_down_ = false};
    static Key f11{.code = SDL_SCANCODE_F11, .was_down_ = false};
    static Key f12{.code = SDL_SCANCODE_F12, .was_down_ = false};
    static Key tab{.code = SDL_SCANCODE_TAB, .was_down_ = false};
    static Key t{.code = SDL_SCANCODE_T, .was_down_ = false};
    static Key home{.code = SDL_SCANCODE_HOME, .was_down_ = false};
    // (every key's state noted each frame, so a press counts once wherever it happened)
    const bool pressed_f9 = squeezed(f9, keys);
    const bool pressed_f10 = squeezed(f10, keys);
    const bool pressed_f11 = squeezed(f11, keys);
    const bool pressed_f12 = squeezed(f12, keys);
    const bool pressed_tab = squeezed(tab, keys);
    const bool pressed_t = squeezed(t, keys);
    const bool pressed_home = squeezed(home, keys);
    if (frontend::level_editor::live_now() || (front_end && front_end->is_halted()) || render::current_level() == 0) {
        return;
    }
    if (!started()) {
        // In the first level (start-up long over; not the early self-tests, which run the front end's input logic
        // before the game is up): what the developers' start-up did - the Select combos on (cheat_select_shift) and
        // the viewer (LaunchViewer: the viewer module and its camera object viewer_cam, kept across levels; the PC
        // tool's network handler isn't made in this exe - 0x00684FA8 is set).
        started() = true;
        get_run("cheat_select_shift");
        get_run("LaunchViewer");
        get_log("debug mode: the viewer launched (select_shift on)");
    }
    // The Select combos (ButtonScripts.qb's UserSelect scripts, as the viewer's task runs them).
    if (pressed_f9) {
        get_run("UserSelectX"); // the free camera on / off (ToggleViewMode)
    }
    if (pressed_f10) {
        get_run("UserSelectTriangle"); // render mode: wireframe, wireframe skins, normal
    }
    if (pressed_f11) {
        get_run("UserSelectCircle"); // in a wireframe mode its style; else the skater put at the camera, camera off
    }
    if (pressed_f12) {
        get_run("UserSelectSquare"); // a screenshot
    }
    if (pressed_home) {
        get_run("UserSelectStart"); // rendering back to normal
    }
    // The free camera's buttons (the UserViewer scripts).
    if (in_view_mode()) {
        if (pressed_tab) {
            get_run("UserViewerCircle"); // speed: medium, fast, super slow
        }
        if (pressed_t) {
            get_run("UserViewerTriangle"); // the next time of day
        }
    }
}

bool keyboard_viewer(ViewerSticks sticks, uint32_t* commands)
{
    auto* const left_x = sticks.left_x_;
    auto* const left_y = sticks.left_y_;
    auto* const right_x = sticks.right_x_;
    auto* const right_y = sticks.right_y_;
    if (!is_switched_on() || frontend::level_editor::live_now()) {
        return false;
    }
    const platform::KeyState keys = platform::keyboard_keys();
    if (keys.empty()) {
        return false;
    }
    auto axis = [keys](SDL_Scancode minus, SDL_Scancode plus) {
        return (core::at(keys, plus) ? kStickFull : 0.0F) - (core::at(keys, minus) ? kStickFull : 0.0F);
    };
    const float move_x = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
    const float move_z = axis(SDL_SCANCODE_W, SDL_SCANCODE_S);
    const float turn_x = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    const float turn_y = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    const bool up = (core::at(keys, SDL_SCANCODE_E) != 0U) || (core::at(keys, SDL_SCANCODE_PAGEUP) != 0U);
    const bool down = (core::at(keys, SDL_SCANCODE_Q) != 0U) || (core::at(keys, SDL_SCANCODE_PAGEDOWN) != 0U);
    if (move_x == 0.0F && move_z == 0.0F && turn_x == 0.0F && turn_y == 0.0F && !up && !down) {
        return false;
    }
    // (the right stick moves - x across, y along the view: up is -127 -, the left stick turns; a key held wins over
    // the stick)
    if (move_x != 0.0F) {
        *right_x = move_x;
    }
    if (move_z != 0.0F) {
        *right_y = move_z;
    }
    if (turn_x != 0.0F) {
        *left_x = turn_x;
    }
    if (turn_y != 0.0F) {
        *left_y = turn_y;
    }
    if (up) {
        *commands |= 0x2;
    } else if (down) {
        *commands |= 0x4;
    }
    return true;
}

} // namespace sys::debug_mode
