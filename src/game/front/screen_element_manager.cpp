// The screen element manager: see screen_element_manager.h.

#include "game/front/screen_element_manager.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/new_frontend.h"
#include "game/front/text_element.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

static_assert(offsetof(CScreenElementManager, m_listener) == 0x28 && offsetof(CScreenElementManager, mp_root) == 0x44 &&
                  offsetof(CScreenElementManager, mp_resolving) == 0x50 &&
                  offsetof(CScreenElementManager, m_focusEntries) == 0x5C &&
                  offsetof(CScreenElementManager, mp_focusLists) == 0x4DC &&
                  offsetof(CScreenElementManager, m_focusListChanged) == 0x4E4 &&
                  offsetof(CScreenElementManager, m_numPriorities) == 0x4E8 &&
                  offsetof(CScreenElementManager, m_priorities) == 0x4EC &&
                  offsetof(CScreenElementManager, m_padEventTypes) == 0x5DC &&
                  offsetof(CScreenElementManager, m_numPadEventTypes) == 0x69C &&
                  offsetof(CScreenElementManager, m_checkDevice) == 0x6A0 && sizeof(CScreenElementManager) == 0x6A4,
              "layout must match the original");

namespace {

using Script::CStruct;

// Names (checksums).
constexpr uint32_t kTagLocalId = 0x9494A525, kChild = 0xDD4CABD6, kUnHide = 0xB60D1F35, kHide = 0x5B6634D4;
constexpr uint32_t kDeviceNum = 0xC9428A08, kTheScore = 0x675ADBAA;
// Events.
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3;
constexpr uint32_t kFocusSentEvent = 0x3056434F, kFocusClearedEvent = 0x4A57316B;
// The script array of the pad event types ("pad_event_types").
const char* PadEventTypesName() { return reinterpret_cast<const char*>(0x0064A10C); }

void** Vtable(uint32_t address) { return reinterpret_cast<void**>(address); }

CScreenElement* AsElement(Obj::CObject* object) { return static_cast<CScreenElement*>(object); }

void MarkRead(Obj::CEvent* event, uint32_t how, uint32_t script) { event->MarkRead(how, script); }

void VSetProperties(CScreenElement* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CScreenElement*, CStruct*)>(e->m_vtable[1])(e, params);
}

