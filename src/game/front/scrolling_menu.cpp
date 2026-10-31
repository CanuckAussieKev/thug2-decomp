// The scrolling menus: see scrolling_menu.h.

#include <cstddef>
#include <cstdint>

#include "game/front/scrolling_menu.h"

#include "game/core/checksums.h"
#include "game/core/mem.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element_manager.h"
#include "game/obj/tracker.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/obj/entity.h"
#include "game/core/stubs.h"
#include "game/front/screen_element.h"

namespace MenuSystem {

namespace {
extern void** const kListenerAdapterVtable;
extern void** const kCScrollingMenuVtable;
extern void** const kListenerAdapterVtableA180;
extern void** const kCScrollingMenuVtableA188;
extern void** const kListenerAdapterVtableA1B4;
extern void** const kCScrollingMenuVtableA1BC;
} // namespace

namespace {

using Scripts::CStruct;

constexpr uint32_t kVScrollingMenu = 0x84298A3C;
constexpr uint32_t kInternalJust = 0x67E093E4, kScrollSpeed = 0x9020A1D1, kNumItemsToShow = 0xC855511C;
constexpr uint32_t kResetWindowTop = 0xED889E1F, kResetWindowBottom = 0xC3A894F6, kRearrange = 0xA1F3B8BB;
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;

// The menus' type names, hashed at run time (the strings 0x0064A1F0 "vmenu" / 0x0064A1E8 "hmenu").
uint32_t VMenuType() { return Checksums::HashFromText("vmenu"); }
uint32_t HMenuType() { return Checksums::HashFromText("hmenu"); }

void VUpdate(CUiWidget* e) { reinterpret_cast<void(__thiscall*)(CUiWidget*)>(e->m_vtable[10])(e); }

void SetEventConsumed(GameObjects::CSignal* event, uint32_t how, uint32_t script) { event->SetEventConsumed(how, script); }

CUiTree* ManagerOf(const CUiWidget* e) { return static_cast<CUiTree*>(e->m_parkOwner); }

// With the manager's pad-event priorities, only the top priority element may have a pad event.
bool MayHave(CUiWidget* e, GameObjects::CSignal* event)
{
    CUiTree* manager = ManagerOf(e);
    if (e->ManagerUsesPriorities() && manager->IsPadEvent(event->m_kindTag))
        return e->HasPriority() && manager->IsTopPriority(e->m_ident);
    return true;
}

} // namespace

// 0x004AE050
CScrollingMenu* CScrollingMenu::MakeNew()
{
    CUiWidget::MakeNew();
    m_listener.MakeNew();
    m_vtable = kCScrollingMenuVtable;
    m_listener.m_vtable = kListenerAdapterVtable;
    m_listener.EnterWaitBoard(this);
    mp_menu = nullptr;
    m_selectedId = 0;
    m_focused = false;
    m_rearrange = false;
    m_windowStart = 0.0f;
    m_scrollSpeed = 10000.0f;
    m_numItemsToShow = -1;
    m_windowLength = 0.0f;
    m_regularSpace = -1.0f;
    ChangeSpecies(kVScrollingMenu);
    return this;
}

// 0x004AE170
CScrollingMenu* CScrollingMenu::ConstructVertical()
{
    MakeNew();
    m_vtable = kCScrollingMenuVtableA188;
    m_listener.m_vtable = kListenerAdapterVtableA180;
    m_vertical = true;
    return this;
}

// 0x004AE1C0
CScrollingMenu* CScrollingMenu::ConstructHorizontal()
{
    MakeNew();
    m_vtable = kCScrollingMenuVtableA1BC;
    m_listener.m_vtable = kListenerAdapterVtableA1B4;
    m_vertical = false;
    return this;
}

// 0x004AE110
void CScrollingMenu::DestroyBody()
{
    m_vtable = kCScrollingMenuVtable;
    m_listener.m_vtable = kListenerAdapterVtable;
    m_listener.Teardown();
    CUiWidget::DestroyBody();
}

// 0x004AE1A0
void CScrollingMenu::DestroyBodyVertical()
{
    m_vtable = kCScrollingMenuVtableA188;
    m_listener.m_vtable = kListenerAdapterVtableA180;
    DestroyBody();
}

// 0x004AE1F0
void CScrollingMenu::DestroyBodyHorizontal()
{
    m_vtable = kCScrollingMenuVtableA1BC;
    m_listener.m_vtable = kListenerAdapterVtableA1B4;
    DestroyBody();
}

// 0x004AE240
CScrollingMenu* CScrollingMenu::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AE270
CScrollingMenu* CScrollingMenu::DestroyVertical(uint8_t flags)
{
    DestroyBodyVertical();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AE2A0
CScrollingMenu* CScrollingMenu::DestroyHorizontal(uint8_t flags)
{
    DestroyBodyHorizontal();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AE2D0
void CScrollingMenu::FindMenu()
{
    {
        ElementPtr child;
        child.AssignValue(mp_firstChild.m_ownerObject);
        while (child) {
            if (child->m_kindTag == VMenuType() && m_vertical)
                mp_menu = child.m_ownerObject;
            else if (child->m_kindTag == HMenuType() && !m_vertical)
                mp_menu = child.m_ownerObject;
            child.AssignValue(child->m_following.m_ownerObject);
        }
    }
    float space = -1.0f;
    if (static_cast<CMenuBase*>(mp_menu)->GetRegularSpace(&space) && m_numItemsToShow >= 0) {
        m_regularSpace = space;
        m_windowLength = static_cast<float>(static_cast<double>(m_numItemsToShow) * space);
    } else {
        m_numItemsToShow = -1;
        m_windowLength = m_vertical ? m_levelY : m_span;
    }
}

// 0x004AE450 (the item's justification is read into the original's argument slots - no effect)
void CScrollingMenu::ItemPlace(ElementPtr* item, float* x, float* y, float* w, float* h)
{
    item->m_ownerObject->FetchLocation(x, y);
    *x = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleX) * *x);
    *y = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleY) * *y);
    *w = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleX) * item->m_ownerObject->m_span *
                            item->m_ownerObject->m_shown.scaleX);
    *h = static_cast<float>(static_cast<double>(mp_menu->m_shown.scaleY) * item->m_ownerObject->m_levelY *
                            item->m_ownerObject->m_shown.scaleY);
    float alignH, alignV;
    item->m_ownerObject->FetchAlignment(&alignH, &alignV);
    *x = static_cast<float>(*x - (static_cast<double>(alignH) + 1.0) * *w * 0.5);
    *y = static_cast<float>(*y - (static_cast<double>(alignV) + 1.0) * *h * 0.5);
}

