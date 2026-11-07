#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <array>
#include <span>

#include "base/core/api.h"
#include "base/core/layout_32bit.h"
#include "engine/objects/smtptr.h"
#include "engine/script/param_struct.h"
#include "engine/script/member_code.h"
#include "engine/script/tokens.h"

// A running script's record (Scripts::CScript, 0xF4 bytes, pool 0x006F3548 of 300 - ours 1200,
// engine/memory/memory_sizes.h), the types its fields hold, and the script functions the game calls by name.
//
// A script runs compiled .qb code (see qb.h) one statement at a time from its program counter until it has to
// wait or it ends; one Update does that once (the callers decide when: the spawned scripts every frame, objects'
// scripts from their own updates, launch_script all at once). Calling another script pushes a return address
// (the caller's whole state, 0x30 bytes) and runs the callee in the same Update; '$' / return pops it.
// Every script is on one list (the newest first). Spawned scripts belong to the list and are updated and deleted by
// tick_child_scripts; the others belong to whoever made them (objects...).
//
// NOT ORIGINAL - what runs a script is the script runtime behind script::Host (host.h): the QB interpreter
// (script_qb/qb_script.h: its operations on this record, the list, the manager), which app/ installs. Here is the
// record - the original's layout, read and set by the commands and objects through its accessors - and the
// functions by name, each the installed runtime's (host.cpp).
//
// Layout and conventions are from the disassembly of 0x004728F0-0x00475920 (docs/script-runtime.md).
namespace objects {
class Entity;           // a game object (SharedBase; +0x100 its component list)
class ObjectModule;     // one of an object's components; can hold a script waiting on it
class SignalRouteTable; // a script's event handlers (engine/objects/signal.h)
struct Signal;          // an event (engine/objects/signal.h)
} // namespace objects

