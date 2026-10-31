// The engine's sprite and textured quad (see engine_sprite.h).

#include "game/gfx/engine_sprite.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"

namespace Engine3D {

namespace {

extern void** const kSpriteVtable;
extern void** const kQuadVtable;
constexpr uint32_t kBlend = 0x54628ED7; // "blend"

// The platform's slot `slot` (no arguments).
template <typename T>
void Notify(T* object, int slot)
{
    reinterpret_cast<void(__thiscall*)(T*)>(object->vtable[slot])(object);
}

} // namespace

// --- the sprite ---------------------------------------------------------------------------------------------

// 0x0049ED60
CBillboardQuad* CBillboardQuad::MakeNew(void* window)
{
    m_posX = -1.0f;
    m_y = -1.0f;
    m_hotX = -1.0f;
    m_hotY = -1.0f;
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    m_bitmap = nullptr;
    m_scaleH = 1.0f;
    m_scaleV = 1.0f;
    m_orientationTurn = 0.0f;
    m_rank = 0.0f;
    m_span = 0;
    m_levelY = 0;
    std::memset(m_rgbaValue, 0x80, sizeof(m_rgbaValue));
    m_screenRegion = window;
    m_invisible = true;
    m_useDepth = false;
    m_flipV = false;
    m_flipU = false;
    m_mixMode = kBlend;
    return this;
}

// 0x0049EBA0
void CBillboardQuad::DestroyBody() { vtable = reinterpret_cast<void**>(kSpriteVtable); }

// 0x0049EDD0
CBillboardQuad* CBillboardQuad::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x0049EBB0
void CBillboardQuad::ChangeConcealed(bool hidden)
{
    m_invisible = hidden;
    Notify(this, 2);
}

// 0x0049EBC0
void CBillboardQuad::SetTexture(uint8_t* texture)
{
    m_bitmap = texture;
    if (texture) {
        m_span = TexelsAcross(texture);
        m_levelY = TexelsDown(texture);
    }
    Notify(this, 3);
}

// 0x0049EC00
void CBillboardQuad::ChooseBlendMode(uint32_t blend)
{
    m_mixMode = blend;
    Notify(this, 3);
}

// 0x0049EC10
void CBillboardQuad::ChangeLocation(float x, float y)
{
    m_posX = x;
    m_y = y;
    Notify(this, 3);
}

// 0x0049EC30
void CBillboardQuad::ChangeExtent(uint16_t width, uint16_t height)
{
    m_span = width;
    m_levelY = height;
    Notify(this, 3);
}

// 0x0049EE00 (the products at 53 bits - exact for floats -, the quotients rounded to floats once)
void CBillboardQuad::ChangeScale(float xscale, float yscale)
{
    float width = 1.0f;
    float height = 1.0f;
    if (m_bitmap) {
        width = static_cast<float>(TexelsAcross(m_bitmap));
        height = static_cast<float>(TexelsDown(m_bitmap));
    }
    constexpr float kNarrowest = 1.25f;                                // 0x00649AC4
    constexpr float kMinHeight = std::bit_cast<float>(0x3F955326u); // 0x00649AC0: 1.1666
    if (xscale > 0.0f && static_cast<double>(width) * xscale <= kNarrowest)
        xscale = static_cast<float>(static_cast<double>(kNarrowest) / width);
    if (yscale > 0.0f && static_cast<double>(height) * yscale <= kMinHeight)
        yscale = static_cast<float>(static_cast<double>(kMinHeight) / height);
    m_scaleV = yscale;
    m_scaleH = xscale;
    Notify(this, 3);
}

// 0x0049EC50
void CBillboardQuad::SetFlip(bool flipV, bool flipU)
{
    m_flipV = flipV;
    m_flipU = flipU;
    Notify(this, 3);
}

// 0x0049EC70
void CBillboardQuad::SetHotSpot(float x, float y)
{
    m_hotX = x;
    m_hotY = y;
    Notify(this, 3);
}

// 0x0049EC90
void CBillboardQuad::ChangeSpin(float rotation)
{
    m_orientationTurn = rotation;
    Notify(this, 3);
}

// 0x0049ECA0
void CBillboardQuad::ChangeRank(float priority)
{
    m_rank = priority;
    m_useDepth = false;
    Notify(this, 4);
}

// 0x0049ECC0
void CBillboardQuad::SetDepth(uint32_t depth)
{
    m_layer = depth;
    m_useDepth = true;
    Notify(this, 4);
}

// 0x0049ECE0
void CBillboardQuad::SetZWrite(bool zWrite)
{
    m_zWrite = zWrite;
    Notify(this, 3);
}

// 0x0049ECF0
void CBillboardQuad::ChangeRgba(uint32_t rgba)
{
    std::memcpy(m_rgbaValue, &rgba, sizeof(m_rgbaValue));
    Notify(this, 3);
}

// --- the textured quad ----------------------------------------------------------------------------------------

// 0x0049EED0
CTexturedQuad* CTexturedQuad::MakeNew(void* window)
{
    m_screenRegion = window;
    vtable = reinterpret_cast<void**>(kQuadVtable);
    m_bitmap = nullptr;
    std::memset(m_rgbaValue, 0x80, sizeof(m_rgbaValue));
    m_invisible = true;
    m_useDepth = false;
    return this;
}

// 0x0049EF00
CTexturedQuad* CTexturedQuad::Teardown(unsigned int freeFlags)
{
    vtable = reinterpret_cast<void**>(kQuadVtable);
    if (freeFlags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x0049EF30
CTexturedQuad* CTexturedQuad::Fabricate()
{
    void* memory = MemSys::Alloc(sizeof(CTexturedQuad));
    return memory ? static_cast<CTexturedQuad*>(memory)->MakeNew(nullptr) : nullptr;
}

// 0x0049ED10
void CTexturedQuad::ChangeConcealed(bool hidden)
{
    m_invisible = hidden;
    Notify(this, 4);
}

// 0x0049ED40
void CTexturedQuad::SetTexture(uint8_t* texture)
{
    m_bitmap = texture;
    Notify(this, 5);
}

// 0x0049ED00
void CTexturedQuad::ChooseBlendMode(uint32_t blend)
{
    m_mixMode = blend;
    Notify(this, 5);
}

// 0x0049ED20
void CTexturedQuad::ChangeRank(float priority)
{
    m_rank = priority;
    m_useDepth = false;
    Notify(this, 6);
}

// 0x0049ED50
void CTexturedQuad::ChangeRgba(uint32_t rgba)
{
    std::memcpy(m_rgbaValue, &rgba, sizeof(m_rgbaValue));
    Notify(this, 5);
}

namespace {
static VtableSlot kSpriteVtable_slots[] = {&Engine3D::CBillboardQuad::Teardown, Stubs::DoNothing, Stubs::DoNothing, Stubs::DoNothing,
                Stubs::DoNothing, Stubs::DoNothing};
void** const kSpriteVtable = reinterpret_cast<void**>(kSpriteVtable_slots);
static VtableSlot kQuadVtable_slots[] = {&Engine3D::CTexturedQuad::Teardown, &Engine3D::CTexturedQuad::Fabricate, Stubs::DoNothing,
                Stubs::DoNothing, Stubs::DoNothing, Stubs::DoNothing, Stubs::DoNothing, Stubs::DoNothing,
                Stubs::IgnoreArgument, Stubs::IgnoreArgument};
void** const kQuadVtable = reinterpret_cast<void**>(kQuadVtable_slots);
} // namespace

} // namespace Nx
