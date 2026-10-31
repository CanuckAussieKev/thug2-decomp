// Screen elements: CUiWidget, the base of every front-end element (its states, placement, morphs, the tree,
// the layout pass, its properties and member functions).

#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/viewport_list.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/obj/entity.h"
#include "game/core/stubs.h"
#include "game/front/screen_constants.h"
#include "game/gfx/weather.h"

namespace MenuSystem {

namespace {
extern void** const kCScreenElementVtable;
} // namespace

float g_aspectToLayout{0.75000006f};
float g_wideScreen{0.9f};
float g_minusInv448{-0.002232143f};
float g_inv448{0.002232143f};
float g_inv896{0.0011160715f};
float g_inv1280{0.00078125f};
float g_invTwoPi{0.15915494f};
float g_twoPi{6.2831855f};
float g_pi{3.1415927f};
float g_layoutTop{-2e+02f};
float g_layoutBottom{648.0f};
float g_shadowBehind{0.005f};
float g_sixteenth{0.0625f};
float g_framesPerSecond{6e+01f};

static_assert(offsetof(SElementState, pos3d) == 0x04 && offsetof(SElementState, x) == 0x14 &&
                  offsetof(SElementState, z) == 0x1C && offsetof(SElementState, left) == 0x20 &&
                  offsetof(SElementState, scaleX) == 0x28 && offsetof(SElementState, baseScaleX) == 0x30 &&
                  offsetof(SElementState, angle) == 0x38 && offsetof(SElementState, rgba) == 0x3C,
              "layout must match the original");
static_assert(offsetof(CUiWidget, m_container) == 0x4C && offsetof(CUiWidget, m_eldest) == 0x58 &&
                  offsetof(CUiWidget, m_following) == 0x64 && offsetof(CUiWidget, m_preceding) == 0x70 &&
                  offsetof(CUiWidget, m_shown) == 0x7C && offsetof(CUiWidget, m_aimPoint) == 0xC0 &&
                  offsetof(CUiWidget, m_morphStart) == 0x104 && offsetof(CUiWidget, m_morphDone) == 0x110 &&
                  offsetof(CUiWidget, m_summed) == 0x114 && offsetof(CUiWidget, m_justX) == 0x158 &&
                  offsetof(CUiWidget, m_span) == 0x160 && offsetof(CUiWidget, m_rgba168) == 0x168 &&
                  offsetof(CUiWidget, m_zPriority) == 0x16C && offsetof(CUiWidget, m_rank) == 0x174 &&
                  sizeof(CUiWidget) == 0x178,
              "layout must match the original");

namespace {

using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kSetProps = 0x6C63C7C5, kDoMorph = 0xDD0C8DF3, kDie = 0xC6870028, kGetProps = 0x9492F814;
constexpr uint32_t kTime = 0x906B67BA, kPos = 0x7F261953, kPos3dName = 0x4B491900, kDims = 0x34A68574;
constexpr uint32_t kRelative = 0x91A4C826, kProportional = 0xB922906A, kParent = 0xC2719FB0, kJust = 0x8B60022F;
constexpr uint32_t kRgba = 0x3F6BCDBA, kRotAngle = 0xED344E1B, kFlipHName = 0xA0C7507F, kFlipVName = 0x5AC86D1C;
constexpr uint32_t kZPriorityName = 0x57710F31, kNotFocusable = 0x1D944426, kFocusable = 0x5CDECF29;
constexpr uint32_t kLocalId = 0xA2A5DEFE, kTagLocalId = 0x9494A525, kUnpause = 0xFF85D4D7;
constexpr uint32_t kBlockEventsName = 0xE5D6FE3E, kUnblockEvents = 0xD5614A50, kHide = 0x5B6634D4;
constexpr uint32_t kUnHide = 0xB60D1F35, kPriorityName = 0x9D5923D8, kKeepScaleName = 0x1CC71DFF;
constexpr uint32_t kFlag1000000Name = 0x1F2C6E1D, kTagNotFocusable = 0xF33A3321;
constexpr uint32_t kRestoreAlpha = 0x5D8BB535, kAlpha = 0x2F1FC695, kRememberAlpha = 0xFF8DF26B;
constexpr uint32_t kRelativeScale = 0xE1F4711F, kScale = 0x13B9DA7B, kAnim = 0x98549BA4;
constexpr uint32_t kEaseOut = 0xF5B95CE4, kFastIn = 0x6AA7DD49, kGentle = 0x768F6843, kInputLocked = 0x73CB6D65;
constexpr uint32_t kLeft = 0x85981897, kTop = 0xE126E035, kRight = 0x4B358AEB, kBottom = 0x76A08D5B;
constexpr uint32_t kWindowElement = 0xA13ADBB7;
// The element's events (sent by the system): its focus flag set / cleared.
constexpr uint32_t kSystem = 0x36B2EE74, kFocusSentEvent = 0x3056434F, kFocusClearedEvent = 0x4A57316B;
constexpr uint32_t kDeadBeef = 0xDEADBEEF;

float Degrees() { return ::ScriptCommands::g_degreeToRadianRatio; }       // pi / 180
float RotationOffset() { return ::GoalSystem::g_goalLifeSecondsPerMillisecond; } // 0.001 (added to a rotation in radians)
float ConvertToDegrees() { return ::ScriptCommands::g_scriptRadiansToDegrees; }

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

void LaunchEvent(uint32_t type, uint32_t target)
{
    GameObjects::Tracker()->LaunchEvent(type, target, kSystem, nullptr, false, -1.0f);
}

void* Hub() { return g_uiElementHub; }

// The screen element manager's pieces.
CUiTree* Elements(void* manager) { return static_cast<CUiTree*>(manager); }
bool ManagerUsesPriorities(void* manager) { return Elements(manager)->HasPriorities(); }
bool ResolveElementId(void* manager, CStruct* params, uint32_t name, uint32_t* id)
{
    return Elements(manager)->ResolveId(params, name, id);
}
ElementPtr* LookUp(void* manager, ElementPtr* out, uint32_t id, int assert)
{
    return Elements(manager)->FetchPart(out, id, assert);
}
void ManagerSetParent(void* manager, ElementPtr* parent, ElementPtr* child, int unknown)
{
    Elements(manager)->ChangeOwner(parent, child, unknown);
}
void ManagerDestroyElement(void* manager, uint32_t id, int recurse, int unknown, Scripts::CScript* script)
{
    Elements(manager)->DismantlePart(id, recurse, unknown, script);
}

// The element's vtable slots (0x0064A090).
void VSetProperties(CUiWidget* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[1])(e, params);
}
void VGetProperties(CUiWidget* e, CStruct* out)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[8])(e, out);
}
void VSetMorph(CUiWidget* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[9])(e, params);
}
void VUpdate(CUiWidget* e) { reinterpret_cast<void(__thiscall*)(CUiWidget*)>(e->m_vtable[10])(e); }

