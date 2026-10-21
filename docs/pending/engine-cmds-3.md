# Engine script commands 0x0044B7E0-0x0044E6B0 (script utilities, arrays, music & streams, sounds) - engine-cmds-3 agent

Branch worktree-agent-a4915cf1938012168. Tests in src/hook/selftests_ec3.cpp (TestScriptUtilCFuncs ...).
Shared files touched: CMakeLists.txt (new files listed).

- [x] Batch EC3-1 (NOT run in the game yet; the script's own parameters, random values, arrays, goto, checksums and
  text - 16 CFunctions): src/game/script/script_util_cfuncs.h / .cpp (new).
  - 0x0044B7E0 RemoveParameter (the component and the flag), 0x0044B820 RemoveFlag, 0x0044B850 GetRandomValue
    (integer: a..b inclusive, either order; float: a + Rnd(resolution (1000)) * (b - a) / (resolution - 1), x87 in
    double, a and b not swapped), 0x0044B9A0 SizeOf (ArraySize via AddComponentOfType), 0x0044B9E0
    GetNextArrayElement (index read from the script's own parameters; -1 / negative = the first; false at the end;
    element by type, other types print 0x00647094 and are still true), 0x0044BBD0 GetRandomArrayElement (strings
    not supported: the error), 0x0044BD30 GotParam, 0x0044BD60 Goto, 0x0044BDC0 GotoRandomScript (only an array of
    names), 0x0044BE20 FormatText (0x0044A480 - the middle agent's range - via GameFunction), 0x0044BE40
    MangleChecksums (a + b, each a checksum or an asserted integer), 0x0044BEC0 AppendSuffixToChecksum
    (ExtendCRCWithString), 0x0044BF20 GetUpperCaseString (128-byte buffer), 0x0044BFA0 AddFlag, 0x0044BFD0
    AppendSuffix (" %d" - a space before the number; the text cut to the limit (15) less the number's length - with
    a number longer than the limit the cut lands before the result, in the number's buffer: the two buffers kept
    in the original's order so that quirk stays harmless; our first version wrote before its own buffer and the
    /GS check failed), 0x0044C120 IsObjectScript.
  - Original quirks kept and noted: uninitialised locals in MangleChecksums / AppendSuffixToChecksum /
    GetUpperCaseString / AddFlag when a parameter is missing (here 0 / null).
  - Test TestScriptUtilCFuncs: original vs ours over hand-built structures and arrays (every element type, index
    edges, empty / missing arrays, objects or not, AppendSuffix's cut cases); the structure-changing methods,
    SetScript, Rnd, Printf, sprintf and 0x0044A480 stood in for by recording code. 30 of 31 deliberate bugs caught;
    the miss (AppendSuffix's `>` as `>=`) is equivalent (cutting at the text's own end changes nothing).
- [x] Batch EC3-2 (NOT run in the game yet; ForEachIn, array building and changing, printf text, Dump - 11 functions):
  src/game/script/script_util_cfuncs.h / .cpp.
  - 0x0044C290 ForEachIn (with params: a structure per element - the element's components then the params - run
    now on the script's object; without: the element itself as the parameters; no array crashes, as in the
    original), 0x0044C430 GetElement (by the name "Index"; integers, structure copies handed over by pointer
    (AddComponentOfType 11), arrays copied; other types print 0x00647110; true without an array, false out of
    range), 0x0044C5A0 PermuteArray (by name strings "Array" / "PermuteSource" / "NewArrayName" /
    "MakeNewFirstDifferFromOldLast": size * 10 random swaps of a copy or the array itself; a first element equal
    to the old last one swapped with a random other), 0x0044C730 GotoPreserveParams, 0x0044C840 FormatPrintfText
    (helper of ScriptAssert: "%s\n" of the text put together by 0x0044A480 with TextName=PrintfText, "" if that
    fails; "%d\n" of an integer; the third argument unused), 0x0044C9B0 Dump (builds "name=%a" with a copy of the
    parameter as a, or "name=[!! DOES NOT EXIST !!]", and frees it: the printing is compiled out), 0x0044CB70
    ScriptAssert (the message only; the assert is compiled out), 0x0044CBC0 CreateIndexArray (index_array),
    0x0044CCC0 AppendStruct (a copy of params added by pointer to the global structure `struct` as `field`),
    0x0044CDB0 StringToCharArray (char_array of one-character strings), 0x0044CEC0 SetArrayElement (the script's
    array; NewValue resolved if it names a global, copied; the old element's string / pair / vector / structure /
    array freed; float arrays take integers; other types untouched).
  - Test TestScriptArrayCFuncs: original vs ours with the pools, structure / array / component methods, RunScript,
    CopyArray, CreateString and FormatText stood in for (allocations from one arena reset per run, so pointers match),
    every case above incl. a global symbol in the stand-in table. 39 of 40 deliberate bugs caught (3 after
    strengthening: MakeNewFirstDifferFromOldLast on 2-element arrays and with distinct others, the resolved
    global); 1 equivalent (PermuteArray's two Rnd calls swapped: the swap is symmetric).
  - Build note: the verify build's compile PDB (CMakeFiles\thug2_code.dir\vc140.pdb) got corrupted (C2471) after
    ~12 mutation builds; out\scripts\mutbuild.cmd retries after deleting it.
- [x] Batch EC3-3 (NOT run in the game yet; the music and stream commands - 25 CFunctions): src/game/pcm/music_cfuncs.h
  / .cpp (new).
  - 0x0044D100 StreamExists, 0x0044D140 AddStream (add=), 0x0044D1B0 RemoveStream (remove=), 0x0044D220 StopStream
    (one, or every stream with -1), 0x0044D260 IsStreamPlaying, 0x0044D2A0 PlayTrack (a file, or playlist 0's track
    n as MUSIC\VAG\SONGS\<name> in a 40-byte buffer; loop=; the music stopped first, the current track set to n -
    999 for a file), 0x0044D380 PlayMusicStream (vol= default -1), 0x0044D3E0 LoadStreamFrameAmp, 0x0044D420
    FreeStreamFrameAmp, 0x0044D460 SetMusicVolume / 0x0044D490 SetMusicStreamVolume (default 100), 0x0044D4C0
    StopMusic, 0x0044D4D0 PauseMusic / 0x0044D500 PauseStream (default 1), 0x0044D530 AddMusicTrack (TrackTitle=;
    the flag 0x389129E4 - name unknown - picks playlist 0 instead of 1), 0x0044D5B0 ChangeTrackState (by the name
    "Off"), 0x0044D5F0 TrackEnabled (playlist 0's off-masks; the bit made as the C runtime's _allshl does - from the
    count's low byte, 0 from 64 on, so negative tracks read as enabled), 0x0044D690 ClearMusicTrackList,
    0x0044D6C0 SkipMusicTrack, 0x0044D6D0 SetMusicMode (n == 0 -> playlist 1), 0x0044D710 SetRandomMode,
    0x0044D740 SetMusicLooping, 0x0044D770 PreLoadStreamDone, 0x0044D790 UseUserSoundtrack, 0x0044D7C0
    UseStandardSoundtrack. (0x0044D680 MusicIsPaused was already source-only in music.cpp.)
  - Test TestMusicCFuncs: every Pcm function stood in for (recording its arguments; strings by text), each command
    called with four different Pcm results, with and without its parameters (not the asserting getters' missing
    cases: the original passes uninitialised values there), TrackEnabled over the masks' edges and negative /
    large tracks. 35 of 35 deliberate bugs caught (2 after strengthening: track 64, the shift count's low byte).
