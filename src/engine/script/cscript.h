#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include "engine/objects/smtptr.h"
#include "engine/script/param_struct.h"
#include "engine/script/member_code.h"
#include "engine/core/slab_bank.h"

// A running script (Scripts::CScript, 0xF4 bytes, pool 0x006F3548 of 300 - ours 1200, engine/core/memory_sizes.h) and the list of all of them.
//
// A script runs compiled .qb code (see qb.h) one statement at a time from its program counter until it has to
// wait or it ends; Update does that once (the callers decide when: the spawned scripts every frame, objects'
// scripts from their own updates, launch_script all at once). Calling another script pushes a return address
// (the caller's whole state, 0x30 bytes) and runs the callee in the same Update; '$' / return pops it.
// Every script is on one list (head 0x006F3538, the newest first). Spawned scripts belong to the list and are
// updated and deleted by tick_child_scripts; the others belong to whoever made them (objects...).
//
// Layout and conventions are from the disassembly of 0x004728F0-0x00475920 (docs/script-runtime.md).
// Every method is __thiscall, callee-pops (RET n = 4 * arguments); the free functions are __cdecl. None takes
// arguments in other registers.
namespace objects {
class Entity;           // a game object (SharedBase; +0x100 its component list)
class ObjectModule;     // one of an object's components; can hold a script waiting on it
class SignalRouteTable; // a script's event handlers (engine/objects/signal.h)
struct Signal;          // an event (engine/objects/signal.h)
} // namespace objects

