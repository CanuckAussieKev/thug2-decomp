#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/smtptr.h"

// Events and the scripts' event handlers (0x0045F7F0-0x00460792).
//
// An event (CEvent) is a type checksum plus who sent it, who it's for and an optional structure of data. The
// tracker (0x006F0FC0, CTracker) hands it to every script listening for its type (Script::CScript::ReceiveEvent),
// and each script's CEventHandlerTable runs the handlers of that type: as an interrupt of the receiving script,
// as a new spawned script on its object, or - an exception handler - by replacing what the script runs.
// The rules (order, removal while running, the in-use / alive protocol) are in docs/script-runtime.md
// section 11.
//
// Every method is __thiscall, callee-pops; PrintTable is __cdecl. No register arguments.

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CObject;

// The checksums of `null_script` (the script of a removed handler entry), `all_groups` and `default`.
constexpr uint32_t kNullScript = 0xC377C572;
constexpr uint32_t kAllGroups = 0x8B713E0E;
constexpr uint32_t kDefaultGroup = 0x1CA1FF20;

// An event (made on the stack by the tracker's launch function 0x00462F00).
struct CEvent {
    enum : uint32_t {
        FLAG_READ = 0x01,     // a script read it
        FLAG_HANDLED = 0x02,  // a handler ran for it
        FLAG_OWNS_DATA = 0x04, // mp_data is deleted with the event
    };

    // 0x0045F7F0: the constructor (flags 0; returns this).
    CEvent* Construct(uint32_t type, uint32_t source, uint32_t target, Script::CStruct* data);
    // 0x0045F980: `script` read it: the tracker's event log is told how, then FLAG_READ.
    void MarkRead(uint32_t how, uint32_t script);
    // 0x0045F9B0: `script` dealt with it: the tracker's event log is told how (0: an event handler ran, 2: a
    // script blocked on it was released), then FLAG_HANDLED.
    void MarkHandled(uint32_t how, uint32_t script);
    // 0x00460150: the destructor: the data goes if the event owns it.
    void Destroy();

    uint32_t m_type;          // +0x00
    uint32_t m_source;        // +0x04 the id of the sender
    uint32_t m_target;        // +0x08 the id of the object (or spawned script) it is for
    uint32_t m_flags;         // +0x0C
    Script::CStruct* mp_data; // +0x10 added to each handler's parameters
};
static_assert(offsetof(CEvent, m_flags) == 0x0C && offsetof(CEvent, mp_data) == 0x10, "layout must match the original");

// 0x0045F9E0: the controller an event came from (its data's `controller`; 0 without one).
int __cdecl EventController(CEvent* event);

// Something the tracker tells about events (an abstract class: vtable 0x006477D8, slot 0 the deleting
// destructor, slot 1 pure - the subclasses' event filter). The tracker keeps them in a singly linked list.
class CEventListener {
public:
    static constexpr uint32_t kVtable = 0x006477D8;

    // 0x0045FAA0
    CEventListener* Construct();
    // 0x0045FAC0: off the tracker's list; the object is let go.
    void Destroy();
    // 0x00460170 (vtable slot 0): Destroy, then freed if `flags` bit 0.
    CEventListener* DeletingDestructor(unsigned int flags);
    // 0x0045FB30: listen on behalf of `object`: point at it, onto the tracker's list.
    void RegisterWithTracker(CObject* object);

    void** m_vtable;
    CSmtPtr<CObject> mp_object;       // +0x04
    CEventListener* mp_nextListener;  // +0x10 the tracker's list (set by the tracker)
    bool m_registered;                // +0x14 on the tracker's list
    uint8_t m_pad15[3];
    int m_busy;                       // +0x18 raised by the tracker while it asks this listener (0x00462F00)
};
static_assert(offsetof(CEventListener, mp_nextListener) == 0x10 && offsetof(CEventListener, m_registered) == 0x14 &&
                  sizeof(CEventListener) == 0x1C,
              "layout must match the original");

