#pragma once

#include <cstddef>
#include <cstdint>

#include "base/core/down_cast.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/tokens.h"
#include <gsl/pointers>

// NOT ORIGINAL - the QB interpreter's side of a running script: CScript's operations on its record (cscript.h) -
// making and tearing it down, binding it to a script, its Update and the statements it runs, its waits, call stack,
// interrupts and event handlers - and the list of all scripts with the manager's walks over it. From the disassembly
// of 0x004728F0-0x00475920 (docs/script-runtime.md); the layout is the record's.
//
// A QbScript is the record seen by the interpreter: it adds methods and no fields, and every script is made by the
// interpreter (create / make_new), so the record a Script* points at is always one - the interpreter converts with
// as_qb. The rest of the game reaches these only through script::Host (qb_host.h): this header is the interpreter's
// own (tools/script_headers.test.ts).
//
// Every method is __thiscall, callee-pops (RET n = 4 * arguments); the free functions are __cdecl. None takes
// arguments in other registers.
namespace script::qb {

class QbScript final : public Script {
public:
    // --- life (group A: cscript_core.cpp) ---
    // 0x004738E0: the constructor (returns this). Zeroes the whole object (after constructing shared_base_:
    // its vptr ends up null - kept), takes the next unique id, goes to the head of the list.
    QbScript* make_new();
    // 0x00474420: the destructor body: reset_script_state, report_stopped, frees the callback parameters, leaves the
    // list, forgets its stored randoms, lets go of the event handler table, ~shared_base_, ~entity_.
    void teardown();
    // Never destroyed by a delete-expression: destroy() runs teardown (the destructor's body - it frees the owned
    // parameters) and gives the memory back to the pool; make_new is the constructor. Never copied or moved.
    ~QbScript() = delete;
    QbScript(const QbScript&) = delete;
    QbScript& operator=(const QbScript&) = delete;
    QbScript(QbScript&&) = delete;
    QbScript& operator=(QbScript&&) = delete;
    // 0x00449970 / 0x004499B0 (cdecl): CPoolable new / delete (the pool 0x006F3548, else the current heap).
    static void* operator new(size_t size);
    static void operator delete(void* script);
    // NOT ORIGINAL - the pair the game's inlined new / delete of a script come to: one from the pool (else the
    // current heap) made by make_new - null when there's no memory -; and one torn down and given back (null:
    // nothing).
    static QbScript* create();
    static void destroy(QbScript* script);

    // 0x00473A80: stop: run the OnExitRun scripts, free the code copy, release the code and every level's
    // code from the script cache, free the parameters and every level's, back to the inline call stack /
    // loop, no wait, no object, no name, interrupt flags cleared.
    void reset_script_state();
    // 0x00473880: run the pending OnExitRun scripts (this level's, then each return address' from the top) as
    // interrupts. Also called by 0x00583470.
    void run_on_exit_scripts();
    // 0x00473010: after reset_script_state: the tracker forgets the event handler table's types for this script and
    // every handler is removed (group all_groups); with kScriptFlagTrackerBlocked, the tracker (0x006F0FC0,
    // 0x00462B90) forgets it.
    void report_stopped();
    // 0x004739D0: a copy of `params` if it is (or is inside) this script's own parameters or function
    // parameters, which reset_script_state is about to free; else null.
    gsl::owner<Struct*> copy_params_if_owned(Struct* params);
    // 0x00474540: run `name` from the start: `code`, or the cached code of `name` (if there is none: stop and
    // report_stopped). The parameters are the script's defaults (its first line) plus `params`.
    void bind_script(uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object);
    // 0x004747A0: bind_script(name, null, params, object). (0x004747C0, the name as a string: Host::bind_script with
    // its checksum.)
    void bind_script(uint32_t name, Struct* params, objects::Entity* object);
    // 0x00474720: bind_script with a private copy of `script`'s code (code_copy_, on the script heap) - a script
    // held in a structure (0x0048B200 gets it with 0x00476900).
    void set_script_copy(ParamScript* script, Struct* params, objects::Entity* object);
    // 0x004747F0: start again the script it was started with (the bottom level's name, parameters, object).
    void relaunch();
    // 0x00472C90: whether the scripts of every level are still defined (the bottom level is skipped when it
    // runs a private copy).
    bool all_scripts_still_defined();
    // 0x00472F90: spawner_file_ = the .qb file of `spawner`'s bottom-level script (SpawnScript's Function).
    void set_spawner_file(Script* spawner);
    // 0x00473BE0: `object` is going: let go of it at every level, and stop waiting.
    void object_destroyed(objects::Entity* object);
    // 0x00472F00: no OnExitRun script at any level.
    void clear_on_exit_scripts();