namespace script {

class Script;

// What Update returns.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
enum ScriptOutcome : uint8_t {
    kScriptOutcomeFinished = 0,  // no code left (the script has ended or was never set)
    kScriptOutcomeBlocked = 1,   // waiting with kSuspendOnBlock or kWaitTypeRealTimer
    kScriptOutcomeSuspended = 2, // any other wait
    // (3 and 4 are never returned by this build)
    kScriptOutcomeEndedByInterrupt = 5, // an interrupt (Interrupt / OnExitRun script) returned
};

// suspend_reason_: what the script waits for before its next statement (checked by update_wait, 0x00473050).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
enum BlockReason : uint8_t {
    kSuspendOnNothing = 0,
    kSuspendOnCount = 1,    // suspend_frames_left_ more Updates (Wait n frames: 0x00472BD0)
    kSuspendOnTimer = 2,    // suspend_clock_ seconds of slomo frame time (Wait n seconds: 0x00472C10)
    kSuspendOnBlock = 3,    // until unblocked - by an event of type suspend_arg_ (Block: 0x00472FD0) or by hand
    kWaitTypeRealTimer = 4, // suspend_clock_ ms of the game clock since suspend_arg_ (0x00473310)
    // 5-10: waiting on blocking_module_, which is asked every Update (its vtable +0x18) - the object waits
    // (move, anim, jump, stop, rotate, stream finished...; set by set_wait_on_component 0x004739A0).
    kWaitTypeObjectFirst = 5,
    kWaitTypeObjectLast = 10,
    kSuspendOnFrame = 11, // like COUNTER, but at 0 only one such script per frame goes on (0x006F352C)
};

// A begin ... repeat loop (0x10 bytes). One is inside the script; the second nested loop moves them all to a
// block of 10 on the script heap (no check beyond 10).
struct RepeatFrame {
    const uint8_t* tokens_begin_; // the first token after 'begin'
    const uint8_t* tokens_end_;   // the token after 'repeat n' (set the first time repeat is reached)
    bool args_seen_;              // repeat had a count (otherwise the loop is endless)
    bool args_expected_;          // repeat's arguments not read yet
    uint16_t unknowna_;
    int headcount_; // repeats left
};
static_assert(sizeof(RepeatFrame) == 0x10, "layout must match the original");
static_assert(offsetof(RepeatFrame, tokens_end_) == 0x4 && offsetof(RepeatFrame, args_seen_) == 0x8 &&
                  offsetof(RepeatFrame, args_expected_) == 0x9 && offsetof(RepeatFrame, headcount_) == 0xC,
              "layout must match the original");

// A return address (0x30 bytes): the calling level's state, saved by run_sub_script and restored by
// return_from_script. Two are inside the script; the third call moves them to a block of 16 on the script
// heap (no check beyond 16).
struct CallFrame {
    uint32_t script_id_;                     // +0x00 script_title_
    Struct* arg_struct_;                     // +0x04 args_ (owned)
    const uint8_t* caller_frame_;            // +0x08 pc_
    objects::Entity* owner_entity_;          // +0x0C entity_ (a plain pointer here: object_destroyed clears it)
    RepeatFrame* loop_state_;                // +0x10 innermost_repeat_
    uint32_t on_exit_script_ptr_;            // +0x14 on_exit_script_
    objects::ObjectModule* sleep_component_; // +0x18 blocking_module_
    float sleep_timer_;                      // +0x1C suspend_clock_
    uint32_t suspend_arg_ref_;               // +0x20 suspend_arg_
    int suspend_frames_ref_;                 // +0x24 suspend_frames_left_
    uint32_t suspend_kind_;                  // +0x28 bits 0-3: suspend_reason_ (the other bits are kept as they are)
    uint8_t trait_mask_;                     // +0x2C bit 0: suspend_clock_started_, bit 1: the level was an interrupt
    uint8_t unknown2d_[3];
};
static_assert(sizeof(CallFrame) == 0x30, "layout must match the original");
static_assert(offsetof(CallFrame, arg_struct_) == 0x04 && offsetof(CallFrame, caller_frame_) == 0x08 &&
                  offsetof(CallFrame, owner_entity_) == 0x0C && offsetof(CallFrame, loop_state_) == 0x10 &&
                  offsetof(CallFrame, on_exit_script_ptr_) == 0x14 && offsetof(CallFrame, sleep_component_) == 0x18 &&
                  offsetof(CallFrame, sleep_timer_) == 0x1C && offsetof(CallFrame, suspend_arg_ref_) == 0x20 &&
                  offsetof(CallFrame, suspend_frames_ref_) == 0x24 && offsetof(CallFrame, suspend_kind_) == 0x28 &&
                  offsetof(CallFrame, trait_mask_) == 0x2C,
              "layout must match the original");
constexpr uint32_t kReturnWaitTypeMask = 0xF;
constexpr uint8_t kReturnFlagTimerStarted = 0x01;
constexpr uint8_t kReturnFlagInterrupted = 0x02;

// A script held by a structure component (8 bytes, filled by CStruct's 0x00476900): its name and its code.
// (Group A: set_script_copy's argument.)
struct ParamScript {
    uint32_t own_name_hash_;
    const uint8_t* code_stream_;
};
static_assert(sizeof(ParamScript) == 8, "allocated as 8 bytes by 0x0048B200");

// A member function's table entry (the value of a kQbvalNativeMethod symbol, token 0x46), 0x18 bytes: the
// tables at 0x00683550 (Entity's, filled in by 0x00642940) and 0x00683268 (GameEntity's, 0x006425D0).
struct MemberFunctionEntry {
    uint32_t callsign_;
    uint32_t owner_kind_; // the object component that runs it; 0x57525413 / 0x0ADA4E74: the object itself
    // +0x08: the member function (the original's: a 16-byte virtual-inheritance member function pointer; ours: its
    // call_member_function, member_code.h); null: the object looks the name up (Entity::invoke_method_by_name 0x0045D850).
    MemberCode method_code_;
    int this_adjustment_;         // +0x0C
    int virtual_base_offsets_[2]; // +0x10 (unused by single-inheritance classes)
};
static_assert(sizeof(MemberFunctionEntry) == 0x18, "a table entry is 0x18 bytes");

class Script {
public:
    // --- life (group A: cscript_core.cpp) ---
    // 0x004738E0: the constructor (returns this). Zeroes the whole object (after constructing shared_base_:
    // its vptr ends up null - kept), takes the next unique id, goes to the head of the list.
    Script* make_new();
    // 0x00474420: the destructor body: reset_script_state, report_stopped, frees the callback parameters, leaves the
    // list, forgets its stored randoms, lets go of the event handler table, ~shared_base_, ~entity_.
    void teardown();
    // 0x00449970 / 0x004499B0 (cdecl): CPoolable new / delete (the pool 0x006F3548, else the current heap).
    static void* operator new(size_t size);
    static void operator delete(void* script);
    // NOT ORIGINAL - the pair the game's inlined new / delete of a script come to: one from the pool (else the
    // current heap) made by make_new - null when there's no memory -; and one torn down and given back (null:
    // nothing).
    static Script* create();
    static void destroy(Script* script);

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
    Struct* copy_params_if_owned(Struct* params);
    // 0x00474540: run `name` from the start: `code`, or the cached code of `name` (if there is none: stop and
    // report_stopped). The parameters are the script's defaults (its first line) plus `params`.
    void bind_script(uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object);
    // 0x004747A0 / 0x004747C0: bind_script(name, null, params, object), the name as a checksum or a string.
    void bind_script(uint32_t name, Struct* params, objects::Entity* object);
    void bind_script(const char* name, Struct* params, objects::Entity* object);
    // 0x00474720: bind_script with a private copy of `script`'s code (code_copy_, on the script heap) - a script
    // held in a structure (0x0048B200 gets it with 0x00476900).
    void set_script_copy(ParamScript* script, Struct* params, objects::Entity* object);
    // 0x004747F0: start again the script it was started with (the bottom level's name, parameters, object).
    void relaunch();
    // 0x00472C90: whether the scripts of every level are still defined (the bottom level is skipped when it
    // runs a private copy).
    bool all_scripts_still_defined();
    // 0x00472940: whether it has code to run.
    bool has_attached_script();
    // 0x00472F90: spawner_file_ = the .qb file of `spawner`'s bottom-level script (SpawnScript's Function).
    void set_spawner_file(Script* spawner);
    // 0x00473BE0: `object` is going: let go of it at every level, and stop waiting.
    void object_destroyed(objects::Entity* object);
    // 0x00472ED0 / 0x00472EE0: the OnExceptionRun script. 0x00472EF0: the OnExitRun script.
    void set_on_exception_script(uint32_t name);
    uint32_t get_on_exception_script();
    void set_on_exit_script(uint32_t name);
    // 0x00472F00: no OnExitRun script at any level.
    void clear_on_exit_scripts();

