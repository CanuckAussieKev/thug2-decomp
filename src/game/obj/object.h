#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/smtptr.h"

// Obj::CObject, the base of every game object (0x4C bytes; CCompositeObject and its components build on it), and
// the managers that own objects (CBaseManager / CGeneralManager, objman.h).
//
// An object has an id (a name checksum, the name scripts write before ':' - `id:Obj_Func`) and a type, both also
// kept as tags; at most one script of its own (m_script, not spawned: whoever updates the object runs it); tags (a
// structure scripts read and write); flags scripts set and test (Obj_SetFlag...). Scripts find an object by its id
// through the object tracker (0x006F0FC0, wt/trk's code): FindObject.
//
// From the disassembly of 0x004607A0-0x00461900 (methods: __thiscall, RET n = 4 * arguments; FindObject and the
// manager walk's callbacks: __cdecl). The member functions scripts call (Obj_SetFlag...) are entries of the member
// function table at 0x00683550 (0x18 bytes each: the name, the component type 0x57525413 - "the object itself" - and
// a member function pointer, filled in by 0x00642940); 0x0045D850 (the object's vtable +0x08) calls them as
// `bool (CObject::*)(CStruct* params, CScript* script)`.
namespace Script {
class CArray;
class CScript;
class CStruct;
struct CMemberFunctionEntry;
} // namespace Script

namespace Obj {

class CBaseComponent;
class CBaseManager;
class CObject;
struct CEvent;

// A second smart-pointer target in each object (CRefCounted, vtable 0x006477E0, +0x08 the object): the pointers to
// it are cleared as soon as the object is marked dead (MarkAsDead), the object's own (CRefCounted) ones only when
// it is deleted.
class CObjectRef : public CRefCounted {
public:
    // 0x00460920: the destructor body (a jump to ~CRefCounted 0x00463870).
    void DestroyBody();
    // 0x00460AF0: the scalar deleting destructor.
    CObjectRef* Destroy(unsigned int flags);

    CObject* mp_object;
};
static_assert(sizeof(CObjectRef) == 0xC, "layout must match the original");

// An object's node in its manager's list (Lst::Node<CObject>, vtable 0x006477E4; data = the object, priority =
// the list's sort key: the highest first).
struct CObjectNode : Mem::ListNode {
    // 0x00460B20: the destructor body: out of its list (and linked to itself).
    void DestroyBody();
    // 0x00460B40: the scalar deleting destructor.
    CObjectNode* Destroy(unsigned int flags);
};

// m_objectFlags (+0x08)
constexpr uint32_t OBJECT_FLAG_DEAD = 0x1;      // MarkAsDead: waiting in the manager's dead list to be deleted
constexpr uint32_t OBJECT_FLAG_LOCKED = 0x2;    // not deleted by the manager walks that delete unlocked objects
constexpr uint32_t OBJECT_FLAG_INVISIBLE = 0x4; // Obj_Invisible / Obj_Visible
constexpr uint32_t OBJECT_FLAG_HAS_POSITION = 0x20;      // the tracker's distance checks use its position (+0x4C)
constexpr uint32_t OBJECT_FLAG_SPAWN_HANDLERS = 0x10000; // its event handlers run as new spawned scripts on it

// No id yet (0x0064780C: the manager gives it a unique one when registered) / no type (0x00647810).
constexpr uint32_t kUnassignedId = 0xFFFFFFFF;
constexpr uint32_t kNoObjectType = 0;

class CObject : public CRefCounted {
public:
    // 0x00460B80: the constructor (returns this): vtable 0x006477E8, no id, no type, no manager, no script, no tags.
    // m_flags (+0x24) is not initialised.
    CObject* Construct();
    // 0x004615D0: the destructor body: tags freed, unregistered from the manager, its script's on-exit scripts
    // dropped and the script deleted, every script using the object stopped, then the smart pointers to it cleared.
    void DestroyBody();
    // 0x00461900: the scalar deleting destructor (vtable slot 0).
    CObject* Destroy(unsigned int flags);

