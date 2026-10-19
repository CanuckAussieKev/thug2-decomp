# NeverScript in this repo

A copy of [violetvandal/NeverScript](https://github.com/violetvandal/NeverScript) at commit `73fe0f9`: a THUG2
fork of [byxor/NeverScript](https://github.com/byxor/NeverScript) (the original author's README is
`.github/README.md`, its two screenshots left out; the fork's own notes are `LIMITATIONS.md`). It's a compiler
(`.ns` text -> QB bytecode) and decompiler (QB -> `.ns`) for Neversoft's QB script format, made by the fork to
handle THUG2's scripts. byxor's terms: "Feel free to fork the repo and make as many changes/improvements as you
like. There's no license, you don't owe me a thing." (both credited in `THIRD_PARTY_NOTICES.md`). Not part of the
game's build: a tool for reading and making `.qb` files.

Build: `powershell -ExecutionPolicy Bypass -File tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`
(downloads a portable Go if none is installed).

    ns -d levels\mainmenu\mainmenu_scripts.qb -o mainmenu_scripts.ns      decompile
    ns -c mainmenu_scripts.ns -o mainmenu_scripts.qb -targetGame thug2    compile

## -lenient (2026-10-19)

`ns -lenient -d file.qb -o file.ns` decompiles malformed QB that other tools let through (RoQ, 2007-2011 mods): a
`(` with no `)` before the line ends, a `[` / `{` still open when the script ends, 0x00 padding before the end of the
file. The text closes what was left open and marks it `/* lenient: ... */`; compiled again, the missing bracket is
added (the bytes differ - it's the fix). Without -lenient nothing changes (every retail script decompiles exactly as
before). The game itself: an unclosed `(` makes the expression fail at run time ("Non-zero parentheses count",
src/game/script/eval.cpp).

## Round trip over the game's files (tested 2026-10-17)

Every `.qb` under `C:\Games\THUG2\Data` (470), decompiled then compiled back:

- **454 byte-identical** - node arrays (`mainmenu.qb`, `LA.qb`...), scripts (`mainmenu_Scripts.qb`, `Levels.qb`,
  the `*_scripts.qb` of the levels), menus, particle data.
- **14 the same text, fewer bytes**: the `goal_*` scripts (cat, competition, countercombo, film, find_gaps, kill,
  team_select, trickspot), `menu/level_textures.qb` and the five `localtext*.qb`. Those originals end lines with
  token 0x02 + a line number; the recompile writes plain 0x01 (12-18% smaller). Decompiling the recompiled file
  gives exactly the text of the original's. The line numbers look like debug information only - not checked in
  the game.
- **2 don't decompile**: `levels\CA\CA_Scripts.qb` and `levels\LA\LA_Scripts.qb` ("Unexpected byte in switch
  body - 0x16").

No recompiled file has been run in our game yet (the fork's author reports an unmodified and an edited
`AU_sfx.qb` working in the game).
