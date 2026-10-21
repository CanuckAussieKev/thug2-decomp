#pragma once

#include <cstdint>
#include <vector>

#include "game/front/level_editor.h"

namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - play from here (ours; see level_editor.h), after Unreal's Play-In-Editor and Unity's Play button:
//   - In the editor, F5 or Ctrl+P closes it straight into play: the skater is put on the ground below the camera,
//     facing the camera's heading (as leaving always did: LevelEditor_Stop), then the pause menu that leaving brings
//     back is closed too (exit_pause_menu, the pause menu's CONTINUE) - the game runs at once.
//   - In play, F5 or Ctrl+P opens the editor again (Start pauses the game as a player's Start does, then the pause
//     menu's LEVEL EDITOR - level_editor_enter - as the -edit start does: start_level.h). If the editor was left by play
//     from here in the same level load, it comes back with the camera exactly where it was and the selection it had.
//   - The editor's own guards hold: never in a network session or a career-type mode (LevelEditor_Available /
//     Layout::Refused) - the key does nothing there; not while paused in a menu (the pause menu's LEVEL EDITOR is there).
// The play key in play is the editor's only key outside it; [features] level_editor=0 turns it off with the editor.
namespace Front::LevelEditor::Play {

// What the editor was showing when play from here left it (the editor fills it in: level_editor.cpp).
struct Resume {
    V3 pos = {};
    float yaw = 0.0f, pitch = 0.0f;
    float speed = 800.0f;
    std::vector<uint32_t> objects; // the selected objects' ids
    std::vector<uint32_t> sectors; // the selected scenery
    uint32_t node = 0;             // a node selected by itself (its name; 0: none)
};

// The play key's press, in the editor (F5, Ctrl+P): asks for play from here.
void Request();
// The editor's input logic (InputLogic, while it runs): a request taken - true once, then `resume` is kept (with the
// level load it belongs to) and the editor runs level_editor_exit.
bool TakeRequest();
void Keep(const Resume& resume, int levelLoad);
// The editor starting, once its camera is known: the state to put back - true only after play from here in the same
// level load, and only once.
bool TakeResume(Resume* resume, int levelLoad);

// Once a frame from the front end's input logic while the editor isn't running (MouseMenus::Update): the pause menu
// closed after leaving, the play key in play.
void Update(Mdl::FrontEnd* frontEnd);

// What the key does in play (pure): the play key pressed, and whether it's allowed (in a level, the skater there, not
// paused, the editor offered here).
enum class Step { None, PauseThenEnter };
Step OnKey(bool pressed, bool inLevel, bool paused, bool offered);
// Whether the play key is down: F5, or Ctrl+P (pure, from SDL's keyboard state).
bool KeyDown(const uint8_t* keys);

} // namespace Front::LevelEditor::Play
