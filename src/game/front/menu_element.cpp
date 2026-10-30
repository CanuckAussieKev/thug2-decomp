// The menus (vmenu / hmenu) and the container element: see menu_element.h.

#include "game/front/menu_element.h"
#include "game/front/screen_element_manager.h"

#include <cstring>

#include "game/core/mem.h"
#include "game/obj/event.h"
#include "game/obj/tracker.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

namespace {

using Script::CStruct;

// Names (checksums).
constexpr uint32_t kVMenu = 0x130EF802, kHMenu = 0xCCDED1E1, kContainerElement = 0x5B9DA842;
constexpr uint32_t kInternalJust = 0x67E093E4, kRegularSpaceAmount = 0xF24ADEDB, kPaddingScale = 0x6D853B88;
constexpr uint32_t kSpacingBetween = 0x4B4E14AB, kEnablePadHandling = 0x4BD08BFE, kDisablePadHandling = 0xC22541CC;
constexpr uint32_t kAllowWrap = 0x43A7692D, kDontAllowWrap = 0x5DEA5C64, kFocusableChild = 0xF1822B9C;
constexpr uint32_t kTagSelectedId = 0xAC988EBE, kTagSelectedIndex = 0xF2239871, kTagGridIndex = 0x8321DD71;
constexpr uint32_t kTagGridX = 0x5B92F8DD, kTagNotFocusable = 0xF33A3321;
constexpr uint32_t kController = 0xB30D9965, kGridIndex = 0xFACF9A8B, kChildId = 0x229D3DE4;
// Events.
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;
constexpr uint32_t kPadUp = 0x30A4F836, kPadDown = 0x0FD1AC26, kPadLeft = 0x6949DB75, kPadRight = 0x05DDD87C;

float Constant(uint32_t address) { return GameVariable<float>(address); }
float RegularSpaceMinimum() { return Constant(0x006453D4); } // 0.0001

void Launch(uint32_t type, uint32_t target, CStruct* data)
{
    Obj::Tracker()->LaunchEvent(type, target, kSystem, data, false, -1.0f);
}

// The screen element manager (0x00701440) and its pieces.
void* Manager() { return GameVariable<void*>(0x00701440); }
CScreenElementManager* Elements(void* manager) { return static_cast<CScreenElementManager*>(manager); }
bool UsesPriorities(void* manager) { return Elements(manager)->HasPriorities(); }
bool IsPriorityEvent(void* manager, uint32_t type) { return Elements(manager)->IsPadEvent(type); }
bool IsTopPriority(void* manager, uint32_t id) { return Elements(manager)->IsTopPriority(id); }
ElementPtr* LookUp(void* manager, ElementPtr* out, uint32_t id, int assert)
{
    return Elements(manager)->GetElement(out, id, assert);
}


// focus to `id` with the controller (and a grid index, unless -1).
void SendFocus(uint32_t id, int controller, int gridIndex)
{
    CStruct data;
    data.Init();
    data.AddInteger(kController, controller);
    if (gridIndex != -1)
        data.AddInteger(kGridIndex, gridIndex);
    Launch(kFocus, id, &data);
    data.Clear();
}

} // namespace

// --- the menu -------------------------------------------------------------------------------------------------

// 0x004A74E0
CBaseMenu* CBaseMenu::Construct()
{
    CScreenElement::Construct();
    m_selectedId = 0;
    m_focused = false;
    m_controller = 0;
    m_regularSpace = 0.0f;
    m_spacingBetween = 0.0f;
    m_internalJustX = 0.0f;
    m_internalJustY = 0.0f;
    m_padHandling = true;
    m_allowWrap = true;
    m_vtable = reinterpret_cast<void**>(0x0064A048);
    m_selectedIndex = -1;
    m_paddingScale = 1.0f;
    m_gridIndex = -1;
    return this;
}

// 0x004A75C0
CBaseMenu* CBaseMenu::ConstructVMenu()
{
    Construct();
    m_vtable = reinterpret_cast<void**>(0x0064A048);
    m_vertical = true;
    SetType(kVMenu);
    return this;
}

// 0x004A7630
CBaseMenu* CBaseMenu::ConstructHMenu()
{
    Construct();
    m_vtable = reinterpret_cast<void**>(0x0064A048);
    m_vertical = false;
    SetType(kHMenu);
    return this;
}

// 0x004A7620
void CBaseMenu::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x0064A048);
    CScreenElement::DestroyBody();
}

// 0x004A76C0
CBaseMenu* CBaseMenu::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004A7550
int CBaseMenu::GetRegularSpace(float* out) const
{
    *out = m_regularSpace;
    return m_regularSpace > RegularSpaceMinimum() ? 1 : 0;
}

