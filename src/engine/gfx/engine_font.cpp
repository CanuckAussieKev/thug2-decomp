// The engine's font (Typeface, the base of the PC font) and the loaded fonts' table: see font.h.

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/str.h"
#include "engine/gfx/font.h"
#include "engine/gfx/pc_font.h"
#include "engine/lists/key_map.h"
#include "engine/script/param_struct.h"
#include "engine/core/down_cast.h"
#include "engine/gfx/screen_2d.h"

namespace render {

// 0x0048D040
void Typeface::offload()
{
    unload_platform();
    unknown4_ = 0;
}

// 0x0048D070
void Typeface::change_spacings(int /*glyph_gap*/, int /*blank_gap*/) { debug::printf("STUB A DUB DUB"); }

// 0x0048D080 (slots 3 and 4: one body)
void Typeface::change_colors(const uint32_t* /*colors*/) { debug::printf("STUB A DUB DUB"); }
void Typeface::flag_button_font(bool /*buttons*/) { debug::printf("STUB A DUB DUB"); }

// 0x0048D6A0
char button_glyph(char digit) { return core::at(screen_2d().glyph_alias_table(), text::hex_digit_value(digit)); }

// 0x0048D6C0
void unload_font(const char* name)
{
    auto& slots = screen_2d().font_slots();
    int index = 0;
    while (std::strcmp(core::at(slots, index).name.data(), name) != 0) {
        if (++index >= kFontSlots)
            return;
    }
    const uint32_t key = checksums::hash_from_text(name);
    screen_2d().font_table().erase_entry(key);
    unload_pc_font(core::down_cast<PcFont>(core::at(slots, index).font));
    core::at(slots, index).font = nullptr;
}

// 0x0048D760
Typeface* get_font(uint32_t name)
{
    auto* slot = static_cast<FontSlot*>(screen_2d().font_table().fetch_entry(name));
    return slot ? slot->font : nullptr;
}

// 0x0048D7B0
bool cfunc_unload_font(script::Struct* params, script::Script* /*script*/)
{
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->fetch_text(0u, &name, 0);
    unload_font(name);
    return true;
}

} // namespace render
