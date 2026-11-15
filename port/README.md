# The macOS (arm64) port

A build of `thug2-decomp` for macOS on Apple Silicon: the same source as the Windows build, compiled with clang for
`arm64-apple-darwin`, running on SDL3 and the Vulkan renderer over MoltenVK.

**Status: it plays.** Boots to the front end, loads a level, renders on MoltenVK at 60 fps (16.67 ms a frame, 2115
frames in one run), the input reads the keyboard and a DualSense, and the sound mixes in SDL3's callback. The game's
own data (2.0 GB) is the only thing it needs from outside this repository — see "The game's data" below.

```
cmake --preset macos-arm64
cmake --build --preset macos-arm64          # 8 cores: ~6 minutes from cold
./out/build/macos-arm64/thug2-decomp        # run it beside a THUG2 PC install's Data folder
```

## What compiles

Every library of the game, unmodified in the game's own code:

`thug2_base`, `thug2_formats`, `thug2_wire`, `thug2_platform`, `thug2_engine`, `thug2_render_pc`, `thug2_game`,
`thug2_frontend`, `thug2_devtools`, `thug2_script_qb`, `thug2_gpu_vulkan`, `thug2_media_ffmpeg`, `thug2_fs_win32`,
`thug2_net_winsock`, `thug2_app` and the exe.

Left out of a portable configure (`THUG2_PORT_READY`, root `CMakeLists.txt`): `gpu_d3d9` (there is no Direct3D 9 here),
`audio_dsound`, `input_dinput`, the GameSpy SDK, the D3DX9 package, the upscalers, `retail_limits`, the editor's
launcher and its plugin.

The thirteen check programs build and run natively; eleven pass unchanged (`format_check`, `sim_check`,
`daylight_check`, `rt_scene_check`, `viewer_check`, `tess_check`, `scatter_check`, `fisheye_check`, `options_check`,
`frame_pacing_check`, `relief_check`). `parse_check` and `printf_check` differ, and only in their **reference** column:
both compare against the C runtime, and macOS's libc is not MSVC's (`%ld` given an `int` is undefined and the test
relies on LLP64's `long`, `1e+`, `nan` and a bare `-` answer differently). The game calls neither.

## How it is put together

Nothing of the game's layering changed; a Windows build is unaffected by every part of this.

| Piece | What it is |
|---|---|
| `CMakeLists.txt`, `CMakePresets.json` | `THUG2_PORTABLE` (`NOT WIN32`), a `THUG2_PORT_READY` layer list, clang flags instead of MSVC's, SDL3 from the system instead of the Visual C++ package, and the `macos-arm64` preset |
| `port/win32_shim/windows.h` + `port/win32.cpp` | a Win32 subset over POSIX: files (`pread`-backed `OVERLAPPED`), threads and their sync (`CREATE_SUSPENDED` included), waitable and multimedia timers, the registry (a file beside the exe), the ini file, `LoadLibrary`→`dlopen`, `VirtualAlloc`→`mmap`, `GlobalMemoryStatus`→Mach, the command line and module paths, and stubs for the window (SDL3's is the game's) |
| `port/macos_compat.h` | MSVC's C runtime, force-included in every file: `_stricmp`, the Annex-K `_s` writers, `sscanf_s` (a real one: it walks the format and drops MSVC's buffer-size arguments), `localtime_s`, `strtok_s`, `_strupr_s`, `mbstowcs_s`, `fopen_s`/`freopen_s`, `__rdtsc` (the monotonic clock) |
| `port/posix_disk.cpp` | `fs::Disk` over `open`/`pread`/`opendir`/`statvfs`, keeping Win32's results (the all-ones handle, the Win32 error numbers, FILETIME's 100 ns) |
| `port/posix_transport.cpp` | `net::Transport` over BSD sockets, keeping Winsock's results and `sockaddr_in`'s 16 bytes (the GameSpy SDK reads them) |
| `port/sdl_audio_device.cpp` | `audio::Device` over SDL3: 48 kHz stereo S16, the game's voices mixed in the audio callback (volume, pan, the 3D voices' range and side, streamed rings for music and movies) |
| `port/gamespy_stub.cpp` | every function of `platform/gamespy.h`, answering with nothing (no lobby, rooms, buddies or stats) |
| `port/platform_stubs.cpp` | `crash.cpp` (signal handlers + `backtrace`, in place of structured exception handling), `frame_profile.cpp` (off), `mute_audio.cpp`, `app_window.cpp`, `child_process.cpp` |
| `port/lzham_stub.cpp` | THUGPRO's compression, absent (LZHAM's source is MSVC's Win32 API; its "macOS" branch is MSVC inline assembly) |
| `port/app_main.cpp` | `main()`: the command line into the Win32 shim, then `WinMain` |
| `port/silent_audio_device.h`, `port/posix_*.h`, `port/gamespy_stub.cpp` | their interfaces |

### Bugs in the shared source that the port found

