#pragma once

namespace script {
class Context;
} // namespace script

// NOT ORIGINAL - the live pause: thug2-decomp.ini [features] live_pause=1 (the default; 0: the original's freeze). In
// single-player free skate the pause menu no longer stops the world: peds, cars, moving objects, particles, the sky's
// time of day and the level's scripts keep running and every frame is drawn, as a network game's pause leaves them -
// so a graphics change in the Options screen shows behind the menu at once.
//
// The skater is the one thing held: frozen where he was (the original pause's own freeze of him - Rider::get_freeze:
// no input, no physics, his combo, balance and score as they were), so nothing he does can land a combo, bail or set
// off a trigger while the menu is up. (The net pause lets him roll on without input; in a single-player game that
// would change the score behind the menu - not wanted.)
//
// What decides it: the script command PauseGame run by handle_start_pressed (the Start button's pause - the pause menu
// itself; any other PauseGame - a cutscene, a goal's intro, a menu of its own - pauses as the original does) when
//  - the switch is on and no demo is being recorded or played (devtools/demo.h - a recording made with one setting
//    and played with the other would part: so a demo's pause is always the original's freeze);
//  - the game mode is free skate (freeskate) - not career / story (its goals and their timers), classic, single
//    session (a timed run), the 2-player modes, horse, trick attack or any network game;
//  - not in the park editor (editing or test-playing a park), not previewing a created trick (in_cat_preview_mode).
// The scripts around it run as before: GoalManager_PauseAllGoals, the music and streams paused (PauseMusicAndStreams),
// the rain paused, the HUD hidden; GameIsPaused answers yes (the scripts that ask hold the skater or put the pause
// back as they found it - a controller pulled out). Sound effects of the world play on, as in the net pause (the
// original's freeze silences them: AudioMixer::is_frozen). Other PauseGames while it's on (the pause menu's own screens) keep it live;
// UnPauseGame - or any code's FrontEnd::get_pause_game - ends it (the level editor's LevelEditor_Start turns it into
// the full freeze its tools expect).
namespace modules::live_pause {

// The switch ([features] live_pause, read once; the Options screen sets it as it writes the ini).
bool& is_switched_on();

// Set by the demo recorder while a demo records or plays (devtools/demo.h): the original freeze then.
bool& demo_running();

// Whether the PauseGame that `context` runs is to be a live pause (the rules above).
bool applies(script::Context context);

} // namespace modules::live_pause