// 0x004AE520 (reset_window_bottom: the last item's bottom - the vertical place, also for the horizontal one)
void CScrollingMenu::ChangeTraits(Scripts::CStruct* params)
{
    float alignH = 0.0f, alignV = 0.0f;
    if (ResolveJust(params, kInternalJust, &alignH, &alignV)) {
        m_internalJustX = alignH;
        m_internalJustY = alignV;
    }
    int speed;
    if (params->FetchIntValue(kScrollSpeed, &speed, 0))
        m_scrollSpeed = static_cast<float>(speed);
    params->FetchIntValue(kNumItemsToShow, &m_numItemsToShow, 0);
    if (params->HasBareFlag(kResetWindowTop)) {
        m_windowStart = 0.0f;
        m_rearrange = true;
    } else if (params->HasBareFlag(kResetWindowBottom)) {
        ElementPtr last;
        last.AssignValue(mp_menu->FetchYoungest());
        float x, y, w, h;
        ItemPlace(&last, &x, &y, &w, &h);
        m_rearrange = true;
        m_windowStart = static_cast<float>(static_cast<double>(y) + h - m_windowLength);
    } else if (params->HasBareFlag(kRearrange)) {
        m_rearrange = true;
    }
    CUiWidget::ChangeTraits(params);
}

// 0x004AE6A0
bool CScrollingMenu::PassEventToScript(GameObjects::CSignal* event)
{
    if (!MayHave(this, event) || !GameObjects::CEntity::PassEventToScript(event))
        return false;
    GameObjects::CEventWaitBoard* tracker = GameObjects::Tracker();
    if (event->m_kindTag == kFocusSentEvent) {
        FindMenu();
        m_rearrange = true;
        VUpdate(this);
        event->SetEventHandled(m_ident, 0);
    }
    if (event->m_kindTag == kFocus) {
        if (!mp_menu)
            FindMenu();
        if (!m_focused) {
            m_focused = true;
            tracker->LaunchEvent(kFocus, mp_menu->m_ident, kSystem, event->m_payload, false, -1.0f);
            event->SetEventHandled(m_ident, 0);
        }
    }
    if (event->m_kindTag == kUnfocus && m_focused) {
        FindMenu();
        m_focused = false;
        tracker->LaunchEvent(kUnfocus, mp_menu->m_ident, kSystem, nullptr, false, -1.0f);
        event->SetEventHandled(m_ident, 0);
    }
    return true;
}

// 0x004AE7E0
void CScrollingMenu::ListenerReceive(GameObjects::CSignal* event)
{
    // (`this` is the listener: the element is 0x178 before it)
    auto* self = reinterpret_cast<CScrollingMenu*>(reinterpret_cast<uint8_t*>(this) - offsetof(CScrollingMenu, m_listener));
    if (!MayHave(self, event))
        return;
    if (event->m_kindTag == kFocusSentEvent && self->mp_menu) {
        if (event->m_aimPoint == self->mp_menu->m_ident) {
            self->m_rearrange = true;
            VUpdate(self);
        }
        SetEventConsumed(event, self->m_ident, 0);
    }
    if (event->m_kindTag == kFocus) {
        if (!self->mp_menu)
            self->FindMenu();
        ElementPtr item;
        item.AssignValue(self->mp_menu->mp_firstChild.m_ownerObject);
        while (item) {
            if (item->m_ident == event->m_aimPoint) {
                self->m_selectedId = item->m_ident;
                self->m_rearrange = true;
                SetEventConsumed(event, self->m_ident, 0);
                break;
            }
            item.AssignValue(item->m_following.m_ownerObject);
        }
    }
}

