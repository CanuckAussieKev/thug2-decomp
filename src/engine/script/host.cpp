// NOT ORIGINAL - the installed script runtime (host.h), and the script functions the game calls by name (cscript.h),
// each the installed runtime's.

#include "engine/script/host.h"

#include <cstdint>

#include "base/core/checksums.h"
#include "engine/script/cscript.h"

namespace script {

namespace {

// The runtime app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Host*& installed()
{
    static Host* runtime = nullptr;
    return runtime;
}

} // namespace

Host& host() { return *installed(); }

void install_host(Host& runtime) { installed() = &runtime; }

void tick_child_scripts() { host().tick_child_scripts(); }
void kill_all_child_scripts() { host().kill_all_child_scripts(); }
Script* spawn_script(uint32_t name, Struct* params, const SpawnSetup& setup)
{
    return host().spawn_script(name, params, setup);
}
// 0x00475670
Script* spawn_script(const char* name, Struct* params, const SpawnSetup& setup)
{
    return host().spawn_script(checksums::hash_from_text(name), params, setup);
}
void kill_spawned_script(Script* script) { host().kill_spawned_script(script); }
void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag, const char* name_string)
{
    host().launch_script(name, params, object, replicate_flag, name_string);
}
// 0x004758F0
void launch_script(const char* name, Struct* params, objects::Entity* object, bool replicate_flag)
{
    host().launch_script(checksums::hash_from_text(name), params, object, replicate_flag, name);
}
void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results)
{
    host().run_script_once(name, params, object, results);
}
void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent)
{
    host().send_spawn_script_message(name, object, node, permanent);
}
void pause_spawned_scripts(bool pause) { host().pause_spawned_scripts(pause); }
void resume_child_script(Script* script) { host().resume_child_script(script); }
bool script_exists(uint32_t name) { return host().script_exists(name); }
uint32_t get_spawn_id(Script* script) { return host().get_spawn_id(script); }
Script* look_up_spawned_script(uint32_t id) { return host().look_up_spawned_script(id); }
Script* find_script_with_unique_id(uint32_t id) { return host().find_script_with_unique_id(id); }
void component_destroyed_all_scripts(objects::ObjectModule* component)
{
    host().component_destroyed_all_scripts(component);
}
void stop_scripts_targeting(uint32_t name) { host().stop_scripts_targeting(name); }
void stop_scripts_by_tag(uint32_t id) { host().stop_scripts_by_tag(id); }
void stop_scripts_of_object_and_tag(objects::Entity* object, uint32_t id, Script* except)
{
    host().stop_scripts_of_object_and_tag(object, id, except);
}
void end_child_scripts_of_entity_by_name(objects::Entity* object, uint32_t name, Script* except)
{
    host().end_child_scripts_of_entity_by_name(object, name, except);
}
void stop_all_scripts() { host().stop_all_scripts(); }
void end_scripts_bound_to_entity(objects::Entity* object) { host().end_scripts_bound_to_entity(object); }
void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name)
{
    host().stop_scripts_using_object_that_refer_to(object, name);
}
void delete_finished_scripts() { host().delete_finished_scripts(); }

} // namespace script
