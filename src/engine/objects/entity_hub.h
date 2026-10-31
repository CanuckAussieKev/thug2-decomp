#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/objects/smtptr.h"

// The managers that own objects: ObjectHubBase (a list of objects, sorted by their node's priority, registered
// with the object tracker as they come and go) and MiscHub (adds a dead list, emptied by a task every
// frame). From the disassembly of 0x004619A0-0x00461FE0 (methods __thiscall, RET n = 4 * arguments; the
// callbacks __cdecl).
namespace objects {

// A manager's list head (lists::Foremost<Entity>, vtable 0x00647814, data -1).
struct ObjectListHead : memory::ListNode {
    // 0x00461B20: the scalar deleting destructor (the node's destructor body 0x00460B20, then freed).
    ObjectListHead* teardown(unsigned int flags);
};

using ObjectCallback = void(__cdecl*)(Entity* object, void* data);

// The base of the object managers (vtable 0x00647818): a list of objects sorted by their node's priority, registered
// with the tracker as they come and go. Abstract: remove_entity is the subclass's.
class ObjectHubBase : public memory::HeapObject {
public:
    // 0x00461BC0: an empty list, no walks.
    ObjectHubBase();
    // 0x00461B50 (0x00461B80 the deleting one): the list head unlinked; the objects stay as they are.
    virtual ~ObjectHubBase();
    ObjectHubBase(const ObjectHubBase&) = delete;
    ObjectHubBase& operator=(const ObjectHubBase&) = delete;

    // 0x00461A00 (the original's slot 1, the same in every manager): take `object` (sorted in by priority); a walk
    // in progress starts again; no id yet: a unique one from the tracker (0x004621D0); the tracker learns of it
    // (0x00462980).
    void enrol_subject(Entity* object);
    // 0x00461A50 (slot 2): the tracker forgets it (0x00462A20, with the id it is getting if being reassigned);
    // if it is in a list: out of it, no manager, a walk in progress starts again.
    virtual void drop_entity_registration(Entity* object);
    // 0x004619A0 (slot 3): a new id for `object`: unregistered and registered again around the change.
    void reassign_object_id(Entity* object, uint32_t id);
    // 0x004619D0 (slot 4): a new place in the list (its node's priority), by reassigning its own id.
    void set_entity_rank(Entity* object, int priority);
    // (slot 5) `object`, marked dead, handed over to be deleted (pure in this class).
    virtual void remove_entity(Entity* object) = 0;
    // 0x004AAD40 (slot 6: pure in this class, this body in every manager's table): the object list's head.
    ObjectListHead* fetch_object_set() { return &entities_; }

    // 0x004619F0: the object with the id `id` (the tracker's 0x00462120; `this` isn't used).
    Entity* look_up_entity(uint32_t id);
    // 0x00461CC0: `callback(object, data)` once for each object of the list, also when the callback adds or
    // removes objects (the walk starts again from the front, skipping the objects it has done - each walk in
    // progress has a bit of its own in walking_ and in every object's visited_mask_).
    void for_each(ObjectCallback callback, void* data);

    ObjectListHead entities_;    // +0x04
    uint32_t walking_ = 0;       // +0x18 the bits of the walks in progress
    uint32_t restart_walks_ = 0; // +0x1C set to every bit when the list changes: the walks start again
    uint32_t new_id_;            // +0x20 reassign_object_id: the id being given (for the tracker)
    bool reassigning_ = false;   // +0x24
    uint8_t unknown25[3];
};
static_assert(offsetof(ObjectHubBase, entities_) == 0x04 && offsetof(ObjectHubBase, walking_) == 0x18 &&
                  offsetof(ObjectHubBase, restart_walks_) == 0x1C && offsetof(ObjectHubBase, new_id_) == 0x20 &&
                  offsetof(ObjectHubBase, reassigning_) == 0x24 && sizeof(ObjectHubBase) == 0x28,
              "layout must match the original");

// The manager with a dead list (vtable 0x00647834), emptied by a task every frame.
class MiscHub : public ObjectHubBase {
public:
    // 0x00461BF0: empty lists, and a task (priority 1000, on the main loop's system task list) that deletes the
    // dead objects every frame.
    MiscHub();
    // 0x00461DF0 (0x00461EA0 the deleting one): every object unlocked, then the unlocked ones (all) deleted; the
    // task deleted; the lists unlinked.
    ~MiscHub() override;
    // 0x00461B00 (slot 5): `object` goes to the front of the dead list.
    void remove_entity(Entity* object) override;
    // 0x00461AB0: delete the objects of the dead list (front first; each node unlinked first).
    void flush_dead_objects();

    ObjectListHead dead_objects_;      // +0x28
    tasks::Task<MiscHub>* flush_task_; // +0x3C running flush_dead_objects_task
};
static_assert(offsetof(MiscHub, dead_objects_) == 0x28 && offsetof(MiscHub, flush_task_) == 0x3C &&
                  sizeof(MiscHub) == 0x40,
              "layout must match the original");

// 0x00461BB0: the task's code: FlushDeadObjects on its data (the manager).
void __cdecl flush_dead_objects_task(tasks::Task<MiscHub>* task);

// A walk over the objects of one type in a manager's list. (The original's has a vtable, 0x00645DFC, and its base
// lists::Hunt<Entity> another, 0x00645DF8, holding only destructors that reset them - 0x0041AA90 / 0x0041AAA0,
// 0x0041A850 - not kept.)
struct ObjectSearch {
    // 0x00461F30: the first object of `type` after `head`, or null.
    Entity* first_item_of_type(memory::ListNode* head, uint32_t type);
    // 0x00461ED0: the next one, or null.
    Entity* next_item_of_type();

    memory::ListNode* list_link_ = nullptr; // the current node (null at the end)
    uint32_t kind_tag_ = 0;
};

// The object tracker (0x006F0FC0, tracker.h) is a reference-counted singleton (0x006F0FC4).
// (EventWaitBoard::subscribe_receiver 0x00461FE0 is in entity_hub.cpp too.)
// 0x00461FA0: drop a reference; the last one deletes it.
void release_tracker();

extern uint32_t g_tracker_references;

} // namespace objects
