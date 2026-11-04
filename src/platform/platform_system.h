#pragma once

#include <windows.h>

#include <cstdint>
#include <span>
#include "base/maths/rect.h"

struct IDirectSound8;

// The Win32 platform layer's system side (0x005F5740-0x005F5AF0): finding the game's CD, the game window (PartyMod's
// SDL window replaces it: platform/sdl_window.h), DirectSound, and two decoders - the sound files' 4-bit ADPCM and
// swizzled texture texels.
namespace os_layer {

// 0x005F5740 (cdecl): the letter of the CD-ROM drive whose volume is called `volume` (0 if none). The letter,
// ':' and '\' are kept (0x007D6A34, 0x007D6A27, 0x007D6A25).
char find_cd_drive(const char* volume);

// 0x005F5860 (cdecl): the game window - its class `class_name` registered (with `window_proc` and the icon `icon`,
// none if 0), titled `title`. Windowed: a captioned window whose inside is width x height, at (x, y) (-1: centred
// on the screen). Full screen: a topmost popup over the whole screen. The cursor hidden. Returns the window (the
// original also kept it at 0x007D6A28 here; its caller keeps it now).
HWND create_game_window(const char* class_name, const char* title, maths::PixelPoint at, maths::PixelSize size,
                        WNDPROC window_proc, HINSTANCE instance, int icon, bool full_screen);

// 0x005F59D0 / 0x005F5A00 (cdecl): DirectSound (priority level, for `window`) made / released. The original kept the
// device in the audio's state itself; WinMain keeps the device init_direct_sound returns there (null if DirectSound
// couldn't be made) and hands it back to release_direct_sound (which takes null).
IDirectSound8* init_direct_sound(HWND window);
void release_direct_sound(IDirectSound8* device);

// 0x005F5A20 (cdecl): `count` 16-bit samples from 4-bit ADPCM (two to a byte, the low nibble first): each nibble
// moves the step index first, then adds +-(step x magnitude / 4 + step / 8), within 16 bits.
void decode_adpcm(std::span<int16_t> out, std::span<const uint8_t> in);

// A texture's width, height and bytes per texel.
struct TexelLayout {
    int width{};
    int height{};
    uint32_t bytes_per_texel_{};
};

// 0x005F5AF0 (cdecl): width x height texels of `bytes_per_texel` from swizzled order (the bits of x and y
// interleaved, x's first) into rows. Returns out.
void unswizzle(std::span<uint8_t> out, std::span<const uint8_t> in, TexelLayout layout);

} // namespace os_layer