    // --- the vtable's other slots (0x006477E8) ---
    // 0x00461710 (slot 1, the member function SetProps): event_handlers (with replace_handlers), tags (added) and
    // remove_tags from `params`.
    void SetProperties(Script::CStruct* params);
    // 0x004608F0 (slot 3): a member function by name: SetProps (0x6C63C7C5) runs slot 1, Die (0xC6870028) slot 6.
    // Always true - also for names it doesn't know.
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00461140 (slot 5): `event` for its script (ReceiveEvent; none: nothing). Returns whether the object
    // still exists afterwards (a smart pointer to it is held across the call).
    bool PassEventToScript(CEvent* event);
    // 0x00460A50 (slot 6): blocks its script for good, stops every script using it, unlocks it, unregisters it
    // and gives it to the manager's dead list (slot 5); dead; the pointers to m_ref are cleared.
    void MarkAsDead();
    // 0x0045D850 (slot 2): a member function table entry run on the object: its member function pointer (on `this`
    // moved by the entry's adjustment), or, with none, slot 3 by the entry's name. Returns what that returns.
    bool CallMemberFunction(Script::CMemberFunctionEntry* entry, Script::CStruct* params, Script::CScript* script);
    // (slot 4 0x005BAB90: empty; slot 7 0x005B9760: true)
    // 0x00460930: delete itself unless it is locked (no callers; the walks use the free DestroyIfUnlocked).
    void DestroyIfUnlocked();
    // Slot 0 through the vtable (subclasses' destructors).
    CObject* VirtualDestroy(unsigned int flags)
    {
        return reinterpret_cast<CObject*(__thiscall*)(CObject*, unsigned int)>(m_vtable[0])(this, flags);
    }

    // --- script ---
    // 0x00461290: a script of its own if it has none (on the script heap), running empty_script (0x3F5CDB8A).
    void CreateScriptIfNeeded();
    // 0x00461350: its script (made if needed, from the current heap) runs `name` instead, with `params`.
    // Returns the script.
    Script::CScript* SwitchScript(uint32_t name, Script::CStruct* params);
    // 0x00461500: `name` runs now as an interrupt of its script (made if needed, from the current heap).
    int CallScript(uint32_t name, Script::CStruct* params);
    // 0x00461410: spawn `name` on the object (spawn id `id`). A C function name gives no script: crash.
    Script::CScript* SpawnScript(uint32_t name, Script::CStruct* params, uint32_t id, bool pauseWithObject);
    // 0x00461480: spawn `name` on the object and Update it once at once (`netEnabled`: the other players are
    // told first, with no object).
    Script::CScript* SpawnAndRunScript(uint32_t name, int node, bool netEnabled, bool permanent,
                                       Script::CStruct* params);

    // --- events (the tracker 0x006F0FC0's LaunchEvent 0x00462F00) ---
    // 0x004609F0: an event of `type` from the object to itself.
    void SelfEvent(uint32_t type, Script::CStruct* data);
    // 0x00460A20: an event of `type` from the object to every listener. `radius` isn't used (-1).
    void BroadcastEvent(uint32_t type, Script::CStruct* data, float radius);

    // --- id, type, tags ---
    // 0x00461930: the new id (the manager is told first if it had one), also as the tag id.
    void SetID(uint32_t id);
    // 0x00461970: the type, also as the tag type.
    void SetType(uint32_t type);
    // 0x004611E0: an empty tag structure if it has none (on the front-end heap).
    void CreateTagsIfNeeded();
    // 0x004616B0 / 0x004616D0: a tag as an integer / a checksum. 0x004616F0: add `tags`' components.
    void SetIntegerTag(uint32_t name, int value);
    void SetChecksumTag(uint32_t name, uint32_t value);
    void SetTagsFromStruct(Script::CStruct* tags);
    // 0x00460C20: remove the tags named in `names` (components and flags); then, if none are left - or straight
    // away when `names` is null - the tag structure is freed.
    void RemoveTags(Script::CArray* names);
    // 0x00460CA0: a copy of the tag named by `params`' unnamed checksum, added to `dest`. False if there are no
    // tags or no such tag.
    bool GetTag(Script::CStruct* params, Script::CStruct* dest);
    // 0x00460D90: copies of the tags named in `names` (the last name first) added to `dest`. True.
    bool GetTags(Script::CArray* names, Script::CStruct* dest);
    // 0x004607B0: remove the flag `flag` from the tags (none: nothing).
    void RemoveTagFlag(uint32_t flag);
    // 0x004607C0 / 0x004607E0: the tag `name` as an integer / a checksum (no tags, or none such: false).
    bool GetIntegerTag(uint32_t name, int* value);
    bool GetChecksumTag(uint32_t name, uint32_t* value);
    // 0x00460800: whether the tags hold the flag `flag` (no tags: false).
    bool ContainsTagFlag(uint32_t flag);
    // 0x00460820: the tags added to `dest` (no tags: nothing).
    void CopyTagsTo(Script::CStruct* dest);
    // 0x004607A0: its script's handler for events of `type` goes (no script: nothing).
    void ClearEventHandler(uint32_t type);
    // 0x00460960: the flag bits `params` names: its unnamed array of names (each one's global integer is a bit
    // number), else 1 << its unnamed integer, else 0. (`this` and `script` aren't used.)
    uint32_t GetFlagMask(Script::CStruct* params, Script::CScript* script);

