// NOT ORIGINAL - the interpreter's side of TypeScript scripts (ts_bridge.h, docs/qb-ts.md "In the game"). A script
// defined by a TypeScript module has a body of one kTsBody token; a call-stack level of a record running it is a level
// of QuickJS's (ts_bridge.cpp). When Update reaches the token, run_ts_level resumes that level until it calls a command
// or ends:
//   - a command: carried out as execute_command carries out a statement's (run_command / run_command_on, with
//     stmt_args_ the call's arguments). pc_ stays on the token, so a script it calls returns here, and the level is
//     resumed with the command's result when Update next reaches the token - after the callee's return, or the wait the
//     command set (Update checks it on its next pass, as after any statement);
//   - its end: as '$' / return - the level's OnExitRun script first, then return_from_script with its return values.
// The level's QuickJS state is kept by (record, call depth) and the level's parameter list (args_): a new list at that
// depth is a new level. A JavaScript fault is logged and ends the level.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "script_qb/qb_script.h"
#include "script_qb/script_check.h"
#include "script_qb/ts_bridge.h"

namespace script::qb {

namespace {

struct TsLevel {
    uint32_t id = 0;              // ts_bridge's level
    const Struct* args = nullptr; // the parameter list it was begun with
    bool result = false;          // its last command's result, for its next resume
};

std::unordered_map<const QbScript*, std::vector<TsLevel>>& ts_levels()
{
    static std::unordered_map<const QbScript*, std::vector<TsLevel>> levels;
    return levels;
}

// The level at `depth` of `script` (made when missing).
TsLevel& ts_level(const QbScript* script, int depth)
{
    std::vector<TsLevel>& stack = ts_levels()[script];
    if (stack.size() <= static_cast<size_t>(depth)) {
        stack.resize(static_cast<size_t>(depth) + 1);
    }
    return stack[static_cast<size_t>(depth)];
}

} // namespace

bool QbScript::run_ts_level(Tokens& pc)
{
    const uint32_t name = fetch_word32(pc + 1); // (the definition: runtime.js define)
    const int depth = call_depth_;
    pc_ = pc.address(); // (a script called from this level returns here)
    {
        TsLevel& level = ts_level(this, depth);
        if (level.id == 0 || level.args != args_.get()) {
            ts::drop(level.id);
            std::string error;
            level = {ts::begin(name, script_title_, args_.get(), unique_id_, error), args_.get(), false};
            if (level.id == 0) {
                get_log("ts: %s failed to start: %s", find_checksum_name_or_unknown(script_title_), error.c_str());
                check::ts_fault(error);
            }
        }
    }
    ts::Answer answer;
    if (const uint32_t id = ts_level(this, depth).id) {
        answer = ts::resume(id, args_.get(), ts_level(this, depth).result, stmt_args_.get());
    } else {
        answer.done = true;
        stmt_args_->get_clear();
    }
    if (!answer.error.empty()) {
        if (!check::ended()) { // (a check run already over stops its TypeScript by an interrupt: no fault)
            get_log("ts: %s: %s", find_checksum_name_or_unknown(script_title_), answer.error.c_str());
        }
        check::ts_fault(answer.error);
        answer.done = true;
        stmt_args_->get_clear();
    }

    if (!answer.done) {
        bool result = false;
        if (const auto stubbed = answer.on ? check::command(this, answer.call, *answer.on, stmt_args_.get())
                                           : std::nullopt) {
            result = *stubbed; // (the script check: object:Command is a stub - script_check.h)
        } else if (answer.on) {
            objects::Entity* object = objects::find_object(*answer.on);
            if (object) {
                result = run_command_on(answer.call, object);
            } else {
                // (the original loops on the ':' for ever here - a TypeScript level is told false instead)
                get_log("ts: %s: object %s not found for %s", find_checksum_name_or_unknown(script_title_),
                        find_checksum_name_or_unknown(*answer.on), find_checksum_name_or_unknown(answer.call));
            }
        } else {
            result = run_command(answer.call);
        }
        // (looked up again: the command may have run levels of its own - an interrupt - or reset the script)
        const auto found = ts_levels().find(this);
        if (found != ts_levels().end() && static_cast<size_t>(depth) < found->second.size()) {
            found->second[static_cast<size_t>(depth)].result = result;
        }
        return false;
    }

    // The end, as '$' / return: the level forgotten, its OnExitRun script run, then the return.
    ts::drop(ts_level(this, depth).id);
    ts_level(this, depth) = {};
    StructPtr results = script::make_struct();
    results->append_structure_if_any(stmt_args_.get());
    if (const uint32_t on_exit = on_exit_script_) {
        on_exit_script_ = 0;
        preempt(on_exit, nullptr);
    }
    return return_from_script(results.get());
}

void QbScript::forget_ts_levels()
{
    const auto found = ts_levels().find(this);
    if (found == ts_levels().end()) {
        return;
    }
    const std::vector<TsLevel> levels = std::move(found->second);
    ts_levels().erase(found);
    for (const TsLevel& level : levels) {
        ts::drop(level.id);
    }
}

} // namespace script::qb