// A state's top left from its position: the justification point (-1..1 across the size) at x, y. Two operand
// orders occur in the original (the x87 products round differently): dims then scale, or scale then dims.
void TopLeftDimsFirst(const CUiWidget* e, SElementState* s)
{
    s->top = static_cast<float>(s->y - (static_cast<double>(e->m_justY) + 1.0) * e->m_levelY * s->scaleY * 0.5);
    s->left = static_cast<float>(s->x - (static_cast<double>(e->m_justX) + 1.0) * e->m_span * s->scaleX * 0.5);
}
void TopLeftScaleFirst(const CUiWidget* e, SElementState* s, double width, double height)
{
    s->top = static_cast<float>(s->y - (static_cast<double>(e->m_justY) + 1.0) * s->scaleY * height * 0.5);
    s->left = static_cast<float>(s->x - (static_cast<double>(e->m_justX) + 1.0) * s->scaleX * width * 0.5);
}

uint32_t Rgba(const uint8_t* bytes)
{
    uint32_t value;
    std::memcpy(&value, bytes, 4);
    return value;
}
void SetRgbaBytes(uint8_t* bytes, uint32_t value) { std::memcpy(bytes, &value, 4); }

// x87 FSIN / FCOS of a parent's angle and the rotated, scaled offset of a child's point: the parent's top left
// plus (px cos - py sin, px sin + py cos), px / py scaled by the parent's summed scale.
void Rotated(const CUiWidget* parent, float x, float y, double c, double s, float* outX, float* outY)
{
    double py = static_cast<double>(parent->m_summed.scaleY) * y;
    double px = static_cast<double>(parent->m_summed.scaleX) * x;
    float parentTop = parent->m_summed.top;
    *outX = static_cast<float>(px * c - py * s + parent->m_summed.left);
    *outY = static_cast<float>(px * s + py * c + parentTop);
}

} // namespace

// 0x004A8730
SElementState* SElementState::Assign(const SElementState* other)
{
    std::memcpy(this, other, sizeof(*this));
    return this;
}

// --- construction ---------------------------------------------------------------------------------------------

// 0x004A8F90
CUiWidget* CUiWidget::MakeNew()
{
    GameObjects::CEntity::MakeNew();
    m_vtable = kCScreenElementVtable;
    m_container.m_preceding = m_container.m_following = nullptr;
    m_container.m_ownerObject = nullptr;
    m_eldest.m_preceding = m_eldest.m_following = nullptr;
    m_eldest.m_ownerObject = nullptr;
    m_following.m_preceding = m_following.m_following = nullptr;
    m_following.m_ownerObject = nullptr;
    m_preceding.m_preceding = m_preceding.m_following = nullptr;
    m_preceding.m_ownerObject = nullptr;
    m_morphLength = 0;
    m_shown.scaleX = m_shown.scaleY = 1.0f;
    m_shown.baseScaleX = m_shown.baseScaleY = 1.0f;
    m_shown.x = m_shown.y = 0.0f;
    m_shown.angle = 0.0f;
    m_shown.pos3d = {0.0f, 0.0f, 0.0f, 1.0f};
    m_shown.rgba[0] = m_shown.rgba[1] = m_shown.rgba[2] = m_shown.rgba[3] = 0x80;
    m_shown.alpha = 1.0f;
    m_aimPoint.Assign(&m_shown);
    m_summed.Assign(&m_shown);
    m_objectFlags |= kNew | kDirty;
    m_span = m_levelY = 0.0f;
    m_rgba168[0] = m_rgba168[1] = m_rgba168[2] = m_rgba168[3] = 0x80;
    m_rank = 0;
    m_zPriority = 0.0f;
    m_rememberedAlpha = 1.0f;
    return this;
}