    // --- the member functions scripts call on the object (the table at 0x00683550) ---
    bool Obj_LeaveScript(Script::CStruct* params, Script::CScript* script);       // 0x00460840 (0xB3C262EC)
    bool Obj_KillSpawnedScript(Script::CStruct* params, Script::CScript* script); // 0x00460850
    bool Obj_Visible(Script::CStruct* params, Script::CScript* script);           // 0x004608D0
    bool Obj_Invisible(Script::CStruct* params, Script::CScript* script);         // 0x004608E0
    bool Obj_GetId(Script::CStruct* params, Script::CScript* script);             // 0x00460940
    bool Obj_GetTags(Script::CStruct* params, Script::CScript* script);           // 0x00460EB0 (GetTags)
    bool Obj_GetSingleTag(Script::CStruct* params, Script::CScript* script);      // 0x00460FB0 (GetSingleTag)
    bool Obj_GetNamedTags(Script::CStruct* params, Script::CScript* script);      // 0x00460FC0 (0xEEEB7220)
    bool Obj_RemoveTags(Script::CStruct* params, Script::CScript* script);        // 0x00460FF0 (RemoveTags)
    bool Obj_FlagNotSet(Script::CStruct* params, Script::CScript* script);        // 0x00461040
    bool Obj_ClearFlag(Script::CStruct* params, Script::CScript* script);         // 0x00461070
    bool Obj_SetFlag(Script::CStruct* params, Script::CScript* script);           // 0x004610C0
    bool Obj_FlagSet(Script::CStruct* params, Script::CScript* script);           // 0x004610F0
    bool Obj_SetTags(Script::CStruct* params, Script::CScript* script);           // 0x004617C0 (SetTags)
    bool Obj_SpawnScript(Script::CStruct* params, Script::CScript* script);       // 0x004617E0
    bool Obj_SwitchScript(Script::CStruct* params, Script::CScript* script);      // 0x004618A0

    // --- fields (after CRefCounted's vtable and smart pointer list) ---
    uint32_t m_objectFlags;        // +0x08 OBJECT_FLAG_*
    CBaseManager* mp_manager;      // +0x0C the manager it is registered with
    Script::CScript* m_script;     // +0x10 its own script (made on demand)
    uint32_t m_id;                 // +0x14 kUnassignedId until registered
    uint32_t m_type;               // +0x18
    uint32_t m_visitedMask;        // +0x1C CBaseManager::ForEach's bits (one per walk in progress)
    Script::CStruct* mp_tags;      // +0x20 made on demand (front-end heap)
    uint32_t m_flags;              // +0x24 the script flags (Obj_SetFlag...; never initialised here)
    CObjectNode m_node;            // +0x28 in the manager's list (or its dead list)
    uint32_t m_aliasCount;         // +0x3C the tracker's aliases of it (tracker.cpp)
    CObjectRef m_ref;              // +0x40
};
static_assert(offsetof(CObject, m_objectFlags) == 0x08 && offsetof(CObject, mp_manager) == 0x0C &&
                  offsetof(CObject, m_script) == 0x10 && offsetof(CObject, m_id) == 0x14 &&
                  offsetof(CObject, m_type) == 0x18 && offsetof(CObject, m_visitedMask) == 0x1C &&
                  offsetof(CObject, mp_tags) == 0x20 && offsetof(CObject, m_flags) == 0x24 &&
                  offsetof(CObject, m_node) == 0x28 && offsetof(CObject, m_aliasCount) == 0x3C &&
                  offsetof(CObject, m_ref) == 0x40 && sizeof(CObject) == 0x4C,
              "layout must match the original (CCompositeObject's fields start at 0x4C)");

// The object with the id `id` (the tracker 0x006F0FC0's 0x00462120), or null.
// 0x00460AD0 (cdecl).
CObject* FindObject(uint32_t id);

// The manager walk's callbacks (ForEach, cdecl; `data` unused).
// 0x00460AC0: unlock the object.
void Unlock(CObject* object, void* data);
// 0x00460AA0: delete the object unless it is locked.
void DestroyIfUnlocked(CObject* object, void* data);

class CCompositeObject;

// One of a composite object's components (Obj::CBaseComponent, vtable 0x00647618 in the base; composite.cpp). A
// component belongs to one object (m_object) and is linked into its list (+0x10) and into the composite object
// manager's list of the components of its type (+0x14; 0x006F0FB8, wt/coim's 0x0045F3E0 / 0x0045F420).
//
// The vtable's slots (from their callers): +0x00 the scalar deleting destructor; +0x04 Update; +0x08
// InitFromStructure(params); +0x0C Finalize; +0x10 its object is being marked dead; +0x18 a script waiting on it
// (cscript_wait.cpp); +0x1C Suspend(bool); +0x20 Hide(bool); +0x24 Teleport; +0x2C a member function table entry
// (0x0045D720: by its name, +0x30); +0x30 a member function by name: 0 = false, 1 = true, 2 = not one of its own.
class CBaseComponent {
public:
    // 0x0045D730: the constructor (returns this): the base vtable, no flags.
    CBaseComponent* Construct();
    // 0x0045D740: the destructor body: if a script waited on it, every script lets go of it
    // (ComponentDestroyedAllScripts).
    void DestroyBody();
    // 0x0045D780: the scalar deleting destructor (the base's vtable slot 0).
    CBaseComponent* Destroy(unsigned int flags);
    // 0x0045D760 (vtable +0x1C): suspended or not (a suspended component isn't updated).
    void Suspend(bool suspend);
    // 0x0045D720 (vtable +0x2C): a member function table entry, run by its name (vtable +0x30) - a component
    // ignores the entry's member function pointer.
    int CallMemberFunction(Script::CMemberFunctionEntry* entry, Script::CStruct* params, Script::CScript* script);

