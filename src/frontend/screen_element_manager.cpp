// The screen element manager: see screen_element_manager.h.

#include "frontend/screen_element_manager.h"

#include <new>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "frontend/new_frontend.h"
#include "frontend/text_element.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "platform/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity_hub.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace frontend {

UiTree* g_ui_element_hub;

namespace {

using script::Struct;

// Names (checksums).
constexpr uint32_t kTagLocalId = 0x9494A525, kChild = 0xDD4CABD6, kUnHide = 0xB60D1F35, kHide = 0x5B6634D4;
constexpr uint32_t kDeviceNum = 0xC9428A08, kScoreElementHash = 0x675ADBAA;
// Events.
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3;
constexpr uint32_t kFocusSentEvent = 0x3056434F, kFocusClearedEvent = 0x4A57316B;
// The script array of the pad event types ("pad_event_types").
const char* pad_event_types_name() { return "pad_event_types"; }

UiWidget* as_element(objects::Entity* object) { return core::down_cast<UiWidget>(object); }

void set_event_consumed(objects::Signal* event, uint32_t how, uint32_t script)
{
    event->set_event_consumed(how, script);
}

// A structure with one flag, given to `element`'s change_traits (Hide / UnHide).
void set_flag_property(UiWidget* element, uint32_t flag)
{
    Struct* params = script::spawn_param_bag();
    if (params)
        params = new (params) script::Struct;
    params->put_name_key(0u, flag);
    element->change_traits(params);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// --- construction ---------------------------------------------------------------------------------------------

// 0x004AB990 (the smart pointers start empty)
UiTree::UiTree()
{
    listener_.enter_wait_board(nullptr);
    for (int i = 0; i < 2; ++i) {
        focus_lists_[i] = nullptr;
        focus_dirty_[i] = false;
    }
    for (FocusEntry& entry : focus_entries_) {
        entry.element.get_release();
        entry.next = nullptr;
    }
    pad_event_kind_count_ = 0;
    std::memset(pad_event_types_, 0, sizeof(pad_event_types_));
    check_device_ = false;
    num_priorities_ = 0;
    for (PriorityEntry& entry : priorities_)
        entry.active = false;
}

// 0x004ABB00 (0x004ACB40 the deleting one): the entries' smart pointers, the root; then (the members) the listener,
// and ObjectHubBase's.
UiTree::~UiTree()
{
    for (int i = kPriorities - 1; i >= 0; --i)
        priorities_[i].element.get_release();
    for (int i = kFocusEntries - 1; i >= 0; --i)
        focus_entries_[i].element.get_release();
    resolving_.get_release();
    tree_root_.get_release();
}

// 0x004AAFF0
void UiTree::drop_entity_registration(objects::Entity* object)
{
    ElementPtr element;
    element.assign_value(as_element(object));
    if (element && tree_root_.owner_object_ == element.owner_object_) {
        tree_root_.get_release();
        for (const memory::ListNode* node = entities_.next; node->data != memory::ListNode::head_marker();
             node = node->next) {
            ElementPtr candidate;
            candidate.assign_value(static_cast<UiWidget*>(node->data));
            if (!candidate->container_ && candidate.owner_object_ != element.owner_object_) {
                tree_root_.get_assign(&candidate);
                break;
            }
        }
    }
    objects::ObjectHubBase::drop_entity_registration(object);
}

// --- elements -------------------------------------------------------------------------------------------------

// 0x004AAE20
ElementPtr* UiTree::fetch_part(ElementPtr* out, uint32_t id, int)
{
    objects::Entity* object = objects::get_tracker()->look_up_object(id);
    out->preceding_ = out->following_ = nullptr;
    out->owner_object_ = nullptr;
    if (object)
        out->assign_value(as_element(object));
    return out;
}

// 0x004AB730
void UiTree::change_root(uint32_t id)
{
    ElementPtr element;
    fetch_part(&element, id, 1);
    tree_root_.assign_value(element.owner_object_);
}

// 0x004ABBE0
void UiTree::change_owner(ElementPtr* parent, ElementPtr* child, int unused)
{
    ElementPtr old_parent;
    old_parent.assign_value(child->owner_object_->container_.owner_object_);
    if (UiWidget* new_parent = parent->owner_object_) {
        if (tree_root_.owner_object_ == child->owner_object_ || !tree_root_) {
            tree_root_.assign_value(new_parent);
            while (tree_root_->container_)
                tree_root_.assign_value(tree_root_->container_.owner_object_);
        }
    }
    remove_focus_entries(child, false, false);
    child->owner_object_->change_owner(parent, unused);
}

// 0x004AC400
void UiTree::destroy_tree(int keep, ElementPtr* element, script::Script* script)
{
    element->owner_object_->set_focus_sent(0);
    ElementPtr child;
    child.assign_value(element->owner_object_->eldest_.owner_object_);
    while (child) {
        ElementPtr next;
        next.assign_value(child->following_.owner_object_);
        destroy_tree(0, &child, script);
        child.assign_value(next.owner_object_);
    }
    if (keep == 0) {
        if (script && element->owner_object_)
            script->object_destroyed(element->owner_object_);
        {
            ElementPtr none;
            change_owner(&none, element, 1);
        }
        drop_entity_registration(element->owner_object_);
        delete element->owner_object_;
    }
}

// 0x004AD100
void UiTree::dismantle_part(uint32_t id, int recurse, int keep, script::Script* script)
{
    ElementPtr element;
    fetch_part(&element, id, 1);
    if (recurse) {
        remove_focus_entries(&element, false, false);
        if (UiWidget* parent = element->container_.owner_object_)
            parent->set_focus_sent(0);
        destroy_tree(keep, &element, script);
    } else {
        remove_focus_entries(&element, false, false);
        if (script && element.owner_object_)
            script->object_destroyed(element.owner_object_);
        drop_entity_registration(element.owner_object_);
        delete element.owner_object_;
    }
}

// 0x004AB130
ElementPtr* UiTree::find_by_local_id(ElementPtr* out, const ElementPtr* parent, uint32_t local_id)
{
    ElementPtr child;
    child.assign_value(parent->owner_object_->eldest_.owner_object_);
    while (child) {
        uint32_t tag = 0;
        if (child->read_hash_tag(kTagLocalId, &tag) && tag == local_id)
            break;
        child.assign_value(child->following_.owner_object_);
    }
    out->preceding_ = out->following_ = nullptr;
    out->owner_object_ = nullptr;
    out->assign_value(child.owner_object_);
    return out;
}

// 0x004AC120
bool UiTree::resolve_id(script::Struct* params, uint32_t name, uint32_t* out)
{
    Struct* path = nullptr;
    if (params->read_sub_struct(name, &path, 0)) {
        uint32_t id = 0;
        path->read_hash_value(0u, &id, 0);
        int index = -1;
        path->fetch_int_value(0u, &index, 0);
        if (id) {
            ElementPtr found;
            if (!resolving_)
                fetch_part(&found, id, 0);
            else
                find_by_local_id(&found, &resolving_, id);
            resolving_.assign_value(found.owner_object_);
            found.get_release();
            if (!resolving_)
                return false;
        } else if (index != -1) {
            resolving_.assign_value(resolving_->find_child_at_slot(index));
            if (!resolving_)
                return false;
        }
        const bool resolved = resolve_id(path, kChild, out);
        resolving_.get_release();
        if (resolved)
            return true;
    }
    uint32_t id = 0;
    if (params->read_hash_value(name, &id, 0)) {
        if (resolving_) {
            ElementPtr found;
            find_by_local_id(&found, &resolving_, id);
            resolving_.assign_value(found.owner_object_);
            found.get_release();
            if (!resolving_)
                return false;
            id = resolving_->ident_;
        } else if (const objects::Entity* aliased = objects::get_tracker()->look_up_by_nickname(id)) {
            id = aliased->ident_;
        }
        resolving_.get_release();
        *out = id;
        return true;
    }
    int index = 0;
    if (params->fetch_int_value(name, &index, 0)) {
        if (!resolving_)
            return false;
        ElementPtr child;
        child.assign_value(resolving_->find_child_at_slot(index));
        resolving_.get_release();
        if (!child)
            return false;
        *out = child->ident_;
        return true;
    }
    resolving_.get_release();
    return false;
}

// 0x004AD1E0
ElementPtr* UiTree::resolve_element(ElementPtr* out, script::Struct* params, uint32_t name, int assert)
{
    uint32_t id = 0;
    if (resolve_id(params, name, &id))
        return fetch_part(out, id, assert);
    out->preceding_ = out->following_ = nullptr;
    out->owner_object_ = nullptr;
    return out;
}

// --- each frame -----------------------------------------------------------------------------------------------

// 0x004ABD00
void UiTree::tick_frame()
{
    if (!tree_root_)
        return;
    // Not original - the new front end (frontend/new_frontend.h): the original's elements hidden or shown for this
    // frame before they are laid out - a menu restyled by ours is never drawn as the game made it, not even a frame.
    // Original behaviour: the tree laid out as the scripts left it.
    frontend::new_frontend::before_screen_layout();
    lock_tree(1);
    tree_root_->update_layout();
    lock_tree(0);
    if (num_priorities_ > 0)
        update_priorities();
}

// 0x004AAF90 (a stack of 32 elements: the next sibling, then the first child, of each)
void UiTree::lock_tree(int lock)
{
    if (!tree_root_)
        return;
    UiWidget* stack[32];
    stack[0] = tree_root_.owner_object_;
    int count = 1;
    while (count) {
        UiWidget* element = stack[--count];
        if (lock == 1)
            ++element->alias_count_;
        else if (static_cast<int>(element->alias_count_) > 0)
            --element->alias_count_;
        if (element->following_)
            stack[count++] = element->following_.owner_object_;
        if (element->eldest_)
            stack[count++] = element->eldest_.owner_object_;
    }
}

// 0x004AAEE0
void UiTree::set_paused_all(bool pause)
{
    const memory::ListNode* node = entities_.next;
    if (node->data == memory::ListNode::head_marker())
        return;
    while (node) {
        ElementPtr element;
        element.assign_value(static_cast<UiWidget*>(node->data));
        element->set_frozen(pause);
        node = node->next;
        if (node->data == memory::ListNode::head_marker())
            node = nullptr;
    }
}

// --- focus and the pad ----------------------------------------------------------------------------------------

// 0x004AB250 (a new entry goes after the last entry looked at - the second list's tail when it has entries,
// whichever list it is for -, or heads `list` when both are empty)
void UiTree::add_focus_entry(ElementPtr* element, int list)
{
    FocusEntry* last = nullptr;
    for (int i = 0; i < 2; ++i) {
        for (FocusEntry* entry = focus_lists_[i]; entry; entry = entry->next) {
            if (entry->element.owner_object_ == element->owner_object_ && list == i) {
                entry->marked = false;
                return;
            }
            last = entry;
        }
    }
    int free = 0;
    while (focus_entries_[free].element) {
        if (++free == kFocusEntries)
            return;
    }
    FocusEntry* entry = &focus_entries_[free];
    entry->element.assign_value(element->owner_object_);
    entry->id = element->owner_object_->ident_;
    entry->next = nullptr;
    entry->marked = false;
    if (last)
        last->next = entry;
    else
        focus_lists_[list] = entry;
    focus_dirty_[list] = true;
}

// 0x004AB330
void UiTree::remove_focus_entries(ElementPtr* element, bool from_parent, bool mark)
{
    for (int i = 0; i < 2; ++i) {
        FocusEntry* previous = nullptr;
        FocusEntry* entry = focus_lists_[i];
        while (entry) {
            FocusEntry* next = entry->next;
            ElementPtr walk;
            walk.assign_value(entry->element.owner_object_);
            if (from_parent && walk)
                walk.assign_value(walk->container_.owner_object_);
            bool matched = false;
            for (; walk; walk.assign_value(walk->container_.owner_object_)) {
                if (walk.owner_object_ == element->owner_object_) {
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
                        focus_lists_[i] = next;
                    entry->element.get_release();
                    focus_dirty_[i] = true;
                    entry = previous;
                }
            }
            previous = entry;
            entry = next;
        }
    }
}

// 0x004AB4D0
void UiTree::clear_marks(ElementPtr* element)
{
    for (auto entry : focus_lists_) {
        for (; entry; entry = entry->next) {
            if (!entry->marked)
                continue;
            ElementPtr walk;
            walk.assign_value(entry->element.owner_object_);
            for (; walk; walk.assign_value(walk->container_.owner_object_)) {
                if (walk.owner_object_ == element->owner_object_) {
                    entry->marked = false;
                    break;
                }
            }
        }
    }
}

// (The original's listener filter is 0x004ABD40 itself, `this` the listener: the manager is 0x28 before it.)
void UiTree::TreeListener::receive(objects::Signal* event)
{
    auto* manager = raw::owner_of<UiTree>(this, offsetof(UiTree, listener_));
    manager->listener_receive(event);
}

// 0x004ABD40
void UiTree::listener_receive(objects::Signal* event)
{
    UiTree* manager = this;
    if (manager->pad_event_kind_count_ == 0) {
        script::Array* types = script::fetch_list(pad_event_types_name()); // (the original passes 1 as well: unused)
        manager->pad_event_kind_count_ = static_cast<int>(types->extent_);
        for (int i = 0; i < manager->pad_event_kind_count_; ++i)
            manager->pad_event_types_[i] = types->fetch_part(i);
    }
    bool pad_event = false;
    if (manager->num_priorities_ > 0) {
        for (int i = 0; i < manager->pad_event_kind_count_; ++i) {
            if (event->kind_tag_ == manager->pad_event_types_[i]) {
                pad_event = true;
                break;
            }
        }
    }
    if (manager->check_device_ && event->payload_) {
        int device = 0;
        if (event->payload_->fetch_int_value(kDeviceNum, &device, 0)) {
            auto* input = pad_input::g_input_manager; // (the input manager)
            if (input->get_player_controller(0) != device)
                return;
        }
    }
    objects::EventWaitBoard* tracker = objects::get_tracker();
    if (event->kind_tag_ == kFocus && event->aim_point_ != kSystem) {
        ElementPtr element;
        manager->fetch_part(&element, event->aim_point_, 0);
        if (element && !element->input_suppressed()) {
            manager->add_focus_entry(&element, objects::event_controller(event));
            set_event_consumed(event, 1, 0);
        }
    }
    if (event->kind_tag_ == kUnfocus && event->aim_point_ != kSystem) {
        ElementPtr element;
        manager->fetch_part(&element, event->aim_point_, 0);
        if (element && !element->input_suppressed()) {
            manager->remove_focus_entries(&element, false, false);
            set_event_consumed(event, 1, 0);
        }
    }
    if (event->kind_tag_ == kFocusClearedEvent) {
        ElementPtr element;
        manager->fetch_part(&element, event->aim_point_, 0);
        manager->remove_focus_entries(&element, true, true);
        set_event_consumed(event, 1, 0);
    }
    if (event->kind_tag_ == kFocusSentEvent) {
        ElementPtr element;
        manager->fetch_part(&element, event->aim_point_, 0);
        manager->clear_marks(&element);
        set_event_consumed(event, 1, 0);
        element.get_release();
    }
    // a pad event from the system: offered to the controller's focused elements until one handles it (a
    // change of the list starts the round again with the elements not offered it yet)
    bool is_pad_type = false;
    for (int i = 0; i < manager->pad_event_kind_count_; ++i) {
        if (event->kind_tag_ == manager->pad_event_types_[i]) {
            is_pad_type = true;
            break;
        }
    }
    if (!is_pad_type || event->aim_point_ != kSystem)
        return;
    const int controller = objects::event_controller(event);
    for (FocusEntry* entry = manager->focus_lists_[controller]; entry; entry = entry->next)
        entry->tried = false;
    manager->focus_dirty_[controller] = true;
    do {
        manager->focus_dirty_[controller] = false;
        for (FocusEntry* entry = manager->focus_lists_[controller]; entry; entry = entry->next) {
            if (!entry->tried && !entry->marked && !entry->element->input_suppressed()) {
                if (pad_event && (!entry->element->has_priority() || !manager->has_top_precedence(entry->id)))
                    continue;
                if (tracker->launch_event(event->kind_tag_, entry->element->ident_, event->from_where_, event->payload_,
                                          false, -1.0f)) {
                    event->set_event_handled(1, 0);
                    return;
                }
                entry->tried = true;
            }
            if (manager->focus_dirty_[controller])
                break;
        }
    } while (manager->focus_dirty_[controller]);
}

// --- pad-event priorities -------------------------------------------------------------------------------------

// 0x004AAD80
bool UiTree::has_priorities() const { return num_priorities_ > 0; }

// 0x004AAD50
bool UiTree::is_pad_event(uint32_t type) const
{
    for (int i = 0; i < pad_event_kind_count_; ++i)
        if (type == pad_event_types_[i])
            return true;
    return false;
}

// 0x004AB7C0
bool UiTree::has_top_precedence(uint32_t id)
{
    ElementPtr element;
    fetch_part(&element, id, 1);
    auto priority = static_cast<uint32_t>(element->fetch_rank());
    uint32_t top = 0;
    for (const PriorityEntry& entry : priorities_)
        if (entry.active && entry.priority > top)
            top = entry.priority;
    return priority == top;
}

// 0x004AC570
bool UiTree::push_priority(uint32_t id, uint32_t priority)
{
    uint32_t lowest = 0, highest = 0;
    int slot = -1, highest_slot = -1;
    bool free = false;
    for (int i = 0; i < kPriorities; ++i) {
        const PriorityEntry& entry = priorities_[i];
        if (entry.active) {
            if (entry.priority < lowest || lowest == 0) {
                lowest = entry.priority;
                slot = i;
            }
            if (entry.priority > highest) {
                highest = entry.priority;
                highest_slot = i;
            }
        } else if (!free) {
            slot = i;
            free = true;
        }
    }
    if (!free && priority < lowest)
        return false;
    PriorityEntry& entry = priorities_[slot];
    entry.id = id;
    entry.active = true;
    ElementPtr element;
    fetch_part(&element, id, 1);
    entry.element.assign_value(element.owner_object_);
    entry.priority = priority;
    if (num_priorities_ > 0) {
        // the top one shown: the new one hidden unless it is the new top, then the old top is
        Struct* params = script::spawn_param_bag();
        if (params)
            params = new (params) script::Struct;
        params->put_name_key(0u, kHide);
        if (priority > highest)
            priorities_[highest_slot].element.owner_object_->change_traits(params);
        else
            element.owner_object_->change_traits(params);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    ++num_priorities_;
    return true;
}

// 0x004AB5D0 (with no priority above 0 the "top" is the entry before the first)
void UiTree::update_priorities()
{
    uint32_t highest = 0;
    int top = -1;
    if (num_priorities_ <= 0)
        return;
    for (int i = 0; i < kPriorities; ++i) {
        PriorityEntry& entry = priorities_[i];
        if (!entry.active)
            continue;
        const UiWidget* element = nullptr;
        {
            ElementPtr found;
            element = fetch_part(&found, entry.id, 0)->owner_object_;
        }
        if (element) {
            if (entry.priority > highest) {
                highest = entry.priority;
                top = i;
            }
        } else {
            entry.active = false;
            --num_priorities_;
        }
    }
    if (num_priorities_ <= 0)
        return;
    set_flag_property(priorities_[top].element.owner_object_, kUnHide);
}

// --- the singleton --------------------------------------------------------------------------------------------

namespace {
uint32_t g_screen_element_manager_uses;
} // namespace

// 0x004ACAF0
void create_screen_element_manager(bool create)
{
    UiTree*& instance = UiTree::sole();
    if (!instance && create) {
        instance = new UiTree; // (zeroed, from the current heap)
        if (instance) {
            ++g_screen_element_manager_uses;
            return;
        }
    }
    ++g_screen_element_manager_uses;
}

// 0x004AACF0
void delete_screen_element_manager()
{
    uint32_t& uses = g_screen_element_manager_uses;
    if (uses > 1) {
        --uses;
        return;
    }
    delete UiTree::sole();
    UiTree::sole() = nullptr;
    uses = 0;
}

// 0x004AB910
void set_score_text(const char* text, int player)
{
    ElementPtr element;
    UiTree::sole()->fetch_part(&element, kScoreElementHash + player, 0);
    if (element)
        core::down_cast<LabelWidget>(element.owner_object_)->change_caption(text);
}

} // namespace frontend

