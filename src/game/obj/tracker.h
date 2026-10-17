#pragma once

#include <cstddef>
#include <cstdint>

#include "game/lst/hashtable.h"
#include "game/obj/event.h"
#include "hook/game.h"

// The object tracker (Obj::CTracker, 0xB21C bytes, the singleton 0x006F0FC0 with its use count 0x006F0FC4):
// - the game objects by id (+0x08) and their aliases (+0x0C) - FindObject / ObjectExists / AssignAlias;
// - for every event type, the scripts listening for it (+0x10): a script is added when it blocks on that type
//   (CScript::Block) or has an event handler for it (the event handler table), and removed again when it stops;
// - the scripts waiting in WaitForEvent (+0xB038, 40 of them);
// - the event listeners (+0x14, C++ objects that see every targeted event first);
// - the event log (+0x20, the last 1024 launches and what became of them; printed by PrintEventLog - in this
//   build only its header and footer, the entries' printing is compiled out).
// LaunchEvent (0x00462F00) delivers an event; how is in docs/script-runtime.md, section 11.
//
// Conventions: every method is __thiscall, callee-pops; CreateTracker and the CFunctions are __cdecl. None takes
// arguments in other registers.

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CObject; // +0x08 flags (0x20: has a position), +0x14 id, +0x3C alias count, +0x4C position

// The "no object" id: the target or source of an event that has none.
constexpr uint32_t kNoObjectId = 0x36B2EE74;

// One script in a list of scripts (Lst::Node<Script::CScript>, 0x14 bytes, vtable 0x006478D8). The list is a
// ring through the head; the head's data is -1.
class CScriptNode {
public:
    // 0x00462530: the destructor body: out of its list (linked to itself again).
    void Unlink();
    // 0x00462550: the scalar deleting destructor (vtable slot 0; `flags` bit 0: free it). Returns this.
    CScriptNode* ScalarDelete(uint8_t flags);

    bool IsHead() const { return mp_data == reinterpret_cast<Script::CScript*>(-1); }

    void** m_vtable;
    Script::CScript* mp_data; // +0x04 (the head: -1)
    int m_priority;           // +0x08 (always 0)
    CScriptNode* mp_next;     // +0x0C
    CScriptNode* mp_prev;     // +0x10
};
static_assert(sizeof(CScriptNode) == 0x14 && offsetof(CScriptNode, mp_next) == 0x0C &&
                  offsetof(CScriptNode, mp_prev) == 0x10,
              "layout must match the original");

// The head of a list of scripts: the scripts listening for one event type (Lst::Head<Script::CScript>, vtable
// 0x006478DC). Made from the pool pair 0x006F0FC8 (CPoolable).
class CScriptList : public CScriptNode {
public:
    // 0x00462620: add `script` at the end (nothing if it's already in), the node on the script heap.
    void Add(Script::CScript* script);
    // 0x004626B0: take `script` out (its node is deleted through its vtable).
    void Remove(Script::CScript* script);
    // 0x00462710: the scalar deleting destructor (vtable slot 0; frees to the heap, not the pool). Returns this.
    CScriptList* ScalarDelete(uint8_t flags);

    bool IsEmpty() const { return mp_prev == this; }

    // CPoolable new / delete: the pool pair 0x006F0FC8 (delete is 0x004624D0, cdecl).
    static void* operator new(size_t size);
    static void operator delete(void* list);
};

// The event log (0xB014 bytes, at tracker +0x20): a ring of the last 1024 entries.
class CEventLog {
public:
    // What an entry records.
    enum EKind : int {
        LAUNCHED = 0, // an event was launched (`script`: whether from a script with a name); the depth goes up
        HANDLED = 1,  // it was handled (`script` the handler's name, `how` how: 2 WaitForEvent, 3 by nobody); down
        READ = 2,     // (0x00462B40)
        FRAME = 3,    // a frame went by (consecutive frames count up in the entry's type field)
        OBJECT_ADDED = 4,
        OBJECT_REMOVED = 5,
    };
    struct Entry {
        uint32_t type;   // the event type (FRAME: the number of frames)
        uint32_t target;
        uint32_t source;
        uint32_t script;
        uint32_t how;
        int kind;        // EKind
        int depth;       // the launch depth after this entry
    };

    // 0x004627C0: add an entry. The first call also makes the table of event names from the global array
    // 0x08114F90 (copied into m_names, keyed by their checksums).
    void AddEntry(uint32_t type, uint32_t target, uint32_t source, uint32_t script, uint32_t how, int kind);
    // 0x004620A0: print it (the entries' printing is compiled out of this build); there are no new entries
    // after. `onlyNew` picks the header; `count` isn't used.
    void Print(bool onlyNew, int count);
    // 0x00462CB0: free the name table.
    void Destroy();

    Entry m_entries[0x400];
    int m_index;               // +0x7000 the next entry
    bool m_wrapped;            // +0x7004 the ring is full
    char m_names[0x100][0x40]; // +0x7005 the event names (no check that there are at most 256)
    uint8_t unknownB005[3];
    Lst::HashTable* mp_names;  // +0xB008 name checksum -> m_names entry (made on the first AddEntry)
    int m_depth;               // +0xB00C how many launches are under way
    int m_newEntries;          // +0xB010 entries since the last Print
};
static_assert(sizeof(CEventLog::Entry) == 0x1C && offsetof(CEventLog, m_index) == 0x7000 &&
                  offsetof(CEventLog, m_names) == 0x7005 && offsetof(CEventLog, mp_names) == 0xB008 &&
                  sizeof(CEventLog) == 0xB014,
              "layout must match the original");

