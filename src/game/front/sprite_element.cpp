// The picture elements (TextureElement, SpriteElement) and the window element: see sprite_element.h.

#include "game/front/sprite_element.h"

#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_sprite.h"
#include "game/gfx/engine_text.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/window2d.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/obj/entity.h"
#include "game/core/stubs.h"
#include "game/front/screen_element.h"
#include "game/front/screen_constants.h"
#include "game/gfx/engine_texture.h"

namespace MenuSystem {

namespace {
extern void** const kCTextureElementVtable;
extern void** const kCSpriteElementVtable;
extern void** const kCWindowElementVtable;
} // namespace

namespace {

using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kTextureElement = 0x118F6383, kSpriteElement = 0xB12B510A, kWindowElement = 0xA13ADBB7;
constexpr uint32_t kTexture = 0x7D99F28D, kRotAngle = 0xED344E1B, kBlend = 0x54628ED7, kNoZWrite = 0x40F139CB;
constexpr uint32_t kRgba = 0x3F6BCDBA;

float Degrees() { return ::ScriptCommands::g_degreeToRadianRatio; }        // pi / 180
float RotationOffset() { return ::GoalSystem::g_goalLifeSecondsPerMillisecond; }  // 0.001 (added to a rotation in radians)
float MinimumAlpha() { return ::GameModules::g_competitionTiny; }    // 0.0001: below it, transparent
float LayoutTop() { return MenuSystem::g_layoutTop; }       // -200: a sprite whose top is above it is hidden
float LayoutBottom() { return MenuSystem::g_layoutBottom; }    // 648: ... or below it
float One() { return ::GameObjects::g_animMoviesOne; }
float Half() { return ::Gfx2D::g_half; }

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The shown colour with its alpha the shown alpha times the summed alpha (none below 0.0001).
uint32_t ShownColour(const CUiWidget* e)
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
    return Engine3D::g_gameTextureDict->FetchSkin(checksum);
}
uint16_t TexelsAcross(void* texture) { return reinterpret_cast<uint16_t(__thiscall*)(void*)>(&Engine3D::VirtualSlot33Jump)(texture); }
uint16_t TexelsDown(void* texture) { return reinterpret_cast<uint16_t(__thiscall*)(void*)>(&Engine3D::VirtualSlot34Jump)(texture); }

// The drawing pieces: made (cdecl), freed (cdecl), and their setters (thiscall).
void* NewTexturedPiece() { return Engine3D::CreateTexturedQuad(); }
void* NewSprite(int unused) { return (void)unused, Engine3D::BuildSpriteQuad(); }
void FreePiece(void* piece) { Engine3D::DeleteModelLights(static_cast<Engine3D::CPcModelLights*>(piece)); }

// The 2D windows (a free list, 0x004A1620 / 0x004A1670; cdecl) and their position / size (thiscall).
void* NewWindow2d(int x, int y, int w, int h)
{
    return Engine3D::AllocateWindow2D(x, y, w, h);
}
void FreeWindow2d(void* window) { Engine3D::FreeWindow2D(static_cast<Engine3D::CScreenWindow*>(window)); }

} // namespace

// --- TextureElement -------------------------------------------------------------------------------------------

// 0x004AED90
CTextureElement* CTextureElement::MakeNew()
{
    CUiWidget::MakeNew();
    m_vtable = kCTextureElementVtable;
    m_texture = 0;
    m_piece = NewTexturedPiece();
    ChangeSpecies(kTextureElement);
    return this;
}

// 0x004AEDF0
void CTextureElement::DestroyBody()
{
    m_vtable = kCTextureElementVtable;
    if (m_piece)
        FreePiece(m_piece);
    CUiWidget::DestroyBody();
}

// 0x004AEE60
CTextureElement* CTextureElement::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AEE90
void CTextureElement::ChangeTraits(CStruct* params)
{
    CUiWidget::ChangeTraits(params);
    uint32_t texture;
    if (params->ReadHashValue(kTexture, &texture, 0)) {
        m_texture = texture;
        reinterpret_cast<Engine3D::CTexturedQuad*>(m_piece)->SetTexture(static_cast<uint8_t*>(FindTexture(texture)));
        m_objectFlags |= kVisibleNow;
    }
    float angle;
    if (params->FetchFloat(kRotAngle, &angle, 0))
        ChangeSpin(angle, 1);
    uint32_t blend;
    if (params->ReadHashValue(kBlend, &blend, 0))
        reinterpret_cast<Engine3D::CTexturedQuad*>(m_piece)->ChooseBlendMode(blend);
}

