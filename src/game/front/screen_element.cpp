// Screen elements: CScreenElement, the base of every front-end element (its states, placement, morphs, the tree,
// the layout pass, its properties and member functions).

#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "game/core/mem.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

static_assert(offsetof(SElementState, pos3d) == 0x04 && offsetof(SElementState, x) == 0x14 &&
                  offsetof(SElementState, z) == 0x1C && offsetof(SElementState, left) == 0x20 &&
                  offsetof(SElementState, scaleX) == 0x28 && offsetof(SElementState, baseScaleX) == 0x30 &&
                  offsetof(SElementState, angle) == 0x38 && offsetof(SElementState, rgba) == 0x3C,
              "layout must match the original");
static_assert(offsetof(CScreenElement, mp_parent) == 0x4C && offsetof(CScreenElement, mp_firstChild) == 0x58 &&
                  offsetof(CScreenElement, mp_next) == 0x64 && offsetof(CScreenElement, mp_prev) == 0x70 &&
                  offsetof(CScreenElement, m_shown) == 0x7C && offsetof(CScreenElement, m_target) == 0xC0 &&
                  offsetof(CScreenElement, m_morphStart) == 0x104 && offsetof(CScreenElement, m_morphDone) == 0x110 &&
                  offsetof(CScreenElement, m_summed) == 0x114 && offsetof(CScreenElement, m_justX) == 0x158 &&
                  offsetof(CScreenElement, m_width) == 0x160 && offsetof(CScreenElement, m_rgba168) == 0x168 &&
                  offsetof(CScreenElement, m_zPriority) == 0x16C && offsetof(CScreenElement, m_priority) == 0x174 &&
                  sizeof(CScreenElement) == 0x178,
              "layout must match the original");

namespace {

using Script::CStruct;

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
constexpr uint32_t kEaseOut = 0xF5B95CE4, kFastIn = 0x6AA7DD49, kGentle = 0x768F6843, kEventsBlocked = 0x73CB6D65;
constexpr uint32_t kLeft = 0x85981897, kTop = 0xE126E035, kRight = 0x4B358AEB, kBottom = 0x76A08D5B;
constexpr uint32_t kWindowElement = 0xA13ADBB7;
// The element's events (sent by the system): its focus flag set / cleared.
constexpr uint32_t kSystem = 0x36B2EE74, kFocusSentEvent = 0x3056434F, kFocusClearedEvent = 0x4A57316B;
constexpr uint32_t kDeadBeef = 0xDEADBEEF;

float Constant(uint32_t address) { return GameVariable<float>(address); }
float Degrees() { return Constant(0x00645464); }       // pi / 180
float RotationOffset() { return Constant(0x006461D8); } // 0.001 (added to a rotation in radians)
float RadiansToDegrees() { return Constant(0x00645468); }

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
    Obj::Tracker()->LaunchEvent(type, target, kSystem, nullptr, false, -1.0f);
}

void* Manager() { return GameVariable<void*>(0x00701440); }

// The screen element manager's pieces.
CScreenElementManager* Elements(void* manager) { return static_cast<CScreenElementManager*>(manager); }
bool ManagerUsesPriorities(void* manager) { return Elements(manager)->HasPriorities(); }
bool ResolveElementId(void* manager, CStruct* params, uint32_t name, uint32_t* id)
{
    return Elements(manager)->ResolveId(params, name, id);
}
ElementPtr* LookUp(void* manager, ElementPtr* out, uint32_t id, int assert)
{
    return Elements(manager)->GetElement(out, id, assert);
}
void ManagerSetParent(void* manager, ElementPtr* parent, ElementPtr* child, int unknown)
{
    Elements(manager)->SetParent(parent, child, unknown);
}
void ManagerDestroyElement(void* manager, uint32_t id, int recurse, int unknown, Script::CScript* script)
{
    Elements(manager)->DestroyElement(id, recurse, unknown, script);
}

// The element's vtable slots (0x0064A090).
void VSetProperties(CScreenElement* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CScreenElement*, CStruct*)>(e->m_vtable[1])(e, params);
}
void VGetProperties(CScreenElement* e, CStruct* out)
{
    reinterpret_cast<void(__thiscall*)(CScreenElement*, CStruct*)>(e->m_vtable[8])(e, out);
}
void VSetMorph(CScreenElement* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CScreenElement*, CStruct*)>(e->m_vtable[9])(e, params);
}
void VUpdate(CScreenElement* e) { reinterpret_cast<void(__thiscall*)(CScreenElement*)>(e->m_vtable[10])(e); }