// 0x004AAB30
void CUiWidget::DestroyBody()
{
    m_vtable = kCScreenElementVtable;
    {
        ElementPtr none;
        ChangeOwner(&none, 0);
    }
    m_ident = kDeadBeef;
    m_preceding.Release();
    m_following.Release();
    m_eldest.Release();
    m_container.Release();
    GameObjects::CEntity::DestroyBody();
}

// 0x004AAC90
CUiWidget* CUiWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// --- simple accessors -----------------------------------------------------------------------------------------

// 0x004A8610
void CUiWidget::FetchAlignment(float* x, float* y) const
{
    *x = m_justX;
    *y = m_justY;
}

// 0x004A87C0
void CUiWidget::FetchLocation(float* x, float* y) const
{
    *x = m_aimPoint.x;
    *y = m_aimPoint.y;
}

// 0x004A87E0
void CUiWidget::GetTopLeft(float* x, float* y) const
{
    *x = m_aimPoint.left;
    *y = m_aimPoint.top;
}

// 0x004A86C0
int CUiWidget::FetchRank() const { return m_rank; }

// 0x004A86B0
bool CUiWidget::ManagerUsesPriorities() const { return MenuSystem::ManagerUsesPriorities(m_parkOwner); }

// 0x004A87A0
double CUiWidget::GetLayoutWidth() const { return static_cast<double>(m_span) * m_summed.baseScaleX; }

// 0x004A87B0
double CUiWidget::GetLayoutHeight() const { return static_cast<double>(m_levelY) * m_summed.baseScaleY; }

// 0x004A8F00
bool CUiWidget::InputSuppressed() const
{
    if (m_objectFlags & kBlockEvents)
        return true;
    return m_container.m_ownerObject && m_container.m_ownerObject->InputSuppressed();
}

// 0x004A8F20
bool CUiWidget::HasPriority() const
{
    if (m_objectFlags & kPriority)
        return true;
    return m_container.m_ownerObject && m_container.m_ownerObject->HasPriority();
}

// 0x004A8F40
bool CUiWidget::IsConcealed() const
{
    if (m_objectFlags & kHidden)
        return true;
    return m_container.m_ownerObject && m_container.m_ownerObject->IsConcealed();
}

// 0x004A8680
void CUiWidget::SetFrozen(bool pause)
{
    if (pause)
        m_objectFlags |= kPauseAsked;
    else
        m_objectFlags &= ~(kPauseAsked | kPaused);
}

// 0x004A8940
void CUiWidget::SetFocusSent(int sent)
{
    int now = (m_objectFlags & kFocusSent) ? 1 : 0;
    if (sent == now)
        return;
    if (sent == 1)
        m_objectFlags |= kFocusSent;
    else
        m_objectFlags &= ~kFocusSent;
    LaunchEvent(sent == 1 ? kFocusSentEvent : kFocusClearedEvent, m_ident);
}

// --- placement ------------------------------------------------------------------------------------------------

// 0x004A8C30
void CUiWidget::PlaceTopLeft(SElementState* s) { TopLeftDimsFirst(this, s); }

// 0x004A9100
void CUiWidget::ChangeLocation(float x, float y, int now)
{
    if (x != m_aimPoint.x || y != m_aimPoint.y) {
        m_aimPoint.x = x;
        m_aimPoint.y = y;
        m_objectFlags |= kDirty;
        TopLeftDimsFirst(this, &m_aimPoint);
    }
    if (now && (x != m_shown.x || y != m_shown.y)) {
        m_shown.x = x;
        m_shown.y = y;
        m_objectFlags |= kDirty;
    }
}

// 0x004A91F0
void CUiWidget::SetPos3d(const Maths::Vec4* pos, int now)
{
    if (m_aimPoint.pos3d != *pos) {
        m_aimPoint.pos3d = *pos;
        m_objectFlags |= kDirty;
        TopLeftDimsFirst(this, &m_aimPoint);
    }
    if (now && m_shown.pos3d != *pos) {
        m_shown.pos3d = *pos;
        m_objectFlags |= kDirty;
    }
    m_objectFlags |= kPos3d;
}

// 0x004A92D0
void CUiWidget::FitToSize(float w, float h, int lock)
{
    if (w != m_span || h != m_levelY) {
        m_objectFlags |= kDirty;
        m_span = w;
        m_levelY = h;
        TopLeftScaleFirst(this, &m_aimPoint, w, h);
    }
    if (lock)
        m_objectFlags |= kDimsLocked;
    else
        m_objectFlags &= ~kDimsLocked;
}