    // --- the interpreter (group B: cscript_exec.cpp) ---
    // 0x004750B0: run statements until it waits or ends.
    ScriptOutcome tick_frame();
    // 0x00474B40: one statement: a C function / member function / script call (also `obj:Func`), an
    // assignment `name = value`, or an expression (its truth value). Returns the call's result.
    bool execute_command();
    // 0x00475050: if / if not (token 0x47): run the condition, skip to the else / endif when false.
    void run_conditional();
    // 0x00473C60: switch (token 0x3C) to the matching case (or past endswitch).
    void run_multi_branch();
    // 0x00472A70: past the endswitch matching the switch just entered (nested switches skipped).
    void skip_to_end_of_switch();
    // 0x00473220 / 0x00472AB0 / 0x00472B50: begin, repeat [n], break.
    void run_loop_open();
    void run_counted_loop();
    void run_loop_exit();
    // 0x00472950: the rest of the line as stmt_args_.
    void parse_function_params();
    // 0x004729E0: the name at pc_ (a name token, or <name> / <...> from the parameters), or 0.
    uint32_t read_name();
    // 0x004731A0 / 0x004731E0: a member function on `object` (null: entity_; none: false) - by table entry
    // or by name.
    bool invoke_method_by_name(MemberFunctionEntry* entry, objects::Entity* object);
    bool invoke_method_by_name(uint32_t name, objects::Entity* object);

    // --- waiting, call stack, events (group C: cscript_wait.cpp) ---
    // 0x00473050: count down / check this Update's wait (see BlockReason).
    void update_wait();
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