// A state's top left from its position: the justification point (-1..1 across the size) at x, y. Two operand
// orders occur in the original (the x87 products round differently): dims then scale, or scale then dims.
void TopLeftDimsFirst(const CScreenElement* e, SElementState* s)
{
    s->top = static_cast<float>(s->y - (static_cast<double>(e->m_justY) + 1.0) * e->m_height * s->scaleY * 0.5);
    s->left = static_cast<float>(s->x - (static_cast<double>(e->m_justX) + 1.0) * e->m_width * s->scaleX * 0.5);
}
void TopLeftScaleFirst(const CScreenElement* e, SElementState* s, double width, double height)
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
void Rotated(const CScreenElement* parent, float x, float y, double c, double s, float* outX, float* outY)
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
CScreenElement* CScreenElement::Construct()
{
    Obj::CObject::Construct();
    m_vtable = reinterpret_cast<void**>(0x0064A090);
    mp_parent.mp_prev = mp_parent.mp_next = nullptr;
    mp_parent.mp_object = nullptr;
    mp_firstChild.mp_prev = mp_firstChild.mp_next = nullptr;
    mp_firstChild.mp_object = nullptr;
    mp_next.mp_prev = mp_next.mp_next = nullptr;
    mp_next.mp_object = nullptr;
    mp_prev.mp_prev = mp_prev.mp_next = nullptr;
    mp_prev.mp_object = nullptr;
    m_morphLength = 0;
    m_shown.scaleX = m_shown.scaleY = 1.0f;
    m_shown.baseScaleX = m_shown.baseScaleY = 1.0f;
    m_shown.x = m_shown.y = 0.0f;
    m_shown.angle = 0.0f;
    m_shown.pos3d = {0.0f, 0.0f, 0.0f, 1.0f};
    m_shown.rgba[0] = m_shown.rgba[1] = m_shown.rgba[2] = m_shown.rgba[3] = 0x80;
    m_shown.alpha = 1.0f;
    m_target.Assign(&m_shown);
    m_summed.Assign(&m_shown);
    m_objectFlags |= kNew | kDirty;
    m_width = m_height = 0.0f;
    m_rgba168[0] = m_rgba168[1] = m_rgba168[2] = m_rgba168[3] = 0x80;
    m_priority = 0;
    m_zPriority = 0.0f;
    m_rememberedAlpha = 1.0f;
    return this;
}

// 0x004AAB30
void CScreenElement::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x0064A090);
    {
        ElementPtr none;
        SetParent(&none, 0);
    }
    m_id = kDeadBeef;
    mp_prev.Release();
    mp_next.Release();
    mp_firstChild.Release();
    mp_parent.Release();
    Obj::CObject::DestroyBody();
}

// 0x004AAC90
CScreenElement* CScreenElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// --- simple accessors -----------------------------------------------------------------------------------------

// 0x004A8610
void CScreenElement::GetJust(float* x, float* y) const
{
    *x = m_justX;
    *y = m_justY;
}

// 0x004A87C0
void CScreenElement::GetPos(float* x, float* y) const
{
    *x = m_target.x;
    *y = m_target.y;
}

// 0x004A87E0
void CScreenElement::GetTopLeft(float* x, float* y) const
{
    *x = m_target.left;
    *y = m_target.top;
}

// 0x004A86C0
int CScreenElement::GetPriority() const { return m_priority; }

// 0x004A86B0
bool CScreenElement::ManagerUsesPriorities() const { return Front::ManagerUsesPriorities(mp_manager); }

// 0x004A87A0
double CScreenElement::GetLayoutWidth() const { return static_cast<double>(m_width) * m_summed.baseScaleX; }

// 0x004A87B0
double CScreenElement::GetLayoutHeight() const { return static_cast<double>(m_height) * m_summed.baseScaleY; }

// 0x004A8F00
bool CScreenElement::EventsBlocked() const
{
    if (m_objectFlags & kBlockEvents)
        return true;
    return mp_parent.mp_object && mp_parent.mp_object->EventsBlocked();
}

// 0x004A8F20
bool CScreenElement::HasPriority() const
{
    if (m_objectFlags & kPriority)
        return true;
    return mp_parent.mp_object && mp_parent.mp_object->HasPriority();
}

// 0x004A8F40
bool CScreenElement::IsHidden() const
{
    if (m_objectFlags & kHidden)
        return true;
    return mp_parent.mp_object && mp_parent.mp_object->IsHidden();
}

// 0x004A8680
void CScreenElement::SetPaused(bool pause)
{
    if (pause)
        m_objectFlags |= kPauseAsked;
    else
        m_objectFlags &= ~(kPauseAsked | kPaused);
}

