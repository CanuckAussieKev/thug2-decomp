# THP8 save: 100% / everything unlocked (agent "thp8-save", 2026-10-24)

The user's request: "can you try and make it 100% progress, everything unlocked in project 8 save?" - their Tony Hawk's
Project 8 (Xbox 360, played through Project8Recomp in C:\Games\THP8) save. Not a decomp batch: nothing to fold into
decomp-progress.md. The tool: tools/thp8_save (TypeScript, tests in thp8_save.test.ts).

    node tools/thp8_save/thp8_save.ts dump <skater-progress>       readable tree, checksums checked
    node tools/thp8_save/thp8_save.ts max <in> <out>               100% / everything unlocked
    node tools/thp8_save/thp8_save.ts diff <a> <b>                 what differs, by path

(`--thp8 <folder>`: the THP8 install whose data names the fields - default C:\Games\THP8, only read.)

## The save
`Documents\thps_p8\<profile>\415607DD\00000001\Project 8 Saved Content\skater-progress` (98304 bytes), with
`__thumbnail.png` and `...\415607DD\Headers\00000001\Project 8 Saved Content.header` (328 bytes: the content's
display name and the title id 415607DD - no size or hash of the save, so it isn't touched).

skater-progress (decoded from the user's own file; it reads and writes back byte for byte):

| offset | what |
|---|---|
| 0x00 | u32 BE: CRC of bytes [0, used) with this word as 0 - Neversoft's checksum (CRC-32 without the final inversion: zlib's crc32 ^ 0xFFFFFFFF, src/game/core/crc.h's) |
| 0x04 | u32 BE: the same CRC of the summary structure's bytes |
| 0x08 | u32 BE: the summary's size (30) |
| 0x0C | u32 BE: used size (79378 in the user's) |
| 0x10 | u32 BE: 0x47 (a version; kept) |
| 0x14 | the summary structure (the load screen's: careermissionscomplete / total, total_goals_complete, filename) |
| ... | the main structure: globalinfo {options, career, gamerecords, pros, shop_*_bought, training}, story {goalmanagerparams}, storyskater {records}, customskater {custom_classic, custom_story}, saveinvalidduetoleethacker |
| used .. 98304 | the byte 0x69 |

The structures are THUG2's WriteToBuffer format (src/game/script/struct_buffer.h): a type byte (| 0x80: the name is
entry n of qb.pak's `writetobuffer_compressionlookuptable_8`, one byte; | 0x40: of `..._16`, two bytes), the name
checksum, the value; 0 ends a structure. Everything is little-endian as THUG2's (names, integers, floats, array counts
and elements) except wide strings: UTF-16 **big-endian** up to a 0 character (the 360's memory copied). THP8's types:
1 int, 2 float, 3 string, 4 wide string (THUG2's local string slot), 5 pair, 6 vector, 0x0A structure, 0x0C array
{u8 type, u16 count, elements}, 0x0D name, 0x0E/0x0F s8/s16, 0x10/0x11 u8/u16, 0x12 int 0, 0x13 float 0, 0x1A an
unnamed reference ($global - the trick mappings' `$DefaultGrabs`).

Global flags (career.globalflags, 32 words): flag n = bit n & 31 of word n >> 5 - checked on the save's own set flags
(CAREER_STARTED 115, RANK_UP_EXPLAINED 119, MOVIE_UNLOCKED_DEFAULT 139, STAT_UP_ / SWITCH_TO_SKATING_ / TUTORIAL_
EXPLAINED 150 / 151 / 153). Node flags: career.nodeflags {On [...] Off [...]}, the world's switches (barriers...).

## What max sets (all from THP8's data: qb.pak, goal_infos.pak, cas.pak, dbg.pak's names)
- **Goals** (story.goalmanagerparams, 157 - goal_infos.pak's 157): islocked 0, hasseen 1, hasbeaten 1, timeswon >= 1,
  timesstarted >= won + lost; grade_achieved 3 (SICK) on the 128 graded ones - those with AM / PRO / SICK texts in
  story_mode_goal_info, or whose type's goal_rewards leaderboard ranks by grade (classic, comp). 128 = the save's own
  careermissionstotal. Story, unlock, pro challenge and m_chalk_intro goals have no grade (the save had 0 on beaten
  story / unlock goals) and keep 0.
- **Story atoms** (career.story_atom_group, 212 = chapter_info_data_internal's 212, index for index): all `complete`;
  pager messages viewed with a send order, the 18 that game_progress_clear_pager_message atoms clear cleared;
  referenced_goals all `won`. The node flags the not-yet-run atoms' scripts would change are changed (Nail the Trick,
  skate shop door, school / funpark unlock scripts).
- **Node flags**: every zone barrier open - game_progress_set_non_story_mode_nodeflags' list (what free roam sets;
  its game-mode flags left out). Only flags the save already tracks are moved between On and Off (20): the others name
  no node, the game doesn't save them.
- **Global flags** (148, 140 newly set): every `unlock_flag` / `unlocked_flag` of qb.pak and cas.pak (CAS parts,
  brands, skaters, shop tricks), the movie lists' flags, the atoms' unlock_skater / unlock_pro_movie flags, the
  constants SKATER_UNLOCKED_*, CAS_UNLOCK_*, CAS_*_UNLOCKED, MOVIE_UNLOCKED_* / UNLOCKED_MOVIE_*, CHEAT_UNLOCKED_1..20
  (the cheats menu's entries - their cheat_on_ flags stay off), SECRET_UNLOCKED_01, VIEWED_CUTSCENE_*, and the
  completion / seen flags (GAME_COMPLETED_BEGINNER/NORMAL/SICK, CAREER_GOALS_COMPLETE, GOT_ALL_GAPS / GOALS,
  CAREER_SEEN_*_MESSAGE, the *_EXPLAINED tutorial pop-ups). Never: cheat_on_*, CHEAT_FULL_* / ALWAYS / SLOWMO /
  UNLOCK_AND_OWN / UNLOCK_ALL / DEBUG / PERFECT, HAS_ANTILEADERBOARD..., CHEAT_CAS_VANS (all cheats in effect).
- **Gaps**: the 283 gap checklist entries gapcount >= 1 (their start positions stay 0 - the gap menu doesn't use them:
  its choose script is nullscript).
- **Counters**: goalpoints 2393 (each goal's goal_rewards points + 1 per gap + 5 per secret spot - the formula gives
  the user's own 53 exactly), stokens 9999, secret_spots_found 36 (global_secret_spots), prosbeaten 11,
  careermissionscomplete 128 (summary too), storystatscompleted 7, beat_a_goal_sick / firststatchallengecomplete 1,
  training [9 x 1].
- **Shop**: shop_specials / decks / mocap _bought all 1 (30 + 66 + 45).
- **Stats**: storyskater.records' air / lip / Grind / Manual / wallride / specials / spin: total up to the last
  stat_levels_<x> threshold, curr_stat_level 7; custom_story's matching stats 3 + 7 = 10 (the save: spin 6 at level 3).
  classicstats all 10.
- Not touched: options, game records, pros (their is_locked is the definitions' own), CAS appearances, the
  rankings / yourrank, classic-mode level data, levelflags.

`max` refuses to write when the input doesn't round-trip, the output doesn't re-read to the same tree / bytes, a
checksum is wrong, or anything changes outside MAX_CHANGES (max.ts). Running it twice gives the same bytes.

## Done
- Backup first: `C:\Users\kevin\Documents\thps_p8\backup-20261024-233213\415607DD` (the whole folder, untouched since).
- The user's skater-progress replaced with max's output (SHA-256 25BE2176...; the original ACA2640F...). Header and
  thumbnail unchanged.
- NOT checked in the game (agents never launch THP8). Restore: copy the backup's `415607DD` folder back over
  `Documents\thps_p8\B13EBABEBABEBABE\415607DD`.
- [ ] The user to check in game: the save loads (the recomp's log says if it was rejected), Records' completion page
  (SICK / PRO / AM 100%, pro challenges 11/11, secret spots 36/36), the skater select / CAS / movies / cheats / shop,
  the city's zones open, story stats 10.
