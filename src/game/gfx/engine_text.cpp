// The engine's text: see engine_text.h.

#include "game/gfx/engine_text.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"

namespace Engine3D {

CGlyphString* g_freeTexts;
namespace {

extern void** const kTextVtable;

CGlyphString*& FreeList() { return g_freeTexts; }

// The platform's updates (virtual [2]-[5]).
void TickFrame(CGlyphString* text, int slot) { reinterpret_cast<void(__thiscall*)(CGlyphString*)>(text->m_vtable[slot])(text); }

// Memory from the front end's heap.
void* FrontEndNew(unsigned int size)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_frontEndHeap);
    void* memory = MemSys::Alloc(size);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return memory;
}

// The effects: an array new of 96 (the count before them; each constructed by the compiler's array helper).
sCharEffect* NewEffects()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_frontEndHeap);
    auto* memory = static_cast<int*>(MemSys::Alloc(4 + kCharEffects * sizeof(sCharEffect)));
    sCharEffect* effects = nullptr;
    if (memory) {
        *memory = kCharEffects;
        effects = reinterpret_cast<sCharEffect*>(memory + 1);
        for (int i = 0; i < kCharEffects; ++i)
            effects[i].MakeNew();
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
    return effects;
}
// (their destructor is empty: the compiler's array helper runs 0x005480A0 over them)
void DeleteEffects(sCharEffect* effects) { MemSys::Dealloc(reinterpret_cast<int*>(effects) - 1); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 16 bits kept.
int16_t Fixed(float value) { return static_cast<int16_t>(static_cast<int64_t>(static_cast<double>(value) * 16384.0)); }

} // namespace

// 0x0048D0A0
sCharEffect* sCharEffect::MakeNew()
{
    x = 0;
    y = 0;
    xscale = 0x4000;
    yscale = 0x4000;
    rotation = 0;
    return this;
}

// 0x0048D220
CGlyphString* CGlyphString::MakeNew(void* window)
{
    m_scaleH = 1.0f;
    m_scaleV = 1.0f;
    m_vtable = reinterpret_cast<void**>(kTextVtable);
    m_typeface = nullptr;
    m_effects = nullptr;
    m_posX = 0.0f;
    m_y = 0.0f;
    m_pivotX = 0.0f;
    m_pivotY = 0.0f;
    m_angle = 0.0f;
    m_rank = 0.0f;
    m_shade = 0x80808080;
    m_fixedColour = false;
    m_overrideAlpha = false;
    m_invisible = true;
    m_zEnabled = false;
    m_viewWindow = window;
    m_nextFree = nullptr;
    m_textBuffer = nullptr;
    return this;
}

// 0x0048D620
void CGlyphString::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kTextVtable);
    if (m_textBuffer)
        MemSys::Dealloc(m_textBuffer); // (not cleared, as the original)
    if (m_effects) {
        DeleteEffects(m_effects);
        m_effects = nullptr;
    }
}

// 0x0048D670
CGlyphString* CGlyphString::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x0048D5A0
void CGlyphString::FreeBuffers()
{
    if (m_textBuffer) {
        MemSys::Dealloc(m_textBuffer);
        m_textBuffer = nullptr;
    }
    if (m_effects) {
        DeleteEffects(m_effects);
        m_effects = nullptr;
    }
}

// 0x0048D0C0
void CGlyphString::ChangeConcealed(bool hidden)
{
    m_invisible = hidden;
    TickFrame(this, 2);
}

// 0x0048D0D0
void CGlyphString::ChangeTypeface(CTypeface* font)
{
    m_typeface = font;
    TickFrame(this, 3);
}

// 0x0048D0E0
void CGlyphString::ChangeLocation(float x, float y)
{
    m_posX = x;
    m_y = y;
    TickFrame(this, 3);
}

// 0x0048D100
void CGlyphString::ChangeScale(float x, float y)
{
    m_scaleH = x;
    m_scaleV = y;
    TickFrame(this, 3);
}

// 0x0048D120
void CGlyphString::ChangeSpin(float angle, float pivotX, float pivotY)
{
    m_angle = angle;
    m_pivotX = pivotX;
    m_pivotY = pivotY;
    TickFrame(this, 3);
}

// 0x0048D140
void CGlyphString::ChangeRank(float priority)
{
    m_rank = priority;
    m_zEnabled = false;
    TickFrame(this, 4);
}

// 0x0048D160
void CGlyphString::SetDepth(float depth)
{
    m_layer = depth;
    m_zEnabled = true;
    TickFrame(this, 4);
}

// 0x0048D180
void CGlyphString::ChangeColour(uint32_t rgba, bool fixed)
{
    m_shade = rgba;
    m_fixedColour = fixed;
    TickFrame(this, 3);
}

// 0x0048D1A0
void CGlyphString::SetOverrideAlpha(bool on)
{
    m_overrideAlpha = on;
    TickFrame(this, 3);
}

// 0x0048D1B0
void CGlyphString::ChangePane(void* window)
{
    m_viewWindow = window;
    TickFrame(this, 5);
}

// 0x0048D530
void CGlyphString::ChangeText(const char* string)
{
    if (!m_textBuffer)
        m_textBuffer = static_cast<char*>(FrontEndNew(kTextLength));
    char* out = m_textBuffer;
    while ((*out++ = *string++) != '\0') {
    }
    TickFrame(this, 3);
}

// 0x0048D280
void CGlyphString::SetCharOffset(int index, int16_t x, int16_t y)
{
    if (!m_effects)
        m_effects = NewEffects();
    m_effects[index].x = x;
    m_effects[index].y = y;
    TickFrame(this, 3);
}

// 0x0048D360
void CGlyphString::SetCharScale(int index, float x, float y)
{
    if (!m_effects)
        m_effects = NewEffects();
    m_effects[index].xscale = Fixed(x);
    m_effects[index].yscale = Fixed(y);
    TickFrame(this, 3);
}

// 0x0048D450
void CGlyphString::SetCharRotation(int index, float rotation)
{
    if (!m_effects)
        m_effects = NewEffects();
    m_effects[index].rotation = Fixed(rotation);
    TickFrame(this, 3);
}

// 0x0048D1D0
CGlyphString* __cdecl NewText()
{
    CGlyphString* text = FreeList();
    if (text)
        FreeList() = text->m_nextFree;
    return text;
}

// 0x0048D5F0
void __cdecl FreeText(CGlyphString* text)
{
    text->m_invisible = true;
    TickFrame(text, 2);
    text->FreeBuffers();
    text->m_nextFree = FreeList();
    FreeList() = text;
}

namespace {
static VtableSlot kTextVtable_slots[] = {&Engine3D::CGlyphString::Teardown, Stubs::DoNothing, Stubs::DoNothing, Stubs::DoNothing,
                Stubs::DoNothing, Stubs::DoNothing};
void** const kTextVtable = reinterpret_cast<void**>(kTextVtable_slots);
} // namespace

} // namespace Nx