// 0x004A7580
void CBaseMenu::SetSelectionTags()
{
    SetChecksumTag(kTagSelectedId, m_selectedId);
    SetIntegerTag(kTagSelectedIndex, m_selectedIndex);
    SetIntegerTag(kTagGridIndex, m_gridIndex);
}

// 0x004A76F0
void CBaseMenu::SetProperties(Script::CStruct* params)
{
    float justX = 0.0f, justY = 0.0f;
    if (ResolveJust(params, kInternalJust, &justX, &justY)) {
        m_internalJustX = justX;
        m_internalJustY = justY;
    }
    int space = 0;
    if (params->GetInteger(kRegularSpaceAmount, &space, 0))
        m_regularSpace = static_cast<float>(space);
    params->GetFloat(kPaddingScale, &m_paddingScale, 0);
    int between = 0;
    if (params->GetInteger(kSpacingBetween, &between, 0))
        m_spacingBetween = static_cast<float>(between);
    if (params->ContainsFlag(kEnablePadHandling))
        m_padHandling = true;
    else if (params->ContainsFlag(kDisablePadHandling))
        m_padHandling = false;
    if (params->ContainsFlag(kAllowWrap))
        m_allowWrap = true;
    else if (params->ContainsFlag(kDontAllowWrap))
        m_allowWrap = false;
    CScreenElement::SetProperties(params);
}

// 0x004A7850 (each item gets two chances: twice the item count of tries)
void CBaseMenu::MoveSelection(int direction, bool stay, bool gridX)
{
    int tries = CountChildren() * 2;
    if (m_selectedIndex < 0)
        m_selectedIndex = 0;
    while (tries != 0) {
        if (!stay) {
            int index = m_selectedIndex + direction;
            m_selectedIndex = index;
            if (index >= CountChildren()) {
                if (m_allowWrap) {
                    m_selectedIndex = 0;
                } else {
                    direction = -direction;
                    m_selectedIndex = CountChildren() - 1;
                }
            } else if (index < 0) {
                if (m_allowWrap) {
                    m_selectedIndex = CountChildren() - 1;
                } else {
                    direction = -direction;
                    m_selectedIndex = 0;
                }
            }
        }
        stay = false;
        ElementPtr item;
        item.Set(GetChildByIndex(m_selectedIndex));
        m_selectedId = item->m_id;
        if (gridX) {
            m_gridIndex = -1;
            item->GetIntegerTag(kTagGridX, &m_gridIndex);
        }
        if (!item->ContainsTagFlag(kTagNotFocusable) && !item->EventsBlocked()) {
            item.Release();
            if (tries == 0)
                break;
            SetSelectionTags();
            return;
        }
        --tries;
    }
    m_selectedId = 0;
    m_selectedIndex = -1;
}

// 0x004A7A40
void CBaseMenu::Step(int direction)
{
    int gridIndex = -1;
    if (m_selectedId) {
        CScreenElement* item;
        {
            ElementPtr found;
            item = LookUp(Manager(), &found, m_selectedId, 0)->mp_object;
        }
        if (item)
            item->GetIntegerTag(kTagGridIndex, &gridIndex);
        Launch(kUnfocus, m_selectedId, nullptr);
    }
    MoveSelection(direction, false, true);
    if (m_selectedId)
        SendFocus(m_selectedId, m_controller, gridIndex);
}