// 0x004AE960
void CScrollingMenu::TickFrame()
{
    if (!mp_menu)
        return;
    int index = 0;
    float before = 0.0f, after = 0.0f; // the window's margins before / after the selected item
    ElementPtr selected;
    if (m_selectedId)
        selected.AssignValue(mp_menu->FindChildWithId(m_selectedId, &index));
    else
        selected.AssignValue(mp_menu->FindChildAtSlot(0));
    if (!selected)
        return;
    int count = mp_menu->ChildTotal();
    if (!(m_regularSpace < 0.0f)) {
        // keep the selection in the middle of the shown items when it can be
        int half = m_numItemsToShow / 2;
        if (index >= half)
            before = static_cast<float>(static_cast<double>((m_numItemsToShow - 1) / 2) * m_regularSpace);
        if (index < count - half)
            after = static_cast<float>(static_cast<double>(half) * m_regularSpace);
    }
    mp_menu->ChangeAlignment(-1.0f, -1.0f);
    float menuX, menuY;
    mp_menu->FetchLocation(&menuX, &menuY);
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
        mp_menu->ChangeLocation(menuX, shift, 1);
    else
        mp_menu->ChangeLocation(shift, menuY, 1);
    // the items in the window shown, the others hidden
    index = 0;
    ElementPtr item;
    item.AssignValue(mp_menu->mp_firstChild.m_ownerObject);
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
        item->ChangeOpacity(inside ? 1.0f : 0.0f, 1);
        item.AssignValue(item->m_following.m_ownerObject);
        ++index;
    }
    m_rearrange = false;
}

} // namespace Front

namespace {

// The listener's slot 0 (`this` = the listener at +0x178): the element's destructor.
struct ListenerAdapter {
    MenuSystem::CScrollingMenu* Self() { return reinterpret_cast<MenuSystem::CScrollingMenu*>(reinterpret_cast<uint8_t*>(this) - 0x178); }
    MenuSystem::CScrollingMenu* Teardown(uint8_t flags) { return Self()->Teardown(flags); }
    MenuSystem::CScrollingMenu* DestroyVertical(uint8_t flags) { return Self()->DestroyVertical(flags); }
    MenuSystem::CScrollingMenu* DestroyHorizontal(uint8_t flags) { return Self()->DestroyHorizontal(flags); }
};

} // namespace

using MenuSystem::CScrollingMenu;

namespace MenuSystem {
namespace {
static VtableSlot kListenerAdapterVtable_slots[] = {&ListenerAdapter::Teardown, &CScrollingMenu::ListenerReceive};
void** const kListenerAdapterVtable = reinterpret_cast<void**>(kListenerAdapterVtable_slots);
static VtableSlot kCScrollingMenuVtable_slots[] = {&CScrollingMenu::Teardown, &CScrollingMenu::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &CScrollingMenu::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                &CScrollingMenu::TickFrame};
void** const kCScrollingMenuVtable = reinterpret_cast<void**>(kCScrollingMenuVtable_slots);
static VtableSlot kListenerAdapterVtableA180_slots[] = {&ListenerAdapter::DestroyVertical,
                &CScrollingMenu::ListenerReceive};
void** const kListenerAdapterVtableA180 = reinterpret_cast<void**>(kListenerAdapterVtableA180_slots);
static VtableSlot kCScrollingMenuVtableA188_slots[] = {&CScrollingMenu::DestroyVertical, &CScrollingMenu::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &CScrollingMenu::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                &CScrollingMenu::TickFrame};
void** const kCScrollingMenuVtableA188 = reinterpret_cast<void**>(kCScrollingMenuVtableA188_slots);
static VtableSlot kListenerAdapterVtableA1B4_slots[] = {&ListenerAdapter::DestroyHorizontal,
                &CScrollingMenu::ListenerReceive};
void** const kListenerAdapterVtableA1B4 = reinterpret_cast<void**>(kListenerAdapterVtableA1B4_slots);
static VtableSlot kCScrollingMenuVtableA1BC_slots[] = {&CScrollingMenu::DestroyHorizontal,
                &CScrollingMenu::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &CScrollingMenu::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                &CScrollingMenu::TickFrame};
void** const kCScrollingMenuVtableA1BC = reinterpret_cast<void**>(kCScrollingMenuVtableA1BC_slots);
} // namespace
} // namespace Front
