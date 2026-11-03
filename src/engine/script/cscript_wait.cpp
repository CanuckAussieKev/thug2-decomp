// CScript, group C: waiting, the call stack, interrupts and event handlers (0x004728F0-0x00475784).
//
// How these schedule a script (the spec for a later script runtime) is in docs/script-runtime.md; in short:
// a wait is set by a statement and counted by update_wait once per Update (the first time in the same Update);
// run_sub_script pushes the whole state of the running level and return_from_script pops it; an interrupt is a call
// run at once by a nested Update, marked so that its return ends that Update.

#include <cstdint>
#include <span>

#include "engine/script/cscript.h"
#include "engine/core/enum_ops.h"

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/entity.h"
#include "engine/objects/signal.h"
#include "engine/objects/tracker.h"
#include "engine/script/cache.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/script/tokens.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace script {

namespace {
// Frame records a script's first call stack holds.
constexpr int kCallFrameCount = 16;

// The game clock in ms (0x0044ECE0) and the offset added to it (0x00696D60).
int game_clock() { return frame_clock::game_clock_elapsed(); }


// Wait n seconds: the milliseconds as seconds (0.001f, 0x3A83126F at 0x006461D8).
constexpr float kSecondsPerMs = 0.001F;

// The checksum of `exception` (an event handler table element that is an exception handler).
constexpr uint32_t kExceptionFlag = 0x80367192;

using objects::SignalRouteTable;

// A new event handler table (on the current heap).
gsl::owner<SignalRouteTable*> new_event_handler_table()
{
    void* memory = memory::alloc(sizeof(SignalRouteTable));
    return memory ? memory::construct_in<SignalRouteTable>(memory) : nullptr;
}

// A new, empty structure.
gsl::owner<Struct*> new_empty_struct() { return new_struct(); }

} // namespace

// 0x00473050
void Script::update_wait()
{
    switch (suspend_reason_) {
    case BlockReason::kCount:
        if (suspend_frames_left_)
            --suspend_frames_left_;
        else
            suspend_reason_ = BlockReason::kNothing;
        break;
    case BlockReason::kFrame:
        if (suspend_frames_left_) {
            --suspend_frames_left_;
        } else if (!one_per_frame_released()) {
            // The first such script to finish counting this frame goes on; the others wait (at 0) for the next.
            one_per_frame_released() = true;
            suspend_reason_ = BlockReason::kNothing;
        }
        break;
    case BlockReason::kTimer: {
        const double frame =
            frame_clock::get_slomo_frame_seconds(); // (unrounded, as the original keeps it on the x87 stack)
        if (frame > suspend_clock_) {
            // Less than a frame left: done - unless this is the first Update, which only zeroes the time.
            if (suspend_clock_started_)
                suspend_reason_ = BlockReason::kNothing;
            else
                suspend_clock_ = 0.0F;
        } else {
            // (also when either is a NaN)
            suspend_clock_ = static_cast<float>(suspend_clock_ - frame);
        }
        suspend_clock_started_ = true;
        break;
    }
    case BlockReason::kRealTimer: {
        const uint32_t start = suspend_arg_;
        const uint32_t elapsed =
            static_cast<uint32_t>(game_clock()) + (static_cast<uint32_t>(objects::clock_offset()) - start);
        if (static_cast<double>(elapsed) >= suspend_clock_) // (not when the time is a NaN)
            suspend_reason_ = BlockReason::kNothing;
        break;
    }
    case BlockReason::kMove:
    case BlockReason::kAnimDone:
    case BlockReason::kJump:
    case BlockReason::kPathStop:
    case BlockReason::kRotate:
    case BlockReason::kOtherObject: {
        // The component decides (and stops the wait itself): its step_wait_state (vtable +0x18).
        blocking_module_->step_wait_state(this, suspend_arg_);
        break;
    }
    case BlockReason::kBlock: // until something unblocks it
    case BlockReason::kNothing:
        break;
    }
}