// 0x004A7B80
void CBaseMenu::Arrange()
{
    float total = 0.0f, largest = 0.0f;
    bool regular = m_regularSpace > RegularSpaceMinimum();
    {
        ElementPtr item;
        item.Set(mp_firstChild.mp_object);
        if (regular) {
            for (; item; item.Set(item->mp_next.mp_object)) {
                double across = m_vertical ? static_cast<double>(item->m_width) * item->m_shown.scaleX
                                           : static_cast<double>(item->m_height) * item->m_shown.scaleY;
                total = static_cast<float>(static_cast<double>(total) + m_regularSpace);
                if (across > largest)
                    largest = static_cast<float>(across);
            }
        } else {
            for (; item; item.Set(item->mp_next.mp_object)) {
                double across, along;
                if (m_vertical) {
                    across = static_cast<double>(item->m_width) * m_paddingScale;
                    along = item->m_height;
                } else {
                    across = static_cast<double>(item->m_height) * m_paddingScale;
                    along = item->m_width;
                }
                total = static_cast<float>(along * m_paddingScale + m_spacingBetween + total);
                if (across > largest)
                    largest = static_cast<float>(across);
            }
            total = static_cast<float>(static_cast<double>(total) - m_spacingBetween);
        }
    }
    if (m_objectFlags & kDimsLocked) {
        // the size only grows
        if (m_vertical) {
            if (total > m_height)
                m_height = total;
        } else if (total > m_width) {
            m_width = total;
        }
    } else if (m_vertical) {
        m_width = largest;
        m_height = total;
    } else {
        m_height = largest;
        m_width = total;
    }
    PlaceTopLeft(&m_target);
    m_objectFlags |= kDirty;
    // where the first item goes: the free space split by internal_just
    float offset = m_vertical
                       ? static_cast<float>((static_cast<double>(m_height) - total) *
                                            (static_cast<double>(m_internalJustY) + 1.0) * 0.5)
                       : static_cast<float>((static_cast<double>(m_width) - total) *
                                            (static_cast<double>(m_internalJustX) + 1.0) * 0.5);
    {
        ElementPtr item;
        item.Set(mp_firstChild.mp_object);
        if (regular) {
            offset = static_cast<float>(static_cast<double>(m_regularSpace) * 0.5 + offset);
            for (; item; item.Set(item->mp_next.mp_object)) {
                if (m_vertical) {
                    item->SetJust(m_internalJustX, 0.0f);
                    item->SetPos(static_cast<float>((static_cast<double>(m_internalJustX) + 1.0) * m_width * 0.5),
                                 offset, 1);
                } else {
                    item->SetJust(0.0f, m_internalJustY);
                    item->SetPos(offset,
                                 static_cast<float>((static_cast<double>(m_internalJustY) + 1.0) * m_height * 0.5), 1);
                }
                offset = static_cast<float>(static_cast<double>(offset) + m_regularSpace);
            }
        } else {
            for (; item; item.Set(item->mp_next.mp_object)) {
                // half the item's padded length; the offset moves by the unrounded half, then by the rounded one
                double half = (m_vertical ? static_cast<double>(item->m_height) : static_cast<double>(item->m_width)) *
                              m_paddingScale * 0.5;
                float halfStored = static_cast<float>(half);
                offset = static_cast<float>(half + offset);
                if (m_vertical) {
                    item->SetJust(m_internalJustX, 0.0f);
                    item->SetPos(static_cast<float>((static_cast<double>(m_internalJustX) + 1.0) * m_width * 0.5),
                                 offset, 1);
                } else {
                    item->SetJust(0.0f, m_internalJustY);
                    item->SetPos(offset,
                                 static_cast<float>((static_cast<double>(m_internalJustY) + 1.0) * m_height * 0.5), 1);
                }
                offset = static_cast<float>(static_cast<double>(halfStored) + offset + m_spacingBetween);
                if (item->m_objectFlags & kDirty)
                    m_objectFlags |= kDirty;
            }
        }
    }
    if (m_selectedId == 0 && mp_firstChild && m_focused) {
        m_selectedIndex = 0;
        MoveSelection(1, true, true);
        if (m_selectedId)
            SendFocus(m_selectedId, m_controller, -1);
    }
}

// 0x004A8100
bool CBaseMenu::PassEventToScript(Obj::CEvent* event)
{
    void* manager = mp_manager;
    if (ManagerUsesPriorities() && IsPriorityEvent(manager, event->m_type)) {
        if (!HasPriority() || !IsTopPriority(manager, m_id))
            return false;
    }
    if (!Obj::CObject::PassEventToScript(event))
        return false;
    if (event->m_type == kFocusSentEvent) {
        // find the selected item again (by id, else by index), lay the items out
        bool refocus = false;
        if (m_selectedId) {
            int index;
            ElementPtr item;
            item.Set(GetChildById(m_selectedId, &index));
            if (!item) {
                ElementPtr byIndex;
                byIndex.Set(GetChildByIndex(m_selectedIndex));
                if (byIndex) {
                    m_selectedId = byIndex->m_id;
                    SetSelectionTags();
                } else {
                    m_selectedIndex = -1;
                    MoveSelection(1, true, true);
                }
                refocus = true;
            } else {
                m_selectedIndex = index;
                SetSelectionTags();
            }
        } else {
            MoveSelection(1, true, true);
            refocus = true;
        }
        if (refocus && m_focused) {
            MoveSelection(1, true, true);
            if (m_selectedId)
                SendFocus(m_selectedId, m_controller, -1);
        }
        Arrange();
        event->MarkHandled(m_id, 0);
    }
    if (event->m_type == kFocus && !m_focused) {
        m_controller = Obj::EventController(event);
        // the item to focus: grid_index (the last item whose tag_grid_x is at most it), child_id
        int grid = -1;
        if (event->mp_data && event->mp_data->GetInteger(kGridIndex, &grid, 0)) {
            m_gridIndex = grid;
            ElementPtr item;
            item.Set(mp_firstChild.mp_object);
            int found = -1;
            for (int i = 0; item; ++i) {
                int x = -1;
                if (item->GetIntegerTag(kTagGridX, &x) && x <= grid)
                    found = i;
                item.Set(item->mp_next.mp_object);
            }
            if (found != -1) {
                m_selectedIndex = found;
                m_selectedId = 1;
            }
        }
        uint32_t childId;
        if (event->mp_data && event->mp_data->GetChecksum(kChildId, &childId, 0)) {
            ElementPtr item;
            item.Set(mp_firstChild.mp_object);
            for (int i = 0; item; ++i) {
                if (childId == item->m_id) {
                    m_selectedId = childId;
                    m_selectedIndex = i;
                    break;
                }
                item.Set(item->mp_next.mp_object);
            }
        }
        m_focused = true;
        if (m_selectedId) {
            MoveSelection(1, true, false);
            if (m_selectedId)
                Launch(kFocus, m_selectedId, event->mp_data);
        }
        event->MarkHandled(m_id, 0);
    }
    if (event->m_type == kUnfocus && m_focused) {
        m_focused = false;
        if (m_selectedId)
            Launch(kUnfocus, m_selectedId, nullptr);
        event->MarkHandled(m_id, 0);
    }
    uint32_t type = event->m_type;
    if (((type == kPadUp && m_vertical) || (type == kPadLeft && !m_vertical)) && m_focused && m_padHandling) {
        Step(-1);
        event->MarkHandled(m_id, 0);
    }
    type = event->m_type;
    if (((type == kPadDown && m_vertical) || (type == kPadRight && !m_vertical)) && m_focused && m_padHandling) {
        Step(1);
        event->MarkHandled(m_id, 0);
    }
    return true;
}

