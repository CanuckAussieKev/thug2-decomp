# HD textures (optional AI upscaling of converted levels)

An optional pass of the level converters (THPS3 so far: `tools/thps3_levels`) that upscales a level's own textures
with [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)'s standalone `realesrgan-ncnn-vulkan` (BSD-3-Clause;
Vulkan, no CUDA or PyTorch). **Off by default** - a convert without `--hd-textures` writes exactly what it wrote
before and never touches the cache. The code is `tools/level_port/hd_textures.ts` (its header has the details),
DXT decode / encode `tools/level_port/dxt.ts`, tests `tools/level_port/hd_textures.test.ts`.

## Setup (once)

    node tools/hd_textures/fetch.ts

downloads the pinned release (realesrgan-ncnn-vulkan-20220424, SHA-256 checked) into `out\hd_textures\` (git-ignored;
nothing of it is committed). Models shipped: `realesrgan-x4plus` (the default) and `realesrgan-x4plus-anime`.

## Use

    node tools/thps3_levels/thps3_levels.ts convert foun can --thug2 C:\Games\THUG2-n --hd-textures 2
    node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2-n

The game reads the levels from the data overlay (`thug2-decomp-data`), so **make_menu_level has to run after every
convert** - converting alone leaves the game on the textures installed before.

| option | |
|---|---|
| `--hd-textures [2\|4]` | turn the pass on, x2 or x4 (default 4) |
| `--hd-max N` | the longer side at most N (default 1024; the scale halves until it fits) |
| `--hd-min N` | textures smaller than N on their longer side left alone (default 16) |
| `--hd-model M` | `realesrgan-x4plus` / `realesrgan-x4plus-anime` |
| `--hd-cache DIR` | the cache (default `%LOCALAPPDATA%\thug2-decomp\hd-textures`) |
| `--hd-cache-clear` | empty the cache first |
| `--hd-no-match` | keep the model's own colours / contrast (default: pulled back to the original's) |
| `--hd-tool EXE`, `--hd-gpu ID` | another upscaler binary, a GPU index (its `-g`) |

What the pass does per texture: colour upscaled on its own (texels under alpha 0 filled from their neighbours first,
so no dark halo); a cut-out's alpha (only 0 / 255) scaled bilinearly and thresholded at 128 - sharp, no grey fringe;
smooth alpha through the model as a grey image; 16 texels of padding (the texture's other edge where it's wrapped,
its own edge where clamped) cut off afterwards, so tiling textures show no seams; the result back-projected so every
scale x scale block averages to the original texel (colours and contrast stay the original's); written in the format
it came in (32-bit, DXT1, DXT5) with a full mip chain.

## Cache

Persistent, outside the repo's tracked files and the game folders: one gzipped BGRA file per texture, keyed by the
SHA-256 of the source texels plus every setting that changes the pixels (pass version, tool release, model, scale used,
padding, wrap u / v, size, format). The convert reports hits and misses:

    HD textures (realesrgan-x4plus, x4, cap 1024): 416 of 416 upscaled (416 from the cache, 0 made)

## Measurements (2026-10-22, Foundry + Canada, workspace THUG2-3)

| | texture data | convert (cold / cached) | game peak working set | GPU dedicated |
|---|---|---|---|---|
| original | 16.4 MB | - | 160-163 MB | 45-52 MB |
| x2 | 65.8 MB | 54 s / - | 193-206 MB | 77-92 MB |
| x4 (cap 1024) | 263.3 MB | 62 s / 9 s | 304-372 MB | 121-194 MB |

Both levels load and play at x4 (no warnings, no errors in the log); time to the level didn't change measurably
(47-58 s from start in every case, run-to-run noise). Cache for both levels at x2 and x4: 105 MB.

Known look issues: the model invents fine detail - brushed / noisy metal can turn into a woven pattern (Canada's
quarter pipes), plain concrete gets grain; a cut-out's isolated single opaque texels (Canada's cloud billboard) show
as specks a little more than in the original.

## Comparing

- `node tools/hd_textures/compare_tex.ts <orig.tex.xbx> <hd.tex.xbx> <out>` - texture pairs on a checkerboard.
- `node tools/hd_textures/compare_shots.ts <out.png> <a.png> <b.png> [...] [--crop x,y,w,h] [--zoom N]` - game
  screenshots of the same views side by side.

## HD menu UI (experiment, off by default)

The menus' 2D images (`.img.xbx` sprites) and bitmap fonts (`.fnt.xbx` glyph pages) upscaled into the data overlay:
`tools/hd_textures/ui.ts` (the same pass: cache, back-projection; `realesrgan-x4plus-anime` by default - flat
colours, outlines and text). Make the files, then switch them on in the ini:

    node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2-n --hd-ui 4    (or 2; without it: no HD files)
    thug2-decomp.ini  [features] hd_ui=1                                          (default 0: the files are never read)

The files go in the overlay's `thug2-decomp-data\hd_ui` folder (same relative names as the normal overlay; the data
overlay consults it first only while hd_ui=1), so one install carries them and the ini alone switches. The DLL always
has the code.

A sprite's size on screen comes from its texture's image size and a glyph's from the font's rectangles, so the
upscaled files carry a marker (`.img.xbx` header +0x14 = 0x4844000s, `.fnt.xbx` glyph page header +8 = "HDUI", +0xC
the scale) and the DLL reports the original's sizes (src/game/gfx/hd_ui.h).

Files: every image and font in mainmenusprites / panelsprites / Levelselect / multiplayersprites / Themes / fonts.prx
and the overlay's own level select signs. Not: the loading screens, Create-A-Graphic pieces, PermTex / Bits, level
textures (`.tex.xbx`, also the front end's THPS4 skate shop). `node tools/hd_textures/ui.ts <game> <out> [--scale N]`
writes them anywhere. Measurements and what was seen: docs/pending/hd-ui.md.
