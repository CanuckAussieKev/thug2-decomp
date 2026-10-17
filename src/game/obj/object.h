#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
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

// No id yet (0x0064780C: the manager gives it a unique one when registered) / no type (0x00647810).
constexpr uint32_t kNoObjectId = 0xFFFFFFFF;
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
    // (slot 2 0x0045D850: a member function table entry; slot 4 0x005BAB90: empty; slot 7 0x005B9760: true)
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
    uint32_t m_id;                 // +0x14 kNoObjectId until registered
    uint32_t m_type;               // +0x18
    uint32_t m_visitedMask;        // +0x1C CBaseManager::ForEach's bits (one per walk in progress)
    Script::CStruct* mp_tags;      // +0x20 made on demand (front-end heap)
    uint32_t m_flags;              // +0x24 the script flags (Obj_SetFlag...; never initialised here)
    CObjectNode m_node;            // +0x28 in the manager's list (or its dead list)
    uint32_t unknown3C;            // +0x3C (zeroed)
    CObjectRef m_ref;              // +0x40
};
static_assert(offsetof(CObject, m_objectFlags) == 0x08 && offsetof(CObject, mp_manager) == 0x0C &&
                  offsetof(CObject, m_script) == 0x10 && offsetof(CObject, m_id) == 0x14 &&
                  offsetof(CObject, m_type) == 0x18 && offsetof(CObject, m_visitedMask) == 0x1C &&
                  offsetof(CObject, mp_tags) == 0x20 && offsetof(CObject, m_flags) == 0x24 &&
                  offsetof(CObject, m_node) == 0x28 && offsetof(CObject, unknown3C) == 0x3C &&
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

// An object that holds components (CCompositeObject: CObject + 0xC4 bytes; its components' list at +0x100).
class CCompositeObject : public CObject {
public:
    // 0x0045DB90: its component of `type` (its list: the component's +0x04 type, +0x10 next), or null.
    CBaseComponent* GetComponent(uint32_t type);

    uint8_t unknown4C[0x100 - 0x4C];
    CBaseComponent* mp_components; // +0x100
};
static_assert(offsetof(CCompositeObject, mp_components) == 0x100, "layout must match the original");

} // namespace Obj