namespace script {

class Script;
namespace qb {
class QbScript;
} // namespace qb

// What Update returns.
enum class ScriptOutcome : uint8_t {
    kFinished = 0,  // no code left (the script has ended or was never set)
    kBlocked = 1,   // waiting with BlockReason::kBlock or kRealTimer
    kSuspended = 2, // any other wait
    // (3 and 4 are never returned by this build)
    kEndedByInterrupt = 5, // an interrupt (Interrupt / OnExitRun script) returned
};

// suspend_reason_: what the script waits for before its next statement (checked by update_wait, 0x00473050).
// A word in the script (a call frame keeps its low 4 bits); 12-15 are never set.
enum class BlockReason : uint8_t {
    kNothing = 0,
    kCount = 1,     // suspend_frames_left_ more Updates (Wait n frames: 0x00472BD0)
    kTimer = 2,     // suspend_clock_ seconds of slomo frame time (Wait n seconds: 0x00472C10)
    kBlock = 3,     // until unblocked - by an event of type suspend_arg_ (Block: 0x00472FD0) or by hand
    kRealTimer = 4, // suspend_clock_ ms of the game clock since suspend_arg_ (0x00473310)
    // 5-10: waiting on blocking_module_, which is asked every Update (its vtable +0x18) - the object waits
    // (move, anim, jump, stop, rotate...; set by set_wait_on_component 0x004739A0).
    kMove = 5,
    kAnimDone = 6,
    kJump = 7,
    kPathStop = 8,
    kRotate = 9,
    kOtherObject = 10, // (nothing here sets it; the component is asked all the same)
    kFrame = 11,       // like COUNTER, but at 0 only one such script per frame goes on (0x006F352C)
};

constexpr size_t kRepeatFrameBytes = 0x10;
constexpr size_t kRepeatFrameArgsSeenOffset = 0x8;
constexpr size_t kRepeatFrameArgsExpectedOffset = 0x9;
constexpr size_t kRepeatFrameHeadcountOffset = 0xC;
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
static_assert(core::layout_32bit(sizeof(RepeatFrame) == kRepeatFrameBytes), "layout must match the original");
static_assert(core::layout_32bit(offsetof(RepeatFrame, tokens_end_) == 0x4 && offsetof(RepeatFrame, args_seen_) == kRepeatFrameArgsSeenOffset &&
                  offsetof(RepeatFrame, args_expected_) == kRepeatFrameArgsExpectedOffset && offsetof(RepeatFrame, headcount_) == kRepeatFrameHeadcountOffset),
              "layout must match the original");

constexpr size_t kCallFrameBytes = 0x30;
constexpr size_t kCallFrameCallerFrameOffset = 0x08;
constexpr size_t kCallFrameOwnerEntityOffset = 0x0C;
constexpr size_t kCallFrameLoopStateOffset = 0x10;
constexpr size_t kCallFrameOnExitScriptPtrOffset = 0x14;
constexpr size_t kCallFrameSleepComponentOffset = 0x18;
constexpr size_t kCallFrameSleepTimerOffset = 0x1C;
constexpr size_t kCallFrameSuspendArgRefOffset = 0x20;
constexpr size_t kCallFrameSuspendFramesRefOffset = 0x24;
constexpr size_t kCallFrameSuspendKindOffset = 0x28;
constexpr size_t kCallFrameTraitMaskOffset = 0x2C;
// A return address (0x30 bytes): the calling level's state, saved by run_sub_script and restored by
// return_from_script. Two are inside the script; the third call moves them to a block of 16 on the script
// heap (no check beyond 16).
struct CallFrame {
    uint32_t script_id_;                     // +0x00 script_title_
    Struct* arg_struct_;                     // +0x04 args_ (the script's: reset_script_state / the return free it)
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
    std::array<uint8_t, 3> unknown2d_;
};
static_assert(core::layout_32bit(sizeof(CallFrame) == kCallFrameBytes), "layout must match the original");
static_assert(core::layout_32bit(offsetof(CallFrame, arg_struct_) == 0x04 && offsetof(CallFrame, caller_frame_) == kCallFrameCallerFrameOffset &&
                  offsetof(CallFrame, owner_entity_) == kCallFrameOwnerEntityOffset && offsetof(CallFrame, loop_state_) == kCallFrameLoopStateOffset &&
                  offsetof(CallFrame, on_exit_script_ptr_) == kCallFrameOnExitScriptPtrOffset && offsetof(CallFrame, sleep_component_) == kCallFrameSleepComponentOffset &&
                  offsetof(CallFrame, sleep_timer_) == kCallFrameSleepTimerOffset && offsetof(CallFrame, suspend_arg_ref_) == kCallFrameSuspendArgRefOffset &&
                  offsetof(CallFrame, suspend_frames_ref_) == kCallFrameSuspendFramesRefOffset && offsetof(CallFrame, suspend_kind_) == kCallFrameSuspendKindOffset &&
                  offsetof(CallFrame, trait_mask_) == kCallFrameTraitMaskOffset),
              "layout must match the original");
constexpr uint32_t kReturnWaitTypeMask = 0xF;
constexpr uint8_t kReturnFlagTimerStarted = 0x01;
constexpr uint8_t kReturnFlagInterrupted = 0x02;

constexpr size_t kParamScriptBytes = 8;
// A script held by a structure component (8 bytes, filled by CStruct's 0x00476900): its name and its code.
// (Group A: set_script_copy's argument.)
struct ParamScript {
    uint32_t own_name_hash_;
    const uint8_t* code_stream_;
};
static_assert(core::layout_32bit(sizeof(ParamScript) == kParamScriptBytes), "allocated as 8 bytes by 0x0048B200");

constexpr size_t kMemberFunctionEntryBytes = 0x18;
// A member function's table entry (the value of a QbValueKind::kNativeMethod symbol, token 0x46), 0x18 bytes: the
// tables at 0x00683550 (Entity's, filled in by 0x00642940) and 0x00683268 (GameEntity's, 0x006425D0).
struct MemberFunctionEntry {
    uint32_t callsign_;
    uint32_t owner_kind_; // the object component that runs it; 0x57525413 / 0x0ADA4E74: the object itself
    // +0x08: the member function (the original's: a 16-byte virtual-inheritance member function pointer; ours: its
    // call_member_function, member_code.h); null: the object looks the name up (Entity::invoke_method_by_name 0x0045D850).
    MemberCode method_code_;
    int this_adjustment_;         // +0x0C
    std::array<int, 2> virtual_base_offsets_; // +0x10 (unused by single-inheritance classes)
};
static_assert(core::layout_32bit(sizeof(MemberFunctionEntry) == kMemberFunctionEntryBytes), "a table entry is 0x18 bytes");

constexpr size_t kScriptCodeCopyOffset = 0x08;
constexpr size_t kScriptPcOffset = 0x0C;
constexpr size_t kScriptStmtArgsOffset = 0x10;
constexpr size_t kScriptArgsOffset = 0x14;
constexpr size_t kScriptInnermostRepeatOffset = 0x18;
constexpr size_t kScriptInlineLoopOffset = 0x1C;
constexpr size_t kScriptLoopsOffset = 0x2C;
constexpr size_t kScriptCallDepthOffset = 0x30;
constexpr size_t kScriptInlineReturnAddressesOffset = 0x34;
constexpr size_t kScriptFrameRecordsOffset = 0x94;
constexpr size_t kScriptSuspendFramesLeftOffset = 0x98;
constexpr size_t kScriptSuspendClockStartedOffset = 0x9C;
constexpr size_t kScriptSuspendClockOffset = 0xA0;
constexpr size_t kScriptSuspendReasonOffset = 0xA4;
constexpr size_t kScriptSuspendArgOffset = 0xA8;
constexpr size_t kScriptBlockingModuleOffset = 0xAC;
constexpr size_t kScriptUniqueIdOffset = 0xB0;
constexpr size_t kScriptFlagBitsOffset = 0xB4;
constexpr size_t kScriptListLinkOffset = 0xB8;
constexpr size_t kScriptEntityOffset = 0xBC;
constexpr size_t kScriptScriptTitleOffset = 0xC8;
constexpr size_t kScriptSpawnFlagsOffset = 0xCC;
constexpr size_t kScriptSpawnerFileOffset = 0xD0;
constexpr size_t kScriptSpawnIdOffset = 0xD4;
constexpr size_t kScriptOnFinishScriptOffset = 0xD8;
constexpr size_t kScriptCallbackParamsOffset = 0xDC;
constexpr size_t kScriptSignalRoutesOffset = 0xE0;
constexpr size_t kScriptOnExceptionScriptOffset = 0xE4;
constexpr size_t kScriptOnExitScriptOffset = 0xE8;
constexpr size_t kScriptSharedBaseOffset = 0xEC;
constexpr size_t kScriptBytes = 0xF4;
class Script {
public:
    // Made and given back only by the script runtime (Host::create / destroy); never destroyed by a delete-expression,
    // never copied or moved.
    ~Script() = delete;
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;
    Script(Script&&) = delete;
    Script& operator=(Script&&) = delete;

