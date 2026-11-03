// The script interpreter: CScript::tick_frame and the statements it runs (group B of the CScript batch; see
// cscript.h for the class and docs/script-runtime.md for the scheduling notes).
//
// How a script runs (the spec for a later port):
//
// Update (0x004750B0) runs tokens from pc_ until the script waits or ends:
//   - It first clears kScriptRunWaitUpdated, then loops while pc_ is set. Each pass: the script becomes the
//     current script (0x006F3530); unless kScriptRunWaitUpdated is set, update_wait counts the wait down /
//     checks it; if a wait is still on, Update sets kScriptRunWaitUpdated, CLEARS kScriptRunInterrupted,
//     clears the current script and returns BLOCKED (wait types 3 and 4) or WAITING (any other type).
//     Otherwise it runs ONE token or statement (below) and loops.
//   - kScriptRunWaitUpdated is only ever set by an Update that is returning because of a wait. Inside the loop
//     it is clear - except after a statement that ran a nested Update on the same script (an interrupt: the
//     '$' / return OnExitRun case, or a C function calling Interrupt) which ended waiting: the outer Update
//     then doesn't count the same wait a second time, sees it and returns too.
//   - A wait a statement sets is therefore checked at once, in the same Update: Wait n (frames) makes this
//     Update return WAITING with n-1 left; Wait 0 doesn't wait.
//   - pc_ == 0 (no code, or the script ended): FINISHED, with kScriptRunInterrupted cleared and no current
//     script.
//   - '$' / return of a level entered as an interrupt (return_from_script says so): FINISHED_INTERRUPT (5) at
//     once - the current script is left set and kScriptRunInterrupted isn't cleared.
//
// The tokens Update handles itself (all others start a statement, run by execute_command):
//   01 end of line, 28, 3D endswitch: skipped (one byte)      02 end of line with line number: 5 bytes
//   20 begin, 21 repeat, 22 break: run_loop_open / run_counted_loop / run_loop_exit
//   24 '$' end of script, 29 return [args]: this level's OnExitRun script (if any) is cleared and run as an
//      interrupt first (a nested Update), then return_from_script ('$': no results; return: its arguments)
//   2E jump, 2F/37/40/41 random blocks, 49 short jump: resolve_random_jumps
//   3C switch: run_multi_branch      47 if: run_conditional      48 else (reached at the end of an if's true part):
//      jump by its 2-byte offset (to the endif)
//   25, 26, 3E case, 3F default: nothing - pc_ doesn't move and Update loops for ever (these never start a
//      statement in compiled code; kept as in the original)
//
// A statement (execute_command) is a call or an assignment; its result is only used by `if`. A script call
// (run_sub_script) pushes the caller's state and runs the callee in the same Update loop, from its first token after
// the default-parameters line (the call counts as true). `obj:Name args` runs Name on the object called obj.

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cache.h"
#include "engine/script/cell.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/params.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/loading_console.h"
#include "platform/run_log.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include <gsl/pointers>

