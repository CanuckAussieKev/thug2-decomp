# pm/movie: movie playback, the playlist shuffle, Sio's motors (PartyMod round)

- [x] Batch (PartyMod round; NOT run in the game yet): movie playback 0x44FDC0 with its helpers, the playlist's
      next track 0x452200, and Sio::SetActuators pointed at Platform::SetActuators.
  - Nx::PlayMovieFile 0x44FDC0 (cdecl name; also reached by the jump 0x44F830: REPLACE_SOURCE_ONLY). Bink's sound
    system (BinkSetSoundSystem(BinkOpenDirectSound, the DirectSound object 0x7D6A38); "Bink can't use DSound"
    if it fails), the sound manager singleton (0x47BA60(false) / released 0x47C6C0 on every exit), Bink volume =
    max(music 0x450440, effects 0x47AFD0) * 0.01f * 16384.0f truncated (x87 53-bit); path
    "data\movies\bik\" + the name after its last backslash (the search never looks at name[0]: "\lead" keeps
    its backslash; '/' isn't a separator) + ".bik"; music paused 0x450650, streams stopped 0x450690(-1);
    BinkOpen (null: release, return); BeginMovie; the screen saver timeout read (SPI_GETSCREENSAVETIMEOUT) and,
    if nonzero, set again every 201st loop pass (resets its timer); fog off (0x490DE0) and back after. Loop
    (only with a texture; Sio manager referenced): app inactive -> BinkPause(1), Sleep(100) + window pump until
    active, loop (not unpaused there); quit request -> end; device lost -> reset 0x44F250, still lost ->
    loop, else BinkPause(0); BinkWait -> loop; BinkDoFrame, LockRect -> BinkCopyToBuffer(bits, pitch,
    height, 0, 0, 0x67D7C4 | 0x80000000) + UnlockRect; DrawMovie; end at frameNum >= frames - 1 (unsigned: a
    0-frame movie never ends by itself); window pump every 2nd frame; then the pads: ports 0-3, start (byte 2
    bit 3) or X (byte 3 bit 6) pressed after being seen up (control data bits are set while up) ends it.
    PartyMod NOPs the JNZ at 0x450121 that read the pads only every 5th frame - ours reads every frame (the
    original's line in a comment). Cleanup: the playing flag 0x696D95 off, fog back, EndMovie, BinkClose,
    0x696D90 = null, stage 0 alpha op MODULATE, sound manager released. Bink is called through the game's
    import table (0x6452E8-0x645320) until it's linked; SystemParametersInfoA / Sleep directly.
  - Nx::BeginMovie 0x44F8C0 (saves cull mode, multisample AA and sampler 0 min filter in 0x696D88/84/80; cull
    none, AA off, lighting off, 0x4DAB60(1/2, 0), min filter linear; the movie size (unsigned -> float) in
    0x67D7D0/D4 and the back buffer's in 0x67D7C8/CC; CreateTexture(w, h, 1, 0, X8R8G8B8, MANAGED): the texture
    + playing flag on, or null), Nx::EndMovie 0x44F9F0 (states back, 0x4DAB60(1/2, 1), Release, 0x4DA730(0,0,0)),
    Nx::DrawMovie 0x44FCC0 (BeginScene, Clear target black, DrawMovieFrame(0, 0, 0x67D7D8, 0x67D7DC, 1),
    EndScene, Present, BinkNextFrame), Nx::ButtonPressedAgain 0x44F840 (EAX data, ECX mask, ESI seen-up word,
    returns AL: naked stub, REPLACE_UNCHECKED).
  - Pcm::PlayNextTrack 0x452200 (new src/game/pcm/music.*). Ghidra has no function there: it's the target of
    0x452910's tail jump (JMP at 0x452A83), 0x452200-0x452676 (RET, then int3 padding); no other references
    (scanned the exe for calls/jumps/pointers). check_partymod_overlap.py now has an extra-bodies table for it.
    Logic: music on (DirectSound object, platform != 2, 0x67D7E0) and the playlist's byte +0xCA8 clear, no
    stream playing (0x453250 == 1); 0x69BAAC = 0; 0x451400; playlist 1: the script's integer 0xF18AB1F9 picks
    the track if < count (signed: negative indexes before the array); the special track 0x69806C (flag
    0x69BAA0) with 1; empty playlist: nothing. Reshuffle (flag 0x67D7F4): last = order[count-1] (with an empty
    order that's 0x696D9C, the movie dump counter), the on tracks (128-bit off masks at +0xC88/+0xC90, tested
    with __allshl semantics: shifts by the low byte, 0 from 64) into the order 0x696DA0 (count 0x67D7F8),
    Printf "shuffling random song order num_songs=%i", 2000 swaps of two random places, if order[0] is the last
    track: swap it with place Rnd(n-1)+1 (or 0 when n <= 1 - that draw is the game's even under PartyMod),
    position 0x69BAC4 = 0, played masks (+0xC98/+0xCA0) cleared. Else position+1, reshuffle next time at
    position >= n-1. Then the track at the position plays unless off or already played (the original repeats
    that same test `count` times - written once): played bit set, 0x4520D0(track, 0), current track 0x69BA98,
    script 0x300CAD34 run with {0x8D02705D: track} (a new CStruct, cleared and deleted).
    PartyMod: the two swap draws (calls at 0x4523A7/0x4523B4) go to its our_random: draws the game's Rnd(n)
    (thrown away) then returns rand() % n (srand(time) at start-up) - Pcm::ShuffleRandom. our_random is
    declared void but returns the value; the test (original with PartyMod's calls written in vs ours) shows
    its EAX is rand() % n. ShuffleRandom returns the game's 0 for n == 0 instead of dividing by zero
    (PartyMod would crash on a playlist with tracks but all of them off; the original wouldn't).
  - Sio::SetActuators (device.cpp) now calls Platform::SetActuators (PartyMod's set_actuators) instead of
    0x5F3A50. TestSio and TestSioManager write PartyMod's jump at 0x5F3A50 -> set_actuators (as in the game)
    and jump from set_actuators to a recorder, so both runs trace the motor levels there (the DirectInput
    effect stand-ins are no longer reached in TestSio). TestDetour moved up in selftest.cpp for that.
  - PartyMod's runtime patches (patchMovieSkip, patchPlaylistShuffle) are left in src/partymod, as batch 60
    did: they only touch the originals' bodies, which our replacements bypass, and keep PartyMod's behaviour
    if a replacement is disabled in thug2-decomp.ini.
  - Tests (selftests_partymod.cpp): TestPlayMovieFile - 8 runs (sound set-up fails + no movie; the texture
    can't be made; plays out with waits, failed locks and port 0's buttons held from the start (no skip);
    start pressed on port 2; X on port 3 with the app inactive and the device lost for two resets; 450 frames
    with the screen saver reset twice and quit requested; 0 frames (quit); 1 frame) - the original with
    PartyMod's NOP vs ours, Bink through stand-ins in its import slots, SystemParametersInfoA stood in at its
    import slot and in user32 itself, the D3D device/texture, the renderer, sound, Sio, window pump,
    DrawMovieFrame and Dbg::Printf stood in (game addresses and our functions). TestButtonPressedAgain - the
    register stub vs the original over masks/bytes/seen bits/null data. TestPlayNextTrack - music off three
    ways, +0xCA8, stream states 2/0, playlist 1 (in range, past, negative), the special track, empty, 45 tracks
    of a 20-track playlist (3 + 1 off, one played early, one switched off mid-round), 210 of a 100-track
    playlist (off in both halves, track 63 too), one track on (n = 1), a negative count - the original with
    PartyMod's two calls written in, rand reseeded and the game's random streams reset for both runs.
    TestPlayMovie now also stands in for ours' direct call to PlayMovieFile.
    74 deliberate bugs: all caught, 2 after strengthening (the app-inactive path wasn't reached - the
    every-other-frame pump re-activated the app first; track 63 wasn't off anywhere). stub_order is caught by a
    crash (swapped stub arguments). Two early runs were reported caught only by "PC async handle" (flaky,
    probably parallel worktrees' temp files): rerun, caught by the movie test.
  - Checks: build clean, thug2_check 0 MISMATCHED / 72 early tests pass (after merging main), register scan (only the known
    0x4EFF92), call audit (run against this worktree - the script hard-codes C:\Dev\thug2-decomp\src): no
    MISMATCH (the two "?" are tail jumps: 0x490DE0 -> 0x4B8E40, 0x44F250 -> 0x44EE90, both plain RET),
    check_partymod_overlap: no overlaps, 3 new HANDLED entries (0x450121, 0x4523A7, 0x4523B4).
  - Not done, next to it: the SingletonPtr destructors 0x44FD40/0x44FD50 (MOV [ECX],vtable; JMP release) and
    their scalar deleting destructors 0x44FD60/0x44FD90 (vtables 0x6472B4/0x6472B8, shared with other
    functions), 0x44F870/0x44F890, and the Pcm API around 0x450220-0x4504B0 and 0x452680/0x452910 (the
    caller). find_gaps' one thunk (0x48C1A0 -> SetLetterbox) predates this batch.

Shared files touched: CMakeLists.txt (src/game/pcm/music.cpp after movie.cpp), tools/check_partymod_overlap.py
(HANDLED + the extra-bodies table), src/hook/selftest.cpp (TestSio/TestSioManager, TestDetour moved up),
src/hook/selftests_partymod.cpp, src/game/sio/device.cpp/.h. No src/partymod changes.
