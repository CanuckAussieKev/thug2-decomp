// The scrolling menus: see scrolling_menu.h.

#include "game/front/scrolling_menu.h"

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element_manager.h"
#include "game/obj/tracker.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

namespace {

using Script::CStruct;

constexpr uint32_t kVScrollingMenu = 0x84298A3C;
constexpr uint32_t kInternalJust = 0x67E093E4, kScrollSpeed = 0x9020A1D1, kNumItemsToShow = 0xC855511C;
constexpr uint32_t kResetWindowTop = 0xED889E1F, kResetWindowBottom = 0xC3A894F6, kRearrange = 0xA1F3B8BB;
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;

void** Vtable(uint32_t address) { return reinterpret_cast<void**>(address); }

// The menus' type names, hashed at run time (the strings 0x0064A1F0 "vmenu" / 0x0064A1E8 "hmenu").
uint32_t VMenuType() { return Crc::GenerateCRCFromString(reinterpret_cast<const char*>(0x0064A1F0)); }
uint32_t HMenuType() { return Crc::GenerateCRCFromString(reinterpret_cast<const char*>(0x0064A1E8)); }

void VUpdate(CScreenElement* e) { reinterpret_cast<void(__thiscall*)(CScreenElement*)>(e->m_vtable[10])(e); }

void MarkRead(Obj::CEvent* event, uint32_t how, uint32_t script) { event->MarkRead(how, script); }

CScreenElementManager* ManagerOf(const CScreenElement* e) { return static_cast<CScreenElementManager*>(e->mp_manager); }

// With the manager's pad-event priorities, only the top priority element may have a pad event.
bool MayHave(CScreenElement* e, Obj::CEvent* event)
{
    CScreenElementManager* manager = ManagerOf(e);
    if (e->ManagerUsesPriorities() && manager->IsPadEvent(event->m_type))
        return e->HasPriority() && manager->IsTopPriority(e->m_id);
    return true;
}

} // namespace

// 0x004AE050
CScrollingMenu* CScrollingMenu::Construct()
{
    CScreenElement::Construct();
    m_listener.Construct();
    m_vtable = Vtable(0x0064A154);
    m_listener.m_vtable = Vtable(0x0064A14C);
    m_listener.RegisterWithTracker(this);
    mp_menu = nullptr;
    m_selectedId = 0;
    m_focused = false;
    m_rearrange = false;
    m_windowStart = 0.0f;
    m_scrollSpeed = 10000.0f;
    m_numItemsToShow = -1;
    m_windowLength = 0.0f;
    m_regularSpace = -1.0f;
    SetType(kVScrollingMenu);
    return this;
}

// 0x004AE170
CScrollingMenu* CScrollingMenu::ConstructVertical()
{
    Construct();
    m_vtable = Vtable(0x0064A188);
    m_listener.m_vtable = Vtable(0x0064A180);
    m_vertical = true;
    return this;
}

// 0x004AE1C0
CScrollingMenu* CScrollingMenu::ConstructHorizontal()
{
    Construct();
    m_vtable = Vtable(0x0064A1BC);
    m_listener.m_vtable = Vtable(0x0064A1B4);
    m_vertical = false;
    return this;
}

// 0x004AE110
void CScrollingMenu::DestroyBody()
{
    m_vtable = Vtable(0x0064A154);
    m_listener.m_vtable = Vtable(0x0064A14C);
    m_listener.Destroy();
    CScreenElement::DestroyBody();
}

// 0x004AE1A0
void CScrollingMenu::DestroyBodyVertical()
{
    m_vtable = Vtable(0x0064A188);
    m_listener.m_vtable = Vtable(0x0064A180);
    DestroyBody();
}

// 0x004AE1F0
void CScrollingMenu::DestroyBodyHorizontal()
{
    m_vtable = Vtable(0x0064A1BC);
    m_listener.m_vtable = Vtable(0x0064A1B4);
    DestroyBody();
}

// 0x004AE240
CScrollingMenu* CScrollingMenu::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AE270
CScrollingMenu* CScrollingMenu::DestroyVertical(uint8_t flags)
{
    DestroyBodyVertical();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AE2A0
CScrollingMenu* CScrollingMenu::DestroyHorizontal(uint8_t flags)
{
    DestroyBodyHorizontal();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AE2D0
void CScrollingMenu::FindMenu()
{
    {
        ElementPtr child;
        child.Set(mp_firstChild.mp_object);
        while (child) {
            if (child->m_type == VMenuType() && m_vertical)
                mp_menu = child.mp_object;
            else if (child->m_type == HMenuType() && !m_vertical)
                mp_menu = child.mp_object;
            child.Set(child->mp_next.mp_object);
        }
    }
    float space = -1.0f;
    if (static_cast<CBaseMenu*>(mp_menu)->GetRegularSpace(&space) && m_numItemsToShow >= 0) {
        m_regularSpace = space;
        m_windowLength = static_cast<float>(static_cast<double>(m_numItemsToShow) * space);
    } else {
        m_numItemsToShow = -1;
        m_windowLength = m_vertical ? m_height : m_width;
    }
}

// 0x004AE450 (the item's justification is read into the original's argument slots - no effect)
void CScrollingMenu::ItemPlace(ElementPtr* item, float* x, float* y, float* w, float* h)
{
    item->mp_object->GetPos(x, y);
    *x = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleX) * *x);
    *y = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleY) * *y);
    *w = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleX) * item->mp_object->m_width *
                            item->mp_object->m_shown.scaleX);
    *h = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleY) * item->mp_object->m_height *
                            item->mp_object->m_shown.scaleY);
    float justX, justY;
    item->mp_object->GetJust(&justX, &justY);
    *x = static_cast<float>(*x - (static_cast<double>(justX) + 1.0) * *w * 0.5);
    *y = static_cast<float>(*y - (static_cast<double>(justY) + 1.0) * *h * 0.5);
}

