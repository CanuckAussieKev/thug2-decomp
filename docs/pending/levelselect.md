# levelselect - the level select in sections, stars on the globe, signs for our levels

Agent "levelselect" (branch worktree-agent-ae38c3bcf5625afad). **NOT run in the game yet** (game runs were
stopped while the user was at the PC; one early run before the change only reached the existing menu).

## What changed

Data overlay only (no C++): `tools/menu_level/level_select.py` (new), `tools/menu_level/level_select_icons.py`
(new), and four lines in `make_menu_level.py` main() that call them (plus its header comment).

- **Sections.** `level_select.sections(thps4=..., thug1=...)` is the table: THUG2 (the game's own
  `level_select_menu_level_info`, with our SKATE SHOP / TEST LEVEL), then THUG1 (when its manifest is passed), then
  THPS4 - newest game first. Each extra section is a new array (`level_select_menu_thps4_level_info`, ...).
  - Up / Down (pad_up / pad_down handlers on the level HMenu, outside classic mode) run
    `level_select_section_change`: the global `level_select_section` moves on (wrapping), the level select is built
    again (`create_level_select_menu <...> keep_globe`: the globe keeps its position and turns to the new focus).
  - The section is remembered between visits (a global).
  - A header above the signs: the game's name with up / down arrows (the menus' `up_arrow` / `down_arrow`); the helper
    text gains "Up/Down = Game".
  - Classic mode keeps the game's own list and brackets (its brackets use Up / Down).
  - Network level select (in_server_options) goes through the same `create_level_select_menu`, so it gets the
    sections too; choosing still calls `level_select_menu_exit` with the level.
  - Mouse: the wheel sends pad_up / pad_down, so it switches games; hovering / clicking signs is unchanged.
  - TRAINING's `new_vmenu` is dropped (it put TRAINING alone in a VMenu whose pad_up / pad_down handlers would see
    the switch first; nothing else is in that column outside development builds).
  - `level_select_calculate_dots` caches the stars by game mode and dot count, which two sections can share: the
    cache is invalidated on every build. `level_select_valid_choice`'s star animation goes over the section's list.
- **Stars.** THUG2's star is `level_dot` (an orange star); the map is a 512 x 256 strip of 64-pixel tiles
  (`map0..21`, `level_map_pieces`) that `level_select_spin_globe` scrolls; `dot_pos` is a map pixel and the globe
  turns to the focused level's x. Each added level gets `dot_pos` from its real place: a longitude / latitude fit
  through THUG2's own stars (LA, Boston, New Orleans, Philadelphia, Skatopia, Barcelona, Berlin, Tokyo, Melbourne)
  plus the nearby stars' offsets (the map is freehand). Checked on a render of the map tiles. Levels with no place
  (the skate shops, TEST LEVEL, Hotter Than Hell) have no `dot_pos`: the globe spins, as for THUG2's PRO SKATER.
- **Signs.** Our own signs: THUG2's sign frame (from the user's `sign_training`) with a pictogram drawn in it
  (`level_select_icons.PICTOGRAMS`: skateboard, cone, bars, bridge, mortarboard, palm, anchor, bigben, paw, ferris,
  skyline, motorbike, torch, wave, sun, house, ramp, dome, flame), written as `images\LevelSelectSprites\sign_*.img.xbx`
  and loaded with the level select's sprites (`sprites.qb`). The img.xbx format (8-bit, 16-colour BGRA palette,
  swizzled) is read / written by `level_select_icons.read_img` / `write_img`: all 61 of Levelselect.prx's images
  round-trip byte-identically. Preview: `python -I tools/menu_level/level_select_icons.py <out.png>`.

## Per level

| Section | Level | Sign | Place (star) |
|---|---|---|---|
| THUG2 | SKATE SHOP | skateboard | none (spins) |
| THUG2 | TEST LEVEL | cone | none (spins) |
| THPS4 | COLLEGE | mortarboard | USC, Los Angeles ("California College" in THPS4's level list; the Coliseum by the campus) |
| THPS4 | SAN FRANCISCO | bridge | San Francisco |
| THPS4 | ALCATRAZ | bars | Alcatraz Island |
| THPS4 | KONA | palm | Kona Skatepark, Jacksonville, Florida (snake run, comps, skate camp) - not Hawaii |
| THPS4 | SHIPYARD | anchor | Baltimore harbour - inferred: Baltimore is the only city its scripts name |
| THPS4 | LONDON | bigben | London |
| THPS4 | ZOO | paw | San Diego Zoo |
| THPS4 | CARNIVAL | ferris | Louisiana bayou - inferred: a county fair with a swamp and alligators, no city named |
| THPS4 | CHICAGO (was "HOFFMAN") | skyline | Chicago (loadscrn_chicago; THPS4's own level select calls it Chicago) |
| THPS4 | SKATE SHOP | skateboard | none (spins) |
| THPS4 | MOTOX | motorbike | Chicago (Chicago's loading screen and sky) |

THUG1 (`THUG1_LEVELS`, from the thug1 agent's manifest: its `location` text is matched against `PLACES`, its `icon`
used where we have no pictogram): NJ house, NY torch, FL palm, SD sun, HI wave, VC THUG2's sign_canada, SJ ramp,
RU dome, SE flame (no star), SC / SC2 sign_school, DJ sign_jam, PH sign_philly, VN palm, HN sign_airport, NJ skate
shop skateboard. Offline: the three-section level select compiles with NeverScript.

## For the merge

- `make_menu_level.py` passes only `thps4=`; once the THUG1 levels are installed by their tooling, pass
  `thug1=<its manifest>` to `level_select.sections` (and don't add THUG1 signs to THUG2's list).
- `add_skateshop_sign(select)` is now called without THPS4 (its THPS4 signs went into THUG2's list).

## Not checked yet (needs a game run)

- The header's place (320, 16) and size, the arrows' scale; whether the focused (enlarged) sign covers it.
- That pad_up / pad_down reach the HMenu's handlers (as pad_left / pad_right do), the rebuild, the sound.
- The new signs and stars drawn; the globe turning to THPS4 levels; locked levels; network level select.
