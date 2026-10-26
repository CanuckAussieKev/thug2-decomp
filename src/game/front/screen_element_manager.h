#pragma once

#include <cstddef>
#include <cstdint>

#include "game/front/screen_element.h"
#include "game/obj/event.h"
#include "game/obj/objman.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The screen element manager (Front::CScreenElementManager, 0x6A4 bytes, the singleton 0x00701440 with its use
// count 0x00701444): the manager of every screen element object, the root of the tree (laid out each frame), the
// elements' ids in scripts (ResolveId: an id, an alias, or a { parent / local id, child } path), which elements
// have the focus per controller (from the focus / unfocus events it listens to) and get the pad events, and the
// pad-event priorities (Priority: only the top element of up to 10 gets the pad events; the others are hidden).
namespace Front {

class CScreenElementManager : public Obj::CBaseManager {
public:
    // A focused element of one controller's list (0x18 bytes).
    struct FocusEntry {
        ElementPtr element;   // +0x00 (null: a free entry)
        uint32_t id;          // +0x0C the element's id
        FocusEntry* next;     // +0x10
        bool tried;           // +0x14 already offered the current pad event
        bool marked;          // +0x15 its focus is being dropped (an ancestor lost its focus flag)
        uint8_t pad16[2];
    };
    static_assert(sizeof(FocusEntry) == 0x18, "layout must match the original");
    // An element with a pad-event priority (0x18 bytes).
    struct PriorityEntry {
        ElementPtr element;   // +0x00
        uint32_t id;          // +0x0C
        uint32_t priority;    // +0x10
        bool active;          // +0x14
        uint8_t pad15[3];
    };
    static_assert(sizeof(PriorityEntry) == 0x18, "layout must match the original");
    enum { kFocusEntries = 0x30, kPriorities = 10, kMaxPadEventTypes = 0x30 };

    static CScreenElementManager*& Instance() { return GameVariable<CScreenElementManager*>(0x00701440); }

    // 0x004AB990: the constructor (returns this): vtables 0x0064A0E8 / 0x0064A104 (the listener), no root, the
    // listener on the tracker's list for no object, no focus, no priorities, no pad event types read yet.
    CScreenElementManager* Construct();
    // 0x004ABB00: the destructor body (the entries' smart pointers, the root, the listener, the object list).
    void DestroyBody();
    // 0x004ACB40 (vtable slot 0): the scalar deleting destructor. 0x004ABAF0: the same from the listener.
    CScreenElementManager* Destroy(uint8_t flags);

    // --- the vtable (0x0064A0E8: CBaseManager's with 0, 2 and 6 overridden; 0x004AAD30 slot 1 jumps to
    //     RegisterObject 0x00461A00) ---
    // 0x004AAFF0 (slot 2): the root unregistered: the first element without a parent becomes the root.
    void UnregisterObject(Obj::CObject* object);
    // 0x004AAD40 (slot 6): the object list's head.
    Obj::CObjectListHead* GetObjectList();

    // --- elements ---
    // 0x004AAE20: the object with the id (any object: the tracker's lookup) as a smart pointer into `out`;
    // `this` and `assert` aren't used.
    ElementPtr* GetElement(ElementPtr* out, uint32_t id, int assert);
    // 0x004ACB70 (screen_element_cfuncs.cpp): a new element of `type` (none for an unknown type) on the heap
    // `params` names (none given: the front end's; Default: the current one), with the id, registered, its
    // properties and morph from `params`, a pad-event priority if given; into `out`.
    ElementPtr* CreateElement(ElementPtr* out, uint32_t type, uint32_t id, Script::CStruct* params);
    // 0x004AB730: the root (the element `id`).
    void SetRoot(uint32_t id);
    // 0x004ABBE0: `child` under `parent` (CScreenElement::SetParent; the third argument is passed on): a root
    // that was the child (or none) becomes the new parent's topmost ancestor; the child's focus entries go.
    void SetParent(ElementPtr* parent, ElementPtr* child, int unused);
    // 0x004AC400: `element`'s children destroyed (depth first), then - unless `keep` - the element itself: the
    // script forgets it, out of the tree, unregistered, deleted. Its focus flag is cleared first.
    void DestroyTree(int keep, ElementPtr* element, Script::CScript* script);
    // 0x004AD100: the element `id` (asserting) destroyed: with `recurse` its tree (DestroyTree, its parent's focus
    // flag cleared first); else just the element (out of the tree, unregistered, deleted).
    void DestroyElement(uint32_t id, int recurse, int keep, Script::CScript* script);
    // 0x004AB130: the child of `parent` with the tag_local_id `localId` (null: none) into `out`.
    ElementPtr* FindByLocalId(ElementPtr* out, const ElementPtr* parent, uint32_t localId);
    // 0x004AC120: an element id from `params`' `name`: a structure (an unnamed id or index - an index of the
    // element found so far - then its `child` resolved within it), an id (a local id within the element found
    // so far, an alias, or itself), or an index. False if none.
    bool ResolveId(Script::CStruct* params, uint32_t name, uint32_t* out);
    // 0x004AD1E0: ResolveId's element (looked up with `assert`) into `out`.
    ElementPtr* ResolveElement(ElementPtr* out, Script::CStruct* params, uint32_t name, int assert);

