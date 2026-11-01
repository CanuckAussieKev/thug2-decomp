#pragma once

#include <cstddef>
#include <cstdint>

#include "frontend/screen_element.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity_hub.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The screen element manager (MenuSystem::UiTree, 0x6A4 bytes, the singleton 0x00701440 with its use
// count 0x00701444): the manager of every screen element object, the root of the tree (laid out each frame), the
// elements' ids in scripts (resolve_id: an id, an alias, or a { parent / local id, child } path), which elements
// have the focus per controller (from the focus / unfocus events it listens to) and get the pad events, and the
// pad-event priorities (Priority: only the top element of up to 10 gets the pad events; the others are hidden).
namespace frontend {

class UiTree;
extern UiTree* g_ui_element_hub; // the singleton

class UiTree final : public objects::ObjectHubBase {
public:
    // A focused element of one controller's list (0x18 bytes).
    struct FocusEntry {
        ElementPtr element; // +0x00 (null: a free entry)
        uint32_t id;        // +0x0C the element's id
        FocusEntry* next;   // +0x10
        bool tried;         // +0x14 already offered the current pad event
        bool marked;        // +0x15 its focus is being dropped (an ancestor lost its focus flag)
        uint8_t pad16[2];
    };
    // An element with a pad-event priority (0x18 bytes).
    struct PriorityEntry {
        ElementPtr element; // +0x00
        uint32_t id;        // +0x0C
        uint32_t priority;  // +0x10
        bool active;        // +0x14
        uint8_t pad15[3];
    };
    enum { kFocusEntries = 0x30, kPriorities = 10, kPadEventSlots = 0x30 };

    static UiTree*& sole() { return g_ui_element_hub; }

    // 0x004AB990: vtables 0x0064A0E8 / 0x0064A104 (the listener), no root, the listener on the tracker's list for no
    // object, no focus, no priorities, no pad event types read yet.
    UiTree();
    // 0x004ABB00 (0x004ACB40 the deleting one; 0x004ABAF0 the same from the listener): the entries' smart
    // pointers, the root, the listener, the object list.
    ~UiTree() override;

    // --- the manager's virtuals (vtable 0x0064A0E8; 0x004AAD30, slot 1, jumps to enrol_subject 0x00461A00) ---
    // 0x004AAFF0: the root unregistered: the first element without a parent becomes the root.
    void drop_entity_registration(objects::Entity* object) override;
    // (slot 5: an empty stub - a dead element stays where it is)
    void remove_entity(objects::Entity*) override {}

    // --- elements ---
    // 0x004AAE20: the object with the id (any object: the tracker's lookup) as a smart pointer into `out`;
    // `this` and `assert` aren't used.
    ElementPtr* fetch_part(ElementPtr* out, uint32_t id, int assert);
    // 0x004ACB70 (screen_element_cfuncs.cpp): a new element of `type` (none for an unknown type) on the heap
    // `params` names (none given: the front end's; Default: the current one), with the id, registered, its
    // properties and morph from `params`, a pad-event priority if given; into `out`.
    ElementPtr* spawn_part(ElementPtr* out, uint32_t type, uint32_t id, script::Struct* params);
    // 0x004AB730: the root (the element `id`).
    void change_root(uint32_t id);
    // 0x004ABBE0: `child` under `parent` (UiWidget::change_owner; the third argument is passed on): a root
    // that was the child (or none) becomes the new parent's topmost ancestor; the child's focus entries go.
    void change_owner(ElementPtr* parent, ElementPtr* child, int unused);
    // 0x004AC400: `element`'s children destroyed (depth first), then - unless `keep` - the element itself: the
    // script forgets it, out of the tree, unregistered, deleted. Its focus flag is cleared first.
    void destroy_tree(int keep, ElementPtr* element, script::Script* script);
    // 0x004AD100: the element `id` (asserting) destroyed: with `recurse` its tree (destroy_tree, its parent's focus
    // flag cleared first); else just the element (out of the tree, unregistered, deleted).
    void dismantle_part(uint32_t id, int recurse, int keep, script::Script* script);
    // 0x004AB130: the child of `parent` with the tag_local_id `local_id` (null: none) into `out`.
    ElementPtr* find_by_local_id(ElementPtr* out, const ElementPtr* parent, uint32_t local_id);
    // 0x004AC120: an element id from `params`' `name`: a structure (an unnamed id or index - an index of the
    // element found so far - then its `child` resolved within it), an id (a local id within the element found
    // so far, an alias, or itself), or an index. False if none.
    bool resolve_id(script::Struct* params, uint32_t name, uint32_t* out);
    // 0x004AD1E0: resolve_id's element (looked up with `assert`) into `out`.
    ElementPtr* resolve_element(ElementPtr* out, script::Struct* params, uint32_t name, int assert);

