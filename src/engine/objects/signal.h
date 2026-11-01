#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/smtptr.h"

// Events and the scripts' event handlers (0x0045F7F0-0x00460792).
//
// An event (Signal) is a type checksum plus who sent it, who it's for and an optional structure of data. The
// tracker (0x006F0FC0, EventWaitBoard) hands it to every script listening for its type (Scripts::CScript::receive_event),
// and each script's SignalRouteTable runs the handlers of that type: as an interrupt of the receiving script,
// as a new spawned script on its object, or - an exception handler - by replacing what the script runs.
// The rules (order, removal while running, the in-use / alive protocol) are in docs/script-runtime.md
// section 11.
//
// Every method is __thiscall, callee-pops; list_wait_table is __cdecl. No register arguments.

namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace objects {

class Entity;

// The checksums of `null_script` (the script of a removed handler entry), `all_groups` and `default`.
constexpr uint32_t kNullScript = 0xC377C572;
constexpr uint32_t kAllGroups = 0x8B713E0E;
constexpr uint32_t kDefaultGroup = 0x1CA1FF20;

// An event (made on the stack by the tracker's launch function 0x00462F00).
struct Signal {
    enum : uint32_t {
        kFlagRead = 0x01,     // a script read it
        kFlagHandled = 0x02,  // a handler ran for it
        kFlagOwnsData = 0x04, // payload_ is deleted with the event
    };

    // 0x0045F7F0: the constructor (flags 0;).
    Signal(uint32_t type, uint32_t source, uint32_t target, script::Struct* data);
    // 0x0045F980: `script` read it: the tracker's event log is told how, then kFlagRead.
    void set_event_consumed(uint32_t how, uint32_t script);
    // 0x0045F9B0: `script` dealt with it: the tracker's event log is told how (0: an event handler ran, 2: a
    // script blocked on it was released), then kFlagHandled.
    void set_event_handled(uint32_t how, uint32_t script);
    // 0x00460150: the destructor: the data goes if the event owns it.
    void teardown();

    uint32_t kind_tag_;       // +0x00
    uint32_t from_where_;     // +0x04 the id of the sender
    uint32_t aim_point_;      // +0x08 the id of the object (or spawned script) it is for
    uint32_t flag_bits_;      // +0x0C
    script::Struct* payload_; // +0x10 added to each handler's parameters
};

// 0x0045F9E0: the controller an event came from (its data's `controller`; 0 without one).
int event_controller(Signal* event);

// Something the tracker tells about events (an abstract class: the original's vtable 0x006477D8 - its destructor and
// the subclasses' event filter). The tracker keeps them in a singly linked list.
class SignalReceiver {
public:
    // 0x0045FAA0
    SignalReceiver() = default;
    // 0x0045FAC0 (0x00460170 the deleting one): off the tracker's list; the object is let go.
    virtual ~SignalReceiver();
    SignalReceiver(const SignalReceiver&) = delete;
    SignalReceiver& operator=(const SignalReceiver&) = delete;

    // An event the tracker hands on (0x004A1090 calls it): the subclass's filter.
    virtual void receive(Signal* event) = 0;

    // 0x0045FB30: listen on behalf of `object`: point at it, onto the tracker's list.
    void enter_wait_board(Entity* object);

    SharedRef<Entity> owner_object_;            // +0x04
    SignalReceiver* next_subscriber_ = nullptr; // +0x10 the tracker's list (set by the tracker)
    bool on_board_ = false;                     // +0x14 on the tracker's list
    int busy_ = 0;                              // +0x18 raised by the tracker while it asks this listener (0x00462F00)
};

// A script's event handlers (Scripts::CScript::signal_routes_, 0x10 bytes, on the front-end heap).
// Entries are never taken out while the table might be running them: removing one sets its script to
// kNullScript (and frees its parameters); Compact drops those later.
class SignalRouteTable {
public:
    struct TableRow {
        enum : uint8_t {
            kFlagReplacesRunning = 0x01, // replaces the running script instead of interrupting it
            kFlagMarked = 0x02,          // marked for removal (mark_group / remove_marked)
        };
        uint32_t type;          // +0x00 the event type (0 after remove_type)
        uint32_t script;        // +0x04 the handler script; kNullScript = removed
        uint32_t group;         // +0x08
        script::Struct* params; // +0x0C a copy, owned
        uint8_t flags;          // +0x10 (bits 2-7 are left as they were - uninitialised in new entries)
        uint8_t pad[3];
    };

    // 0x0045F820
    SignalRouteTable();
    // 0x0045FB70: the destructor: every entry's parameters, then the array (the fields aren't cleared).
    ~SignalRouteTable();
    SignalRouteTable(const SignalRouteTable&) = delete;
    SignalRouteTable& operator=(const SignalRouteTable&) = delete;

    // 0x0045FBC0: a handler for `type`: replaces the entry of that type, else reuses a removed entry, else a new
    // one at the end. The tracker is told `script` listens for `type` unless the entry of that type was live.
    void append_listener(script::Script* script, uint32_t type, uint32_t script_name, uint32_t group, bool exception,
                         script::Struct* params);
    // 0x004601A0: the handler structures of `handlers` ({type, script, [params = {...}], [group = g], [exception]})
    // go in front of the existing entries; with `replace`, existing entries of each type are removed first, and
    // an empty array removes (and drops) them all. The tracker isn't told.
    void add_handlers(script::Array* handlers, bool replace);

    // 0x0045FDF0: remove the entries of `type` (the tracker isn't told).
    void remove_type(uint32_t type);
    // 0x0045FE60: remove the entries of `group` (kAllGroups: all; the tracker isn't told).
    void remove_group(uint32_t group);
    // 0x004600E0: the same, and the tracker forgets `script` listens for their types.
    void remove_group(script::Script* script, uint32_t group);
    // 0x0045F840: mark the live entries of `group` (not kAllGroups) for remove_marked.
    void mark_group(uint32_t group);
    // 0x0045FED0: remove the marked entries; the tracker forgets `script` listens for their types.
    void remove_marked(script::Script* script);
    // 0x0045FF50: drop the removed entries (a new array on the front-end heap).
    void compact();

    // 0x0045FA10 / 0x0045FA60: tell the tracker `script` listens for the live entries' types / no longer
    // listens for any entry's type.
    void enter_wait_board(script::Script* script);
    void unregister_with_tracker(script::Script* script);

    // 0x00460470: run the handlers of `event` for `script` (the table's owner), in table order.
    void forward_trigger(Signal* event, script::Script* script);

    // 0x0045F880 (cdecl): print the table (null: "Emtpy" [sic]); returns debug::Printf's result.
    static int list_wait_table(SignalRouteTable* table);

    TableRow* entries_; // +0x00
    int num_;           // +0x04
    bool alive_; // +0x08 cleared when the script dies while forward_trigger runs (forward_trigger then frees the table)
    bool altered_; // +0x09 entries were added or moved (forward_trigger stops: its entry pointer may be stale)
    uint8_t pad0a_[2];
    int in_use_; // +0x0C forward_trigger nesting depth
};

} // namespace objects
