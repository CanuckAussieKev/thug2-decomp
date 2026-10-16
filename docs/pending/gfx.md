# PartyMod round, gfx agent (part 2): graphics functions (NOT run in the game yet; branch pm/gfx)

- [x] Batch (PartyMod round, gfx part 2; NOT run in the game yet): new src/game/gfx/render.* (namespace Nx), tests in
      new src/hook/selftests_gfx2.cpp (Early).
      - 0x004DB200 Nx::SetCamera (set_camera; cdecl matrix, position, horizontal angle in degrees, aspect, zoom, bool
        infinite): copies the camera position / orientation rows 2 (backward) and 1 (up) to 0x786CC4/0x786CDC/0x786CE8,
        world = identity (0x786B90), D3DX look-at (0x5C0985) into 0x786BD0, near 2, far **96000 (PartyMod's float at
        0x4DB35D; original 32000)** or the distance-clipping setting (0x7D6444, unsigned) x 18 when 0x7D643A,
        perspective (0x5C0ACD) w = 4 tan(angle/2), h = w / (aspect / 1.1), zoom != 1 (NaN too) scales x/y (D3DX
        scaling + multiply), infinite far plane writes _33 = -0.999999 / _43 = 1e-6, SetTransform world/view/projection,
        the view's half-angle tangents/sines/cosines (0x786E18-0x786E2C), the heading rotation (atan2 - pi/2, D3DX
        RotationY into 0x786C50), then the frustum update 0x4CDAA0 (thiscall on 0x775B80). The original's FPTAN / FPATAN
        are inline asm so the roundings match: the first tangent takes the half-angle unrounded (double), the second its
        float - different results for e.g. 30.0071 degrees. The original reuses its argument slots as scratch.
      - 0x004DF390 Nx::DrawScreenBlur (the motion blur pass; void): if 0x7CC244, fog off, render states, linear min/mag
        filters on 4 samplers (saved / restored), 0x4DEB70 if 0x705720, the quad (screen size 0x786A7C/80, u/v inset by
        (1 - amount/255)/2 over the texture's size - u rounded from the unrounded quotient, v from the float), colour
        0x80808080 or amount<<24|0x808080, blend 5 if the blur level 0x78732C > 2; draws the other texture into the
        current one (level > 2), then the current one onto the back buffer (cleared to the fog colour 0x78733C with fog
        and distance clipping, else 0x786CC0); swaps 0x786D68, fog back on. **PartyMod's JMP at 0x4DF63A**: the
        letterbox adjustment of the quad never runs (the original's is in a comment - it adds a pixel count to v).
      - Both REPLACE (hookable); both PartyMod patch addresses are in check_partymod_overlap.py's HANDLED list. PartyMod's
        runtime patches stay in src/partymod (they only touch the originals' bodies, which our hooks bypass; the tests
        write the same bytes into the originals).
      - Tests: TestSetCamera - 5 orientations (all four headings, tilted) x 6 angles x 3 aspects x zoom 1 / 1.37 / NaN x
        infinite or not (bool with garbage upper bytes to the original) x distance clipping off / on / past 2^31; the
        original with PartyMod's 96000 written in; stand-in device (SetTransform recorded) and frustum update; every
        camera global compared. TestDrawScreenBlur - off / on x both textures current x 5 amounts (incl. past 2^31) x
        blur levels 1-4 x letterbox / fog / clipping / extra pass flags, screen and texture sizes, samplers linear or
        not; original with PartyMod's JMP; stand-in device, textures, surfaces and renderer state calls.
        41 deliberate bugs: 38 caught (1 after adding angles where the first tangent's rounding matters), 3 equivalent
        (texture size as signed - sizes < 2^31; u's rounded via float or double - float subtraction/division double-
        rounded to float is exact).