// 0x004728F0
void Script::stop_waiting() { suspend_reason_ = BlockReason::kNothing; }

// 0x00472900
bool Script::is_blocked(bool print)
{
    if (print)
        debug::printf("m_wait_type == %i\n" /* "m_wait_type == %i\n" */, core::to_underlying(suspend_reason_));
    return suspend_reason_ == BlockReason::kBlock;
}

// 0x00472BD0
void Script::get_stall(int frames)
{
    suspend_reason_ = BlockReason::kCount;
    suspend_frames_left_ = frames;
}

// 0x00472BF0
void Script::suspend_until_next_frame(int frames)
{
    suspend_reason_ = BlockReason::kFrame;
    suspend_frames_left_ = frames;
}

// 0x00472C10 (the milliseconds unsigned)
void Script::lull(int ms)
{
    suspend_reason_ = BlockReason::kTimer;
    suspend_clock_started_ = false;
    suspend_clock_ = static_cast<float>(static_cast<double>(static_cast<uint32_t>(ms)) * kSecondsPerMs);
}

// 0x00473310 (the milliseconds unsigned)
void Script::wait_real_time(int ms)
{
    suspend_reason_ = BlockReason::kRealTimer;
    suspend_arg_ = static_cast<uint32_t>(game_clock() + objects::clock_offset());
    suspend_clock_ = static_cast<float>(static_cast<uint32_t>(ms));
}

// 0x00472FD0
void Script::gate_off(uint32_t event_type)
{
    suspend_reason_ = BlockReason::kBlock;
    suspend_arg_ = event_type;
    if (event_type) {
        objects::get_tracker()->add_listener(event_type, this);
        spawn_flags_ |= kScriptFlagTrackerBlocked;
    }
}

// 0x004739A0
void Script::set_wait_on_component(BlockReason type, objects::ObjectModule* component, uint32_t arg)
{
    suspend_reason_ = type;
    blocking_module_ = component;
    suspend_arg_ = arg;
    // (the component's flags: a script waits on it)
    component->flag_bits() |= 2;
}

// 0x00472D20
void Script::component_destroyed(objects::ObjectModule* component)
{
    if (blocking_module_ && blocking_module_ == component)
        suspend_reason_ = BlockReason::kNothing;
    // The levels it's in the middle of: the saved wait type only (the component pointer stays).
    for (int i = 0; i < call_depth_; ++i) {
        CallFrame& frame = core::at(call_stack(), i);
        if (frame.sleep_component_ && frame.sleep_component_ == component)
            frame.suspend_kind_ &= ~kReturnWaitTypeMask;
    }
}

// 0x00472E70
void component_destroyed_all_scripts(objects::ObjectModule* component)
{
    for (Script* script = script_list_head(); script; script = script->following())
        script->component_destroyed(component);
}

// 0x00473800
bool Script::receive_event(objects::Signal* event)
{
    if (suspend_reason_ == BlockReason::kBlock && suspend_arg_ && event->kind_tag() == suspend_arg_)
        suspend_reason_ = BlockReason::kNothing;
    // Levels blocked on it further down go on when they're returned to.
    for (int i = 0; i < call_depth_; ++i) {
        CallFrame& frame = core::at(call_stack(), i);
        if (static_cast<BlockReason>(frame.suspend_kind_ & kReturnWaitTypeMask) == BlockReason::kBlock && frame.suspend_arg_ref_ &&
            event->kind_tag() == frame.suspend_arg_ref_)
            frame.suspend_kind_ &= ~kReturnWaitTypeMask;
    }
    if (signal_routes_)
        signal_routes_->forward_trigger(event, this);
    return true;
}

// 0x00472F40: from the top down, the first level entered as an interrupt (the top's flag is flag_bits_ bit 0,
// a lower level's is its saved frame's bit 1): the level below it - the one it interrupted - stops waiting.
void Script::unblock_interrupted_script()
{
    bool interrupt = (flag_bits_ & kScriptRunInterrupted) != 0;
    for (int level = call_depth_; level > 0; --level) {
        CallFrame& below = core::at(call_stack(), level - 1);
        if (interrupt) {
            below.suspend_kind_ &= ~kReturnWaitTypeMask;
            return;
        }
        interrupt = (below.trait_mask_ & kReturnFlagInterrupted) != 0;
    }
}

