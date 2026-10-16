# Handoff: PartyMod's patches into the source (2026-10-16)

State: everything through batch 60 (batch 60 not run in the game yet) is merged on main, pushed, and run in the game (all self-tests pass).
No work in flight - no worktrees or branches besides main.

Read CLAUDE.md and `.claude/skills/decomp-batch/` first; the history is in `docs/decomp-progress.md`.

Goal: give source to the 48 game functions PartyMod patches (113 patches, ~35 KB), with PartyMod's changes
built in (and the original's logic kept visible in a comment), so its run-time patching can go.
`tools/ghidra/partymod_patches.py <snapshot>` lists them; `python -I tools/check_partymod_overlap.py` fails
on any PartyMod patch inside a rewritten function its HANDLED list doesn't account for.

Done (21 of 48): batch 60: 0x44FA80; batch 59: 0x4B9030; batch 58: 0x54DCB0; batch 57 (skater): 0x51F4C0, 0x527510, 0x527600; LoadQB; Sio::Device::Process, ActivateActuator; batch 54 (platform): SetActuators,
ProcessWindowEvents, InitInput 0x5F44F0, GetMemCardSpaceAvailable; batch 55 (gfx): SetScreenAspectRatio,
GetScreenAngleFactor, SetScreenBlur, SetLetterbox, CFunc_SetScreenBlur; batch 56 (glyph): LoadFont 0x48D7E0,
CFunc_LoadFont, UpdateLoadingBarColors 0x4B6B80. 

Still to do (batches 54-56's records have notes - disassembly read, conventions, test plans - for most):
- graphics: 0x4B2D80, 0x4DB200 (far plane 32000 -> 96000; x87 FPTAN/FPATAN - needs inline asm to match), 0x4DF390
- menus/text: 0x4CFD00 (SText::Draw), 0x4CECB0 (SFont::QueryString), 0x4D8650 (D3D init)
- skater controls (PS2-style triggers, spin delay; depend on PartyMod settings): 0x50CA00, 0x509BD0,
  0x4FCBA0, 0x504060, 0x507120, 0x5334D0. Notes on 0x4FCBA0 (lip trick / jump direction, thiscall, 2 args,
  RET 8, returns int 0/1): reads the skater's velocity from [this+0xC] +0x8C/+0x94/+0x98, normalises the
  horizontal part (FSQRT, compared with the float at 0x66D520), then checks the input at [this+0x150C]+0x20
  (+0x60 stops it; +0xA0 and +0x100 held - PartyMod's lip patch is the JZ at 0x4FCC59 -> JNZ 0x0A under PS2
  controls; then +0x40 with a timer 0x46B920 vs a script value 0x580440, then +0x00, then +0x20) and adds
  physics values (0x580400: script floats 0x1F96C224 / 0xDC818B7E / 0x1AB3809F) to the vector at
  [this+0xC]+0xAC. Dense x87 code with stack-slot reuse - one branch multiplies by a slot left from an
  earlier call's arguments: map every stack slot before writing it.
- platform/misc: 0x44FDC0 (movie skip), 0x452200 (playlist shuffle - Ghidra shows it inside 0x452910),
  0x4E1EF0 (fast exit), 0x5A8730, 0x545880
- WinMain 0x4E2680 and MainLoop 0x44EEF0: both host the test runner's hooks (src/hook/selftest_runner.cpp;
  ParseNetworkParams 0x4E1220 too) - plan that first.
- The 8 GameSpy functions (0x5F8690 and up) are library code: PartyMod's patches there (OpenSpy host names)
  become configuration of the linked SDK.
- Sio::SetActuators (device.cpp) still calls 0x5F3A50; pointing it at Platform::SetActuators needs TestSio's
  original run to have PartyMod's jump written at 0x5F3A50 first.

Parallel agents worked well: one git worktree + branch each (`git worktree add C:\Dev\thug2-wt\<name> -b pm/<name>`),
their own copy of the Ghidra snapshot (Ghidra locks a project) and of data/derived/ (untracked), tests in
src/hook/selftests_<name>.cpp, records in docs/pending/<name>.md; merges only conflict in CMakeLists.txt and
the HANDLED list (keep both sides). They cost a lot of usage - scope each to a few functions.

Then: the decompilation's own "- [ ] Next:" line in docs/decomp-progress.md. Delete this file when done.