// 0x004A8940
void CScreenElement::SetFocusSent(int sent)
{
    int now = (m_objectFlags & kFocusSent) ? 1 : 0;
    if (sent == now)
        return;
    if (sent == 1)
        m_objectFlags |= kFocusSent;
    else
        m_objectFlags &= ~kFocusSent;
    LaunchEvent(sent == 1 ? kFocusSentEvent : kFocusClearedEvent, m_id);
}

// --- placement ------------------------------------------------------------------------------------------------

// 0x004A8C30
void CScreenElement::PlaceTopLeft(SElementState* s) { TopLeftDimsFirst(this, s); }

// 0x004A9100
void CScreenElement::SetPos(float x, float y, int now)
{
    if (x != m_target.x || y != m_target.y) {
        m_target.x = x;
        m_target.y = y;
        m_objectFlags |= kDirty;
        TopLeftDimsFirst(this, &m_target);
    }
    if (now && (x != m_shown.x || y != m_shown.y)) {
        m_shown.x = x;
        m_shown.y = y;
        m_objectFlags |= kDirty;
    }
}

// 0x004A91F0
void CScreenElement::SetPos3d(const Mth::Vector* pos, int now)
{
    if (m_target.pos3d != *pos) {
        m_target.pos3d = *pos;
        m_objectFlags |= kDirty;
        TopLeftDimsFirst(this, &m_target);
    }
    if (now && m_shown.pos3d != *pos) {
        m_shown.pos3d = *pos;
        m_objectFlags |= kDirty;
    }
    m_objectFlags |= kPos3d;
}

// 0x004A92D0
void CScreenElement::SetDims(float w, float h, int lock)
{
    if (w != m_width || h != m_height) {
        m_objectFlags |= kDirty;
        m_width = w;
        m_height = h;
        TopLeftScaleFirst(this, &m_target, w, h);
    }
    if (lock)
        m_objectFlags |= kDimsLocked;
    else
        m_objectFlags &= ~kDimsLocked;
}

// 0x004A9390: the position moves by the justification's change so the element stays where it was.
void CScreenElement::SetJust(float x, float y)
{
    if (x == m_justX && y == m_justY)
        return;
    double dx = (static_cast<double>(x) - m_justX) * m_shown.scaleX * m_width * 0.5;
    double dy = (static_cast<double>(y) - m_justY) * m_shown.scaleY * m_height * 0.5;
    float shownY = m_shown.y;
    m_shown.x = static_cast<float>(dx + m_shown.x);
    m_shown.y = static_cast<float>(shownY + dy);
    float targetY = m_target.y;
    m_target.x = static_cast<float>(dx + m_target.x);
    m_target.y = static_cast<float>(targetY + dy);
    m_justX = x;
    m_justY = y;
    m_objectFlags |= kDirty;
    m_target.top = static_cast<float>(m_target.y - (static_cast<double>(y) + 1.0) * m_target.scaleY * m_height * 0.5);
    m_target.left = static_cast<float>(m_target.x - (static_cast<double>(x) + 1.0) * m_target.scaleX * m_width * 0.5);
}

// 0x004A94A0
void CScreenElement::SetScale(float x, float y, bool relative, int now)
{
    bool same = x == m_target.scaleX && y == m_target.scaleY;
    if (!same || relative) {
        if (relative) {
            x = static_cast<float>(static_cast<double>(x) * m_target.baseScaleX);
            y = static_cast<float>(static_cast<double>(y) * m_target.baseScaleY);
        } else {
            m_target.baseScaleX = x;
            m_target.baseScaleY = y;
        }
        m_target.scaleX = x;
        m_target.scaleY = y;
        TopLeftDimsFirst(this, &m_target);
        m_objectFlags |= kDirty;
    }
    if (now && (x != m_shown.scaleX || y != m_shown.scaleY)) {
        m_shown.scaleX = x;
        m_shown.scaleY = y;
        m_objectFlags |= kDirty;
    }
}

// 0x004A8630
void CScreenElement::SetAlpha(float alpha, int now)
{
    if (alpha != m_target.alpha) {
        m_target.alpha = alpha;
        m_objectFlags |= kDirty;
    }
    if (now && alpha != m_shown.alpha) {
        m_shown.alpha = alpha;
        m_objectFlags |= kDirty;
    }
}

// 0x004A8800 (the angle is compared unrounded, stored as a float)
void CScreenElement::SetRotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
    if (radians != m_target.angle) {
        m_target.angle = static_cast<float>(radians);
        m_objectFlags |= kDirty;
    }
    if (now && radians != m_shown.angle) {
        m_shown.angle = static_cast<float>(radians);
        m_objectFlags |= kDirty;
    }
}

