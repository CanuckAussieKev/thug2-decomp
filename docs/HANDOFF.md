# Handoff: work in flight (2026-10-16)

Read CLAUDE.md and `.claude/skills/decomp-batch/` first; the history is in `docs/decomp-progress.md`.

## Writing PartyMod's patches into the source

Goal: give source to the 48 game functions PartyMod patches (113 patches, ~35 KB), with PartyMod's changes
built in, so its run-time patching can go. `tools/ghidra/partymod_patches.py <snapshot>` lists them;
`python -I tools/check_partymod_overlap.py` fails on any PartyMod patch inside a rewritten function that its
HANDLED list doesn't account for. Done so far: LoadQB, Sio::Device::Process, ActivateActuator.

Parallel agents were started, each in its own git worktree and branch, with its own Ghidra snapshot copy
(`C:\Dev\thug2-wt\ghidra-<name>`) and a copy of `data/derived/`:

| Worktree / branch | Functions | State |
|---|---|---|
| `C:\Dev\thug2-wt\gfx` / `pm/gfx` | 0x4A0780, 0x4A07C0, 0x4B2330, 0x4B2440 (PartyMod jumps from their start), maybe 0x48C170 | capped; check its commits |
| `C:\Dev\thug2-wt\glyph` / `pm/glyph` | small ones: 0x48D7E0, 0x48D950, 0x4B6B80 | capped; check its commits |
| `C:\Dev\thug2-wt\platform` / `pm/platform` | 0x5F3A50, 0x5F56E0 (PartyMod jumps from their start), maybe 0x5A6E40, 0x5F44F0 | capped; check its commits |
| `C:\Dev\thug2-wt\skater-a` / `pm/skater-a` | 0x50CA00, 0x509BD0 | stopped at the start - nothing useful |
| `C:\Dev\thug2-wt\skater-b` / `pm/skater-b` | 0x4FCBA0, 0x504060, 0x507120, 0x51F4C0, 0x527510, 0x527600, 0x5334D0 | stopped at the start - nothing useful |

Each agent writes its record to `docs/pending/<name>.md` and its tests to `src/hook/selftests_<name>.cpp`.

### Merging a branch (from C:\Dev\thug2-decomp, on main)

1. `git log main..pm/<name>` and review the diff; a branch may hold uncommitted work in its worktree
   (`git -C C:\Dev\thug2-wt\<name> status`) if its agent was cut off - check before discarding it.
2. `git merge pm/<name>`; conflicts are expected only in CMakeLists.txt (file lists) and the HANDLED dict in
   tools/check_partymod_overlap.py - keep both sides.
3. Build (`tools\verify_build.cmd`), run `out\build\verify\thug2_check.exe` (all early tests pass, 0 MISMATCHED),
   `python -I tools/check_partymod_overlap.py`, the register scan and the call audit (see the skill).
4. Move `docs/pending/<name>.md` into `docs/decomp-progress.md` as a numbered batch entry (before "- [ ] Next:"),
   delete the pending file, run `python -I tools/progress_report.py`, commit, push.
5. When all are merged: `git worktree remove C:\Dev\thug2-wt\<name>` and `git branch -d pm/<name>`; delete
   `C:\Dev\thug2-wt\ghidra-*`.

### Still to do after that

- The rest of the 48: graphics 0x4B2D80, 0x4B9030, 0x4DB200, 0x4DF390, 0x44FA80; menus/glyphs 0x4CFD00,
  0x4CECB0, 0x4D8650; skater controls (the skater-a/b lists above); platform 0x44FDC0, 0x452910, 0x4E1EF0,
  0x5A8730, 0x545880, 0x54DCB0; WinMain 0x4E2680 and MainLoop 0x44EEF0 (both host the test runner's hooks -
  see src/hook/selftest_runner.cpp; ParseNetworkParams 0x4E1220 too). The 8 GameSpy functions (0x5F8690 and
  up) are library code: PartyMod's patches there (OpenSpy host names) become configuration of the linked SDK.
- Then: the decompilation's own "- [ ] Next:" line in docs/decomp-progress.md.

Delete this file when the work above is merged.