    // --- the vtable ---
    CBaseComponent* VirtualDestroy(unsigned int flags)
    {
        return reinterpret_cast<CBaseComponent*(__thiscall*)(CBaseComponent*, unsigned int)>(m_vtable[0])(this, flags);
    }
    void Update() { reinterpret_cast<void(__thiscall*)(CBaseComponent*)>(m_vtable[1])(this); }
    void InitFromStructure(Script::CStruct* params)
    {
        reinterpret_cast<void(__thiscall*)(CBaseComponent*, Script::CStruct*)>(m_vtable[2])(this, params);
    }
    void Finalize() { reinterpret_cast<void(__thiscall*)(CBaseComponent*)>(m_vtable[3])(this); }
    void ObjectMarkedDead() { reinterpret_cast<void(__thiscall*)(CBaseComponent*)>(m_vtable[4])(this); }
    void VirtualSuspend(bool suspend)
    {
        reinterpret_cast<void(__thiscall*)(CBaseComponent*, bool)>(m_vtable[7])(this, suspend);
    }
    void Hide(bool hide) { reinterpret_cast<void(__thiscall*)(CBaseComponent*, bool)>(m_vtable[8])(this, hide); }
    void Teleport() { reinterpret_cast<void(__thiscall*)(CBaseComponent*)>(m_vtable[9])(this); }
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
    {
        return reinterpret_cast<int(__thiscall*)(CBaseComponent*, uint32_t, Script::CStruct*, Script::CScript*)>(
            m_vtable[12])(this, name, params, script);
    }