namespace script {

namespace {

// The script whose missing calls aren't reported (presumably the default level's script).
constexpr uint32_t kQuietScriptName = 0xFB11E6CD;
// Member function entries for these component types are run by the object itself.
constexpr uint32_t kObjectComponentType = 0x57525413;
constexpr uint32_t kObjectComponentType2 = 0x0ADA4E74;
// A short jump, the Case it leads to and the case's own short jump.
constexpr int kShortJumpCaseBytes = 7;
// The loop frames a script's first heap block holds (not checked beyond).
constexpr size_t kLoopFrameCount = 10;
// Steps in one Update after which a script that never yields is logged; scripts logged at most this many.
constexpr uint32_t kRunawayStepCount = 5000000u;
constexpr int kLoggedScriptCount = 16;

// The object's and its components' script member functions. The object's: Entity::invoke_method (the original's
// slot 2) and Entity::invoke_method_by_name (slot 3).
bool object_call_member_function(objects::Entity* object, const MemberFunctionEntry* entry, Struct* params, Script* script)
{
    return object->invoke_method(entry, params, script);
}
bool object_call_member_function(objects::Entity* object, uint32_t name, Struct* params, Script* script)
{
    return object->invoke_method_by_name(name, params, script);
}
// ObjectModule::invoke_method (vtable +0x2C; the original tests the whole of EAX, not just AL).
bool component_call_member_function(objects::ObjectModule* component, const MemberFunctionEntry* entry, Struct* params,
                                    Script* script)
{
    return component->invoke_method(entry, params, script) != 0;
}
// The object's component of `type` (GameEntity::fetch_module).
objects::ObjectModule* fetch_module(objects::Entity* object, uint32_t type)
{
    return core::down_cast<objects::GameEntity>(object)->fetch_module(type);
}
// The object named `name` (GameObjects::find_object: the object tracker's), or null.
objects::Entity* find_object(uint32_t name) { return objects::find_object(name); }
// 0x0044E4F0 on online::Hub (0x007CCE14): called where a member function has no object to run on, its
// result unused - what is left of a compiled-out assert.
void no_object_check() { online::net_game_state().online_hub()->in_net_game(); }

// A component made and freed around one expression / switch value.
void delete_component(Component* component)
{
    release_component(component);
    free_component(component);
}

} // namespace

// 0x00472980: run a member function's table entry on `object` (no null checks).
bool invoke_method_by_name(const MemberFunctionEntry* entry, Struct* params, objects::Entity* object, Script* script)
{
    const uint32_t type = entry->owner_kind_;
    if (type == kObjectComponentType || type == kObjectComponentType2)
        return object_call_member_function(object, entry, params, script);
    return component_call_member_function(fetch_module(object, type), entry, params, script);
}

// 0x004731A0
bool Script::invoke_method_by_name(const MemberFunctionEntry* entry, objects::Entity* object)
{
    if (!object)
        object = entity_.get_pull();
    if (!object) {
        no_object_check();
        return false;
    }
    return script::invoke_method_by_name(entry, stmt_args_.get(), object, this);
}

// 0x004731E0
bool Script::invoke_method_by_name(uint32_t name, objects::Entity* object)
{
    if (!object)
        object = entity_.get_pull();
    if (!object) {
        no_object_check();
        return false;
    }
    return object_call_member_function(object, name, stmt_args_.get(), this);
}

// 0x00472950 (pc_ is kept at the statement as it is read: a C function inside an expression may look at the script)
void Script::parse_function_params(Tokens& pc)
{
    pc_ = pc.address();
    stmt_args_->get_clear();
    pc = parse_arguments(stmt_args_.get(), pc, args_.get());
    pc_ = pc.address();
}

// 0x004729E0: a name token, `<name>` (the parameter's checksum value) or `<...>` (the unnamed checksum
// parameter); anything else is 0 and isn't moved past.
uint32_t Script::read_name(Tokens& pc)
{
    uint32_t name = 0;
    switch (*pc) {
    case kTokenName:
        ++pc;
        name = fetch_word32(pc);
        pc += 4;
        break;
    case kTokenAllArguments:
        ++pc;
        args_->read_hash_value(0u, &name, 0);
        break;
    case kTokenLocalArgument: // 2D 16 <name>
        pc += 2;
        {
            const uint32_t parameter = fetch_word32(pc);
            pc += 4;
            args_->read_hash_value(parameter, &name, 0);
        }
        break;
    default:
        break;
    }
    return name;
}

// 0x00474B40: one statement, at pc_:
//   44 <name> args          a member function (without a table entry) on the script's object
//   46 <entry> args         a member function's table entry on the script's object
//   43 <function> args      a C function
//   ( expression )          a name result is the name to run (0: the name after it); any other result is the
//                           statement's value (non-zero: true) and nothing is run
//   name / <name> / <...>   then:
//     name : ...            on the object called `name`: 44 <name>, or a name (a script, or a member function -
//                           any other symbol does nothing). No such object: true, and pc_ isn't moved - the
//                           next pass runs the ':' as a statement, looking for the object named 0.
//     name = value          sets the script parameter `name` (the 4 bytes before the '=' are read as the name)
//     name args             a script (called: true), a C function, or a member function on the script's object;
//                           an unknown name is reported (except in the script 0xFB11E6CD) and is false
// The arguments are parsed into stmt_args_ (they may refer to args_). pc_ is where the statement ends before anything
// is called (what is called may move it: a script called, a Goto...).
bool Script::execute_command(Tokens& pc)
{
    switch (*pc) {
    case kTokenMemberFunctionName: {
        ++pc;
        const uint32_t name = fetch_word32(pc);
        pc += 4;
        parse_function_params(pc);
        if (!entity_.get_pull()) {
            no_object_check();
            return false;
        }
        return object_call_member_function(entity_.get_pull(), name, stmt_args_.get(), this);
    }
    case kTokenMemberFunction: {
        ++pc;
        const MemberFunctionEntry* entry = member_entry_at(pc);
        pc += 4;
        parse_function_params(pc);
        if (!entity_.get_pull()) {
            no_object_check();
            return false;
        }
        return script::invoke_method_by_name(entry, stmt_args_.get(), entity_.get_pull(), this);
    }
    case kTokenNativeFunction: {
        ++pc;
        const NativeFunction function = native_function_at(pc);
        pc += 4;
        parse_function_params(pc);
        return function(stmt_args_.get(), this);
    }
    default:
        break;
    }

    uint32_t name = 0;
    if (*pc == kTokenOpenParenthesis) {
        gsl::owner<Component*> value = new_component();
        pc_ = pc.address();
        pc = parse_expression(pc, args_.get(), value);
        pc_ = pc.address();
        if (value_kind(*value) != kQbvalName) {
            const bool result = value->value.word() != 0;
            delete_component(value);
            return result;
        }
        name = value->value.word();
        delete_component(value);
        if (!name)
            name = read_name(pc);
    } else {
        name = read_name(pc);
    }
    pc_ = pc.address();

    if (*pc == kTokenColon) { // name:
        objects::Entity* object = find_object(name);
        if (!object)
            return true; // (pc_ left at the ':')
        ++pc;
        pc = resolve_random_jumps(pc);
        if (*pc == kTokenMemberFunctionName) {
            ++pc;
            const uint32_t member = fetch_word32(pc);
            pc += 4;
            parse_function_params(pc);
            return invoke_method_by_name(member, object);
        }
        const uint32_t function = read_name(pc);
        parse_function_params(pc);
        const SymbolTableEntry* symbol = resolve_symbol(function);
        if (!symbol && script_title_ == kQuietScriptName)
            return false;
        // (anywhere else a missing name's type is read through the null symbol: the original crashes - kept)
        switch (symbol->type()) {
        case kQbvalScriptBody: {
            const uint8_t* code = script_store_instance()->fetch_script(symbol->name_value());
            run_sub_script(function, code, stmt_args_.get(), object, false);
            return true;
        }
        case kQbvalNativeMethod:
            if (auto* entry = symbol->value().method())
                return invoke_method_by_name(entry, object);
            return invoke_method_by_name(function, object);
        default: // (C functions included: not run on an object)
            return false;
        }
    }

    if (*pc == kTokenEquals) { // name = value
        pc -= 4;
        const uint32_t target = fetch_word32(pc);
        pc += kTokenWithWordBytes;
        pc = resolve_random_jumps(pc);
        gsl::owner<Component*> value = new_component();
        pc_ = pc.address();
        if (*pc == kTokenOpenParenthesis)
            pc = parse_expression(pc, args_.get(), value);
        else
            pc = parse_value(pc, args_.get(), value);
        pc_ = pc.address();
        if (value_kind(*value) != kQbvalNone) {
            value->name = target;
            args_->attach_module(value);
        } else {
            free_component(value);
        }
        return true;
    }

    parse_function_params(pc);
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (!symbol) {
        if (script_title_ != kQuietScriptName)
            debug::printf("WARNING: script %s not found, ignoring in default level.\n",
                          find_checksum_name_or_unknown(name));
        return false;
    }
    // Not original - the loading console (engine/gfx/loading_console.h): while a loading screen is up, the scripts
    // called and the commands that load or build things.
    if (gfx::loading_console::live_now() && (symbol->type() == kQbvalScriptBody || symbol->type() == kQbvalNativeFunc))
        gfx::loading_console::note_script_call(name, symbol->type() == kQbvalNativeFunc, stmt_args_.get());
    switch (symbol->type()) {
    case kQbvalScriptBody: {
        const uint8_t* code = script_store_instance()->fetch_script(symbol->name_value());
        run_sub_script(name, code, stmt_args_.get(), entity_.get_pull(), false);
        return true;
    }
    case kQbvalNativeFunc:
        return symbol->value().function()(stmt_args_.get(), this);
    case kQbvalNativeMethod: {
        auto* entry = symbol->value().method();
        objects::Entity* object = entity_.get_pull();
        if (entry) {
            if (object)
                return script::invoke_method_by_name(entry, stmt_args_.get(), object, this);
        } else if (object) {
            return object_call_member_function(object, name, stmt_args_.get(), this);
        }
        no_object_check();
        return false;
    }
    default:
        return false;
    }
}

// 0x00475050: 47 <offset to the else / endif> [39 not] condition. The condition is a statement (a script call
// in it runs first, and counts as true). If the script is still running and the (negated) result is false, it
// jumps; else it goes on into the true part. (pc_ is left where the statement put it.)
void Script::run_conditional(Tokens& pc)
{
    ++pc;
    const Tokens target = pc + fetch_word16(pc);
    pc += 2;
    bool negate = false;
    if (*pc == kTokenNot) {
        ++pc;
        negate = true;
    }
    bool condition = execute_command(pc);
    if (!pc_)
        return;
    if (negate)
        condition = !condition;
    if (!condition) {
        pc = target;
        pc_ = target.address();
    }
}

// 0x00472A70: pc just after a 3C switch: past its 3D endswitch (switches inside skipped whole).
void Script::skip_to_end_of_switch(Tokens& pc)
{
    while (*pc != kTokenEndSwitch) {
        if (*pc == kTokenSwitch) {
            ++pc;
            skip_to_end_of_switch(pc);
        } else {
            pc = step_over_token(pc);
        }
    }
    ++pc;
}

// 0x00473C60: 3C value ... then cases `3E 49 <offset to the next case> value ...`, `3F 49 <offset>` default
// and 3D endswitch. The switch's value is the first component of its line; a case matches if its line's first
// component equals it (components_equal). On a match, the case labels right after it (`49 xx xx 3E 49 xx xx
// value`) are skipped and the case's statements run from there; the 49 jump at the end of a case's statements
// goes to the endswitch. No match: on to the next case, finally into the default or past the endswitch.
void Script::run_multi_branch(Tokens& pc)
{
    ++pc;
    parse_function_params(pc);
    gsl::owner<Component*> value = new_component();
    if (const Component* first = stmt_args_->step_to_entry(nullptr))
        duplicate_component(value, first, 0);
    for (;;) {
        const uint8_t token = *pc;
        if (token == kTokenScriptEnd) // '$': not moved past - the original loops for ever here (kept)
            continue;
        if (token == kTokenSwitch) { // a switch in a case not taken
            ++pc;
            skip_to_end_of_switch(pc);
            continue;
        }
        if (token == kTokenEndSwitch) {
            ++pc;
            break;
        }
        if (token == kTokenDefault) { // default: past 3F 49 xx xx
            pc += 4;
            break;
        }
        if (token != kTokenCase) {
            pc = step_over_token(pc);
            continue;
        }
        pc += 2; // the jump's offset
        const Tokens next = pc + fetch_word16(pc);
        pc += 2;
        parse_function_params(pc);
        Component* match = stmt_args_->step_to_entry(nullptr);
        if (!match || !components_equal(value, match)) {
            pc = next;
            continue;
        }
        pc = skip_newline_tokens(pc);
        while (*pc == kTokenShortJump && pc[3] == kTokenCase) {
            pc += kShortJumpCaseBytes;
            parse_function_params(pc);
            pc = skip_newline_tokens(pc);
        }
        break;
    }
    delete_component(value);
}

// 0x00473220: 20 begin. The next loop of the loop stack starts after the token; the first nested loop (the
// inline loop in use) moves the stack to a block of 10 on the script heap - also when the inline loop belongs
// to a calling level (run_sub_script keeps innermost_repeat_: one stack for every level), whose saved loop pointers are
// moved with it. Not checked beyond 10.
void Script::run_loop_open(Tokens& pc)
{
    ++pc;
    if (innermost_repeat_ == &inline_loop_) {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->script_heap());
        loops_ = static_cast<RepeatFrame*>(memory::alloc_block(kLoopFrameCount * sizeof(RepeatFrame)));
        memory::Hub::get_pull()->leave_heap_scope();
        const std::span<RepeatFrame> moved = loop_stack();
        core::at(moved, 0) = inline_loop_;
        innermost_repeat_ = &core::at(moved, 0);
        for (int i = 0; i < call_depth_; ++i) {
            CallFrame& frame = core::at(call_stack(), i);
            if (const RepeatFrame* loop = frame.loop_state_)
                frame.loop_state_ = &core::at(moved, loop - &inline_loop_);
        }
    }
    innermost_repeat_ = &core::at(loop_stack(), innermost_repeat_ ? loop_index(innermost_repeat_) + 1 : 0);
    innermost_repeat_->tokens_begin_ = pc.address();
    innermost_repeat_->tokens_end_ = nullptr;
    innermost_repeat_->args_seen_ = false;
    innermost_repeat_->args_expected_ = true;
    innermost_repeat_->headcount_ = 0;
}