// 0x004AEFD0
void CTextureElement::GetProperties(CStruct* out)
{
    uint8_t rgba[4];
    std::memcpy(rgba, m_summed.rgba, 4);
    Scripts::CArray* array = Scripts::NewArray();
    if (array)
        array = array->PrepareAll();
    array->ConfigureSizeKind(4, Scripts::QBVAL_INTEGER);
    for (int i = 0; i < 4; ++i)
        array->SetElement(i, rgba[i]);
    out->AppendArrayValue(kRgba, array);
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}

// 0x004AEF40
void CTextureElement::TickFrame()
{
    reinterpret_cast<Engine3D::CTexturedQuad*>(m_piece)->ChangeConcealed(false);
    reinterpret_cast<Engine3D::CTexturedQuad*>(m_piece)->ChangeRank(m_zPriority);
    reinterpret_cast<Engine3D::CTexturedQuad*>(m_piece)->ChangeRgba(ShownColour(this));
}

// --- SpriteElement --------------------------------------------------------------------------------------------

// 0x004AF1A0
CImageWidget* CImageWidget::MakeNew()
{
    CUiWidget::MakeNew();
    m_vtable = kCSpriteElementVtable;
    m_texture = 0;
    m_quad = NewSprite(0);
    m_zWrite = true;
    ChangeSpecies(kSpriteElement);
    return this;
}

// 0x004AF0D0
void CImageWidget::DestroyBody()
{
    m_vtable = kCSpriteElementVtable;
    if (m_quad)
        FreePiece(m_quad);
    CUiWidget::DestroyBody();
}

// 0x004AF210
CImageWidget* CImageWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AF130 (no texture of that name: the original reads through null)
void CImageWidget::SetTexture(uint32_t texture)
{
    m_texture = texture;
    void* found = FindTexture(texture);
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->SetTexture(static_cast<uint8_t*>(found));
    float height = static_cast<float>(TexelsDown(found));
    float width = static_cast<float>(TexelsAcross(found));
    FitToSize(width, height, 0);
    m_objectFlags |= kVisibleNow;
}

// 0x004AF240 (the target compared unrounded, the shown one with the float)
void CImageWidget::SetSpriteRotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
    float angle = static_cast<float>(radians);
    if (radians != m_aimPoint.angle) {
        m_aimPoint.angle = angle;
        m_objectFlags |= kDirty;
    }
    if (now && angle != m_shown.angle) {
        reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeSpin(angle);
        m_objectFlags |= kDirty;
        m_shown.angle = angle;
    }
}

// 0x004AF520
void CImageWidget::ChangeTraits(CStruct* params)
{
    CUiWidget::ChangeTraits(params);
    uint32_t texture;
    if (params->ReadHashValue(kTexture, &texture, 0))
        SetTexture(texture);
    float angle;
    if (params->FetchFloat(kRotAngle, &angle, 0))
        SetSpriteRotation(angle, 1);
    uint32_t blend;
    if (params->ReadHashValue(kBlend, &blend, 0))
        reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChooseBlendMode(blend);
    if (params->HasBareFlag(kNoZWrite))
        m_zWrite = false;
    // (no window above it: the original reads through null)
    auto* window = static_cast<CPanelWidget*>(FetchPane());
    reinterpret_cast<Engine3D::CGlyphString*>(m_quad)->ChangePane(window->FetchScreenRect());
}

// 0x004AF4E0
void CImageWidget::GetProperties(CStruct* out)
{
    CUiWidget::GetProperties(out);
    out->PushFloat(kRotAngle, m_shown.angle);
    out->PutNameKey(kTexture, m_texture);
}

