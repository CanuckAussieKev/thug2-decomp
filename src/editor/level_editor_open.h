#pragma once

namespace modules {
struct FrontEnd;
}

// NOT ORIGINAL - the level editor opened in the level -edit / edit_level asked for (ours, at the user's request;
// game/front_end/start_level.h loads the level, then asks the extensions - the editor's: level_editor_extension.h).
// Once the level is up - the skater there, not paused, a second and a half settled - Start is pressed for the player
// (the pad_start event the menus get: handle_start_pressed pauses the game and makes the pause menu), and once the
// pause menu is up, level_editor_enter runs - what the pause menu's LEVEL EDITOR item runs. With [features]
// level_editor=0, without the overlay's level_editor_enter, or where the editor isn't offered (a network session, a
// career-type mode: level_editor_layout.h) it is logged and the level stays free skate.
namespace frontend::level_editor::open_in_level {

// The requested level's load began: the editor opens once it is up.
void begin();
// Once a frame from start level's frame (after its -at teleport, if any): the steps above.
void tick_frame(modules::FrontEnd* front_end);

} // namespace frontend::level_editor::open_in_level
