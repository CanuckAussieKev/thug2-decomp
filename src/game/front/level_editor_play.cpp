// NOT ORIGINAL - play from here (ours). See level_editor_play.h.

#include "game/front/level_editor_play.h"

#include <SDL2/SDL.h>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/level_editor_layout.h"
#include "game/front/mouse_menus.h"
#include "game/gfx/screen.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/mlp.h"
#include "hook/log.h"

namespace Front::LevelEditor::Play {

Step OnKey(bool pressed, bool inLevel, bool paused, bool offered)
{
    if (!pressed || !inLevel || paused || !offered)
        return Step::None;
    return Step::PauseThenEnter;
}

bool KeyDown(const uint8_t* keys)
{
    if (!keys)
        return false;
    bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    return keys[SDL_SCANCODE_F5] || (ctrl && keys[SDL_SCANCODE_P]);
}

namespace {

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kUnpauseFrames = 2;       // (after level_editor_exit's pause menu, before it's closed)
constexpr int kPauseWaitFrames = 120;   // (for the game to pause after Start)
constexpr int kPauseMenuFrames = 10;    // (the pause menu up this long before LEVEL EDITOR is chosen)

bool g_requested;
bool g_haveResume;
int g_resumeLoad = -1;
Resume g_resume;
// After leaving: the pause menu closed a couple of frames later. Back to the editor: waiting for the pause, then
// for the pause menu.
enum class Phase { Idle, Unpause, WaitPause, WaitMenu };
Phase g_phase = Phase::Idle;
int g_frames;
bool g_keyUp = true;

// The overlay's level_editor_enter is loaded (tools/menu_level/make_menu_level.ts).
bool EnterScriptLoaded()
{
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(Crc::GenerateCRCFromString("level_editor_enter"));
    return symbol && symbol->type == Script::ESYMBOLTYPE_QSCRIPT;
}

} // namespace

void Request() { g_requested = true; }

bool TakeRequest()
{
    bool requested = g_requested;
    g_requested = false;
    if (requested) {
        g_phase = Phase::Unpause;
        g_frames = 0;
    }
    return requested;
}

void Keep(const Resume& resume, int levelLoad)
{
    g_resume = resume;
    g_resumeLoad = levelLoad;
    g_haveResume = true;
}

bool TakeResume(Resume* resume, int levelLoad)
{
    if (!g_haveResume || g_resumeLoad != levelLoad)
        return false;
    g_haveResume = false;
    *resume = g_resume;
    return true;
}

void Update(Mdl::FrontEnd* frontEnd)
{
    if (!LevelEditor::Enabled() || Mlp::FramesRun() == 0)
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    bool down = KeyDown(keys);
    bool pressed = down && g_keyUp;
    g_keyUp = !down;
    bool paused = frontEnd && frontEnd->m_paused;
    bool inLevel = Nx::CurrentLevel() != 0 && Obj::FindObject(kSkater) != nullptr;
    switch (g_phase) {
    case Phase::Unpause:
        // (level_editor_exit brought the pause menu back: closed as its CONTINUE closes it)
        if (++g_frames < kUnpauseFrames)
            return;
        g_phase = Phase::Idle;
        if (paused && inLevel) {
            Log("level editor: play from here - the pause menu closed");
            Script::RunScript("exit_pause_menu", nullptr, nullptr, false);
        }
        return;
    case Phase::WaitPause:
        if (!inLevel) {
            g_phase = Phase::Idle;
            return;
        }
        if (!paused) {
            if (++g_frames >= kPauseWaitFrames) {
                Log("level editor: back to the editor - the game didn't pause (the pause menu's LEVEL EDITOR opens it)");
                g_phase = Phase::Idle;
            }
            return;
        }
        g_phase = Phase::WaitMenu;
        g_frames = 0;
        return;
    case Phase::WaitMenu:
        if (!inLevel || !paused) {
            g_phase = Phase::Idle;
            return;
        }
        if (++g_frames < kPauseMenuFrames)
            return;
        g_phase = Phase::Idle;
        Log("level editor: back to the editor (the play key) - running level_editor_enter");
        Script::RunScript("level_editor_enter", nullptr, nullptr, false);
        return;
    case Phase::Idle:
        break;
    }
    if (!pressed || LevelEditor::Active())
        return;
    bool offered = EnterScriptLoaded() && CFunc_Available(nullptr, nullptr) && !Layout::Refused();
    if (OnKey(pressed, inLevel, paused, offered) != Step::PauseThenEnter) {
        if (inLevel && !paused && !offered)
            Log("level editor: the play key - the editor isn't offered here");
        return;
    }
    // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
    Log("level editor: back to the editor (the play key) - pausing");
    MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadStart);
    g_phase = Phase::WaitPause;
    g_frames = 0;
}

} // namespace Front::LevelEditor::Play