// 0x004A9390: the position moves by the justification's change so the element stays where it was.
void CUiWidget::ChangeAlignment(float x, float y)
{
    if (x == m_justX && y == m_justY)
        return;
    double dx = (static_cast<double>(x) - m_justX) * m_shown.scaleX * m_span * 0.5;
    double dy = (static_cast<double>(y) - m_justY) * m_shown.scaleY * m_levelY * 0.5;
    float shownY = m_shown.y;
    m_shown.x = static_cast<float>(dx + m_shown.x);
    m_shown.y = static_cast<float>(shownY + dy);
    float targetY = m_aimPoint.y;
    m_aimPoint.x = static_cast<float>(dx + m_aimPoint.x);
    m_aimPoint.y = static_cast<float>(targetY + dy);
    m_justX = x;
    m_justY = y;
    m_objectFlags |= kDirty;
    m_aimPoint.top = static_cast<float>(m_aimPoint.y - (static_cast<double>(y) + 1.0) * m_aimPoint.scaleY * m_levelY * 0.5);
    m_aimPoint.left = static_cast<float>(m_aimPoint.x - (static_cast<double>(x) + 1.0) * m_aimPoint.scaleX * m_span * 0.5);
}

// 0x004A94A0
void CUiWidget::ChangeScale(float x, float y, bool relative, int now)
{
    bool same = x == m_aimPoint.scaleX && y == m_aimPoint.scaleY;
    if (!same || relative) {
        if (relative) {
            x = static_cast<float>(static_cast<double>(x) * m_aimPoint.baseScaleX);
            y = static_cast<float>(static_cast<double>(y) * m_aimPoint.baseScaleY);
        } else {
            m_aimPoint.baseScaleX = x;
            m_aimPoint.baseScaleY = y;
        }
        m_aimPoint.scaleX = x;
        m_aimPoint.scaleY = y;
        TopLeftDimsFirst(this, &m_aimPoint);
        m_objectFlags |= kDirty;
    }
    if (now && (x != m_shown.scaleX || y != m_shown.scaleY)) {
        m_shown.scaleX = x;
        m_shown.scaleY = y;
        m_objectFlags |= kDirty;
    }
}

// 0x004A8630
void CUiWidget::ChangeOpacity(float alpha, int now)
{
    if (alpha != m_aimPoint.alpha) {
        m_aimPoint.alpha = alpha;
        m_objectFlags |= kDirty;
    }
    if (now && alpha != m_shown.alpha) {
        m_shown.alpha = alpha;
        m_objectFlags |= kDirty;
    }
}

// 0x004A8800 (the angle is compared unrounded, stored as a float)
void CUiWidget::ChangeSpin(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
    if (radians != m_aimPoint.angle) {
        m_aimPoint.angle = static_cast<float>(radians);
        m_objectFlags |= kDirty;
    }
    if (now && radians != m_shown.angle) {
        m_shown.angle = static_cast<float>(radians);
        m_objectFlags |= kDirty;
    }
}

// 0x004A88C0 (the shown colour is set without comparing)
void CUiWidget::ChangeRgba(uint32_t rgba, int now)
{
    if (Rgba(m_aimPoint.rgba) != rgba) {
        SetRgbaBytes(m_aimPoint.rgba, rgba);
        m_objectFlags |= kDirty;
    }
    if (now) {
        SetRgbaBytes(m_shown.rgba, rgba);
        m_objectFlags |= kDirty;
    }
}

// 0x004A8870
void CUiWidget::StartMorph(int ms)
{
    m_morphStart = FrameClock::GetTime();
    m_morphLength = ms;
    m_morphCurveDone = 0.0f;
    m_morphDone = 0.0f;
    if (ms == 0)
        m_shown.Assign(&m_aimPoint);
    m_objectFlags = (m_objectFlags & ~(kPauseAsked | kPaused)) | kDirty;
}

// 0x004AA390
void CUiWidget::ChangeDrawDepth(float z)
{
    if (!(m_objectFlags & kZPriority)) {
        m_objectFlags |= kVisibleNow;
        m_zPriority = z;
    }
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    if (child) {
        float next = static_cast<float>(static_cast<double>(z) + 1.0);
        while (child) {
            child->ChangeDrawDepth(next);
            child.AssignValue(child->m_following.m_ownerObject);
        }
    }
}

// 0x004AA470
void CUiWidget::SetChildrenPriority(int priority)
{
    m_objectFlags |= kPriority;
    m_rank = priority;
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    while (child) {
        child->SetChildrenPriority(priority);
        child.AssignValue(child->m_following.m_ownerObject);
    }
}

// --- the tree -------------------------------------------------------------------------------------------------

// 0x004A8B80
ElementPtr* CUiWidget::GetParent(ElementPtr* out) const
{
    out->m_preceding = nullptr;
    out->m_following = nullptr;
    out->m_ownerObject = m_container.m_ownerObject;
    if (out->m_ownerObject)
        reinterpret_cast<GameObjects::CSharedBase*>(out->m_ownerObject)
            ->AttachSharedRef(reinterpret_cast<GameObjects::CSharedRef<GameObjects::CSharedBase>*>(out));
    return out;
}

// 0x004A8BB0
CUiWidget* CUiWidget::FetchPane() const
{
    CUiWidget* element;
    {
        ElementPtr parent;
        element = GetParent(&parent)->m_ownerObject;
    }
    while (element) {
        if (element->m_kindTag == kWindowElement)
            return element;
        ElementPtr parent;
        element = element->GetParent(&parent)->m_ownerObject;
    }
    return nullptr;
}

