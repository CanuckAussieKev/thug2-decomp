# mv-a: 0x44E6B0-0x450F70 (the start of "Movies (Bink) & streamed audio")

The range turned out to hold four things: the input manager (Inp), the game clock, the module manager (Mdl) and
the music/streams front end (Pcm) - the movie code itself (0x44F830-0x450220) was already done.

## Classes defined here (full layouts)
- `Inp::RecordedData`, `Inp::Data`, `Inp::Server`, `Inp::Manager` (src/game/sio/inpman.h) - the constructor and most
  methods are in this range (0x44E4F0-0x44E660, outside it, stay GameFunction calls: the manager's destructor
  0x44E530, the server's 0x44E520, the task 0x44E660, the getters/setters 0x44E590-0x44E640).
  front_end.h's `InputControlData` is the first part of `Inp::Data` (left as it is).
- `Mdl::Module`, `Mdl::Manager`, `Mdl::ModuleListDestructors` (src/game/sys/module.h). Mdl::FrontEnd and
  File::AsyncLoaderModule still spell the module's fields as bytes (left as they are).
- `Pcm::FamData` (src/game/pcm/music_api.h). Stream fields used by music_api.cpp are a local struct
  (`StreamFields`, anonymous namespace) so music.h's `Pcm::Stream` isn't changed.

## Batches
- [x] mv-a batch 1 (NOT run in the game yet): the input manager and the game clock - 13 functions, all REPLACE.
  - Inp::Data (0x44E840 MaskDigitalInput, 0x44E860 MaskAnalogInput, 0x44E8C0 ConvertDigitalToAnalog (table at
    0x67D730), 0x44E900 HandleAnalogTolerance (dead zone 50), 0x44EA30 OverridePadWithStick (the left stick
    works the d-pad; quirk kept: up's pressure is y + 0x80, growing towards the centre)); Inp::Server (0x44E9E0
    constructor, 0x44EAA0 ServiceHandlers: playback of a recording, else the device's PS2 control data by pad
    type 4/5/7, recording, the per-device button words 0x696D48/50/58, the handlers); Inp::Manager (0x44E700
    constructor; 0x44E7F0 / 0x44E6B0 the singleton).
  - The game clock (src/game/sys/game_clock): 0x44ECE0 GameClockElapsed (235 callers; the unsigned real time since
    the last change times the speed, _ftol truncation), 0x44ED10 PauseGameClock, 0x44ED70 SetGameClockSpeed.
  - Test (selftests_mva.cpp: TestInpManager, TestGameClock): 43 deliberate bugs, 42 caught (4 after splitting the
    manager's constructor test from the singleton's, whose stand-in had covered our constructor). cre1
    (`Instance() = nullptr` dropped on the out-of-memory path) is equivalent: only reached when it's null.

- [x] mv-a batch 2 (run in the game: see below): the module manager (src/game/sys/module) - 17 functions, all REPLACE.
  Mdl::Module (0x44F7E0 constructor - m_command left unset, as the original; 0x44F790 / 0x44F7B0 destructors),
  Mdl::Manager (0x44F5E0 constructor: the list head and its task, priority -1000; 0x44F680 destructor;
  0x44F3C0 RegisterModule (onto the end); 0x44F3E0 UnregisterModule (`this` unused); 0x44F310 StartModule (not
  when locked); 0x44F530 StopAllModules; 0x44F440 ProcessModules, the task: start / stop / restart by command and
  state), the singleton 0x44F6F0 / 0x44F740, and the module list's Lst destructors (Node 0x44F360 / 0x44F380,
  Head 0x44F410, Search 0x44F300 / 0x44F330). singleton.cpp's SingletonPtr instances for this and the input
  manager now call our Create/Release (selftests_gapsB's table updated to match).
  Test (selftests_mva2.cpp: TestModules): 25 deliberate bugs, 25 caught.
- [x] mv-a batch 3 (run in the game: see below): the music and streams front end (src/game/pcm/music_api) - 22
  functions, all REPLACE: NoMusicPlease 0x4505F0, StreamsDisabled 0x450620, StreamExists 0x450500,
  Get/SetCurrentTrack 0x450520 / 0x450530, StopMusic 0x450650, PlayTrack 0x450820 (the user soundtrack's next
  track), PlayMusicStream 0x450EF0 (its name unused on PC), PreLoadMusicStream 0x4509E0,
  PreLoadMusicStreamDone 0x450A50, StartPreLoadedMusicStream 0x450A80, StopStreams 0x450690, StopStream 0x4506D0,
  SetStreamVolume 0x450780, SetStreamPitch 0x4507D0 (a stub on PC), PreLoadStreamDone 0x450940,
  StartPreLoadedStream 0x450980, RemoveStream 0x450AE0 (pops the stream's queue head), UpdateStreamVolume
  0x450BE0 (positional streams; quirk kept: the playing check is of stream `slot`, not the one found), Init
  0x450D60 (quirk kept: only the first two of the three playlists emptied), UsingCD 0x450EB0, FamData::Load
  0x450540 (fam\<hex>.fam lip-sync data, < 4 KB).
  Test (selftests_mva3.cpp: TestMusicApi): 400 seeded cases over every function with the whole platform layer
  stood in (2200 seeded cases; extra ones for PlayTrack's user soundtrack and RemoveStream's queue): 40 deliberate
  bugs, 39 caught (8 after adding those cases and steering UpdateStreamVolume's owner ids onto streams). Not
  caught offline: SfxVolume back at 0x18 bytes (the overrun found in the game; the static_assert keeps it).

## In-game findings
- First in-game run (batches 1-3, before commit): crash in Pcm::UpdateStreamVolume (0x450BE0) in the front end -
  Sfx::sVolume is 0x2C bytes, not 0x18 (0x0047BE10 also writes a direction at +0x18 and a float at +0x28), so our
  smaller local was overrun. Fixed (SfxVolume 0x2C). Offline tests couldn't see it (the stand-in for 0x0047BE10
  doesn't write the volume).
- After the fix: intros skipped - starts, front end, a level loads and plays (music track shown), pause menu; log
  clean (no exceptions, run-time checks or self-test failures).
- Intros on (skip_intro_movies=0): the game hangs in the first intro movie, the main thread spinning (1 s CPU per s,
  not responding). Sampled with SuspendThread/GetThreadContext: it's in Nx::PlayMovieFile's loop
  `if (DeviceLost()) { ResetDeviceIfLost(); if (DeviceLost()) continue; }` (movie.cpp, 0x0044FDC0 - not this
  range). The original does the same (0x00450053-0x00450066: no Sleep, no window events while the device is lost),
  so once the D3D device is lost during a movie (here: the game started while another full-screen program had the
  display) nothing restores it. A quirk of the original; worth a Sleep + ProcessWindowEvents there as a fix.
  Not caused by this range's code.
