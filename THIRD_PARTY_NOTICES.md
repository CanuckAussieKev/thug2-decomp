# Third-party code

This project contains or downloads the following. Their licences apply to those parts; binaries built
from this repo include them, so keep these notices with any distributed `dinput8.dll`.

| Component | Where | Licence |
|---|---|---|
| PartyMod for THUG2 v1.0.4 (PARTYMANX) | moved into the game's source (`src/game/`; each file with its code says so) - the PC port's window, input, widescreen, settings file and fixes | MIT - `LICENSES/partymod.txt` |
| SDL2 2.30.9 | downloaded at configure time (FetchContent) | zlib - https://github.com/libsdl-org/SDL/blob/release-2.30.9/LICENSE.txt |
| MinHook 1.3.3 (Tsuda Kageyu) | downloaded at configure time (FetchContent) | BSD 2-clause - https://github.com/TsudaKageyu/minhook/blob/v1.3.3/LICENSE.txt |

MinHook includes Hacker Disassembler Engine 32 by Vyacheslav Patkov (BSD 2-clause, same licence file).