// 0x004A95E0
CUiWidget* CUiWidget::FetchYoungest() const
{
    CUiWidget* child = m_eldest.m_ownerObject;
    if (!child)
        return nullptr;
    while (child->m_following.m_ownerObject)
        child = child->m_following.m_ownerObject;
    return child;
}

// 0x004A9600
int CUiWidget::ChildTotal() const
{
    int count = 0;
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    while (child) {
        child.AssignValue(child->m_following.m_ownerObject);
        ++count;
    }
    return count;
}

// 0x004AA2A0
ElementPtr* CUiWidget::FetchOffspring(ElementPtr* out, uint32_t id, int* index)
{
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    for (int i = 0; child; ++i) {
        if (id == 0) {
            if (*index == i)
                break;
        } else if (child->m_ident == id) {
            if (index)
                *index = i;
            break;
        }
        child.AssignValue(child->m_following.m_ownerObject);
    }
    out->m_preceding = nullptr;
    out->m_following = nullptr;
    out->m_ownerObject = nullptr;
    out->AssignValue(child.m_ownerObject);
    return out;
}

// 0x004AA690
CUiWidget* CUiWidget::FindChildWithId(uint32_t id, int* index)
{
    ElementPtr child;
    return FetchOffspring(&child, id, index)->m_ownerObject;
}

// 0x004AA6D0
CUiWidget* CUiWidget::FindChildAtSlot(int index)
{
    ElementPtr child;
    return FetchOffspring(&child, 0, &index)->m_ownerObject;
}

// 0x004AA8E0
void CUiWidget::ChangeOwner(const ElementPtr* parent, int)
{
    if (m_container) {
        // out of the old parent's children
        ElementPtr child;
        child.AssignValue(m_container->m_eldest.m_ownerObject);
        while (child && child.m_ownerObject != this)
            child.AssignValue(child->m_following.m_ownerObject);
        if (child) {
            if (m_preceding)
                m_preceding->m_following.AssignValue(m_following.m_ownerObject);
            else
                m_container->m_eldest.AssignValue(m_following.m_ownerObject);
            if (m_following)
                m_following->m_preceding.AssignValue(m_preceding.m_ownerObject);
        }
    }
    m_container.AssignValue(parent->m_ownerObject);
    if (m_container) {
        // last among the new parent's children
        ElementPtr last;
        last.AssignValue(m_container->m_eldest.m_ownerObject);
        ElementPtr* slot;
        if (last) {
            while (last->m_following)
                last.AssignValue(last->m_following.m_ownerObject);
            slot = &last->m_following;
        } else {
            slot = &m_container->m_eldest;
        }
        slot->AssignValue(this);
        m_preceding.AssignValue(last.m_ownerObject);
    } else {
        m_preceding.Release();
    }
    m_following.Release();
    if (CUiWidget* newParent = parent->m_ownerObject) {
        if (newParent->HasPriority())
            SetChildrenPriority(newParent->m_rank);
        ChangeDrawDepth(static_cast<float>(static_cast<double>(parent->m_ownerObject->m_zPriority) + 1.0));
    } else {
        ChangeDrawDepth(0.0f);
    }
}

// --- the layout pass ------------------------------------------------------------------------------------------