// 0x004AE520 (reset_window_bottom: the last item's bottom - the vertical place, also for the horizontal one)
void CScrollingMenu::SetProperties(Script::CStruct* params)
{
    float justX = 0.0f, justY = 0.0f;
    if (ResolveJust(params, kInternalJust, &justX, &justY)) {
        m_internalJustX = justX;
        m_internalJustY = justY;
    }
    int speed;
    if (params->GetInteger(kScrollSpeed, &speed, 0))
        m_scrollSpeed = static_cast<float>(speed);
    params->GetInteger(kNumItemsToShow, &m_numItemsToShow, 0);
    if (params->ContainsFlag(kResetWindowTop)) {
        m_windowStart = 0.0f;
        m_rearrange = true;
    } else if (params->ContainsFlag(kResetWindowBottom)) {
        ElementPtr last;
        last.Set(mp_menu->GetLastChild());
        float x, y, w, h;
        ItemPlace(&last, &x, &y, &w, &h);
        m_rearrange = true;
        m_windowStart = static_cast<float>(static_cast<double>(y) + h - m_windowLength);
    } else if (params->ContainsFlag(kRearrange)) {
        m_rearrange = true;
    }
    CScreenElement::SetProperties(params);
}

// 0x004AE6A0
bool CScrollingMenu::PassEventToScript(Obj::CEvent* event)
{
    if (!MayHave(this, event) || !Obj::CObject::PassEventToScript(event))
        return false;
    Obj::CTracker* tracker = Obj::Tracker();
    if (event->m_type == kFocusSentEvent) {
        FindMenu();
        m_rearrange = true;
        VUpdate(this);
        event->MarkHandled(m_id, 0);
    }
    if (event->m_type == kFocus) {
        if (!mp_menu)
            FindMenu();
        if (!m_focused) {
            m_focused = true;
            tracker->LaunchEvent(kFocus, mp_menu->m_id, kSystem, event->mp_data, false, -1.0f);
            event->MarkHandled(m_id, 0);
        }
    }
    if (event->m_type == kUnfocus && m_focused) {
        FindMenu();
        m_focused = false;
        tracker->LaunchEvent(kUnfocus, mp_menu->m_id, kSystem, nullptr, false, -1.0f);
        event->MarkHandled(m_id, 0);
    }
    return true;
}

// 0x004AE7E0
void CScrollingMenu::ListenerReceive(Obj::CEvent* event)
{
    // (`this` is the listener: the element is 0x178 before it)
    auto* self = reinterpret_cast<CScrollingMenu*>(reinterpret_cast<uint8_t*>(this) - offsetof(CScrollingMenu, m_listener));
    if (!MayHave(self, event))
        return;
    if (event->m_type == kFocusSentEvent && self->mp_menu) {
        if (event->m_target == self->mp_menu->m_id) {
            self->m_rearrange = true;
            VUpdate(self);
        }
        MarkRead(event, self->m_id, 0);
    }
    if (event->m_type == kFocus) {
        if (!self->mp_menu)
            self->FindMenu();
        ElementPtr item;
        item.Set(self->mp_menu->mp_firstChild.mp_object);
        while (item) {
            if (item->m_id == event->m_target) {
                self->m_selectedId = item->m_id;
                self->m_rearrange = true;
                MarkRead(event, self->m_id, 0);
                break;
            }
            item.Set(item->mp_next.mp_object);
        }
    }
}

