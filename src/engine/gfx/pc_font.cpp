// The engine's fonts and texts on PC: see pc_font.h.

#include "engine/gfx/pc_font.h"

#include <memory>
#include <bit>
#include <cstdint>
#include <span>

#include "engine/core/mem.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/text.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/screen_2d.h"
#include <gsl/pointers>

namespace render {

namespace {

// The button glyph font (0x00780FDC) and the texts' free list (0x006FA804).

} // namespace

// 0x004B3E00
PcFont::~PcFont()
{
    if (font) {
        TypefaceData::offload(font);
        font = nullptr;
    }
}

// 0x004B3BD0
bool PcFont::read_in(const char* name)
{
    font = TypefaceData::read_in(name);
    return font != nullptr;
}

// 0x004B3C00
void PcFont::change_spacings(int glyph_gap, int blank_gap)
{
    font->set_glyph_gap(static_cast<int16_t>(glyph_gap));
    if (blank_gap > 0)
        font->set_blank_gap(static_cast<int16_t>(blank_gap));
}

// 0x004B3C30
void PcFont::change_colors(const uint32_t* colors)
{
    const std::span<const uint32_t, 16> given(colors, 16);
    for (int i = 0; i < 16; ++i)
        core::at(font->colors_value(), i) = core::at(given, i);
}

// 0x004B3CB0
void PcFont::flag_button_font(bool buttons) { screen_2d().pad_glyph_typeface() = buttons ? font : nullptr; }

// 0x004B3CD0
void PcFont::unload_platform()
{
    TypefaceData::offload(font);
    font = nullptr;
}

// 0x004B3CF0
uint32_t PcFont::fetch_tallness() { return font->height_value(); }

// 0x004B3D00
uint32_t PcFont::get_base() { return font->base_value(); }

// 0x004B3D10
void PcFont::read_string_param(const char* string, float* width, float* height)
{
    font->read_string_param(string, width, height);
}

// 0x004B3E70
PcText::PcText() : GlyphString(nullptr)
{
    text = std::make_unique<GlyphRun>(0.0f);
    set_layer(0.0f);
    update_settings();
    update_priority();
    update_hidden();
}

// 0x004B3D20
PcText::~PcText() { text.reset(); }

// 0x004B3D80
void PcText::refresh_all()
{
    update_settings();
    update_priority();
    update_hidden();
}

// 0x004B3DA0
void PcText::update_hidden() { text->change_concealed(invisible()); }

// 0x004B3F50
void PcText::update_settings()
{
    text->text = text_buffer();
    if (typeface())
        text->font = core::down_cast<PcFont>(typeface())->font;
    text->pivot_x_ = pivot_x();
    text->pivot_y_ = pivot_y();
    text->effects = effects();
    text->angle = angle();
    text->x = pos_x();
    text->y = y();
    text->xscale = scale_h();
    text->yscale = scale_v();
    text->color = shade();
    text->fixed_color_ = fixed_colour();
}

// 0x004B3DB0 (the depth read as a 16.16 integer, as the original - the engine's set_depth stores a float there)
void PcText::update_priority()
{
    text->change_rank(rank());
    if (z_enabled())
        text->set_z(static_cast<float>(static_cast<double>(std::bit_cast<uint32_t>(layer())) * (1.0 / 65536.0)));
    else
        text->set_z(0.0f);
}

// 0x004B3FD0
void allocate_text_pool(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        auto text = memory::make_owned<PcText>();
        // (no check: a failed allocation writes through null, as the original)
        text->set_next_free(screen_2d().free_texts());
        screen_2d().free_texts() = text;
    }
}

// 0x004B4090
void unload_pc_font(PcFont* font) { font->offload(); }

// 0x004B40A0
gsl::owner<PcFont*> create_pc_font(const char* name)
{
    auto font = memory::make_owned<PcFont>();
    // (the engine's Load, through 0x004A1090; no check for a failed allocation, as the original)
    font->read_in(name);
    return font;
}

} // namespace render
