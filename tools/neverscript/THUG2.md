# NeverScript in this repo

A copy of [byxor/NeverScript](https://github.com/byxor/NeverScript) at commit `77baa23` (the author's README is
`.github/README.md`; the two screenshots are left out). It's a compiler (`.ns` text -> QB bytecode) and decompiler
(QB -> `.ns`) for Neversoft's QB script format. The author's terms: "Feel free to fork the repo and make as many
changes/improvements as you like. There's no license, you don't owe me a thing." (credited in
`THIRD_PARTY_NOTICES.md`). Not part of the game's build: a tool for reading and making `.qb` data files.

Build: `powershell -ExecutionPolicy Bypass -File tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`
(downloads a portable Go if none is installed).

    ns -d levels\mainmenu\mainmenu.qb -o mainmenu.ns                 decompile
    ns -c mainmenu.ns -o mainmenu.qb -targetGame thug2               compile

## What works with THUG2's files (tested 2026-10-17)

- **Node arrays** (`<level>.qb`, e.g. `mainmenu.qb`, 18562 bytes): decompile to readable text (`mainmenu_NodeArray =
  [ { Pos=(...) Angles=(...) Name=wol_inner_ring Class=... } ... ]`) and compile back. Re-decompiling the
  recompiled file gives the same text; the bytes differ (18534 bytes: the original starts with an end-of-line token
  0x16 then the name, NeverScript's output with 0x01; some end-of-line tokens differ). The game's loader only cares
  about the tokens' meaning, but this hasn't been run in the game.
- **The compiler wants `= [` on one line**: the decompiler writes `name =` then `[` on the next line, which the
  compiler rejects. Join them before compiling, e.g. Python `re.sub(r'=\s*\n\s*\[', '= [', text)`.
- **Scripts don't decompile**: THUG2's script files (`*_scripts.qb`, `levels.qb`...) use token 0x47 (THUG2's
  newer `if` with a jump offset - see `src/game/script/qb.h`), and the decompiler stops with "Byte not recognised
  in body of code - 0x47 byte". The compiler's THUG2 target may not write it either (not checked).
