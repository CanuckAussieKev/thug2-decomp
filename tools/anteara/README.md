# The Anteara Mod (tools/anteara)

The user's own THUG2 script mod, "Anteara Mod". Its 2010 and 2011 versions are merged here, with 2011 as the base.
Every 2010 feature that 2011 dropped is back, except the Polish menu (`skejt23.qb`). It's an optional part of the data
overlay and is off by default.

## Switching it on

Set this in `thug2-decomp.ini`:

```
[features]
anteara_mod=1
```

- `anteara_mod=1` gives you the whole mod: its menus (Pause > ANTEARA MENU, with a **2010** submenu for 2010's menu
  features) and every gameplay change listed below.
- `anteara_mod=0` (the default) gives the game exactly as before.

It's one switch for now. Per-feature switches would go into the mod's scripts as flags, the way the 2010 menu's
KEYBOARD SYMBOLS toggle works.

With the mod on, the data overlay's own changes keep working: the ported levels in the level select and Levels.qb, SKATE
SHOP / TEST LEVEL, LEVEL EDITOR and DEBUG OPTIONS in the pause menu, the skate-shop front end and its standard main
menu, and CREATE 100% CAS.

## How it's built

- **Only the mod's own changes are in the repo, in `mod\`.**
  - `mod\new\`: the mod's own files as NeverScript `.ns` (`anteara_stuff\*`, `anteara_2010.qb`, `night_menu.qb`,
    `Car_ModelViewer.qb`).
  - `mod\patch\`: for each game script the mod changes, only the scripts and globals it changes or adds.
  - `mod\manifest.json`:
    - per file: the units the mod removes, where each new unit goes (`insert_after`), and line edits for
      `localtext.qb` (two words in its huge `Translations` array);
    - the mod's `qdir.txt` changes: files it removes, adds, or loads in another order.
- **`anteara.ts install`, called by `tools/menu_level/make_menu_level.ts` on every run**, writes
  `<game>\thug2-decomp-data\anteara\`: the mod's complete script set.
  - Every file in its `qdir.txt` comes from the game's loose `Data\scripts`, which is what the mod was made from. The
    archive `qb_scripts.prx` differs from those loose files in about 60 files (debug printfs, for one), and it would
    win over them.
  - The mod's changes are applied on top.
  - The overlay's own edits are re-applied where it changes the same files, using make_menu_level.ts's functions:
    - `Levels.qb`: the skate shop and ported levels;
    - `gamemenu_levelselect.qb` / `sprites.qb`: the sections and signs;
    - `gamemenu_pause.qb`: LEVEL EDITOR and DEBUG OPTIONS;
    - `anteara\front_end\scripts\game\menu\roundbarmenu.qb`: the front end's roundbar, while the THPS4 skate shop
      backdrop is installed.
  - Files with no change are copied as they are. `qdir.txt` and `localtext.qb` are written too.
  - CREATE 100% CAS's `mainmenu_options.qb`, the front end's `mainmenu_scripts.qb` and the rest aren't in the mod's
    set, so they come from the overlay as before.
- **The DLL** (`src/game/front/data_overlay.cpp`):
  - While `anteara_mod=1`, a file is looked for in `anteara\front_end\` (only while the skate-shop front end is on),
    then `anteara\`, then the overlay's `front_end\` and top as before, then the game's archives and files
    (`SearchFolders`; self-test `TestDataOverlay`).
  - The game reads `scripts\qdir.txt` after `qb_scripts.pre`, so the mod's `qdir.txt` decides what loads.
  - The log says `anteara_mod: on - ...` once at start-up.
- **Remaking `mod\` from a merged mod folder** (the `.ns` sources and `qdir.txt`; the user's copy is
  `C:\Games\THUG2_PM_BAK\Backup\scripts_unified`):
  - `node tools/anteara/anteara.ts extract --unified <folder> --game <game> --ns out\neverscript\ns.exe` compares
    each file with the game's loose script unit by unit, with names as checksums, and keeps only what differs.
  - `... check --unified <folder> --overlay <game>\thug2-decomp-data` compares an installed set with the merged sources,
    names ignored. Expected differences: the overlay's own edits in Levels / level select / sprites / pause menu, and
    patched files' units in another order.
- **The `.ns` text is Latin-1.**
  - Some strings hold bytes ≥ 0x80 (button glyphs in the keyboard and chat), which NeverScript copies byte for byte.
    `.gitattributes` keeps `mod\` byte-exact.
  - Names with spaces or dashes ("AL Deck 1", "Baseball-T") are in backticks.
- **Assets the mod names that the game doesn't have stay missing, as in the original.**
  - The luge and slalom animations: `anims\skater_luge\*.ska`, `anims\skater_slalom\*.ska`. The loader skips the
    whole set when its first file is missing.
  - Whatever 2010/2011 shipped outside the scripts.
  - Checked against Data and every `.prx`'s file list. Every model the mod names was found under some name (the
    match is by file name only), including the fireball and halfpipe models.

## 2010 features brought back

### In the 2010 submenu (`game\menu\anteara_stuff\anteara_2010.qb`)

All of these are 2010's own scripts, unchanged. Their Back / Done buttons go where they went in 2010, mostly to the
Anteara Menu.

| Item | Script(s) | In 2010 |
|---|---|---|
| ON/OFF MENU | `anteara_onoff_menu` and its `anteara_*onoff` submenus (combo run-out timer, wall jumps, wallshuffles, skate on walls, no clip, night lights) | Anteara Menu |
| CUSTOM RESTARTS | `create_customrestart_menu` (2011's version, using `set_custom_restart1`) | Anteara Menu (2011 still had the script but no menu item) |
| AMMO MENU | `create_ammoyeah_menu`, `create_ammoitem_menu`, `give_me_ammo` | Anteara Menu |
| OBSERVE MENU | `anteara_observing_menu`, `anteara_start_observing`, `anteara_stop_observing` | Anteara Menu |
| SPAWN NPCS | `create_mynpc_menu` | Anteara Menu |
| NIGHT MENU | `night_menu` (in `night_menu.qb`) | Anteara Menu |
| Balance Meter Colour | `anteara_bal_menu`, `bal_*` | Theme Menu |
| Balance Arrow Colour | `anteara_arrow_menu`, `arrow_*` | Theme Menu |
| Sit Down Luge | `anteara_slalom` (needs retail's `slalom_params`, below) | Vehicle Menu |
| Halfpipe | 2010's halfpipe spawn, `#fcaa1c10`. Its name is written as the checksum: see "Names" below | 3D Model Menu |
| FLICKERING SKATER | `flickering_skater` (2011's script) | Miscellaneous Menu |
| FLICKERING DECK | `flickering_deck` (2011's script) | Miscellaneous Menu |
| CREATE 3D TEXT | `create_3dtext`, `Create3dText_final` (2011's 3D model menu has its own `anteara_3d_text`) | Miscellaneous Menu |
| SCREEN MODE | 2010's aspect ratio menu, renamed `anteara_2010_screen_menu` because 2011 has its own `create_antearascreen_menu`. It uses `anteara_screen_set_menu` | Anteara Menu |
| GUITAR MENU | `anteara_guitar_menu` (it plays 2011's `anteara_guitar*` scripts) | no menu reached it |
| CHAT SPAM (/df) | `kirillgta_spam` | chat command `/df` |
| CHAT SPAM: ALX THE BEST (/2010) | `kirillgta_spam2` | chat command `/2010` |
| MIDDLEMAN: CLEAN + HELP | `anteara_middleman` | not reached |
| MIDDLEMAN 2: CLEAN + LIGHTS | `anteara_middleman2` | not reached |
| VEHICLE TEST (TRICYCLE) | `anteara_vehtest` | not reached |
| HIDE BOARD / UNHIDE BOARD | `anteara_hide_board`, `anteara_unhide_board` | not reached |
| MODELS MENU | `models_menu` (`night_menu.qb`) | not reached |
| Morning RGB2 | `morning_shadows2` (`night_menu.qb`) | not reached (2010's NIGHT MENU "Morning RGB2" item runs `morning_shadows`, as written) |
| TIME OF DAY TEST MENU | `test_func3` (`night_menu.qb`) | not reached |
| TIME OF DAY CYCLE (LOOPS) | `test_func` (`night_menu.qb`): morning, day, evening, night every 5 s. It calls itself at the end, so it nests one level deeper each cycle | not reached |
| CUSTOM POPUP MESSAGE | `launch_popup_keyboard` → `menu_pupup_message` → `popup_enter_mess` (in `net.qb`). The message has no player name / position, because nothing passes them | not reached |
| KEYBOARD SYMBOLS (2010): OFF/ON | toggles `anteara_2010_keyboard`, see below | (2010 always on) |

The items' text is 2010's where 2010 had a menu item. Items that 2010 never put in a menu got a descriptive label.

### Applied where they belong (no menu item)

- **Player options dialog** (`game\net\net.qb`, `create_player_options_dialog`): 2010's dialog.
  - Back: STFU NOOB, is a Gay, like cumshots, keyboard, Teleport to host, Show Host Position, Show Host Speed On
    chat, and the "Popups List" dialog when you're not the host.
  - Scripts added at the end of `net.qb`: `popup_noob`, `popup_gay`, `popup_cum`, `popup_slap`, `teleport_to`,
    `idSkater_pos`, `idSkater_speed`, `launch_popup_keyboard`, `menu_pupup_message`, `popup_enter_mess`.
- **Pedestrian knock-down** (`game\ped\Sk6Ped_StateLogic.qb`): 2010's `STATECONTROL_chasecatch` and the counter
  `ped_chase_count = 0`.
  - A ped that catches you knocks you down (`PedKnockDown`), shows "You suck! ... Knock dows?", waits and goes back
    to its default state.
  - Also 2010's `STATECONTROL_disgust`: a disgusted ped gives chase.
  - See the bug fixes for what never worked here.
- **Projectiles** (`game\skater\projectiles.qb`, `ClientLaunchProjectile`): 2010's skateboard, Crystal_Ball and
  Anteara_Fire models. 2011's own ammo menu also offers Skateboard and Crystal Ball, which had no model in 2011.
- **Free skate and other game modes** (`gamemodes\gamemode.qb`):
  - `mode_freeskate`: `accumulate_score=1`, `should_modulate_color=1`.
  - `mode_freeskate2p`: `accumulate_score=1`.
  - `mode_netlobby`: `accumulate_score=1`, `should_modulate_color=1`, `show_leader_messages=1`.
  - `mode_singlesession`: `should_modulate_color=1`, plus the other 2010 values in that struct.
- **Theme colours** (`game\menu\themes.qb`), 2010's values:
  - `DE_ALT_COLOR` [180 67 92 128].
  - `DE_BALANCE_ARROW_COLOR` [0 255 96 100].
  - `DE_BALANCE_METER_COLOR` [225 18 213 100]: pink, which is what 2011's theme menu says the bar starts as.
  - `DE_HIGHLIGHTED_TEXT_COLOR_NEW` and `DE_UNHIGHLIGHTED_TEXT_COLOR_NEW` [39 230 223 118]: the cyan menu text.
- **Tricycle model line** (`game\menu\models.qb`, `create_cutscene_heads_model_set`).
- **Front Foot Impossible** `AirBorne` (`game\skater\airtricks.qb`).
- **Boneless**: Left, Right, Square (`TripleInOrder`) in `FlatLandBranches` (`game\skater\manualtricks.qb`), added
  ahead of 2011's entries.
- **Infinite time limit** (`game\misc\gameoptions.qb`, `time_limit_options`).
- **Start-up movie** (`game\misc\loading_screens.qb`): 2010's Shorty's movie (`movies\shortys`) plays, then 2011's
  `loadscrn` loading screen shows.
- **Chat commands** (`anteara_chat.qb`, `entered_chat_message`): `/df`, `/2010`, `/type` (`kirillgta_spam3`) and
  `/lolwtf` (`anteara_spam1`). They were also added to the list of commands that aren't sent as chat.
- **Luge / slalom animations** (`allanims.qb`: `animload_THPS6_LugeandSlalom` and its call).
- **Retail's slalom vehicle** (`game\skater\skater_vehicle.qb`: `slalom_params`, `slalom_start_script`,
  `slalom_bail_script`). 2010 kept these from retail and 2011 had deleted them. `slalom_params` is back in the
  vehicle exit's `RemoveTags` list, as in retail.
- **night_menu.qb**: 2010's file, minus the scripts 2011 redefines (see "Conflicts").

## Conflicts and how they were resolved

- **Keyboard symbols**:
  - 2010's `keyboard_button_pressed` turned `[ ] { } ` * ^ & # !` into button glyphs, and `#` into a coloured
    "Anteara Mod". 2011 dropped that and types those characters as they are.
  - Kept 2011's. 2010's mappings are in 2011's script behind `anteara_2010_keyboard`, which is off by default; the
    2010 menu's KEYBOARD SYMBOLS item toggles it.
- **Shadows / time of day**:
  - `night_shadows`, `morning_shadows`, `day_shadows`, `evening_shadows`, `kirillgta_nightON` and
    `kirillgta_nightOFF` are in both versions. Kept 2011's, which also switch the night lights.
  - `night_menu.qb` keeps only 2010's own scripts, with a comment where each one was removed.
- **Chat**:
  - 2010's `/ohai` ran `kirillgta_spam4`. In 2011 `/ohai` runs `anteara_spam1` and `/hai` runs `kirillgta_spam4`.
    Kept 2011's. 2010's `/lolwtf` → `anteara_spam1` is back.
  - 2010 coloured every chat line at random. 2011 uses the TextColor preference. Kept 2011's.
  - 2010's `anteara_text_help` (its /help list) and its longer `kirillgta_spam4` animation: kept 2011's.
- **Pause / Anteara menu scripts 2011 rewrote** (kept 2011's):
  - `create_anteara_menu`, `create_more_menu`, `anteara_theme_menu` (now toggles), `anteara_vehicle_menu` (now
    the 22-vehicle picker).
  - The vehicle scripts (`anteara_tricycle`, `anteara_scooter` and the rest, which 2011 made exit the old vehicle
    first), `anims_menu2`, `anteara_starwars`, `specialitems_menu`, `moving_menu`, `create_customrestart_menu`,
    `flickering_deck`, `create_pause_menu`.
  - The on/off scripts: `walljumpon/off`, `wallskateon/off`, `wallshuffleon/off`, `anteara_timeron/off`,
    `anteara_noclipoff`.
- **The 2010 menus call 2011 scripts.** The 2010 menus above call some scripts that 2011 kept under the same name but
  changed (`anteara_theme_menu`, the on/off scripts, `anteara_noclipoff`, `anteara_guitar*`). So:
  - Choosing a 2010 colour reopens 2011's theme menu, whose "Balance Bar Colour: ..." label doesn't follow the
    change.
  - 2010's ON/OFF submenus no longer jump back to the ON/OFF menu after On / Off (2011's versions don't). 2011's
    ON/OFF menu labels don't follow these changes either.
- **Halfpipe**: 2010's halfpipe spawn and 2011's `anteara_pipe` (3D MODEL MENU > Halfpipe) are both kept. 2010's is
  the 2010 menu's Halfpipe.
- **Airshuffle**: 2011 put the trick behind the `ANTEARA_AIRSHUFFLE` flag (ON/OFF MENU). Kept 2011's.
- **Start-up**: 2010 played the movie and stayed on a blank screen. 2011 showed `loadscrn`. Merged: movie, then
  `loadscrn`.

## Bug fixes

Each fix is marked `// MERGE: FIX` in the `.ns`.

1. **randomrange** (`game\misc\SK4_Scripts.qb`): all 18 `randomrange(a, b)` calls are back, in the 5 sound / idle
   scripts plus the wait and cycle counts. 2011's compiler had written them as bare pairs `(a, b)`.
   - The same loss was in two more 2011 files and is fixed there too: `game\skater_fx.qb` (6, the explode velocities)
     and `game\skater\skater_vehicle.qb` (2: a rotation and a sticker-slap pitch).
   - 2010's pedestrian scripts had lost theirs. The merged `Sk6Ped_StateLogic.qb` is 2011's, which matches retail,
     plus 2010's two changed scripts, which don't use randomrange.
2. **`SPECIALGRIND_SCORE = 1250`** is back in `game\skater\grindscripts.qb`, where retail and 2010 have it.
3. **The "no-kill flag" wasn't undefined.**
   - The checksum `#3c97b174` that `SK3_KillSkater` / `_Water` / `_Pungee` test is `FREEMAN_BOUND`. It was declared
     all along in `global_flags.qb` as 323.
   - 2011's ON/OFF MENU "No Out Of Bounds" sets and clears it, which stops kill zones from killing you.
   - The earlier report called it an undeclared `ANTEARA_NO_KILL_FLAG`. That was wrong, so nothing was added to
     `global_flags.qb`. The sources now say `FREEMAN_BOUND`.
4. **Stray 0x16 byte** (`game\cas_skater_f.qb`): the byte before `01 04 01 03` in `skater_f_torso` is dropped. The
   `None` torso and the "SPORTB" Shirtless torso are two entries again.

### Calls that never worked in the original

The mod's own compiler stored some names' checksums byte-reversed, so the game never found what they named. These
are fixed to the real names so the features work as written:

- **2010 knock-down** (`STATECONTROL_chasecatch`): `FormatText`, `TextName`, `text`, `create_console_message`,
  `rgba` and `wait_and_die` were all reversed, so the "You suck! ... Knock dows?" message never showed in 2010.
  - The counter itself (`ped_chase_count`) was reversed too, but consistently, so writing its real name changes
    nothing.
  - The script still does what it says. Its line `ped_chase_count (<ped_chase_count> + 1)` calls the global instead
    of changing it, and the message reads a `<ped_chase_count>` parameter that isn't set, so the count shows empty
    and stays 0.
  - Left as written. The author probably meant `Change ped_chase_count=(ped_chase_count + 1)` and
    `a=(ped_chase_count)`.
- **2011 exploding skater** (`game\skater_fx.qb`, `make_skater_explode`): the four `Dismember` bones
  (`Bone_Collar_L`, `Bone_Collar_R`, `Bone_Chest`, `Cloth_breast`) were reversed, so those bones never came off.
- **Names reversed consistently** (no change in behaviour), now written out:
  - `make_skater_lol` in `skater_fx.qb`. Nothing calls it.
  - The counter `ped_chase_count`.
  - Exception: 2010's halfpipe script (`anteara_pipe`, reversed) stays as `#fcaa1c10`. Written out, it would collide
    with 2011's different `anteara_pipe`. Its menu item calls the same checksum.

### Small things the rebuild changes by itself

- `game\menu\anteara_stuff\anteara_misc.qb`: one line in 2011 had an unclosed `(` (`<hmm> = ((<myArray> [0]).Four)`),
  which fails at run time. The source closes it, marked `/* lenient: ... */`.
- `game\skater\walking_control.qb`: 2011's file had 0x00 padding before its end. Gone (no code change).

## Left out

- `skejt23.qb` (2010's Polish menu, as asked). Its GPR Menu item in 2010's Miscellaneous Menu goes with it.
- 2011's unloaded working copies: `game\skater_mine\skater_nobugs\*`, `game\menu\gamemenu_pause_working.qb`,
  `game\goal_utilities2.qb`, `game\goal_utilities_old.qb`, `game\player_controlled_vehicle_old.qb`.
- 2010's unloaded backups (`game\skater\projectiles backup`, `tricks backup`, `game\misc\new intro`,
  `game\menu\themes\themes.qb`) and `localtext.BAK`.
- 2010 files that were retail copies: the `goal_*` scripts, `localtext_french/german/italian/spanish.qb`, and the
  empty `game\misc\tagsdb.qb` and `game\skater\deprecated_tricks.qb` (2010's qdir loaded these two; they hold nothing).
- 2010's menu item "test" (`ped_spawn_test`): the 2010 file ends in the middle of it (a `]` and two `}` missing), so
  it never ran. It's kept as a comment at the end of `anteara_2010.qb`.

## Names

- **Recovered originals** now used in the sources: `natas_spin` and `natas_extra_tricks` (grindscripts),
  `freeman_bound` (above), `anteara_explode` and `bone_breast` (skater_fx), and `getup_ped_chase`, `ped_chase_end`,
  `skater_bail_chase` (ped logic, unused).
  - Each was checked by compiling: the same checksums as the original files.
- **Real names stored reversed in the originals**, now written out: `anteara_pipe` (only as a comment: the script is
  `#fcaa1c10`), `make_skater_lol`, `ped_chase_count`.
- **No name known**: `#07b12bd2` in `skater_fx.qb` stays a raw checksum. The only match found, "0yes_truckstandbranches",
  is a collision.
- `decks.qb` and `cas_skater_f.qb`:
  - Their `desc_id`s with spaces or dashes ("AL Deck 1", "Baseball-T") are written in backticks, as retail does.
    Without them they'd compile wrong.
  - Their checksum tables (`__register_checksums__`) were dropped, because those names can't be written in the
    table's space-separated form. That only affects the names a decompiler shows, not the game.

## Checks done on the merged mod (scripts_unified, before it came here)

- Every `.ns` compiles with `ns -c` (no `-lenient`), and every compiled `.qb` decompiles again.
- Compared with the original 2011 `.qb` (both decompiled, then compiled with `-removeChecksums` so names don't count):
  195 of the 2011 files are unchanged code, 2 of them needing `-lenient` to decompile the original. The 19 changed
  files differ only by the edits listed here.
  - `localtext.qb` is the same code as 2010's.
- The names the merged code calls are defined in these files or retail's scripts, or are engine commands. The
  exceptions are names 2010 already called without a definition: `create_tool_menu` (3D text's keyboard cancel),
  `load_3d_obj_01`..`07` (MODELS MENU), and the time-of-day globals that both versions `Change`
  (`sun_red`, `bloom_on`...).
