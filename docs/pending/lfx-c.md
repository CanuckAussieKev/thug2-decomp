# lfx-c: 0x0059C900-0x0059F610 (Lighting / FX / park memory, third part)

Records for the lead to fold into docs/decomp-progress.md.

- [x] Batch lfx-c 1 (NOT run in the game yet): the skate game's script commands at the end of the range and two
  helpers - src/game/script/skate_cfuncs.cpp, src/game/core/dump_text.cpp (24 functions, all REPLACE).
  - CFunc_ControllerDebounce 0x0059EDF0 (button not "pressed" for `time` s: per-button times at 0x007CE758,
    indexed like the button table 0x0067D6D8 via 0x0044E5A0; x87 unsigned clock + time*1000 truncated through
    64 bits), ControllerPressed 0x0059EE80 (the helper of CFunc_ControllerPressed 0x0059F9A0: the controller's
    input state at +0x18, one byte per button - right +4, left +5, up +6, down +7, triangle +8, circle +9,
    x +0xA, square +0xB, L1 +0xC, R1 +0xD, L2 +0xE, R2 +0xF, black +0x16, white +0x17, z +0x18; diagonals
    need both; debounced while the time is later than now (unsigned, strict)).
  - FakeLights 0x0059F0F0 (id= changes one light, 0x004901D0; prefix= lists nodes 0x0045BF90 with a 16-bit
    count and sets lights up 0x0048FE40; flag 0x80804991 = not a prefix), UpdateFxLighting, NodeExists,
    LoadNodeArray (kills the session's spawned scripts, notes the name at 0x007CE6D8 unbounded, loads the .qb),
    ResetCamera ("TODO: ScriptResetCamera not implemented" per viewport with a camera), Set{Movement,Rotate}Velocity
    (viewer speeds, defaults 400 / 120), ClearViewerObject, CasArtist (global cas_artist), SpawnScript (all the
    options; NotSessionSpecific is an integer; at the caller's node; notes the caller's .qb file unless flag
    0x01917DF2 - and, as in the original, calls SetSpawnerFile on a null result), UnBlockInterruptedScript,
    KillSpawnedScript (Name= first, else Id=), Pause/UnPauseSpawnedScripts, DisplayLoadingScreen (default
    "images\xxxxx", freeze, blank, a float = the bar's time), HideLoadingScreen, LastBroadcastedOmnigonWas /
    OmnigonAllowed (checksum 0xAE94C183 vs 0x007CEBC8), SetLevelName (sprintf with the name as the format -
    kept), DumpScripts (compiled out).
  - DumpText 0x0059ED60: the pool-usage dump's (0x00448330) line buffer 0x007CE7B8-0x007CEBA8, '\n' -> CR LF,
    flushed with File::Write to the handle at 0x007CEBB8 - which nothing ever sets (a dead log in this build).
    The fit check is made before the CR LF expansion and skipped for an empty buffer (both can overrun - kept).
  - Test: TestSkateCFuncs (src/hook/selftests_lfxc.cpp, Early): every command against the original on
    hand-built structures, every game function they call stood in for by recording code (StandIns), all 20
    buttons over 24 input patterns with debounced entries, the dump buffer through two flushes and an overrun.
    33 deliberate bugs caught, 3 of them after strengthening (the debounce table recorded before ControllerPressed
    overwrote it).
  - Unknown checksum names: 0x80804991 (FakeLights), 0x01917DF2 (SpawnScript), 0xAE94C183 (Omnigon),
    0x2AB71EEF (DumpScripts).