// 0x004AF5E0
void CImageWidget::BlendTowards(CStruct* params)
{
    CUiWidget::BlendTowards(params);
    float degrees;
    if (params->FetchFloat(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
        if (radians != m_aimPoint.angle) {
            m_aimPoint.angle = static_cast<float>(radians);
            m_objectFlags |= kDirty;
        }
    }
}

// 0x004AF2C0
void CImageWidget::TickFrame()
{
    float top = m_summed.top;
    bool outside = top < LayoutTop() || top > LayoutBottom();
    bool hidden = IsConcealed();
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeConcealed(outside || hidden);
    if (!(m_objectFlags & (kChildrenDirty | kVisibleNow)))
        return;
    uint32_t rgba = ShownColour(this);
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeLocation(m_summed.x, m_summed.y);
    // (the size truncated: the height first)
    int height = Truncated(m_levelY);
    int width = Truncated(m_span);
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeExtent(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->SetFlip(static_cast<bool>((m_objectFlags >> 25) & 1), static_cast<bool>((m_objectFlags >> 26) & 1));
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeSpin((m_objectFlags & kFlag1000000) ? m_shown.angle : m_summed.angle);
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->SetHotSpot(m_justX, m_justY);
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeScale(m_summed.scaleX, m_summed.scaleY);
    if (m_objectFlags & kPos3d) {
        // the depth's bits, 5000 more with kKeepScale (an integer added to the float's bits, as the original)
        uint32_t depth;
        std::memcpy(&depth, &m_summed.z, 4);
        depth += (m_objectFlags & kKeepScale) ? 5000u : 0u;
        reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->SetDepth(depth);
        reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeConcealed((m_objectFlags & kOffScreen) != 0 || hidden);
    } else {
        reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeRank(m_zPriority);
        reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeConcealed(outside || hidden);
    }
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->SetZWrite(m_zWrite);
    reinterpret_cast<Engine3D::CBillboardQuad*>(m_quad)->ChangeRgba(rgba);
}

// --- WindowElement --------------------------------------------------------------------------------------------

// 0x004B2060
CPanelWidget* CPanelWidget::MakeNew()
{
    CUiWidget::MakeNew();
    m_vtable = kCWindowElementVtable;
    ChangeSpecies(kWindowElement);
    m_viewWindow = NewWindow2d(0, 0, 640, 448);
    ChangeAlignment(-1.0f, -1.0f);
    ChangeLocation(0.0f, 0.0f, 1);
    FitToSize(640.0f, 448.0f, 0);
    return this;
}

// 0x004B2100
void CPanelWidget::DestroyBody()
{
    m_vtable = kCWindowElementVtable;
    FreeWindow2d(m_viewWindow);
    CUiWidget::DestroyBody();
}

// 0x004B2170
CPanelWidget* CPanelWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004B2160
void* CPanelWidget::FetchScreenRect() const
{
    return m_viewWindow;
}

// 0x004B21A0
void CPanelWidget::TickFrame()
{
    if (!(m_objectFlags & (kChildrenDirty | kVisibleNow)))
        return;
    int top = Truncated(m_summed.y - (static_cast<double>(m_justY) + One()) * Half() * m_levelY);
    int left = Truncated(m_summed.x - (static_cast<double>(m_justX) + One()) * Half() * m_span);
    reinterpret_cast<Engine3D::CScreenWindow*>(m_viewWindow)->ChangeLocation(left, top);
    int height = Truncated(m_levelY);
    int width = Truncated(m_span);
    reinterpret_cast<Engine3D::CScreenWindow*>(m_viewWindow)->ChangeExtent(width, height);
}

namespace {
static VtableSlot kCTextureElementVtable_slots[] = {&CTextureElement::Teardown, &CTextureElement::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CTextureElement::GetProperties,
                &CUiWidget::BlendTowards, &CTextureElement::TickFrame};
void** const kCTextureElementVtable = reinterpret_cast<void**>(kCTextureElementVtable_slots);
static VtableSlot kCSpriteElementVtable_slots[] = {&CImageWidget::Teardown, &CImageWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CImageWidget::GetProperties, &CImageWidget::BlendTowards,
                &CImageWidget::TickFrame};
void** const kCSpriteElementVtable = reinterpret_cast<void**>(kCSpriteElementVtable_slots);
static VtableSlot kCWindowElementVtable_slots[] = {&CPanelWidget::Teardown, &CUiWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                &CPanelWidget::TickFrame};
void** const kCWindowElementVtable = reinterpret_cast<void**>(kCWindowElementVtable_slots);
} // namespace

} // namespace Front

using MenuSystem::CImageWidget;
using MenuSystem::CTextureElement;
using MenuSystem::CPanelWidget;
