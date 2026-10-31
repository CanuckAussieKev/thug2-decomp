// The menus (vmenu / hmenu) and the container element: see menu_element.h.

#include "frontend/menu_element.h"
#include "frontend/screen_element_manager.h"

#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/objects/signal.h"
#include "engine/objects/tracker.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "engine/core/stubs.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"

namespace MenuSystem {

namespace {
extern void** const kCBaseMenuVtable;
extern void** const kCContainerElementVtable;
} // namespace

namespace {

using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kVMenu = 0x130EF802, kHMenu = 0xCCDED1E1, kContainerElement = 0x5B9DA842;
constexpr uint32_t kInnerAlign = 0x67E093E4, kRegularSpaceAmount = 0xF24ADEDB, kInsetFactor = 0x6D853B88;
constexpr uint32_t kItemSpacing = 0x4B4E14AB, kEnablePadHandling = 0x4BD08BFE, kDisablePadHandling = 0xC22541CC;
constexpr uint32_t kWrapAround = 0x43A7692D, kDontAllowWrap = 0x5DEA5C64, kFocusableChild = 0xF1822B9C;
constexpr uint32_t kTagSelectedId = 0xAC988EBE, kTagSelectedIndex = 0xF2239871, kTagGridIndex = 0x8321DD71;
constexpr uint32_t kTagGridX = 0x5B92F8DD, kTagNotFocusable = 0xF33A3321;
constexpr uint32_t kController = 0xB30D9965, kGridIndex = 0xFACF9A8B, kChildId = 0x229D3DE4;
// Events.
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;
constexpr uint32_t kPadUp = 0x30A4F836, kPadDown = 0x0FD1AC26, kPadLeft = 0x6949DB75, kPadRight = 0x05DDD87C;

float RegularSpaceMinimum() { return ::GameModules::g_competitionTiny; } // 0.0001

void Launch(uint32_t type, uint32_t target, CStruct* data)
{
    GameObjects::Tracker()->LaunchEvent(type, target, kSystem, data, false, -1.0f);
}

// The screen element manager (0x00701440) and its pieces.
void* Hub() { return g_uiElementHub; }
CUiTree* Elements(void* manager) { return static_cast<CUiTree*>(manager); }
bool UsesPriorities(void* manager) { return Elements(manager)->HasPriorities(); }
bool IsPriorityEvent(void* manager, uint32_t type) { return Elements(manager)->IsPadEvent(type); }
bool HasTopPrecedence(void* manager, uint32_t id) { return Elements(manager)->HasTopPrecedence(id); }
ElementPtr* LookUp(void* manager, ElementPtr* out, uint32_t id, int assert)
{
    return Elements(manager)->FetchPart(out, id, assert);
}

// focus to `id` with the controller (and a grid index, unless -1).
void SendFocus(uint32_t id, int controller, int gridIndex)
{
    CStruct data;
    data.PrepareAll();
    data.PutInt(kController, controller);
    if (gridIndex != -1)
        data.PutInt(kGridIndex, gridIndex);
    Launch(kFocus, id, &data);
    data.Clear();
}

} // namespace

// --- the menu -------------------------------------------------------------------------------------------------

// 0x004A74E0
CMenuBase* CMenuBase::MakeNew()
{
    CUiWidget::MakeNew();
    m_chosenItemId = 0;
    m_focused = false;
    m_controller = 0;
    m_regularSpace = 0.0f;
    m_itemGap = 0.0f;
    m_innerAlignH = 0.0f;
    m_innerAlignV = 0.0f;
    m_padHandling = true;
    m_cyclesAround = true;
    m_vtable = kCBaseMenuVtable;
    m_selectedIndex = -1;
    m_marginFactor = 1.0f;
    m_gridIndex = -1;
    return this;
}

// 0x004A75C0
CMenuBase* CMenuBase::ConstructVMenu()
{
    MakeNew();
    m_vtable = kCBaseMenuVtable;
    m_vertical = true;
    ChangeSpecies(kVMenu);
    return this;
}

// 0x004A7630
CMenuBase* CMenuBase::ConstructHMenu()
{
    MakeNew();
    m_vtable = kCBaseMenuVtable;
    m_vertical = false;
    ChangeSpecies(kHMenu);
    return this;
}

// 0x004A7620
void CMenuBase::DestroyBody()
{
    m_vtable = kCBaseMenuVtable;
    CUiWidget::DestroyBody();
}

// 0x004A76C0
CMenuBase* CMenuBase::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004A7550
int CMenuBase::GetRegularSpace(float* out) const
{
    *out = m_regularSpace;
    return m_regularSpace > RegularSpaceMinimum() ? 1 : 0;
}

// 0x004A7580
void CMenuBase::SetSelectionTags()
{
    WriteHashTag(kTagSelectedId, m_chosenItemId);
    StoreIntTag(kTagSelectedIndex, m_selectedIndex);
    StoreIntTag(kTagGridIndex, m_gridIndex);
}

// 0x004A76F0
void CMenuBase::ChangeTraits(Scripts::CStruct* params)
{
    float alignH = 0.0f, alignV = 0.0f;
    if (ReadAlignmentParam(params, kInnerAlign, &alignH, &alignV)) {
        m_innerAlignH = alignH;
        m_innerAlignV = alignV;
    }
    int space = 0;
    if (params->FetchIntValue(kRegularSpaceAmount, &space, 0))
        m_regularSpace = static_cast<float>(space);
    params->FetchFloat(kInsetFactor, &m_marginFactor, 0);
    int between = 0;
    if (params->FetchIntValue(kItemSpacing, &between, 0))
        m_itemGap = static_cast<float>(between);
    if (params->HasBareFlag(kEnablePadHandling))
        m_padHandling = true;
    else if (params->HasBareFlag(kDisablePadHandling))
        m_padHandling = false;
    if (params->HasBareFlag(kWrapAround))
        m_cyclesAround = true;
    else if (params->HasBareFlag(kDontAllowWrap))
        m_cyclesAround = false;
    CUiWidget::ChangeTraits(params);
}

// 0x004A7850 (each item gets two chances: twice the item count of tries)
void CMenuBase::MoveSelection(int direction, bool stay, bool gridX)
{
    int tries = ChildTotal() * 2;
    if (m_selectedIndex < 0)
        m_selectedIndex = 0;
    while (tries != 0) {
        if (!stay) {
            int index = m_selectedIndex + direction;
            m_selectedIndex = index;
            if (index >= ChildTotal()) {
                if (m_cyclesAround) {
                    m_selectedIndex = 0;
                } else {
                    direction = -direction;
                    m_selectedIndex = ChildTotal() - 1;
                }
            } else if (index < 0) {
                if (m_cyclesAround) {
                    m_selectedIndex = ChildTotal() - 1;
                } else {
                    direction = -direction;
                    m_selectedIndex = 0;
                }
            }
        }
        stay = false;
        ElementPtr item;
        item.AssignValue(FindChildAtSlot(m_selectedIndex));
        m_chosenItemId = item->m_ident;
        if (gridX) {
            m_gridIndex = -1;
            item->ReadIntTag(kTagGridX, &m_gridIndex);
        }
        if (!item->ContainsTagFlag(kTagNotFocusable) && !item->InputSuppressed()) {
            item.Release();
            if (tries == 0)
                break;
            SetSelectionTags();
            return;
        }
        --tries;
    }
    m_chosenItemId = 0;
    m_selectedIndex = -1;
}

// 0x004A7A40
void CMenuBase::Step(int direction)
{
    int gridIndex = -1;
    if (m_chosenItemId) {
        CUiWidget* item;
        {
            ElementPtr found;
            item = LookUp(Hub(), &found, m_chosenItemId, 0)->m_ownerObject;
        }
        if (item)
            item->ReadIntTag(kTagGridIndex, &gridIndex);
        Launch(kUnfocus, m_chosenItemId, nullptr);
    }
    MoveSelection(direction, false, true);
    if (m_chosenItemId)
        SendFocus(m_chosenItemId, m_controller, gridIndex);
}

// 0x004A7B80
void CMenuBase::Arrange()
{
    float total = 0.0f, largest = 0.0f;
    bool regular = m_regularSpace > RegularSpaceMinimum();
    {
        ElementPtr item;
        item.AssignValue(m_eldest.m_ownerObject);
        if (regular) {
            for (; item; item.AssignValue(item->m_following.m_ownerObject)) {
                double across = m_vertical ? static_cast<double>(item->m_span) * item->m_shown.scaleX
                                           : static_cast<double>(item->m_levelY) * item->m_shown.scaleY;
                total = static_cast<float>(static_cast<double>(total) + m_regularSpace);
                if (across > largest)
                    largest = static_cast<float>(across);
            }
        } else {
            for (; item; item.AssignValue(item->m_following.m_ownerObject)) {
                double across, along;
                if (m_vertical) {
                    across = static_cast<double>(item->m_span) * m_marginFactor;
                    along = item->m_levelY;
                } else {
                    across = static_cast<double>(item->m_levelY) * m_marginFactor;
                    along = item->m_span;
                }
                total = static_cast<float>(along * m_marginFactor + m_itemGap + total);
                if (across > largest)
                    largest = static_cast<float>(across);
            }
            total = static_cast<float>(static_cast<double>(total) - m_itemGap);
        }
    }
    if (m_objectFlags & kDimsLocked) {
        // the size only grows
        if (m_vertical) {
            if (total > m_levelY)
                m_levelY = total;
        } else if (total > m_span) {
            m_span = total;
        }
    } else if (m_vertical) {
        m_span = largest;
        m_levelY = total;
    } else {
        m_levelY = largest;
        m_span = total;
    }
    PlaceTopLeft(&m_aimPoint);
    m_objectFlags |= kDirty;
    // where the first item goes: the free space split by internal_just
    float offset = m_vertical
                       ? static_cast<float>((static_cast<double>(m_levelY) - total) *
                                            (static_cast<double>(m_innerAlignV) + 1.0) * 0.5)
                       : static_cast<float>((static_cast<double>(m_span) - total) *
                                            (static_cast<double>(m_innerAlignH) + 1.0) * 0.5);
    {
        ElementPtr item;
        item.AssignValue(m_eldest.m_ownerObject);
        if (regular) {
            offset = static_cast<float>(static_cast<double>(m_regularSpace) * 0.5 + offset);
            for (; item; item.AssignValue(item->m_following.m_ownerObject)) {
                if (m_vertical) {
                    item->ChangeAlignment(m_innerAlignH, 0.0f);
                    item->ChangeLocation(static_cast<float>((static_cast<double>(m_innerAlignH) + 1.0) * m_span * 0.5),
                                 offset, 1);
                } else {
                    item->ChangeAlignment(0.0f, m_innerAlignV);
                    item->ChangeLocation(offset,
                                 static_cast<float>((static_cast<double>(m_innerAlignV) + 1.0) * m_levelY * 0.5), 1);
                }
                offset = static_cast<float>(static_cast<double>(offset) + m_regularSpace);
            }
        } else {
            for (; item; item.AssignValue(item->m_following.m_ownerObject)) {
                // half the item's padded length; the offset moves by the unrounded half, then by the rounded one
                double half = (m_vertical ? static_cast<double>(item->m_levelY) : static_cast<double>(item->m_span)) *
                              m_marginFactor * 0.5;
                float halfStored = static_cast<float>(half);
                offset = static_cast<float>(half + offset);
                if (m_vertical) {
                    item->ChangeAlignment(m_innerAlignH, 0.0f);
                    item->ChangeLocation(static_cast<float>((static_cast<double>(m_innerAlignH) + 1.0) * m_span * 0.5),
                                 offset, 1);
                } else {
                    item->ChangeAlignment(0.0f, m_innerAlignV);
                    item->ChangeLocation(offset,
                                 static_cast<float>((static_cast<double>(m_innerAlignV) + 1.0) * m_levelY * 0.5), 1);
                }
                offset = static_cast<float>(static_cast<double>(halfStored) + offset + m_itemGap);
                if (item->m_objectFlags & kDirty)
                    m_objectFlags |= kDirty;
            }
        }
    }
    if (m_chosenItemId == 0 && m_eldest && m_focused) {
        m_selectedIndex = 0;
        MoveSelection(1, true, true);
        if (m_chosenItemId)
            SendFocus(m_chosenItemId, m_controller, -1);
    }
}

// 0x004A8100
bool CMenuBase::PassEventToScript(GameObjects::CSignal* event)
{
    void* manager = m_parkOwner;
    if (ManagerUsesPriorities() && IsPriorityEvent(manager, event->m_kindTag)) {
        if (!HasPriority() || !HasTopPrecedence(manager, m_ident))
            return false;
    }
    if (!GameObjects::CEntity::PassEventToScript(event))
        return false;
    if (event->m_kindTag == kFocusSentEvent) {
        // find the selected item again (by id, else by index), lay the items out
        bool refocus = false;
        if (m_chosenItemId) {
            int index;
            ElementPtr item;
            item.AssignValue(FindChildWithId(m_chosenItemId, &index));
            if (!item) {
                ElementPtr byIndex;
                byIndex.AssignValue(FindChildAtSlot(m_selectedIndex));
                if (byIndex) {
                    m_chosenItemId = byIndex->m_ident;
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
            if (m_chosenItemId)
                SendFocus(m_chosenItemId, m_controller, -1);
        }
        Arrange();
        event->SetEventHandled(m_ident, 0);
    }
    if (event->m_kindTag == kFocus && !m_focused) {
        m_controller = GameObjects::EventController(event);
        // the item to focus: grid_index (the last item whose tag_grid_x is at most it), child_id
        int grid = -1;
        if (event->m_payload && event->m_payload->FetchIntValue(kGridIndex, &grid, 0)) {
            m_gridIndex = grid;
            ElementPtr item;
            item.AssignValue(m_eldest.m_ownerObject);
            int found = -1;
            for (int i = 0; item; ++i) {
                int x = -1;
                if (item->ReadIntTag(kTagGridX, &x) && x <= grid)
                    found = i;
                item.AssignValue(item->m_following.m_ownerObject);
            }
            if (found != -1) {
                m_selectedIndex = found;
                m_chosenItemId = 1;
            }
        }
        uint32_t childId;
        if (event->m_payload && event->m_payload->ReadHashValue(kChildId, &childId, 0)) {
            ElementPtr item;
            item.AssignValue(m_eldest.m_ownerObject);
            for (int i = 0; item; ++i) {
                if (childId == item->m_ident) {
                    m_chosenItemId = childId;
                    m_selectedIndex = i;
                    break;
                }
                item.AssignValue(item->m_following.m_ownerObject);
            }
        }
        m_focused = true;
        if (m_chosenItemId) {
            MoveSelection(1, true, false);
            if (m_chosenItemId)
                Launch(kFocus, m_chosenItemId, event->m_payload);
        }
        event->SetEventHandled(m_ident, 0);
    }
    if (event->m_kindTag == kUnfocus && m_focused) {
        m_focused = false;
        if (m_chosenItemId)
            Launch(kUnfocus, m_chosenItemId, nullptr);
        event->SetEventHandled(m_ident, 0);
    }
    uint32_t type = event->m_kindTag;
    if (((type == kPadUp && m_vertical) || (type == kPadLeft && !m_vertical)) && m_focused && m_padHandling) {
        Step(-1);
        event->SetEventHandled(m_ident, 0);
    }
    type = event->m_kindTag;
    if (((type == kPadDown && m_vertical) || (type == kPadRight && !m_vertical)) && m_focused && m_padHandling) {
        Step(1);
        event->SetEventHandled(m_ident, 0);
    }
    return true;
}

// --- the container element ------------------------------------------------------------------------------------

// 0x004AAC20
CGroupWidget* CGroupWidget::MakeNew()
{
    CUiWidget::MakeNew();
    m_vtable = kCContainerElementVtable;
    m_focusableChild = 0;
    ChangeSpecies(kContainerElement);
    return this;
}

// 0x004AAC80
void CGroupWidget::DestroyBody()
{
    m_vtable = kCContainerElementVtable;
    CUiWidget::DestroyBody();
}

// 0x004AACC0
CGroupWidget* CGroupWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AA530
void CGroupWidget::ChangeTraits(Scripts::CStruct* params)
{
    params->ReadHashValue(kFocusableChild, &m_focusableChild, 0);
    CUiWidget::ChangeTraits(params);
}

// 0x004AA560
bool CGroupWidget::PassEventToScript(GameObjects::CSignal* event)
{
    void* manager = m_parkOwner;
    if (UsesPriorities(manager) && IsPriorityEvent(manager, event->m_kindTag)) {
        if (!HasPriority() || !HasTopPrecedence(manager, m_ident))
            return false;
    }
    if (!GameObjects::CEntity::PassEventToScript(event))
        return false;
    if (event->m_kindTag == kFocus || event->m_kindTag == kUnfocus) {
        if (m_focusableChild) {
            CStruct data;
            data.PrepareAll();
            data.PutInt(kController, GameObjects::EventController(event));
            Launch(event->m_kindTag, m_focusableChild, &data);
            data.Clear();
        }
        event->SetEventHandled(m_ident, 0);
    }
    return true;
}

namespace {
static VtableSlot kCBaseMenuVtable_slots[] = {&CMenuBase::Teardown, &CMenuBase::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &CMenuBase::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                Stubs::DoNothing};
void** const kCBaseMenuVtable = reinterpret_cast<void**>(kCBaseMenuVtable_slots);
static VtableSlot kCContainerElementVtable_slots[] = {&CGroupWidget::Teardown, &CGroupWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &CGroupWidget::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CUiWidget::BlendTowards,
                Stubs::DoNothing};
void** const kCContainerElementVtable = reinterpret_cast<void**>(kCContainerElementVtable_slots);
} // namespace

} // namespace Front

using MenuSystem::CMenuBase;
using MenuSystem::CGroupWidget;
