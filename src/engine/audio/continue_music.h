#pragma once

namespace script {
class Context;
} // namespace script

// NOT ORIGINAL: thug2-decomp.ini [features] continue_music=1 (default 0: the original's behaviour) keeps the song
// that's playing through level loads (the front end's own menus that are levels - the skate shop, CAS, the park
// editor - included) and run starts, instead of stopping it and starting another. When the song ends the playlist
// goes on as usual.
//
// What the original does on a level change, and what this skips:
//  - ChangeLevel (Skate::cfunc_change_level) stops the music before the level change;
//  - Cleanup (cfunc_cleanup, the level's unload) sweeps the sound effects up, and with them stops the music
//    (stop_all_voices);
//  - the script cleanup_before_loading_level then runs StopMusic (and PauseMusic: kept - the song is paused through
//    that load and goes on from where it was when the run's PauseMusic 0 comes);
//  - every .pre file loaded synchronously (the level's, at each load) stops the music and the streams first
//    (disc_mode_active: the consoles' disc can't stream music while it loads; on the PC the song plays from its own
//    file, read by the movie codec on the stream thread, so it plays on - the other streams still stop);
//  - every run start (GameFlow_StartRun: each level start, each new run of a timed mode) runs SkipMusicTrack.
// Left as they are: the player's own Skip (the SkipTrack script and the sound options), the music volume at 0 (the
// level then plays its ambient playlist: SetMusicMode stops the song), movies and cutscenes (their own stops, in
// code), the sound options' track preview and user soundtracks, and quitting. Pausing (the pause menu and the screens
// that pause the game through PauseMusicAndStreams) leaves the song playing too.
namespace audio::continue_music {

// The switch ([features] continue_music, read once; the Options screen sets it as it writes the ini).
bool& is_switched_on();

// Whether the StopMusic / SkipMusicTrack that `script` runs is a level change's or a run start's (the script is
// cleanup_before_loading_level or GameFlow_StartRun) and is to be skipped: false when switched off.
bool skips_script_stop(script::Context context);

// Whether the PauseMusic 1 that `script` runs is a menu's (the script PauseMusicAndStreams - the pause menu, its
// screens, the stats and goal messages, a controller pulled out) and is to be skipped: the song plays on under the
// paused game (the user's request, 2026-11-02; the other streams still pause). False when switched off.
bool skips_script_pause(script::Context context);

// While one lives and the switch is on, stop_music leaves the music playing (a level change's own stops: ChangeLevel
// and the level's Cleanup). Nested holds count.
class Hold {
public:
    Hold();
    ~Hold();
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;

private:
    bool held_;
};

// Whether a Hold is in force.
bool is_held();

} // namespace audio::continue_music
