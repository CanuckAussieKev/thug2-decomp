// The engine's fonts and texts on PC: see pc_font.h.

#include "engine/gfx/pc_font.h"

#include <memory>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/draw2d.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "engine/gfx/text.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace render {

namespace {

// The button glyph font (0x00780FDC) and the texts' free list (0x006FA804).
TypefaceData*& pad_glyph_typeface() { return g_glyph_typeface; }
GlyphString*& free_texts() { return g_free_texts; }

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
    font->glyph_gap_ = static_cast<int16_t>(glyph_gap);
    if (blank_gap > 0)
        font->blank_gap_ = static_cast<int16_t>(blank_gap);
}

// 0x004B3C30
void PcFont::change_colors(const uint32_t* colors)
{
    const std::span<const uint32_t, 16> given(colors, 16);
    for (int i = 0; i < 16; ++i)
        core::at(font->colors, i) = core::at(given, i);
}

// 0x004B3CB0
void PcFont::flag_button_font(bool buttons) { pad_glyph_typeface() = buttons ? font : nullptr; }

// 0x004B3CD0
void PcFont::unload_platform()
{
    TypefaceData::offload(font);
    font = nullptr;
}

// 0x004B3CF0
uint32_t PcFont::fetch_tallness() { return font->height; }

// 0x004B3D00
uint32_t PcFont::get_base() { return font->base; }

// 0x004B3D10
void PcFont::read_string_param(const char* string, float* width, float* height)
{
    font->read_string_param(string, width, height);
}

// 0x004B3E70
PcText::PcText() : GlyphString(nullptr)
{
    text = std::make_unique<GlyphRun>(0.0f);
    layer_ = 0.0f;
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
void PcText::update_hidden() { text->change_concealed(invisible_); }

// 0x004B3F50
void PcText::update_settings()
{
    text->text = text_buffer_;
    if (typeface_)
        text->font = core::down_cast<PcFont>(typeface_)->font;
    text->pivot_x_ = pivot_x_;
    text->pivot_y_ = pivot_y_;
    text->effects = effects_;
    text->angle = angle_;
    text->x = pos_x_;
    text->y = y_;
    text->xscale = scale_h_;
    text->yscale = scale_v_;
    text->color = shade_;
    text->fixed_color_ = fixed_colour_;
}

// 0x004B3DB0 (the depth read as a 16.16 integer, as the original - the engine's set_depth stores a float there)
void PcText::update_priority()
{
    text->change_rank(rank_);
    if (z_enabled_)
        text->set_z(static_cast<float>(static_cast<double>(std::bit_cast<uint32_t>(layer_)) * (1.0 / 65536.0)));
    else
        text->set_z(0.0f);
}

// 0x004B3FD0
void allocate_text_pool(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        auto text = memory::make_owned<PcText>();
        // (no check: a failed allocation writes through null, as the original)
        text->next_free_ = free_texts();
        free_texts() = text;
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