    void** m_vtable;
    uint32_t m_type;                // +0x04 its component type (a checksum)
    uint32_t m_flags;               // +0x08 COMPONENT_FLAG_*
    CCompositeObject* m_object;     // +0x0C
    CBaseComponent* mp_next;        // +0x10 the next of its object's components
    CBaseComponent* mp_nextOfType;  // +0x14 the next of the manager's components of its type
};
static_assert(offsetof(CBaseComponent, m_type) == 0x04 && offsetof(CBaseComponent, m_flags) == 0x08 &&
                  offsetof(CBaseComponent, m_object) == 0x0C && offsetof(CBaseComponent, mp_next) == 0x10 &&
                  offsetof(CBaseComponent, mp_nextOfType) == 0x14,
              "layout must match the original");

// CBaseComponent::m_flags (+0x08)
constexpr uint32_t COMPONENT_FLAG_SUSPENDED = 0x1;
constexpr uint32_t COMPONENT_FLAG_SCRIPT_WAITING = 0x2; // a script waited on it (SetWaitOnComponent)

// CCompositeObject::m_compositeFlags (+0x104)
constexpr uint32_t COMPOSITE_FLAG_PAUSED = 0x1;      // not updated at all; its PAUSE_WITH_OBJECT scripts wait too
constexpr uint32_t COMPOSITE_FLAG_SUSPENDED = 0x2;   // its script and own update skipped (components still run)
constexpr uint32_t COMPOSITE_FLAG_HIDDEN = 0x4;
constexpr uint32_t COMPOSITE_FLAG_TELEPORTED = 0x8;  // until the end of its next update
constexpr uint32_t COMPOSITE_FLAG_FINALIZED = 0x20;  // Finalize (which also sets 0x40)
constexpr uint32_t COMPOSITE_FLAG_FINALIZED2 = 0x40; // (set with 0x20; meaning unknown)
constexpr uint32_t COMPOSITE_FLAG_DESTROYING = 0x80; // its destructor is running

// An object made of components (Obj::CCompositeObject, vtable 0x00647650; composite.cpp): skaters, peds, cars,
// level objects. It has a position, an orientation, a velocity and a display matrix; each frame its update runs
// its own script, then its own update (vtable +0x28, for subclasses), then its components'.
//
// Its vtable (11 slots): CObject's, with 0 Destroy, 3 CallMemberFunction (components first), 6 MarkAsDead
// (components told first), 7 ShouldUpdateScripts (not paused), and three more: +0x20 Finalize, +0x24
// CreateComponentFromStructure, +0x28 its own update (empty here). Slot 5 (+0x14, PassEventToScript) stays
// CObject's - in its three subclasses' vtables too (0x0064EC44, 0x0064EF48, 0x0064F138).
class CCompositeObject : public CObject {
public:
    // 0x0045E510: the constructor (returns this): at the origin (w = 1), identity matrices, no velocity, no
    // components, no flags; OBJECT_FLAG_HAS_POSITION. (m_oldPos isn't initialised.)
    CCompositeObject* Construct();
    // 0x0045D890: the destructor body: DESTROYING, each component (first to last) taken off the list, out of the
    // manager's and deleted, then CObject's.
    void DestroyBody();
    // 0x0045E620: the scalar deleting destructor (vtable slot 0).
    CCompositeObject* Destroy(unsigned int flags);

    // 0x0045D920: one frame: unless paused, its script's Update and its own update (vtable +0x28) - both skipped
    // while suspended, and nothing more if that killed the object - then the Update of each component that isn't
    // suspended (stopping if the object dies); then m_pos.w = 1 and TELEPORTED cleared.
    void Update();
    // 0x0045D990: hidden or not; when that changes, each component's Hide(hide).
    void Hide(bool hide);
    // 0x0045D9E0: suspended or not; when that changes, each component's Suspend(suspend).
    void Suspend(bool suspend);
    // 0x0045DA30: `component` added at the end of the list, its object set, and registered with the composite
    // object manager.
    void AddComponent(CBaseComponent* component);
    // 0x0045DA90: CreateComponentFromStructure (vtable +0x24) for each structure in `components`, with `extra`.
    void CreateComponentsFromArray(Script::CArray* components, Script::CStruct* extra);
    // 0x0045DAD0: name= (the id), the position and orientation (0x0045C050 / 0x0045C0B0), `permanent` (locked).
    void InitFromStructure(Script::CStruct* params);
    // 0x0045DB30 (vtable +0x20): every component's Finalize, FINALIZED, then every component's Teleport.
    void Finalize();
    // 0x0045DB90: its component of `type`, or null.
    CBaseComponent* GetComponent(uint32_t type);
    // 0x0045E450 (vtable +0x0C): a member function by name: each component's (+0x30) in turn until one says 0
    // (false) or 1 (true), then CObject's.
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0045D7C0 (vtable slot 6): each component's +0x10, then CObject::MarkAsDead.
    void MarkAsDead();
    // 0x0045E4C0: TELEPORTED; with `teleportComponents`, each component's Teleport too.
    void SetTeleported(bool teleportComponents);
    // 0x0045E500 (vtable slot 7): whether its PAUSE_WITH_OBJECT scripts may run (it isn't paused).
    bool ShouldUpdateScripts();
    // 0x0045EE20 (vtable +0x24): a component of the type `params` names (component=), made by the composite object
    // manager, added, and initialised from `params` (with `extra`, from a merged copy).
    void CreateComponentFromStructure(Script::CStruct* params, Script::CStruct* extra);

