#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include "engine/core/mem.h"
#include "engine/lists/key_map.h"
#include "engine/objects/signal.h"

// The object tracker (GameObjects::EventWaitBoard, 0xB21C bytes, the singleton 0x006F0FC0 with its use count 0x006F0FC4):
// - the game objects by id (+0x08) and their aliases (+0x0C) - find_object / ObjectExists / AssignAlias;
// - for every event type, the scripts listening for it (+0x10): a script is added when it blocks on that type
//   (CScript::gate_off) or has an event handler for it (the event handler table), and removed again when it stops;
// - the scripts waiting in WaitForEvent (+0xB038, 40 of them);
// - the event listeners (+0x14, C++ objects that see every targeted event first);
// - the event log (+0x20, the last 1024 launches and what became of them; printed by PrintEventLog - in this
//   build only its header and footer, the entries' printing is compiled out).
// LaunchEvent (0x00462F00) delivers an event; how is in docs/script-runtime.md, section 11.
//
// Conventions: every method is __thiscall, callee-pops; create_tracker and the CFunctions are __cdecl. None takes
// arguments in other registers.

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class Entity; // +0x08 flags (0x20: has a position), +0x14 id, +0x3C alias count, +0x4C position

// The "no object" id: the target or source of an event that has none.
constexpr uint32_t kNoObjectId = 0x36B2EE74;

// One script in a list of scripts (the original's node type has a vtable, 0x006478D8, holding only its destructor).
// The list is a ring through the head; the head's data is -1.
class ScriptNode {
public:
    // 0x00462530 (0x00462550 the deleting destructor): out of its list (linked to itself again).
    void uncouple();

    bool is_head() const { return payload_ == reinterpret_cast<script::Script*>(-1); }

    script::Script* payload_; // the head: -1
    int rank_;                // always 0
    ScriptNode* following_;
    ScriptNode* preceding_;
};

// The head of a list of scripts: the scripts listening for one event type (the original's head type has a vtable,
// 0x006478DC, holding only its destructor). Made from the pool pair 0x006F0FC8.
class ScriptList : public ScriptNode {
public:
    // 0x00462620: add `script` at the end (nothing if it's already in), the node on the script heap.
    void append_one(script::Script* script);
    // 0x004626B0: take `script` out (its node deleted).
    void drop_out(script::Script* script);
    // (0x00462710, the head's deleting destructor - freeing to the heap, not the pool - is never called.)

    bool is_vacant() const { return preceding_ == this; }

    // The pool pair 0x006F0FC8's new / delete (delete is 0x004624D0, cdecl).
    static void* operator new(size_t size);
    static void operator delete(void* list);
};

// The event log (0xB014 bytes, at tracker +0x20): a ring of the last 1024 entries.
class SignalTrace {
public:
    // What an entry records.
    enum Kind : int {
        kLaunched = 0, // an event was launched (`script`: whether from a script with a name); the depth goes up
        kHandled = 1,  // it was handled (`script` the handler's name, `how` how: 2 WaitForEvent, 3 by nobody); down
        kRead = 2,     // (0x00462B40)
        kFrame = 3,    // a frame went by (consecutive frames count up in the entry's type field)
        kObjectAdded = 4,
        kObjectRemoved = 5,
    };
    struct TableRow {
        uint32_t type; // the event type (FRAME: the number of frames)
        uint32_t target;
        uint32_t source;
        uint32_t script;
        uint32_t how;
        int kind;  // Kind
        int depth; // the launch depth after this entry
    };

    // 0x004627C0: add an entry. The first call also makes the table of event names from the global array
    // 0x08114F90 (copied into names_, keyed by their checksums).
    void add_row(uint32_t type, uint32_t target, uint32_t source, uint32_t script, uint32_t how, int kind);
    // 0x004620A0: print it (the entries' printing is compiled out of this build); there are no new entries
    // after. `only_new` picks the header; `count` isn't used.
    void echo_out(bool only_new, int count);
    // 0x00462CB0: free the name table.
    void teardown();

    TableRow entries_[0x400];
    int ordinal_;             // +0x7000 the next entry
    bool has_looped_;         // +0x7004 the ring is full
    char names_[0x100][0x40]; // +0x7005 the event names (no check that there are at most 256)
    uint8_t unknownb005_[3];
    lists::KeyMap* names_ptr_; // +0xB008 name checksum -> names_ entry (made on the first add_row)
    int layer_;                // +0xB00C how many launches are under way
    int added_since_sweep_;    // +0xB010 entries since the last Print
};

// The tracker (the original's vtable 0x006478E0 holds only its destructor).
class EventWaitBoard : public memory::HeapObject {
public:
    // A script waiting in WaitForEvent.
    struct Waiter {
        uint32_t script_id_; // its unique_id_
        uint32_t type;       // 0: a free slot
        uint32_t object_id_; // its object's id, or 0
    };

