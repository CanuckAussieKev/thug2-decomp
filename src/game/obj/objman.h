#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/obj/event.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/obj/smtptr.h"
#include "hook/game_global.h"

// The managers that own objects: CBaseManager (a list of objects, sorted by their node's priority, registered
// with the object tracker as they come and go) and CGeneralManager (adds a dead list, emptied by a task every
// frame). From the disassembly of 0x004619A0-0x00461FE0 (methods __thiscall, RET n = 4 * arguments; the
// callbacks __cdecl).
namespace Obj {

// A manager's list head (Lst::Head<CObject>, vtable 0x00647814, data -1).
struct CObjectListHead : Mem::ListNode {
    // 0x00461B20: the scalar deleting destructor (the node's destructor body 0x00460B20, then freed).
    CObjectListHead* Destroy(unsigned int flags);
};

using ObjectCallback = void(__cdecl*)(CObject* object, void* data);

class CBaseManager {
public:
    // 0x00461BC0: the constructor (returns this): vtable 0x00647818, an empty list, no walks.
    CBaseManager* Construct();
    // 0x00461B50: the destructor body (the list head unlinked; the objects stay as they are).
    void DestroyBody();
    // 0x00461B80: the scalar deleting destructor.
    CBaseManager* Destroy(unsigned int flags);

    // --- the vtable (0x00647818; CGeneralManager's 0x00647834 overrides 0, 5 and 6) ---
    // 0x00461A00 (slot 1): take `object` (sorted in by priority); a walk in progress starts again; no id yet: a
    // unique one from the tracker (0x004621D0); the tracker learns of it (0x00462980).
    void RegisterObject(CObject* object);
    // 0x00461A50 (slot 2): the tracker forgets it (0x00462A20, with the id it is getting if being reassigned);
    // if it is in a list: out of it, no manager, a walk in progress starts again.
    void UnregisterObject(CObject* object);
    // 0x004619A0 (slot 3): a new id for `object`: unregistered and registered again around the change.
    void ReassignObjectId(CObject* object, uint32_t id);
    // 0x004619D0 (slot 4): a new place in the list (its node's priority), by reassigning its own id.
    void SetObjectPriority(CObject* object, int priority);
    // (slot 5 KillObject: pure in this class; slot 6: pure)

    // 0x004619F0: the object with the id `id` (the tracker's 0x00462120; `this` isn't used).
    CObject* GetObjectByID(uint32_t id);
    // 0x00461CC0: `callback(object, data)` once for each object of the list, also when the callback adds or
    // removes objects (the walk starts again from the front, skipping the objects it has done - each walk in
    // progress has a bit of its own in m_walking and in every object's m_visitedMask).
    void ForEach(ObjectCallback callback, void* data);

    // Calls through the vtable (subclasses override them).
    void VirtualRegisterObject(CObject* object);
    void VirtualUnregisterObject(CObject* object);
    void VirtualReassignObjectId(CObject* object, uint32_t id);
    void VirtualKillObject(CObject* object);

    void** m_vtable;
    CObjectListHead m_objects; // +0x04
    uint32_t m_walking;        // +0x18 the bits of the walks in progress
    uint32_t m_restartWalks;   // +0x1C set to every bit when the list changes: the walks start again
    uint32_t m_newId;          // +0x20 ReassignObjectId: the id being given (for the tracker)
    bool m_reassigning;        // +0x24
    uint8_t unknown25[3];
};
static_assert(offsetof(CBaseManager, m_objects) == 0x04 && offsetof(CBaseManager, m_walking) == 0x18 &&
                  offsetof(CBaseManager, m_restartWalks) == 0x1C && offsetof(CBaseManager, m_newId) == 0x20 &&
                  offsetof(CBaseManager, m_reassigning) == 0x24 && sizeof(CBaseManager) == 0x28,
              "layout must match the original");

class CGeneralManager : public CBaseManager {
public:
    // 0x00461BF0: the constructor (returns this): vtable 0x00647834, empty lists, and a task (priority 1000,
    // on the main loop's system task list) that deletes the dead objects every frame.
    CGeneralManager* Construct();
    // 0x00461DF0: the destructor body: every object unlocked, then the unlocked ones (all) deleted; the task
    // deleted; the lists unlinked.
    void DestroyBody();
    // 0x00461EA0: the scalar deleting destructor (slot 0).
    CGeneralManager* Destroy(unsigned int flags);
    // 0x00461B00 (slot 5): `object` goes to the front of the dead list.
    void KillObject(CObject* object);
    // 0x00461AB0: delete the objects of the dead list (front first; each node unlinked first).
    void FlushDeadObjects();
    // (slot 6 0x004AAD40: the object list's head)

    CObjectListHead m_deadObjects; // +0x28
    void* m_flushTask;             // +0x3C a Tsk::Task (vtable 0x006476F0) running FlushDeadObjectsTask
};
static_assert(offsetof(CGeneralManager, m_deadObjects) == 0x28 && offsetof(CGeneralManager, m_flushTask) == 0x3C &&
                  sizeof(CGeneralManager) == 0x40,
              "layout must match the original");

// 0x00461BB0: the task's code: FlushDeadObjects on its data (the manager).
void __cdecl FlushDeadObjectsTask(void* task);

// A walk over the objects of one type in a manager's list (vtable 0x00645DFC, Lst::Search's).
struct CObjectSearch {
    // 0x00461F30: the first object of `type` after `head`, or null.
    CObject* FirstItemOfType(Mem::ListNode* head, uint32_t type);
    // 0x00461ED0: the next one, or null.
    CObject* NextItemOfType();
    // 0x0041AA90: the destructor body (only its base's vtable, Lst::Search<CObject>'s 0x00645DF8, put back).
    void DestroyBody();
    // 0x0041AAA0: the scalar deleting destructor (vtable slot 0).
    CObjectSearch* Destroy(unsigned int flags);
    // 0x0041A850: its base's scalar deleting destructor (slot 0 of 0x00645DF8; the vtable put back).
    CObjectSearch* DestroyAsBase(unsigned int flags);

    void** m_vtable;
    Mem::ListNode* m_node; // +0x04 the current node (null at the end)
    uint32_t m_type;       // +0x08
};

// The object tracker (0x006F0FC0, tracker.h) is a reference-counted singleton (0x006F0FC4).
// (CTracker::RegisterEventListener 0x00461FE0 is in objman.cpp too.)
// 0x00461FA0: drop a reference; the last one deletes it.
void ReleaseTracker();

GAME_GLOBAL_EXTERN(uint32_t, g_trackerReferences);

} // namespace Obj
