// The engine's particle system's own members: see particle_sets.h.

#include <bit>
#include <cstdint>
#include "engine/gfx/particle_sets.h"
#include "engine/script/cscript.h"
#include "engine/core/checked_index.h"

namespace render {

constexpr uint32_t kExponentMask = 0x7F800000U; // a float with all exponent bits set is an infinity or a NaN
constexpr uint32_t kMantissaMask = 0x007FFFFFU;
constexpr uint32_t kQuietNanBit = 0x00400000U; // the x87 turns a signalling NaN into a quiet one

namespace {

// A float passed through the x87 (FLD / FSTP): a signalling NaN comes out quiet.
float through_x87(float value)
{
    auto bits = std::bit_cast<uint32_t>(value);
    if ((bits & kExponentMask) == kExponentMask && ((bits & kMantissaMask) != 0U)) {
        bits |= kQuietNanBit;
    }
    return std::bit_cast<float>(bits);
}

} // namespace

// 0x00494E00
void Sparkle::render(bool advance)
{
    if (!args_.hidden() && !args_.held_back()) {
        draw(advance);
    }
}

// 0x00494E20
void Sparkle::tick_frame()
{
    if (!args_.hidden() && !args_.held_back()) {
        update();
    }
}

// 0x00494E40
void Sparkle::get_release()
{
    if (args_.emit_script() != 0U) {
        script::stop_scripts_by_tag(args_.emit_script());
        args_.set_emit_script(0);
    }
    free_resources();
}

// 0x00494E70
void Sparkle::conceal(bool hide)
{
    if (args_.hidden() == hide) {
        return;
    }
    args_.set_hidden(hide);
    on_visibility_changed(hide);
}

// 0x00494E90
void Sparkle::set_rate(float value) { args_.rate() = value; }

// 0x00494EA0
void Sparkle::set_lifespan(float value) { args_.lifetime() = value; }

// 0x00494EB0
void Sparkle::change_extent(int which, float size) { core::at(args_.extent(), which) = through_x87(size); }

// 0x00494EC0
void Sparkle::change_colour(int which, const Rgba& colour)
{
    core::at(args_.shade(), which) = colour;
    on_colour_changed();
}

// 0x00494EE0
void Sparkle::set_brightness(float value)
{
    args_.set_brightness(value);
    on_colour_changed();
}

// 0x00494EF0
void Sparkle::set_colour_mid_percent(float percent)
{
    args_.set_colour_mid_percent(percent);
    on_colour_changed();
}

} // namespace render
