# Fix: the level select's Default (debug_mode=1) crashed

- Cause: the PC release ships only the DEVKIT_ONLY level Default's scripts and sky (Data\levels\default\default_Scripts.qb,
  default_sky\). Its scene (levels\Default\Default.scn.xbx) and node array (Default.qb) aren't there, and no pre has
  them. LoadScene's load (0x004B36D0) returns no scene, and 0x0048C560 writes through it (an access violation at
  0x0048C5AA). Every way of loading Default crashes, `-level Default` included. Skipping the scene load only moves
  the crash to the missing node array (NextNode / CArray::GetElement). The user's "other way" was TEST LEVEL
  (Level_TestLevel: scene TestLevel from TestLevelscn.pre). That's a different level; it only shares
  level_number=LevelNum_Default.
- tools/menu_level/make_menu_level.py: `drop_uninstalled_default` drops the Default entry from the overlay's level
  select unless levels\Default\Default.scn.xbx and Default.qb are installed.
- src/game/front/start_level.cpp (not original): `-level` refuses a level that has no scnpre and whose
  levels\<level>\<level>.scn.xbx isn't installed. It logs this, and the main menu stays.
- Tested in the game (THUG2-1, debug_mode=1):
  - In the level select, SKATE SHOP is followed directly by TEST LEVEL.
  - `-level Default` logs the refusal and stays at the main menu.
  - `-level thps4_zoo` and `-level BO` load.
  - Self-tests pass.
