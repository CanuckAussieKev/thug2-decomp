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

## Batch mv-b-2 (NOT run in the game yet): the streams and the PC's Bink stream files
- src/game/pcm/stream.cpp: 0x451010 PauseStream, 0x451620 LoadStreamHeader, 0x451660 IsStreamPlaying,
  0x4516A0 LoadStreamFrameAmp, 0x4516E0 GetStreamFrameAmp, 0x451720 FreeStreamFrameAmp, 0x451770 StartStream,
  0x451960 PlayStream, 0x451A50 PreLoadStream, 0x451B80 AddStream, 0x451E20 PlayStreamFromObject (11 REPLACE)
  + 0x451E7D REPLACE_SOURCE_ONLY (not a function: Ghidra split 0x451E20's body there).
- src/game/pcm/bink_stream.cpp: 0x452A90 LoadStreamFile (12-slot cache of stream files read into memory with the
  game's C runtime), 0x452C00 BinkStreamView::Construct, 0x452C60 BinkStreamView::Open (BinkOpen from memory,
  video off; BinkSetSoundSystem(DirectSound) once) - 3 REPLACE.
- Findings: Pcm::Stream's fields filled in (volume, dropoff, priority, option, nameCount, startFrame, usePosInfo,
  dropoffFunction, names[8]; `listener` is the stream's FrameAmp slot). A new stream takes a free channel or the
  one with the lowest priority / oldest start (unsigned frames) if the new priority is at least that. PlayStream
  callers push a 7th argument that's ignored; StreamIdFinished's answer is ignored. 0x0069BAAA blocks new streams
  (nothing sets it). AddStream has no bound on its 8 names (original quirk). PC streams are audio-only Bink files.
- Tests: TestStreamCommands, TestBinkStreamFiles (selftests_mvb.cpp). 58 deliberate bugs: 54 caught (6 after
  strengthening: equal start frames, usePosInfo cleared, name counts kept in bounds, equal LRU ages); 4 equivalent:
  ChannelStatus != 2 vs == 1 (0x453270 returns only 1 or 2), the start frame as int (compared unsigned anyway),
  SetSilent after the sVolume constructor (it already zeroes), the volume scale's association (same float).

## Classes / types
- Pcm::Stream (music.h, existed): filled in its unknown fields - check against mv-a if it also did (0x450BE0 reads them).
- Pcm::FrameAmp (stream.h, new): the frame-amplitude slot {checksum, size, data[0x1000]} - 3 of its 4 functions are
  mine (0x450540, its load, is mv-a's and is called by address).
- Pcm::BinkStreamView (stream.h, new): only the Bink stream's constructor and Open, over offsets (no layout). The
  class (0x1C0 bytes, made at 0x453500) has its other methods in mv-c's range: if mv-c defines it, fold these two
  methods in there and delete BinkStreamView.
