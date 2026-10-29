// The engine's text: see engine_text.h.

#include "game/gfx/engine_text.h"

#include <bit>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kTextVtable = 0x00648AA0;

CText*& FreeList() { return GameVariable<CText*>(0x006FA804); }

// The platform's updates (virtual [2]-[5]).
void Update(CText* text, int slot) { reinterpret_cast<void(__thiscall*)(CText*)>(text->m_vtable[slot])(text); }

// Memory from the front end's heap.
void* FrontEndNew(unsigned int size)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_frontEndHeap);
    void* memory = Mem::New(size);
    Mem::Manager::Get()->PopContext();
    return memory;
}

// The effects: an array new of 96 (the count before them; each constructed by the compiler's array helper).
sCharEffect* NewEffects()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_frontEndHeap);
    auto* memory = static_cast<int*>(Mem::New(4 + kCharEffects * sizeof(sCharEffect)));
    sCharEffect* effects = nullptr;
    if (memory) {
        *memory = kCharEffects;
        effects = reinterpret_cast<sCharEffect*>(memory + 1);
        for (int i = 0; i < kCharEffects; ++i)
            effects[i].Construct();
    }
    Mem::Manager::Get()->PopContext();
    return effects;
}
// (their destructor is empty: the compiler's array helper runs 0x005480A0 over them)
void DeleteEffects(sCharEffect* effects) { Mem::Free(reinterpret_cast<int*>(effects) - 1); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 16 bits kept.
int16_t Fixed(float value) { return static_cast<int16_t>(static_cast<int64_t>(static_cast<double>(value) * 16384.0)); }

} // namespace

// 0x0048D0A0
sCharEffect* sCharEffect::Construct()
{
    x = 0;
    y = 0;
    xscale = 0x4000;
    yscale = 0x4000;
    rotation = 0;
    return this;
}

// 0x0048D220
CText* CText::Construct(void* window)
{
    m_xscale = 1.0f;
    m_yscale = 1.0f;
    m_vtable = reinterpret_cast<void**>(kTextVtable);
    mp_font = nullptr;
    mp_effects = nullptr;
    m_x = 0.0f;
    m_y = 0.0f;
    m_pivotX = 0.0f;
    m_pivotY = 0.0f;
    m_angle = 0.0f;
    m_priority = 0.0f;
    m_colour = 0x80808080;
    m_fixedColour = false;
    m_overrideAlpha = false;
    m_hidden = true;
    m_zEnabled = false;
    mp_window = window;
    mp_nextFree = nullptr;
    mp_string = nullptr;
    return this;
}

// 0x0048D620
void CText::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kTextVtable);
    if (mp_string)
        Mem::Free(mp_string); // (not cleared, as the original)
    if (mp_effects) {
        DeleteEffects(mp_effects);
        mp_effects = nullptr;
    }
}

// 0x0048D670
CText* CText::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x0048D5A0
void CText::FreeBuffers()
{
    if (mp_string) {
        Mem::Free(mp_string);
        mp_string = nullptr;
    }
    if (mp_effects) {
        DeleteEffects(mp_effects);
        mp_effects = nullptr;
    }
}

// 0x0048D0C0
void CText::SetHidden(bool hidden)
{
    m_hidden = hidden;
    Update(this, 2);
}

// 0x0048D0D0
void CText::SetFont(CFont* font)
{
    mp_font = font;
    Update(this, 3);
}

// 0x0048D0E0
void CText::SetPos(float x, float y)
{
    m_x = x;
    m_y = y;
    Update(this, 3);
}

// 0x0048D100
void CText::SetScale(float x, float y)
{
    m_xscale = x;
    m_yscale = y;
    Update(this, 3);
}

// 0x0048D120
void CText::SetRotation(float angle, float pivotX, float pivotY)
{
    m_angle = angle;
    m_pivotX = pivotX;
    m_pivotY = pivotY;
    Update(this, 3);
}

// 0x0048D140
void CText::SetPriority(float priority)
{
    m_priority = priority;
    m_zEnabled = false;
    Update(this, 4);
}

// 0x0048D160
void CText::SetDepth(float depth)
{
    m_depth = depth;
    m_zEnabled = true;
    Update(this, 4);
}

// 0x0048D180
void CText::SetColour(uint32_t rgba, bool fixed)
{
    m_colour = rgba;
    m_fixedColour = fixed;
    Update(this, 3);
}

// 0x0048D1A0
void CText::SetOverrideAlpha(bool on)
{
    m_overrideAlpha = on;
    Update(this, 3);
}

// 0x0048D1B0
void CText::SetWindow(void* window)
{
    mp_window = window;
    Update(this, 5);
}

// 0x0048D530
void CText::SetString(const char* string)
{
    if (!mp_string)
        mp_string = static_cast<char*>(FrontEndNew(kTextLength));
    char* out = mp_string;
    while ((*out++ = *string++) != '\0') {
    }
    Update(this, 3);
}

// 0x0048D280
void CText::SetCharOffset(int index, int16_t x, int16_t y)
{
    if (!mp_effects)
        mp_effects = NewEffects();
    mp_effects[index].x = x;
    mp_effects[index].y = y;
    Update(this, 3);
}

// 0x0048D360
void CText::SetCharScale(int index, float x, float y)
{
    if (!mp_effects)
        mp_effects = NewEffects();
    mp_effects[index].xscale = Fixed(x);
    mp_effects[index].yscale = Fixed(y);
    Update(this, 3);
}

// 0x0048D450
void CText::SetCharRotation(int index, float rotation)
{
    if (!mp_effects)
        mp_effects = NewEffects();
    mp_effects[index].rotation = Fixed(rotation);
    Update(this, 3);
}

// 0x0048D1D0
CText* __cdecl NewText()
{
    CText* text = FreeList();
    if (text)
        FreeList() = text->mp_nextFree;
    return text;
}

// 0x0048D5F0
void __cdecl FreeText(CText* text)
{
    text->m_hidden = true;
    Update(text, 2);
    text->FreeBuffers();
    text->mp_nextFree = FreeList();
    FreeList() = text;
}

} // namespace Nx

REPLACE(0x0048D0A0, &Nx::sCharEffect::Construct); // (the array constructor's callback)
REPLACE(0x0048D220, &Nx::CText::Construct);
REPLACE(0x0048D620, &Nx::CText::DestroyBody);
REPLACE(0x0048D670, &Nx::CText::Destroy);
REPLACE(0x0048D5A0, &Nx::CText::FreeBuffers);
REPLACE(0x0048D0C0, &Nx::CText::SetHidden);
REPLACE(0x0048D0D0, &Nx::CText::SetFont);
REPLACE(0x0048D0E0, &Nx::CText::SetPos);
REPLACE(0x0048D100, &Nx::CText::SetScale);
REPLACE(0x0048D120, &Nx::CText::SetRotation);
REPLACE(0x0048D140, &Nx::CText::SetPriority);
REPLACE(0x0048D160, &Nx::CText::SetDepth);
REPLACE(0x0048D180, &Nx::CText::SetColour);
REPLACE(0x0048D1A0, &Nx::CText::SetOverrideAlpha);
REPLACE(0x0048D1B0, &Nx::CText::SetWindow);
REPLACE(0x0048D530, &Nx::CText::SetString);
REPLACE(0x0048D280, &Nx::CText::SetCharOffset);
REPLACE(0x0048D360, &Nx::CText::SetCharScale);
REPLACE(0x0048D450, &Nx::CText::SetCharRotation);
REPLACE(0x0048D1D0, Nx::NewText);
REPLACE(0x0048D5F0, Nx::FreeText);
