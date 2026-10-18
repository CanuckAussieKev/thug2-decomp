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
