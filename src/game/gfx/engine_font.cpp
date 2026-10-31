// The engine's font (CFont, the base of the PC font) and the loaded fonts' table: see font.h.

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/str.h"
#include "game/gfx/font.h"
#include "game/gfx/pc_font.h"
#include "game/lst/hashtable.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"

namespace Nx {

char g_metaButtonMap[32];
FontSlot g_fontSlots[16];

namespace {

extern void** const kFontVtable;

FontSlot* Slots() { return g_fontSlots; }
Lst::HashTable& FontTable() { return g_fontTable; }
char* MetaButtonMap() { return g_metaButtonMap; }

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
    Dbg::Printf("STUB A DUB DUB"); // "STUB A DUB DUB"
}

// 0x0048D080
void CFont::PlatSetColors(const uint32_t* /*colors*/)
{
    Dbg::Printf("STUB A DUB DUB"); // "STUB A DUB DUB"
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

namespace {
static VtableSlot kFontVtable_slots[] = {&Nx::CFont::Destroy, Stubs::ReturnFalse1, &Nx::CFont::PlatSetSpacings,
                &Nx::CFont::PlatSetColors, &Nx::CFont::PlatSetColors, Stubs::DoNothing, Stubs::ReturnZero,
                Stubs::ReturnZero, Stubs::IgnoreArguments3};
void** const kFontVtable = reinterpret_cast<void**>(kFontVtable_slots);
} // namespace

} // namespace Nx