    // NOT ORIGINAL - the loop stack and the call stack as runs of what they hold now: the one loop / two frames inside
    // the script, or the blocks on the script heap (kHeapLoops / kHeapFrames; the original didn't check beyond them).
    static constexpr std::size_t kHeapLoops = 10;
    static constexpr std::size_t kHeapFrames = 16;
    [[nodiscard]] std::span<RepeatFrame> loop_stack()
    {
        return loops_ == &inline_loop_ ? std::span<RepeatFrame>(&inline_loop_, 1) : std::span<RepeatFrame>(loops_, kHeapLoops);
    }
    [[nodiscard]] std::span<CallFrame> call_stack()
    {
        return frame_records_ == inline_return_addresses_.data() ? std::span<CallFrame>(inline_return_addresses_)
                                                                 : std::span<CallFrame>(frame_records_, kHeapFrames);
    }
    [[nodiscard]] std::span<const CallFrame> call_stack() const
    {
        return frame_records_ == inline_return_addresses_.data()
                   ? std::span<const CallFrame>(inline_return_addresses_)
                   : std::span<const CallFrame>(frame_records_, kHeapFrames);
    }
    // Where `loop` is on the loop stack.
    [[nodiscard]] std::ptrdiff_t loop_index(const RepeatFrame* loop) const
    {
        return loop - (loops_ == &inline_loop_ ? &inline_loop_ : loops_);
    }

