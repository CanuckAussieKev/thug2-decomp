#pragma once

#include <cstdint>

#include "engine/script/cscript.h"

// NOT ORIGINAL - script::Host: what the game asks of the script runtime - scripts started by name (spawned, launched to
// their end, run for one Update), the spawned scripts' frame (their one Update a frame, pausing, killing), the stop
// walks, and what objects, components and commands do to one running script (make it, bind it to a script, give it
// its Update, set or end its wait, deliver an event, interrupt it, change its event handlers). app/ installs the QB
// interpreter's (script_qb/qb_host.h) at start-up, before any of the original's code runs; not installed, a call
// dereferences null. A later QuickJS runtime is the planned second one.
//
// The grain is one script operation - a script spawned, launched or stopped, one Update of one script, an event
// delivered - and one call a frame for the spawned scripts' walk; never a statement or a token: the interpreter's
// loop (QbScript::tick_frame and what it runs) stays inside script_qb, non-virtual.
//
// The methods are the runtime's functions as the game called them, one for one, with the same names, arguments and
// results (docs/script-runtime.md has what each does and when): the same calls happen in the same order as before -
// the host adds a virtual call, nothing else. What a later runtime has to keep is the scheduling those functions
// define: an object's own script gets its Update from its object's update (the object walk, before the spawned
// scripts' walk); the spawned scripts get theirs from tick_child_scripts, once a frame, newest first; a spawned
// script's first Update is the next walk that reaches it; launch_script runs a script to its end at once;
// run_script_once gives it exactly one Update; events and interrupts run inside the call that delivers them.
//
// The script itself is still the original's record (cscript.h: its fields, read by the commands through its
// accessors); script::Context (stage 11 round 22) is to take the commands' side of it.
namespace objects {
class Entity;
class ObjectModule;
struct Signal;
} // namespace objects

namespace script {

class Array;
class Struct;

class Host {
public:
    Host() = default;
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    Host(Host&&) = delete;
    Host& operator=(Host&&) = delete;

