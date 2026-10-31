// The object managers (entity_hub.h): ObjectHubBase's sorted list of objects and its re-entrant walk, MiscHub's
// dead list, the search by type, and two pieces of the object tracker.

#include "engine/objects/entity_hub.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/sys/mlp.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/code_address.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/entity.h"

namespace objects {

uint32_t g_tracker_references;

namespace {

extern void** const kManagerVtable;
extern void** const kGeneralManagerVtable;
extern void** const kListHeadVtable;
constexpr int kFlushTaskPriority = 1000;

// The tracker's reference count.
uint32_t& tracker_references() { return g_tracker_references; }

// lists::Foremost<Entity>'s insert by priority (the highest first, equal ones newest first).
void add_node_by_priority(memory::ListNode* head, memory::ListNode* node)
{
    reinterpret_cast<lists::Foremost*>(head)->append_vertex(node);
}
// tasks::Task's constructor and the main loop's system task list's add.
void queue_engine_job(tasks::SchedulerTask* task) { assets::g_main_loop_manager->system_tasks_.queue_job(task); }

// The node after `node`, or null at the head.
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
Entity* object_of(memory::ListNode* node) { return node ? static_cast<Entity*>(node->data) : nullptr; }

void setup_list_sentinel(memory::ListNode* head)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kListHeadVtable;
}

} // namespace

// 0x00461B20
ObjectListHead* ObjectListHead::teardown(unsigned int flags)
{
    static_cast<EntityListEntry*>(static_cast<memory::ListNode*>(this))->destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00461BC0
ObjectHubBase* ObjectHubBase::make_new()
{
    vtable_ = kManagerVtable;
    setup_list_sentinel(&entities_);
    walking_ = 0;
    restart_walks_ = 0;
    reassigning_ = false;
    return this;
}

// 0x00461B50
void ObjectHubBase::destroy_body()
{
    vtable_ = kManagerVtable;
    entities_.unlink_as(kObjectNodeVtable);
}

// 0x00461B80
ObjectHubBase* ObjectHubBase::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

void ObjectHubBase::virtual_register_object(Entity* object)
{
    reinterpret_cast<void(__thiscall*)(ObjectHubBase*, Entity*)>(vtable_[1])(this, object);
}
void ObjectHubBase::virtual_unregister_object(Entity* object)
{
    reinterpret_cast<void(__thiscall*)(ObjectHubBase*, Entity*)>(vtable_[2])(this, object);
}
void ObjectHubBase::virtual_reassign_object_id(Entity* object, uint32_t id)
{
    reinterpret_cast<void(__thiscall*)(ObjectHubBase*, Entity*, uint32_t)>(vtable_[3])(this, object, id);
}
void ObjectHubBase::virtual_kill_object(Entity* object)
{
    reinterpret_cast<void(__thiscall*)(ObjectHubBase*, Entity*)>(vtable_[5])(this, object);
}

// 0x00461A00
void ObjectHubBase::enrol_subject(Entity* object)
{
    EventWaitBoard* tracker = get_tracker();
    object->park_owner_ = this;
    add_node_by_priority(&entities_, &object->list_link_);
    restart_walks_ = 0xFFFFFFFF;
    if (object->ident_ == kUnassignedId)
        object->change_serial(tracker->fetch_distinct_id());
    tracker->add_object(object);
}

// 0x00461A50
void ObjectHubBase::drop_entity_registration(Entity* object)
{
    get_tracker()->remove_object(object, new_id_, reassigning_);
    memory::ListNode* node = &object->list_link_;
    if (node->prev != node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
        object->park_owner_ = nullptr;
        restart_walks_ = 0xFFFFFFFF;
    }
}

// 0x004619A0
void ObjectHubBase::reassign_object_id(Entity* object, uint32_t id)
{
    new_id_ = id;
    reassigning_ = true;
    virtual_unregister_object(object);
    object->ident_ = id;
    virtual_register_object(object);
    reassigning_ = false;
}

// 0x004619D0
void ObjectHubBase::set_entity_rank(Entity* object, int priority)
{
    uint32_t id = object->ident_;
    object->list_link_.priority = priority;
    virtual_reassign_object_id(object, id);
}

// 0x004619F0
Entity* ObjectHubBase::look_up_entity(uint32_t id) { return get_tracker()->look_up_object(id); }

// 0x00461CC0. The next object is looked at before the callback runs; a callback that changes the list (register
// or unregister) makes the walk start again from the front, past the objects already done.
void ObjectHubBase::for_each(ObjectCallback callback, void* data)
{
    uint32_t bit = 1;
    while (walking_ & bit)
        bit <<= 1;
    walking_ |= bit;
    for (memory::ListNode* node = get_next_node(&entities_); node && node->data; node = get_next_node(node))
        static_cast<Entity*>(node->data)->visited_mask_ &= ~bit;
    do {
        restart_walks_ &= ~bit;
        memory::ListNode* node = get_next_node(&entities_);
        Entity* object = object_of(node);
        while (object) {
            memory::ListNode* next_node = get_next_node(node);
            Entity* next = object_of(next_node);
            if (!(object->visited_mask_ & bit)) {
                object->visited_mask_ |= bit;
                callback(object, data);
                if (restart_walks_ & bit)
                    break;
            }
            node = next_node;
            object = next;
        }
    } while (restart_walks_ & bit);
    walking_ &= ~bit;
}

// 0x00461BF0
MiscHub* MiscHub::make_new()
{
    ObjectHubBase::make_new();
    vtable_ = kGeneralManagerVtable;
    setup_list_sentinel(&dead_objects_);
    flush_task_ = new tasks::Task<MiscHub>(&flush_dead_objects_task, this, kFlushTaskPriority);
    queue_engine_job(flush_task_);
    return this;
}

// 0x00461DF0
void MiscHub::destroy_body()
{
    vtable_ = kGeneralManagerVtable;
    for_each(&get_unlock, nullptr);
    for_each(&dispose_when_free, nullptr);
    delete flush_task_;
    dead_objects_.unlink_as(kObjectNodeVtable);
    vtable_ = kManagerVtable;
    entities_.unlink_as(kObjectNodeVtable);
}

// 0x00461EA0
MiscHub* MiscHub::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00461B00
void MiscHub::remove_entity(Entity* object)
{
    memory::ListNode* node = &object->list_link_;
    memory::ListNode* head = &dead_objects_;
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}

// 0x00461AB0
void MiscHub::flush_dead_objects()
{
    memory::ListNode* node = dead_objects_.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    do {
        memory::ListNode* next = get_next_node(node);
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
        if (auto* object = static_cast<Entity*>(node->data))
            object->virtual_destroy(1);
        node = next;
    } while (node);
}

// 0x00461BB0
void __cdecl flush_dead_objects_task(tasks::Task<MiscHub>* task) { task->content_->flush_dead_objects(); }

// 0x00461F30
Entity* ObjectSearch::first_item_of_type(memory::ListNode* head, uint32_t type)
{
    list_link_ = head;
    list_link_ = get_next_node(head);
    Entity* object = object_of(list_link_);
    kind_tag_ = type;
    if (!object)
        return nullptr;
    while (object->kind_tag_ != type) {
        list_link_ = get_next_node(list_link_);
        if (!list_link_)
            return nullptr;
        object = object_of(list_link_);
        if (!object)
            return nullptr;
    }
    return object;
}

// 0x00461ED0
Entity* ObjectSearch::next_item_of_type()
{
    list_link_ = get_next_node(list_link_);
    if (!list_link_)
        return nullptr;
    Entity* object = object_of(list_link_);
    if (!object)
        return nullptr;
    while (object->kind_tag_ != kind_tag_) {
        list_link_ = get_next_node(list_link_);
        if (!list_link_)
            return nullptr;
        object = object_of(list_link_);
        if (!object)
            return nullptr;
    }
    return object;
}

// 0x00461FA0
void release_tracker()
{
    uint32_t& references = tracker_references();
    if (references > 1) {
        --references;
        return;
    }
    delete get_tracker();
    get_tracker() = nullptr;
    references = 0;
}

// 0x00461FE0
void EventWaitBoard::subscribe_receiver(SignalReceiver* listener)
{
    listener->next_subscriber_ = event_listeners_;
    event_listeners_ = listener;
    listener->on_board_ = true;
}

} // namespace objects