// A structure with one flag, given to `element`'s SetProperties (Hide / UnHide).
void SetFlagProperty(CScreenElement* element, uint32_t flag)
{
    CStruct* params = Script::NewStruct();
    if (params)
        params = params->Init();
    params->AddChecksum(0u, flag);
    VSetProperties(element, params);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// --- construction ---------------------------------------------------------------------------------------------

// 0x004AB990
CScreenElementManager* CScreenElementManager::Construct()
{
    Obj::CBaseManager::Construct();
    m_listener.Construct();
    m_listener.m_vtable = Vtable(0x0064A104);
    m_vtable = Vtable(0x0064A0E8);
    mp_root.mp_prev = mp_root.mp_next = nullptr;
    mp_root.mp_object = nullptr;
    mp_resolving.mp_prev = mp_resolving.mp_next = nullptr;
    mp_resolving.mp_object = nullptr;
    for (FocusEntry& entry : m_focusEntries) {
        entry.element.mp_object = nullptr;
        entry.element.mp_prev = entry.element.mp_next = nullptr;
    }
    for (PriorityEntry& entry : m_priorities) {
        entry.element.mp_object = nullptr;
        entry.element.mp_prev = entry.element.mp_next = nullptr;
    }
    mp_root.Release();
    mp_resolving.Release();
    m_listener.RegisterWithTracker(nullptr);
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
void CScreenElementManager::DestroyBody()
{
    m_vtable = Vtable(0x0064A0E8);
    m_listener.m_vtable = Vtable(0x0064A104);
    for (int i = kPriorities - 1; i >= 0; --i)
        m_priorities[i].element.Release();
    for (int i = kFocusEntries - 1; i >= 0; --i)
        m_focusEntries[i].element.Release();
    mp_resolving.Release();
    mp_root.Release();
    m_listener.Destroy();
    Obj::CBaseManager::DestroyBody();
}

// 0x004ACB40
CScreenElementManager* CScreenElementManager::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AAD40
Obj::CObjectListHead* CScreenElementManager::GetObjectList() { return &m_objects; }

// 0x004AAFF0
void CScreenElementManager::UnregisterObject(Obj::CObject* object)
{
    ElementPtr element;
    element.Set(AsElement(object));
    if (element && mp_root.mp_object == element.mp_object) {
        mp_root.Release();
        for (Mem::ListNode* node = m_objects.next; node->data != reinterpret_cast<void*>(-1); node = node->next) {
            ElementPtr candidate;
            candidate.Set(static_cast<CScreenElement*>(node->data));
            if (!candidate->mp_parent && candidate.mp_object != element.mp_object) {
                mp_root.Assign(&candidate);
                break;
            }
        }
    }
    Obj::CBaseManager::UnregisterObject(object);
}

// --- elements -------------------------------------------------------------------------------------------------

// 0x004AAE20
ElementPtr* CScreenElementManager::GetElement(ElementPtr* out, uint32_t id, int)
{
    Obj::CObject* object = Obj::Tracker()->LookUpObject(id);
    out->mp_prev = out->mp_next = nullptr;
    out->mp_object = nullptr;
    if (object)
        out->Set(AsElement(object));
    return out;
}

// 0x004AB730
void CScreenElementManager::SetRoot(uint32_t id)
{
    ElementPtr element;
    GetElement(&element, id, 1);
    mp_root.Set(element.mp_object);
}

// 0x004ABBE0
void CScreenElementManager::SetParent(ElementPtr* parent, ElementPtr* child, int unused)
{
    ElementPtr oldParent;
    oldParent.Set(child->mp_object->mp_parent.mp_object);
    if (CScreenElement* newParent = parent->mp_object) {
        if (mp_root.mp_object == child->mp_object || !mp_root) {
            mp_root.Set(newParent);
            while (mp_root->mp_parent)
                mp_root.Set(mp_root->mp_parent.mp_object);
        }
    }
    RemoveFocusEntries(child, false, false);
    child->mp_object->SetParent(parent, unused);
}

// 0x004AC400
void CScreenElementManager::DestroyTree(int keep, ElementPtr* element, Script::CScript* script)
{
    element->mp_object->SetFocusSent(0);
    ElementPtr child;
    child.Set(element->mp_object->mp_firstChild.mp_object);
    while (child) {
        ElementPtr next;
        next.Set(child->mp_next.mp_object);
        DestroyTree(0, &child, script);
        child.Set(next.mp_object);
    }
    if (keep == 0) {
        if (script && element->mp_object)
            script->ObjectDestroyed(element->mp_object);
        {
            ElementPtr none;
            SetParent(&none, element, 1);
        }
        VirtualUnregisterObject(element->mp_object);
        if (element->mp_object)
            element->mp_object->VirtualDestroy(1);
    }
}

// 0x004AD100
void CScreenElementManager::DestroyElement(uint32_t id, int recurse, int keep, Script::CScript* script)
{
    ElementPtr element;
    GetElement(&element, id, 1);
    if (recurse) {
        RemoveFocusEntries(&element, false, false);
        if (CScreenElement* parent = element->mp_parent.mp_object)
            parent->SetFocusSent(0);
        DestroyTree(keep, &element, script);
    } else {
        RemoveFocusEntries(&element, false, false);
        if (script && element.mp_object)
            script->ObjectDestroyed(element.mp_object);
        VirtualUnregisterObject(element.mp_object);
        if (element.mp_object)
            element.mp_object->VirtualDestroy(1);
    }
}

// 0x004AB130
ElementPtr* CScreenElementManager::FindByLocalId(ElementPtr* out, const ElementPtr* parent, uint32_t localId)
{
    ElementPtr child;
    child.Set(parent->mp_object->mp_firstChild.mp_object);
    while (child) {
        uint32_t tag;
        if (child->GetChecksumTag(kTagLocalId, &tag) && tag == localId)
            break;
        child.Set(child->mp_next.mp_object);
    }
    out->mp_prev = out->mp_next = nullptr;
    out->mp_object = nullptr;
    out->Set(child.mp_object);
    return out;
}

// 0x004AC120
bool CScreenElementManager::ResolveId(Script::CStruct* params, uint32_t name, uint32_t* out)
{
    CStruct* path = nullptr;
    if (params->GetStructure(name, &path, 0)) {
        uint32_t id = 0;
        path->GetChecksum(0u, &id, 0);
        int index = -1;
        path->GetInteger(0u, &index, 0);
        if (id) {
            ElementPtr found;
            if (!mp_resolving)
                GetElement(&found, id, 0);
            else
                FindByLocalId(&found, &mp_resolving, id);
            mp_resolving.Set(found.mp_object);
            found.Release();
            if (!mp_resolving)
                return false;
        } else if (index != -1) {
            mp_resolving.Set(mp_resolving->GetChildByIndex(index));
            if (!mp_resolving)
                return false;
        }
        bool resolved = ResolveId(path, kChild, out);
        mp_resolving.Release();
        if (resolved)
            return true;
    }
    uint32_t id;
    if (params->GetChecksum(name, &id, 0)) {
        if (mp_resolving) {
            ElementPtr found;
            FindByLocalId(&found, &mp_resolving, id);
            mp_resolving.Set(found.mp_object);
            found.Release();
            if (!mp_resolving)
                return false;
            id = mp_resolving->m_id;
        } else if (Obj::CObject* aliased = Obj::Tracker()->GetObjectByAlias(id)) {
            id = aliased->m_id;
        }
        mp_resolving.Release();
        *out = id;
        return true;
    }
    int index;
    if (params->GetInteger(name, &index, 0)) {
        if (!mp_resolving)
            return false;
        ElementPtr child;
        child.Set(mp_resolving->GetChildByIndex(index));
        mp_resolving.Release();
        if (!child)
            return false;
        *out = child->m_id;
        return true;
    }
    mp_resolving.Release();
    return false;
}

// 0x004AD1E0
ElementPtr* CScreenElementManager::ResolveElement(ElementPtr* out, Script::CStruct* params, uint32_t name, int assert)
{
    uint32_t id;
    if (ResolveId(params, name, &id))
        return GetElement(out, id, assert);
    out->mp_prev = out->mp_next = nullptr;
    out->mp_object = nullptr;
    return out;
}

// --- each frame -----------------------------------------------------------------------------------------------

// 0x004ABD00
void CScreenElementManager::Update()
{
    if (!mp_root)
        return;
    // Not original - the new front end (game/front/new_frontend.h): the original's elements hidden or shown for this
    // frame before they are laid out - a menu restyled by ours is never drawn as the game made it, not even a frame.
    // Original behaviour: the tree laid out as the scripts left it.
    Front::NewFrontend::BeforeScreenLayout();
    LockTree(1);
    mp_root->UpdateLayout();
    LockTree(0);
    if (m_numPriorities > 0)
        UpdatePriorities();
}

// 0x004AAF90 (a stack of 32 elements: the next sibling, then the first child, of each)
void CScreenElementManager::LockTree(int lock)
{
    if (!mp_root)
        return;
    CScreenElement* stack[32];
    stack[0] = mp_root.mp_object;
    int count = 1;
    while (count) {
        CScreenElement* element = stack[--count];
        if (lock == 1)
            ++element->m_aliasCount;
        else if (static_cast<int>(element->m_aliasCount) > 0)
            --element->m_aliasCount;
        if (element->mp_next)
            stack[count++] = element->mp_next.mp_object;
        if (element->mp_firstChild)
            stack[count++] = element->mp_firstChild.mp_object;
    }
}

// 0x004AAEE0
void CScreenElementManager::SetPausedAll(bool pause)
{
    Mem::ListNode* node = m_objects.next;
    if (node->data == reinterpret_cast<void*>(-1))
        return;
    while (node) {
        ElementPtr element;
        element.Set(static_cast<CScreenElement*>(node->data));
        element->SetPaused(pause);
        node = node->next;
        if (node->data == reinterpret_cast<void*>(-1))
            node = nullptr;
    }
}

// --- focus and the pad ----------------------------------------------------------------------------------------

// 0x004AB250 (a new entry goes after the last entry looked at - the second list's tail when it has entries,
// whichever list it is for -, or heads `list` when both are empty)
void CScreenElementManager::AddFocusEntry(ElementPtr* element, int list)
{
    FocusEntry* last = nullptr;
    for (int i = 0; i < 2; ++i) {
        for (FocusEntry* entry = mp_focusLists[i]; entry; entry = entry->next) {
            if (entry->element.mp_object == element->mp_object && list == i) {
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
    entry->element.Set(element->mp_object);
    entry->id = element->mp_object->m_id;
    entry->next = nullptr;
    entry->marked = false;
    if (last)
        last->next = entry;
    else
        mp_focusLists[list] = entry;
    m_focusListChanged[list] = true;
}

// 0x004AB330
void CScreenElementManager::RemoveFocusEntries(ElementPtr* element, bool fromParent, bool mark)
{
    for (int i = 0; i < 2; ++i) {
        FocusEntry* previous = nullptr;
        FocusEntry* entry = mp_focusLists[i];
        while (entry) {
            FocusEntry* next = entry->next;
            ElementPtr walk;
            walk.Set(entry->element.mp_object);
            if (fromParent && walk)
                walk.Set(walk->mp_parent.mp_object);
            bool matched = false;
            for (; walk; walk.Set(walk->mp_parent.mp_object)) {
                if (walk.mp_object == element->mp_object) {
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
void CScreenElementManager::ClearMarks(ElementPtr* element)
{
    for (int i = 0; i < 2; ++i) {
        for (FocusEntry* entry = mp_focusLists[i]; entry; entry = entry->next) {
            if (!entry->marked)
                continue;
            ElementPtr walk;
            walk.Set(entry->element.mp_object);
            for (; walk; walk.Set(walk->mp_parent.mp_object)) {
                if (walk.mp_object == element->mp_object) {
                    entry->marked = false;
                    break;
                }
            }
        }
    }
}

// 0x004ABD40
void CScreenElementManager::ListenerReceive(Obj::CEvent* event)
{
    // (`this` is the listener: the manager is 0x28 before it)
    CScreenElementManager* manager =
        reinterpret_cast<CScreenElementManager*>(reinterpret_cast<uint8_t*>(this) - offsetof(CScreenElementManager, m_listener));
    if (manager->m_numPadEventTypes == 0) {
        Script::CArray* types = Script::GetArray(PadEventTypesName()); // (the original passes 1 as well: unused)
        manager->m_numPadEventTypes = static_cast<int>(types->m_size);
        for (int i = 0; i < manager->m_numPadEventTypes; ++i)
            manager->m_padEventTypes[i] = types->GetElement(i);
    }
    bool padEvent = false;
    if (manager->m_numPriorities > 0) {
        for (int i = 0; i < manager->m_numPadEventTypes; ++i) {
            if (event->m_type == manager->m_padEventTypes[i]) {
                padEvent = true;
                break;
            }
        }
    }
    if (manager->m_checkDevice && event->mp_data) {
        int device;
        if (event->mp_data->GetInteger(kDeviceNum, &device, 0)) {
            auto* input = GameVariable<Inp::Manager*>(0x00696D40); // (the input manager)
            if (input->GetPlayerController(0) != device)
                return;
        }
    }
    Obj::CTracker* tracker = Obj::Tracker();
    if (event->m_type == kFocus && event->m_target != kSystem) {
        ElementPtr element;
        manager->GetElement(&element, event->m_target, 0);
        if (element && !element->EventsBlocked()) {
            manager->AddFocusEntry(&element, Obj::EventController(event));
            MarkRead(event, 1, 0);
        }
    }
    if (event->m_type == kUnfocus && event->m_target != kSystem) {
        ElementPtr element;
        manager->GetElement(&element, event->m_target, 0);
        if (element && !element->EventsBlocked()) {
            manager->RemoveFocusEntries(&element, false, false);
            MarkRead(event, 1, 0);
        }
    }
    if (event->m_type == kFocusClearedEvent) {
        ElementPtr element;
        manager->GetElement(&element, event->m_target, 0);
        manager->RemoveFocusEntries(&element, true, true);
        MarkRead(event, 1, 0);
    }
    if (event->m_type == kFocusSentEvent) {
        ElementPtr element;
        manager->GetElement(&element, event->m_target, 0);
        manager->ClearMarks(&element);
        MarkRead(event, 1, 0);
        element.Release();
    }
    // a pad event from the system: offered to the controller's focused elements until one handles it (a
    // change of the list starts the round again with the elements not offered it yet)
    bool isPadType = false;
    for (int i = 0; i < manager->m_numPadEventTypes; ++i) {
        if (event->m_type == manager->m_padEventTypes[i]) {
            isPadType = true;
            break;
        }
    }
    if (!isPadType || event->m_target != kSystem)
        return;
    int controller = Obj::EventController(event);
    for (FocusEntry* entry = manager->mp_focusLists[controller]; entry; entry = entry->next)
        entry->tried = false;
    manager->m_focusListChanged[controller] = true;
    do {
        manager->m_focusListChanged[controller] = false;
        for (FocusEntry* entry = manager->mp_focusLists[controller]; entry; entry = entry->next) {
            if (!entry->tried && !entry->marked && !entry->element->EventsBlocked()) {
                if (padEvent && (!entry->element->HasPriority() || !manager->IsTopPriority(entry->id)))
                    continue;
                if (tracker->LaunchEvent(event->m_type, entry->element->m_id, event->m_source, event->mp_data, false,
                                         -1.0f)) {
                    event->MarkHandled(1, 0);
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
bool CScreenElementManager::HasPriorities() const { return m_numPriorities > 0; }

// 0x004AAD50
bool CScreenElementManager::IsPadEvent(uint32_t type) const
{
    for (int i = 0; i < m_numPadEventTypes; ++i)
        if (type == m_padEventTypes[i])
            return true;
    return false;
}

// 0x004AB7C0
bool CScreenElementManager::IsTopPriority(uint32_t id)
{
    ElementPtr element;
    GetElement(&element, id, 1);
    uint32_t priority = static_cast<uint32_t>(element->GetPriority());
    uint32_t top = 0;
    for (const PriorityEntry& entry : m_priorities)
        if (entry.active && entry.priority > top)
            top = entry.priority;
    return priority == top;
}

// 0x004AC570
bool CScreenElementManager::PushPriority(uint32_t id, uint32_t priority)
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
    GetElement(&element, id, 1);
    entry.element.Set(element.mp_object);
    entry.priority = priority;
    if (m_numPriorities > 0) {
        // the top one shown: the new one hidden unless it is the new top, then the old top is
        CStruct* params = Script::NewStruct();
        if (params)
            params = params->Init();
        params->AddChecksum(0u, kHide);
        if (priority > highest)
            VSetProperties(m_priorities[highestSlot].element.mp_object, params);
        else
            VSetProperties(element.mp_object, params);
        if (params) {
            params->Clear();
            Script::DeleteStruct(params);
        }
    }
    ++m_numPriorities;
    return true;
}

// 0x004AB5D0 (with no priority above 0 the "top" is the entry before the first)
void CScreenElementManager::UpdatePriorities()
{
    uint32_t highest = 0;
    int top = -1;
    if (m_numPriorities <= 0)
        return;
    for (int i = 0; i < kPriorities; ++i) {
        PriorityEntry& entry = m_priorities[i];
        if (!entry.active)
            continue;
        CScreenElement* element;
        {
            ElementPtr found;
            element = GetElement(&found, entry.id, 0)->mp_object;
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
    SetFlagProperty(m_priorities[top].element.mp_object, kUnHide);
}

// --- the singleton --------------------------------------------------------------------------------------------

// 0x004ACAF0
void CreateScreenElementManager(bool create)
{
    CScreenElementManager*& instance = CScreenElementManager::Instance();
    if (!instance && create) {
        void* memory = Mem::Calloc(sizeof(CScreenElementManager), static_cast<Mem::Heap*>(nullptr), 1);
        if (memory) {
            instance = static_cast<CScreenElementManager*>(memory)->Construct();
            ++GameVariable<uint32_t>(0x00701444);
            return;
        }
        instance = nullptr;
    }
    ++GameVariable<uint32_t>(0x00701444);
}

// 0x004AACF0
void DeleteScreenElementManager()
{
    uint32_t& uses = GameVariable<uint32_t>(0x00701444);
    if (uses > 1) {
        --uses;
        return;
    }
    if (CScreenElementManager* instance = CScreenElementManager::Instance())
        reinterpret_cast<void*(__thiscall*)(void*, int)>(instance->m_vtable[0])(instance, 1);
    CScreenElementManager::Instance() = nullptr;
    uses = 0;
}

// 0x004AB910
void SetScoreText(const char* text, int player)
{
    ElementPtr element;
    CScreenElementManager::Instance()->GetElement(&element, kTheScore + player, 0);
    if (element)
        static_cast<CTextElement*>(element.mp_object)->SetText(text);
}

} // namespace Front

namespace {

// 0x004ABAF0: the listener's slot 0 (`this` = the listener): the manager's Destroy.
struct ListenerAdapter {
    Front::CScreenElementManager* Destroy(uint8_t flags)
    {
        auto* manager = reinterpret_cast<Front::CScreenElementManager*>(reinterpret_cast<uint8_t*>(this) - 0x28);
        return manager->Destroy(flags);
    }
};

} // namespace

using Front::CScreenElementManager;

REPLACE(0x004AB990, &CScreenElementManager::Construct);
REPLACE(0x004ABB00, &CScreenElementManager::DestroyBody);
REPLACE(0x004ACB40, &CScreenElementManager::Destroy);
REPLACE(0x004ABAF0, &ListenerAdapter::Destroy);
REPLACE(0x004AAFF0, &CScreenElementManager::UnregisterObject);
REPLACE_SOURCE_ONLY(0x004AAD40, &CScreenElementManager::GetObjectList); // 4 bytes
REPLACE(0x004AAE20, &CScreenElementManager::GetElement);
REPLACE(0x004AB730, &CScreenElementManager::SetRoot);
REPLACE(0x004ABBE0, &CScreenElementManager::SetParent);
REPLACE(0x004AC400, &CScreenElementManager::DestroyTree);
REPLACE(0x004AD100, &CScreenElementManager::DestroyElement);
REPLACE(0x004AB130, &CScreenElementManager::FindByLocalId);
REPLACE(0x004AC120, &CScreenElementManager::ResolveId);
REPLACE(0x004AD1E0, &CScreenElementManager::ResolveElement);
REPLACE(0x004ABD00, &CScreenElementManager::Update);
REPLACE(0x004AAF90, &CScreenElementManager::LockTree);
REPLACE(0x004AAEE0, &CScreenElementManager::SetPausedAll);
REPLACE(0x004AB250, &CScreenElementManager::AddFocusEntry);
REPLACE(0x004AB330, &CScreenElementManager::RemoveFocusEntries);
REPLACE(0x004AB4D0, &CScreenElementManager::ClearMarks);
REPLACE(0x004ABD40, &CScreenElementManager::ListenerReceive);
REPLACE(0x004AAD80, &CScreenElementManager::HasPriorities);
REPLACE(0x004AAD50, &CScreenElementManager::IsPadEvent);
REPLACE(0x004AB7C0, &CScreenElementManager::IsTopPriority);
REPLACE(0x004AC570, &CScreenElementManager::PushPriority);
REPLACE(0x004AB5D0, &CScreenElementManager::UpdatePriorities);
REPLACE(0x004ACAF0, Front::CreateScreenElementManager);
REPLACE(0x004AACF0, Front::DeleteScreenElementManager);
REPLACE(0x004AB910, Front::SetScoreText);