// 0x004748E0
void Script::run_sub_script(uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object,
                            bool interrupted)
{
    if (!pc_) {
        // Nothing running: just start it.
        bind_script(name, code, params, object);
        flag_bits_ =
            static_cast<uint8_t>((flag_bits_ & ~kScriptRunInterrupted) | (interrupted ? kScriptRunInterrupted : 0));
        return;
    }
    // The third level: the call stack moves from inside the script to 16 on the script heap (no check beyond).
    if (frame_records_ == inline_return_addresses_.data() && call_depth_ == 2) {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        frame_records_ = static_cast<CallFrame*>(memory::alloc_block(kCallFrameCount * sizeof(CallFrame)));
        memory::leave_heap_scope();
        const std::span<CallFrame> moved = call_stack();
        core::at(moved, 0) = core::at(inline_return_addresses_, 0);
        core::at(moved, 1) = core::at(inline_return_addresses_, 1);
    }
    // The caller's state (the frame's other wait-type and flag bits are left as they were).
    CallFrame& frame = core::at(call_stack(), call_depth_++);
    frame.script_id_ = script_title_;
    frame.arg_struct_ = args_.release(); // (the frame keeps them: the return gives them back)
    frame.caller_frame_ = pc_;
    frame.owner_entity_ = entity_.get_pull();
    frame.loop_state_ = innermost_repeat_;
    frame.on_exit_script_ptr_ = on_exit_script_;
    frame.suspend_kind_ = (frame.suspend_kind_ & ~kReturnWaitTypeMask) | (core::to_underlying(suspend_reason_) & kReturnWaitTypeMask);
    frame.sleep_component_ = blocking_module_;
    frame.suspend_arg_ref_ = suspend_arg_;
    frame.suspend_frames_ref_ = suspend_frames_left_;
    frame.sleep_timer_ = suspend_clock_;
    frame.trait_mask_ = static_cast<uint8_t>((frame.trait_mask_ & ~(kReturnFlagTimerStarted | kReturnFlagInterrupted)) |
                                             (suspend_clock_started_ ? kReturnFlagTimerStarted : 0) |
                                             ((flag_bits_ & kScriptRunInterrupted) ? kReturnFlagInterrupted : 0));
    // The callee: its default parameters (its first line), then the call's. (With no code the original reads
    // through null - not checked.) The loop pointer is kept: the callee's loops go above the caller's.
    script_title_ = name;
    pc_ = code;
    args_ = memory::take_unique<Struct, StructFree>(new_empty_struct());
    pc_ = parse_arguments(args_.get(), tokens_at(pc_), nullptr).address();
    if (params)
        args_->merge_structure(params);
    entity_.assign_value(object);
    flag_bits_ =
        static_cast<uint8_t>((flag_bits_ & ~kScriptRunInterrupted) | (interrupted ? kScriptRunInterrupted : 0));
    suspend_reason_ = BlockReason::kNothing;
    on_exit_script_ = 0;
}

