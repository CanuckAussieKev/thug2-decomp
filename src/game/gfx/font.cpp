// Script-loaded fonts (LoadFont), with PartyMod's button glyph style (game/gfx/glyphs.h) built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include <cstdint>
#include <new>

#include "game/gfx/font.h"

#include "game/core/crc.h"
#include "game/gfx/glyphs.h"
#include "game/gfx/pc_font.h"
#include "game/lst/hashtable.h"
#include "game/script/params.h"
#include "game/script/struct.h"
#include "game/obj/camera_lookaround.h"
#include "game/sys/startup_init.h"

namespace Engine3D {

ListKit::HashTable g_fontTable{ListKit::HashTable::Unconstructed{}}; // (the exe makes it with HashTable(4) at start-up)

namespace {

FontSlot* Slots() { return g_fontSlots; }
ListKit::HashTable& FontTable() { return g_fontTable; }
// The meta button map: for each meta button (0-31) the digit of the button it stands for.
char* MetaButtonMap() { return g_metaButtonMap; }
bool g_metaButtonMapLoaded;

bool& MetaButtonMapLoaded() { return g_metaButtonMapLoaded; }

// The loaded font named by this checksum, or null.
CFont* FindFont(uint32_t checksum) { return GetFont(checksum); }
// Load a font file (fonts\<name>.fnt...).
CFont* LoadFontFile(const char* name) { return reinterpret_cast<CFont*>(CreatePcFont(name)); }

// Engine3D::CFont's setters (each calls the platform font's virtual).
void ChangeSpacings(CFont* font, int glyphGap, int blankGap)
{
    // The original calls 0x004E3F90, a shared tail jump to vtable slot 2 (GameObjects::ReapplyFromData, a naked stub):
    // the slot called here.
    using Slot = void(__thiscall*)(CFont*, int, int);
    (*reinterpret_cast<Slot**>(font))[2](font, glyphGap, blankGap);
}
void SetColorTable(CFont* font, uint32_t* colors)
{
    // (slot 3, through the jump 0x004A10A0)
    reinterpret_cast<void(__thiscall*)(CFont*, uint32_t*)>((*reinterpret_cast<void***>(font))[3])(font, colors);
}
void FlagButtonFont(CFont* font, bool buttonFont)
{
    // (slot 4, through the jump 0x00487D50)
    reinterpret_cast<void(__thiscall*)(CFont*, bool)>((*reinterpret_cast<void***>(font))[4])(font, buttonFont);
}

// Calls to the empty 0x005480A0 (compiled-out debug output).
void DebugFontName(const char* /*name*/) {}
void DebugFontLoaded() {}

} // namespace

// PartyMod's GlyphStyle in place of the original's Xbox names (PartyMod patched the two names into the
// original: the PUSHed string at 0x0048D8F8 and the MOVed one at 0x0048D983).
const char* ButtonFontName()
{
    if (GlyphStyle() == 1)
        return "ButtonsPs2";
    if (GlyphStyle() == 2)
        return "ButtonsNgc";
    return "ButtonsXbox";
}

const char* MetaButtonMapName()
{
    if (GlyphStyle() == 1)
        return "meta_button_map_ps2";
    if (GlyphStyle() == 2)
        return "meta_button_map_gamecube";
    return "meta_button_map_xbox";
}

// 0x0048D7E0
void LoadFont(const char* name, int glyphGap, int blankGap, uint32_t* colors, bool buttonFont)
{
    if (FindFont(Checksums::HashFromText(name))) {
        // Already loaded: just update it (looked up again for each setter, as the original does).
        ChangeSpacings(FindFont(Checksums::HashFromText(name)), glyphGap, blankGap);
        if (colors)
            SetColorTable(FindFont(Checksums::HashFromText(name)), colors);
        FlagButtonFont(FindFont(Checksums::HashFromText(name)), buttonFont);
        return;
    }

    FontSlot* slots = Slots();
    int index = 0;
    while (index < kFontSlots && slots[index].font)
        ++index;
    if (index < kFontSlots) {
        FontSlot& slot = slots[index];
        slot.font = LoadFontFile(name);
        ChangeSpacings(slot.font, glyphGap, blankGap);
        if (colors)
            SetColorTable(slot.font, colors);
        FlagButtonFont(slot.font, buttonFont);
        // The original copies the name with no length limit, like strcpy.
        const char* source = name;
        char* out = slot.name;
        while ((*out++ = *source++) != '\0') {
        }
        uint32_t key = Checksums::HashFromText(name);
        FontTable().StoreEntry(key, slot.name);
    }
    // (With every slot taken nothing is loaded, but the map below still is.)

    if (MetaButtonMapLoaded())
        return;
    Scripts::CArray* map = Scripts::GetArray(MetaButtonMapName());
    if (!map)
        return;
    for (uint32_t i = 0; i < 32; ++i) {
        int digit = i < map->m_size ? static_cast<int>(map->GetElement(i)) : 0;
        MetaButtonMap()[i] = static_cast<char>(digit > 9 ? digit + 'a' - 10 : digit + '0');
    }
    MetaButtonMapLoaded() = true;
}

// 0x0048D950
bool __cdecl CFunc_LoadFont(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    bool buttonFont = params->HasBareFlag("buttons_font");
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->GetString(0u, &name, false);
    if (buttonFont)
        name = ButtonFontName();
    DebugFontName(name);

    float glyphGap = 0.0f;
    params->GetFloat("char_spacing", &glyphGap, false);
    float blankGap = 0.0f;
    params->GetFloat("space_spacing", &blankGap, false);

    // Up to 16 colours, each an array of up to 4 bytes (r g b a: lowest byte first). The original leaves the
    // entries the script doesn't give as stack garbage; here they're 0.
    uint32_t colors[16] = {};
    Scripts::CArray* table;
    if (params->GetArray("color_tab", &table, false)) {
        for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(table->m_size); ++i) {
            if (i >= 16)
                break;
            auto* color = reinterpret_cast<Scripts::CArray*>(table->GetElement(i));
            uint32_t packed = 0;
            for (int j = 0; j < static_cast<int>(color->m_size); ++j)
                packed |= (color->GetElement(j) & 0xFF) << ((8 * j) & 31); // (a 5th byte wraps round, as SHL does)
            colors[i] = packed;
        }
    }

    LoadFont(name, static_cast<int>(glyphGap), static_cast<int>(blankGap), colors, buttonFont);
    DebugFontLoaded();
    return true;
}

} // namespace Nx

namespace Engine3D {

// 0x00640B40 (a static initialiser): the loaded fonts' table, 16 buckets.
void ConstructFontTable() { new (&g_fontTable) ListKit::HashTable(4); }

} // namespace Nx
