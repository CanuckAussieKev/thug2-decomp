#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
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

class ObjectHubBase {
public:
    // 0x00461BC0: the constructor (returns this): vtable 0x00647818, an empty list, no walks.
    ObjectHubBase* make_new();
    // 0x00461B50: the destructor body (the list head unlinked; the objects stay as they are).
    void destroy_body();
    // 0x00461B80: the scalar deleting destructor.
    ObjectHubBase* teardown(unsigned int flags);

    // --- the vtable (0x00647818; MiscHub's 0x00647834 overrides 0, 5 and 6) ---
    // 0x00461A00 (slot 1): take `object` (sorted in by priority); a walk in progress starts again; no id yet: a
    // unique one from the tracker (0x004621D0); the tracker learns of it (0x00462980).
    void enrol_subject(Entity* object);
    // 0x00461A50 (slot 2): the tracker forgets it (0x00462A20, with the id it is getting if being reassigned);
    // if it is in a list: out of it, no manager, a walk in progress starts again.
    void drop_entity_registration(Entity* object);
    // 0x004619A0 (slot 3): a new id for `object`: unregistered and registered again around the change.
    void reassign_object_id(Entity* object, uint32_t id);
    // 0x004619D0 (slot 4): a new place in the list (its node's priority), by reassigning its own id.
    void set_entity_rank(Entity* object, int priority);
    // (slot 5 remove_entity: pure in this class; slot 6: pure)

    // 0x004619F0: the object with the id `id` (the tracker's 0x00462120; `this` isn't used).
    Entity* look_up_entity(uint32_t id);
    // 0x00461CC0: `callback(object, data)` once for each object of the list, also when the callback adds or
    // removes objects (the walk starts again from the front, skipping the objects it has done - each walk in
    // progress has a bit of its own in walking_ and in every object's visited_mask_).
    void for_each(ObjectCallback callback, void* data);

    // Calls through the vtable (subclasses override them).
    void virtual_register_object(Entity* object);
    void virtual_unregister_object(Entity* object);
    void virtual_reassign_object_id(Entity* object, uint32_t id);
    void virtual_kill_object(Entity* object);

    void** vtable_;
    ObjectListHead entities_; // +0x04
    uint32_t walking_;        // +0x18 the bits of the walks in progress
    uint32_t restart_walks_;   // +0x1C set to every bit when the list changes: the walks start again
    uint32_t new_id_;          // +0x20 reassign_object_id: the id being given (for the tracker)
    bool reassigning_;        // +0x24
    uint8_t unknown25[3];
};
static_assert(offsetof(ObjectHubBase, entities_) == 0x04 && offsetof(ObjectHubBase, walking_) == 0x18 &&
                  offsetof(ObjectHubBase, restart_walks_) == 0x1C && offsetof(ObjectHubBase, new_id_) == 0x20 &&
                  offsetof(ObjectHubBase, reassigning_) == 0x24 && sizeof(ObjectHubBase) == 0x28,
              "layout must match the original");

class MiscHub : public ObjectHubBase {
public:
    // 0x00461BF0: the constructor (returns this): vtable 0x00647834, empty lists, and a task (priority 1000,
    // on the main loop's system task list) that deletes the dead objects every frame.
    MiscHub* make_new();
    // 0x00461DF0: the destructor body: every object unlocked, then the unlocked ones (all) deleted; the task
    // deleted; the lists unlinked.
    void destroy_body();
    // 0x00461EA0: the scalar deleting destructor (slot 0).
    MiscHub* teardown(unsigned int flags);
    // 0x00461B00 (slot 5): `object` goes to the front of the dead list.
    void remove_entity(Entity* object);
    // 0x00461AB0: delete the objects of the dead list (front first; each node unlinked first).
    void flush_dead_objects();
    // (slot 6 0x004AAD40: the object list's head)

    ObjectListHead dead_objects_; // +0x28
    void* flush_task_;             // +0x3C a tasks::Task (vtable 0x006476F0) running flush_dead_objects_task
};
static_assert(offsetof(MiscHub, dead_objects_) == 0x28 && offsetof(MiscHub, flush_task_) == 0x3C &&
                  sizeof(MiscHub) == 0x40,
              "layout must match the original");

// 0x00461BB0: the task's code: FlushDeadObjects on its data (the manager).
void __cdecl flush_dead_objects_task(void* task);

// A walk over the objects of one type in a manager's list (vtable 0x00645DFC, lists::Hunt's).
struct ObjectSearch {
    // 0x00461F30: the first object of `type` after `head`, or null.
    Entity* first_item_of_type(memory::ListNode* head, uint32_t type);
    // 0x00461ED0: the next one, or null.
    Entity* next_item_of_type();
    // 0x0041AA90: the destructor body (only its base's vtable, lists::Hunt<Entity>'s 0x00645DF8, put back).
    void destroy_body();
    // 0x0041AAA0: the scalar deleting destructor (vtable slot 0).
    ObjectSearch* teardown(unsigned int flags);
    // 0x0041A850: its base's scalar deleting destructor (slot 0 of 0x00645DF8; the vtable put back).
    ObjectSearch* destroy_as_base(unsigned int flags);

    void** vtable_;
    memory::ListNode* list_link_; // +0x04 the current node (null at the end)
    uint32_t kind_tag_;       // +0x08
};

// The object tracker (0x006F0FC0, tracker.h) is a reference-counted singleton (0x006F0FC4).
// (EventWaitBoard::subscribe_receiver 0x00461FE0 is in entity_hub.cpp too.)
// 0x00461FA0: drop a reference; the last one deletes it.
void release_tracker();

extern uint32_t g_tracker_references;

// 0x00645DFC: objectSearchVtable (defined where the class's functions are).
extern void** const kObjectSearchVtable;

} // namespace Obj