namespace {
// The loop below the current one (none: null).
RepeatFrame* outer_loop(Script& script, const RepeatFrame* loop)
{
    const std::ptrdiff_t index = script.loop_index(loop);
    return index == 0 ? nullptr : &core::at(script.loop_stack(), index - 1);
}
} // namespace

// 0x00472AB0: 21 repeat [count]. Its arguments are read the first time only (tokens_end_ is set past them; pc_
// stays after the 21 - it only moves by jumping back or to tokens_end_). No count: back to the start for ever (until
// a break). A count: counted down, back to the start until it reaches 0 - so `repeat 0` (or a negative count)
// goes round 2^32 times, as in the original.
void Script::run_counted_loop(Tokens& pc)
{
    RepeatFrame* loop = innermost_repeat_;
    if (loop->args_expected_) {
        ++pc;
        pc_ = pc.address();
        stmt_args_->get_clear();
        innermost_repeat_->tokens_end_ = parse_arguments(stmt_args_.get(), pc, args_.get()).address();
        innermost_repeat_->args_seen_ = stmt_args_->fetch_int_value(0u, &innermost_repeat_->headcount_, 0);
        innermost_repeat_->args_expected_ = false;
    }
    loop = innermost_repeat_;
    if (!loop->args_seen_) {
        pc = tokens_at(pc, loop->tokens_begin_);
        return;
    }
    --loop->headcount_;
    if (innermost_repeat_->headcount_ != 0) {
        pc = tokens_at(pc, innermost_repeat_->tokens_begin_);
        return;
    }
    pc = tokens_at(pc, innermost_repeat_->tokens_end_);
    innermost_repeat_ = outer_loop(*this, innermost_repeat_);
}