    // 0x00462CF0
    EventWaitBoard();
    // 0x00462E40 (0x00463780 the deleting one). The listener table isn't freed - an original leak.
    ~EventWaitBoard();
    EventWaitBoard(const EventWaitBoard&) = delete;
    EventWaitBoard& operator=(const EventWaitBoard&) = delete;

    // 0x00462120: the object with this id or alias, or null. 0x00462190: only by alias.
    Entity* look_up_object(uint32_t id);
    Entity* look_up_by_nickname(uint32_t alias);
    // 0x004621D0: the checksum of the next "autoid<n>" that no object has (n counts up to 999999, then 0).
    uint32_t fetch_distinct_id();
    // 0x00462980: track `object` under its id (an alias with that id is dropped).
    void add_object(Entity* object);
    // 0x00462A20: stop tracking `object`. `renamed`: WaitForEvent waiters on its id follow `fresh_id`; otherwise its
    // aliases go.
    void remove_object(Entity* object, uint32_t fresh_id, bool renamed);
    // 0x00462590: every alias of `object` (by id) goes.
    void remove_aliases_of(Entity* object);
    // 0x00462A90: `alias` names `object` (replacing what it named).
    void register_nickname(uint32_t alias, Entity* object);

    // 0x00461FE0 (entity_hub.cpp): `listener` goes on the front of the list, registered. 0x00462000: the event
    // listener stops seeing events.
    void subscribe_receiver(SignalReceiver* listener);
    void remove_event_listener(SignalReceiver* listener);
    // 0x00463370 / 0x00463450: `script` listens / stops listening for events of type `type`.
    void add_listener(uint32_t type, script::Script* script);
    void remove_listener(uint32_t type, script::Script* script);
    // 0x00462B90: `script` stops listening for any type (when it stops).
    void forget_script(script::Script* script);
    // 0x00462260: `script` waits (gate_off(0)) until an event of `type` reaches its object (or any, without one).
    // With all 40 slots taken by live scripts it silently doesn't wait.
    void wait_for_event(script::Script* script, uint32_t type);

    // 0x00462F00: launch an event. Returns whether something marked it handled.
    bool launch_event(uint32_t type, uint32_t target, uint32_t source, script::Struct* data, bool broadcast,
                      float radius);

    // The log entries other code makes: 0x00462B10 / 0x00462B40 (`event` handled / read by `script`, `how`),
    // 0x00462B70 a frame went by.
    void log_handled(Signal* event, uint32_t how, uint32_t script);
    void log_read(Signal* event, uint32_t how, uint32_t script);
    void log_frame();

    // (The offsets are the original's, which has its vtable at +0x00.)
    int next_auto_id_;                // +0x04
    lists::KeyMap* entity_list_;      // +0x08 id -> Entity (8 bits)
    lists::KeyMap* aliases_;          // +0x0C alias -> Entity (4 bits)
    lists::KeyMap* listeners_;        // +0x10 event type -> ScriptList (8 bits)
    SignalReceiver* event_listeners_; // +0x14
    bool unknown18;                   // +0x18 (cleared by the constructor only)
    uint8_t unknown19[3];
    int launch_depth_;         // +0x1C
    SignalTrace log_;          // +0x20
    int launched_from_script_; // +0xB034 for the next launch's log entry (set by the LaunchEvent Function)
    Waiter waiters_[40];       // +0xB038
    bool unknownb218_;         // +0xB218 (cleared by the constructor only)
};

// The tracker (null until made).
extern EventWaitBoard* g_tracker;
inline EventWaitBoard*& get_tracker() { return g_tracker; }
// 0x00463730: make the tracker if there's none and `create`; count a use either way. Returns it.
EventWaitBoard* create_tracker(bool create);

// The script commands (__cdecl).
bool __cdecl cfunc_wait_for_event(script::Struct* params, script::Script* script);            // 0x00462310
bool __cdecl cfunc_object_exists(script::Struct* params, script::Script* script);             // 0x00462340
bool __cdecl cfunc_terminate_objects_scripts(script::Struct* params, script::Script* script); // 0x00462380
bool __cdecl cfunc_set_object_props(script::Struct* params, script::Script* script);          // 0x00462440
bool __cdecl cfunc_print_event_log(script::Struct* params, script::Script* script);           // 0x00462480
bool __cdecl cfunc_assign_alias(script::Struct* params, script::Script* script);              // 0x00462C40
bool __cdecl cfunc_launch_event(script::Struct* params, script::Script* script);              // 0x004634D0

} // namespace objects
