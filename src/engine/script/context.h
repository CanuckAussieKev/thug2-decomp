#pragma once

#include <cstdint>

#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "engine/script/param_struct.h"

// NOT ORIGINAL - script::Context: what a script command gets from the script that runs it. A command is
// `bool f(script::Struct* params, script::Context context)` (NativeFunction, cell.h): `params` are the statement's
// arguments, `context` the running script - its parameters (where a command leaves its results), its object, its
// spawn node and flags, the script it runs, its OnExceptionRun / OnExitRun scripts, its wait and its event handlers.
// The original passed the CScript itself.
//
// A view over the running script's record (cscript.h), one pointer, passed by value: every method is inline and is
// what the command did to the record before - a field read or set, or the installed host's operation on the running
// script (host.h) - so the QB interpreter's commands cost what they did. It isn't a second interface of its own: the
// part of a script that differs between runtimes is already behind script::Host's vtable (a wait, an event handler,
// a restart are the host's operations, which Context forwards), and the fields read here are the record's, which
// every runtime's scripts have (Host::create makes them; objects, the spawned list and the stop walks hold them).
// A later runtime (QuickJS, docs/qb-js-plan.md) gives its commands a Context on its own scripts' records; should one
// keep a script's parameters or object elsewhere, these accessors - the only place commands read them - are what
// would change, not the commands.
//
// Empty (no script) when a command is called without one: launch_script / run_script_once with a C function name
// (0x00475790), as the original passed null - a command that needs its script and is called so faults as before.
namespace script {

class Context {
public:
    constexpr Context() noexcept = default;
    constexpr explicit Context(Script* script) noexcept : script_(script) {}

    // Whether a script runs the command.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return script_ != nullptr; }
    // The running script's record, for what takes a script and isn't one of the methods below: the stop walks'
    // `except`, another script's operations (its spawner file), an object or component that keeps the script to
    // answer it later, a member function (member_code.h) the command calls for it.
    [[nodiscard]] constexpr Script* script() const noexcept { return script_; }

    // --- its data (the record's fields) ---
    // Its parameters: what the command reads beside `params` and where it leaves its results.
    [[nodiscard]] StructPtr& args() const { return script_->args(); }
    // The object it runs on (empty: none).
    [[nodiscard]] objects::SharedRef<objects::Entity>& entity() const { return script_->entity(); }
    // SpawnScript's node (the node array index it was spawned for).
    [[nodiscard]] int list_link() const { return script_->list_link(); }
    // The script it runs (its top level's name).
    [[nodiscard]] uint32_t script_title() const { return script_->script_title(); }
    // SCRIPT_FLAG_* (cscript.h: kScriptFlagSpawned...).
    [[nodiscard]] uint8_t& spawn_flags() const { return script_->spawn_flags(); }
    // OnExceptionRun / OnExitRun (this level's).
    void set_on_exception_script(uint32_t name) const { script_->set_on_exception_script(name); }
    void set_on_exit_script(uint32_t name) const { script_->set_on_exit_script(name); }

    // --- the host's operations on it (host.h has what each does) ---
    // Its wait.
    void stop_waiting() const { host().stop_waiting(script_); }
    void get_stall(int frames) const { host().get_stall(script_, frames); }
    void suspend_until_next_frame(int frames) const { host().suspend_until_next_frame(script_, frames); }
    void lull(int ms) const { host().lull(script_, ms); }
    void wait_real_time(int ms) const { host().wait_real_time(script_, ms); }
    void gate_off(uint32_t event_type) const { host().gate_off(script_, event_type); }
    void unblock_interrupted_script() const { host().unblock_interrupted_script(script_); }
    // Its event handlers.
    void set_event_handler(uint32_t event_type, uint32_t handler, uint32_t group, bool exception, Struct* params) const
    {
        host().set_event_handler(script_, event_type, handler, group, exception, params);
    }
    void clear_event_handler(uint32_t event_type) const { host().clear_event_handler(script_, event_type); }
    void clear_event_handler_group(uint32_t group) const { host().clear_event_handler_group(script_, group); }
    [[nodiscard]] int print_event_handler_table() const { return host().print_event_handler_table(script_); }
    void reset_event_handlers_from_table(Array* table, uint32_t group) const
    {
        host().reset_event_handlers_from_table(script_, table, group);
    }
    // Started again on `name` (Goto): no statement of it runs until its next Update.
    void bind_script(uint32_t name, Struct* params, objects::Entity* object) const
    {
        host().bind_script(script_, name, params, object);
    }
    // As a spawned script: its spawn id (0 if not spawned), and unpaused.
    [[nodiscard]] uint32_t get_spawn_id() const { return host().get_spawn_id(script_); }
    void resume_child_script() const { host().resume_child_script(script_); }
    // `spawned` (a script it spawned) given the .qb file of its bottom-level script as its spawner file.
    void give_spawner_file_to(Script* spawned) const { host().set_spawner_file(spawned, script_); }

private:
    Script* script_ = nullptr;
};

} // namespace script
