// The object managers (entity_hub.h): ObjectHubBase's sorted list of objects and its re-entrant walk, MiscHub's
// dead list, the search by type, and two pieces of the object tracker.

#include "engine/objects/entity_hub.h"

#include <memory>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/objects/signal.h"
#include "engine/objects/tracker.h"
#include "engine/sys/mlp.h"
#include "engine/assets/resource_registry.h"
#include "engine/objects/entity.h"
#include "engine/core/arena_alloc.h"
#include "engine/objects/objects_shared.h"
namespace objects {

namespace {

constexpr int kFlushTaskPriority = 1000;

// The tracker's reference count.

// lists::Foremost<Entity>'s insert by priority (the highest first, equal ones newest first).
void add_node_by_priority(memory::ListNode* head, memory::ListNode* node)
{
    lists::append_vertex(*head, node);
}
// tasks::Task's constructor and the main loop's system task list's add.
void queue_engine_job(tasks::SchedulerTask* task) { assets::get_main_loop_manager()->system_tasks_.queue_job(task); }

// The node after `node`, or null at the head.
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return next->data == memory::ListNode::head_marker() ? nullptr : next;
}
Entity* object_of(memory::ListNode* node) { return node ? static_cast<Entity*>(node->data) : nullptr; }

} // namespace

// 0x00461BC0
ObjectHubBase::ObjectHubBase() { entities_.make_head(); }

// 0x00461B50
ObjectHubBase::~ObjectHubBase() { entities_.uncouple(); }

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
    drop_entity_registration(object);
    object->ident_ = id;
    enrol_subject(object);
    reassigning_ = false;
}

// 0x004619D0
void ObjectHubBase::set_entity_rank(Entity* object, int priority)
{
    const uint32_t id = object->ident_;
    object->list_link_.priority = priority;
    reassign_object_id(object, id);
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
    for (bool again = true; again; again = restart_walks_ & bit) {
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
    }
    walking_ &= ~bit;
}

// 0x00461BF0
MiscHub::MiscHub()
{
    dead_objects_.make_head();
    flush_task_ = std::make_unique<tasks::Task<MiscHub>>(&flush_dead_objects_task, this, kFlushTaskPriority);
    queue_engine_job(flush_task_.get());
}

// 0x00461DF0
MiscHub::~MiscHub()
{
    for_each(&get_unlock, nullptr);
    for_each(&dispose_when_free, nullptr);
    flush_task_.reset();
    dead_objects_.uncouple();
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
    if (node->data == memory::ListNode::head_marker() || !node)
        return;
    for (bool again = true; again; again = node) {
        memory::ListNode* next = get_next_node(node);
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
        gsl::owner<Entity*> entity = memory::adopt<Entity*>(node->data); // (the list's)
        delete entity;
        node = next;
    }
}

// 0x00461BB0
void flush_dead_objects_task(tasks::Task<MiscHub>* task) { task->content_->flush_dead_objects(); }

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
    uint32_t& references = objects_shared().tracker_references();
    if (references > 1) {
        --references;
        return;
    }
    memory::destroy_then_clear(objects_shared().tracker());
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