    // --- the interpreter (group B: cscript_exec.cpp) ---
    // 0x004750B0: run statements until it waits or ends.
    ScriptOutcome tick_frame();
    // The statements tick_frame runs, from `pc` (NOT ORIGINAL: a cursor at pc_ over its block of code, moved as the
    // original moved pc_; pc_ is written back before anything is called).
    // 0x00474B40: one statement: a C function / member function / script call (also `obj:Func`), an
    // assignment `name = value`, or an expression (its truth value). Returns the call's result.
    bool execute_command(Tokens& pc);
    // 0x00475050: if / if not (token 0x47): run the condition, skip to the else / endif when false.
    void run_conditional(Tokens& pc);
    // 0x00473C60: switch (token 0x3C) to the matching case (or past endswitch).
    void run_multi_branch(Tokens& pc);
    // 0x00472A70: past the endswitch matching the switch just entered (nested switches skipped).
    void skip_to_end_of_switch(Tokens& pc);
    // A matched case: past the labels after it, to its statements. NOT ORIGINAL (the script check): a switch's case
    // labels counted; into the arm'th one's statements (past the last: the default or past the endswitch).
    void skip_case_labels(Tokens& pc);
    int count_case_labels(Tokens pc);
    void go_to_case(Tokens& pc, int arm);
    // 0x00473220 / 0x00472AB0 / 0x00472B50: begin, repeat [n], break.
    void run_loop_open(Tokens& pc);
    void run_counted_loop(Tokens& pc);
    void run_loop_exit(Tokens& pc);
    // 0x00472950: the rest of the line as stmt_args_.
    void parse_function_params(Tokens& pc);
    // 0x004729E0: the name at pc (a name token, or <name> / <...> from the parameters), or 0.
    uint32_t read_name(Tokens& pc);
    // NOT ORIGINAL - execute_command's end, from its arguments in stmt_args_ on: `name` run (a script called, a C
    // function or member function run) / `function` run on `object` (object:Command). Also run for a TypeScript
    // level's command calls.
    bool run_command(uint32_t name);
    bool run_command_on(uint32_t function, objects::Entity* object);
    // NOT ORIGINAL - TypeScript levels (cscript_ts.cpp, ts_bridge.h): the level at pc (a kTsBody token) run on until it
    // calls a command (carried out here) or ends (as a return: true when that level was an interrupt - Update returns
    // FINISHED_INTERRUPT); every level's QuickJS state dropped (reset_script_state).
    bool run_ts_level(Tokens& pc);
    void forget_ts_levels();
    // 0x004731A0 / 0x004731E0: a member function on `object` (null: entity_; none: false) - by table entry
    // or by name.
    bool invoke_method_by_name(const MemberFunctionEntry* entry, objects::Entity* object);
    bool invoke_method_by_name(uint32_t name, objects::Entity* object);

