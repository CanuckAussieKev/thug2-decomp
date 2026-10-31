#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/objects/smtptr.h"

// The managers that own objects: CObjectHubBase (a list of objects, sorted by their node's priority, registered
// with the object tracker as they come and go) and CMiscHub (adds a dead list, emptied by a task every
// frame). From the disassembly of 0x004619A0-0x00461FE0 (methods __thiscall, RET n = 4 * arguments; the
// callbacks __cdecl).
namespace GameObjects {

// A manager's list head (ListKit::Foremost<CEntity>, vtable 0x00647814, data -1).
struct CObjectListHead : MemSys::ListNode {
    // 0x00461B20: the scalar deleting destructor (the node's destructor body 0x00460B20, then freed).
    CObjectListHead* Teardown(unsigned int flags);
};

using ObjectCallback = void(__cdecl*)(CEntity* object, void* data);

class CObjectHubBase {
public:
    // 0x00461BC0: the constructor (returns this): vtable 0x00647818, an empty list, no walks.
    CObjectHubBase* MakeNew();
    // 0x00461B50: the destructor body (the list head unlinked; the objects stay as they are).
    void DestroyBody();
    // 0x00461B80: the scalar deleting destructor.
    CObjectHubBase* Teardown(unsigned int flags);

    // --- the vtable (0x00647818; CMiscHub's 0x00647834 overrides 0, 5 and 6) ---
    // 0x00461A00 (slot 1): take `object` (sorted in by priority); a walk in progress starts again; no id yet: a
    // unique one from the tracker (0x004621D0); the tracker learns of it (0x00462980).
    void EnrolSubject(CEntity* object);
    // 0x00461A50 (slot 2): the tracker forgets it (0x00462A20, with the id it is getting if being reassigned);
    // if it is in a list: out of it, no manager, a walk in progress starts again.
    void DropEntityRegistration(CEntity* object);
    // 0x004619A0 (slot 3): a new id for `object`: unregistered and registered again around the change.
    void ReassignObjectId(CEntity* object, uint32_t id);
    // 0x004619D0 (slot 4): a new place in the list (its node's priority), by reassigning its own id.
    void SetEntityRank(CEntity* object, int priority);
    // (slot 5 RemoveEntity: pure in this class; slot 6: pure)

    // 0x004619F0: the object with the id `id` (the tracker's 0x00462120; `this` isn't used).
    CEntity* LookUpEntity(uint32_t id);
    // 0x00461CC0: `callback(object, data)` once for each object of the list, also when the callback adds or
    // removes objects (the walk starts again from the front, skipping the objects it has done - each walk in
    // progress has a bit of its own in m_walking and in every object's m_visitedMask).
    void ForEach(ObjectCallback callback, void* data);

    // Calls through the vtable (subclasses override them).
    void VirtualRegisterObject(CEntity* object);
    void VirtualUnregisterObject(CEntity* object);
    void VirtualReassignObjectId(CEntity* object, uint32_t id);
    void VirtualKillObject(CEntity* object);

    void** m_vtable;
    CObjectListHead m_entities; // +0x04
    uint32_t m_walking;        // +0x18 the bits of the walks in progress
    uint32_t m_restartWalks;   // +0x1C set to every bit when the list changes: the walks start again
    uint32_t m_newId;          // +0x20 ReassignObjectId: the id being given (for the tracker)
    bool m_reassigning;        // +0x24
    uint8_t unknown25[3];
};
static_assert(offsetof(CObjectHubBase, m_entities) == 0x04 && offsetof(CObjectHubBase, m_walking) == 0x18 &&
                  offsetof(CObjectHubBase, m_restartWalks) == 0x1C && offsetof(CObjectHubBase, m_newId) == 0x20 &&
                  offsetof(CObjectHubBase, m_reassigning) == 0x24 && sizeof(CObjectHubBase) == 0x28,
              "layout must match the original");

class CMiscHub : public CObjectHubBase {
public:
    // 0x00461BF0: the constructor (returns this): vtable 0x00647834, empty lists, and a task (priority 1000,
    // on the main loop's system task list) that deletes the dead objects every frame.
    CMiscHub* MakeNew();
    // 0x00461DF0: the destructor body: every object unlocked, then the unlocked ones (all) deleted; the task
    // deleted; the lists unlinked.
    void DestroyBody();
    // 0x00461EA0: the scalar deleting destructor (slot 0).
    CMiscHub* Teardown(unsigned int flags);
    // 0x00461B00 (slot 5): `object` goes to the front of the dead list.
    void RemoveEntity(CEntity* object);
    // 0x00461AB0: delete the objects of the dead list (front first; each node unlinked first).
    void FlushDeadObjects();
    // (slot 6 0x004AAD40: the object list's head)

    CObjectListHead m_deadObjects; // +0x28
    void* m_flushTask;             // +0x3C a Scheduling::Task (vtable 0x006476F0) running FlushDeadObjectsTask
};
static_assert(offsetof(CMiscHub, m_deadObjects) == 0x28 && offsetof(CMiscHub, m_flushTask) == 0x3C &&
                  sizeof(CMiscHub) == 0x40,
              "layout must match the original");

// 0x00461BB0: the task's code: FlushDeadObjects on its data (the manager).
void __cdecl FlushDeadObjectsTask(void* task);

// A walk over the objects of one type in a manager's list (vtable 0x00645DFC, ListKit::Hunt's).
struct CObjectSearch {
    // 0x00461F30: the first object of `type` after `head`, or null.
    CEntity* FirstItemOfType(MemSys::ListNode* head, uint32_t type);
    // 0x00461ED0: the next one, or null.
    CEntity* NextItemOfType();
    // 0x0041AA90: the destructor body (only its base's vtable, ListKit::Hunt<CEntity>'s 0x00645DF8, put back).
    void DestroyBody();
    // 0x0041AAA0: the scalar deleting destructor (vtable slot 0).
    CObjectSearch* Teardown(unsigned int flags);
    // 0x0041A850: its base's scalar deleting destructor (slot 0 of 0x00645DF8; the vtable put back).
    CObjectSearch* DestroyAsBase(unsigned int flags);

    void** m_vtable;
    MemSys::ListNode* m_listLink; // +0x04 the current node (null at the end)
    uint32_t m_kindTag;       // +0x08
};

// The object tracker (0x006F0FC0, tracker.h) is a reference-counted singleton (0x006F0FC4).
// (CEventWaitBoard::SubscribeReceiver 0x00461FE0 is in entity_hub.cpp too.)
// 0x00461FA0: drop a reference; the last one deletes it.
void ReleaseTracker();

extern uint32_t g_trackerReferences;

// 0x00645DFC: objectSearchVtable (defined where the class's functions are).
extern void** const g_objectSearchVtable;

} // namespace Obj