// 0x00473E70
bool Script::return_from_script(Struct* results)
{
    script_store_instance()->release_script(script_title_);
    const bool interrupted = (flag_bits_ & kScriptRunInterrupted) != 0;
    if (call_depth_ == 0) {
        // The bottom level: the script ends (its parameters stay, for the results).
        pc_ = nullptr;
    } else {
        --call_depth_;
        if (args_)
            memory::destroy_then_clear(args_); // (cleared and given back to the pool, as free_struct)
        const CallFrame& frame = core::at(call_stack(), call_depth_);
        script_title_ = frame.script_id_;
        args_ = memory::take_unique<Struct, StructFree>(memory::owned_element(frame.arg_struct_));
        pc_ = frame.caller_frame_;
        entity_.assign_value(frame.owner_entity_);
        innermost_repeat_ = frame.loop_state_;
        on_exit_script_ = frame.on_exit_script_ptr_;
        suspend_reason_ = static_cast<BlockReason>(frame.suspend_kind_ & kReturnWaitTypeMask);
        // The rest of the wait only if it was waiting (otherwise the callee's values stay).
        if (suspend_reason_ != BlockReason::kNothing) {
            suspend_frames_left_ = frame.suspend_frames_ref_;
            suspend_clock_ = frame.sleep_timer_;
            suspend_clock_started_ = (frame.trait_mask_ & kReturnFlagTimerStarted) != 0;
            blocking_module_ = frame.sleep_component_;
            suspend_arg_ = frame.suspend_arg_ref_;
        }
        flag_bits_ = static_cast<uint8_t>((flag_bits_ & ~kScriptRunInterrupted) |
                                          ((frame.trait_mask_ & kReturnFlagInterrupted) ? kScriptRunInterrupted : 0));
    }
    if (results)
        args_->append_structure_if_any(results);
    return interrupted;
}

// 0x00475750
ScriptOutcome Script::preempt(uint32_t name, Struct* params)
{
    const uint8_t* code = script_store_instance()->fetch_script(name);
    run_sub_script(name, code, params, entity_.get_pull(), true);
    return tick_frame();
}

// 0x00472EA0
void Script::clear_event_handler_group(uint32_t group)
{
    if (signal_routes_)
        signal_routes_->remove_group(this, group);
}

// 0x00472EC0
int Script::print_event_handler_table() { return SignalRouteTable::list_wait_table(signal_routes_); }

// 0x004734E0
void Script::install_event_table(Array* handlers, bool replace)
{
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    if (!signal_routes_) {
        gsl::owner<objects::SignalRouteTable*> made = new_event_handler_table();
        signal_routes_ = made;
    } else {
        signal_routes_->unregister_with_tracker(this);
    }
    signal_routes_->add_handlers(handlers, replace);
    signal_routes_->compact();
    signal_routes_->enter_wait_board(this);
    memory::leave_heap_scope();
}

// 0x004735D0
void Script::set_event_handler(uint32_t event_type, uint32_t script, uint32_t group, bool exception, Struct* params)
{
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    if (!signal_routes_) {
        gsl::owner<objects::SignalRouteTable*> made = new_event_handler_table();
        signal_routes_ = made;
    }
    signal_routes_->append_listener(this, {.type = event_type, .script_name_ = script, .group = group}, exception, params);
    memory::leave_heap_scope();
}

// 0x00473690
void Script::clear_event_handler(uint32_t event_type)
{
    if (signal_routes_) {
        signal_routes_->remove_type(event_type);
        objects::get_tracker()->remove_listener(event_type, this);
    }
}

// 0x004736C0: the group's handlers become those of `table`, an array of structures {flag, type, script,
// [params]} - the flag `exception` makes it an exception handler. (At least three components each: not checked.)
void Script::reset_event_handlers_from_table(Array* table, uint32_t group)
{
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    if (signal_routes_)
        signal_routes_->mark_group(group);
    const int count = static_cast<int>(table->extent());
    if (!signal_routes_ && count) {
        gsl::owner<objects::SignalRouteTable*> made = new_event_handler_table();
        signal_routes_ = made;
    }
    for (int i = 0; i < count; ++i) {
        const Component* flag = table->struct_at(static_cast<uint32_t>(i))->step_to_entry(nullptr);
        const Component* type = flag->next;
        const Component* script = type->next;
        const Component* params = script->next;
        signal_routes_->append_listener(this, {.type = type->value.word(), .script_name_ = script->value.word(), .group = group}, flag->value.word() == kExceptionFlag,
                                        params ? params->value.structure() : nullptr);
    }
    // (the group's handlers not added again go)
    if (signal_routes_)
        signal_routes_->remove_marked(this);
    memory::leave_heap_scope();
}

} // namespace script
