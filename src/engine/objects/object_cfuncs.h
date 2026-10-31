#pragma once

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) for nodes and composite objects, 0x00449280-0x00449F80: node parameters and names,
// the COIM heap, making composite objects and finding free ids, screen positions, running scripts on components and
// objects, local-to-world transforms, destroying particle groups, orienting objects, event handlers on objects. All
// are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_append_node_params(script::Struct* params, script::Script* script);          // 0x00449280
bool __cdecl cfunc_init_coim(script::Struct* params, script::Script* script);                  // 0x004492E0
bool __cdecl cfunc_resize_coim_heap(script::Struct* params, script::Script* script);            // 0x00449380
bool __cdecl cfunc_create_composite_object(script::Struct* params, script::Script* script);     // 0x00449540
bool __cdecl cfunc_get_unique_composite_object_id(script::Struct* params, script::Script* script); // 0x004495A0
bool __cdecl cfunc_get_obj_screen_coords(script::Struct* params, script::Script* script);        // 0x00449610
bool __cdecl cfunc_run_script_on_component_type(script::Struct* params, script::Script* script);  // 0x004496B0
bool __cdecl cfunc_transform_local_to_world(script::Struct* params, script::Script* script);     // 0x00449740
bool __cdecl cfunc_destroy_particles_by_group_id(script::Struct* params, script::Script* script); // 0x004497D0
bool __cdecl cfunc_get_node(script::Struct* params, script::Script* script);                   // 0x00449850
bool __cdecl cfunc_get_node_name(script::Struct* params, script::Script* script);               // 0x00449890
bool __cdecl cfunc_get_trigger_node_name(script::Struct* params, script::Script* script);        // 0x004498C0
bool __cdecl cfunc_run_script_on_object(script::Struct* params, script::Script* script);         // 0x00449A10
bool __cdecl cfunc_orient(script::Struct* params, script::Script* script);                    // 0x00449AC0
bool __cdecl cfunc_set_event_handler_on_object(script::Struct* params, script::Script* script);   // 0x00449F80

} // namespace commands
