# mv-b: Movies (Bink) & streamed audio, 0x450F70-0x452D20

## Batch mv-b-1 (NOT run in the game yet): the playlist and music track commands (src/game/pcm/music.cpp)
- 0x450F70 PauseMusic, 0x4510B0 SetMusicVolume, 0x451130 SetMusicStreamVolume, 0x4511A0 AddTrackToPlaylist,
  0x4512D0 ClearPlaylist, 0x451350 SkipMusicTrack, 0x451380 SetRandomMode, 0x451400 ApplyTrackVolume,
  0x451450 IsTrackPlaying, 0x4514C0 GetNumTracks, 0x451500 GetTrackName, 0x4515A0 SetMusicMode,
  0x4515E0 LoadMusicHeader, 0x451C70 ChangeTrackState, 0x4520D0 PlayTrack - 15 REPLACE. The script commands of
  the same names (0x44D4D0... in cfuncs.csv) call them.
- Findings: 0x0069BAAC is what the music's channel plays (0 a track, 1 a music stream - 0x450EF0, 2 - 0x4509E0);
  0x0067D7E8 (GetSfxVolume's) is really the music *stream* volume; 0x0069BAA1 is set by the music's init
  (0x450D60). PauseMusic(-1) unpauses only if the music wasn't already paused when last paused (0x0069CAD0).
  Playlist 0's titles are one table by track index at 0x6999F8 (100 chars, "No title provided"). GetTrackName
  with the music off writes "Empty." over playlist 0's first track. Original quirk kept: ChangeTrackState's
  "any track on?" test checks every track against both masks, so only an empty playlist gets marked "nothing to
  play" (unknownCA8). Script::GetInteger(0xD7BB618D) is read and ignored (compiled-out debug print). The
  0x005B9760 `return true` stub is the PS2's file check (AddTrack, LoadMusicHeader).
- Test: TestPlaylistCommands (src/hook/selftests_mvb.cpp), original vs ours on stand-ins for the platform music
  calls. 40 deliberate bugs caught, 4 after strengthening (title terminator, the "no track" flag, a track one
  past the count, "Empty." length).

## Classes / types
- (none yet)