// 0x004A88C0 (the shown colour is set without comparing)
void CScreenElement::SetRGBA(uint32_t rgba, int now)
{
    if (Rgba(m_target.rgba) != rgba) {
        SetRgbaBytes(m_target.rgba, rgba);
        m_objectFlags |= kDirty;
    }
    if (now) {
        SetRgbaBytes(m_shown.rgba, rgba);
        m_objectFlags |= kDirty;
    }
}

// 0x004A8870
void CScreenElement::StartMorph(int ms)
{
    m_morphStart = Tmr::GetTime();
    m_morphLength = ms;
    m_morphCurveDone = 0.0f;
    m_morphDone = 0.0f;
    if (ms == 0)
        m_shown.Assign(&m_target);
    m_objectFlags = (m_objectFlags & ~(kPauseAsked | kPaused)) | kDirty;
}

// 0x004AA390
void CScreenElement::SetZPriority(float z)
{
    if (!(m_objectFlags & kZPriority)) {
        m_objectFlags |= kVisibleNow;
        m_zPriority = z;
    }
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    if (child) {
        float next = static_cast<float>(static_cast<double>(z) + 1.0);
        while (child) {
            child->SetZPriority(next);
            child.Set(child->mp_next.mp_object);
        }
    }
}

// 0x004AA470
void CScreenElement::SetChildrenPriority(int priority)
{
    m_objectFlags |= kPriority;
    m_priority = priority;
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    while (child) {
        child->SetChildrenPriority(priority);
        child.Set(child->mp_next.mp_object);
    }
}

// --- the tree -------------------------------------------------------------------------------------------------

// 0x004A8B80
ElementPtr* CScreenElement::GetParent(ElementPtr* out) const
{
    out->mp_prev = nullptr;
    out->mp_next = nullptr;
    out->mp_object = mp_parent.mp_object;
    if (out->mp_object)
        reinterpret_cast<Obj::CRefCounted*>(out->mp_object)
            ->AddSmartPointer(reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>*>(out));
    return out;
}

// 0x004A8BB0
CScreenElement* CScreenElement::GetWindow() const
{
    CScreenElement* element;
    {
        ElementPtr parent;
        element = GetParent(&parent)->mp_object;
    }
    while (element) {
        if (element->m_type == kWindowElement)
            return element;
        ElementPtr parent;
        element = element->GetParent(&parent)->mp_object;
    }
    return nullptr;
}

// 0x004A95E0
CScreenElement* CScreenElement::GetLastChild() const
{
    CScreenElement* child = mp_firstChild.mp_object;
    if (!child)
        return nullptr;
    while (child->mp_next.mp_object)
        child = child->mp_next.mp_object;
    return child;
}

// 0x004A9600
int CScreenElement::CountChildren() const
{
    int count = 0;
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    while (child) {
        child.Set(child->mp_next.mp_object);
        ++count;
    }
    return count;
}

// 0x004AA2A0
ElementPtr* CScreenElement::GetChild(ElementPtr* out, uint32_t id, int* index)
{
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    for (int i = 0; child; ++i) {
        if (id == 0) {
            if (*index == i)
                break;
        } else if (child->m_id == id) {
            if (index)
                *index = i;
            break;
        }
        child.Set(child->mp_next.mp_object);
    }
    out->mp_prev = nullptr;
    out->mp_next = nullptr;
    out->mp_object = nullptr;
    out->Set(child.mp_object);
    return out;
}

// 0x004AA690
CScreenElement* CScreenElement::GetChildById(uint32_t id, int* index)
{
    ElementPtr child;
    return GetChild(&child, id, index)->mp_object;
}

// 0x004AA6D0
CScreenElement* CScreenElement::GetChildByIndex(int index)
{
    ElementPtr child;
    return GetChild(&child, 0, &index)->mp_object;
}

// 0x004AA8E0
void CScreenElement::SetParent(const ElementPtr* parent, int)
{
    if (mp_parent) {
        // out of the old parent's children
        ElementPtr child;
        child.Set(mp_parent->mp_firstChild.mp_object);
        while (child && child.mp_object != this)
            child.Set(child->mp_next.mp_object);
        if (child) {
            if (mp_prev)
                mp_prev->mp_next.Set(mp_next.mp_object);
            else
                mp_parent->mp_firstChild.Set(mp_next.mp_object);
            if (mp_next)
                mp_next->mp_prev.Set(mp_prev.mp_object);
        }
    }
    mp_parent.Set(parent->mp_object);
    if (mp_parent) {
        // last among the new parent's children
        ElementPtr last;
        last.Set(mp_parent->mp_firstChild.mp_object);
        ElementPtr* slot;
        if (last) {
            while (last->mp_next)
                last.Set(last->mp_next.mp_object);
            slot = &last->mp_next;
        } else {
            slot = &mp_parent->mp_firstChild;
        }
        slot->Set(this);
        mp_prev.Set(last.mp_object);
    } else {
        mp_prev.Release();
    }
    mp_next.Release();
    if (CScreenElement* newParent = parent->mp_object) {
        if (newParent->HasPriority())
            SetChildrenPriority(newParent->m_priority);
        SetZPriority(static_cast<float>(static_cast<double>(parent->mp_object->m_zPriority) + 1.0));
    } else {
        SetZPriority(0.0f);
    }
}

