// The engine's particle system's own members: see particle_sets.h.

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/gfx/particle_sets.h"
#include "engine/script/cscript.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

// A float passed through the x87 (FLD / FSTP): a signalling NaN comes out quiet.
float through_x87(float value)
{
    auto bits = std::bit_cast<uint32_t>(value);
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu))
        bits |= 0x00400000u;
    return std::bit_cast<float>(bits);
}

} // namespace

// 0x00494E00
void Sparkle::render(bool advance)
{
    if (!args_.hidden_ && !args_.held_back_)
        draw(advance);
}

// 0x00494E20
void Sparkle::tick_frame()
{
    if (!args_.hidden_ && !args_.held_back_)
        update();
}

// 0x00494E40
void Sparkle::get_release()
{
    if (args_.emit_script_) {
        script::stop_scripts_by_tag(args_.emit_script_);
        args_.emit_script_ = 0;
    }
    free_resources();
}

// 0x00494E70
void Sparkle::conceal(bool hide)
{
    if (args_.hidden_ == hide)
        return;
    args_.hidden_ = hide;
    on_visibility_changed(hide);
}

// 0x00494E90
void Sparkle::set_rate(float value) { args_.rate_ = value; }

// 0x00494EA0
void Sparkle::set_lifespan(float value) { args_.lifetime_ = value; }

// 0x00494EB0
void Sparkle::change_extent(int which, float size) { core::at(args_.extent_, which) = through_x87(size); }

// 0x00494EC0
void Sparkle::change_colour(int which, const uint32_t* colour)
{
    std::memcpy(core::at(args_.shade_, which).data(), colour, 4);
    on_colour_changed();
}

// 0x00494EE0
void Sparkle::set_brightness(float value)
{
    args_.brightness_ = value;
    on_colour_changed();
}

// 0x00494EF0
void Sparkle::set_colour_mid_percent(float percent)
{
    args_.colour_mid_percent_ = percent;
    on_colour_changed();
}

} // namespace render
