// The picture elements (TextureElement, SpriteElement) and the window element: see sprite_element.h.

#include "game/front/sprite_element.h"

#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_sprite.h"
#include "game/gfx/engine_text.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/window2d.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/obj/object.h"
#include "game/core/stubs.h"
#include "game/front/screen_element.h"
#include "game/front/screen_constants.h"
#include "game/gfx/engine_texture.h"

namespace Front {

namespace {
extern void** const kCTextureElementVtable;
extern void** const kCSpriteElementVtable;
extern void** const kCWindowElementVtable;
} // namespace


namespace {

using Script::CStruct;

// Names (checksums).
constexpr uint32_t kTextureElement = 0x118F6383, kSpriteElement = 0xB12B510A, kWindowElement = 0xA13ADBB7;
constexpr uint32_t kTexture = 0x7D99F28D, kRotAngle = 0xED344E1B, kBlend = 0x54628ED7, kNoZWrite = 0x40F139CB;
constexpr uint32_t kRgba = 0x3F6BCDBA;

float Degrees() { return ::CFuncs::g_degreesToRadians; }        // pi / 180
float RotationOffset() { return ::Game::g_goalLifeSecondsPerMillisecond; }  // 0.001 (added to a rotation in radians)
float MinimumAlpha() { return ::Mdl::g_competitionTiny; }    // 0.0001: below it, transparent
float LayoutTop() { return Front::g_layoutTop; }       // -200: a sprite whose top is above it is hidden
float LayoutBottom() { return Front::g_layoutBottom; }    // 648: ... or below it
float One() { return ::Obj::g_animMoviesOne; }
float Half() { return ::Gfx::g_half; }

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The shown colour with its alpha the shown alpha times the summed alpha (none below 0.0001).
uint32_t ShownColour(const CScreenElement* e)
{
    uint32_t rgba;
    std::memcpy(&rgba, e->m_shown.rgba, 4);
    uint8_t alpha = 0;
    if (e->m_summed.alpha >= MinimumAlpha()) // (NaN: transparent)
        alpha = static_cast<uint8_t>(Truncated(static_cast<double>(static_cast<int>(rgba >> 24)) * e->m_summed.alpha));
    return (rgba & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}

// The texture with this checksum (the texture dictionary 0x00700F1C's lookup, 0x0049F540) and its size
// (vtable slots 0x84 / 0x88).
void* FindTexture(uint32_t checksum)
{
    return Nx::g_gameTextureDict->GetTexture(checksum);
}
uint16_t TextureWidth(void* texture) { return reinterpret_cast<uint16_t(__thiscall*)(void*)>(&Nx::VirtualSlot33Jump)(texture); }
uint16_t TextureHeight(void* texture) { return reinterpret_cast<uint16_t(__thiscall*)(void*)>(&Nx::VirtualSlot34Jump)(texture); }

// The drawing pieces: made (cdecl), freed (cdecl), and their setters (thiscall).
void* NewTexturedPiece() { return Nx::CreateTexturedQuad(); }
void* NewSprite(int unused) { return (void)unused, Nx::CreateSprite(); }
void FreePiece(void* piece) { Nx::DeleteModelLights(static_cast<Nx::CPcModelLights*>(piece)); }

// The 2D windows (a free list, 0x004A1620 / 0x004A1670; cdecl) and their position / size (thiscall).
void* NewWindow2d(int x, int y, int w, int h)
{
    return Nx::AllocateWindow2D(x, y, w, h);
}
void FreeWindow2d(void* window) { Nx::FreeWindow2D(static_cast<Nx::CWindow2D*>(window)); }

} // namespace

// --- TextureElement -------------------------------------------------------------------------------------------

// 0x004AED90
CTextureElement* CTextureElement::Construct()
{
    CScreenElement::Construct();
    m_vtable = kCTextureElementVtable;
    m_texture = 0;
    mp_piece = NewTexturedPiece();
    SetType(kTextureElement);
    return this;
}

// 0x004AEDF0
void CTextureElement::DestroyBody()
{
    m_vtable = kCTextureElementVtable;
    if (mp_piece)
        FreePiece(mp_piece);
    CScreenElement::DestroyBody();
}

// 0x004AEE60
CTextureElement* CTextureElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AEE90
void CTextureElement::SetProperties(CStruct* params)
{
    CScreenElement::SetProperties(params);
    uint32_t texture;
    if (params->GetChecksum(kTexture, &texture, 0)) {
        m_texture = texture;
        reinterpret_cast<Nx::CTexturedQuad*>(mp_piece)->SetTexture(static_cast<uint8_t*>(FindTexture(texture)));
        m_objectFlags |= kVisibleNow;
    }
    float angle;
    if (params->GetFloat(kRotAngle, &angle, 0))
        SetRotation(angle, 1);
    uint32_t blend;
    if (params->GetChecksum(kBlend, &blend, 0))
        reinterpret_cast<Nx::CTexturedQuad*>(mp_piece)->SetBlendMode(blend);
}

// 0x004AEFD0
void CTextureElement::GetProperties(CStruct* out)
{
    uint8_t rgba[4];
    std::memcpy(rgba, m_summed.rgba, 4);
    Script::CArray* array = Script::NewArray();
    if (array)
        array = array->Init();
    array->SetSizeAndType(4, Script::ESYMBOLTYPE_INTEGER);
    for (int i = 0; i < 4; ++i)
        array->SetElement(i, rgba[i]);
    out->AddArray(kRgba, array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

// 0x004AEF40
void CTextureElement::Update()
{
    reinterpret_cast<Nx::CTexturedQuad*>(mp_piece)->SetHidden(false);
    reinterpret_cast<Nx::CTexturedQuad*>(mp_piece)->SetPriority(m_zPriority);
    reinterpret_cast<Nx::CTexturedQuad*>(mp_piece)->SetRGBA(ShownColour(this));
}

// --- SpriteElement --------------------------------------------------------------------------------------------

// 0x004AF1A0
CSpriteElement* CSpriteElement::Construct()
{
    CScreenElement::Construct();
    m_vtable = kCSpriteElementVtable;
    m_texture = 0;
    mp_sprite = NewSprite(0);
    m_zWrite = true;
    SetType(kSpriteElement);
    return this;
}

// 0x004AF0D0
void CSpriteElement::DestroyBody()
{
    m_vtable = kCSpriteElementVtable;
    if (mp_sprite)
        FreePiece(mp_sprite);
    CScreenElement::DestroyBody();
}

// 0x004AF210
CSpriteElement* CSpriteElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AF130 (no texture of that name: the original reads through null)
void CSpriteElement::SetTexture(uint32_t texture)
{
    m_texture = texture;
    void* found = FindTexture(texture);
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetTexture(static_cast<uint8_t*>(found));
    float height = static_cast<float>(TextureHeight(found));
    float width = static_cast<float>(TextureWidth(found));
    SetDims(width, height, 0);
    m_objectFlags |= kVisibleNow;
}

// 0x004AF240 (the target compared unrounded, the shown one with the float)
void CSpriteElement::SetSpriteRotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
    float angle = static_cast<float>(radians);
    if (radians != m_target.angle) {
        m_target.angle = angle;
        m_objectFlags |= kDirty;
    }
    if (now && angle != m_shown.angle) {
        reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetRotation(angle);
        m_objectFlags |= kDirty;
        m_shown.angle = angle;
    }
}

// 0x004AF520
void CSpriteElement::SetProperties(CStruct* params)
{
    CScreenElement::SetProperties(params);
    uint32_t texture;
    if (params->GetChecksum(kTexture, &texture, 0))
        SetTexture(texture);
    float angle;
    if (params->GetFloat(kRotAngle, &angle, 0))
        SetSpriteRotation(angle, 1);
    uint32_t blend;
    if (params->GetChecksum(kBlend, &blend, 0))
        reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetBlendMode(blend);
    if (params->ContainsFlag(kNoZWrite))
        m_zWrite = false;
    // (no window above it: the original reads through null)
    auto* window = static_cast<CWindowElement*>(GetWindow());
    reinterpret_cast<Nx::CText*>(mp_sprite)->SetWindow(window->GetWindow2d());
}

// 0x004AF4E0
void CSpriteElement::GetProperties(CStruct* out)
{
    CScreenElement::GetProperties(out);
    out->AddFloat(kRotAngle, m_shown.angle);
    out->AddChecksum(kTexture, m_texture);
}

// 0x004AF5E0
void CSpriteElement::SetMorph(CStruct* params)
{
    CScreenElement::SetMorph(params);
    float degrees;
    if (params->GetFloat(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
        if (radians != m_target.angle) {
            m_target.angle = static_cast<float>(radians);
            m_objectFlags |= kDirty;
        }
    }
}

// 0x004AF2C0
void CSpriteElement::Update()
{
    float top = m_summed.top;
    bool outside = top < LayoutTop() || top > LayoutBottom();
    bool hidden = IsHidden();
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetHidden(outside || hidden);
    if (!(m_objectFlags & (kChildrenDirty | kVisibleNow)))
        return;
    uint32_t rgba = ShownColour(this);
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetPos(m_summed.x, m_summed.y);
    // (the size truncated: the height first)
    int height = Truncated(m_height);
    int width = Truncated(m_width);
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetSize(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetFlip(static_cast<bool>((m_objectFlags >> 25) & 1), static_cast<bool>((m_objectFlags >> 26) & 1));
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetRotation((m_objectFlags & kFlag1000000) ? m_shown.angle : m_summed.angle);
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetHotSpot(m_justX, m_justY);
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetScale(m_summed.scaleX, m_summed.scaleY);
    if (m_objectFlags & kPos3d) {
        // the depth's bits, 5000 more with kKeepScale (an integer added to the float's bits, as the original)
        uint32_t depth;
        std::memcpy(&depth, &m_summed.z, 4);
        depth += (m_objectFlags & kKeepScale) ? 5000u : 0u;
        reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetDepth(depth);
        reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetHidden((m_objectFlags & kOffScreen) != 0 || hidden);
    } else {
        reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetPriority(m_zPriority);
        reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetHidden(outside || hidden);
    }
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetZWrite(m_zWrite);
    reinterpret_cast<Nx::CSprite*>(mp_sprite)->SetRGBA(rgba);
}

// --- WindowElement --------------------------------------------------------------------------------------------

// 0x004B2060
CWindowElement* CWindowElement::Construct()
{
    CScreenElement::Construct();
    m_vtable = kCWindowElementVtable;
    SetType(kWindowElement);
    mp_window = NewWindow2d(0, 0, 640, 448);
    SetJust(-1.0f, -1.0f);
    SetPos(0.0f, 0.0f, 1);
    SetDims(640.0f, 448.0f, 0);
    return this;
}

// 0x004B2100
void CWindowElement::DestroyBody()
{
    m_vtable = kCWindowElementVtable;
    FreeWindow2d(mp_window);
    CScreenElement::DestroyBody();
}

// 0x004B2170
CWindowElement* CWindowElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004B2160
void* CWindowElement::GetWindow2d() const
{
    return mp_window;
}

// 0x004B21A0
void CWindowElement::Update()
{
    if (!(m_objectFlags & (kChildrenDirty | kVisibleNow)))
        return;
    int top = Truncated(m_summed.y - (static_cast<double>(m_justY) + One()) * Half() * m_height);
    int left = Truncated(m_summed.x - (static_cast<double>(m_justX) + One()) * Half() * m_width);
    reinterpret_cast<Nx::CWindow2D*>(mp_window)->SetPos(left, top);
    int height = Truncated(m_height);
    int width = Truncated(m_width);
    reinterpret_cast<Nx::CWindow2D*>(mp_window)->SetSize(width, height);
}

namespace {
GAME_VTABLE(kCTextureElementVtable, 0x0064A1F8, &CTextureElement::Destroy, &CTextureElement::SetProperties,
            static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CObject::CallMemberFunction),
            &CScreenElement::CallMemberFunction, Stubs::IgnoreArgument, &Obj::CObject::PassEventToScript,
            &Obj::CObject::MarkAsDead, Stubs::ReturnTrue, &CTextureElement::GetProperties,
            &CScreenElement::SetMorph, &CTextureElement::Update);
GAME_VTABLE(kCSpriteElementVtable, 0x0064A228, &CSpriteElement::Destroy, &CSpriteElement::SetProperties,
            static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CObject::CallMemberFunction),
            &CScreenElement::CallMemberFunction, Stubs::IgnoreArgument, &Obj::CObject::PassEventToScript,
            &Obj::CObject::MarkAsDead, Stubs::ReturnTrue, &CSpriteElement::GetProperties, &CSpriteElement::SetMorph,
            &CSpriteElement::Update);
GAME_VTABLE(kCWindowElementVtable, 0x0064A2C0, &CWindowElement::Destroy, &CScreenElement::SetProperties,
            static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CObject::CallMemberFunction),
            &CScreenElement::CallMemberFunction, Stubs::IgnoreArgument, &Obj::CObject::PassEventToScript,
            &Obj::CObject::MarkAsDead, Stubs::ReturnTrue, &CScreenElement::GetProperties, &CScreenElement::SetMorph);
} // namespace

} // namespace Front

using Front::CSpriteElement;
using Front::CTextureElement;
using Front::CWindowElement;

REPLACE(0x004AED90, &CTextureElement::Construct);
REPLACE(0x004AEDF0, &CTextureElement::DestroyBody);
REPLACE(0x004AEE60, &CTextureElement::Destroy);
REPLACE(0x004AEE90, &CTextureElement::SetProperties);
REPLACE(0x004AEFD0, &CTextureElement::GetProperties);
REPLACE(0x004AEF40, &CTextureElement::Update);
REPLACE(0x004AF1A0, &CSpriteElement::Construct);
REPLACE(0x004AF0D0, &CSpriteElement::DestroyBody);
REPLACE(0x004AF210, &CSpriteElement::Destroy);
REPLACE(0x004AF130, &CSpriteElement::SetTexture);
REPLACE(0x004AF240, &CSpriteElement::SetSpriteRotation);
REPLACE(0x004AF520, &CSpriteElement::SetProperties);
REPLACE(0x004AF4E0, &CSpriteElement::GetProperties);
REPLACE(0x004AF5E0, &CSpriteElement::SetMorph);
REPLACE(0x004AF2C0, &CSpriteElement::Update);
REPLACE(0x004B2060, &CWindowElement::Construct);
REPLACE(0x004B2100, &CWindowElement::DestroyBody);
REPLACE(0x004B2170, &CWindowElement::Destroy);
REPLACE(0x004B2160, &CWindowElement::GetWindow2d);
REPLACE(0x004B21A0, &CWindowElement::Update);
