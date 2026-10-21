# Third-party code

This project contains or downloads the following. Their licences apply to those parts; binaries built
from this repo include them, so keep these notices with any distributed `dinput8.dll`.

| Component | Where | Licence |
|---|---|---|
| PartyMod v1.0.4 (PARTYMANX) - https://github.com/PARTYMANX/partymod-thug2 | moved into the game's source (`src/game/`; each file with its code says so; code doing what PartyMod did is marked "Originally from PartyMod Fixes") - PartyMod's window, input, widescreen, settings and fixes | MIT - below |
| SDL2 2.30.9 | downloaded at configure time (FetchContent); its SDL2.dll copied into the game folder by the build | zlib - https://github.com/libsdl-org/SDL/blob/release-2.30.9/LICENSE.txt |
| SDL_GameControllerDB, commit 8b2d1a7 (2022-09-06) - https://github.com/mdqinc/SDL_GameControllerDB | gamecontrollerdb.txt, downloaded at configure time and copied into the game folder by the build | zlib - https://github.com/mdqinc/SDL_GameControllerDB/blob/8b2d1a791c8e5004c736e5137699e8e97ae558c5/LICENSE |
| Dear ImGui 1.91.9b (Omar Cornut) - https://github.com/ocornut/imgui | downloaded at configure time (FetchContent); the core, its SDL2 platform backend and Direct3D 9 renderer backend are built into the DLL (the level editor's UI) | MIT - below |
| Inter 4.1 (The Inter Project Authors) - https://github.com/rsms/inter | downloaded at configure time (FetchContent); Inter-Regular and Inter-SemiBold compressed into the DLL (the level editor's UI font) | SIL Open Font License 1.1 - below |
| JetBrains Mono 2.304 (The JetBrains Mono Project Authors) - https://github.com/JetBrains/JetBrainsMono | downloaded at configure time (FetchContent); JetBrainsMono-Regular compressed into the DLL (the level editor's numbers and names) | SIL Open Font License 1.1 - below |
| stb_image_write 1.16 and stb_image 2.27 (Sean Barrett) - https://github.com/nothings/stb, commit 1ee679c | downloaded at configure time (FetchContent); built into the DLL: the screenshot key's PNG writer, and the PNG reader for its self-test | public domain or MIT, at the user's choice - MIT below |
| LZHAM 1.0 (Richard Geldreich, Jr.) - https://github.com/richgel999/lzham_codec, commit d379b1f | downloaded at configure time (FetchContent, SHA-256 checked); built into the DLL: THUGPRO's network compression (method 3, the THUGPRO peer mode) | public domain (dedicated by its author on 2020-09-15; releases before that were MIT) - below |
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

## Dear ImGui 1.91.9b (Omar Cornut) (MIT)

Source: https://github.com/ocornut/imgui (tag v1.91.9b)

```
The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

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

## stb_image_write 1.16 / stb_image 2.27 (Sean Barrett) (MIT)

Source: https://github.com/nothings/stb (commit 1ee679ca2ef753a528db5ba6801e1067b40481b8). The files offer two licences
("choose whichever you prefer"): public domain (Unlicense) or MIT. Under the MIT one:

```
MIT License

Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

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

## Inter 4.1 and JetBrains Mono 2.304 (SIL Open Font License 1.1)

Inter: https://github.com/rsms/inter (release v4.1) - Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter).

JetBrains Mono: https://github.com/JetBrains/JetBrainsMono (release v2.304) - Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono).

The fonts are embedded unmodified (compressed) in the DLL; they are not sold by themselves. Both are licensed under the SIL Open Font License, Version 1.1:

```
-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded, 
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

## LZHAM 1.0 (Richard Geldreich, Jr.) (public domain)

Source: https://github.com/richgel999/lzham_codec (commit d379b1f9121e2197881c61cfc4713c78848bdfe7, LZHAM_DLL_VERSION
0x1010). Its LICENSE: "THIS SOFTWARE IS IN THE PUBLIC DOMAIN ... EXPLICITLY AND OVERTLY RELEASED AND CONTRIBUTED TO THE
PUBLIC DOMAIN, PERMANENTLY, IRREVOCABLY AND UNCONDITIONALLY WAIVING ANY AND ALL CLAIM OF COPYRIGHT, IN PERPETUITY ON
SEPTEMBER 15, 2020", with its fallback clauses ("MAY BE FREELY USED, DERIVED FROM, EXECUTED, LINKED WITH, MODIFIED AND
DISTRIBUTED FOR ANY PURPOSE ... WITH NO ATTRIBUTION") and no warranty ("THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
WARRANTY OF ANY KIND"). Earlier releases (the copy THUGPRO builds in) were under the MIT licence; the codec and its
stream format are the same.