// 0x004A9D30
void CUiWidget::UpdateLayout()
{
    int now = FrameClock::GetTime();
    ++m_aliasCount; // (the object's use count while the pass runs)
    if (!(m_objectFlags & kFocusSent)) {
        m_objectFlags |= kFocusSent;
        LaunchEvent(kFocusSentEvent, m_ident);
    }
    bool changed = false;
    if (m_objectFlags & kPos3d) {
        // projected through the first viewport's camera (0x004A06D0; 0x004A1090 calls its vtable slot 1: the
        // depth scale in ST0, negative behind the camera)
        void* camera = Engine3D::FetchCurrentView(0);
        float x, y, z;
        float scale = reinterpret_cast<float(__thiscall*)(void*, const Maths::Vec4*, float*, float*, float*)>(&Engine3D::VirtualSlot1Jump)(
            camera, &m_aimPoint.pos3d, &x, &y, &z);
        if (scale < 0.0f) {
            m_aimPoint.x = -320.0f;
            m_aimPoint.y = -224.0f;
            m_objectFlags |= kOffScreen;
        } else {
            m_aimPoint.x = x;
            m_aimPoint.y = y;
            m_aimPoint.z = z;
            if (!(m_objectFlags & kKeepScale))
                ChangeScale(scale, scale, true, 0);
            m_objectFlags &= ~kOffScreen;
        }
        TopLeftScaleFirst(this, &m_aimPoint, m_span, m_levelY);
        m_objectFlags |= kDirty;
    }
    if ((m_objectFlags & kDirty) && !(m_objectFlags & kPaused)) {
        if (m_morphLength) {
            uint32_t elapsed = static_cast<uint32_t>(now - m_morphStart);
            uint32_t length = static_cast<uint32_t>(m_morphLength);
            float t = static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(length));
            if (elapsed >= length) {
                t = 1.0f;
                m_morphLength = 0;
            }
            float curve;
            switch ((m_objectFlags >> 8) & 3) {
            case 1: // ease out
                curve = static_cast<float>((static_cast<double>(t) + t) - static_cast<double>(t) * t);
                break;
            case 2: // fast in
                curve = static_cast<float>(static_cast<double>(t) * t);
                break;
            case 3: // gentle
                curve = static_cast<float>((3.0 - (static_cast<double>(t) + t)) * t * t);
                break;
            default:
                curve = t;
                break;
            }
            // what's left of the move this frame: the curve's for the position, the time's for the rest
            double k = (static_cast<double>(curve) - m_morphCurveDone) / (1.0 - m_morphCurveDone);
            float kt = static_cast<float>((static_cast<double>(t) - m_morphDone) / (1.0 - m_morphDone));
            SElementState& s = m_shown;
            float shownY = s.y;
            s.x = static_cast<float>((static_cast<double>(m_aimPoint.x) - s.x) * k + s.x);
            s.y = static_cast<float>((static_cast<double>(m_aimPoint.y) - shownY) * k + shownY);
            float shownScaleY = s.scaleY;
            s.scaleX = static_cast<float>((static_cast<double>(m_aimPoint.scaleX) - s.scaleX) * kt + s.scaleX);
            s.scaleY = static_cast<float>((static_cast<double>(m_aimPoint.scaleY) - shownScaleY) * kt + shownScaleY);
            s.angle = static_cast<float>((static_cast<double>(m_aimPoint.angle) - s.angle) * kt + s.angle);
            s.alpha = static_cast<float>((static_cast<double>(m_aimPoint.alpha) - s.alpha) * kt + s.alpha);
            PlaceTopLeft(&s);
            uint8_t rgba[4];
            for (int i = 0; i < 4; ++i) {
                int from = s.rgba[i];
                rgba[i] = static_cast<uint8_t>(
                    Truncated(static_cast<double>(m_aimPoint.rgba[i] - from) * kt + static_cast<double>(from)));
            }
            std::memcpy(s.rgba, rgba, 4);
            m_morphCurveDone = curve;
            m_morphDone = t;
        } else {
            m_shown.Assign(&m_aimPoint);
            m_objectFlags &= ~kDirty;
        }
        changed = true;
    }
    CUiWidget* parent = m_container.m_ownerObject;
    if (parent && (parent->m_objectFlags & kChildrenDirty))
        changed = true;
    m_objectFlags &= ~kChildrenDirty;
    if (changed) {
        if (parent && !(m_objectFlags & kPos3d)) {
            m_summed.scaleX = static_cast<float>(static_cast<double>(parent->m_summed.scaleX) * m_shown.scaleX);
            m_summed.scaleY = static_cast<float>(static_cast<double>(parent->m_summed.scaleY) * m_shown.scaleY);
            parent = m_container.m_ownerObject;
            m_summed.angle = static_cast<float>(static_cast<double>(m_shown.angle) + parent->m_summed.angle);
            double c = X87Cos(parent->m_summed.angle);
            double s = X87Sin(parent->m_summed.angle);
            Rotated(parent, m_shown.x, m_shown.y, c, s, &m_summed.x, &m_summed.y);
            parent = m_container.m_ownerObject;
            m_summed.alpha = static_cast<float>(static_cast<double>(parent->m_summed.alpha) * m_shown.alpha);
            Rotated(parent, m_shown.left, m_shown.top, c, s, &m_summed.left, &m_summed.top);
        } else {
            m_summed.Assign(&m_shown);
        }
        m_objectFlags |= kChildrenDirty;
    }
    VUpdate(this);
    m_objectFlags &= ~kVisibleNow;
    if (m_objectFlags & kPauseAsked)
        m_objectFlags |= kPaused;
    for (CUiWidget* child = m_eldest.m_ownerObject; child; child = child->m_following.m_ownerObject)
        child->UpdateLayout();
    if (static_cast<int>(m_aliasCount) > 0)
        --m_aliasCount;
}

// --- parameters -----------------------------------------------------------------------------------------------

// 0x004A8C80 (a Pos structure with neither flag gives true but leaves x, y alone)
bool CUiWidget::ReadPositionParam(Scripts::CStruct* params, float* x, float* y)
{
    Scripts::CPair pair;
    pair.PrepareAll();
    CStruct* pos;
    if (params->ReadSubStruct(kPos, &pos, 0)) {
        pos->FetchPair(0, &pair, true);
        if (pos->HasBareFlag(kRelative)) {
            *x = static_cast<float>(static_cast<double>(pair.x) + m_aimPoint.x);
            *y = static_cast<float>(static_cast<double>(pair.y) + m_aimPoint.y);
            return true;
        }
        if (pos->HasBareFlag(kProportional)) {
            *x = static_cast<float>(static_cast<double>(pair.x) * m_container->m_span);
            *y = static_cast<float>(static_cast<double>(pair.y) * m_container->m_levelY);
        }
        return true;
    }
    if (params->FetchPair(kPos, &pair, false)) {
        *x = pair.x;
        *y = pair.y;
        return true;
    }
    Scripts::VectorValue v;
    if (params->FetchVec(kPos, &v, 0)) {
        *x = v.x;
        *y = v.y;
        return true;
    }
    return false;
}

