// The engine's font (CFont, the base of the PC font) and the loaded fonts' table: see font.h.

#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/str.h"
#include "game/gfx/font.h"
#include "game/gfx/pc_font.h"
#include "game/lst/hashtable.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kFontVtable = 0x00648A6C;

FontSlot* Slots() { return &GameVariable<FontSlot>(0x006FA830); }
Lst::HashTable& FontTable() { return GameVariable<Lst::HashTable>(0x006FA9F0); }
char* MetaButtonMap() { return &GameVariable<char>(0x006FA808); }

template <typename R = void, typename... A>
R Virtual(CFont* font, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CFont*, A...)>(font->m_vtable[slot])(font, args...);
}

} // namespace

// 0x0048D020
CFont* CFont::Construct()
{
    m_vtable = reinterpret_cast<void**>(kFontVtable);
    m_unknown4 = 0;
    return this;
}

// 0x0048D030
void CFont::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kFontVtable);
}

// 0x0048D1F0
CFont* CFont::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(kFontVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0048D040
void CFont::Unload()
{
    Virtual(this, 5);
    m_unknown4 = 0;
}

// 0x0048D070
void CFont::PlatSetSpacings(int /*charSpacing*/, int /*spaceSpacing*/)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x00648A90)); // "STUB A DUB DUB"
}

// 0x0048D080
void CFont::PlatSetColors(const uint32_t* /*colors*/)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x00648A90)); // "STUB A DUB DUB"
}

// 0x0048D6A0
char __cdecl ButtonGlyph(const char* digit)
{
    return MetaButtonMap()[Str::DehexifyDigit(digit)];
}

// 0x0048D6C0
void __cdecl UnloadFont(const char* name)
{
    FontSlot* slots = Slots();
    int index = 0;
    while (std::strcmp(slots[index].name, name) != 0) {
        if (++index >= kFontSlots)
            return;
    }
    uint32_t key = Crc::GenerateCRCFromString(name);
    FontTable().FlushItem(key);
    UnloadPcFont(reinterpret_cast<CPcFont*>(slots[index].font));
    slots[index].font = nullptr;
}

// 0x0048D760
CFont* __cdecl GetFont(uint32_t name)
{
    auto* slot = static_cast<FontSlot*>(FontTable().GetItem(name));
    return slot ? slot->font : nullptr;
}

// 0x0048D7B0
bool __cdecl CFunc_UnloadFont(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->GetString(0u, &name, 0);
    UnloadFont(name);
    return true;
}

} // namespace Nx

REPLACE(0x0048D020, &Nx::CFont::Construct);
REPLACE(0x0048D030, &Nx::CFont::DestroyBody);
REPLACE(0x0048D1F0, &Nx::CFont::Destroy);
REPLACE(0x0048D040, &Nx::CFont::Unload);
REPLACE(0x0048D070, &Nx::CFont::PlatSetSpacings);
REPLACE(0x0048D080, &Nx::CFont::PlatSetColors);
REPLACE(0x0048D6A0, Nx::ButtonGlyph);
REPLACE(0x0048D6C0, Nx::UnloadFont);
REPLACE(0x0048D760, Nx::GetFont);
REPLACE(0x0048D7B0, Nx::CFunc_UnloadFont);
