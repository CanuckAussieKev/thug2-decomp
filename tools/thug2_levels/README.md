# THUG2's own levels as THUGPRO has them (TP-42)

THUGPRO plays THUG2's own 15 levels with changes of its own: it loads its own node arrays for TR, BO, BA, AU and LA
(and LA's level scripts), its own collision for AP, TR, DJ and SC (and TR's scene), and after loading applies
`<L>_LevelChanges` (trigger scripts replaced - BA's kill zones -, nodes killed, banners) and its host options
(`<L>_LEVEL_CHANGE_0n`). NO, SE and SE2 are THUG2's own. For online play with THUGPRO players both sides must build
the same rails, walls and kill zones; these tools compare, record and (when the user switches entries on) apply them.
Nothing of either game is in the repo: everything is read from the user's installs each time (THUGPRO's own scripts
and geometry by reference: archive entry and CRC-32).

- `node tools/thug2_levels/thugpro_compare.ts [levels...] [--details]` - the differences, level by level, grouped into
  entries with a class: **parity** (online play: rails, collision, kill zones, spawns / crowns / CTF, graffiti
  objects), **visible** (gap triggers, vehicles, loose objects), **single_player**, **cosmetic**, **none**.
  `--extract-thugpro` writes them into `tools/compat/thugpro/thug2_levels.json` (each entry's `enabled` kept).
- `node tools/thug2_levels/thugpro_apply.ts [--enable T2-BO-5,T2-AU-6 | parity | all] [--level BO]` - the entries
  switched on (`enabled` in the register, or `--enable`) into changed level files: `out\thug2_levels` by default, a
  workspace's data overlay with `--install --game C:\Games\THUG2-n` (never the main install without `--allow-main`,
  never the backups or THUGPRO's folders). `make_menu_level.ts` applies the register's switched-on entries to the
  overlay it makes (none by default).

Files: `sources.ts` (where each side's files are), `diff.ts` (the comparison and classification), `register.ts`
(entries), `thugpro_compare.ts`, `thugpro_apply.ts`, `thug2_levels.test.ts`. Background and per-level findings:
`docs/pending/thug2-levels-thugpro.md`; the register row: `tools/compat/thugpro/README.md` TP-42.