// 0x004A8DC0
bool CUiWidget::ReadAlignmentParam(Scripts::CStruct* params, uint32_t name, float* x, float* y)
{
    Scripts::CArray* array;
    if (!params->FetchList(name, &array, 0))
        return false;
    if (array->m_kindTag == Scripts::QBVAL_FLOAT) {
        *x = array->FetchFloat(0);
        *y = array->FetchFloat(1);
    } else if (array->m_kindTag == Scripts::QBVAL_NAME) {
        float just[2];
        for (int i = 0; i < 2; ++i) {
            uint32_t side = array->FetchPart(i);
            if (side == kLeft || side == kTop)
                just[i] = -1.0f;
            else if (side == kRight || side == kBottom)
                just[i] = 1.0f;
            else
                just[i] = 0.0f;
        }
        *x = just[0];
        *y = just[1];
    }
    return true;
}

// 0x004A8E90 (shifts past 31 bits wrap, as x86 SHL does)
bool CUiWidget::ReadColourParam(Scripts::CStruct* params, uint32_t name, uint32_t* rgba)
{
    Scripts::CArray* array;
    if (!params->FetchList(name, &array, 0))
        return false;
    uint32_t count = array->m_extent;
    uint32_t value = 0;
    for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(count); ++i)
        value |= (array->FetchPart(i) & 0xFF) << ((8 * i) & 31);
    *rgba = value;
    return true;
}

// --- the vtable -----------------------------------------------------------------------------------------------

// 0x004A96B0
void CUiWidget::ChangeTraits(Scripts::CStruct* params)
{
    void* manager = m_parkOwner;
    Scripts::CPair pair;
    pair.PrepareAll();
    Scripts::VectorValue v;
    if (params->FetchPair(kPos, &pair, false))
        ChangeLocation(pair.x, pair.y, 1);
    else if (params->FetchVec(kPos, &v, 0))
        ChangeLocation(v.x, v.y, 1);
    Scripts::VectorValue pos3d;
    if (params->FetchVec(kPos3dName, &pos3d, 0)) {
        pos3d.w = 1.0f;
        SetPos3d(reinterpret_cast<const Maths::Vec4*>(&pos3d), 1);
    }
    if (params->HasBareFlag(kKeepScaleName))
        m_objectFlags |= kKeepScale;
    Scripts::CPair dims;
    dims.PrepareAll();
    if (params->FetchPair(kDims, &dims, false))
        FitToSize(dims.x, dims.y, 1);
    uint32_t parentId;
    if (ResolveElementId(manager, params, kParent, &parentId)) {
        ElementPtr parent;
        LookUp(manager, &parent, parentId, 0);
        ElementPtr self;
        self.AssignValue(this);
        ManagerSetParent(manager, &parent, &self, 1);
    }
    float alignH = 0.0f, alignV = 0.0f;
    if (ReadAlignmentParam(params, kJust, &alignH, &alignV))
        ChangeAlignment(alignH, alignV);
    uint32_t rgba;
    if (ReadColourParam(params, kRgba, &rgba))
        ChangeRgba(rgba, 1);
    float degrees = 0.0f;
    if (params->FetchFloat(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * Degrees();
        m_objectFlags |= kDirty;
        m_shown.angle = static_cast<float>(radians);
        m_aimPoint.angle = static_cast<float>(radians);
    }
    if (params->HasBareFlag(kFlag1000000Name))
        m_objectFlags |= kFlag1000000;
    if (params->HasBareFlag(kFlipHName))
        m_objectFlags |= kFlipH;
    else
        m_objectFlags &= ~kFlipH;
    if (params->HasBareFlag(kFlipVName))
        m_objectFlags |= kFlipV;
    else
        m_objectFlags &= ~kFlipV;
    float z;
    if (params->FetchFloat(kZPriorityName, &z, 0)) {
        m_zPriority = z;
        m_objectFlags |= kZPriority | kVisibleNow;
    }
    if (params->HasBareFlag(kNotFocusable))
        WriteHashTag(0, kTagNotFocusable);
    else if (params->HasBareFlag(kFocusable))
        RemoveTagFlag(kTagNotFocusable);
    uint32_t localId;
    if (params->ReadHashValue(kLocalId, &localId, 0))
        WriteHashTag(kTagLocalId, localId);
    if (params->HasBareFlag(kUnpause))
        m_objectFlags &= ~(kPauseAsked | kPaused);
    if (params->HasBareFlag(kBlockEventsName))
        m_objectFlags |= kBlockEvents;
    else if (params->HasBareFlag(kUnblockEvents))
        m_objectFlags &= ~kBlockEvents;
    if (params->HasBareFlag(kHide))
        m_objectFlags |= kHidden;
    else if (params->HasBareFlag(kUnHide))
        m_objectFlags = (m_objectFlags & ~kHidden) | kVisibleNow;
    int priority;
    if (params->FetchIntValue(kPriorityName, &priority, 0) && priority > 0) {
        m_rank = priority;
        m_objectFlags |= kPriority;
    }
    GameObjects::CEntity::ChangeTraits(params);
}

// 0x004A89B0
bool CUiWidget::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    if (name == kSetProps) {
        VSetProperties(this, params);
        return true;
    }
    if (name == kDoMorph) {
        float seconds;
        if (params->FetchFloat(kTime, &seconds, 0))
            script->Lull(Truncated(static_cast<double>(seconds) * 1000.0f));
        VSetMorph(this, params);
        return true;
    }
    if (name == kDie) {
        // the parent's focus flag dropped (its event sent) while the element goes, and set again after
        void* manager = Hub();
        ElementPtr parent;
        parent.AssignValue(m_container.m_ownerObject);
        if (parent->m_objectFlags & kFocusSent) {
            parent->m_objectFlags &= ~kFocusSent;
            LaunchEvent(kFocusClearedEvent, parent->m_ident);
        }
        ManagerDestroyElement(manager, m_ident, 1, 0, script);
        if (!(parent->m_objectFlags & kFocusSent)) {
            parent->m_objectFlags |= kFocusSent;
            LaunchEvent(kFocusSentEvent, parent->m_ident);
        }
        return true;
    }
    if (name == kGetProps) {
        VGetProperties(this, script->m_args);
        return true;
    }
    return GameObjects::CEntity::InvokeMethodByName(name, params, script);
}