    // --- the member functions scripts call (the table at 0x00683268, component type 0x0ADA4E74) ---
    // (Names in brackets are the checksums of names not recovered; the C++ names say what they do.)
    bool Obj_GetPosition(Script::CStruct* params, Script::CScript* script);         // 0x0045DBC0
    bool Obj_PrintDetails(Script::CStruct* params, Script::CScript* script);        // 0x0045D7F0
    bool Obj_CreateComponent(Script::CStruct* params, Script::CScript* script);     // 0x0045D810 [0x406998A5]
    bool Obj_ComponentExists(Script::CStruct* params, Script::CScript* script);     // 0x0045DBF0 [0x2BFFEAA3]
    bool Obj_SetPosition(Script::CStruct* params, Script::CScript* script);         // 0x0045D830
    bool Obj_ForceUpdate(Script::CStruct* params, Script::CScript* script);         // 0x0045DC40
    bool Obj_GetVelocity(Script::CStruct* params, Script::CScript* script);         // 0x0045DC50
    bool GetSpeed(Script::CStruct* params, Script::CScript* script);                // 0x0045DC80
    bool Obj_Hide(Script::CStruct* params, Script::CScript* script);                // 0x0045DCD0 (Hide)
    bool Obj_Unhide(Script::CStruct* params, Script::CScript* script);              // 0x0045DD20 (Unhide)
    bool Obj_IsHidden(Script::CStruct* params, Script::CScript* script);            // 0x0045DD70 (IsHidden)
    bool Obj_MoveLocal(Script::CStruct* params, Script::CScript* script);           // 0x0045DD80 [0x10C1C887]
    bool Obj_Suspend(Script::CStruct* params, Script::CScript* script);             // 0x0045DED0 (Suspend)
    bool Obj_Unsuspend(Script::CStruct* params, Script::CScript* script);           // 0x0045DF20 (Unsuspend)
    bool Obj_GetDistanceToObject(Script::CStruct* params, Script::CScript* script); // 0x0045E0B0
    bool Obj_MovingBackward(Script::CStruct* params, Script::CScript* script);      // 0x0045E260 [0xF8CFD515]
    bool Obj_SpeedNear(Script::CStruct* params, Script::CScript* script);           // 0x0045E2A0 [0x7DCC5FB9]
    bool Obj_SpeedAbove(Script::CStruct* params, Script::CScript* script);          // 0x0045E310 [0xE5DF66D7]
    bool Obj_SpeedBelow(Script::CStruct* params, Script::CScript* script);          // 0x0045E380 [0xDD468509]
    bool Obj_GetType(Script::CStruct* params, Script::CScript* script);             // 0x0045E3F0
    bool Obj_HasComponent(Script::CStruct* params, Script::CScript* script);        // 0x0045E410
    bool Obj_Pause(Script::CStruct* params, Script::CScript* script);               // 0x0045E9B0 (Pause)
    bool Obj_Unpause(Script::CStruct* params, Script::CScript* script);             // 0x0045E9D0 (Unpause)

    // --- its own vtable slots ---
    void VirtualOwnUpdate() { reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(m_vtable[10])(this); }
    void VirtualCreateComponentFromStructure(Script::CStruct* params, Script::CStruct* extra)
    {
        reinterpret_cast<void(__thiscall*)(CCompositeObject*, Script::CStruct*, Script::CStruct*)>(m_vtable[9])(
            this, params, extra);
    }

    Mth::Vector m_pos;             // +0x4C (w = 1 after every update)
    Mth::Vector m_oldPos;          // +0x5C (not initialised here; the skater's code sets it)
    Mth::Matrix m_matrix;          // +0x6C its orientation (rows: right, up, at, and a fourth)
    Mth::Vector m_vel;             // +0xAC
    uint8_t m_unknownBC;           // +0xBC (cleared by the constructor)
    uint8_t padBD[3];
    Mth::Matrix m_displayMatrix;   // +0xC0
    CBaseComponent* mp_components; // +0x100
    uint32_t m_compositeFlags;     // +0x104 COMPOSITE_FLAG_*
};
static_assert(offsetof(CCompositeObject, m_pos) == 0x4C && offsetof(CCompositeObject, m_oldPos) == 0x5C &&
                  offsetof(CCompositeObject, m_matrix) == 0x6C && offsetof(CCompositeObject, m_vel) == 0xAC &&
                  offsetof(CCompositeObject, m_unknownBC) == 0xBC &&
                  offsetof(CCompositeObject, m_displayMatrix) == 0xC0 &&
                  offsetof(CCompositeObject, mp_components) == 0x100 &&
                  offsetof(CCompositeObject, m_compositeFlags) == 0x104,
              "layout must match the original");

} // namespace Obj
