# Level select: MISCELLANEOUS section

Request: move the skate shops / tutorials / test levels out of their games' sections into one last section; THP8 after THAW.

- tools/menu_level/level_select.ts: `sections()` takes the levels flagged `misc` in the game tables (THPS3 SkateShop, Tut;
  THPS4 Motox, SkateShop; THUG1 NJ_Skateshop; THP8 z_bedroom, z_skateshop, z_training) plus THUG2's SKATE SHOP (18) and
  TEST LEVEL (21) (`THUG2_EXTRA`, which now carries level_num and load script; `editLevelSelect` deletes those two entries
  from THUG2's own array instead of re-signing them) into a last section `level_select_menu_misc_level_info`, title
  MISCELLANEOUS, in `MISC_ORDER`. Sign text is "<game> <name>" (THUG2 SKATE SHOP, P8 BEDROOM, THPS3 TUTORIALS, THUG SKATE SHOP...).
  A game section left with no level is dropped (THUG1, and THPS4 when only those were converted). THUG2's stays.
- level_nums: unchanged - every level keeps the number it had (18, 21, THP8 / THPS3 / THPS4 / THUG1 manifest numbers: 73, 72, 31, 29,
  55 here); `sections()` stops with an error if two in the section collide (none do). They stay in the info-panel records skip
  lists (global by number, as before); dots are worked out per section on every build, so a star's id never meets another section's.
- Entries of other games carry `game_tag="THPS3"` etc. (THPS3 / THPS4 / THUG1 only - not THP8): level_editor_nodes.cpp
  LevelIsShared reads it from the misc list, so editing those levels is "shared with that game" as before.
- C++: the new list name added to the lists that walk the level select's arrays: net GetLevelName / LevelListName
  (gamenet_manager.cpp, kPortedLevelLists - THUG2's own skate shop / test level names come from it now), thugpro_compat.cpp
  (position scale lookup), level_editor_layout.cpp (edited copies). (Noted, not changed: gamenet_manager's list has no
  thps2 / thp8 arrays.)
- Order (Up/Down): THUG2, THUG1, THPS4, THPS3, THPS2, THAW, THP8, MISCELLANEOUS, then EDITED LEVELS (OFFLINE) (when any).
  THP8 already followed THAW in the generated qb; what the user saw is Up from THUG2 (the first section) wrapping to the last
  one, which was THP8 - now MISCELLANEOUS (or EDITED LEVELS when it exists).
- Checks: npm typecheck / lint / test pass (new test in menu_level.test.ts); verify_build ok. NOT run in the user's game. Workspace
  THUG2-2 run: Up from THUG2 shows MISCELLANEOUS, THPS3 SKATE SHOP loads (a picture of the shop, no errors in the log).