// 0x004AA710
void CUiWidget::GetProperties(Scripts::CStruct* out)
{
    out->PushFloat(kAlpha, m_shown.alpha);
    uint8_t rgba[4];
    std::memcpy(rgba, m_shown.rgba, 4);
    Scripts::CArray* array = Scripts::AllocScriptArray();
    if (array)
        array = array->PrepareAll();
    array->ConfigureSizeKind(4, Scripts::QBVAL_INTEGER);
    for (int i = 0; i < 4; ++i)
        array->SetElement(i, rgba[i]);
    out->AppendArrayValue(kRgba, array);
    Scripts::ReleaseArrayStorage(array);
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
    float scaleX = m_shown.scaleX, scaleY = m_shown.scaleY;
    if (scaleX == scaleY)
        out->PushFloat(kScale, scaleX);
    else
        out->InsertPair(kScale, scaleX, scaleY);
    out->InsertPair(kPos, m_shown.x, m_shown.y);
    out->PushFloat(kRotAngle, static_cast<float>(static_cast<double>(m_shown.angle) * ConvertToDegrees()));
    out->PutInt(kInputLocked, InputSuppressed() ? 1 : 0);
}

// 0x004A9AA0
void CUiWidget::BlendTowards(Scripts::CStruct* params)
{
    // (ReadPositionParam may leave them: the original's y is its argument's slot - the params' address -, x a stale local)
    float x = 0.0f, y;
    std::memcpy(&y, &params, sizeof(y));
    if (ReadPositionParam(params, &x, &y))
        ChangeLocation(x, y, 0);
    if (params->HasBareFlag(kRestoreAlpha)) {
        float alpha = m_rememberedAlpha;
        if (alpha != m_aimPoint.alpha) {
            m_aimPoint.alpha = alpha;
            m_objectFlags |= kDirty;
        }
    }
    float alpha;
    if (params->FetchFloat(kAlpha, &alpha, 0)) {
        if (params->HasBareFlag(kRememberAlpha))
            m_rememberedAlpha = m_shown.alpha;
        if (alpha != m_aimPoint.alpha) {
            m_aimPoint.alpha = alpha;
            m_objectFlags |= kDirty;
        }
    }
    bool relative = params->HasBareFlag(kRelativeScale);
    float scale;
    if (params->FetchFloat(kScale, &scale, 0))
        ChangeScale(scale, scale, relative, 0);
    Scripts::CPair pair;
    pair.PrepareAll();
    if (params->FetchPair(kScale, &pair, false))
        ChangeScale(pair.x, pair.y, relative, 0);
    float degrees;
    if (params->FetchFloat(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
        if (radians != m_aimPoint.angle) {
            m_aimPoint.angle = static_cast<float>(radians);
            m_objectFlags |= kDirty;
        }
    }
    float seconds;
    int ms = params->FetchFloat(kTime, &seconds, 0) ? Truncated(static_cast<double>(seconds) * 1000.0f) : 0;
    StartMorph(ms);
    m_objectFlags &= ~kMorphCurve;
    uint32_t anim = 0;
    if (params->ReadHashValue(kAnim, &anim, 0)) {
        if (anim == kEaseOut)
            m_objectFlags |= 0x100;
        else if (anim == kFastIn)
            m_objectFlags |= 0x200;
        else if (anim == kGentle)
            m_objectFlags |= 0x300;
    }
    uint32_t rgba;
    if (ReadColourParam(params, kRgba, &rgba) && Rgba(m_aimPoint.rgba) != rgba) {
        SetRgbaBytes(m_aimPoint.rgba, rgba);
        m_objectFlags |= kDirty;
    }
}

void FetchPart(ElementPtr* out, uint32_t id) { LookUp(Hub(), out, id, 0); }

namespace {
static VtableSlot kCScreenElementVtable_slots[] = {&CUiWidget::Teardown, &CUiWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                Stubs::DoNothing};
void** const kCScreenElementVtable = reinterpret_cast<void**>(kCScreenElementVtable_slots);
} // namespace

} // namespace Front

using MenuSystem::CUiWidget;
using MenuSystem::SElementState;
