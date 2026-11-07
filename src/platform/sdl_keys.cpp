// NOT ORIGINAL - SDL 2's key numbers and names as SDL 3's (sdl_keys.h).

#include "platform/sdl_keys.h"

#include <algorithm>
#include <array>
#include <string>

#include <SDL3/SDL.h>

#include "base/core/str.h"

namespace platform {

namespace {

// SDL 2's scan codes 258-286 (SDL_scancode.h of 2.30) and the same keys in SDL 3 (its README-migration.md; on
// Windows, SDL 2's AudioPlay was the Play/Pause key - SDL 3's MediaPlayPause - and SDL 2 gave Mute as Mute, never
// AudioMute). SDL_SCANCODE_UNKNOWN: SDL 3 has no such key. Every other scan code is the same in both.
struct RenumberedKey {
    int sdl2;
    const char* sdl2_name;
    SDL_Scancode sdl3;
};
constexpr int kFirstRenumbered = 258;
constexpr int kLastRenumbered = 286;
constexpr auto kRenumberedKeys = std::to_array<RenumberedKey>({
    {.sdl2 = 258, .sdl2_name = "AudioNext", .sdl3 = SDL_SCANCODE_MEDIA_NEXT_TRACK},
    {.sdl2 = 259, .sdl2_name = "AudioPrev", .sdl3 = SDL_SCANCODE_MEDIA_PREVIOUS_TRACK},
    {.sdl2 = 260, .sdl2_name = "AudioStop", .sdl3 = SDL_SCANCODE_MEDIA_STOP},
    {.sdl2 = 261, .sdl2_name = "AudioPlay", .sdl3 = SDL_SCANCODE_MEDIA_PLAY_PAUSE},
    {.sdl2 = 262, .sdl2_name = "AudioMute", .sdl3 = SDL_SCANCODE_MUTE},
    {.sdl2 = 263, .sdl2_name = "MediaSelect", .sdl3 = SDL_SCANCODE_MEDIA_SELECT},
    {.sdl2 = 264, .sdl2_name = "WWW", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 265, .sdl2_name = "Mail", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 266, .sdl2_name = "Calculator", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 267, .sdl2_name = "Computer", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 268, .sdl2_name = "AC Search", .sdl3 = SDL_SCANCODE_AC_SEARCH},
    {.sdl2 = 269, .sdl2_name = "AC Home", .sdl3 = SDL_SCANCODE_AC_HOME},
    {.sdl2 = 270, .sdl2_name = "AC Back", .sdl3 = SDL_SCANCODE_AC_BACK},
    {.sdl2 = 271, .sdl2_name = "AC Forward", .sdl3 = SDL_SCANCODE_AC_FORWARD},
    {.sdl2 = 272, .sdl2_name = "AC Stop", .sdl3 = SDL_SCANCODE_AC_STOP},
    {.sdl2 = 273, .sdl2_name = "AC Refresh", .sdl3 = SDL_SCANCODE_AC_REFRESH},
    {.sdl2 = 274, .sdl2_name = "AC Bookmarks", .sdl3 = SDL_SCANCODE_AC_BOOKMARKS},
    {.sdl2 = 275, .sdl2_name = "BrightnessDown", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 276, .sdl2_name = "BrightnessUp", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 277, .sdl2_name = "DisplaySwitch", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 278, .sdl2_name = "KBDIllumToggle", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 279, .sdl2_name = "KBDIllumDown", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 280, .sdl2_name = "KBDIllumUp", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 281, .sdl2_name = "Eject", .sdl3 = SDL_SCANCODE_MEDIA_EJECT},
    {.sdl2 = 282, .sdl2_name = "Sleep", .sdl3 = SDL_SCANCODE_SLEEP},
    {.sdl2 = 283, .sdl2_name = "App1", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 284, .sdl2_name = "App2", .sdl3 = SDL_SCANCODE_UNKNOWN},
    {.sdl2 = 285, .sdl2_name = "AudioRewind", .sdl3 = SDL_SCANCODE_MEDIA_REWIND},
    {.sdl2 = 286, .sdl2_name = "AudioFastForward", .sdl3 = SDL_SCANCODE_MEDIA_FAST_FORWARD},
});
static_assert(kRenumberedKeys.size() == kLastRenumbered - kFirstRenumbered + 1, "every renumbered key, in order");

} // namespace

int scancode_from_sdl2(int code)
{
    if (code < kFirstRenumbered || code > kLastRenumbered) {
        return code;
    }
    const auto found = std::ranges::find(kRenumberedKeys, code, &RenumberedKey::sdl2);
    return found == kRenumberedKeys.end() ? code : static_cast<int>(found->sdl3);
}

int scancode_from_name(const std::string& name)
{
    const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
    if (code != SDL_SCANCODE_UNKNOWN) {
        return static_cast<int>(code);
    }
    // (SDL's own lookup ignores case: so does this)
    const auto found = std::ranges::find_if(kRenumberedKeys, [&name](const RenumberedKey& key) {
        return text::compare_caseless(key.sdl2_name, name.c_str()) == 0;
    });
    return found == kRenumberedKeys.end() ? 0 : static_cast<int>(found->sdl3);
}

} // namespace platform
