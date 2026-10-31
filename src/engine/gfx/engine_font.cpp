// The engine's font (Typeface, the base of the PC font) and the loaded fonts' table: see font.h.

#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/str.h"
#include "engine/gfx/font.h"
#include "engine/gfx/pc_font.h"
#include "engine/lists/key_map.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"

namespace render {

char g_glyph_alias_table[32];
FontSlot g_font_slots[16];

namespace {

extern void** const kFontVtable;

FontSlot* get_slots() { return g_font_slots; }
lists::KeyMap& font_table() { return g_font_table; }
char* glyph_escape_table() { return g_glyph_alias_table; }

template <typename R = void, typename... A>
R Virtual(Typeface* font, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(Typeface*, A...)>(font->vtable_[slot])(font, args...);
}

} // namespace

// 0x0048D020
Typeface* Typeface::make_new()
{
    vtable_ = reinterpret_cast<void**>(kFontVtable);
    unknown4_ = 0;
    return this;
}

// 0x0048D030
void Typeface::destroy_body()
{
    vtable_ = reinterpret_cast<void**>(kFontVtable);
}

// 0x0048D1F0
Typeface* Typeface::teardown(unsigned int flags)
{
    vtable_ = reinterpret_cast<void**>(kFontVtable);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0048D040
void Typeface::offload()
{
    Virtual(this, 5);
    unknown4_ = 0;
}

// 0x0048D070
void Typeface::set_glyph_gaps(int /*glyphGap*/, int /*blankGap*/)
{
    debug::printf("STUB A DUB DUB"); // "STUB A DUB DUB"
}

// 0x0048D080
void Typeface::plat_set_colors(const uint32_t* /*colors*/)
{
    debug::printf("STUB A DUB DUB"); // "STUB A DUB DUB"
}

// 0x0048D6A0
char __cdecl button_glyph(const char* digit)
{
    return glyph_escape_table()[text::hex_digit_value(digit)];
}

// 0x0048D6C0
void __cdecl unload_font(const char* name)
{
    FontSlot* slots = get_slots();
    int index = 0;
    while (std::strcmp(slots[index].name, name) != 0) {
        if (++index >= kFontSlots)
            return;
    }
    uint32_t key = checksums::hash_from_text(name);
    font_table().erase_entry(key);
    unload_pc_font(reinterpret_cast<PcFont*>(slots[index].font));
    slots[index].font = nullptr;
}

// 0x0048D760
Typeface* __cdecl get_font(uint32_t name)
{
    auto* slot = static_cast<FontSlot*>(font_table().fetch_entry(name));
    return slot ? slot->font : nullptr;
}

// 0x0048D7B0
bool __cdecl cfunc_unload_font(script::Struct* params, script::Script* /*script*/)
{
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->fetch_text(0u, &name, 0);
    unload_font(name);
    return true;
}

namespace {
static VtableSlot g_font_vtable_slots[] = {&render::Typeface::teardown, stubs::return_false1, &render::Typeface::set_glyph_gaps,
                &render::Typeface::plat_set_colors, &render::Typeface::plat_set_colors, stubs::do_nothing, stubs::return_zero,
                stubs::return_zero, stubs::ignore_arguments3};
void** const kFontVtable = reinterpret_cast<void**>(g_font_vtable_slots);
} // namespace

} // namespace Nx