    // The fields other code reads and sets.
    [[nodiscard]] Script* following() const { return following_; }
    [[nodiscard]] const uint8_t* pc() const { return pc_; }
    StructPtr& args() { return args_; }
    [[nodiscard]] const StructPtr& args() const { return args_; }
    [[nodiscard]] int call_depth() const { return call_depth_; }
    [[nodiscard]] BlockReason suspend_reason() const { return suspend_reason_; }
    [[nodiscard]] uint32_t unique_id() const { return unique_id_; }
    [[nodiscard]] int list_link() const { return list_link_; }
    void set_list_link(int value) { list_link_ = value; }
    objects::SharedRef<objects::Entity>& entity() { return entity_; }
    [[nodiscard]] const objects::SharedRef<objects::Entity>& entity() const { return entity_; }
    [[nodiscard]] uint32_t script_title() const { return script_title_; }
    uint8_t& spawn_flags() { return spawn_flags_; }
    [[nodiscard]] uint8_t spawn_flags() const { return spawn_flags_; }
    [[nodiscard]] uint32_t spawner_file() const { return spawner_file_; }
    [[nodiscard]] uint32_t spawn_id() const { return spawn_id_; }
    void set_spawn_id(uint32_t value) { spawn_id_ = value; }
    [[nodiscard]] uint32_t on_finish_script() const { return on_finish_script_; }
    void set_on_finish_script(uint32_t value) { on_finish_script_ = value; }
    StructPtr& callback_params() { return callback_params_; }
    [[nodiscard]] const StructPtr& callback_params() const { return callback_params_; }
    [[nodiscard]] objects::SignalRouteTable* signal_routes() const { return signal_routes_; }
    objects::SharedBase& shared_base() { return shared_base_; }
    [[nodiscard]] const objects::SharedBase& shared_base() const { return shared_base_; }
    // 0x00472940: whether it has code to run.
    [[nodiscard]] bool has_attached_script() const { return pc_ != nullptr; }
    // 0x00472ED0 / 0x00472EE0: the OnExceptionRun script. 0x00472EF0: this level's OnExitRun script.
    void set_on_exception_script(uint32_t name) { on_exception_script_ = name; }
    [[nodiscard]] uint32_t get_on_exception_script() const { return on_exception_script_; }
    void set_on_exit_script(uint32_t name) { on_exit_script_ = name; }

private:
    // The QB interpreter's view of the record: its operations (script_qb/qb_script.h), which add no fields.
    friend class qb::QbScript;