// --- the layout pass ------------------------------------------------------------------------------------------

// 0x004A9D30
void CScreenElement::UpdateLayout()
{
    int now = Tmr::GetTime();
    ++m_aliasCount; // (the object's use count while the pass runs)
    if (!(m_objectFlags & kFocusSent)) {
        m_objectFlags |= kFocusSent;
        LaunchEvent(kFocusSentEvent, m_id);
    }
    bool changed = false;
    if (m_objectFlags & kPos3d) {
        // projected through the first viewport's camera (0x004A06D0; 0x004A1090 calls its vtable slot 1: the
        // depth scale in ST0, negative behind the camera)
        void* camera = GameFunction<void*(__cdecl*)(int)>(0x004A06D0)(0);
        float x, y, z;
        float scale = GameFunction<float(__thiscall*)(void*, const Mth::Vector*, float*, float*, float*)>(0x004A1090)(
            camera, &m_target.pos3d, &x, &y, &z);
        if (scale < 0.0f) {
            m_target.x = -320.0f;
            m_target.y = -224.0f;
            m_objectFlags |= kOffScreen;
        } else {
            m_target.x = x;
            m_target.y = y;
            m_target.z = z;
            if (!(m_objectFlags & kKeepScale))
                SetScale(scale, scale, true, 0);
            m_objectFlags &= ~kOffScreen;
        }
        TopLeftScaleFirst(this, &m_target, m_width, m_height);
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
            s.x = static_cast<float>((static_cast<double>(m_target.x) - s.x) * k + s.x);
            s.y = static_cast<float>((static_cast<double>(m_target.y) - shownY) * k + shownY);
            float shownScaleY = s.scaleY;
            s.scaleX = static_cast<float>((static_cast<double>(m_target.scaleX) - s.scaleX) * kt + s.scaleX);
            s.scaleY = static_cast<float>((static_cast<double>(m_target.scaleY) - shownScaleY) * kt + shownScaleY);
            s.angle = static_cast<float>((static_cast<double>(m_target.angle) - s.angle) * kt + s.angle);
            s.alpha = static_cast<float>((static_cast<double>(m_target.alpha) - s.alpha) * kt + s.alpha);
            PlaceTopLeft(&s);
            uint8_t rgba[4];
            for (int i = 0; i < 4; ++i) {
                int from = s.rgba[i];
                rgba[i] = static_cast<uint8_t>(
                    Truncated(static_cast<double>(m_target.rgba[i] - from) * kt + static_cast<double>(from)));
            }
            std::memcpy(s.rgba, rgba, 4);
            m_morphCurveDone = curve;
            m_morphDone = t;
        } else {
            m_shown.Assign(&m_target);
            m_objectFlags &= ~kDirty;
        }
        changed = true;
    }
    CScreenElement* parent = mp_parent.mp_object;
    if (parent && (parent->m_objectFlags & kChildrenDirty))
        changed = true;
    m_objectFlags &= ~kChildrenDirty;
    if (changed) {
        if (parent && !(m_objectFlags & kPos3d)) {
            m_summed.scaleX = static_cast<float>(static_cast<double>(parent->m_summed.scaleX) * m_shown.scaleX);
            m_summed.scaleY = static_cast<float>(static_cast<double>(parent->m_summed.scaleY) * m_shown.scaleY);
            parent = mp_parent.mp_object;
            m_summed.angle = static_cast<float>(static_cast<double>(m_shown.angle) + parent->m_summed.angle);
            double c = X87Cos(parent->m_summed.angle);
            double s = X87Sin(parent->m_summed.angle);
            Rotated(parent, m_shown.x, m_shown.y, c, s, &m_summed.x, &m_summed.y);
            parent = mp_parent.mp_object;
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
    for (CScreenElement* child = mp_firstChild.mp_object; child; child = child->mp_next.mp_object)
        child->UpdateLayout();
    if (static_cast<int>(m_aliasCount) > 0)
        --m_aliasCount;
}

// --- parameters -----------------------------------------------------------------------------------------------

// 0x004A8C80 (a Pos structure with neither flag gives true but leaves x, y alone)
bool CScreenElement::ResolvePos(Script::CStruct* params, float* x, float* y)
{
    Script::CPair pair;
    pair.Init();
    CStruct* pos;
    if (params->GetStructure(kPos, &pos, 0)) {
        pos->GetPair(0, &pair, true);
        if (pos->ContainsFlag(kRelative)) {
            *x = static_cast<float>(static_cast<double>(pair.x) + m_target.x);
            *y = static_cast<float>(static_cast<double>(pair.y) + m_target.y);
            return true;
        }
        if (pos->ContainsFlag(kProportional)) {
            *x = static_cast<float>(static_cast<double>(pair.x) * mp_parent->m_width);
            *y = static_cast<float>(static_cast<double>(pair.y) * mp_parent->m_height);
        }
        return true;
    }
    if (params->GetPair(kPos, &pair, false)) {
        *x = pair.x;
        *y = pair.y;
        return true;
    }
    Script::VectorValue v;
    if (params->GetVector(kPos, &v, 0)) {
        *x = v.x;
        *y = v.y;
        return true;
    }
    return false;
}

// 0x004A8DC0
bool CScreenElement::ResolveJust(Script::CStruct* params, uint32_t name, float* x, float* y)
{
    Script::CArray* array;
    if (!params->GetArray(name, &array, 0))
        return false;
    if (array->m_type == Script::ESYMBOLTYPE_FLOAT) {
        *x = array->GetFloat(0);
        *y = array->GetFloat(1);
    } else if (array->m_type == Script::ESYMBOLTYPE_NAME) {
        float just[2];
        for (int i = 0; i < 2; ++i) {
            uint32_t side = array->GetElement(i);
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
bool CScreenElement::ResolveRGBA(Script::CStruct* params, uint32_t name, uint32_t* rgba)
{
    Script::CArray* array;
    if (!params->GetArray(name, &array, 0))
        return false;
    uint32_t count = array->m_size;
    uint32_t value = 0;
    for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(count); ++i)
        value |= (array->GetElement(i) & 0xFF) << ((8 * i) & 31);
    *rgba = value;
    return true;
}

// --- the vtable -----------------------------------------------------------------------------------------------

// 0x004A96B0
void CScreenElement::SetProperties(Script::CStruct* params)
{
    void* manager = mp_manager;
    Script::CPair pair;
    pair.Init();
    Script::VectorValue v;
    if (params->GetPair(kPos, &pair, false))
        SetPos(pair.x, pair.y, 1);
    else if (params->GetVector(kPos, &v, 0))
        SetPos(v.x, v.y, 1);
    Script::VectorValue pos3d;
    if (params->GetVector(kPos3dName, &pos3d, 0)) {
        pos3d.w = 1.0f;
        SetPos3d(reinterpret_cast<const Mth::Vector*>(&pos3d), 1);
    }
    if (params->ContainsFlag(kKeepScaleName))
        m_objectFlags |= kKeepScale;
    Script::CPair dims;
    dims.Init();
    if (params->GetPair(kDims, &dims, false))
        SetDims(dims.x, dims.y, 1);
    uint32_t parentId;
    if (ResolveElementId(manager, params, kParent, &parentId)) {
        ElementPtr parent;
        LookUp(manager, &parent, parentId, 0);
        ElementPtr self;
        self.Set(this);
        ManagerSetParent(manager, &parent, &self, 1);
    }
    float justX = 0.0f, justY = 0.0f;
    if (ResolveJust(params, kJust, &justX, &justY))
        SetJust(justX, justY);
    uint32_t rgba;
    if (ResolveRGBA(params, kRgba, &rgba))
        SetRGBA(rgba, 1);
    float degrees = 0.0f;
    if (params->GetFloat(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * Degrees();
        m_objectFlags |= kDirty;
        m_shown.angle = static_cast<float>(radians);
        m_target.angle = static_cast<float>(radians);
    }
    if (params->ContainsFlag(kFlag1000000Name))
        m_objectFlags |= kFlag1000000;
    if (params->ContainsFlag(kFlipHName))
        m_objectFlags |= kFlipH;
    else
        m_objectFlags &= ~kFlipH;
    if (params->ContainsFlag(kFlipVName))
        m_objectFlags |= kFlipV;
    else
        m_objectFlags &= ~kFlipV;
    float z;
    if (params->GetFloat(kZPriorityName, &z, 0)) {
        m_zPriority = z;
        m_objectFlags |= kZPriority | kVisibleNow;
    }
    if (params->ContainsFlag(kNotFocusable))
        SetChecksumTag(0, kTagNotFocusable);
    else if (params->ContainsFlag(kFocusable))
        RemoveTagFlag(kTagNotFocusable);
    uint32_t localId;
    if (params->GetChecksum(kLocalId, &localId, 0))
        SetChecksumTag(kTagLocalId, localId);
    if (params->ContainsFlag(kUnpause))
        m_objectFlags &= ~(kPauseAsked | kPaused);
    if (params->ContainsFlag(kBlockEventsName))
        m_objectFlags |= kBlockEvents;
    else if (params->ContainsFlag(kUnblockEvents))
        m_objectFlags &= ~kBlockEvents;
    if (params->ContainsFlag(kHide))
        m_objectFlags |= kHidden;
    else if (params->ContainsFlag(kUnHide))
        m_objectFlags = (m_objectFlags & ~kHidden) | kVisibleNow;
    int priority;
    if (params->GetInteger(kPriorityName, &priority, 0) && priority > 0) {
        m_priority = priority;
        m_objectFlags |= kPriority;
    }
    Obj::CObject::SetProperties(params);
}

// 0x004A89B0
bool CScreenElement::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name == kSetProps) {
        VSetProperties(this, params);
        return true;
    }
    if (name == kDoMorph) {
        float seconds;
        if (params->GetFloat(kTime, &seconds, 0))
            script->WaitTime(Truncated(static_cast<double>(seconds) * 1000.0f));
        VSetMorph(this, params);
        return true;
    }
    if (name == kDie) {
        // the parent's focus flag dropped (its event sent) while the element goes, and set again after
        void* manager = Manager();
        ElementPtr parent;
        parent.Set(mp_parent.mp_object);
        if (parent->m_objectFlags & kFocusSent) {
            parent->m_objectFlags &= ~kFocusSent;
            LaunchEvent(kFocusClearedEvent, parent->m_id);
        }
        ManagerDestroyElement(manager, m_id, 1, 0, script);
        if (!(parent->m_objectFlags & kFocusSent)) {
            parent->m_objectFlags |= kFocusSent;
            LaunchEvent(kFocusSentEvent, parent->m_id);
        }
        return true;
    }
    if (name == kGetProps) {
        VGetProperties(this, script->m_params);
        return true;
    }
    return Obj::CObject::CallMemberFunction(name, params, script);
}

// 0x004AA710
void CScreenElement::GetProperties(Script::CStruct* out)
{
    out->AddFloat(kAlpha, m_shown.alpha);
    uint8_t rgba[4];
    std::memcpy(rgba, m_shown.rgba, 4);
    Script::CArray* array = Script::NewArray();
    if (array)
        array = array->Init();
    array->SetSizeAndType(4, Script::ESYMBOLTYPE_INTEGER);
    for (int i = 0; i < 4; ++i)
        array->SetElement(i, rgba[i]);
    out->AddArray(kRgba, array);
    Script::CleanUpArray(array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
    float scaleX = m_shown.scaleX, scaleY = m_shown.scaleY;
    if (scaleX == scaleY)
        out->AddFloat(kScale, scaleX);
    else
        out->AddPair(kScale, scaleX, scaleY);
    out->AddPair(kPos, m_shown.x, m_shown.y);
    out->AddFloat(kRotAngle, static_cast<float>(static_cast<double>(m_shown.angle) * RadiansToDegrees()));
    out->AddInteger(kEventsBlocked, EventsBlocked() ? 1 : 0);
}

// 0x004A9AA0
void CScreenElement::SetMorph(Script::CStruct* params)
{
    // (ResolvePos may leave them: the original's y is its argument's slot - the params' address -, x a stale local)
    float x = 0.0f, y;
    std::memcpy(&y, &params, sizeof(y));
    if (ResolvePos(params, &x, &y))
        SetPos(x, y, 0);
    if (params->ContainsFlag(kRestoreAlpha)) {
        float alpha = m_rememberedAlpha;
        if (alpha != m_target.alpha) {
            m_target.alpha = alpha;
            m_objectFlags |= kDirty;
        }
    }
    float alpha;
    if (params->GetFloat(kAlpha, &alpha, 0)) {
        if (params->ContainsFlag(kRememberAlpha))
            m_rememberedAlpha = m_shown.alpha;
        if (alpha != m_target.alpha) {
            m_target.alpha = alpha;
            m_objectFlags |= kDirty;
        }
    }
    bool relative = params->ContainsFlag(kRelativeScale);
    float scale;
    if (params->GetFloat(kScale, &scale, 0))
        SetScale(scale, scale, relative, 0);
    Script::CPair pair;
    pair.Init();
    if (params->GetPair(kScale, &pair, false))
        SetScale(pair.x, pair.y, relative, 0);
    float degrees;
    if (params->GetFloat(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
        if (radians != m_target.angle) {
            m_target.angle = static_cast<float>(radians);
            m_objectFlags |= kDirty;
        }
    }
    float seconds;
    int ms = params->GetFloat(kTime, &seconds, 0) ? Truncated(static_cast<double>(seconds) * 1000.0f) : 0;
    StartMorph(ms);
    m_objectFlags &= ~kMorphCurve;
    uint32_t anim = 0;
    if (params->GetChecksum(kAnim, &anim, 0)) {
        if (anim == kEaseOut)
            m_objectFlags |= 0x100;
        else if (anim == kFastIn)
            m_objectFlags |= 0x200;
        else if (anim == kGentle)
            m_objectFlags |= 0x300;
    }
    uint32_t rgba;
    if (ResolveRGBA(params, kRgba, &rgba) && Rgba(m_target.rgba) != rgba) {
        SetRgbaBytes(m_target.rgba, rgba);
        m_objectFlags |= kDirty;
    }
}

void GetElement(ElementPtr* out, uint32_t id) { LookUp(Manager(), out, id, 0); }

} // namespace Front

using Front::CScreenElement;
using Front::SElementState;

REPLACE(0x004A8730, &SElementState::Assign);
REPLACE(0x004A8F90, &CScreenElement::Construct);
REPLACE(0x004AAB30, &CScreenElement::DestroyBody);
REPLACE(0x004AAC90, &CScreenElement::Destroy);
REPLACE(0x004A8610, &CScreenElement::GetJust);
REPLACE(0x004A87C0, &CScreenElement::GetPos);
REPLACE(0x004A87E0, &CScreenElement::GetTopLeft);
REPLACE_SOURCE_ONLY(0x004A86C0, &CScreenElement::GetPriority); // 7 bytes
REPLACE(0x004A86B0, &CScreenElement::ManagerUsesPriorities);
REPLACE(0x004A87A0, &CScreenElement::GetLayoutWidth);
REPLACE(0x004A87B0, &CScreenElement::GetLayoutHeight);
REPLACE(0x004A8F00, &CScreenElement::EventsBlocked);
REPLACE(0x004A8F20, &CScreenElement::HasPriority);
REPLACE(0x004A8F40, &CScreenElement::IsHidden);
REPLACE(0x004A8680, &CScreenElement::SetPaused);
REPLACE(0x004A8940, &CScreenElement::SetFocusSent);
REPLACE(0x004A8C30, &CScreenElement::PlaceTopLeft);
REPLACE(0x004A9100, &CScreenElement::SetPos);
REPLACE(0x004A91F0, &CScreenElement::SetPos3d);
REPLACE(0x004A92D0, &CScreenElement::SetDims);
REPLACE(0x004A9390, &CScreenElement::SetJust);
REPLACE(0x004A94A0, &CScreenElement::SetScale);
REPLACE(0x004A8630, &CScreenElement::SetAlpha);
REPLACE(0x004A8800, &CScreenElement::SetRotation);
REPLACE(0x004A88C0, &CScreenElement::SetRGBA);
REPLACE(0x004A8870, &CScreenElement::StartMorph);
REPLACE(0x004AA390, &CScreenElement::SetZPriority);
REPLACE(0x004AA470, &CScreenElement::SetChildrenPriority);
REPLACE(0x004A8B80, &CScreenElement::GetParent);
REPLACE(0x004A8BB0, &CScreenElement::GetWindow);
REPLACE(0x004A95E0, &CScreenElement::GetLastChild);
REPLACE(0x004A9600, &CScreenElement::CountChildren);
REPLACE(0x004AA2A0, &CScreenElement::GetChild);
REPLACE(0x004AA690, &CScreenElement::GetChildById);
REPLACE(0x004AA6D0, &CScreenElement::GetChildByIndex);
REPLACE(0x004AA8E0, &CScreenElement::SetParent);
REPLACE(0x004A9D30, &CScreenElement::UpdateLayout);
REPLACE(0x004A8C80, &CScreenElement::ResolvePos);
REPLACE(0x004A8DC0, &CScreenElement::ResolveJust);
REPLACE(0x004A8E90, &CScreenElement::ResolveRGBA);
REPLACE(0x004A96B0, &CScreenElement::SetProperties);
REPLACE(0x004A89B0, &CScreenElement::CallMemberFunction);
REPLACE(0x004AA710, &CScreenElement::GetProperties);
REPLACE(0x004A9AA0, &CScreenElement::SetMorph);
REPLACE_SOURCE_ONLY(0x004AEE50, &CScreenElement::SetMorph); // a jump to 0x004A9AA0 (the texture element's slot 9)