// A script's event handlers (Script::CScript::m_eventHandlers, 0x10 bytes, on the front-end heap).
// Entries are never taken out while the table might be running them: removing one sets its script to
// kNullScript (and frees its parameters); Compact drops those later.
class CEventHandlerTable {
public:
    struct Entry {
        enum : uint8_t {
            FLAG_EXCEPTION = 0x01, // replaces the running script instead of interrupting it
            FLAG_MARKED = 0x02,    // marked for removal (MarkGroup / RemoveMarked)
        };
        uint32_t type;            // +0x00 the event type (0 after RemoveType)
        uint32_t script;          // +0x04 the handler script; kNullScript = removed
        uint32_t group;           // +0x08
        Script::CStruct* params;  // +0x0C a copy, owned
        uint8_t flags;            // +0x10 (bits 2-7 are left as they were - uninitialised in new entries)
        uint8_t pad[3];
    };
    static_assert(sizeof(Entry) == 0x14, "layout must match the original");

    // 0x0045F820
    CEventHandlerTable* Construct();
    // 0x0045FB70: the destructor: every entry's parameters, then the array (the fields aren't cleared).
    void Destroy();

    // 0x0045FBC0: a handler for `type`: replaces the entry of that type, else reuses a removed entry, else a new
    // one at the end. The tracker is told `script` listens for `type` unless the entry of that type was live.
    void AddHandler(Script::CScript* script, uint32_t type, uint32_t scriptName, uint32_t group, bool exception,
                    Script::CStruct* params);
    // 0x004601A0: the handler structures of `handlers` ({type, script, [params = {...}], [group = g], [exception]})
    // go in front of the existing entries; with `replace`, existing entries of each type are removed first, and
    // an empty array removes (and drops) them all. The tracker isn't told.
    void AddHandlers(Script::CArray* handlers, bool replace);

    // 0x0045FDF0: remove the entries of `type` (the tracker isn't told).
    void RemoveType(uint32_t type);
    // 0x0045FE60: remove the entries of `group` (kAllGroups: all; the tracker isn't told).
    void RemoveGroup(uint32_t group);
    // 0x004600E0: the same, and the tracker forgets `script` listens for their types.
    void RemoveGroup(Script::CScript* script, uint32_t group);
    // 0x0045F840: mark the live entries of `group` (not kAllGroups) for RemoveMarked.
    void MarkGroup(uint32_t group);
    // 0x0045FED0: remove the marked entries; the tracker forgets `script` listens for their types.
    void RemoveMarked(Script::CScript* script);
    // 0x0045FF50: drop the removed entries (a new array on the front-end heap).
    void Compact();

    // 0x0045FA10 / 0x0045FA60: tell the tracker `script` listens for the live entries' types / no longer
    // listens for any entry's type.
    void RegisterWithTracker(Script::CScript* script);
    void UnregisterWithTracker(Script::CScript* script);

    // 0x00460470: run the handlers of `event` for `script` (the table's owner), in table order.
    void PassEvent(CEvent* event, Script::CScript* script);

    // 0x0045F880 (cdecl): print the table (null: "Emtpy" [sic]); returns Dbg::Printf's result.
    static int PrintTable(CEventHandlerTable* table);

    Entry* m_entries;   // +0x00
    int m_count;        // +0x04
    bool m_alive;       // +0x08 cleared when the script dies while PassEvent runs (PassEvent then frees the table)
    bool m_changed;     // +0x09 entries were added or moved (PassEvent stops: its entry pointer may be stale)
    uint8_t m_pad0A[2];
    int m_inUse;        // +0x0C PassEvent nesting depth
};
static_assert(sizeof(CEventHandlerTable) == 0x10 && offsetof(CEventHandlerTable, m_inUse) == 0x0C,
              "layout must match the original");

} // namespace Obj
