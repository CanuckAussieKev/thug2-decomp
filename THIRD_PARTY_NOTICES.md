# Third-party code

This project contains or downloads the following. Their licences apply to those parts; binaries built
from this repo include them, so keep these notices with any distributed `dinput8.dll`.

| Component | Where | Licence |
|---|---|---|
| PartyMod v1.0.4 (PARTYMANX) - https://github.com/PARTYMANX/partymod-thug2 | moved into the game's source (`src/game/`; each file with its code says so; code doing what PartyMod did is marked "Originally from PartyMod Fixes") - PartyMod's window, input, widescreen, settings and fixes | MIT - below |
| SDL2 2.30.9 | downloaded at configure time (FetchContent) | zlib - https://github.com/libsdl-org/SDL/blob/release-2.30.9/LICENSE.txt |
| MinHook 1.3.3 (Tsuda Kageyu) | downloaded at configure time (FetchContent) | BSD 2-clause - https://github.com/TsudaKageyu/minhook/blob/v1.3.3/LICENSE.txt |
| NeverScript (byxor), THUG2 fork by violetvandal - https://github.com/violetvandal/NeverScript, commit 73fe0f9 | copied into `tools/neverscript/` (a tool, not part of the DLL) | no licence; the original author's permission - below |
| go-difflib 1.0.0 (Patrick Mezard) | downloaded by Go when building NeverScript (`tools/build_neverscript.ps1`) | BSD 3-clause - https://github.com/pmezard/go-difflib/blob/v1.0.0/LICENSE |

MinHook includes Hacker Disassembler Engine 32 by Vyacheslav Patkov (BSD 2-clause, same licence file).

## PartyMod v1.0.4 (PARTYMANX) (MIT)

Source: https://github.com/PARTYMANX/partymod-thug2 (v1.0.4)

```
MIT License

Copyright (c) 2024 PARTYMANX

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## NeverScript (byxor; THUG2 fork by violetvandal)

Source: https://github.com/violetvandal/NeverScript (commit 73fe0f9) - violetvandal's THUG2 fork of
https://github.com/byxor/NeverScript by byxor (Brandon Ibbotson) and contributors. Neither has a licence file; the
original's README (kept in the fork) says:

> Feel free to fork the repo and make as many changes/improvements as you like. There's no license, you don't owe
> me a thing.

Thanks to byxor for the compiler and decompiler, and to violetvandal for making them work with THUG2.

## thps-formats (Morten1337) (MIT)

Source: https://github.com/Morten1337/thps-formats (commit e0a6e06), MIT licence (its pyproject.toml; author "MK").
Its experimental RenderWare reader for THPS3 (`thps_formats/experimental/chunky.py`, `enums.py`) was read as
documentation for tools/thps3_levels (the chunk ids, the geometry and atomic sector field order); no code was copied.
Thanks to Morten1337.

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