    // --- the fields ---
    Script* following_;             // +0x00 the list (older)
    Script* prior_entry_;           // +0x04 (newer; null for the head)
    uint8_t* code_copy_;            // +0x08 a private copy of the code (set_script_copy), freed by reset_script_state
    const uint8_t* pc_;             // +0x0C the next token; null: not running
    Struct* stmt_args_;             // +0x10 the current statement's arguments (owned)
    Struct* args_;                  // +0x14 the script's parameters (owned; CFunctions leave results here)
    RepeatFrame* innermost_repeat_; // +0x18 the innermost begin ... repeat (null: none)
    RepeatFrame inline_loop_;       // +0x1C
    RepeatFrame* loops_;            // +0x2C inline_loop_, or 10 on the script heap
    int call_depth_;                // +0x30 the call depth
    CallFrame inline_return_addresses_[2]; // +0x34
    CallFrame* frame_records_;             // +0x94 inline_return_addresses_, or 16 on the script heap
    int suspend_frames_left_;              // +0x98 kSuspendOnCount / ONE_PER_FRAME
    bool suspend_clock_started_;           // +0x9C kSuspendOnTimer: the first Update has been
    uint8_t unknown9d_[3];
    float suspend_clock_;     // +0xA0 kSuspendOnTimer seconds left / REAL_TIMER ms
    uint32_t suspend_reason_; // +0xA4 BlockReason
    uint32_t suspend_arg_;    // +0xA8 BLOCKED: the event type; REAL_TIMER: the start; 5-10: for the component
    objects::ObjectModule* blocking_module_; // +0xAC types 5-10
    uint32_t unique_id_;                     // +0xB0 from 0x006F3540
    uint8_t flag_bits_;                      // +0xB4 SCRIPT_RUN_*
    uint8_t unknownb5_[3];
    int list_link_;                              // +0xB8 SpawnScript's node
    objects::SharedRef<objects::Entity> entity_; // +0xBC the object it runs on (+0xC0 / +0xC4 the pointer's links)
    uint32_t script_title_;                      // +0xC8 the script running now (the top level)
    uint8_t spawn_flags_;                        // +0xCC SCRIPT_FLAG_*
    uint8_t unknowncd_[3];
    uint32_t spawner_file_;                    // +0xD0 the .qb file of the script that spawned it (set_spawner_file)
    uint32_t spawn_id_;                        // +0xD4 SpawnScript's id
    uint32_t on_finish_script_;                // +0xD8 run when a spawned script ends
    Struct* callback_params_;                  // +0xDC (owned)
    objects::SignalRouteTable* signal_routes_; // +0xE0 made on demand
    uint32_t on_exception_script_;             // +0xE4 OnExceptionRun
    uint32_t on_exit_script_;                  // +0xE8 OnExitRun (this level's)
    objects::SharedBase shared_base_;          // +0xEC lets smart pointers point at the script (vtable 0x006478E4)
};
static_assert(offsetof(Script, prior_entry_) == 0x04 && offsetof(Script, code_copy_) == 0x08 &&
                  offsetof(Script, pc_) == 0x0C && offsetof(Script, stmt_args_) == 0x10 &&
                  offsetof(Script, args_) == 0x14 && offsetof(Script, innermost_repeat_) == 0x18 &&
                  offsetof(Script, inline_loop_) == 0x1C && offsetof(Script, loops_) == 0x2C &&
                  offsetof(Script, call_depth_) == 0x30 && offsetof(Script, inline_return_addresses_) == 0x34 &&
                  offsetof(Script, frame_records_) == 0x94,
              "layout must match the original");
static_assert(offsetof(Script, suspend_frames_left_) == 0x98 && offsetof(Script, suspend_clock_started_) == 0x9C &&
                  offsetof(Script, suspend_clock_) == 0xA0 && offsetof(Script, suspend_reason_) == 0xA4 &&
                  offsetof(Script, suspend_arg_) == 0xA8 && offsetof(Script, blocking_module_) == 0xAC &&
                  offsetof(Script, unique_id_) == 0xB0 && offsetof(Script, flag_bits_) == 0xB4 &&
                  offsetof(Script, list_link_) == 0xB8 && offsetof(Script, entity_) == 0xBC,
              "layout must match the original");
static_assert(offsetof(Script, script_title_) == 0xC8 && offsetof(Script, spawn_flags_) == 0xCC &&
                  offsetof(Script, spawner_file_) == 0xD0 && offsetof(Script, spawn_id_) == 0xD4 &&
                  offsetof(Script, on_finish_script_) == 0xD8 && offsetof(Script, callback_params_) == 0xDC &&
                  offsetof(Script, signal_routes_) == 0xE0 && offsetof(Script, on_exception_script_) == 0xE4 &&
                  offsetof(Script, on_exit_script_) == 0xE8 && offsetof(Script, shared_base_) == 0xEC,
              "layout must match the original");
static_assert(sizeof(Script) == 0xF4, "allocated as 0xF4 bytes by the original");

// flag_bits_ (+0xB4)
constexpr uint8_t kScriptRunInterrupted = 0x01; // the top level was entered as an interrupt
constexpr uint8_t kScriptRunWaitUpdated = 0x02; // the wait was already counted this Update (set on return)
// spawn_flags_ (+0xCC)
constexpr uint8_t kScriptFlagSpawned = 0x01;
constexpr uint8_t kScriptFlagNotSessionSpecific = 0x02; // survives kill_all_child_scripts
constexpr uint8_t kScriptFlagPaused = 0x04;
constexpr uint8_t kScriptFlagPauseWithObject = 0x08; // not updated while its object says no (vtable +0x1C)
constexpr uint8_t kScriptFlagTrackerBlocked = 0x10;  // Block registered it with the tracker

// The globals.
extern Script* g_script_list_head;
extern Script* g_running_script;
extern int g_script_count;
extern uint32_t g_next_script_unique_id;
extern bool g_updating_spawned_scripts;
extern bool g_delete_spawned_scripts_pending;
extern bool g_one_per_frame_released; // kSuspendOnFrame
extern memory::PoolPair g_script_pools;
inline Script*& script_list_head() { return g_script_list_head; }
inline Script*& current_script_var() { return g_running_script; } // get_running_script 0x004728C0
inline int& script_count() { return g_script_count; }
inline uint32_t& next_script_unique_id() { return g_next_script_unique_id; }
inline bool& updating_spawned_scripts() { return g_updating_spawned_scripts; }
inline bool& delete_spawned_scripts_pending() { return g_delete_spawned_scripts_pending; }
inline bool& one_per_frame_released() { return g_one_per_frame_released; }
// (also: the CScript pool pair 0x006F3548 - qb.cpp creates it; the script cache 0x006F3558 - cache.h; the object
// tracker 0x006F0FC0; online::Hub 0x007CCE14; the game clock offset 0x00696D60)

// --- free functions ---
// 0x00472980 (group B): a member function on `object` (no null check): the object itself for the two special
// component types (vtable +0x08), else its component of that type (0x0045DB90; vtable +0x2C).
bool invoke_method_by_name(MemberFunctionEntry* entry, Struct* params, objects::Entity* object, Script* script);
// 0x004734B0 (group A): the script with this unique id, or null.
Script* find_script_with_unique_id(uint32_t id);

// The script manager (group D: cscript_manager.cpp).
// 0x00475920: one Update of every spawned script (newest first); finished ones run their callback and go.
void tick_child_scripts();
// 0x004753C0: delete the spawned scripts that aren't kScriptFlagNotSessionSpecific (deferred to the end of
// tick_child_scripts if called during it).
void kill_all_child_scripts();
// 0x00475420 / 0x00475670: spawn `name` (a C function is just called, with a temporary script; returns null).
Script* spawn_script(uint32_t name, Struct* params, uint32_t callback, Struct* callback_params, int node, uint32_t id,
                     bool replicate_flag, bool permanent, bool not_session_specific, bool pause_with_object);
Script* spawn_script(const char* name, Struct* params, uint32_t callback, Struct* callback_params, int node,
                     uint32_t id, bool replicate_flag, bool permanent, bool not_session_specific,
                     bool pause_with_object);
// 0x004756C0: delete `script` if it's spawned.
void kill_spawned_script(Script* script);
// 0x00475790 / 0x004758F0: run `name` to the end now (a C function / member function is just called); `name`
// (the string) isn't used. With `replicate_flag` in a network game, the other players are told (0x00473350).
void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                   const char* name_string);
void launch_script(const char* name, Struct* params, objects::Entity* object, bool replicate_flag);
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
} // namespace script