    // --- the fields ---
    Script* following_;             // +0x00 the list (older)
    Script* prior_entry_;           // +0x04 (newer; null for the head)
    uint8_t* code_copy_;            // +0x08 a private copy of the code (set_script_copy), freed by reset_script_state
    const uint8_t* pc_;             // +0x0C the next token; null: not running
    StructPtr stmt_args_;           // +0x10 the current statement's arguments
    StructPtr args_;                // +0x14 the script's parameters (CFunctions leave results here)
    RepeatFrame* innermost_repeat_; // +0x18 the innermost begin ... repeat (null: none)
    RepeatFrame inline_loop_;       // +0x1C
    RepeatFrame* loops_;            // +0x2C inline_loop_, or 10 on the script heap
    int call_depth_;                // +0x30 the call depth
    std::array<CallFrame, 2> inline_return_addresses_; // +0x34
    CallFrame* frame_records_;             // +0x94 inline_return_addresses_, or 16 on the script heap
    int suspend_frames_left_;              // +0x98 kSuspendOnCount / ONE_PER_FRAME
    bool suspend_clock_started_;           // +0x9C kSuspendOnTimer: the first Update has been
    std::array<uint8_t, 3> unknown9d_;
    float suspend_clock_;     // +0xA0 kSuspendOnTimer seconds left / REAL_TIMER ms
    BlockReason suspend_reason_; // +0xA4 (the original's u32: the rest is zero)
    std::array<uint8_t, 3> unknowna5_;
    uint32_t suspend_arg_;    // +0xA8 BLOCKED: the event type; REAL_TIMER: the start; 5-10: for the component
    objects::ObjectModule* blocking_module_; // +0xAC types 5-10
    uint32_t unique_id_;                     // +0xB0 from 0x006F3540
    uint8_t flag_bits_;                      // +0xB4 SCRIPT_RUN_*
    std::array<uint8_t, 3> unknownb5_;
    int list_link_;                              // +0xB8 SpawnScript's node
    objects::SharedRef<objects::Entity> entity_; // +0xBC the object it runs on (+0xC0 / +0xC4 the pointer's links)
    uint32_t script_title_;                      // +0xC8 the script running now (the top level)
    uint8_t spawn_flags_;                        // +0xCC SCRIPT_FLAG_*
    std::array<uint8_t, 3> unknowncd_;
    uint32_t spawner_file_;                    // +0xD0 the .qb file of the script that spawned it (set_spawner_file)
    uint32_t spawn_id_;                        // +0xD4 SpawnScript's id
    uint32_t on_finish_script_;                // +0xD8 run when a spawned script ends
    StructPtr callback_params_;                // +0xDC
    objects::SignalRouteTable* signal_routes_; // +0xE0 made on demand (the script's: teardown frees it, or marks it for its handlers to)
    uint32_t on_exception_script_;             // +0xE4 OnExceptionRun
    uint32_t on_exit_script_;                  // +0xE8 OnExitRun (this level's)
    objects::SharedBase shared_base_;          // +0xEC lets smart pointers point at the script (vtable 0x006478E4)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(Script, prior_entry_) == 0x04 && offsetof(Script, code_copy_) == kScriptCodeCopyOffset &&
                          offsetof(Script, pc_) == kScriptPcOffset && offsetof(Script, stmt_args_) == kScriptStmtArgsOffset &&
                          offsetof(Script, args_) == kScriptArgsOffset && offsetof(Script, innermost_repeat_) == kScriptInnermostRepeatOffset &&
                          offsetof(Script, inline_loop_) == kScriptInlineLoopOffset && offsetof(Script, loops_) == kScriptLoopsOffset &&
                          offsetof(Script, call_depth_) == kScriptCallDepthOffset && offsetof(Script, inline_return_addresses_) == kScriptInlineReturnAddressesOffset &&
                          offsetof(Script, frame_records_) == kScriptFrameRecordsOffset),
                      "layout must match the original");
        static_assert(core::layout_32bit(offsetof(Script, suspend_frames_left_) == kScriptSuspendFramesLeftOffset && offsetof(Script, suspend_clock_started_) == kScriptSuspendClockStartedOffset &&
                          offsetof(Script, suspend_clock_) == kScriptSuspendClockOffset && offsetof(Script, suspend_reason_) == kScriptSuspendReasonOffset &&
                          offsetof(Script, suspend_arg_) == kScriptSuspendArgOffset && offsetof(Script, blocking_module_) == kScriptBlockingModuleOffset &&
                          offsetof(Script, unique_id_) == kScriptUniqueIdOffset && offsetof(Script, flag_bits_) == kScriptFlagBitsOffset &&
                          offsetof(Script, list_link_) == kScriptListLinkOffset && offsetof(Script, entity_) == kScriptEntityOffset),
                      "layout must match the original");
        static_assert(core::layout_32bit(offsetof(Script, script_title_) == kScriptScriptTitleOffset && offsetof(Script, spawn_flags_) == kScriptSpawnFlagsOffset &&
                          offsetof(Script, spawner_file_) == kScriptSpawnerFileOffset && offsetof(Script, spawn_id_) == kScriptSpawnIdOffset &&
                          offsetof(Script, on_finish_script_) == kScriptOnFinishScriptOffset && offsetof(Script, callback_params_) == kScriptCallbackParamsOffset &&
                          offsetof(Script, signal_routes_) == kScriptSignalRoutesOffset && offsetof(Script, on_exception_script_) == kScriptOnExceptionScriptOffset &&
                          offsetof(Script, on_exit_script_) == kScriptOnExitScriptOffset && offsetof(Script, shared_base_) == kScriptSharedBaseOffset),
                      "layout must match the original");
    }
};
static_assert(core::layout_32bit(sizeof(Script) == kScriptBytes), "allocated as 0xF4 bytes by the original");

