// NOT ORIGINAL - the QB interpreter behind script::Host (qb_host.h): one line each, into the interpreter's functions
// (qb_script.h) - the same calls, with the same arguments, in the same order as the game made them.

#include "script_qb/qb_host.h"

#include <cstdint>

#include "base/core/no_destroy.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "script_qb/qb_script.h"

namespace objects {
class Entity;
class ObjectModule;
struct Signal;
} // namespace objects

namespace script {
class Array;
class Struct;
} // namespace script

namespace script::qb {

namespace {

class QbHost final : public Host {
public:
    // --- scripts by name ---
    Script* spawn_script(uint32_t name, Struct* params, const SpawnSetup& setup) override
    {
        return qb::spawn_script(name, params, setup);
    }
    void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                       const char* name_string) override
    {
        qb::launch_script(name, params, object, replicate_flag, name_string);
    }
    void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results) override
    {
        qb::run_script_once(name, params, object, results);
    }
    bool script_exists(uint32_t name) override { return qb::script_exists(name); }

    // --- the spawned scripts ---
    void tick_child_scripts() override { qb::tick_child_scripts(); }
    void kill_all_child_scripts() override { qb::kill_all_child_scripts(); }
    void kill_spawned_script(Script* script) override { qb::kill_spawned_script(script); }
    void pause_spawned_scripts(bool pause) override { qb::pause_spawned_scripts(pause); }
    void resume_child_script(Script* script) override { qb::resume_child_script(script); }
    uint32_t get_spawn_id(Script* script) override { return qb::get_spawn_id(script); }
    Script* look_up_spawned_script(uint32_t id) override { return qb::look_up_spawned_script(id); }
    void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent) override
    {
        qb::send_spawn_script_message(name, object, node, permanent);
    }

    // --- the stop walks ---
    void stop_scripts_targeting(uint32_t name) override { qb::stop_scripts_targeting(name); }
    void stop_scripts_by_tag(uint32_t id) override { qb::stop_scripts_by_tag(id); }
    void stop_scripts_of_object_and_tag(objects::Entity* object, uint32_t id, Script* except) override
    {
        qb::stop_scripts_of_object_and_tag(object, id, except);
    }
    void end_child_scripts_of_entity_by_name(objects::Entity* object, uint32_t name, Script* except) override
    {
        qb::end_child_scripts_of_entity_by_name(object, name, except);
    }
    void stop_all_scripts() override { qb::stop_all_scripts(); }
    void end_scripts_bound_to_entity(objects::Entity* object) override { qb::end_scripts_bound_to_entity(object); }
    void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name) override
    {
        qb::stop_scripts_using_object_that_refer_to(object, name);
    }
    void delete_finished_scripts() override { qb::delete_finished_scripts(); }

    // --- every script ---
    Script* get_running_script() override { return current_script_var(); }
    Script* find_script_with_unique_id(uint32_t id) override { return qb::find_script_with_unique_id(id); }
    void component_destroyed_all_scripts(objects::ObjectModule* component) override
    {
        qb::component_destroyed_all_scripts(component);
    }
    void relaunch_scripts_referring_to(uint32_t name) override { qb::relaunch_scripts_referring_to(name); }
    void stop_scripts_left_without_code() override { qb::stop_scripts_left_without_code(); }

    // --- one script ---
    Script* create() override { return QbScript::create(); }
    void destroy(Script* script) override { QbScript::destroy(as_qb(script)); }
    void bind_script(Script* script, uint32_t name, Struct* params, objects::Entity* object) override
    {
        as_qb(script)->bind_script(name, params, object);
    }
    void set_script_copy(Script* script, ParamScript* code, Struct* params, objects::Entity* object) override
    {
        as_qb(script)->set_script_copy(code, params, object);
    }
    ScriptOutcome tick_frame(Script* script) override { return as_qb(script)->tick_frame(); }
    void object_destroyed(Script* script, objects::Entity* object) override
    {
        as_qb(script)->object_destroyed(object);
    }
    void run_on_exit_scripts(Script* script) override { as_qb(script)->run_on_exit_scripts(); }
    void clear_on_exit_scripts(Script* script) override { as_qb(script)->clear_on_exit_scripts(); }
    void set_spawner_file(Script* spawned, Script* spawner) override { as_qb(spawned)->set_spawner_file(spawner); }

    // --- one script's wait ---
    void stop_waiting(Script* script) override { as_qb(script)->stop_waiting(); }
    bool is_blocked(Script* script, bool print) override { return as_qb(script)->is_blocked(print); }
    void get_stall(Script* script, int frames) override { as_qb(script)->get_stall(frames); }
    void suspend_until_next_frame(Script* script, int frames) override
    {
        as_qb(script)->suspend_until_next_frame(frames);
    }
    void lull(Script* script, int ms) override { as_qb(script)->lull(ms); }
    void wait_real_time(Script* script, int ms) override { as_qb(script)->wait_real_time(ms); }
    void gate_off(Script* script, uint32_t event_type) override { as_qb(script)->gate_off(event_type); }
    void set_wait_on_component(Script* script, BlockReason type, objects::ObjectModule* component,
                               uint32_t arg) override
    {
        as_qb(script)->set_wait_on_component(type, component, arg);
    }
    void unblock_interrupted_script(Script* script) override { as_qb(script)->unblock_interrupted_script(); }

    // --- one script's events ---
    bool receive_event(Script* script, objects::Signal* event) override { return as_qb(script)->receive_event(event); }
    ScriptOutcome preempt(Script* script, uint32_t name, Struct* params) override
    {
        return as_qb(script)->preempt(name, params);
    }
    void clear_event_handler_group(Script* script, uint32_t group) override
    {
        as_qb(script)->clear_event_handler_group(group);
    }
    int print_event_handler_table(Script* script) override { return as_qb(script)->print_event_handler_table(); }
    void install_event_table(Script* script, Array* handlers, bool replace) override
    {
        as_qb(script)->install_event_table(handlers, replace);
    }
    void set_event_handler(Script* script, uint32_t event_type, uint32_t handler, uint32_t group, bool exception,
                           Struct* params) override
    {
        as_qb(script)->set_event_handler(event_type, handler, group, exception, params);
    }
    void clear_event_handler(Script* script, uint32_t event_type) override
    {
        as_qb(script)->clear_event_handler(event_type);
    }
    void reset_event_handlers_from_table(Script* script, Array* table, uint32_t group) override
    {
        as_qb(script)->reset_event_handlers_from_table(table, group);
    }
};

} // namespace

Host& host()
{
    static core::NoDestroy<QbHost> runtime;
    return runtime.get();
}

} // namespace script::qb