    // --- waiting, call stack, events (group C: cscript_wait.cpp) ---
    // 0x00473050: count down / check this Update's wait (see BlockReason).
    void update_wait();
    // NOT ORIGINAL - the script check (script_check.h): a wait for time or frames ends at the next Update - nothing in
    // a check's run sees time pass but its waits.
    void skip_wait();
    // 0x004728F0: stop waiting. 0x00472900: whether blocked (`print`: prints "m_wait_type == %i").
    void stop_waiting();
    bool is_blocked(bool print);
    // 0x00472BD0 / 0x00472BF0: wait `frames` Updates (one per frame: kSuspendOnFrame).
    void get_stall(int frames);
    void suspend_until_next_frame(int frames);
    // 0x00472C10: wait `ms` of frame time (as seconds, * 0.001f). 0x00473310: `ms` of the game clock.
    void lull(int ms);
    void wait_real_time(int ms);
    // 0x00472FD0: block until an event of type `event_type` (0: until unblocked); registers with the tracker.
    void gate_off(uint32_t event_type);
    // 0x004739A0: wait (type 5-10) until `component` says so; marks the component (+0x08 |= 2).
    void set_wait_on_component(BlockReason type, objects::ObjectModule* component, uint32_t arg);
    // 0x00472D20: `component` is going: no level waits on it any more.
    void component_destroyed(objects::ObjectModule* component);
    // 0x00473800: an event: unblocks the levels blocked on its type, then the event handler table gets it.
    // Always true.
    bool receive_event(objects::Signal* event);
    // 0x00472F40: the level an interrupt interrupted stops waiting (the Function UnBlockInterruptedScript).
    void unblock_interrupted_script();
    // 0x004748E0: call `name` (`code`; its default parameters plus `params`, on `object`): the current state
    // goes on the call stack. With no code running, just bind_script. `interrupted`: returning ends Update (5).
    void run_sub_script(uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object, bool interrupted);
    // 0x00473E70: '$' / return: release the code, pop the caller's state (none: the script ends); `results`
    // (the return's parameters) are added to the caller's parameters. Returns whether the level that ended
    // was an interrupt.
    bool return_from_script(Struct* results);
    // 0x00475750: run the cached script `name` now, as an interrupt, then carry on Updating.
    ScriptOutcome preempt(uint32_t name, Struct* params);
    // Event handlers (0x00472EA0, 0x00472EC0, 0x004734E0, 0x004735D0, 0x00473690, 0x004736C0): through
    // signal_routes_ (made on demand: 0x10 bytes, 0x0045F820, on the front-end heap).
    void clear_event_handler_group(uint32_t group);          // 0x00472EA0 (0x004600E0)
    int print_event_handler_table();                         // 0x00472EC0 (0x0045F880, cdecl)
    void install_event_table(Array* handlers, bool replace); // 0x004734E0
    void set_event_handler(uint32_t event_type, uint32_t script, uint32_t group, bool exception,
                           Struct* params);                             // 0x004735D0
    void clear_event_handler(uint32_t event_type);                      // 0x00473690
    void reset_event_handlers_from_table(Array* table, uint32_t group); // 0x004736C0
};
static_assert(sizeof(QbScript) == sizeof(Script), "the interpreter's view adds no fields to the record");

// The record a script pointer points at, as the interpreter's (every one is: the interpreter makes them all).
inline QbScript* as_qb(Script* script) { return core::down_cast<QbScript>(script); }

// The scripts' shared state (the original's globals; cscript_core.cpp's record).
QbScript*& script_list_head();
QbScript*& current_script_var(); // get_running_script 0x004728C0
int& script_count();
uint32_t& next_script_unique_id();
bool& updating_spawned_scripts();
bool& delete_spawned_scripts_pending();
bool& one_per_frame_released(); // kSuspendOnFrame
// (also: the CScript pool pair 0x006F3548 - param_struct.h, made by qb.cpp's create_script_pools; the script cache
// 0x006F3558 - cache.h; the object tracker 0x006F0FC0; online::Hub 0x007CCE14; the game clock offset 0x00696D60)

// --- free functions ---
// 0x00472980 (group B): a member function on `object` (no null check): the object itself for the two special
// component types (vtable +0x08), else its component of that type (0x0045DB90; vtable +0x2C).
bool invoke_method_by_name(const MemberFunctionEntry* entry, Struct* params, objects::Entity* object, Script* script);
// 0x004734B0 (group A): the script with this unique id, or null.
Script* find_script_with_unique_id(uint32_t id);

// The script manager (group D: cscript_manager.cpp).
// 0x00475920: one Update of every spawned script (newest first); finished ones run their callback and go.
void tick_child_scripts();
// 0x004753C0: delete the spawned scripts that aren't kScriptFlagNotSessionSpecific (deferred to the end of
// tick_child_scripts if called during it).
void kill_all_child_scripts();
// 0x00475420: spawn `name` (a C function is just called, with a temporary script; returns null).
Script* spawn_script(uint32_t name, Struct* params, const SpawnSetup& setup);
// 0x004756C0: delete `script` if it's spawned.
void kill_spawned_script(Script* script);
// 0x00475790: run `name` to the end now (a C function / member function is just called); `name_string` isn't used.
// With `replicate_flag` in a network game, the other players are told (0x00473350).
void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                   const char* name_string);
// 0x004752F0: one Update of `name`, then its parameters are added to `results` (not checked for null).
void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results);
// 0x00473350 / 0x00473420: the network messages for launch_script (0x48) and SpawnScript (0x49).
void send_run_script_message(uint32_t name, Struct* params, objects::Entity* object);
void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent);
// 0x00472D80 / 0x00472DC0: pause or unpause every spawned script / unpause one.
void pause_spawned_scripts(bool pause);
void resume_child_script(Script* script);
// 0x00472DE0: whether `name` is a script. 0x00472E00: `script`'s spawn id (0 if not spawned).
bool script_exists(uint32_t name);
uint32_t get_spawn_id(Script* script);
// 0x00472E20: the spawned script with spawn id `id`, or null.
Script* look_up_spawned_script(uint32_t id);
// 0x00472E70 (group C): component_destroyed on every script.
void component_destroyed_all_scripts(objects::ObjectModule* component);
// Stopping scripts (reset_script_state + report_stopped; they stay on the list, tick_child_scripts deletes spawned
// ones once Update says finished):
void stop_scripts_targeting(uint32_t name); // 0x00473FA0 (any level)
void stop_scripts_by_tag(uint32_t id);      // 0x00474040 (spawn id, unique id or spawner file)
void stop_scripts_of_object_and_tag(objects::Entity* object, uint32_t id, Script* except);        // 0x004740D0
void end_child_scripts_of_entity_by_name(objects::Entity* object, uint32_t name, Script* except); // 0x00474190
void stop_all_scripts();                                                // 0x00474250 (every script)
void stop_scripts_using_object(objects::Entity* object, uint32_t name); // 0x004742B0 (name 0: any; top level only)
void end_scripts_bound_to_entity(objects::Entity* object);              // 0x004756F0 = 0x004742B0(object, 0)
void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name); // 0x00474350 (name at any level)
// 0x00475700: delete the scripts that aren't spawned, have no code and no event handler table (Cleanup).
void delete_finished_scripts();
// The loader's walks (load.cpp): every script with `name` at any level started again (restart_changed_scripts
// 0x0046ED20, for a script that changed); every script whose code is gone stopped (unload_qb 0x0046EEC0).
void relaunch_scripts_referring_to(uint32_t name);
void stop_scripts_left_without_code();

} // namespace script::qb
