// The engine's sprite and textured quad (see engine_sprite.h).

#include "game/gfx/engine_sprite.h"

#include <bit>
#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kSpriteVtable = 0x00649A80;
constexpr uint32_t kQuadVtable = 0x00649A98;
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
CSprite* CSprite::Construct(void* window)
{
    m_x = -1.0f;
    m_y = -1.0f;
    m_hotX = -1.0f;
    m_hotY = -1.0f;
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    mp_texture = nullptr;
    m_xscale = 1.0f;
    m_yscale = 1.0f;
    m_rotation = 0.0f;
    m_priority = 0.0f;
    m_width = 0;
    m_height = 0;
    std::memset(m_rgba, 0x80, sizeof(m_rgba));
    mp_window = window;
    m_hidden = true;
    m_useDepth = false;
    m_flipV = false;
    m_flipU = false;
    m_blend = kBlend;
    return this;
}

// 0x0049EBA0
void CSprite::DestroyBody() { vtable = reinterpret_cast<void**>(kSpriteVtable); }

// 0x0049EDD0
CSprite* CSprite::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049EBB0
void CSprite::SetHidden(bool hidden)
{
    m_hidden = hidden;
    Notify(this, 2);
}

// 0x0049EBC0
void CSprite::SetTexture(uint8_t* texture)
{
    mp_texture = texture;
    if (texture) {
        m_width = TextureWidth(texture);
        m_height = TextureHeight(texture);
    }
    Notify(this, 3);
}

// 0x0049EC00
void CSprite::SetBlendMode(uint32_t blend)
{
    m_blend = blend;
    Notify(this, 3);
}

// 0x0049EC10
void CSprite::SetPos(float x, float y)
{
    m_x = x;
    m_y = y;
    Notify(this, 3);
}

// 0x0049EC30
void CSprite::SetSize(uint16_t width, uint16_t height)
{
    m_width = width;
    m_height = height;
    Notify(this, 3);
}

// 0x0049EE00 (the products at 53 bits - exact for floats -, the quotients rounded to floats once)
void CSprite::SetScale(float xscale, float yscale)
{
    float width = 1.0f;
    float height = 1.0f;
    if (mp_texture) {
        width = static_cast<float>(TextureWidth(mp_texture));
        height = static_cast<float>(TextureHeight(mp_texture));
    }
    constexpr float kMinWidth = 1.25f;                                // 0x00649AC4
    constexpr float kMinHeight = std::bit_cast<float>(0x3F955326u); // 0x00649AC0: 1.1666
    if (xscale > 0.0f && static_cast<double>(width) * xscale <= kMinWidth)
        xscale = static_cast<float>(static_cast<double>(kMinWidth) / width);
    if (yscale > 0.0f && static_cast<double>(height) * yscale <= kMinHeight)
        yscale = static_cast<float>(static_cast<double>(kMinHeight) / height);
    m_yscale = yscale;
    m_xscale = xscale;
    Notify(this, 3);
}

// 0x0049EC50
void CSprite::SetFlip(bool flipV, bool flipU)
{
    m_flipV = flipV;
    m_flipU = flipU;
    Notify(this, 3);
}

// 0x0049EC70
void CSprite::SetHotSpot(float x, float y)
{
    m_hotX = x;
    m_hotY = y;
    Notify(this, 3);
}

// 0x0049EC90
void CSprite::SetRotation(float rotation)
{
    m_rotation = rotation;
    Notify(this, 3);
}

// 0x0049ECA0
void CSprite::SetPriority(float priority)
{
    m_priority = priority;
    m_useDepth = false;
    Notify(this, 4);
}

// 0x0049ECC0
void CSprite::SetDepth(uint32_t depth)
{
    m_depth = depth;
    m_useDepth = true;
    Notify(this, 4);
}

// 0x0049ECE0
void CSprite::SetZWrite(bool zWrite)
{
    m_zWrite = zWrite;
    Notify(this, 3);
}

