// The screen element manager: see screen_element_manager.h.

#include "game/front/screen_element_manager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/new_frontend.h"
#include "game/front/text_element.h"
#include "game/obj/entity.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sio/input_hub.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/entity_hub.h"

namespace MenuSystem {

namespace {
extern void** const kCScreenElementManagerVtable;
extern void** const kListenerAdapterVtable;
} // namespace

CUiTree* g_screenElementManager;

static_assert(offsetof(CUiTree, m_listener) == 0x28 && offsetof(CUiTree, mp_root) == 0x44 &&
                  offsetof(CUiTree, mp_resolving) == 0x50 &&
                  offsetof(CUiTree, m_focusEntries) == 0x5C &&
                  offsetof(CUiTree, mp_focusLists) == 0x4DC &&
                  offsetof(CUiTree, m_focusListChanged) == 0x4E4 &&
                  offsetof(CUiTree, m_numPriorities) == 0x4E8 &&
                  offsetof(CUiTree, m_priorities) == 0x4EC &&
                  offsetof(CUiTree, m_padEventTypes) == 0x5DC &&
                  offsetof(CUiTree, m_numPadEventTypes) == 0x69C &&
                  offsetof(CUiTree, m_checkDevice) == 0x6A0 && sizeof(CUiTree) == 0x6A4,
              "layout must match the original");

namespace {

using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kTagLocalId = 0x9494A525, kChild = 0xDD4CABD6, kUnHide = 0xB60D1F35, kHide = 0x5B6634D4;
constexpr uint32_t kDeviceNum = 0xC9428A08, kTheScore = 0x675ADBAA;
// Events.
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3;
constexpr uint32_t kFocusSentEvent = 0x3056434F, kFocusClearedEvent = 0x4A57316B;
// The script array of the pad event types ("pad_event_types").
const char* PadEventTypesName() { return "pad_event_types"; }

CUiWidget* AsElement(GameObjects::CEntity* object) { return static_cast<CUiWidget*>(object); }

void SetEventConsumed(GameObjects::CSignal* event, uint32_t how, uint32_t script) { event->SetEventConsumed(how, script); }

void VSetProperties(CUiWidget* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[1])(e, params);
}

// A structure with one flag, given to `element`'s ChangeTraits (Hide / UnHide).
void SetFlagProperty(CUiWidget* element, uint32_t flag)
{
    CStruct* params = Scripts::NewStruct();
    if (params)
        params = params->PrepareAll();
    params->PutNameKey(0u, flag);
    VSetProperties(element, params);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// --- construction ---------------------------------------------------------------------------------------------

// 0x004AB990
CUiTree* CUiTree::MakeNew()
{
    GameObjects::CObjectHubBase::MakeNew();
    m_listener.MakeNew();
    m_listener.m_vtable = kListenerAdapterVtable;
    m_vtable = kCScreenElementManagerVtable;
    mp_root.m_preceding = mp_root.m_following = nullptr;
    mp_root.m_ownerObject = nullptr;
    mp_resolving.m_preceding = mp_resolving.m_following = nullptr;
    mp_resolving.m_ownerObject = nullptr;
    for (FocusEntry& entry : m_focusEntries) {
        entry.element.m_ownerObject = nullptr;
        entry.element.m_preceding = entry.element.m_following = nullptr;
    }
    for (PriorityEntry& entry : m_priorities) {
        entry.element.m_ownerObject = nullptr;
        entry.element.m_preceding = entry.element.m_following = nullptr;
    }
    mp_root.Release();
    mp_resolving.Release();
    m_listener.EnterWaitBoard(nullptr);
    for (int i = 0; i < 2; ++i) {
        mp_focusLists[i] = nullptr;
        m_focusListChanged[i] = false;
    }
    for (FocusEntry& entry : m_focusEntries) {
        entry.element.Release();
        entry.next = nullptr;
    }
    m_numPadEventTypes = 0;
    std::memset(m_padEventTypes, 0, sizeof(m_padEventTypes));
    m_checkDevice = false;
    m_numPriorities = 0;
    for (PriorityEntry& entry : m_priorities)
        entry.active = false;
    return this;
}

// 0x004ABB00
void CUiTree::DestroyBody()
{
    m_vtable = kCScreenElementManagerVtable;
    m_listener.m_vtable = kListenerAdapterVtable;
    for (int i = kPriorities - 1; i >= 0; --i)
        m_priorities[i].element.Release();
    for (int i = kFocusEntries - 1; i >= 0; --i)
        m_focusEntries[i].element.Release();
    mp_resolving.Release();
    mp_root.Release();
    m_listener.Teardown();
    GameObjects::CObjectHubBase::DestroyBody();
}

// 0x004ACB40
CUiTree* CUiTree::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AAD40
GameObjects::CObjectListHead* CUiTree::FetchObjectSet() { return &m_entities; }

// 0x004AAFF0
void CUiTree::DropEntityRegistration(GameObjects::CEntity* object)
{
    ElementPtr element;
    element.AssignValue(AsElement(object));
    if (element && mp_root.m_ownerObject == element.m_ownerObject) {
        mp_root.Release();
        for (MemSys::ListNode* node = m_entities.next; node->data != reinterpret_cast<void*>(-1); node = node->next) {
            ElementPtr candidate;
            candidate.AssignValue(static_cast<CUiWidget*>(node->data));
            if (!candidate->m_container && candidate.m_ownerObject != element.m_ownerObject) {
                mp_root.Assign(&candidate);
                break;
            }
        }
    }
    GameObjects::CObjectHubBase::DropEntityRegistration(object);
}

// --- elements -------------------------------------------------------------------------------------------------

// 0x004AAE20
ElementPtr* CUiTree::FetchPart(ElementPtr* out, uint32_t id, int)
{
    GameObjects::CEntity* object = GameObjects::Tracker()->LookUpObject(id);
    out->m_preceding = out->m_following = nullptr;
    out->m_ownerObject = nullptr;
    if (object)
        out->AssignValue(AsElement(object));
    return out;
}

// 0x004AB730
void CUiTree::ChangeRoot(uint32_t id)
{
    ElementPtr element;
    FetchPart(&element, id, 1);
    mp_root.AssignValue(element.m_ownerObject);
}

// 0x004ABBE0
void CUiTree::ChangeOwner(ElementPtr* parent, ElementPtr* child, int unused)
{
    ElementPtr oldParent;
    oldParent.AssignValue(child->m_ownerObject->m_container.m_ownerObject);
    if (CUiWidget* newParent = parent->m_ownerObject) {
        if (mp_root.m_ownerObject == child->m_ownerObject || !mp_root) {
            mp_root.AssignValue(newParent);
            while (mp_root->m_container)
                mp_root.AssignValue(mp_root->m_container.m_ownerObject);
        }
    }
    RemoveFocusEntries(child, false, false);
    child->m_ownerObject->ChangeOwner(parent, unused);
}

// 0x004AC400
void CUiTree::DestroyTree(int keep, ElementPtr* element, Scripts::CScript* script)
{
    element->m_ownerObject->SetFocusSent(0);
    ElementPtr child;
    child.AssignValue(element->m_ownerObject->mp_firstChild.m_ownerObject);
    while (child) {
        ElementPtr next;
        next.AssignValue(child->m_following.m_ownerObject);
        DestroyTree(0, &child, script);
        child.AssignValue(next.m_ownerObject);
    }
    if (keep == 0) {
        if (script && element->m_ownerObject)
            script->ObjectDestroyed(element->m_ownerObject);
        {
            ElementPtr none;
            ChangeOwner(&none, element, 1);
        }
        VirtualUnregisterObject(element->m_ownerObject);
        if (element->m_ownerObject)
            element->m_ownerObject->VirtualDestroy(1);
    }
}

// 0x004AD100
void CUiTree::DismantlePart(uint32_t id, int recurse, int keep, Scripts::CScript* script)
{
    ElementPtr element;
    FetchPart(&element, id, 1);
    if (recurse) {
        RemoveFocusEntries(&element, false, false);
        if (CUiWidget* parent = element->m_container.m_ownerObject)
            parent->SetFocusSent(0);
        DestroyTree(keep, &element, script);
    } else {
        RemoveFocusEntries(&element, false, false);
        if (script && element.m_ownerObject)
            script->ObjectDestroyed(element.m_ownerObject);
        VirtualUnregisterObject(element.m_ownerObject);
        if (element.m_ownerObject)
            element.m_ownerObject->VirtualDestroy(1);
    }
}

// 0x004AB130
ElementPtr* CUiTree::FindByLocalId(ElementPtr* out, const ElementPtr* parent, uint32_t localId)
{
    ElementPtr child;
    child.AssignValue(parent->m_ownerObject->mp_firstChild.m_ownerObject);
    while (child) {
        uint32_t tag;
        if (child->ReadHashTag(kTagLocalId, &tag) && tag == localId)
            break;
        child.AssignValue(child->m_following.m_ownerObject);
    }
    out->m_preceding = out->m_following = nullptr;
    out->m_ownerObject = nullptr;
    out->AssignValue(child.m_ownerObject);
    return out;
}

// 0x004AC120
bool CUiTree::ResolveId(Scripts::CStruct* params, uint32_t name, uint32_t* out)
{
    CStruct* path = nullptr;
    if (params->ReadSubStruct(name, &path, 0)) {
        uint32_t id = 0;
        path->ReadHashValue(0u, &id, 0);
        int index = -1;
        path->FetchIntValue(0u, &index, 0);
        if (id) {
            ElementPtr found;
            if (!mp_resolving)
                FetchPart(&found, id, 0);
            else
                FindByLocalId(&found, &mp_resolving, id);
            mp_resolving.AssignValue(found.m_ownerObject);
            found.Release();
            if (!mp_resolving)
                return false;
        } else if (index != -1) {
            mp_resolving.AssignValue(mp_resolving->FindChildAtSlot(index));
            if (!mp_resolving)
                return false;
        }
        bool resolved = ResolveId(path, kChild, out);
        mp_resolving.Release();
        if (resolved)
            return true;
    }
    uint32_t id;
    if (params->ReadHashValue(name, &id, 0)) {
        if (mp_resolving) {
            ElementPtr found;
            FindByLocalId(&found, &mp_resolving, id);
            mp_resolving.AssignValue(found.m_ownerObject);
            found.Release();
            if (!mp_resolving)
                return false;
            id = mp_resolving->m_ident;
        } else if (GameObjects::CEntity* aliased = GameObjects::Tracker()->LookUpByNickname(id)) {
            id = aliased->m_ident;
        }
        mp_resolving.Release();
        *out = id;
        return true;
    }
    int index;
    if (params->FetchIntValue(name, &index, 0)) {
        if (!mp_resolving)
            return false;
        ElementPtr child;
        child.AssignValue(mp_resolving->FindChildAtSlot(index));
        mp_resolving.Release();
        if (!child)
            return false;
        *out = child->m_ident;
        return true;
    }
    mp_resolving.Release();
    return false;
}

// 0x004AD1E0
ElementPtr* CUiTree::ResolveElement(ElementPtr* out, Scripts::CStruct* params, uint32_t name, int assert)
{
    uint32_t id;
    if (ResolveId(params, name, &id))
        return FetchPart(out, id, assert);
    out->m_preceding = out->m_following = nullptr;
    out->m_ownerObject = nullptr;
    return out;
}

// --- each frame -----------------------------------------------------------------------------------------------

// 0x004ABD00
void CUiTree::TickFrame()
{
    if (!mp_root)
        return;
    // Not original - the new front end (game/front/new_frontend.h): the original's elements hidden or shown for this
    // frame before they are laid out - a menu restyled by ours is never drawn as the game made it, not even a frame.
    // Original behaviour: the tree laid out as the scripts left it.
    MenuSystem::NewFrontend::BeforeScreenLayout();
    LockTree(1);
    mp_root->UpdateLayout();
    LockTree(0);
    if (m_numPriorities > 0)
        UpdatePriorities();
}

// 0x004AAF90 (a stack of 32 elements: the next sibling, then the first child, of each)
void CUiTree::LockTree(int lock)
{
    if (!mp_root)
        return;
    CUiWidget* stack[32];
    stack[0] = mp_root.m_ownerObject;
    int count = 1;
    while (count) {
        CUiWidget* element = stack[--count];
        if (lock == 1)
            ++element->m_aliasCount;
        else if (static_cast<int>(element->m_aliasCount) > 0)
            --element->m_aliasCount;
        if (element->m_following)
            stack[count++] = element->m_following.m_ownerObject;
        if (element->mp_firstChild)
            stack[count++] = element->mp_firstChild.m_ownerObject;
    }
}

// 0x004AAEE0
void CUiTree::SetPausedAll(bool pause)
{
    MemSys::ListNode* node = m_entities.next;
    if (node->data == reinterpret_cast<void*>(-1))
        return;
    while (node) {
        ElementPtr element;
        element.AssignValue(static_cast<CUiWidget*>(node->data));
        element->SetFrozen(pause);
        node = node->next;
        if (node->data == reinterpret_cast<void*>(-1))
            node = nullptr;
    }
}

// --- focus and the pad ----------------------------------------------------------------------------------------

// 0x004AB250 (a new entry goes after the last entry looked at - the second list's tail when it has entries,
// whichever list it is for -, or heads `list` when both are empty)
void CUiTree::AddFocusEntry(ElementPtr* element, int list)
{
    FocusEntry* last = nullptr;
    for (int i = 0; i < 2; ++i) {
        for (FocusEntry* entry = mp_focusLists[i]; entry; entry = entry->next) {
            if (entry->element.m_ownerObject == element->m_ownerObject && list == i) {
                entry->marked = false;
                return;
            }
            last = entry;
        }
    }
    int free = 0;
    while (m_focusEntries[free].element) {
        if (++free == kFocusEntries)
            return;
    }
    FocusEntry* entry = &m_focusEntries[free];
    entry->element.AssignValue(element->m_ownerObject);
    entry->id = element->m_ownerObject->m_ident;
    entry->next = nullptr;
    entry->marked = false;
    if (last)
        last->next = entry;
    else
        mp_focusLists[list] = entry;
    m_focusListChanged[list] = true;
}

// 0x004AB330
void CUiTree::RemoveFocusEntries(ElementPtr* element, bool fromParent, bool mark)
{
    for (int i = 0; i < 2; ++i) {
        FocusEntry* previous = nullptr;
        FocusEntry* entry = mp_focusLists[i];
        while (entry) {
            FocusEntry* next = entry->next;
            ElementPtr walk;
            walk.AssignValue(entry->element.m_ownerObject);
            if (fromParent && walk)
                walk.AssignValue(walk->m_container.m_ownerObject);
            bool matched = false;
            for (; walk; walk.AssignValue(walk->m_container.m_ownerObject)) {
                if (walk.m_ownerObject == element->m_ownerObject) {
                    matched = true;
                    break;
                }
            }
            if (matched) {
                if (mark) {
                    entry->marked = true;
                } else {
                    if (previous)
                        previous->next = next;
                    else
                        mp_focusLists[i] = next;
                    entry->element.Release();
                    m_focusListChanged[i] = true;
                    entry = previous;
                }
            }
            previous = entry;
            entry = next;
        }
    }
}

// 0x004AB4D0
void CUiTree::ClearMarks(ElementPtr* element)
{
    for (int i = 0; i < 2; ++i) {
        for (FocusEntry* entry = mp_focusLists[i]; entry; entry = entry->next) {
            if (!entry->marked)
                continue;
            ElementPtr walk;
            walk.AssignValue(entry->element.m_ownerObject);
            for (; walk; walk.AssignValue(walk->m_container.m_ownerObject)) {
                if (walk.m_ownerObject == element->m_ownerObject) {
                    entry->marked = false;
                    break;
                }
            }
        }
    }
}

// 0x004ABD40
void CUiTree::ListenerReceive(GameObjects::CSignal* event)
{
    // (`this` is the listener: the manager is 0x28 before it)
    CUiTree* manager =
        reinterpret_cast<CUiTree*>(reinterpret_cast<uint8_t*>(this) - offsetof(CUiTree, m_listener));
    if (manager->m_numPadEventTypes == 0) {
        Scripts::CArray* types = Scripts::FetchList(PadEventTypesName()); // (the original passes 1 as well: unused)
        manager->m_numPadEventTypes = static_cast<int>(types->m_extent);
        for (int i = 0; i < manager->m_numPadEventTypes; ++i)
            manager->m_padEventTypes[i] = types->FetchPart(i);
    }
    bool padEvent = false;
    if (manager->m_numPriorities > 0) {
        for (int i = 0; i < manager->m_numPadEventTypes; ++i) {
            if (event->m_kindTag == manager->m_padEventTypes[i]) {
                padEvent = true;
                break;
            }
        }
    }
    if (manager->m_checkDevice && event->m_payload) {
        int device;
        if (event->m_payload->FetchIntValue(kDeviceNum, &device, 0)) {
            auto* input = PadInput::g_inputManager; // (the input manager)
            if (input->GetPlayerController(0) != device)
                return;
        }
    }
    GameObjects::CEventWaitBoard* tracker = GameObjects::Tracker();
    if (event->m_kindTag == kFocus && event->m_aimPoint != kSystem) {
        ElementPtr element;
        manager->FetchPart(&element, event->m_aimPoint, 0);
        if (element && !element->InputSuppressed()) {
            manager->AddFocusEntry(&element, GameObjects::EventController(event));
            SetEventConsumed(event, 1, 0);
        }
    }
    if (event->m_kindTag == kUnfocus && event->m_aimPoint != kSystem) {
        ElementPtr element;
        manager->FetchPart(&element, event->m_aimPoint, 0);
        if (element && !element->InputSuppressed()) {
            manager->RemoveFocusEntries(&element, false, false);
            SetEventConsumed(event, 1, 0);
        }
    }
    if (event->m_kindTag == kFocusClearedEvent) {
        ElementPtr element;
        manager->FetchPart(&element, event->m_aimPoint, 0);
        manager->RemoveFocusEntries(&element, true, true);
        SetEventConsumed(event, 1, 0);
    }
    if (event->m_kindTag == kFocusSentEvent) {
        ElementPtr element;
        manager->FetchPart(&element, event->m_aimPoint, 0);
        manager->ClearMarks(&element);
        SetEventConsumed(event, 1, 0);
        element.Release();
    }
    // a pad event from the system: offered to the controller's focused elements until one handles it (a
    // change of the list starts the round again with the elements not offered it yet)
    bool isPadType = false;
    for (int i = 0; i < manager->m_numPadEventTypes; ++i) {
        if (event->m_kindTag == manager->m_padEventTypes[i]) {
            isPadType = true;
            break;
        }
    }
    if (!isPadType || event->m_aimPoint != kSystem)
        return;
    int controller = GameObjects::EventController(event);
    for (FocusEntry* entry = manager->mp_focusLists[controller]; entry; entry = entry->next)
        entry->tried = false;
    manager->m_focusListChanged[controller] = true;
    do {
        manager->m_focusListChanged[controller] = false;
        for (FocusEntry* entry = manager->mp_focusLists[controller]; entry; entry = entry->next) {
            if (!entry->tried && !entry->marked && !entry->element->InputSuppressed()) {
                if (padEvent && (!entry->element->HasPriority() || !manager->IsTopPriority(entry->id)))
                    continue;
                if (tracker->LaunchEvent(event->m_kindTag, entry->element->m_ident, event->m_fromWhere, event->m_payload, false,
                                         -1.0f)) {
                    event->SetEventHandled(1, 0);
                    return;
                }
                entry->tried = true;
            }
            if (manager->m_focusListChanged[controller])
                break;
        }
    } while (manager->m_focusListChanged[controller]);
}

// --- pad-event priorities -------------------------------------------------------------------------------------

// 0x004AAD80
bool CUiTree::HasPriorities() const { return m_numPriorities > 0; }

// 0x004AAD50
bool CUiTree::IsPadEvent(uint32_t type) const
{
    for (int i = 0; i < m_numPadEventTypes; ++i)
        if (type == m_padEventTypes[i])
            return true;
    return false;
}

// 0x004AB7C0
bool CUiTree::IsTopPriority(uint32_t id)
{
    ElementPtr element;
    FetchPart(&element, id, 1);
    uint32_t priority = static_cast<uint32_t>(element->FetchRank());
    uint32_t top = 0;
    for (const PriorityEntry& entry : m_priorities)
        if (entry.active && entry.priority > top)
            top = entry.priority;
    return priority == top;
}

// 0x004AC570
bool CUiTree::PushPriority(uint32_t id, uint32_t priority)
{
    uint32_t lowest = 0, highest = 0;
    int slot = -1, highestSlot = -1;
    bool free = false;
    for (int i = 0; i < kPriorities; ++i) {
        PriorityEntry& entry = m_priorities[i];
        if (entry.active) {
            if (entry.priority < lowest || lowest == 0) {
                lowest = entry.priority;
                slot = i;
            }
            if (entry.priority > highest) {
                highest = entry.priority;
                highestSlot = i;
            }
        } else if (!free) {
            slot = i;
            free = true;
        }
    }
    if (!free && priority < lowest)
        return false;
    PriorityEntry& entry = m_priorities[slot];
    entry.id = id;
    entry.active = true;
    ElementPtr element;
    FetchPart(&element, id, 1);
    entry.element.AssignValue(element.m_ownerObject);
    entry.priority = priority;
    if (m_numPriorities > 0) {
        // the top one shown: the new one hidden unless it is the new top, then the old top is
        CStruct* params = Scripts::NewStruct();
        if (params)
            params = params->PrepareAll();
        params->PutNameKey(0u, kHide);
        if (priority > highest)
            VSetProperties(m_priorities[highestSlot].element.m_ownerObject, params);
        else
            VSetProperties(element.m_ownerObject, params);
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
    ++m_numPriorities;
    return true;
}

// 0x004AB5D0 (with no priority above 0 the "top" is the entry before the first)
void CUiTree::UpdatePriorities()
{
    uint32_t highest = 0;
    int top = -1;
    if (m_numPriorities <= 0)
        return;
    for (int i = 0; i < kPriorities; ++i) {
        PriorityEntry& entry = m_priorities[i];
        if (!entry.active)
            continue;
        CUiWidget* element;
        {
            ElementPtr found;
            element = FetchPart(&found, entry.id, 0)->m_ownerObject;
        }
        if (element) {
            if (entry.priority > highest) {
                highest = entry.priority;
                top = i;
            }
        } else {
            entry.active = false;
            --m_numPriorities;
        }
    }
    if (m_numPriorities <= 0)
        return;
    SetFlagProperty(m_priorities[top].element.m_ownerObject, kUnHide);
}

// --- the singleton --------------------------------------------------------------------------------------------

uint32_t g_screenElementManagerUses;

// 0x004ACAF0
void CreateScreenElementManager(bool create)
{
    CUiTree*& instance = CUiTree::Sole();
    if (!instance && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CUiTree), static_cast<MemSys::Arena*>(nullptr), 1);
        if (memory) {
            instance = static_cast<CUiTree*>(memory)->MakeNew();
            ++g_screenElementManagerUses;
            return;
        }
        instance = nullptr;
    }
    ++g_screenElementManagerUses;
}

// 0x004AACF0
void DeleteScreenElementManager()
{
    uint32_t& uses = g_screenElementManagerUses;
    if (uses > 1) {
        --uses;
        return;
    }
    if (CUiTree* instance = CUiTree::Sole())
        reinterpret_cast<void*(__thiscall*)(void*, int)>(instance->m_vtable[0])(instance, 1);
    CUiTree::Sole() = nullptr;
    uses = 0;
}

// 0x004AB910
void SetScoreText(const char* text, int player)
{
    ElementPtr element;
    CUiTree::Sole()->FetchPart(&element, kTheScore + player, 0);
    if (element)
        static_cast<CLabelWidget*>(element.m_ownerObject)->ChangeCaption(text);
}

} // namespace Front

