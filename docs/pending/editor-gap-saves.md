# Level editor: the career save never gets an edited copy's gaps (NOT ORIGINAL - the user's request)

Branch `worktree-agent-aa55df361c5b8d12a` (off main fb4aefe). **NOT run in the game yet** (the user tests the editor by
hand): built and checked offline only. Supersedes phase 5's caveat (docs/pending/editor-triggers.md: "saving the career
while an edited level with new gaps is loaded writes those gaps into that level's career gap list").

## Where the save writes the gap checklist (found in THUG2PM.exe)

- The game save's structure is built by **0x005A7560** (not rewritten; its only caller of the career's writer at
  0x005A7905): the skate module's settings, then **`CSkaterCareer::WriteToStructure` 0x00585550** (ours,
  src/game/skate/career.cpp), then the level records **0x0057FC50** (ours, records.cpp: high scores per level), then
  the pros **0x0057B840** (ours, profile_manager.cpp: stats). The script command GetCareerSaveInfo (0x00585E20) calls
  the same writer.
- 0x00585550 writes, for each of the 21 levels, its gap checklist (the career's +0x63C + level * 4): `built`, and when
  built every gap's name, times landed, score, the skater's start position / direction. The load side is
  `ReadFromStructure` 0x00584D00 (called by 0x005A7CF0): each checklist cleared and its gaps added again.
- The editor's gaps go into the current level's checklist (the skate module's 0x005656C0 -> the career's), and gaps
  landed count there (GotGap 0x00578C70 -> AwardPendingGaps 0x005796C0: m_count).

## What was done

- **src/game/front/level_editor_career.{h,cpp}** (new, `Front::LevelEditor::Career`): when the level becomes its
  edited copy (`Layout`'s three places that set it: Tick's layout put in place, EnterEditedCopy - the editor opened -,
  the in-place switch), the current level's checklist is copied first - the **baseline** (built; each gap's name,
  score, times landed, positions). While it is kept, the save writes the baseline for that checklist (`ForSave`): no
  editor gaps, no gaps landed in the edited copy (the level's own included - the layout may have made them trivial).
  When the level stops being the edited copy (another level / the same one loaded again: OnSceneLoaded; switched back
  in place to the level as shipped), the checklist is **put back as the baseline** (cleared, the gaps added again -
  what the career's load does; `built` as it was), so a save made later doesn't get them either. Logged:
  `the career's gap list kept as it is (...)`, `... put back as before the edited copy (...)`,
  `the career save gets this level's gap list as before the edited copy (...)`.
- With no baseline (no checklist when the copy began) the save still leaves out the editor's gaps of the checklist
  they went into, by name (a level gap of the same name - the level's own EndGaps - is kept):
  `Triggers::EditorGaps` (new, level_editor_triggers.h).
- The career replaced while a baseline is kept (a save loaded - end of ReadFromStructure; a new career - Reset with
  its gaps): the baseline is taken again from the career as it now is (`CareerReplaced`), so putting it back never
  brings an older career back.
- career.cpp's WriteToStructure: when `Career::SaveView` says so, the level's entry is written from the view (same
  fields and order as the original's); otherwise exactly as before (the original's self-test, TestCareerSave in
  selftests_viewer17, still matches byte for byte).

## Decision on career credit in an edited copy (task item 2)

Conservative: playing an edited copy must not change career progress. **Gaps: done** (above) - the gap list in the
pause menu still shows gaps landed during the session (the player sees them), but the save never gets them and they're
gone once the edited copy is left. **Not covered (no single clean place - documented risks):**
- goals completed in an edited copy (career mode: the editor can be opened in a career level - LevelEditor_Available
  has no game-mode check): the career's goal / flag words (SetGoal 0x00584140), cash, the goal manager's rewards (stat
  points in the pros, 0x0057B840) - suppressing SetGoal would desync the goal manager (not rewritten);
- the goal manager told of gaps landed (AwardPendingGaps -> 0x005586E0: gap-count goals);
- the level's high scores (records 0x0057FC50) from an edited copy;
- `[replacements] disable=career.cpp` (the original writer runs) turns the save filter off; disabling
  level_editor_layout's hooks doesn't matter (the calls are in our code).
A follow-up could block the career save (or the goal manager's credit) while `Layout::EditingCopy()` in career mode, or
hide LEVEL EDITOR in career mode.

## Checks (offline)

- Build (own out\build\verify, own PDB endpoint): no errors / warnings. thug2_check on THUG2PM.exe and on
  C:\Games\THUG2\Game\THUG2.exe (read-only): 0 MISMATCHED, early self-tests all passed (397). check_code_writes: no
  overlaps.
- New self-test `TestEditorCareerSave` (src/hook/selftests_editor_career.cpp): EditorGap (editor name, a level gap of
  that name, case, no level names), ForSave without a baseline (editor gaps dropped, order / scores / positions kept),
  with a baseline (written instead of the live list; an editor gap in it dropped; an unbuilt baseline written unbuilt).
- Mutations (out\scripts\mut_career.ts): 8/8 caught (level-gap check off, editor-name check off, never the editor's,
  baseline ignored, built forced / taken from the live list, the filter off / inverted).
- The game-side parts (baseline taken / put back, SaveView) aren't covered offline: the checklist list needs the
  game's heap - the in-game list below.

## In-game test checklist (for the user) - NOT run in the game yet

Use a throwaway save (or back up the save folder) first.
1. **Baseline taken:** free skate Boston (shipped), land one of its gaps (e.g. a stair gap) - it shows in the pause
   menu's gap list. Open LEVEL EDITOR: log `the career's gap list kept as it is (the edited copy: N gaps, M landed)`.
2. **Editor gap not saved:** add a Gap (Triggers window), land it, and land a level gap not landed before. The gap list
   shows both landed. Save the game (pause > save, or the career save). Log
   `the career save gets this level's gap list as before the edited copy (...)`.
3. Quit to the main menu, load that save, free skate Boston as shipped: the gap list has **no "Editor Gap 1"**, the
   level gap landed in step 2 **not landed**, the gap from step 1 still landed.
4. **Put back on leaving:** load the edited copy again (level select's EDITED LEVELS), land a level gap, then pick
   Boston as shipped from the level select (switch in place): log `... put back as before the edited copy (...)`; the
   gap list as it was before (that gap not landed, no editor gaps). Save, reload: the same.
5. **Another level:** in the edited copy land a level gap, then load another level (e.g. New York) and save; load the
   save: Boston's list (shipped) without that gap landed, no editor gaps.
6. **Loading a save while edited** (if the menus allow it mid-level): log `kept as it is (the career replaced ...)`;
   leaving the edited copy afterwards keeps the loaded save's gaps (not the older ones).
7. **Shipped levels unaffected:** in a level never edited, land a gap and save: it is saved landed (no editor log
   lines).
8. Career mode (risk check): open the editor in a career level, complete a goal, save - note whether the goal counts
   (expected: it does - not covered, see above).

## Files

New: src/game/front/level_editor_career.{h,cpp}, src/hook/selftests_editor_career.cpp. Changed (small):
CMakeLists.txt (+2 lines), src/game/skate/career.cpp (WriteToStructure's view branch, two CareerReplaced calls),
src/game/front/level_editor_layout.cpp (Career::EditedCopy at the five places g_editedCopy changes, an include),
src/game/front/level_editor_triggers.{h,cpp} (EditorGaps).
