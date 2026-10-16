# PartyMod round, text agent: font text and the Direct3D start-up (NOT run in the game yet; branch pm/text)

- [x] Batch (PartyMod round, menus/text; NOT run in the game yet): new src/game/gfx/text.* and src/game/gfx/device.*
      (namespace Nx), tests in new src/hook/selftests_text.cpp (Early).
      - Layouts: `SFont` (+0 height, +4 base, +8 SChar[], +0xC map[256], +0x10C special map, +0x134/+0x136 int16
        char/space spacing, colours from +0x138 - "\c<i>" reads +0x134 + 4i, past the table for i > 16), `SChar` (0x18:
        u0 v0 u1 v1, uint16 w h baseline), `STextEffect` (10 bytes: int16 x, y, xscale, yscale /16384, rotation
        radians*16384), `TextVertex` (0x1C), `SText` (vtable 0x64A89C: [0] dtor 0x4CFAF0, [1] BeginDraw 0x4CEF00 - buffer
        = 0x779FD8, [2] Draw, [3] EndDraw 0x4CEF10; +0x10 z, +0x14 font, +0x18 effects, +0x1C text, +0x20 x/y, +0x28
        scales, +0x30 pivot, +0x38 angle, +0x3C RGBA colour, +0x40 fixed colour). Vertex buffer 0x780FE8, count
        0x780FE4 (flushed at 0x400 via EndDraw + BeginDraw), button font 0x780FDC, texture font 0x780FE0.
      - Escapes (the jump tables): `\\` (the backslash is a character), `\c<d>` colour (QueryString skips 2), `\s<d>` /
        `\S` special glyph, `\b<d>` / `\m<d>` buttons (`\m` through the meta button map 0x48D6A0, then DehexifyDigit on a
        local), `\u` / `\U` skip 2, **only capital `\L` skips 1 (`\l` is drawn as an l)**, other letters are drawn. An
        escape that places a glyph (or a key name) ends the escape chain; a backslash as its digit is then a character.
      - 0x004CECB0 `SFont::QueryString(text, float* width, float* height)` (thiscall, RET 0xC; reached via 0x4B3D10):
        width = sum of (glyph / space / character width + this font's char spacing); key names add each character's
        width through the map indexed by *signed* char; the digit itself isn't counted; height = the font's (unsigned).
      - 0x004CFD00 `SText::Draw()` (thiscall, no args, also virtual [2]): screen position/pivot through 0x786D80-8C,
        corners relative to the pivot in whole pixels (_ftol) turned by the text's angle (x87 FCOS/FSIN, as inline asm;
        left/top offsets go through floats, right/bottom don't), per-character effects (offset, scale, rotation about
        the centre - quirks kept: the centre is negated and offset by the pivot's **x** on both axes, the corners are
        turned by sin/cos swapped (x' = dx sin - dy cos, y' = dy sin + dx cos), the effect's x offset carries into the
        next character); quads (LT, LB, RB, RT); button glyphs switch to the button font for one character (EndDraw /
        BeginDraw before, texture font 0x780FE0, and after); key names drawn by DrawKeyChar; spaces advance (space +
        char spacing) and skip the rest; xscale or yscale 0 draws nothing.
      - 0x004CFB20 `SText::DrawKeyChar(char, float* x, float y)` (thiscall, RET 0xC; only caller Draw): one plain quad,
        top/bottom kept at double precision until stored, x advanced by width + spacing.
      - 0x004D8650 `InitDirect3D()` (cdecl void; callers 0x4B25F0, 0x4D89C0 read nothing back): resets the screen
        conversion, Direct3DCreate9(31) via 0x5BEB14 (null: MessageBoxA - text "Error", caption the long message, the
        wrong way round - and return), GetAdapterDisplayMode (failure: return), present parameters 0x786A7C (hwnd
        0x7D6A28, discard, 1 back buffer, auto depth D24X8, X8R8G8B8, 2x multisample if 0x7D6434), sizes
        1280x1024 / 1024x768 / 800x600 / 640x480 from index 3 - setting(0x7D643C) (unsigned clamp to 3), 0x4D7C80
        (cdecl w, h, hardware vertex processing, multisample) tried for {hw, ms} {sw, ms} {hw, -} {sw, -} at each size
        down, the game's exit(0) (0x624AFE) if none works, scale = back buffer / 640, / 480 (unsigned), offset y =
        _ftol(scaleY * 16), GetCreationParameters + CheckDeviceFormat(D24S8 texture) -> 0x786AB9, clears
        0x786D1C-3C and 0x786D64, then 0x4D7D70, 0x4D8420, 0x4D7B60, 0x786D90 = 0x5BA1D0() (returns 0), 0x5BE7C0.
      - PartyMod built in (original logic in comments): glyph.c - `dehexifyDigitWrapper` for `\b` (0x4CED4B / 0x4CFF0C)
        and `shouldUseGlyph` decides glyph or key name (0x4CED6D / 0x4CFF36; originally glyph for 4-7 and 10-13);
        window.c - no ShowWindow (0x4D869C), Windowed = its `isWindowed` (0x4D8720), the 640x480 slot = its
        `resX`/`resY` (0x4D8776 / 0x4D8792), no SetWindowPos (0x4D8891), no ShowCursor loop (0x4D8989). All 28 patch
        addresses are in check_partymod_overlap.py's HANDLED list. src/partymod is unchanged: its runtime patches only
        touch the originals' bodies (bypassed by the hooks; the tests write the same bytes into the originals), and the
        variables/functions used are already global.
      - Tests (Early): `TestQueryString` - 17 texts covering every escape x 2 fonts x PartyMod's three input states
        (keyboard, controller, keyboard + PreferGlyphs), key names stood in (0x5BCD50; empty, plain, chars > 0x7F),
        the meta map filled, the original with PartyMod's bytes. `TestDrawText` - the texts plus a 300-character one
        (buffer flushes) x 8 setups (screen scales/offsets, rotated text, pivots - one far away, past 2^24 - effects
        with and without rotations, fixed colour, zero scales) x the three input states, stand-in vtable (BeginDraw /
        EndDraw record the buffer); and DrawKeyChar directly, with the buffer one quad from full or not.
        `TestInitDirect3D` - 36 scripts (no Direct3D, display mode failing, settings -1..7 x multisample x windowed,
        devices failing 0-4 times or always (the game's exit, stood in by a throw - a returning exit would unbalance the
        original's stack: it doesn't pop exit's argument), PartyMod sizes incl. negative (unsigned), another created
        width, the depth check both ways); Direct3D, the import table's MessageBoxA and the callees stood in; the
        original with PartyMod's bytes.
      - 61 deliberate bugs: 59 caught (4 after strengthening: a backslash as the digit, DrawKeyChar's own flush, a far
        pivot for the float-rounded offsets, MessageBox text/caption; one equivalent mutation replaced), 2 not
        observable: rounding the pivot sum (pivot + y) to float before scaling (the later float rounding and _ftol
        swallow it), and the sine rounded to double before the product in the effect rotation.
      - Register scan (only the known 0x4EFF92 false alarm; nothing reads EAX after the four), call audit (no MISMATCH),
        overlap check (no overlaps, all handled): clean. find_gaps: nothing new (the neighbours 0x4CEED0-0x4CFAF0 -
        SText ctor/dtors, BeginDraw/EndDraw, 0x4CF0F0 - are more than 0x800 apart from these and still to do;
        the SetLetterbox thunk 0x48C1A0 is from batch 55).
      - Note for the game: Direct3D sets the x87 to 24-bit precision on its thread (no D3DCREATE_FPU_PRESERVE in
        0x4D7C80), so in the game the originals' x87 code (all of these, like earlier batches') rounds differently from
        our double-precision code; the tests compare at 53 bits.
      - Also: tools/check/main.cpp (from main) catches the game's own /GS report; this agent's earlier runs hit it
        through the returning exit stand-in (fixed by the throw).