namespace {

// 0x004ABAF0: the listener's slot 0 (`this` = the listener): the manager's Destroy.
struct ListenerAdapter {
    MenuSystem::CUiTree* Teardown(uint8_t flags)
    {
        auto* manager = reinterpret_cast<MenuSystem::CUiTree*>(reinterpret_cast<uint8_t*>(this) - 0x28);
        return manager->Teardown(flags);
    }
};

} // namespace

using MenuSystem::CUiTree;

namespace MenuSystem {
namespace {
static VtableSlot kCScreenElementManagerVtable_slots[] = {&CUiTree::Teardown,
                &GameObjects::CObjectHubBase::EnrolSubject, &CUiTree::DropEntityRegistration,
                &GameObjects::CObjectHubBase::ReassignObjectId, &GameObjects::CObjectHubBase::SetEntityRank, Stubs::IgnoreArgument,
                &CUiTree::FetchObjectSet};
void** const kCScreenElementManagerVtable = reinterpret_cast<void**>(kCScreenElementManagerVtable_slots);
static VtableSlot kListenerAdapterVtable_slots[] = {&ListenerAdapter::Teardown, &CUiTree::ListenerReceive};
void** const kListenerAdapterVtable = reinterpret_cast<void**>(kListenerAdapterVtable_slots);
} // namespace
} // namespace Front