These are real (each one is a bug on any LP64 and/or non-MSVC build), and each is marked `NOT ORIGINAL (macOS port)`:

- **`base/core/format.h`** — `kLongSize` was hard-coded to 4 with the comment "as on Windows", while `classify<long>()`
  reads `sizeof(long)`: on LP64 a format string that is correct on both platforms (`%ld` with a `long`) is refused at
  compile time. Now `sizeof(long)`.
- **`engine/audio/stream.h`** — `using Result = long; // an HRESULT`: on LP64 an error code such as `0x80070057` is a
  *positive* 64-bit value, so every `<= 0` test inverts. Now `int32_t`.
- **`platform/registry.h`** — `unsigned long*` for what Win32 calls `DWORD*` (4 bytes on Windows, 8 here).
- **`platform/port_controls.h`** — `inline` on a function whose only definition is in a `.cpp`: clang emits no
  out-of-line copy, so every other file's call is an undefined symbol (MSVC emits one).
- **`platform/platform_config.cpp`, `platform/run_log.cpp`** — the executable's folder found by the last `\` only.
- **`engine/audio/audio_state.h`, `engine/gfx/screen_2d.h`** — a `void*` standing for a `HANDLE` (one type on Windows,
  two here).
- **`gpu_vulkan/vulkan_context.cpp`** — `VK_KHR_portability_enumeration` (with its flag) and `VK_KHR_portability_subset`
  were not enabled: MoltenVK makes no instance and no device without them. Both are now enabled when a driver offers
  them, which changes nothing for a driver that doesn't.
- **`gpu_vulkan/vulkan_loader.cpp`** — volk opens the loader by name and then `/usr/local/lib`: on Apple Silicon the
  loader and MoltenVK are Homebrew's under `/opt/homebrew/lib`, so those paths are tried too.
- **`platform/sdl_window.cpp`** — the game's window handle is Win32's `HWND` property; with none, the game read "no
  window" and quit. A stand-in handle keeps the window.
- **`game/skate/game_mode.cpp`** — `apply_match_style` reads the mode list the scripts make; with no data files it
  dereferenced the null. Logged instead.
- **`app/main.cpp`, `app/renderer_choice.cpp`, `app/CMakeLists.txt`, `media_ffmpeg/ffmpeg_api.cpp`** — the portable
  build's own paths: POSIX files, no DirectSound/DirectInput, no Direct3D 9, FFmpeg's dylibs in place of its DLLs.

## Where it stops now

```
pc: Window: 2048x1332 pixels, display scale 1.00
pc: Initializing Input Manager!
script pools: CComponent 360000, ...
vulkan: drawing on Apple M2 (dynamic rendering, push descriptors; shader and pipeline caches kept beside the exe)
vulkan: ray tracing off on Apple M2: not asked for ([Graphics] ray_tracing=1 turns it on)
   -> surface_kind::load_terrain_types, from the Skate module's start callback: the level's data isn't there
```

**The game's data.** A THUG2 **PC** install's `Data` folder is needed. The retail 3-CD release
(`rld-thua.bin` / `rld-thub.bin` / `rld-thuc.bin`, MODE1/2352 images) gives it without an installer and without Wine:

```sh
# 1. each .bin is a raw 2352-byte-sector CD image: take each sector's 2048 data bytes (MODE1: 16 bytes in)
python3 - <<'PY'
SECTOR, DATA = 2352, 16
for name in ('rld-thua', 'rld-thub', 'rld-thuc'):
    with open(name + '.bin', 'rb') as src, open(name + '.iso', 'wb') as dst:
        while chunk := src.read(SECTOR * 512):
            for i in range(0, len(chunk), SECTOR):
                dst.write(chunk[i + DATA:i + DATA + 2048])
PY

# 2. CD1 is a Windows Installer package (Tony Hawk's Underground 2.msi + .cab files), CD2 holds the four data cabs
#    (Pre.cab, Images.cab, levels.cab, pcm_st~1.cab), CD3 the movies as plain files
7zz x -ocd1 cd1.iso ; 7zz x -ocd2 cd2.iso
mkdir msi && cp cd1/*.cab cd1/*.msi cd2/*.cab msi/

# 3. the cabs' entries are MSI *stream* names; the real file names are in the .msi's File table, so msitools reads them
brew install msitools
msiextract -C out msi/*.msi        # -> out/.../Game/Data/...  (1.3 GB)

# 4. the movies are on CD3 as plain files
7zz x -ocd3 -y cd3.iso 'Setup/Data/Game/Data/*' && cp -R cd3/Setup/Data/Game/Data/* <game>/Data/
```

The result beside the exe: `Data/{anims,customparks,cutscenes,fam,fonts,images,levels,models,movies,pre,scripts,skeletons,
sounds,streams,textures}` — **2.0 GB**, and `Data/pre/qb_scripts.prx` is the file that proves it. Three publisher-logo
movies (`atvi.bik`, `nslogo.bik`, `Beenox_Shift.bik`) aren't in this release at all; the game logs them and carries on.