// 0x004AE960
void CScrollingMenu::Update()
{
    if (!mp_menu)
        return;
    int index = 0;
    float before = 0.0f, after = 0.0f; // the window's margins before / after the selected item
    ElementPtr selected;
    if (m_selectedId)
        selected.Set(mp_menu->GetChildById(m_selectedId, &index));
    else
        selected.Set(mp_menu->GetChildByIndex(0));
    if (!selected)
        return;
    int count = mp_menu->CountChildren();
    if (!(m_regularSpace < 0.0f)) {
        // keep the selection in the middle of the shown items when it can be
        int half = m_numItemsToShow / 2;
        if (index >= half)
            before = static_cast<float>(static_cast<double>((m_numItemsToShow - 1) / 2) * m_regularSpace);
        if (index < count - half)
            after = static_cast<float>(static_cast<double>(half) * m_regularSpace);
    }
    mp_menu->SetJust(-1.0f, -1.0f);
    float menuX, menuY;
    mp_menu->GetPos(&menuX, &menuY);
    float place, length, other1, other2; // along the menu, and across it
    if (m_vertical)
        ItemPlace(&selected, &other1, &place, &other2, &length);
    else
        ItemPlace(&selected, &place, &other1, &length, &other2);
    if (!(m_regularSpace < 0.0f)) {
        length = m_regularSpace;
        place = static_cast<float>(static_cast<double>(index) * m_regularSpace);
    }
    if (static_cast<double>(before) + m_windowStart > place) {
        // scroll back (by scroll_speed), not past the item's margin
        double start = static_cast<double>(m_windowStart) - m_scrollSpeed;
        m_windowStart = static_cast<float>(start);
        if (start < place)
            m_windowStart = static_cast<float>(static_cast<double>(place) - before);
        m_rearrange = true;
    } else {
        float end = static_cast<float>(static_cast<double>(place) + length);
        if (static_cast<double>(m_windowLength) + m_windowStart - after < end) {
            double start = static_cast<double>(m_scrollSpeed) + m_windowStart;
            m_windowStart = static_cast<float>(start);
            if (start + m_windowLength > end)
                m_windowStart = static_cast<float>(static_cast<double>(end) - m_windowLength + after);
            m_rearrange = true;
        }
    }
    if (!m_rearrange)
        return;
    float shift = -m_windowStart;
    if (m_vertical)
        mp_menu->SetPos(menuX, shift, 1);
    else
        mp_menu->SetPos(shift, menuY, 1);
    // the items in the window shown, the others hidden
    index = 0;
    ElementPtr item;
    item.Set(mp_menu->mp_firstChild.mp_object);
    while (item) {
        if (m_vertical)
            ItemPlace(&item, &other1, &place, &other2, &length);
        else
            ItemPlace(&item, &place, &other1, &length, &other2);
        if (!(m_regularSpace < 0.0f)) {
            length = m_regularSpace;
            place = static_cast<float>(static_cast<double>(index) * m_regularSpace);
        }
        bool inside = !(place < m_windowStart) &&
                      !(static_cast<double>(m_windowLength) + m_windowStart < static_cast<double>(place) + length);
        item->SetAlpha(inside ? 1.0f : 0.0f, 1);
        item.Set(item->mp_next.mp_object);
        ++index;
    }
    m_rearrange = false;
}

} // namespace Front

namespace {

// The listener's slot 0 (`this` = the listener at +0x178): the element's destructor.
struct ListenerAdapter {
    Front::CScrollingMenu* Self() { return reinterpret_cast<Front::CScrollingMenu*>(reinterpret_cast<uint8_t*>(this) - 0x178); }
    Front::CScrollingMenu* Destroy(uint8_t flags) { return Self()->Destroy(flags); }
    Front::CScrollingMenu* DestroyVertical(uint8_t flags) { return Self()->DestroyVertical(flags); }
    Front::CScrollingMenu* DestroyHorizontal(uint8_t flags) { return Self()->DestroyHorizontal(flags); }
};

} // namespace

using Front::CScrollingMenu;

REPLACE(0x004AE050, &CScrollingMenu::Construct);
REPLACE(0x004AE170, &CScrollingMenu::ConstructVertical);
REPLACE(0x004AE1C0, &CScrollingMenu::ConstructHorizontal);
REPLACE(0x004AE110, &CScrollingMenu::DestroyBody);
REPLACE(0x004AE1A0, &CScrollingMenu::DestroyBodyVertical);
REPLACE(0x004AE1F0, &CScrollingMenu::DestroyBodyHorizontal);
REPLACE(0x004AE240, &CScrollingMenu::Destroy);
REPLACE(0x004AE270, &CScrollingMenu::DestroyVertical);
REPLACE(0x004AE2A0, &CScrollingMenu::DestroyHorizontal);
REPLACE(0x004AE210, &ListenerAdapter::Destroy);
REPLACE(0x004AE220, &ListenerAdapter::DestroyVertical);
REPLACE(0x004AE230, &ListenerAdapter::DestroyHorizontal);
REPLACE(0x004AE520, &CScrollingMenu::SetProperties);
REPLACE(0x004AE6A0, &CScrollingMenu::PassEventToScript);
REPLACE(0x004AE960, &CScrollingMenu::Update);
REPLACE(0x004AE7E0, &CScrollingMenu::ListenerReceive);
REPLACE(0x004AE2D0, &CScrollingMenu::FindMenu);
REPLACE(0x004AE450, &CScrollingMenu::ItemPlace);
