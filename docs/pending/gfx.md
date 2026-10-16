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
      - 0x004B2D80 Nx::RenderWorld (the frame's 3D; void, no arguments): frame counter 0x786AC4; nothing but the
        full-screen viewport while the device is lost (0x786D04). Otherwise the time 0x5BE970 -> 0x786D00, render
        states, 0x4DBBD0, 0x4DEA80, then per viewport (count 0x701378; 0x4A06D0; skipped without a camera at +0x18):
        0x6FA7B8 = index, the device viewport from its fractions (x/width from the int width, y/height from the height
        as a float; truncated), **PartyMod's setDisplayRegion when letterboxed (its calls at 0x4B2E65; the original
        moved y down H/8 and took H/4 off the height - in a comment)**, set_camera (angle 0x485B10, position 0x485A00,
        matrix 0x485A10, aspect 0x4A0D20 - in that call order), the world scenes (list 0x6FA7EC, ends at a node whose
        scene is -1; the next node taken before each; non-sky: 0x4D8ED0, meshes prepared once via flag 0x100 /
        0x4D6530, pass 0x55 via 0x4DC700), 0x490DB0, 0x4C7520, 0x48E370, pass 1, the sky (set_camera from the origin,
        infinite; fog range 20-21 set on the device and put back; fog off; sky scenes drawn at pass 3 with linear
        min / no mip filter unless distance clipping and fog are both on), 0x443870 before or after depending on
        0x786AB5, set_camera again, pass 0x82, non-sky pass 0x46, pass 0x102, then the later passes (0x4A0F30,
        0x49AB50(index > 0), 0x496640, 0x4A10A0 virtual, 0x4920E0, 0x49C840, 0x4DCC70(0), 0x491910(index, 0)).
        After the viewports: full-screen viewport (**PartyMod's setDisplayRegion when letterboxed, 0x4B33CA**),
        0x4DE980, the blur pass (ours), fog off, **the 2D through PartyMod's draw2DWrapper (0x4B341D; the original
        called 0x4DFB90 - PartyMod adds black bars)**, fog back, full-screen viewport again. The original passes the
        saved fog flag as a dword with garbage upper bytes (0x490DE0 reads only AL).
      - All three PartyMod patch addresses are in the HANDLED list.
      - Test TestRenderWorld (Early): 0-3 viewports (one without a camera) x 64 flag combinations (letterbox, fog on
        as 1 / 0x100 / off, distance clipping, fog setting, 0x786AB5, blur on/off) + the device lost, the scene list
        full (world unprepared / sky / world prepared / sky) or empty, three back-buffer shapes (PartyMod's black
        bars); the original with PartyMod's three calls, far plane and blur JMP written in; every callee stood in
        by a recorder (25 game functions, the device's viewport / render state / transform / sampler / draw calls);
        set_camera and the blur pass run for real (ours vs original). 38 deliberate bugs, all caught (3 after fixing
        the test - it dirtied the letterbox flag after setting it, and sampler 0's filters were equal; 3 by the crash
        the bug causes).
      - Test helpers in selftests_gfx2.cpp are copies of selftests_partymod.cpp's CodePatch / JumpTo (a shared header
        would be nicer - left for the merger to avoid conflicts).
