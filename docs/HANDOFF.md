# Handoff: PartyMod's patches into the source (2026-10-16)

State: everything through batch 67 is merged on main (not pushed). Batches 60-67 are NOT run in the game yet;
offline, thug2_check passes (704 replacements, 0 MISMATCHED, 81 early self-tests) and check_partymod_overlap
reports no overlaps. No work in flight - no worktrees or branches besides main.

Read CLAUDE.md and `.claude/skills/decomp-batch/` first; the history is in `docs/decomp-progress.md`.

Goal: give source to the 48 game functions PartyMod patches, with PartyMod's changes built in (and the
original's logic kept visible in a comment), so its run-time patching can go.
`tools/ghidra/partymod_patches.py <snapshot>` lists them; `python -I tools/check_partymod_overlap.py` fails
on any PartyMod patch inside a rewritten function its HANDLED list doesn't account for.

Done (38 of 48): batches 54-60 (21, see the progress doc) and, from six parallel agents on 2026-10-16:
batch 62 Spin 0x504060, BreakVert 0x507120, GameNet::KeyboardTask 0x5334D0; batch 63 RenderWorld 0x4B2D80,
SetCamera 0x4DB200, DrawScreenBlur 0x4DF390; batch 64 PlayMovieFile 0x44FDC0 (+4 helpers), Pcm::PlayNextTrack
0x452200, Sio::SetActuators -> Platform::SetActuators; batch 65 Sys::Main 0x4E1EF0, Net::ServiceTask 0x545880,
Mc::LoadFromMemoryCard 0x5A8730; batch 66 SFont::QueryString 0x4CECB0, SText::Draw 0x4CFD00 (+DrawKeyChar
0x4CFB20), InitDirect3D 0x4D8650; batch 67 LipPushOff 0x4FCBA0, CheckForGround 0x509BD0, DoAirPhysics 0x50CA00
(Skater::CorePhysicsComponent is in src/game/skater/core_physics.h).

Still to do:
- Run batches 60-67 in the game (the user) - bisect with thug2-decomp.ini if anything breaks.
- WinMain 0x4E2680 and MainLoop 0x44EEF0: both host the test runner's hooks (src/hook/selftest_runner.cpp;
  ParseNetworkParams 0x4E1220 too) - plan that first. Sys::Main (batch 65) calls MainLoop through the game's
  address, so the runner's MainLoop hook still fires.
- The 8 GameSpy functions (0x5F8690 and up) are library code: PartyMod's patches there (OpenSpy host names)
  become configuration of the linked SDK.
- src/partymod still applies all its runtime byte patches (they only touch originals our hooks bypass, and keep
  PartyMod's behaviour if a replacement is disabled in the ini). Remove them once the game is built from source.

Neighbours the agents found but left (find_gaps): SText ctor/dtors 0x4CEED0/0x4CEEF0/0x4CFAF0, BeginDraw
0x4CEF00, EndDraw 0x4CEF10, 0x4CF0F0, 0x4CFA80; ~50 SingletonPtr holder methods at 0x4E1970-0x4E1EE0 (vtables
0x64AFB0-0x64B000) and 0x44FD40-0x44FD90; 0x44F870, 0x44F890; Pcm 0x450220-0x4504B0, 0x452680, 0x452910; the
thunk 0x48C1A0 -> SetLetterbox and 0x4B2350/0x4B2360.

Things learned this round:
- Direct3D drops the x87 to 24-bit precision when it creates the device (no D3DCREATE_FPU_PRESERVE). In the game
  the originals' float code rounds differently from our SSE/double code; the offline tests compare at 53 bits.
- TestPcAsyncFileHandle ("PC async handle ...") failed once in two agents' runs while several thug2_check runs
  read the same game file at once, and passed on rerun - looks timing-sensitive under load.
- thug2_check never shows a dialog now: CRT reports, run-time check failures and the game's own /GS handler
  (0x6287DD) print and exit with code 3. A test that stands in for a function that never returns (exit) must not
  return from the stand-in - that's what tripped the game's /GS check.
- tools/mutate.py stops before its final rebuild if a spec line's text isn't unique - rebuild before trusting
  the last check.
- Reusable test harnesses: `StandIns` (selftests_misc.cpp) and the recording harness in selftests_skaterA.cpp
  replace every callee with a recorder; selftests_gfx2.cpp has its own copy of CodePatch/JumpTo (a shared header
  would be cleaner).

Parallel agents: one git worktree + branch each (`git worktree add C:\Dev\thug2-wt\<name> -b pm/<name>`), their
own copy of the Ghidra snapshot and data/derived/, tests in src/hook/selftests_<name>.cpp, records in
docs/pending/<name>.md; merges only conflict in CMakeLists.txt and the HANDLED list (keep both sides).

Then: the decompilation's own "- [ ] Next:" line in docs/decomp-progress.md. Delete this file when done.
