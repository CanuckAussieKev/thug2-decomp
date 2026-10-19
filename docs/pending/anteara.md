# Anteara Mod in the project (agent "anteara", branch anteara-mod) - not a decomp batch

The user's own THUG2 script mod (Anteara Mod, 2010 and 2011 merged as in `C:\Games\THUG2\Backup\scripts_unified`) as
an optional part of the data overlay. It's switched by `thug2-decomp.ini [features] anteara_mod=1` (default 0). Details
are in tools/anteara/README.md.

- **What the repo holds (tools/anteara/mod)**: only the mod's own changes.
  - 11 files of its own (`new\`, .ns).
  - For 42 game scripts, only the scripts / globals it changes or adds (`patch\`, .ns).
  - `manifest.json`: removed units, where new units go, two line edits in localtext.qb, and qdir.txt
    removals / additions / reorders.
  - `.gitattributes` keeps mod\ byte-exact (Latin-1 glyph bytes in strings).
  - `anteara.py extract` makes mod\ from a merged mod folder and the game's loose scripts. `check` compares an
    installed set with the merged sources, names ignored.
- **The base is the game's loose `Data\scripts`**, not `qb_scripts.prx`.
  - The mod was made from those loose files, which differ from the archive in about 60 files (debug printfs, for
    one).
  - Since the archive wins over loose files, the installed set is complete: every file of the mod's qdir.txt
    (164 copied as they are).
  - Change from the lead's design: unchanged files are copied, not left to fall through, because the game would then
    take the archive's different versions.
- **Install**: make_menu_level.py calls `anteara.install()` on every run.
  - It writes `thug2-decomp-data\anteara\scripts\...`: qdir.txt, localtext.qb, the patched / new / copied files.
  - The overlay's own edits are re-applied with make_menu_level's functions to Levels.qb (skate shop, ported levels),
    gamemenu_levelselect.qb and sprites.qb (sections, signs) and gamemenu_pause.qb (LEVEL EDITOR, DEBUG OPTIONS).
  - `anteara\front_end\scripts\game\menu\roundbarmenu.qb` gets the front end's roundbar edit on the mod's version.
  - make_menu_level.py changes: the anteara import and call; the levels / level select / sprites edits as small local
    functions used for both; YOUR OPTIONS matched with `[Ii]d=` (the mod writes `Id=`); two header lines.
- **DLL**:
  - `Front::DataOverlay::SearchFolders` (pure) gives the search order: `anteara\front_end\` (while the skate-shop front
    end is on), `anteara\`, `front_end\`, the top. `OverlayPath` walks it.
  - `AntearaMod()` reads `anteara_mod` once and logs `anteara_mod: on - ...` (or that the folder is missing).
  - Self-test `TestDataOverlay`: all four combinations, proved by a mutation (a wrong folder name fails it).
- **skip_intro_movies** (the user's request): `Nx::PlayMovie` also skips any movie the start-up script
  `startup_loading_screen` plays, i.e. the mod's Shorty's movie. The same movie from GAME MOVIES still plays.
  - Self-test `TestPlayMovie` covers a stand-in start-up script and another script, proved by a mutation.
  - In the game: `skipping intro movie movies\shortys`, straight to the main menu.
- **Equivalence**: the installed set compared with scripts_unified, names ignored.
  - 175 files have the same code and 40 the same units in another order (patched files).
  - The other 4 (Levels, level select, sprites, pause menu) differ only by the overlay's own units (level structs,
    level select sections, LEVEL EDITOR scripts and items).
- **Checks**:
  - verify build (own PDB server, -j 2), thug2_check: early self-tests all passed (300).
  - `check_code_writes.py`: no overlaps.
- **Tested in the game (THUG2-3, under the lock; DLL from this branch, make_menu_level from this branch)**:
  - anteara_mod=0: the plain game as before. THPS4 Zoo via `-level`; the pause menu has LEVEL EDITOR / DEBUG OPTIONS
    and no Anteara menu; HIGH SCORE / FREE SKATE goes to the skater select.
  - anteara_mod=1 with the skate-shop backdrop:
    - the standard main menu (the mod's 2010 cyan menu colours), GAME OPTIONS with CREATE 100% CAS, the skater select;
    - the level select in sections, THPS4's College picked and loaded;
    - its pause menu with LEVEL EDITOR, DEBUG OPTIONS and ANTEARA MENU; the Anteara menu; the 2010 submenu.
  - Logs clean (no exceptions / run-time checks / CRT reports; self-tests passed).
- **Left for the user**: playing the mod's features (vehicles, 2010 items, chat commands, net popups). Every
  2010/2011 note stands (README).
  - The luge / slalom animations (`anims\skater_luge`, `anims\skater_slalom`) aren't in the game, as in the original.
  - CREATE 100% CAS reads the archive's scripts, not the mod's (its goal_utilities change is control types only).