namespace objects {
namespace {

static VtableSlot g_list_head_vtable_slots[] = {&objects::ObjectListHead::teardown};
void** const kListHeadVtable = reinterpret_cast<void**>(g_list_head_vtable_slots);
static VtableSlot g_manager_vtable_slots[] = {&objects::ObjectHubBase::teardown,
                                              &objects::ObjectHubBase::enrol_subject,
                                              &objects::ObjectHubBase::drop_entity_registration,
                                              &objects::ObjectHubBase::reassign_object_id,
                                              &objects::ObjectHubBase::set_entity_rank,
                                              &_purecall,
                                              &_purecall};
void** const kManagerVtable = reinterpret_cast<void**>(g_manager_vtable_slots);
static VtableSlot g_general_manager_vtable_slots[] = {&objects::MiscHub::teardown,
                                                      &objects::ObjectHubBase::enrol_subject,
                                                      &objects::ObjectHubBase::drop_entity_registration,
                                                      &objects::ObjectHubBase::reassign_object_id,
                                                      &objects::ObjectHubBase::set_entity_rank,
                                                      &objects::MiscHub::remove_entity,
                                                      &frontend::UiTree::fetch_object_set};
void** const kGeneralManagerVtable = reinterpret_cast<void**>(g_general_manager_vtable_slots);

} // namespace
} // namespace objects
