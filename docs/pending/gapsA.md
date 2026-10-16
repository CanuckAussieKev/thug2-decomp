- [x] Batch gapsA (gaps next to batches 55/63-66; NOT run in the game yet; branch wt/gapsA): 33 addresses filled in -
      28 REPLACE, 5 REPLACE_SOURCE_ONLY (jumps). Tests in new src/hook/selftests_gapsA.cpp (Early, StandIns harness).
      - Text (src/game/gfx/text.*): `SFont` layout finished - specialMap is 0x20 bytes ([0x1F] doubles as the default
        glyph), +0x12C `format`, +0x130 `next` (font list 0x780FD8), +0x178 `texture` (IDirect3DTexture9), +0x17C
        (passed with it to 0x4DA730), size 0x180. SText: 0x4CEED0 `Construct(float priority)` (base drawable 0x4DFA70
        with "not listed" = 1, vtable 0x64A89C, returns this), 0x4CEEF0 `DestroyBody` (vtable, then the base
        destructor 0x4DFAA0), 0x4CFAF0 `Destroy(flags)` (deleting destructor), 0x4CEF00 `ResetVertices` (BeginDraw:
        buffer pointer 0x780FE8 = 0x779FD8), 0x4CEF10 `DrawVertices` (EndDraw: makes the 256-quad index buffer
        0x780FF0 once (flag 0x781BF0), then if count > 0 (signed): the texture font's (0x780FE0) or own font's
        texture, 7 texture stage states, FVF 0x144, cull none, unless 0x7D1EC0: counters 0x786EBC / 0x786EC0 and
        DrawIndexedPrimitiveUP; stage states back; count and texture font cleared - count <= 0 leaves both).
        0x4CF0F0 `SFont::Load(name, fromMemory)` (cdecl; "fonts/<name>.fnt.xbx" or the bytes at name): header, the
        character map (format 1: one character or special -1-index per glyph, specials past the map written past it;
        format 2: up to four characters, 0xFF ends), the default glyph given to unmapped characters (not 'A'/'a' -
        glyph 0 counts as unmapped) and specials, CreateTexture A8R8G8B8 managed (failure: return null, file left
        open), palette RGBA -> ARGB with PS2 alpha doubled (>= 0x80 opaque), 0x5F45E0 expands the indices, LockRect
        (failure: null), pixels copied packed (pitch ignored), glyph UVs (right/bottom from the unrounded left/top),
        listed, closed, spaceSpacing = width of 'I'. 0x4CFA80 `SFont::Unload` (off the list, frees glyphs and font -
        not the texture). selftests_text.cpp fills 0x20 special-map entries now (was 0x28: past the array).
      - Device (src/game/gfx/device.*): 0x44F870 `Nx::SetRenderState(state, value)`, 0x44F890 `Nx::ClearDevice(...)`
        - cdecl wrappers returning the HRESULT (32 and 4 callers; movie.cpp keeps its own inline calls).
      - Music (src/game/pcm/music.*): `Stream` (0x74 bytes, three at 0x69B938: +0 name, +4 id, +0x10 channel, +0x30
        listener). 0x450220 StreamIdFinished, 0x450260 FindPlayingStream (by id, then by name, -1), 0x4502D0
        FreeStreamId, 0x450310 ShuffleUserSoundtrack (game's Rnd; at most 600; no PartyMod change here), 0x4503D0
        UseUserSoundtrack, 0x450410 UseStandardSoundtrack, 0x450420 StopUserSoundtrack (source only: jump to
        0x4531A0), 0x450430 MusicIsPaused (CFunction MusicIsPaused jumps to it via 0x44D680 - not done), 0x450440 /
        0x450450 music / sfx volume (FLD), 0x450460 RandomTrackOrder (int, MOVZX), 0x450470 SetMusicLooping (so
        0x69BAA0 is the loop flag, renamed from "special track set"), 0x450480 / 0x4504B0 Get/SetTracksOff
        (playlist 0's off masks), 0x4504E0 SetUserSoundtrackVolume (float; 0x453400 only for playlist 1), 0x452680
        PlayNextTrackInOrder (first track on and not played; all played: masks cleared, nothing plays), 0x452910
        Update (stream system 0x453940, 0x453BD0, pause on failure, owners/listeners of ended streams, every 64
        frames unless volume 0 (playlist != 1) or paused: in order or PlayNextTrack), 0x4B2350 GetNumSoundtracks
        (source only, jump to 0x4542D0), 0x4B2360 GetSoundtrackName (wide name through user32 wsprintfA "%ls" per
        character, 99 max, unconvertible / 0xA1-0xAC bytes -> '~', "~" if none), and the thunks 0x48C150 / 0x48C160
        to them (source only).
      - 0x48C1A0 -> SetLetterbox: REPLACE_SOURCE_ONLY in src/game/gfx/screen.cpp.
      - Tests (Early): TestSTextDrawing, TestLoadFont (7 font files: both formats, defaults, past-the-map special,
        another format, texture create / lock failures, memory and file; Unload at head / middle / end / missing),
        TestDeviceWrappers, TestMusicStreams (lookups, getters/setters, shuffles incl. the last-first swap, names),
        TestPlayNextTrackInOrder, TestMusicUpdate (20 cases). File, memory manager, device / texture vtables,
        platform music functions stood in for.
      - 81 deliberate bugs: 76 caught, 3 more after strengthening (format-1 code 0, the in-order round restart with
        tracks over 63 played, frame 32), 2 equivalent: the default fill's `c < 0x1F` vs `<= 0x1F` (entry 0x1F is the
        default itself, never 0 there) and '~' for an empty conversion (wsprintf "%ls" never gives one).
      - Register scan (nothing new beyond the known 0x4EFF92; EAX readers of these all return values), call audit (no
        MISMATCH), overlap check (no overlaps): clean.
      - Not done: CFunction thunk 0x44D680 (MusicIsPaused -> 0x450430); gapsB left 0x4B2270-0x4B2320 next to
        0x4B2350 for this branch - not taken (see report).