// 0x0049ECF0
void CSprite::SetRGBA(uint32_t rgba)
{
    std::memcpy(m_rgba, &rgba, sizeof(m_rgba));
    Notify(this, 3);
}

// --- the textured quad ----------------------------------------------------------------------------------------

// 0x0049EED0
CTexturedQuad* CTexturedQuad::Construct(void* window)
{
    mp_window = window;
    vtable = reinterpret_cast<void**>(kQuadVtable);
    mp_texture = nullptr;
    std::memset(m_rgba, 0x80, sizeof(m_rgba));
    m_hidden = true;
    m_useDepth = false;
    return this;
}

// 0x0049EF00
CTexturedQuad* CTexturedQuad::Destroy(unsigned int freeFlags)
{
    vtable = reinterpret_cast<void**>(kQuadVtable);
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049EF30
CTexturedQuad* CTexturedQuad::Create()
{
    void* memory = Mem::New(sizeof(CTexturedQuad));
    return memory ? static_cast<CTexturedQuad*>(memory)->Construct(nullptr) : nullptr;
}

// 0x0049ED10
void CTexturedQuad::SetHidden(bool hidden)
{
    m_hidden = hidden;
    Notify(this, 4);
}

// 0x0049ED40
void CTexturedQuad::SetTexture(uint8_t* texture)
{
    mp_texture = texture;
    Notify(this, 5);
}

// 0x0049ED00
void CTexturedQuad::SetBlendMode(uint32_t blend)
{
    m_blend = blend;
    Notify(this, 5);
}

// 0x0049ED20
void CTexturedQuad::SetPriority(float priority)
{
    m_priority = priority;
    m_useDepth = false;
    Notify(this, 6);
}

// 0x0049ED50
void CTexturedQuad::SetRGBA(uint32_t rgba)
{
    std::memcpy(m_rgba, &rgba, sizeof(m_rgba));
    Notify(this, 5);
}

} // namespace Nx

REPLACE(0x0049ED60, &Nx::CSprite::Construct);
REPLACE(0x0049EBA0, &Nx::CSprite::DestroyBody);
REPLACE(0x0049EDD0, &Nx::CSprite::Destroy);
REPLACE(0x0049EBB0, &Nx::CSprite::SetHidden);
REPLACE(0x0049EBC0, &Nx::CSprite::SetTexture);
REPLACE(0x0049EC00, &Nx::CSprite::SetBlendMode);
REPLACE(0x0049EC10, &Nx::CSprite::SetPos);
REPLACE(0x0049EC30, &Nx::CSprite::SetSize);
REPLACE(0x0049EE00, &Nx::CSprite::SetScale);
REPLACE(0x0049EC50, &Nx::CSprite::SetFlip);
REPLACE(0x0049EC70, &Nx::CSprite::SetHotSpot);
REPLACE(0x0049EC90, &Nx::CSprite::SetRotation);
REPLACE(0x0049ECA0, &Nx::CSprite::SetPriority);
REPLACE(0x0049ECC0, &Nx::CSprite::SetDepth);
REPLACE(0x0049ECE0, &Nx::CSprite::SetZWrite);
REPLACE(0x0049ECF0, &Nx::CSprite::SetRGBA);
REPLACE(0x0049EED0, &Nx::CTexturedQuad::Construct);
REPLACE(0x0049EF00, &Nx::CTexturedQuad::Destroy);
REPLACE(0x0049EF30, &Nx::CTexturedQuad::Create);
REPLACE(0x0049ED10, &Nx::CTexturedQuad::SetHidden);
REPLACE(0x0049ED40, &Nx::CTexturedQuad::SetTexture);
REPLACE(0x0049ED00, &Nx::CTexturedQuad::SetBlendMode);
REPLACE(0x0049ED20, &Nx::CTexturedQuad::SetPriority);
REPLACE(0x0049ED50, &Nx::CTexturedQuad::SetRGBA);
