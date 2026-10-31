// The engine's fonts and texts on PC: see pc_font.h.

#include "engine/gfx/pc_font.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/draw2d.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "engine/gfx/text.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"

namespace render {

namespace {

extern void** const kPcFontVtable;
extern void** const kPcTextVtable;

// The button glyph font (0x00780FDC) and the texts' free list (0x006FA804).
TypefaceData*& pad_glyph_typeface() { return g_glyph_typeface; }
PcText*& free_texts() { return reinterpret_cast<PcText*&>(g_free_texts); }

template <typename T>
void call_virtual(T* object, int slot)
{
    reinterpret_cast<void(__thiscall*)(T*)>(object->vtable[slot])(object);
}

OverlayDraw* draw_2d(GlyphRun* text) { return reinterpret_cast<OverlayDraw*>(text); }

} // namespace

// 0x004B3BB0
PcFont* PcFont::make_new()
{
    reinterpret_cast<Typeface*>(this)->Typeface::make_new();
    vtable = reinterpret_cast<void**>(kPcFontVtable);
    font = nullptr;
    return this;
}

// 0x004B3E00
void PcFont::destroy_body()
{
    vtable = reinterpret_cast<void**>(kPcFontVtable);
    if (font) {
        TypefaceData::offload(font);
        font = nullptr;
    }
    reinterpret_cast<Typeface*>(this)->Typeface::destroy_body();
}

// 0x004B4060
PcFont* PcFont::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004B3BD0
bool PcFont::read_in(const char* name)
{
    font = TypefaceData::read_in(name, false);
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
    for (int i = 0; i < 16; ++i)
        font->colors[i] = colors[i];
}

// 0x004B3CB0
void PcFont::flag_button_font(bool buttons)
{
    pad_glyph_typeface() = buttons ? font : nullptr;
}

// 0x004B3CD0
void PcFont::offload()
{
    TypefaceData::offload(font);
    font = nullptr;
}

// 0x004B3CF0
uint32_t PcFont::fetch_tallness()
{
    return font->height;
}

// 0x004B3D00
uint32_t PcFont::get_base()
{
    return font->base;
}

// 0x004B3D10
void PcFont::read_string_param(const char* string, float* width, float* height)
{
    font->read_string_param(string, width, height);
}

// 0x004B3E70
PcText* PcText::make_new()
{
    reinterpret_cast<GlyphString*>(this)->GlyphString::make_new(nullptr);
    vtable = reinterpret_cast<void**>(kPcTextVtable);
    void* memory = memory::alloc(sizeof(GlyphRun));
    text = memory ? static_cast<GlyphRun*>(memory)->make_new(0.0f) : nullptr;
    z = 0;
    call_virtual(this, 3); // tick_traits
    call_virtual(this, 4); // update_priority
    call_virtual(this, 2); // update_hidden
    return this;
}

// 0x004B3D20
void PcText::destroy_body()
{
    vtable = reinterpret_cast<void**>(kPcTextVtable);
    if (text)
        reinterpret_cast<void*(__thiscall*)(GlyphRun*, unsigned int)>(text->vtable[0])(text, 1);
    reinterpret_cast<GlyphString*>(this)->GlyphString::destroy_body();
}

// 0x004B3F20
PcText* PcText::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004B3D80
void PcText::tick_frame()
{
    call_virtual(this, 3); // tick_traits
    call_virtual(this, 4); // update_priority
    call_virtual(this, 2); // update_hidden
}

// 0x004B3DA0
void PcText::update_hidden()
{
    draw_2d(text)->change_concealed(hidden);
}

// 0x004B3F50
void PcText::tick_traits()
{
    text->text = string;
    if (font)
        text->font = font->font;
    text->pivot_x_ = pivot_x_;
    text->pivot_y_ = pivot_y_;
    text->effects = static_cast<const TextEffect*>(effects);
    text->angle = angle;
    text->x = x;
    text->y = y;
    text->xscale = xscale;
    text->yscale = yscale;
    text->color = color;
    text->fixed_color_ = fixed_color_;
}

// 0x004B3DB0
void PcText::update_priority()
{
    draw_2d(text)->change_rank(priority);
    if (z_enabled_)
        draw_2d(text)->set_z(static_cast<float>(static_cast<double>(z) * (1.0 / 65536.0)));
    else
        draw_2d(text)->set_z(0.0f);
}

// 0x004B3FD0
void __cdecl allocate_text_pool(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        void* memory = memory::alloc(sizeof(PcText));
        PcText* text = memory ? static_cast<PcText*>(memory)->make_new() : nullptr;
        // (no check: a failed allocation writes through null, as the original)
        text->next_free_ = free_texts();
        free_texts() = text;
    }
}

// 0x004B4090
void __cdecl unload_pc_font(PcFont* font)
{
    reinterpret_cast<Typeface*>(font)->offload();
}

// 0x004B40A0
PcFont* __cdecl create_pc_font(const char* name)
{
    void* memory = memory::alloc(sizeof(PcFont));
    PcFont* font = memory ? static_cast<PcFont*>(memory)->make_new() : nullptr;
    // (the engine's Load: virtual [1], through 0x004A1090; no check for a failed allocation, as the original)
    reinterpret_cast<bool(__thiscall*)(PcFont*, const char*)>(font->vtable[1])(font, name);
    return font;
}

namespace {
static VtableSlot g_pc_font_vtable_slots[] = {&render::PcFont::teardown, &render::PcFont::read_in, &render::PcFont::change_spacings,
                &render::PcFont::change_colors, &render::PcFont::flag_button_font, &render::PcFont::offload, &render::PcFont::fetch_tallness,
                &render::PcFont::get_base, &render::PcFont::read_string_param};
void** const kPcFontVtable = reinterpret_cast<void**>(g_pc_font_vtable_slots);
static VtableSlot g_pc_text_vtable_slots[] = {&render::PcText::teardown, &render::PcText::tick_frame, &render::PcText::update_hidden,
                &render::PcText::tick_traits, &render::PcText::update_priority, stubs::do_nothing};
void** const kPcTextVtable = reinterpret_cast<void**>(g_pc_text_vtable_slots);
} // namespace

} // namespace Nx
