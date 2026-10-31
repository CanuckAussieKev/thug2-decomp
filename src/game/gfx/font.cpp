// Script-loaded fonts (LoadFont), with PartyMod's button glyph style (game/gfx/glyphs.h) built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/font.h"

#include "game/core/crc.h"
#include "game/gfx/glyphs.h"
#include "game/gfx/pc_font.h"
#include "game/lst/hashtable.h"
#include "game/script/params.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/obj/camera_lookaround.h"

namespace Nx {

GAME_GLOBAL(Lst::HashTable, g_fontTable, 0x006FA9F0, Lst::HashTable::Unconstructed{}); // (the exe makes it with HashTable(4) at start-up)

namespace {

FontSlot* Slots() { return g_fontSlots; }
Lst::HashTable& FontTable() { return g_fontTable; }
// The meta button map: for each meta button (0-31) the digit of the button it stands for.
char* MetaButtonMap() { return g_metaButtonMap; }
GAME_GLOBAL(bool, g_metaButtonMapLoaded, 0x006FA828);

bool& MetaButtonMapLoaded() { return g_metaButtonMapLoaded; }

// The loaded font named by this checksum, or null.
CFont* FindFont(uint32_t checksum) { return GetFont(checksum); }
// Load a font file (fonts\<name>.fnt...).
CFont* LoadFontFile(const char* name) { return reinterpret_cast<CFont*>(CreatePcFont(name)); }

// Nx::CFont's setters (each calls the platform font's virtual).
void SetSpacings(CFont* font, int charSpacing, int spaceSpacing)
{
    // The original calls 0x004E3F90, a shared tail jump to vtable slot 2 (Obj::RefreshFromStructure, a naked stub):
    // the slot called here.
    using Slot = void(__thiscall*)(CFont*, int, int);
    (*reinterpret_cast<Slot**>(font))[2](font, charSpacing, spaceSpacing);
}
void SetColorTable(CFont* font, uint32_t* colors)
{
    // (slot 3, through the jump 0x004A10A0)
    reinterpret_cast<void(__thiscall*)(CFont*, uint32_t*)>((*reinterpret_cast<void***>(font))[3])(font, colors);
}
void MarkAsButtonFont(CFont* font, bool buttonFont)
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
void LoadFont(const char* name, int charSpacing, int spaceSpacing, uint32_t* colors, bool buttonFont)
{
    if (FindFont(Crc::GenerateCRCFromString(name))) {
        // Already loaded: just update it (looked up again for each setter, as the original does).
        SetSpacings(FindFont(Crc::GenerateCRCFromString(name)), charSpacing, spaceSpacing);
        if (colors)
            SetColorTable(FindFont(Crc::GenerateCRCFromString(name)), colors);
        MarkAsButtonFont(FindFont(Crc::GenerateCRCFromString(name)), buttonFont);
        return;
    }

    FontSlot* slots = Slots();
    int index = 0;
    while (index < kFontSlots && slots[index].font)
        ++index;
    if (index < kFontSlots) {
        FontSlot& slot = slots[index];
        slot.font = LoadFontFile(name);
        SetSpacings(slot.font, charSpacing, spaceSpacing);
        if (colors)
            SetColorTable(slot.font, colors);
        MarkAsButtonFont(slot.font, buttonFont);
        // The original copies the name with no length limit, like strcpy.
        const char* source = name;
        char* out = slot.name;
        while ((*out++ = *source++) != '\0') {
        }
        uint32_t key = Crc::GenerateCRCFromString(name);
        FontTable().PutItem(key, slot.name);
    }
    // (With every slot taken nothing is loaded, but the map below still is.)

    if (MetaButtonMapLoaded())
        return;
    Script::CArray* map = Script::GetArray(MetaButtonMapName());
    if (!map)
        return;
    for (uint32_t i = 0; i < 32; ++i) {
        int digit = i < map->m_size ? static_cast<int>(map->GetElement(i)) : 0;
        MetaButtonMap()[i] = static_cast<char>(digit > 9 ? digit + 'a' - 10 : digit + '0');
    }
    MetaButtonMapLoaded() = true;
}

// 0x0048D950
bool __cdecl CFunc_LoadFont(Script::CStruct* params, Script::CScript* /*script*/)
{
    bool buttonFont = params->ContainsFlag("buttons_font");
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->GetString(0u, &name, false);
    if (buttonFont)
        name = ButtonFontName();
    DebugFontName(name);

    float charSpacing = 0.0f;
    params->GetFloat("char_spacing", &charSpacing, false);
    float spaceSpacing = 0.0f;
    params->GetFloat("space_spacing", &spaceSpacing, false);

    // Up to 16 colours, each an array of up to 4 bytes (r g b a: lowest byte first). The original leaves the
    // entries the script doesn't give as stack garbage; here they're 0.
    uint32_t colors[16] = {};
    Script::CArray* table;
    if (params->GetArray("color_tab", &table, false)) {
        for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(table->m_size); ++i) {
            if (i >= 16)
                break;
            auto* color = reinterpret_cast<Script::CArray*>(table->GetElement(i));
            uint32_t packed = 0;
            for (int j = 0; j < static_cast<int>(color->m_size); ++j)
                packed |= (color->GetElement(j) & 0xFF) << ((8 * j) & 31); // (a 5th byte wraps round, as SHL does)
            colors[i] = packed;
        }
    }

    LoadFont(name, static_cast<int>(charSpacing), static_cast<int>(spaceSpacing), colors, buttonFont);
    DebugFontLoaded();
    return true;
}

} // namespace Nx

REPLACE(0x0048D7E0, Nx::LoadFont);
REPLACE(0x0048D950, Nx::CFunc_LoadFont);