class CTracker {
public:
    // A script waiting in WaitForEvent.
    struct SWaiter {
        uint32_t scriptId; // its m_uniqueId
        uint32_t type;     // 0: a free slot
        uint32_t objectId; // its object's id, or 0
    };

    // 0x00462CF0: the constructor (returns this). 0x00462E40: the destructor. 0x00463780: the scalar deleting
    // destructor (vtable 0x006478E0 slot 0; returns this).
    CTracker* Construct();
    void Destruct();
    CTracker* ScalarDelete(uint8_t flags);

    // 0x00462120: the object with this id or alias, or null. 0x00462190: only by alias.
    CObject* LookUpObject(uint32_t id);
    CObject* GetObjectByAlias(uint32_t alias);
    // 0x004621D0: the checksum of the next "autoid<n>" that no object has (n counts up to 999999, then 0).
    uint32_t GetUniqueId();
    // 0x00462980: track `object` under its id (an alias with that id is dropped).
    void AddObject(CObject* object);
    // 0x00462A20: stop tracking `object`. `renamed`: WaitForEvent waiters on its id follow `newId`; otherwise its
    // aliases go.
    void RemoveObject(CObject* object, uint32_t newId, bool renamed);
    // 0x00462590: every alias of `object` (by id) goes.
    void RemoveAliasesOf(CObject* object);
    // 0x00462A90: `alias` names `object` (replacing what it named).
    void AddAlias(uint32_t alias, CObject* object);

    // 0x00462000: the event listener stops seeing events.
    void RemoveEventListener(CEventListener* listener);
    // 0x00463370 / 0x00463450: `script` listens / stops listening for events of type `type`.
    void AddListener(uint32_t type, Script::CScript* script);
    void RemoveListener(uint32_t type, Script::CScript* script);
    // 0x00462B90: `script` stops listening for any type (when it stops).
    void ForgetScript(Script::CScript* script);
    // 0x00462260: `script` waits (Block(0)) until an event of `type` reaches its object (or any, without one).
    // With all 40 slots taken by live scripts it silently doesn't wait.
    void WaitForEvent(Script::CScript* script, uint32_t type);

    // 0x00462F00: launch an event. Returns whether something marked it handled.
    bool LaunchEvent(uint32_t type, uint32_t target, uint32_t source, Script::CStruct* data, bool broadcast,
                     float radius);

    // The log entries other code makes: 0x00462B10 / 0x00462B40 (`event` handled / read by `script`, `how`),
    // 0x00462B70 a frame went by.
    void LogHandled(CEvent* event, uint32_t how, uint32_t script);
    void LogRead(CEvent* event, uint32_t how, uint32_t script);
    void LogFrame();

    void** m_vtable;
    int m_nextAutoId;                  // +0x04
    Lst::HashTable* mp_objects;        // +0x08 id -> CObject (8 bits)
    Lst::HashTable* mp_aliases;        // +0x0C alias -> CObject (4 bits)
    Lst::HashTable* mp_listeners;      // +0x10 event type -> CScriptList (8 bits)
    CEventListener* mp_eventListeners; // +0x14
    bool unknown18;                    // +0x18 (cleared by the constructor only)
    uint8_t unknown19[3];
    int m_launchDepth;                 // +0x1C
    CEventLog m_log;                   // +0x20
    int m_launchedFromScript;          // +0xB034 for the next launch's log entry (set by the LaunchEvent CFunction)
    SWaiter m_waiters[40];             // +0xB038
    bool unknownB218;                  // +0xB218 (cleared by the constructor only)
    uint8_t unknownB219[3];
};
static_assert(offsetof(CTracker, mp_eventListeners) == 0x14 && offsetof(CTracker, m_launchDepth) == 0x1C &&
                  offsetof(CTracker, m_log) == 0x20 && offsetof(CTracker, m_launchedFromScript) == 0xB034 &&
                  offsetof(CTracker, m_waiters) == 0xB038 && offsetof(CTracker, unknownB218) == 0xB218 &&
                  sizeof(CTracker) == 0xB21C,
              "layout must match the original");

// The tracker (null until made).
inline CTracker*& Tracker() { return GameVariable<CTracker*>(0x006F0FC0); }
// 0x00463730: make the tracker if there's none and `create`; count a use either way. Returns it.
CTracker* CreateTracker(bool create);

// The script commands (__cdecl).
bool __cdecl CFunc_WaitForEvent(Script::CStruct* params, Script::CScript* script);           // 0x00462310
bool __cdecl CFunc_ObjectExists(Script::CStruct* params, Script::CScript* script);           // 0x00462340
bool __cdecl CFunc_TerminateObjectsScripts(Script::CStruct* params, Script::CScript* script); // 0x00462380
bool __cdecl CFunc_SetObjectProps(Script::CStruct* params, Script::CScript* script);         // 0x00462440
bool __cdecl CFunc_PrintEventLog(Script::CStruct* params, Script::CScript* script);          // 0x00462480
bool __cdecl CFunc_AssignAlias(Script::CStruct* params, Script::CScript* script);            // 0x00462C40
bool __cdecl CFunc_LaunchEvent(Script::CStruct* params, Script::CScript* script);            // 0x004634D0

} // namespace Obj
