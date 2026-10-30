// The engine's fonts and texts on PC: see pc_font.h.

#include "game/gfx/pc_font.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/engine_text.h"
#include "game/gfx/font.h"
#include "game/gfx/text.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kPcFontVtable = 0x0064A328;
constexpr uint32_t kPcTextVtable = 0x0064A34C;

// The button glyph font (0x00780FDC) and the texts' free list (0x006FA804).
SFont*& ButtonFont() { return g_buttonFont; }
CPcText*& FreeTexts() { return reinterpret_cast<CPcText*&>(g_freeTexts); }

template <typename T>
void CallVirtual(T* object, int slot)
{
    reinterpret_cast<void(__thiscall*)(T*)>(object->vtable[slot])(object);
}

SDraw2D* Draw2D(SText* text) { return reinterpret_cast<SDraw2D*>(text); }

} // namespace

// 0x004B3BB0
CPcFont* CPcFont::Construct()
{
    reinterpret_cast<CFont*>(this)->CFont::Construct();
    vtable = reinterpret_cast<void**>(kPcFontVtable);
    font = nullptr;
    return this;
}

// 0x004B3E00
void CPcFont::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kPcFontVtable);
    if (font) {
        SFont::Unload(font);
        font = nullptr;
    }
    reinterpret_cast<CFont*>(this)->CFont::DestroyBody();
}

// 0x004B4060
CPcFont* CPcFont::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B3BD0
bool CPcFont::Load(const char* name)
{
    font = SFont::Load(name, false);
    return font != nullptr;
}

// 0x004B3C00
void CPcFont::SetSpacings(int charSpacing, int spaceSpacing)
{
    font->charSpacing = static_cast<int16_t>(charSpacing);
    if (spaceSpacing > 0)
        font->spaceSpacing = static_cast<int16_t>(spaceSpacing);
}

// 0x004B3C30
void CPcFont::SetColors(const uint32_t* colors)
{
    for (int i = 0; i < 16; ++i)
        font->colors[i] = colors[i];
}

// 0x004B3CB0
void CPcFont::MarkAsButtonFont(bool buttons)
{
    ButtonFont() = buttons ? font : nullptr;
}

// 0x004B3CD0
void CPcFont::Unload()
{
    SFont::Unload(font);
    font = nullptr;
}

// 0x004B3CF0
uint32_t CPcFont::GetHeight()
{
    return font->height;
}

// 0x004B3D00
uint32_t CPcFont::GetBase()
{
    return font->base;
}

// 0x004B3D10
void CPcFont::QueryString(const char* string, float* width, float* height)
{
    font->QueryString(string, width, height);
}

// 0x004B3E70
CPcText* CPcText::Construct()
{
    reinterpret_cast<CText*>(this)->CText::Construct(nullptr);
    vtable = reinterpret_cast<void**>(kPcTextVtable);
    void* memory = Mem::New(sizeof(SText));
    text = memory ? static_cast<SText*>(memory)->Construct(0.0f) : nullptr;
    z = 0;
    CallVirtual(this, 3); // UpdateProperties
    CallVirtual(this, 4); // UpdatePriority
    CallVirtual(this, 2); // UpdateHidden
    return this;
}

// 0x004B3D20
void CPcText::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kPcTextVtable);
    if (text)
        reinterpret_cast<void*(__thiscall*)(SText*, unsigned int)>(text->vtable[0])(text, 1);
    reinterpret_cast<CText*>(this)->CText::DestroyBody();
}

// 0x004B3F20
CPcText* CPcText::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B3D80
void CPcText::Update()
{
    CallVirtual(this, 3); // UpdateProperties
    CallVirtual(this, 4); // UpdatePriority
    CallVirtual(this, 2); // UpdateHidden
}

// 0x004B3DA0
void CPcText::UpdateHidden()
{
    Draw2D(text)->SetHidden(hidden);
}

// 0x004B3F50
void CPcText::UpdateProperties()
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
    Draw2D(text)->SetPriority(priority);
    if (zEnabled)
        Draw2D(text)->SetZ(static_cast<float>(static_cast<double>(z) * (1.0 / 65536.0)));
    else
        Draw2D(text)->SetZ(0.0f);
}

// 0x004B3FD0
void __cdecl AllocateTextPool(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        void* memory = Mem::New(sizeof(CPcText));
        CPcText* text = memory ? static_cast<CPcText*>(memory)->Construct() : nullptr;
        // (no check: a failed allocation writes through null, as the original)
        text->nextFree = FreeTexts();
        FreeTexts() = text;
    }
}

// 0x004B4090
void __cdecl UnloadPcFont(CPcFont* font)
{
    reinterpret_cast<CFont*>(font)->Unload();
}

// 0x004B40A0
CPcFont* __cdecl CreatePcFont(const char* name)
{
    void* memory = Mem::New(sizeof(CPcFont));
    CPcFont* font = memory ? static_cast<CPcFont*>(memory)->Construct() : nullptr;
    // (the engine's Load: virtual [1], through 0x004A1090; no check for a failed allocation, as the original)
    reinterpret_cast<bool(__thiscall*)(CPcFont*, const char*)>(font->vtable[1])(font, name);
    return font;
}

} // namespace Nx

REPLACE(0x004B3BB0, &Nx::CPcFont::Construct);
REPLACE(0x004B3E00, &Nx::CPcFont::DestroyBody);
REPLACE(0x004B4060, &Nx::CPcFont::Destroy);
REPLACE(0x004B3BD0, &Nx::CPcFont::Load);
REPLACE(0x004B3C00, &Nx::CPcFont::SetSpacings);
REPLACE(0x004B3C30, &Nx::CPcFont::SetColors);
REPLACE(0x004B3CB0, &Nx::CPcFont::MarkAsButtonFont);
REPLACE(0x004B3CD0, &Nx::CPcFont::Unload);
REPLACE(0x004B3CF0, &Nx::CPcFont::GetHeight); // a jump to 0x00469490 with the font
REPLACE(0x004B3D00, &Nx::CPcFont::GetBase); // a jump to 0x005480C0 with the font
REPLACE(0x004B3D10, &Nx::CPcFont::QueryString); // a jump to 0x004CECB0 with the font
REPLACE(0x004B3E70, &Nx::CPcText::Construct);
REPLACE(0x004B3D20, &Nx::CPcText::DestroyBody);
REPLACE(0x004B3F20, &Nx::CPcText::Destroy);
REPLACE(0x004B3D80, &Nx::CPcText::Update);
REPLACE(0x004B3DA0, &Nx::CPcText::UpdateHidden);
REPLACE(0x004B3F50, &Nx::CPcText::UpdateProperties);
REPLACE(0x004B3DB0, &Nx::CPcText::UpdatePriority);
REPLACE(0x004B3FD0, Nx::AllocateTextPool);
REPLACE(0x004B4090, Nx::UnloadPcFont);
REPLACE(0x004B40A0, Nx::CreatePcFont);
REPLACE_SOURCE_ONLY(0x0048D1C0, Nx::AllocateTextPool); // a jump to 0x004B3FD0
