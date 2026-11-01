// Script-loaded fonts (LoadFont), with PartyMod's button glyph style (engine/gfx/glyphs.h) built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/core/str.h"

#include <cstdint>
#include <new>

#include "engine/gfx/font.h"

#include "engine/core/checksums.h"
#include "engine/gfx/glyphs.h"
#include "engine/gfx/pc_font.h"
#include "engine/lists/key_map.h"
#include "engine/script/params.h"
#include "engine/script/param_struct.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/sys/startup_init.h"
#include "engine/core/ints.h"

namespace render {

lists::KeyMap g_font_table{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(4) at start-up)

namespace {

FontSlot* get_slots() { return g_font_slots; }
lists::KeyMap& font_table() { return g_font_table; }
// The meta button map: for each meta button (0-31) the digit of the button it stands for.
char* glyph_escape_table() { return g_glyph_alias_table; }
bool g_meta_button_map_loaded;

bool& meta_button_map_loaded() { return g_meta_button_map_loaded; }

// The loaded font named by this checksum, or null.
Typeface* find_font(uint32_t checksum) { return get_font(checksum); }
// Load a font file (fonts\<name>.fnt...).
Typeface* load_font_file(const char* name) { return create_pc_font(name); }

// render::Typeface's setters (the original reaches the font's virtual functions through shared tail jumps:
// 0x004E3F90, 0x004A10A0, 0x00487D50).
void change_spacings(Typeface* font, int glyph_gap, int blank_gap) { font->change_spacings(glyph_gap, blank_gap); }
void set_color_table(Typeface* font, uint32_t* colors) { font->change_colors(colors); }
void flag_button_font(Typeface* font, bool button_font) { font->flag_button_font(button_font); }

// Calls to the empty 0x005480A0 (compiled-out debug output).
void debug_font_name(const char* /*name*/) {}
void debug_font_loaded() {}

} // namespace

// PartyMod's GlyphStyle in place of the original's Xbox names (PartyMod patched the two names into the
// original: the PUSHed string at 0x0048D8F8 and the MOVed one at 0x0048D983).
const char* button_font_name()
{
    if (glyph_style() == 1)
        return "ButtonsPs2";
    if (glyph_style() == 2)
        return "ButtonsNgc";
    return "ButtonsXbox";
}

const char* meta_button_map_name()
{
    if (glyph_style() == 1)
        return "meta_button_map_ps2";
    if (glyph_style() == 2)
        return "meta_button_map_gamecube";
    return "meta_button_map_xbox";
}

// 0x0048D7E0
void load_font(const char* name, int glyph_gap, int blank_gap, uint32_t* colors, bool button_font)
{
    if (find_font(checksums::hash_from_text(name))) {
        // Already loaded: just update it (looked up again for each setter, as the original does).
        change_spacings(find_font(checksums::hash_from_text(name)), glyph_gap, blank_gap);
        if (colors)
            set_color_table(find_font(checksums::hash_from_text(name)), colors);
        flag_button_font(find_font(checksums::hash_from_text(name)), button_font);
        return;
    }

    FontSlot* slots = get_slots();
    int index = 0;
    while (index < kFontSlots && slots[index].font)
        ++index;
    if (index < kFontSlots) {
        FontSlot& slot = slots[index];
        slot.font = load_font_file(name);
        change_spacings(slot.font, glyph_gap, blank_gap);
        if (colors)
            set_color_table(slot.font, colors);
        flag_button_font(slot.font, button_font);
        // The original copies the name with no length limit, like strcpy.
        text::copy_unbounded(slot.name, name);
        const uint32_t key = checksums::hash_from_text(name);
        font_table().store_entry(key, slot.name);
    }
    // (With every slot taken nothing is loaded, but the map below still is.)

    if (meta_button_map_loaded())
        return;
    script::Array* map = script::fetch_list(meta_button_map_name());
    if (!map)
        return;
    for (uint32_t i = 0; i < 32; ++i) {
        const int digit = i < map->extent_ ? static_cast<int>(map->fetch_part(i)) : 0;
        glyph_escape_table()[i] = static_cast<char>(digit > 9 ? digit + 'a' - 10 : digit + '0');
    }
    meta_button_map_loaded() = true;
}

// 0x0048D950
bool cfunc_load_font(script::Struct* params, script::Script* /*script*/)
{
    const bool button_font = params->has_bare_flag("buttons_font");
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->fetch_text(0u, &name, false);
    if (button_font)
        name = button_font_name();
    debug_font_name(name);

    float glyph_gap = 0.0f;
    params->fetch_float("char_spacing", &glyph_gap, false);
    float blank_gap = 0.0f;
    params->fetch_float("space_spacing", &blank_gap, false);

    // Up to 16 colours, each an array of up to 4 bytes (r g b a: lowest byte first). The original leaves the
    // entries the script doesn't give as stack garbage; here they're 0.
    uint32_t colors[16] = {};
    script::Array* table = nullptr;
    if (params->fetch_list("color_tab", &table, false)) {
        for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(table->extent_); ++i) {
            if (i >= 16)
                break;
            auto* color = table->array_at(i);
            uint32_t packed = 0;
            for (int j = 0; j < ints::as_signed(color->extent_); ++j)
                packed |= (color->fetch_part(j) & 0xFF) << ((8 * j) & 31); // (a 5th byte wraps round, as SHL does)
            colors[i] = packed;
        }
    }

    load_font(name, static_cast<int>(glyph_gap), static_cast<int>(blank_gap), colors, button_font);
    debug_font_loaded();
    return true;
}

} // namespace render

namespace render {

// 0x00640B40 (a static initialiser): the loaded fonts' table, 16 buckets.
void construct_font_table() { new (&g_font_table) lists::KeyMap(4); }

} // namespace render