    // --- each frame ---
    // 0x004ABD00: the tree locked, laid out, unlocked; the priorities checked.
    void Update();
    // 0x004AAF90: every element's use count up (1) or down.
    void LockTree(int lock);
    // 0x004AAEE0: every element paused or not (the front end's pause).
    void SetPausedAll(bool pause);

    // --- focus and the pad ---
    // 0x004AB250: a focus entry for `element` in controller `list`'s list (an existing one unmarked).
    void AddFocusEntry(ElementPtr* element, int list);
    // 0x004AB330: the entries of `element` (with `fromParent`: of the elements below it) dropped, or with `mark`
    // only marked.
    void RemoveFocusEntries(ElementPtr* element, bool fromParent, bool mark);
    // 0x004AB4D0: the marks of the entries of `element` and the elements below it cleared.
    void ClearMarks(ElementPtr* element);
    // 0x004ABD40 (the listener's vtable slot 1, `this` = the listener at +0x28): focus / unfocus and the focus
    // flag's events update the lists; a pad event goes to the controller's focused elements in turn until one
    // handles it.
    void ListenerReceive(Obj::CEvent* event);

    // --- pad-event priorities ---
    // 0x004AAD80: any priorities.
    bool HasPriorities() const;
    // 0x004AAD50: whether `type` is a pad event type.
    bool IsPadEvent(uint32_t type) const;
    // 0x004AB7C0: whether the element `id` has the top priority.
    bool IsTopPriority(uint32_t id);
    // 0x004AC570: the element `id` takes a priority slot (a free one, else the lowest if it's at least as high);
    // the new top is shown, the others hidden. False if no slot.
    bool PushPriority(uint32_t id, uint32_t priority);
    // 0x004AB5D0: elements gone leave their slots; the top one is shown.
    void UpdatePriorities();

    Obj::CEventListener m_listener;                  // +0x28
    ElementPtr mp_root;                              // +0x44
    ElementPtr mp_resolving;                         // +0x50 ResolveId's element so far
    FocusEntry m_focusEntries[kFocusEntries];        // +0x5C
    FocusEntry* mp_focusLists[2];                    // +0x4DC one per controller
    bool m_focusListChanged[2];                      // +0x4E4
    uint8_t pad4E6[2];
    int m_numPriorities;                             // +0x4E8
    PriorityEntry m_priorities[kPriorities];         // +0x4EC
    uint32_t m_padEventTypes[kMaxPadEventTypes];     // +0x5DC (from the script array 0x0064A10C)
    int m_numPadEventTypes;                          // +0x69C
    bool m_checkDevice;                              // +0x6A0 (pad events of other devices ignored)
    uint8_t pad6A1[3];
};

// 0x004ACAF0: the manager made if `create` and there is none (on the heap of Calloc mode 1); the use count up.
void CreateScreenElementManager(bool create);
// 0x004AACF0: the use count down; the manager deleted at the last.
void DeleteScreenElementManager();

// 0x004AB910 (cdecl): the score text element of `player` (id the_score + player) shows `text`.
void SetScoreText(const char* text, int player);

} // namespace Front
