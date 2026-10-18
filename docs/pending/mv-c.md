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
- [x] Batch mv-c-1 (NOT run in the game yet): the stream classes, src/game/pcm/bink_stream.cpp.
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

## Findings
- 0x5B9760 / 0x5BA1D0 / 0x5BAB90 / 0x48DCD0 / 0x454440 are the stream classes' linker-folded stubs on PC: they're
  written as inline members (ReadyToFree / PreloadDone true, Update 0, the volume calls empty).
- 0x8000000A (E_PENDING) in a stream's status word keeps a finished stream from being freed.
- 0x453100 (the stream thread's function) isn't a function in Ghidra (only referenced by CreateThread's argument).