// 0x00472B50: 22 break: past the repeat of this loop (counting nested begin / repeat), whose arguments are
// parsed (without the script's parameters) and dropped, then out of the loop.
void Script::run_loop_exit(Tokens& pc)
{
    int depth = 0;
    bool searching = true;
    for (bool again = true; again; again = searching) {
        const uint8_t token = *pc;
        if (token == kTokenLoopStart) {
            ++depth;
        } else if (token == kTokenLoopRepeat) {
            if (depth)
                --depth;
            else
                searching = false;
        }
        pc = step_over_token(pc);
    }
    pc_ = pc.address();
    stmt_args_->get_clear();
    pc = parse_arguments(stmt_args_.get(), pc, nullptr);
    innermost_repeat_ = outer_loop(*this, innermost_repeat_);
}

// 0x004750B0 (see the top of the file)
ScriptOutcome Script::tick_frame()
{
    flag_bits_ &= ~kScriptRunWaitUpdated;
    // NOT ORIGINAL - the tokens are read through a cursor over the block of code pc_ is in (tokens.h). pc_ stays the
    // script's place, written back after each token or statement and before anything is called; what is called may
    // move it (a script called or returned to, a Goto, an interrupt, a block of code made or freed): the cursor then
    // follows it - a look-up per script taken up, not per token.
    Tokens pc = tokens_at(pc_);
    uint32_t blocks_seen = code_blocks_generation();
    // (NOT ORIGINAL, log only: a script that runs this many steps in one Update never yields - a loop whose wait
    // ends at once, e.g. `while { Obj_WaitAnimFinished }` with no animation playing: the game hangs here, as the
    // original does. Logged once per script, with its name and its object's id, before the hang.)
    uint32_t steps = 0;
    while (pc_) {
        if (++steps == kRunawayStepCount) {
            constexpr size_t kSLoggedSize = kLoggedScriptCount;
            static std::array<uint32_t, kSLoggedSize> s_logged;
            static int tally;
            bool seen = false;
            for (int i = 0; i < tally; ++i)
                seen = seen || core::at(s_logged, i) == script_title_;
            if (!seen) {
                if (tally < kLoggedScriptCount)
                    core::at(s_logged, tally++) = script_title_;
                const objects::Entity* object = entity_.get_pull();
                // (the object's id as a number: find_checksum_name_or_unknown's text for an unknown name is one buffer)
                get_log("script: %s (object id %08X) has run %u steps in one frame without waiting - a loop that never "
                        "yields (the game hangs here)",
                        find_checksum_name_or_unknown(script_title_), object ? object->ident() : 0u, steps);
            }
        }
        current_script_var() = this;
        if (!(flag_bits_ & kScriptRunWaitUpdated))
            update_wait();
        if (suspend_reason_ != kSuspendOnNothing) {
            flag_bits_ = static_cast<uint8_t>((flag_bits_ & ~kScriptRunInterrupted) | kScriptRunWaitUpdated);
            current_script_var() = nullptr;
            return suspend_reason_ == kSuspendOnBlock || suspend_reason_ == kWaitTypeRealTimer
                       ? kScriptOutcomeBlocked
                       : kScriptOutcomeSuspended;
        }
        if (pc.address() != pc_ || blocks_seen != code_blocks_generation()) {
            pc = tokens_at(pc_);
            blocks_seen = code_blocks_generation();
        }
        switch (*pc) {
        case kTokenEndOfLine: // end of line
        case kTokenEndIf:
        case kTokenEndSwitch: // endswitch
            ++pc;
            break;
        case kTokenEndOfLineNumbered: // end of line + line number
            pc += kTokenWithWordBytes;
            break;
        case kTokenLoopStart:
            run_loop_open(pc);
            break;
        case kTokenLoopRepeat:
            run_counted_loop(pc);
            break;
        case kTokenLoopBreak:
            run_loop_exit(pc);
            break;
        case kTokenScriptEnd: // '$'
            if (const uint32_t on_exit = on_exit_script_) {
                on_exit_script_ = 0;
                preempt(on_exit, nullptr);
            }
            if (return_from_script(nullptr))
                return kScriptOutcomeEndedByInterrupt;
            continue; // (pc_ is the caller's now, or null)
        case kTokenReturn: // return [args]
            if (const uint32_t on_exit = on_exit_script_) {
                on_exit_script_ = 0;
                preempt(on_exit, nullptr);
            }
            // (from wherever the on-exit script left pc_ - see docs/script-runtime.md)
            pc = tokens_at(pc, pc_);
            ++pc;
            parse_function_params(pc);
            if (return_from_script(stmt_args_.get()))
                return kScriptOutcomeEndedByInterrupt;
            continue;
        case kTokenIf:
        case kTokenElse:
        case kTokenCase: // case
        case kTokenDefault: // default
            break; // (not moved past: loops for ever, as in the original)
        case kTokenLongJump:
        case kTokenRandom:
        case kTokenRandomSideStream:
        case kTokenRandomNoRepeat:
        case kTokenRandomPermute:
        case kTokenShortJump:
            pc = resolve_random_jumps(pc);
            break;
        case kTokenSwitch:
            run_multi_branch(pc);
            break;
        case kTokenShortIf:
            run_conditional(pc);
            continue; // (pc_ set by the statement it ran)
        case kTokenShortElse: // else: jump to the endif
            ++pc;
            pc += fetch_word16(pc);
            break;
        default:
            execute_command(pc);
            continue; // (pc_ set by the statement, or by what it called)
        }
        pc_ = pc.address();
    }
    flag_bits_ &= ~kScriptRunInterrupted;
    current_script_var() = nullptr;
    return kScriptOutcomeFinished;
}

} // namespace script