    // --- scripts by name (docs/script-runtime.md sections 9 and 10) ---
    // 0x00475420: a new spawned script on `name` (a C function is just called, with a temporary script: null).
    virtual Script* spawn_script(uint32_t name, Struct* params, const SpawnSetup& setup) = 0;
    // 0x00475790: `name` run to its end now (a C function / member function is just called); with `replicate_flag`
    // the other players are told first. `name_string` isn't used.
    virtual void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                               const char* name_string) = 0;
    // 0x004752F0: one Update of `name`, its parameters then added to `results`, the script deleted.
    virtual void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results) = 0;
    // 0x00472DE0: whether `name` is a script.
    virtual bool script_exists(uint32_t name) = 0;

    // --- the spawned scripts (section 2) ---
    // 0x00475920: the frame's walk - one Update of every spawned script, newest first.
    virtual void tick_child_scripts() = 0;
    // 0x004753C0 / 0x004756C0: every spawned script deleted but the not session specific ones (after the walk, when
    // called during it) / one deleted at once.
    virtual void kill_all_child_scripts() = 0;
    virtual void kill_spawned_script(Script* script) = 0;
    // 0x00472D80 / 0x00472DC0: the spawned scripts' pause.
    virtual void pause_spawned_scripts(bool pause) = 0;
    virtual void resume_child_script(Script* script) = 0;
    // 0x00472E00 / 0x00472E20: a script's spawn id (0 if not spawned); the spawned script with this spawn id.
    virtual uint32_t get_spawn_id(Script* script) = 0;
    virtual Script* look_up_spawned_script(uint32_t id) = 0;
    // 0x00473420: the network message for a script spawned on an object (SpawnScript, 0x49).
    virtual void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent) = 0;

    // --- the stop walks (section 10) ---
    virtual void stop_scripts_targeting(uint32_t name) = 0;                                              // 0x00473FA0
    virtual void stop_scripts_by_tag(uint32_t id) = 0;                                                   // 0x00474040
    virtual void stop_scripts_of_object_and_tag(objects::Entity* object, uint32_t id, Script* except) = 0; // 0x004740D0
    virtual void end_child_scripts_of_entity_by_name(objects::Entity* object, uint32_t name,
                                                     Script* except) = 0;                               // 0x00474190
    virtual void stop_all_scripts() = 0;                                                                 // 0x00474250
    virtual void end_scripts_bound_to_entity(objects::Entity* object) = 0;                               // 0x004756F0
    virtual void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name) = 0;    // 0x00474350
    // 0x00475700: the scripts that aren't spawned, have no code and no event handler table deleted (Cleanup).
    virtual void delete_finished_scripts() = 0;

    // --- every script ---
    // 0x004728C0: the script running now (in its Update), or null.
    virtual Script* get_running_script() = 0;
    // 0x004734B0: the script with this unique id, or null.
    virtual Script* find_script_with_unique_id(uint32_t id) = 0;
    // 0x00472E70: `component` is going - no script waits on it any more.
    virtual void component_destroyed_all_scripts(objects::ObjectModule* component) = 0;
    // The loader's walks (load.cpp): every script with `name` at any level started again (restart_changed_scripts,
    // 0x0046ED20, for a script that changed); every script whose code is gone stopped (unload_qb, 0x0046EEC0).
    virtual void relaunch_scripts_referring_to(uint32_t name) = 0;
    virtual void stop_scripts_left_without_code() = 0;

    // --- one script (an object's, a component's or a command's; section 8) ---
    // A new script (from the script pool, else the current heap; null when there's no memory) / one torn down and
    // given back (null: nothing).
    virtual Script* create() = 0;
    virtual void destroy(Script* script) = 0;
    // 0x004747A0 / 0x00474720: `script` started on `name` (or a private copy of a structure's script) with its
    // default parameters plus `params`, on `object`. No statement runs until its next Update.
    virtual void bind_script(Script* script, uint32_t name, Struct* params, objects::Entity* object) = 0;
    virtual void set_script_copy(Script* script, ParamScript* code, Struct* params, objects::Entity* object) = 0;
    // 0x004750B0: one Update - statements until it waits or ends (section 3).
    virtual ScriptOutcome tick_frame(Script* script) = 0;
    // 0x00473BE0: `object` is going: `script` lets go of it at every level and stops waiting.
    virtual void object_destroyed(Script* script, objects::Entity* object) = 0;
    // 0x00473880 / 0x00472F00: the pending OnExitRun scripts run as interrupts / dropped.
    virtual void run_on_exit_scripts(Script* script) = 0;
    virtual void clear_on_exit_scripts(Script* script) = 0;
    // 0x00472F90: `spawned`'s spawner file = the .qb file of `spawner`'s bottom-level script.
    virtual void set_spawner_file(Script* spawned, Script* spawner) = 0;

    // --- one script's wait (section 5) ---
    virtual void stop_waiting(Script* script) = 0;                 // 0x004728F0
    virtual bool is_blocked(Script* script, bool print) = 0;       // 0x00472900
    virtual void get_stall(Script* script, int frames) = 0;        // 0x00472BD0: `frames` Updates
    virtual void suspend_until_next_frame(Script* script, int frames) = 0; // 0x00472BF0: one per frame
    virtual void lull(Script* script, int ms) = 0;                 // 0x00472C10: frame time
    virtual void wait_real_time(Script* script, int ms) = 0;       // 0x00473310: the game clock
    virtual void gate_off(Script* script, uint32_t event_type) = 0; // 0x00472FD0: blocked until the event
    // 0x004739A0: until `component` says so.
    virtual void set_wait_on_component(Script* script, BlockReason type, objects::ObjectModule* component,
                                       uint32_t arg) = 0;
    // 0x00472F40: the level an interrupt interrupted stops waiting.
    virtual void unblock_interrupted_script(Script* script) = 0;

    // --- one script's events (section 11) ---
    // 0x00473800: the levels blocked on the event's type unblocked, then the event handler table gets it. True.
    virtual bool receive_event(Script* script, objects::Signal* event) = 0;
    // 0x00475750: `name` run now on `script`, as an interrupt, then its Update carried on.
    virtual ScriptOutcome preempt(Script* script, uint32_t name, Struct* params) = 0;
    // The event handler table (0x00472EA0, 0x00472EC0, 0x004734E0, 0x004735D0, 0x00473690, 0x004736C0).
    virtual void clear_event_handler_group(Script* script, uint32_t group) = 0;
    virtual int print_event_handler_table(Script* script) = 0;
    virtual void install_event_table(Script* script, Array* handlers, bool replace) = 0;
    virtual void set_event_handler(Script* script, uint32_t event_type, uint32_t handler, uint32_t group,
                                   bool exception, Struct* params) = 0;
    virtual void clear_event_handler(Script* script, uint32_t event_type) = 0;
    virtual void reset_event_handlers_from_table(Script* script, Array* table, uint32_t group) = 0;

protected:
    ~Host() = default;
};

// The installed runtime (app/main.cpp), and installing one.
Host& host();
void install_host(Host& runtime);

} // namespace script
