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

#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cache.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/params.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/loading_console.h"
#include "platform/run_log.h"

namespace script {

namespace {

// The script whose missing calls aren't reported (presumably the default level's script).
constexpr uint32_t kQuietScriptName = 0xFB11E6CD;
// Member function entries for these component types are run by the object itself.
constexpr uint32_t kObjectComponentType = 0x57525413;
constexpr uint32_t kObjectComponentType2 = 0x0ADA4E74;

// The object's and its components' script member functions. The object's: Entity::invoke_method (the original's
// slot 2) and Entity::invoke_method_by_name (slot 3).
bool object_call_member_function(objects::Entity* object, MemberFunctionEntry* entry, Struct* params, Script* script)
{
    return object->invoke_method(entry, params, script);
}
bool object_call_member_function(objects::Entity* object, uint32_t name, Struct* params, Script* script)
{
    return object->invoke_method_by_name(name, params, script);
}
// ObjectModule::invoke_method (vtable +0x2C; the original tests the whole of EAX, not just AL).
bool component_call_member_function(objects::ObjectModule* component, MemberFunctionEntry* entry, Struct* params,
                                    Script* script)
{
    return component->invoke_method(entry, params, script) != 0;
}
// The object's component of `type` (GameEntity::fetch_module).
objects::ObjectModule* fetch_module(objects::Entity* object, uint32_t type)
{
    return static_cast<objects::GameEntity*>(object)->fetch_module(type);
}
// The object named `name` (GameObjects::find_object: the object tracker's), or null.
objects::Entity* find_object(uint32_t name) { return objects::find_object(name); }
// 0x0044E4F0 on online::Hub (0x007CCE14): called where a member function has no object to run on, its
// result unused - what is left of a compiled-out assert.
void no_object_check() { online::g_online_hub->in_net_game(); }

// A component made and freed around one expression / switch value.
void delete_component(Component* component)
{
    release_component(component);
    free_component(component);
}

} // namespace

// 0x00472980: run a member function's table entry on `object` (no null checks).
bool invoke_method_by_name(MemberFunctionEntry* entry, Struct* params, objects::Entity* object, Script* script)
{
    uint32_t type = entry->owner_kind_;
    if (type == kObjectComponentType || type == kObjectComponentType2)
        return object_call_member_function(object, entry, params, script);
    return component_call_member_function(fetch_module(object, type), entry, params, script);
}

// 0x004731A0
bool Script::invoke_method_by_name(MemberFunctionEntry* entry, objects::Entity* object)
{
    if (!object)
        object = entity_.get_pull();
    if (!object) {
        no_object_check();
        return false;
    }
    return script::invoke_method_by_name(entry, stmt_args_, object, this);
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
    return object_call_member_function(object, name, stmt_args_, this);
}

// 0x00472950
void Script::parse_function_params()
{
    stmt_args_->get_clear();
    pc_ = parse_arguments(stmt_args_, pc_, args_);
}

// 0x004729E0: a name token, `<name>` (the parameter's checksum value) or `<...>` (the unnamed checksum
// parameter); anything else is 0 and isn't moved past.
uint32_t Script::read_name()
{
    uint32_t name = 0;
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (*pc_) {
    case 0x16:
        ++pc_;
        name = fetch_word32(pc_);
        pc_ += 4;
        break;
    case 0x2C:
        ++pc_;
        args_->read_hash_value(0u, &name, 0);
        break;
    case 0x2D: // 2D 16 <name>
        pc_ += 2;
        {
            uint32_t parameter = fetch_word32(pc_);
            pc_ += 4;
            args_->read_hash_value(parameter, &name, 0);
        }
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
// The arguments are parsed into stmt_args_ (they may refer to args_).
bool Script::execute_command()
{
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (*pc_) {
    case 0x44: {
        ++pc_;
        uint32_t name = fetch_word32(pc_);
        pc_ += 4;
        parse_function_params();
        if (!entity_.get_pull()) {
            no_object_check();
            return false;
        }
        return object_call_member_function(entity_.get_pull(), name, stmt_args_, this);
    }
    case 0x46: {
        ++pc_;
        auto* entry = reinterpret_cast<MemberFunctionEntry*>(fetch_word32(pc_));
        pc_ += 4;
        parse_function_params();
        if (!entity_.get_pull()) {
            no_object_check();
            return false;
        }
        return script::invoke_method_by_name(entry, stmt_args_, entity_.get_pull(), this);
    }
    case 0x43: {
        ++pc_;
        auto function = reinterpret_cast<Function>(fetch_word32(pc_));
        pc_ += 4;
        parse_function_params();
        return function(stmt_args_, this);
    }
    }

    uint32_t name;
    if (*pc_ == 0x0E) {
        Component* value = new_component();
        pc_ = parse_expression(pc_, args_, value);
        if (value->kind_tag() != kQbvalName) {
            bool result = value->raw != 0;
            delete_component(value);
            return result;
        }
        name = value->checksum;
        delete_component(value);
        if (!name)
            name = read_name();
    } else {
        name = read_name();
    }

    if (*pc_ == 0x42) { // name:
        objects::Entity* object = find_object(name);
        if (!object)
            return true; // (pc_ left at the ':')
        ++pc_;
        pc_ = resolve_random_jumps(pc_);
        if (*pc_ == 0x44) {
            ++pc_;
            uint32_t member = fetch_word32(pc_);
            pc_ += 4;
            parse_function_params();
            return invoke_method_by_name(member, object);
        }
        uint32_t function = read_name();
        parse_function_params();
        SymbolTableEntry* symbol = resolve_symbol(function);
        if (!symbol && script_title_ == kQuietScriptName)
            return false;
        // (anywhere else a missing name's type is read through the null symbol: the original crashes - kept)
        switch (symbol->type) {
        case kQbvalScriptBody: {
            const uint8_t* code = script_store_instance()->fetch_script(symbol->name);
            run_sub_script(function, code, stmt_args_, object, false);
            return true;
        }
        case kQbvalNativeMethod:
            if (auto* entry = reinterpret_cast<MemberFunctionEntry*>(symbol->raw))
                return invoke_method_by_name(entry, object);
            return invoke_method_by_name(function, object);
        default: // (C functions included: not run on an object)
            return false;
        }
    }

    if (*pc_ == 0x07) { // name = value
        pc_ -= 4;
        uint32_t target = fetch_word32(pc_);
        pc_ += 5;
        pc_ = resolve_random_jumps(pc_);
        Component* value = new_component();
        if (*pc_ == 0x0E)
            pc_ = parse_expression(pc_, args_, value);
        else
            pc_ = parse_value(pc_, args_, value);
        if (value->kind_tag() != kQbvalNone) {
            value->name = target;
            args_->attach_module(value);
        } else {
            free_component(value);
        }
        return true;
    }

    parse_function_params();
    SymbolTableEntry* symbol = resolve_symbol(name);
    if (!symbol) {
        if (script_title_ != kQuietScriptName)
            debug::printf("WARNING: script %s not found, ignoring in default level.\n",
                          find_checksum_name_or_unknown(name));
        return false;
    }
    // Not original - the loading console (engine/gfx/loading_console.h): while a loading screen is up, the scripts
    // called and the commands that load or build things.
    if (gfx::loading_console::live_now() && (symbol->type == kQbvalScriptBody || symbol->type == kQbvalNativeFunc))
        gfx::loading_console::note_script_call(name, symbol->type == kQbvalNativeFunc, stmt_args_);
    switch (symbol->type) {
    case kQbvalScriptBody: {
        const uint8_t* code = script_store_instance()->fetch_script(symbol->name);
        run_sub_script(name, code, stmt_args_, entity_.get_pull(), false);
        return true;
    }
    case kQbvalNativeFunc:
        return reinterpret_cast<Function>(symbol->raw)(stmt_args_, this);
    case kQbvalNativeMethod: {
        auto* entry = reinterpret_cast<MemberFunctionEntry*>(symbol->raw);
        objects::Entity* object = entity_.owner_object_;
        if (entry) {
            if (object)
                return script::invoke_method_by_name(entry, stmt_args_, object, this);
        } else if (object) {
            return object_call_member_function(object, name, stmt_args_, this);
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
// jumps; else it goes on into the true part.
void Script::run_conditional()
{
    ++pc_;
    const uint8_t* target = pc_ + fetch_word16(pc_);
    pc_ += 2;
    bool negate = false;
    if (*pc_ == 0x39) {
        ++pc_;
        negate = true;
    }
    bool condition = execute_command();
    if (!pc_)
        return;
    if (negate)
        condition = !condition;
    if (!condition)
        pc_ = target;
}

// 0x00472A70: pc_ just after a 3C switch: past its 3D endswitch (switches inside skipped whole).
void Script::skip_to_end_of_switch()
{
    while (*pc_ != 0x3D) {
        if (*pc_ == 0x3C) {
            ++pc_;
            skip_to_end_of_switch();
        } else {
            pc_ = step_over_token(pc_);
        }
    }
    ++pc_;
}

// 0x00473C60: 3C value ... then cases `3E 49 <offset to the next case> value ...`, `3F 49 <offset>` default
// and 3D endswitch. The switch's value is the first component of its line; a case matches if its line's first
// component equals it (components_equal). On a match, the case labels right after it (`49 xx xx 3E 49 xx xx
// value`) are skipped and the case's statements run from there; the 49 jump at the end of a case's statements
// goes to the endswitch. No match: on to the next case, finally into the default or past the endswitch.
void Script::run_multi_branch()
{
    ++pc_;
    parse_function_params();
    Component* value = new_component();
    if (Component* first = stmt_args_->step_to_entry(nullptr))
        duplicate_component(value, first, 0);
    for (;;) {
        uint8_t token = *pc_;
        if (token == 0x24) // '$': not moved past - the original loops for ever here (kept)
            continue;
        if (token == 0x3C) { // a switch in a case not taken
            ++pc_;
            skip_to_end_of_switch();
            continue;
        }
        if (token == 0x3D) {
            ++pc_;
            break;
        }
        if (token == 0x3F) { // default: past 3F 49 xx xx
            pc_ += 4;
            break;
        }
        if (token != 0x3E) {
            pc_ = step_over_token(pc_);
            continue;
        }
        pc_ += 2; // the jump's offset
        const uint8_t* next = pc_ + fetch_word16(pc_);
        pc_ += 2;
        parse_function_params();
        Component* match = stmt_args_->step_to_entry(nullptr);
        if (!match || !components_equal(value, match)) {
            pc_ = next;
            continue;
        }
        pc_ = skip_newline_tokens(pc_);
        while (*pc_ == 0x49 && pc_[3] == 0x3E) {
            pc_ += 7;
            parse_function_params();
            pc_ = skip_newline_tokens(pc_);
        }
        break;
    }
    delete_component(value);
}

// 0x00473220: 20 begin. The next loop of the loop stack starts after the token; the first nested loop (the
// inline loop in use) moves the stack to a block of 10 on the script heap - also when the inline loop belongs
// to a calling level (run_sub_script keeps innermost_repeat_: one stack for every level), whose saved loop pointers are
// moved with it. Not checked beyond 10.
void Script::run_loop_open()
{
    ++pc_;
    if (innermost_repeat_ == &inline_loop_) {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->script_heap_);
        loops_ = static_cast<RepeatFrame*>(memory::alloc_block(10 * sizeof(RepeatFrame)));
        memory::Hub::get_pull()->leave_heap_scope();
        loops_[0] = inline_loop_;
        innermost_repeat_ = loops_;
        for (int i = 0; i < call_depth_; ++i) {
            if (RepeatFrame* loop = frame_records_[i].loop_state_)
                frame_records_[i].loop_state_ = loops_ + (loop - &inline_loop_);
        }
    }
    innermost_repeat_ = innermost_repeat_ ? innermost_repeat_ + 1 : loops_;
    innermost_repeat_->tokens_begin_ = pc_;
    innermost_repeat_->tokens_end_ = nullptr;
    innermost_repeat_->args_seen_ = false;
    innermost_repeat_->args_expected_ = true;
    innermost_repeat_->headcount_ = 0;
}

// The loop below the current one (none: null).
static RepeatFrame* outer_loop(RepeatFrame* loop, RepeatFrame* loops) { return loop == loops ? nullptr : loop - 1; }

// 0x00472AB0: 21 repeat [count]. Its arguments are read the first time only (tokens_end_ is set past them; pc_
// stays after the 21 - it only moves by jumping back or to tokens_end_). No count: back to the start for ever (until
// a break). A count: counted down, back to the start until it reaches 0 - so `repeat 0` (or a negative count)
// goes round 2^32 times, as in the original.
void Script::run_counted_loop()
{
    RepeatFrame* loop = innermost_repeat_;
    if (loop->args_expected_) {
        ++pc_;
        stmt_args_->get_clear();
        innermost_repeat_->tokens_end_ = parse_arguments(stmt_args_, pc_, args_);
        innermost_repeat_->args_seen_ = stmt_args_->fetch_int_value(0u, &innermost_repeat_->headcount_, 0);
        innermost_repeat_->args_expected_ = false;
    }
    loop = innermost_repeat_;
    if (!loop->args_seen_) {
        pc_ = loop->tokens_begin_;
        return;
    }
    --loop->headcount_;
    if (innermost_repeat_->headcount_ != 0) {
        pc_ = innermost_repeat_->tokens_begin_;
        return;
    }
    pc_ = innermost_repeat_->tokens_end_;
    innermost_repeat_ = outer_loop(innermost_repeat_, loops_);
}

// 0x00472B50: 22 break: past the repeat of this loop (counting nested begin / repeat), whose arguments are
// parsed (without the script's parameters) and dropped, then out of the loop.
void Script::run_loop_exit()
{
    int depth = 0;
    bool searching = true;
    do {
        uint8_t token = *pc_;
        if (token == 0x20) {
            ++depth;
        } else if (token == 0x21) {
            if (depth)
                --depth;
            else
                searching = false;
        }
        pc_ = step_over_token(pc_);
    } while (searching);
    stmt_args_->get_clear();
    pc_ = parse_arguments(stmt_args_, pc_, nullptr);
    innermost_repeat_ = outer_loop(innermost_repeat_, loops_);
}

// 0x004750B0 (see the top of the file)
ScriptOutcome Script::tick_frame()
{
    flag_bits_ &= ~kScriptRunWaitUpdated;
    // (NOT ORIGINAL, log only: a script that runs this many steps in one Update never yields - a loop whose wait
    // ends at once, e.g. `while { Obj_WaitAnimFinished }` with no animation playing: the game hangs here, as the
    // original does. Logged once per script, with its name and its object's id, before the hang.)
    uint32_t steps = 0;
    while (pc_) {
        if (++steps == 5000000u) {
            static uint32_t s_logged[16];
            static int tally;
            bool seen = false;
            for (int i = 0; i < tally; ++i)
                seen = seen || s_logged[i] == script_title_;
            if (!seen) {
                if (tally < 16)
                    s_logged[tally++] = script_title_;
                objects::Entity* object = entity_.owner_object_;
                // (the object's id as a number: find_checksum_name_or_unknown's text for an unknown name is one buffer)
                get_log("script: %s (object id %08X) has run %u steps in one frame without waiting - a loop that never "
                        "yields (the game hangs here)",
                        find_checksum_name_or_unknown(script_title_), object ? object->ident_ : 0u, steps);
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
        switch (*pc_) {
        case 0x01: // end of line
        case 0x28:
        case 0x3D: // endswitch
            ++pc_;
            break;
        case 0x02: // end of line + line number
            pc_ += 5;
            break;
        case 0x20:
            run_loop_open();
            break;
        case 0x21:
            run_counted_loop();
            break;
        case 0x22:
            run_loop_exit();
            break;
        case 0x24: // '$'
            if (uint32_t on_exit = on_exit_script_) {
                on_exit_script_ = 0;
                preempt(on_exit, nullptr);
            }
            if (return_from_script(nullptr))
                return kScriptOutcomeEndedByInterrupt;
            break;
        case 0x29: // return [args]
            if (uint32_t on_exit = on_exit_script_) {
                on_exit_script_ = 0;
                preempt(on_exit, nullptr);
            }
            // (from wherever the on-exit script left pc_ - see docs/script-runtime.md)
            ++pc_;
            parse_function_params();
            if (return_from_script(stmt_args_))
                return kScriptOutcomeEndedByInterrupt;
            break;
        case 0x25:
        case 0x26:
        case 0x3E: // case
        case 0x3F: // default
            break; // (not moved past: loops for ever, as in the original)
        case 0x2E:
        case 0x2F:
        case 0x37:
        case 0x40:
        case 0x41:
        case 0x49:
            pc_ = resolve_random_jumps(pc_);
            break;
        case 0x3C:
            run_multi_branch();
            break;
        case 0x47:
            run_conditional();
            break;
        case 0x48: // else: jump to the endif
            ++pc_;
            pc_ += fetch_word16(pc_);
            break;
        default:
            execute_command();
            break;
        }
    }
    flag_bits_ &= ~kScriptRunInterrupted;
    current_script_var() = nullptr;
    return kScriptOutcomeFinished;
}

} // namespace script
