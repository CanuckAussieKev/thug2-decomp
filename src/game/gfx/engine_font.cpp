// The engine's font (CTypeface, the base of the PC font) and the loaded fonts' table: see font.h.

#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/str.h"
#include "game/gfx/font.h"
#include "game/gfx/pc_font.h"
#include "game/lists/key_map.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"

namespace Engine3D {

char g_glyphAliasTable[32];
FontSlot g_fontSlots[16];

namespace {

extern void** const kFontVtable;

FontSlot* Slots() { return g_fontSlots; }
ListKit::KeyMap& FontTable() { return g_fontTable; }
char* GlyphEscapeTable() { return g_glyphAliasTable; }

template <typename R = void, typename... A>
R Virtual(CTypeface* font, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CTypeface*, A...)>(font->m_vtable[slot])(font, args...);
}

} // namespace

// 0x0048D020
CTypeface* CTypeface::MakeNew()
{
    m_vtable = reinterpret_cast<void**>(kFontVtable);
    m_unknown4 = 0;
    return this;
}

// 0x0048D030
void CTypeface::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kFontVtable);
}

// 0x0048D1F0
CTypeface* CTypeface::Teardown(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(kFontVtable);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0048D040
void CTypeface::Offload()
{
    Virtual(this, 5);
    m_unknown4 = 0;
}

// 0x0048D070
void CTypeface::SetGlyphGaps(int /*glyphGap*/, int /*blankGap*/)
{
    DebugOut::Printf("STUB A DUB DUB"); // "STUB A DUB DUB"
}

// 0x0048D080
void CTypeface::PlatSetColors(const uint32_t* /*colors*/)
{
    DebugOut::Printf("STUB A DUB DUB"); // "STUB A DUB DUB"
}

// 0x0048D6A0
char __cdecl ButtonGlyph(const char* digit)
{
    return GlyphEscapeTable()[TextUtil::HexDigitValue(digit)];
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
    uint32_t key = Checksums::HashFromText(name);
    FontTable().EraseEntry(key);
    UnloadPcFont(reinterpret_cast<CPcFont*>(slots[index].font));
    slots[index].font = nullptr;
}

// 0x0048D760
CTypeface* __cdecl GetFont(uint32_t name)
{
    auto* slot = static_cast<FontSlot*>(FontTable().FetchEntry(name));
    return slot ? slot->font : nullptr;
}

// 0x0048D7B0
bool __cdecl CFunc_UnloadFont(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* name = nullptr; // (the original leaves it unset when the name is missing)
    params->FetchText(0u, &name, 0);
    UnloadFont(name);
    return true;
}

namespace {
static VtableSlot kFontVtable_slots[] = {&Engine3D::CTypeface::Teardown, Stubs::ReturnFalse1, &Engine3D::CTypeface::SetGlyphGaps,
                &Engine3D::CTypeface::PlatSetColors, &Engine3D::CTypeface::PlatSetColors, Stubs::DoNothing, Stubs::ReturnZero,
                Stubs::ReturnZero, Stubs::IgnoreArguments3};
void** const kFontVtable = reinterpret_cast<void**>(kFontVtable_slots);
} // namespace

} // namespace Nx
