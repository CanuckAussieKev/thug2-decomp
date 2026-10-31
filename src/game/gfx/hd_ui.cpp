// NOT ORIGINAL - HD menu UI (see hd_ui.h). The switch is [features] hd_ui.

#include "game/gfx/hd_ui.h"

#include "game/sys/log.h"

namespace Nx::HdUi {

namespace {
int s_images = 0;
int s_fonts = 0;
} // namespace

bool Enabled()
{
    static const bool on = ConfigInt("features", "hd_ui", 0) != 0;
    return on;
}

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
