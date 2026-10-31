// Debug mode (game/sys/debug_mode.h).

#include "game/sys/debug_mode.h"

#include <cstdint>
#include <cstring>

#include <SDL2/SDL.h>

#include "game/core/checksums.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/gfx/screen.h"
#include "game/script/cscript.h"
#include "game/sys/mlp.h"
#include "game/script/script_tables.h"
#include "game/sys/run_log.h"
#include "game/obj/movie_manager.h"

namespace SystemCore::DebugMode {

namespace {

uint32_t NameHash(const char* name) { return Checksums::HashFromText(name); }

// The start-up scripts, which keep the disc build's CD: false there makes default_system_startup take the
// developers' path - no UsePreFilesForLevelLoading (their loose files: the retail data's archives aren't loaded, the
// front end's peds lose their animations - a crash at 0x00482BA0), auto_launch_viewer, the script debugger -
// instead of the main menu; autolaunch and startup_loading_screen run with it.
bool IsStartupScript(const Scripts::CScript* script)
{
    if (!script)
        return false;
    // (the script it's running now: CScript +0xC8)
    uint32_t name = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(script) + 0xC8);
    static const uint32_t kStartup[] = {NameHash("default_system_startup"), NameHash("autolaunch"),
                                        NameHash("startup_loading_screen")};
    for (uint32_t startup : kStartup) {
        if (name == startup)
            return true;
    }
    return false;
}

// The table's CD entry is {"CD", function 0x005B9760 - return true}, and NotCD's {"NotCD", 0x005B9E50 - return false}.
void Point(const char* name, void* function)
{
    if (Scripts::CFunctionEntry* entry = Scripts::FindCFunction(name))
        Scripts::BindNativeCommand(*entry, function);
}

void Run(const char* script) { Scripts::LaunchScript(NameHash(script), nullptr, nullptr, false, script); }

// The view mode (the viewer's free camera): the script global view_mode, which ChangeViewMode copies to 0x007CE4A4.
bool InViewMode() { return GameObjects::g_viewMode != 0; }

// A key's press (down now, up the frame before).
struct Key {
    SDL_Scancode code;
    bool wasDown;
    bool Squeezed(const uint8_t* keys)
    {
        bool down = keys[code] != 0;
        bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    }
};

bool s_started;

} // namespace

bool& IsSwitchedOn()
{
    static bool on = ConfigInt("features", "debug_mode", 0) != 0;
    return on;
}

bool __cdecl CFunc_CD(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    return !IsSwitchedOn() || IsStartupScript(script); // (the original: true - 0x005B9760)
}

bool __cdecl CFunc_NotCD(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    return !CFunc_CD(nullptr, script); // (the original: false - 0x005B9E50)
}

void Install()
{
    if (!IsSwitchedOn())
        return;
    Point("CD", reinterpret_cast<void*>(&CFunc_CD));
    Point("NotCD", reinterpret_cast<void*>(&CFunc_NotCD));
    Log("debug mode: on (CD false / NotCD true after start-up: the developers' menus - DEBUG OPTIONS in the pause "
        "menu - and the viewer: F9 free camera)");
}

void TickFrame(GameModules::FrontEnd* frontEnd)
{
    if (!IsSwitchedOn() || FrameLoop::FramesRun() == 0) // (not in the early self-tests, which run this on stand-in state)
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (!keys)
        return;
    static Key f9{SDL_SCANCODE_F9, false}, f10{SDL_SCANCODE_F10, false}, f11{SDL_SCANCODE_F11, false},
        f12{SDL_SCANCODE_F12, false}, tab{SDL_SCANCODE_TAB, false}, t{SDL_SCANCODE_T, false},
        home{SDL_SCANCODE_HOME, false};
    // (every key's state noted each frame, so a press counts once wherever it happened)
    bool pressedF9 = f9.Squeezed(keys), pressedF10 = f10.Squeezed(keys), pressedF11 = f11.Squeezed(keys),
         pressedF12 = f12.Squeezed(keys), pressedTab = tab.Squeezed(keys), pressedT = t.Squeezed(keys),
         pressedHome = home.Squeezed(keys);
    if (MenuSystem::LevelEditor::LiveNow() || (frontEnd && frontEnd->m_isHalted) || Engine3D::CurrentLevel() == 0)
        return;
    if (!s_started) {
        // In the first level (start-up long over; not the early self-tests, which run the front end's input logic
        // before the game is up): what the developers' start-up did - the Select combos on (cheat_select_shift) and
        // the viewer (LaunchViewer: the viewer module and its camera object viewer_cam, kept across levels; the PC
        // tool's network handler isn't made in this exe - 0x00684FA8 is set).
        s_started = true;
        Run("cheat_select_shift");
        Run("LaunchViewer");
        Log("debug mode: the viewer launched (select_shift on)");
    }
    // The Select combos (ButtonScripts.qb's UserSelect scripts, as the viewer's task runs them).
    if (pressedF9)
        Run("UserSelectX"); // the free camera on / off (ToggleViewMode)
    if (pressedF10)
        Run("UserSelectTriangle"); // render mode: wireframe, wireframe skins, normal
    if (pressedF11)
        Run("UserSelectCircle"); // in a wireframe mode its style; else the skater put at the camera, camera off
    if (pressedF12)
        Run("UserSelectSquare"); // a screenshot
    if (pressedHome)
        Run("UserSelectStart"); // rendering back to normal
    // The free camera's buttons (the UserViewer scripts).
    if (InViewMode()) {
        if (pressedTab)
            Run("UserViewerCircle"); // speed: medium, fast, super slow
        if (pressedT)
            Run("UserViewerTriangle"); // the next time of day
    }
}

bool KeyboardViewer(float* leftX, float* leftY, float* rightX, float* rightY, uint32_t* commands)
{
    if (!IsSwitchedOn() || MenuSystem::LevelEditor::LiveNow())
        return false;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (!keys)
        return false;
    auto axis = [keys](SDL_Scancode minus, SDL_Scancode plus) {
        return (keys[plus] ? 127.0f : 0.0f) - (keys[minus] ? 127.0f : 0.0f);
    };
    float moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D), moveZ = axis(SDL_SCANCODE_W, SDL_SCANCODE_S);
    float turnX = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT), turnY = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    bool up = keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_PAGEUP];
    bool down = keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_PAGEDOWN];
    if (!moveX && !moveZ && !turnX && !turnY && !up && !down)
        return false;
    // (the right stick moves - x across, y along the view: up is -127 -, the left stick turns; a key held wins over
    // the stick)
    if (moveX)
        *rightX = moveX;
    if (moveZ)
        *rightY = moveZ;
    if (turnX)
        *leftX = turnX;
    if (turnY)
        *leftY = turnY;
    if (up)
        *commands |= 0x2;
    else if (down)
        *commands |= 0x4;
    return true;
}

} // namespace SystemCore::DebugMode