    // --- each frame ---
    // 0x004ABD00: the tree locked, laid out, unlocked; the priorities checked.
    void tick_frame();
    // 0x004AAF90: every element's use count up (1) or down.
    void lock_tree(int lock);
    // 0x004AAEE0: every element paused or not (the front end's pause).
    void set_paused_all(bool pause);

    // --- focus and the pad ---
    // 0x004AB250: a focus entry for `element` in controller `list`'s list (an existing one unmarked).
    void add_focus_entry(ElementPtr* element, int list);
    // 0x004AB330: the entries of `element` (with `from_parent`: of the elements below it) dropped, or with `mark`
    // only marked.
    void remove_focus_entries(ElementPtr* element, bool from_parent, bool mark);
    // 0x004AB4D0: the marks of the entries of `element` and the elements below it cleared.
    void clear_marks(ElementPtr* element);
    // 0x004ABD40 (the listener's filter): focus / unfocus and the focus flag's events update the lists; a pad event
    // goes to the controller's focused elements in turn until one handles it.
    void listener_receive(objects::Signal* event);

    // The manager's event listener: its events go to listener_receive.
    struct TreeListener final : objects::SignalReceiver {
        void receive(objects::Signal* event) override;
    };

    // --- pad-event priorities ---
    // 0x004AAD80: any priorities.
    bool has_priorities() const;
    // 0x004AAD50: whether `type` is a pad event type.
    bool is_pad_event(uint32_t type) const;
    // 0x004AB7C0: whether the element `id` has the top priority.
    bool has_top_precedence(uint32_t id);
    // 0x004AC570: the element `id` takes a priority slot (a free one, else the lowest if it's at least as high);
    // the new top is shown, the others hidden. False if no slot.
    bool push_priority(uint32_t id, uint32_t priority);
    // 0x004AB5D0: elements gone leave their slots; the top one is shown.
    void update_priorities();

    TreeListener listener_;                   // +0x28
    ElementPtr tree_root_;                    // +0x44
    ElementPtr resolving_;                    // +0x50 resolve_id's element so far
    FocusEntry focus_entries_[kFocusEntries]; // +0x5C
    FocusEntry* focus_lists_[2];              // +0x4DC one per controller
    bool focus_dirty_[2];                     // +0x4E4
    uint8_t pad4e6_[2];
    int num_priorities_;                       // +0x4E8
    PriorityEntry priorities_[kPriorities];    // +0x4EC
    uint32_t pad_event_types_[kPadEventSlots]; // +0x5DC (from the script array 0x0064A10C)
    int pad_event_kind_count_;                 // +0x69C
    bool check_device_;                        // +0x6A0 (pad events of other devices ignored)
};

// 0x004ACAF0: the manager made if `create` and there is none (on the heap of Calloc mode 1); the use count up.
void create_screen_element_manager(bool create);
// 0x004AACF0: the use count down; the manager deleted at the last.
void delete_screen_element_manager();

// 0x004AB910 (cdecl): the score text element of `player` (id the_score + player) shows `text`.
void set_score_text(const char* text, int player);

} // namespace frontend
