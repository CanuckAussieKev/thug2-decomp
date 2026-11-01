// The engine's text: see engine_text.h.

#include "engine/gfx/engine_text.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/owner.h"

namespace render {

GlyphString* g_free_texts;
namespace {

GlyphString*& free_list() { return g_free_texts; }

// Memory from the front end's heap.
void* front_end_new(unsigned int size)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->front_end_heap_);
    void* memory = memory::alloc(size);
    memory::Hub::get_pull()->leave_heap_scope();
    return memory;
}

// The effects: an array new of 96 (the count before them; each constructed by the compiler's array helper).
CharEffect* new_effects()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->front_end_heap_);
    auto* memory = static_cast<int*>(memory::alloc(4 + kCharEffects * sizeof(CharEffect)));
    CharEffect* effects = nullptr;
    if (memory) {
        *memory = kCharEffects;
        effects = reinterpret_cast<CharEffect*>(memory + 1);
        for (int i = 0; i < kCharEffects; ++i)
            effects[i].make_new();
    }
    memory::Hub::get_pull()->leave_heap_scope();
    return effects;
}
// (their destructor is empty: the compiler's array helper runs 0x005480A0 over them)
void delete_effects(CharEffect* effects) { memory::dealloc(reinterpret_cast<int*>(effects) - 1); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 16 bits kept.
int16_t get_fixed(float value)
{
    return static_cast<int16_t>(static_cast<int64_t>(static_cast<double>(value) * 16384.0));
}

} // namespace

// 0x0048D0A0
CharEffect* CharEffect::make_new()
{
    x = 0;
    y = 0;
    xscale = 0x4000;
    yscale = 0x4000;
    rotation = 0;
    return this;
}

// 0x0048D220
GlyphString::GlyphString(void* window)
{
    scale_h_ = 1.0f;
    scale_v_ = 1.0f;
    typeface_ = nullptr;
    effects_ = nullptr;
    pos_x_ = 0.0f;
    y_ = 0.0f;
    pivot_x_ = 0.0f;
    pivot_y_ = 0.0f;
    angle_ = 0.0f;
    rank_ = 0.0f;
    shade_ = 0x80808080;
    fixed_colour_ = false;
    override_alpha_ = false;
    invisible_ = true;
    z_enabled_ = false;
    screen_region_ = window;
    next_free_ = nullptr;
    text_buffer_ = nullptr;
}

// 0x0048D620
GlyphString::~GlyphString()
{
    if (text_buffer_)
        memory::dealloc(text_buffer_); // (not cleared, as the original)
    if (effects_) {
        delete_effects(effects_);
        effects_ = nullptr;
    }
}

// 0x0048D5A0
void GlyphString::free_buffers()
{
    if (text_buffer_) {
        memory::dealloc(text_buffer_);
        text_buffer_ = nullptr;
    }
    if (effects_) {
        delete_effects(effects_);
        effects_ = nullptr;
    }
}

// 0x0048D0C0
void GlyphString::change_concealed(bool hidden)
{
    invisible_ = hidden;
    update_hidden();
}

// 0x0048D0D0
void GlyphString::change_typeface(Typeface* font)
{
    typeface_ = font;
    update_settings();
}

// 0x0048D0E0
void GlyphString::change_location(float x, float y)
{
    pos_x_ = x;
    y_ = y;
    update_settings();
}

// 0x0048D100
void GlyphString::change_scale(float x, float y)
{
    scale_h_ = x;
    scale_v_ = y;
    update_settings();
}

// 0x0048D120
void GlyphString::change_spin(float angle, float pivot_x, float pivot_y)
{
    angle_ = angle;
    pivot_x_ = pivot_x;
    pivot_y_ = pivot_y;
    update_settings();
}

// 0x0048D140
void GlyphString::change_rank(float priority)
{
    rank_ = priority;
    z_enabled_ = false;
    update_priority();
}

// 0x0048D160
void GlyphString::set_depth(float depth)
{
    layer_ = depth;
    z_enabled_ = true;
    update_priority();
}

// 0x0048D180
void GlyphString::change_colour(uint32_t rgba, bool fixed)
{
    shade_ = rgba;
    fixed_colour_ = fixed;
    update_settings();
}

// 0x0048D1A0
void GlyphString::set_override_alpha(bool on)
{
    override_alpha_ = on;
    update_settings();
}

// 0x0048D1B0
void GlyphString::change_pane(void* window)
{
    screen_region_ = window;
    update_window();
}

// 0x0048D530
void GlyphString::change_text(const char* string)
{
    if (!text_buffer_)
        text_buffer_ = static_cast<char*>(front_end_new(kTextLength));
    char* out = text_buffer_;
    while ((*out++ = *string++) != '\0') {
    }
    update_settings();
}

// 0x0048D280
void GlyphString::set_char_offset(int index, int16_t x, int16_t y)
{
    if (!effects_)
        effects_ = new_effects();
    effects_[index].x = x;
    effects_[index].y = y;
    update_settings();
}

// 0x0048D360
void GlyphString::set_char_scale(int index, float x, float y)
{
    if (!effects_)
        effects_ = new_effects();
    effects_[index].xscale = get_fixed(x);
    effects_[index].yscale = get_fixed(y);
    update_settings();
}

// 0x0048D450
void GlyphString::set_char_rotation(int index, float rotation)
{
    if (!effects_)
        effects_ = new_effects();
    effects_[index].rotation = get_fixed(rotation);
    update_settings();
}

// 0x0048D1D0
memory::owner<GlyphString*> __cdecl new_text()
{
    GlyphString* text = free_list();
    if (text)
        free_list() = text->next_free_;
    return text;
}

// 0x0048D5F0
void __cdecl free_text(GlyphString* text)
{
    text->invisible_ = true;
    text->update_hidden();
    text->free_buffers();
    text->next_free_ = free_list();
    free_list() = text;
}

} // namespace render