// --- the container element ------------------------------------------------------------------------------------

// 0x004AAC20
CContainerElement* CContainerElement::Construct()
{
    CScreenElement::Construct();
    m_vtable = reinterpret_cast<void**>(0x0064A0BC);
    m_focusableChild = 0;
    SetType(kContainerElement);
    return this;
}

// 0x004AAC80
void CContainerElement::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x0064A0BC);
    CScreenElement::DestroyBody();
}

// 0x004AACC0
CContainerElement* CContainerElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AA530
void CContainerElement::SetProperties(Script::CStruct* params)
{
    params->GetChecksum(kFocusableChild, &m_focusableChild, 0);
    CScreenElement::SetProperties(params);
}

// 0x004AA560
bool CContainerElement::PassEventToScript(Obj::CEvent* event)
{
    void* manager = mp_manager;
    if (UsesPriorities(manager) && IsPriorityEvent(manager, event->m_type)) {
        if (!HasPriority() || !IsTopPriority(manager, m_id))
            return false;
    }
    if (!Obj::CObject::PassEventToScript(event))
        return false;
    if (event->m_type == kFocus || event->m_type == kUnfocus) {
        if (m_focusableChild) {
            CStruct data;
            data.Init();
            data.AddInteger(kController, Obj::EventController(event));
            Launch(event->m_type, m_focusableChild, &data);
            data.Clear();
        }
        event->MarkHandled(m_id, 0);
    }
    return true;
}

} // namespace Front

using Front::CBaseMenu;
using Front::CContainerElement;

REPLACE(0x004A74E0, &CBaseMenu::Construct);
REPLACE(0x004A75C0, &CBaseMenu::ConstructVMenu);
REPLACE(0x004A7630, &CBaseMenu::ConstructHMenu);
REPLACE(0x004A7620, &CBaseMenu::DestroyBody);
REPLACE(0x004A76C0, &CBaseMenu::Destroy);
REPLACE(0x004A76F0, &CBaseMenu::SetProperties);
REPLACE(0x004A8100, &CBaseMenu::PassEventToScript);
REPLACE(0x004A7550, &CBaseMenu::GetRegularSpace);
REPLACE(0x004A7580, &CBaseMenu::SetSelectionTags);
REPLACE(0x004A7850, &CBaseMenu::MoveSelection);
REPLACE(0x004A7A40, &CBaseMenu::Step);
REPLACE(0x004A7B80, &CBaseMenu::Arrange);
REPLACE(0x004A7820, &Obj::CSmtPtr<Front::CScreenElement>::ConstructFrom);
REPLACE(0x004A7690, &Obj::CSmtPtr<Front::CScreenElement>::ConstructCopy);
REPLACE(0x004A8F60, &Obj::CSmtPtr<Front::CScreenElement>::Assign);
REPLACE(0x004AAC20, &CContainerElement::Construct);
REPLACE(0x004AAC80, &CContainerElement::DestroyBody);
REPLACE(0x004AACC0, &CContainerElement::Destroy);
REPLACE(0x004AA530, &CContainerElement::SetProperties);
REPLACE(0x004AA560, &CContainerElement::PassEventToScript);
