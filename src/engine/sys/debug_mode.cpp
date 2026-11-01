// Debug mode (engine/sys/debug_mode.h).

#include "engine/sys/debug_mode.h"

#include <cstdint>
#include <cstring>

#include <SDL2/SDL.h>

#include "engine/core/checksums.h"
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

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// The start-up scripts, which keep the disc build's CD: false there makes default_system_startup take the
// developers' path - no UsePreFilesForLevelLoading (their loose files: the retail data's archives aren't loaded, the
// front end's peds lose their animations - a crash at 0x00482BA0), auto_launch_viewer, the script debugger -
// instead of the main menu; autolaunch and startup_loading_screen run with it.
bool is_startup_script(const script::Script* script)
{
    if (!script)
        return false;
    // (the script it's running now: CScript +0xC8)
    const uint32_t name = script->script_title_;
    static const uint32_t kStartup[] = {name_hash("default_system_startup"), name_hash("autolaunch"),
                                        name_hash("startup_loading_screen")};
    for (const uint32_t startup : kStartup) {
        if (name == startup)
            return true;
    }
    return false;
}

// The table's CD entry is {"CD", function 0x005B9760 - return true}, and NotCD's {"NotCD", 0x005B9E50 - return false}.
void get_point(const char* name, void* function)
{
    if (script::NativeCommand* entry = script::find_c_function(name))
        script::bind_native_command(*entry, function);
}

void get_run(const char* script) { script::launch_script(name_hash(script), nullptr, nullptr, false, script); }

// The view mode (the viewer's free camera): the script global view_mode, which change_view_mode copies to 0x007CE4A4.
bool in_view_mode() { return objects::g_view_mode != 0; }

// A key's press (down now, up the frame before).
struct Key {
    SDL_Scancode code;
    bool was_down_;
    bool squeezed(const uint8_t* keys)
    {
        const bool down = keys[code] != 0;
        const bool pressed = down && !was_down_;
        was_down_ = down;
        return pressed;
    }
};

bool s_started;

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
    if (!is_switched_on())
        return;
    get_point("CD", reinterpret_cast<void*>(&cfunc_cd));
    get_point("NotCD", reinterpret_cast<void*>(&cfunc_not_cd));
    get_log("debug mode: on (CD false / NotCD true after start-up: the developers' menus - DEBUG OPTIONS in the pause "
            "menu - and the viewer: F9 free camera)");
}

void tick_frame(modules::FrontEnd* front_end)
{
    if (!is_switched_on() ||
        frame_loop::frames_run() == 0) // (not in the early self-tests, which run this on stand-in state)
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (!keys)
        return;
    static Key f9{SDL_SCANCODE_F9, false}, f10{SDL_SCANCODE_F10, false}, f11{SDL_SCANCODE_F11, false},
        f12{SDL_SCANCODE_F12, false}, tab{SDL_SCANCODE_TAB, false}, t{SDL_SCANCODE_T, false},
        home{SDL_SCANCODE_HOME, false};
    // (every key's state noted each frame, so a press counts once wherever it happened)
    const bool pressed_f9 = f9.squeezed(keys);
    const bool pressed_f10 = f10.squeezed(keys);
    const bool pressed_f11 = f11.squeezed(keys);
    const bool pressed_f12 = f12.squeezed(keys);
    const bool pressed_tab = tab.squeezed(keys);
    const bool pressed_t = t.squeezed(keys);
    const bool pressed_home = home.squeezed(keys);
    if (frontend::level_editor::live_now() || (front_end && front_end->is_halted_) || render::current_level() == 0)
        return;
    if (!s_started) {
        // In the first level (start-up long over; not the early self-tests, which run the front end's input logic
        // before the game is up): what the developers' start-up did - the Select combos on (cheat_select_shift) and
        // the viewer (LaunchViewer: the viewer module and its camera object viewer_cam, kept across levels; the PC
        // tool's network handler isn't made in this exe - 0x00684FA8 is set).
        s_started = true;
        get_run("cheat_select_shift");
        get_run("LaunchViewer");
        get_log("debug mode: the viewer launched (select_shift on)");
    }
    // The Select combos (ButtonScripts.qb's UserSelect scripts, as the viewer's task runs them).
    if (pressed_f9)
        get_run("UserSelectX"); // the free camera on / off (ToggleViewMode)
    if (pressed_f10)
        get_run("UserSelectTriangle"); // render mode: wireframe, wireframe skins, normal
    if (pressed_f11)
        get_run("UserSelectCircle"); // in a wireframe mode its style; else the skater put at the camera, camera off
    if (pressed_f12)
        get_run("UserSelectSquare"); // a screenshot
    if (pressed_home)
        get_run("UserSelectStart"); // rendering back to normal
    // The free camera's buttons (the UserViewer scripts).
    if (in_view_mode()) {
        if (pressed_tab)
            get_run("UserViewerCircle"); // speed: medium, fast, super slow
        if (pressed_t)
            get_run("UserViewerTriangle"); // the next time of day
    }
}

bool keyboard_viewer(float* left_x, float* left_y, float* right_x, float* right_y, uint32_t* commands)
{
    if (!is_switched_on() || frontend::level_editor::live_now())
        return false;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (!keys)
        return false;
    auto axis = [keys](SDL_Scancode minus, SDL_Scancode plus) {
        return (keys[plus] ? 127.0f : 0.0f) - (keys[minus] ? 127.0f : 0.0f);
    };
    const float move_x = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
    const float move_z = axis(SDL_SCANCODE_W, SDL_SCANCODE_S);
    const float turn_x = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    const float turn_y = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    const bool up = keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_PAGEUP];
    const bool down = keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_PAGEDOWN];
    if (move_x == 0.0f && move_z == 0.0f && turn_x == 0.0f && turn_y == 0.0f && !up && !down)
        return false;
    // (the right stick moves - x across, y along the view: up is -127 -, the left stick turns; a key held wins over
    // the stick)
    if (move_x != 0.0f)
        *right_x = move_x;
    if (move_z != 0.0f)
        *right_y = move_z;
    if (turn_x != 0.0f)
        *left_x = turn_x;
    if (turn_y != 0.0f)
        *left_y = turn_y;
    if (up)
        *commands |= 0x2;
    else if (down)
        *commands |= 0x4;
    return true;
}

} // namespace sys::debug_mode
