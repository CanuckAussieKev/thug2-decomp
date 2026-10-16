- [x] PartyMod round, glyph/fonts/window (NOT run in the game yet; branch pm/glyph): the script fonts and the
  loading bar's colours, with PartyMod's patches built in. src/game/gfx/font.cpp, src/game/gfx/loading_bar.cpp,
  tests in src/hook/selftests_glyph.cpp.
  - `Nx::LoadFont` (0x48D7E0, cdecl `(name, charSpacing, spaceSpacing, uint32_t* colors, bool buttonFont)`):
    a loaded font (hash table 0x6FA9F0 of 16 `FontSlot`s {char name[24]; CFont*} at 0x6FA830, looked up via
    0x48D760) is just updated - looked up again before each of its three setters (0x4E3F90 SetSpacings,
    0x4A10A0 SetColorTable if colours, 0x487D50 MarkAsButtonFont; each a jump through the font's vtable) - and
    returns. Otherwise the first free slot gets `Nx::LoadFontFile` (0x4B40A0), the setters, the name
    (unbounded copy) and a hash entry; with every slot taken nothing is loaded. Either way the meta button map
    (0x6FA808, 32 chars, flag 0x6FA828) is then built once from a script array: 0-9 as digits, larger values as letters from a, 0 past the
    array's end.
  - `Nx::CFunc_LoadFont` (0x48D950): `LoadFont <name> [buttons_font] [char_spacing] [space_spacing]
    [color_tab]`; spacings are floats truncated to int; colours are up to 16 arrays of bytes packed lowest
    first (a 5th byte wraps onto the 1st, as SHL's count does). Returns true. The original leaves colour
    entries the script doesn't give (and the name, if missing) as stack garbage; ours zeroes them / uses null.
  - `Nx::UpdateLoadingBarColors` (0x4B6B80; called by the SetLoadingBar* CFuncs): start/end/border colours
    from 0x67DDCC/D0/D4, width = screen width (0x786D6C) * bar width (0x67DDBC) / 640, border ARGB with alpha
    0x80, and a per-pixel gradient table of {r, g, b} ints. Nothing in the exe reads that table.
  - PartyMod built in:
    - glyph.c:74/77 (DWords at 0x48D983 and 0x48D8F8): the button font name ("ButtonsXbox") and the meta
      button map ("meta_button_map_xbox") become PartyMod's PS2 / GameCube ones by its `GlyphStyle` setting.
      `Nx::ButtonFontName()` / `MetaButtonMapName()` pick them from PartyMod's own variables; glyph.c now keeps
      the style in a global `glyphStyle` (was a local of patchButtonGlyphs; noted in src/partymod/UPSTREAM.md).
    - window.c:150 (DWord at 0x4B6C64): the gradient table goes in PartyMod's `resbuffer[100000]` instead of
      0x701688 (which overflows at high resolutions). PartyMod replaced the instruction's base, which is the
      table + 4, so the patched original writes from 4 bytes *before* its buffer; ours writes from the buffer's
      start (same contents, no out-of-bounds write).
    - The three addresses are in HANDLED in tools/check_partymod_overlap.py.
  - Tests (Early): `TestLoadingBar` - 7 cases (rising, falling, ~3000-entry high resolution, short, zero and
    negative width, flat, rounding of both float conversions) against the original with PartyMod's DWord
    written in; compares the globals, the whole table (original's at resbuffer-4, ours at resbuffer) and that
    ours leaves the 4 bytes before the buffer alone. `TestLoadFont` - for each glyph style 0/1/2 (PartyMod's
    two DWords written into the original): CFunc_LoadFont with a hand-built parameter structure (new font with
    3/1/0/5-byte colours and truncated spacings; the same font again; the button font flag; 20 colours) and
    LoadFont directly (no colour table, updates, filling all 16 slots and beyond, map missing then present,
    slots already full, map not reloaded once loaded); stand-in fonts (vtable recording every setter call),
    a stand-in hash table and symbol table; compares slots, hash items, meta map and call log. Not covered:
    a colour-table entry the script leaves out (garbage in the original), hash-bucket chaining (needs the
    pool manager), the real font loader (stood in).
  - 35 deliberate bugs: 34 caught (1 after strengthening: reloading an already loaded meta map), 1 equivalent
    (`(8*j) & 63` vs `& 31` as a shift count - the CPU masks it the same way).
  - Register scan, call audit, overlap check: clean.
  - **Left undone** (the coordinator cut the round short), with what was worked out from the disassembly:
    - 0x4CECB0 `SFont::QueryString(text, float* width, float* height)` (thiscall, RET 0xC; reached via
      0x4B3D10 `mov ecx,[ecx+8]; jmp`). Font layout: +0 height (unsigned -> float), +4 base, +8 SChar[] (0x18
      bytes: u0 v0 u1 v1 floats, w h baseline uint16), +0xC map[256], +0x10C special map[32], +0x134/+0x136
      int16 char/space spacing, colour table from +0x138 (indexed [idx*4+0x134], idx >= 1). Escapes: `\\`
      (counted as a char), `\s<d>`/`\S` special glyph, `\b<d>`/`\m<d>` (meta: 0x48D6A0 maps through the meta
      map, then DehexifyDigit on a 1-byte local) buttons, `\c`/`\u` skip 2, `\l` skip 1. PartyMod: the
      non-meta DehexifyDigit call (0x4CED4B) -> `dehexifyDigitWrapper`; the glyph-or-key-name test at
      0x4CED6D (originally glyph for 4-7 and 10-13) -> `shouldUseGlyph(index)`; key-name path sums
      GetButtonKeyName's characters' widths (signed char map index) + spacing and stops counting the rest.
    - 0x4CFD00 `SText::Draw()` (thiscall, no args; 2383 bytes of x87): same escapes plus `\c<d>` colours
      (from the font's table unless +0x40 overrides); per-character effects from +0x18 (10-byte records:
      int16 x, y, xscale, yscale /16384, rotation /16384); rotation by +0x38 about pivot +0x30/+0x34; vertices
      (xyzrhw, ARGB, uv; 0x1C bytes) into 0x780FE8, count 0x780FE4, flushed through vtable +0xC/+4 at 0x400.
      Button glyphs switch to the button font 0x780FDC for one char; key names are drawn char by char via
      0x4CFB20. Original quirks: the per-char rotation centre uses pivot X for both axes. PartyMod patches the
      same two places as QueryString (0x4CFF0C call, 0x4CFF36 test). Needs a bit-exact float test.
    - 0x4D8650 (D3D init, called from 0x4B25F0 and 0x4D89C0): Direct3DCreate9, GetAdapterDisplayMode,
      present parameters at 0x786A7C, resolution table 1280/1024/800/640 x 1024/768/600/480 tried from
      index 3 - setting 0x7D643C, device creation via 0x4D7C80 over 4 flag pairs, exit(0) on failure,
      conversion factors 0x786D80/84 (= w/640, h/480), CheckDeviceFormat, ShowCursor loop, Tmr::SetFlags.
      PartyMod: no ShowWindow (0x4D869C), no SetWindowPos (NOP at 0x4D8891), no ShowCursor loop (0x4D8989),
      Windowed = 1 when its `isWindowed` (0x4D8720), table slot 3 = its `resX`/`resY` (0x4D8776/0x4D8792).
      Testable offline with a stand-in IDirect3D9 (detour the 0x5BEB14 thunk) and IAT stand-ins.
