# mv-c: Movies (Bink) & streamed audio, 0x452D20-0x4544E0

The PC's streamed-audio platform layer: the music and three sound streams, each a Bink file played for its sound
(binkw32 through DirectSound), serviced by a thread every 5 ms under a mutex (0x006EE01C). The console versions'
raw pcm.wad stream class (WadStream) is still created in places but its playing is compiled out on PC (its update,
volume and "ready" calls are linker-folded stubs; nothing opens one with a real file).

## Classes defined (src/game/pcm/stream.h)
- `Pcm::WadStream` (0xA8): constructor 0x454340 and all its own methods are in this range.
- `Pcm::BinkStream` (0x1C0): 6 of its 8 methods are here (Open, Pause, SetVolume, SetVolumeAndPan, the
  destructor, Update); its constructor 0x452C00 and OpenFromWad 0x452C60 are mv-b's - called by address here.
- `Pcm::StreamSlot` (0x14; the music's at 0x6EDFF4, three streams' at 0x6EDFB8), `Pcm::WadEntry` (pcm.dat's
  index entries, 12 bytes), `Pcm::StreamVolume` (5 levels + a type).
- `Nx::Bink` (movie.h): three header fields added (lastFrameNum, frameRate, frameRateDiv) - additive.

## Batches
- [x] Batch mv-c-1 (run in the game: see below): the stream classes, src/game/pcm/bink_stream.cpp.
  BinkStream: Open 0x452D20 (Bink's DirectSound set up once - "Bink can't use DSound" - I/O size 0x40000, BinkOpen
  with BINKIOSIZE, video off, path kept for reopening; E_INVALIDARG if it won't open), Pause 0x452DC0 (a request
  the thread carries out; a lock-out flag 0x69CB98 that's never set), SetVolume 0x452DE0 (0-100 -> 0-32768 by
  327.68; BinkSetVolume even with no Bink), SetVolumeAndPan 0x452FC0 (volume = mean; level of the louder side,
  capped 32768; pan = right/(left+right) * 65536 with the float-rounded total), the destructor 0x452E30
  (BinkClose even of null, the file closed unless shared, the pcm.wad Bink entry's users counted down and last
  use noted), Update 0x452EA0 (thread: pause = close the Bink noting the frame, resume = reopen and BinkGoto;
  frames decoded while the window has focus; plays counted down at the last frame, finished at 0).
  WadStream: constructor 0x454340, StartRead 0x454370 (overlapped ReadFile of 8 KB; ERROR_IO_PENDING = 0,
  errors <= 0 returned raw, else HRESULT_FROM_WIN32), OpenFromWad 0x454450, Open 0x454490 (an invalid handle:
  HRESULT_FROM_WIN32(GetLastError()) with GetLastError called twice), PreloadDone 0x4543F0 (false unless
  preloading), Pause 0x454430, SetChannelVolumes 0x454440 (empty RET 0x14, also used for BinkStreams; source-only).
  Tests (src/hook/selftests_pcmstream.cpp, TestBinkStream / TestWadStream): original vs ours against stand-ins
  for binkw32's import slots, CloseHandle/ReadFile/GetLastError, Dbg::Printf and Mem free; 43 deliberate bugs
  caught, 9 of them after strengthening (volumes with fractions >= .5, a level of exactly 0x8001, a total not
  representable as a float, wad entry 0, an error above 0xFFFF, a non-preloading state with the read done,
  a negative pause).

- [x] Batch mv-c-2 (run in the game: see below): the slots and the platform layer, src/game/pcm/stream.cpp.
  StreamSlot::Create 0x453500 (new WadStream / BinkStream), Open 0x453090 (on a pcm.wad entry; -1 with no
  stream), SetVolumeAndPan 0x4530E0, Free 0x4538F0; StreamThread 0x453100 (every 5 ms under the lock: each
  stream's Update; skipped on WAIT_TIMEOUT), StopStreamThread 0x4531A0 (the handle isn't closed),
  NewOverlapped 0x4535E0 (a ring of 256; loops for ever if all are in use), Init 0x453670 (slots cleared, user
  soundtracks counted, pcm.wad opened overlapped + unbuffered, pcm.dat's index read and exchange-sorted by
  checksum, the thread started above normal priority), FindWadEntry 0x4533A0 (EBX; binary search; naked stub),
  StreamExists 0x4533E0, MusicStatus 0x453250 / StreamStatus 0x453270 (1 free, 2 playing; -1 = any of the
  three), MusicPreloadDone 0x453220 / StreamPreloadDone 0x4531D0, PauseStreams 0x4532C0, UpdateStreams
  0x453940 (frees what finished - unless E_PENDING - or was asked to stop), UserSoundtrackCount 0x4542D0 /
  Name 0x454300 / Tracks 0x454320 (none on PC).
- [x] Batch mv-c-3 (run in the game: see below): playing, src/game/pcm/stream_play.cpp.
  LoadMusicTrack 0x453C20 (<root>data\streams\music\%.8x.bik as a Bink stream), LoadMusicTrackByName
  0x453D70, PlayMusicTrack 0x453DB0, StartPreloadedMusic 0x453BF0, PlayUserTrack 0x453850 (always false on PC,
  leaving a WadStream in the music slot), StopMusic 0x453AA0, SetMusicVolume 0x453400, SyncMusicToTime 0x453470
  (cutscenes: BinkGoto when more than 5 frames off; assumes a Bink stream with a Bink open), SetStreamVolume
  0x453DF0 (levels x the sound effects volume / 100; 5 channels or left/right), LoadStream 0x454080, PlayStream
  0x454200, StartPreloadedStream 0x454270, StopStream 0x453B30, StopStreams 0x453BD0.
  Tests (src/hook/selftests_pcmplay.cpp, TestStreamPlatform / TestStreamPlay): original vs ours against stand-ins
  for the stream classes' methods (both addresses), the memory manager, Win32 (mutex, Sleep, files, thread),
  binkw32's BinkGoto slot, the sound manager and the C runtime's sprintf / strrchr. Batches 2 and 3: 77 deliberate
  bugs, 76 caught (14 after strengthening: both classes in one slot, flags differing between them, soundtrack
  index = count, a positive open error, the constructors' start flags, a frame exactly 5 off, 60 random
  five-channel volumes for the mean's rounding); 1 equivalent (the stream buffer's address: Bink streams don't
  use it, and LoadStream only makes Bink streams).

- [x] Batches mv-c-1..3 tested in the game (worktree build, under the lock): self-tests all pass (194 early, 17 late,
  2 engine-started), 1597/1597 replacements installed; Free Skate into a level, the pause menu (music paused) with
  the playing music track shown ("25 Ta Life - Over the Years": a Bink music stream loaded by LoadMusicTrack and
  run by the stream thread); a second run with intro movies on (Escape pressed through them) into a level. No
  crashes, debug-CRT reports or run-time check failures in thug2-decomp.log.

## Findings
- 0x5B9760 / 0x5BA1D0 / 0x5BAB90 / 0x48DCD0 / 0x454440 are the stream classes' linker-folded stubs on PC: they're
  written as inline members (ReadyToFree / PreloadDone true, Update 0, the volume calls empty).
- 0x8000000A (E_PENDING) in a stream's status word keeps a finished stream from being freed.
- 0x453100 (the stream thread's function) isn't a function in Ghidra (only referenced by CreateThread's argument).
