// NOT ORIGINAL - HD menu UI (see hd_ui.h). Only called with the CMake option THUG2_HD_UI on.

#include "game/gfx/hd_ui.h"

#include "hook/log.h"

namespace Nx::HdUi {

namespace {
int s_images = 0;
int s_fonts = 0;
} // namespace

void NoteImage(uint32_t scale, uint32_t width, uint32_t height)
{
    ++s_images;
    // (the first one, then every 100th: how many have been loaded)
    if (s_images == 1 || s_images % 100 == 0)
        Log("hd ui: %d upscaled image(s) loaded (this one x%u, %ux%u texels; drawn at %ux%u)", s_images, scale, width,
            height, width / scale, height / scale);
}

void NoteFont(const char* name, uint32_t scale, uint32_t width, uint32_t height)
{
    ++s_fonts;
    Log("hd ui: font %s upscaled x%u (glyph page %ux%u)", name ? name : "(from memory)", scale, width, height);
}

} // namespace Nx::HdUi