What does **not** work, tried: the Russian 2-CD repack (the payload is InstallShield 5 "z" cabinets and the `data1.hdr`
file table was stripped, so `unshield`, `7zz` and the installer itself are all out), and the R.G. Mechanics repack
(FreeArc containers whose blocks are **AES-256-CTR encrypted**, the key held by its Delphi installer). A native
arm64 FreeArc unpacker (`xredor/unarc`, a few macOS fixes: `sysinfo`, `get_nprocs`, `malloc.h`) reads their headers and
stops at the `aes-256/ctr` descriptor — keep it for any unencrypted repack.

## Left to do

Cheapest first, and none of them blocks the game from running:

1. **Real game data** (above) — the only thing between here and a playable build.
2. **Sound**: done — `port/sdl_audio_device.cpp` mixes the game's voices in SDL3's callback (the game's pan is a level
   ratio, `log10(left/right) * 1000`, not DirectSound's ±10000, and a streamed buffer is a ring that never ends).
3. **Online**: the GameSpy SDK has a POSIX platform layer; `platform/gamespy.cpp` and `cmake/gamespy_sdk.cmake` are
   what to port (until then `port/gamespy_stub.cpp` answers with nothing).
4. **THUGPRO's compression**: LZHAM builds with clang once its `lzham_platform.cpp`/`lzham_traits.h` MSVC-isms are
   worked around, or a portable codec is dropped in.
5. **The level editor** — its plugin is a Windows DLL loaded with `LoadLibrary` and sharing the MSVC C++ ABI; a
   `.dylib` with a C entry point is the port. `Import Level`'s child processes need `posix_spawn` in place of
   `CreateProcess` and its job object.
6. **Frame profiling** (`platform/frame_profile.cpp` integrates with the x64 unwind data).
7. The window's placement, kept in the ini, is not saved between runs.
8. **Packaging**: the binary loads the system's SDL3 and MoltenVK (Homebrew's). A distributable `.app` would carry them
   and its `Data` check, and be signed — today a copy of the binary has to be re-signed (`codesign --force --sign -`).
9. **The shared source's bugs**: the fixes listed above are correct on Windows too, and several (the `inline` in a
   header defined in a `.cpp`, the `long`-as-HRESULT, the format checker's `kLongSize`, the backslash path splits) bite
   any non-MSVC or LP64 build — worth upstreaming rather than keeping in this fork.

## The sound, and what was wrong with it

Four bugs, all in this port's own device (`port/sdl_audio_device.cpp`), each found by logging and measuring rather than
by ear:

1. **Two locks.** The callback locked one mutex and the game's `write_buffer` / `buffer_position` locked another (and
   `write_buffer` locked none), so samples were read while being written: a hiss, cropped notes, and a ring that looked
   like it jumped. One lock over all audio state fixed it.
2. **Two attenuation units.** A voice's volume comes from `sfx_voices` (`kLevelDecibelScale` = 2000 per decade,
   DirectSound's centibels); a movie's or a stream's from `ffmpeg_sound` (`kAttenuationPerDecade` = 10000/3). Undoing
   both with 2000 played every music track and movie 4 dB quiet. Each is undone with the constant its own code used.
3. **Pan units.** The engine sends DirectSound's ±10000 (visible in the log as `pan=-10000`), and the level's mono 44.1
   kHz streams were being read as log-ratios, silencing a whole channel — heard as "hiss on the left". Streams are now
   never panned (their material is centred or stereo already) and a pan takes the far side down 12 dB at most.
4. **A use-after-free.** `release_buffer` freed a voice but left its pointer in the list the audio thread walks (an
   "all outputs" registry that was never filled), so the mixer read freed memory: a `Bus error: 10` inside `mix_audio`
   about once a minute, and the rare notes that seemed to be swallowed. Each buffer now knows its output and is taken
   out of that list before it is freed.

The music is trimmed +2 dB and the effects −2 dB by ear (`kMusicTrim`, `kEffectTrim`), from the game's own 4 dB gap
between them; both are plain constants beside the master gain. `THUG2_AUDIO_DEBUG=1` in the environment logs the mix
(each buffer's format, attenuation, pan and position once a second, and the peak of the streamed and the one-shot halves
of the mix, with any voice cut, replaced or freed while it was still sounding).

## Notes for anyone picking this up

- The Windows build is untouched: every change is inside `if(THUG2_PORTABLE)` / `#ifdef THUG2_PORTABLE`, and the few
  edits to shared files are marked and are correct on Windows too.
- `-fsigned-char` is not optional: `char` is unsigned on arm64 and signed on x86/Windows, and the game reads bytes from
  files and scripts into `char` and compares them with negative values.
- The overlapped file reads are done at once (`pread`): nothing is left pending, so the game's asynchronous file layer
  is correct, only slower to fill.
- Ray tracing is off on MoltenVK (no `VK_KHR_ray_query`); the renderer already degrades gracefully, and
  `[Graphics] ray_tracing=1` is what asks for it.
