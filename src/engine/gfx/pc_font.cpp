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

namespace Engine3D {

namespace {

extern void** const kPcFontVtable;
extern void** const kPcTextVtable;

// The button glyph font (0x00780FDC) and the texts' free list (0x006FA804).
STypeface*& PadGlyphTypeface() { return g_glyphTypeface; }
CPcText*& FreeTexts() { return reinterpret_cast<CPcText*&>(g_freeTexts); }

template <typename T>
void CallVirtual(T* object, int slot)
{
    reinterpret_cast<void(__thiscall*)(T*)>(object->vtable[slot])(object);
}

SOverlayDraw* Draw2D(SGlyphRun* text) { return reinterpret_cast<SOverlayDraw*>(text); }

} // namespace

// 0x004B3BB0
CPcFont* CPcFont::MakeNew()
{
    reinterpret_cast<CTypeface*>(this)->CTypeface::MakeNew();
    vtable = reinterpret_cast<void**>(kPcFontVtable);
    font = nullptr;
    return this;
}

// 0x004B3E00
void CPcFont::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kPcFontVtable);
    if (font) {
        STypeface::Offload(font);
        font = nullptr;
    }
    reinterpret_cast<CTypeface*>(this)->CTypeface::DestroyBody();
}

// 0x004B4060
CPcFont* CPcFont::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004B3BD0
bool CPcFont::ReadIn(const char* name)
{
    font = STypeface::ReadIn(name, false);
    return font != nullptr;
}

// 0x004B3C00
void CPcFont::ChangeSpacings(int glyphGap, int blankGap)
{
    font->glyphGap = static_cast<int16_t>(glyphGap);
    if (blankGap > 0)
        font->blankGap = static_cast<int16_t>(blankGap);
}

// 0x004B3C30
void CPcFont::ChangeColors(const uint32_t* colors)
{
    for (int i = 0; i < 16; ++i)
        font->colors[i] = colors[i];
}

// 0x004B3CB0
void CPcFont::FlagButtonFont(bool buttons)
{
    PadGlyphTypeface() = buttons ? font : nullptr;
}

// 0x004B3CD0
void CPcFont::Offload()
{
    STypeface::Offload(font);
    font = nullptr;
}

// 0x004B3CF0
uint32_t CPcFont::FetchTallness()
{
    return font->height;
}

// 0x004B3D00
uint32_t CPcFont::GetBase()
{
    return font->base;
}

// 0x004B3D10
void CPcFont::ReadStringParam(const char* string, float* width, float* height)
{
    font->ReadStringParam(string, width, height);
}

// 0x004B3E70
CPcText* CPcText::MakeNew()
{
    reinterpret_cast<CGlyphString*>(this)->CGlyphString::MakeNew(nullptr);
    vtable = reinterpret_cast<void**>(kPcTextVtable);
    void* memory = MemSys::Alloc(sizeof(SGlyphRun));
    text = memory ? static_cast<SGlyphRun*>(memory)->MakeNew(0.0f) : nullptr;
    z = 0;
    CallVirtual(this, 3); // TickTraits
    CallVirtual(this, 4); // UpdatePriority
    CallVirtual(this, 2); // UpdateHidden
    return this;
}

// 0x004B3D20
void CPcText::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kPcTextVtable);
    if (text)
        reinterpret_cast<void*(__thiscall*)(SGlyphRun*, unsigned int)>(text->vtable[0])(text, 1);
    reinterpret_cast<CGlyphString*>(this)->CGlyphString::DestroyBody();
}

// 0x004B3F20
CPcText* CPcText::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004B3D80
void CPcText::TickFrame()
{
    CallVirtual(this, 3); // TickTraits
    CallVirtual(this, 4); // UpdatePriority
    CallVirtual(this, 2); // UpdateHidden
}

// 0x004B3DA0
void CPcText::UpdateHidden()
{
    Draw2D(text)->ChangeConcealed(hidden);
}

// 0x004B3F50
void CPcText::TickTraits()
{
    text->text = string;
    if (font)
        text->font = font->font;
    text->pivotX = pivotX;
    text->pivotY = pivotY;
    text->effects = static_cast<const STextEffect*>(effects);
    text->angle = angle;
    text->x = x;
    text->y = y;
    text->xscale = xscale;
    text->yscale = yscale;
    text->color = color;
    text->fixedColor = fixedColor;
}

// 0x004B3DB0
void CPcText::UpdatePriority()
{
    Draw2D(text)->ChangeRank(priority);
    if (zEnabled)
        Draw2D(text)->SetZ(static_cast<float>(static_cast<double>(z) * (1.0 / 65536.0)));
    else
        Draw2D(text)->SetZ(0.0f);
}

// 0x004B3FD0
void __cdecl AllocateTextPool(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        void* memory = MemSys::Alloc(sizeof(CPcText));
        CPcText* text = memory ? static_cast<CPcText*>(memory)->MakeNew() : nullptr;
        // (no check: a failed allocation writes through null, as the original)
        text->nextFree = FreeTexts();
        FreeTexts() = text;
    }
}

// 0x004B4090
void __cdecl UnloadPcFont(CPcFont* font)
{
    reinterpret_cast<CTypeface*>(font)->Offload();
}

// 0x004B40A0
CPcFont* __cdecl CreatePcFont(const char* name)
{
    void* memory = MemSys::Alloc(sizeof(CPcFont));
    CPcFont* font = memory ? static_cast<CPcFont*>(memory)->MakeNew() : nullptr;
    // (the engine's Load: virtual [1], through 0x004A1090; no check for a failed allocation, as the original)
    reinterpret_cast<bool(__thiscall*)(CPcFont*, const char*)>(font->vtable[1])(font, name);
    return font;
}

namespace {
static VtableSlot kPcFontVtable_slots[] = {&Engine3D::CPcFont::Teardown, &Engine3D::CPcFont::ReadIn, &Engine3D::CPcFont::ChangeSpacings,
                &Engine3D::CPcFont::ChangeColors, &Engine3D::CPcFont::FlagButtonFont, &Engine3D::CPcFont::Offload, &Engine3D::CPcFont::FetchTallness,
                &Engine3D::CPcFont::GetBase, &Engine3D::CPcFont::ReadStringParam};
void** const kPcFontVtable = reinterpret_cast<void**>(kPcFontVtable_slots);
static VtableSlot kPcTextVtable_slots[] = {&Engine3D::CPcText::Teardown, &Engine3D::CPcText::TickFrame, &Engine3D::CPcText::UpdateHidden,
                &Engine3D::CPcText::TickTraits, &Engine3D::CPcText::UpdatePriority, Stubs::DoNothing};
void** const kPcTextVtable = reinterpret_cast<void**>(kPcTextVtable_slots);
} // namespace

} // namespace Nx