// flag_bits_ (+0xB4)
constexpr uint8_t kScriptRunInterrupted = 0x01; // the top level was entered as an interrupt
constexpr uint8_t kScriptRunWaitUpdated = 0x02; // the wait was already counted this Update (set on return)
// spawn_flags_ (+0xCC)
constexpr uint8_t kScriptFlagSpawned = 0x01;
constexpr uint8_t kScriptFlagNotSessionSpecific = 0x02; // survives kill_all_child_scripts
constexpr uint8_t kScriptFlagPaused = 0x04;
constexpr uint8_t kScriptFlagPauseWithObject = 0x08; // not updated while its object says no (vtable +0x1C)
constexpr uint8_t kScriptFlagTrackerBlocked = 0x10;  // Block registered it with the tracker

// --- the script functions the game calls by name (host.cpp: each the installed runtime's - host.h) ---
// 0x004734B0: the script with this unique id, or null.
Script* find_script_with_unique_id(uint32_t id);

// 0x00475920: one Update of every spawned script (newest first); finished ones run their callback and go.
void tick_child_scripts();
// 0x004753C0: delete the spawned scripts that aren't kScriptFlagNotSessionSpecific (deferred to the end of
// tick_child_scripts if called during it).
void kill_all_child_scripts();
// How a script is spawned: the script run when it ends and that one's parameters (merged into a copy), its node (the
// script's list link), its spawn id, whether it is sent to the other players (with `permanent`), whether it survives
// the session's end and pauses with its object. The defaults: none of these.
struct SpawnSetup {
    uint32_t callback = 0;
    Struct* callback_params_ = nullptr;
    int node = 0;
    uint32_t id = 0;
    bool replicate = false;
    bool permanent = false;
    bool not_session_specific_ = false;
    bool pause_with_object_ = false;
};
// 0x00475420 / 0x00475670: spawn `name` (a C function is just called, with a temporary script; returns null).
THUG2_API Script* spawn_script(uint32_t name, Struct* params, const SpawnSetup& setup = {});
Script* spawn_script(const char* name, Struct* params, const SpawnSetup& setup = {});
// 0x004756C0: delete `script` if it's spawned.
void kill_spawned_script(Script* script);
// 0x00475790 / 0x004758F0: run `name` to the end now (a C function / member function is just called); `name`
// (the string) isn't used. With `replicate_flag` in a network game, the other players are told (0x00473350).
THUG2_API void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                   const char* name_string);
THUG2_API void launch_script(const char* name, Struct* params, objects::Entity* object, bool replicate_flag);
// 0x004752F0: one Update of `name`, then its parameters are added to `results` (not checked for null).
void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results);
// 0x00473420: the network message for SpawnScript (0x49).
void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent);
// 0x00472D80 / 0x00472DC0: pause or unpause every spawned script / unpause one.
void pause_spawned_scripts(bool pause);
void resume_child_script(Script* script);
// 0x00472DE0: whether `name` is a script. 0x00472E00: `script`'s spawn id (0 if not spawned).
THUG2_API bool script_exists(uint32_t name);
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
void end_scripts_bound_to_entity(objects::Entity* object);              // 0x004756F0 (every script using it)
void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name); // 0x00474350 (name at any level)
// 0x00475700: delete the scripts that aren't spawned, have no code and no event handler table (Cleanup).
void delete_finished_scripts();
} // namespace script
