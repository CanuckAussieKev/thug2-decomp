#pragma once

#include <cstdint>

namespace script {
class Script;
class Struct;
} // namespace script

// The screen element script commands (the Function table's entries; cdecl).
namespace frontend {

bool __cdecl cfunc_screen_element_system_init(script::Struct* params, script::Script* script);    // 0x004AAD90
bool __cdecl cfunc_create_screen_element(script::Struct* params, script::Script* script);         // 0x004AD240
bool __cdecl cfunc_destroy_screen_element(script::Struct* params, script::Script* script);        // 0x004AD300
bool __cdecl cfunc_run_script_on_screen_element(script::Struct* params, script::Script* script);  // 0x004AD370
bool __cdecl cfunc_set_screen_element_props(script::Struct* params, script::Script* script);      // 0x004AD4C0
bool __cdecl cfunc_do_screen_element_morph(script::Struct* params, script::Script* script);       // 0x004AD540
bool __cdecl cfunc_set_screen_element_lock(script::Struct* params, script::Script* script);       // 0x004AD5C0
bool __cdecl cfunc_get_screen_element_dims(script::Struct* params, script::Script* script);       // 0x004AD650
bool __cdecl cfunc_get_screen_element_props(script::Struct* params, script::Script* script);      // 0x004AC750
bool __cdecl cfunc_set_root_screen_element(script::Struct* params, script::Script* script);       // 0x004AC900
bool __cdecl cfunc_get_screen_element_children(script::Struct* params, script::Script* script);   // 0x004AC940
bool __cdecl cfunc_text_element_concatenate(script::Struct* params, script::Script* script);      // 0x004AD710
bool __cdecl cfunc_text_element_backspace(script::Struct* params, script::Script* script);        // 0x004AD800
bool __cdecl cfunc_get_text_element_string(script::Struct* params, script::Script* script);       // 0x004AD8D0
bool __cdecl cfunc_get_text_element_length(script::Struct* params, script::Script* script);       // 0x004AD9C0
bool __cdecl cfunc_get_screen_element_position(script::Struct* params, script::Script* script);   // 0x004ADA80
bool __cdecl cfunc_menu_selected_index_is(script::Struct* params, script::Script* script);        // 0x004ADB20
bool __cdecl cfunc_screen_element_exists(script::Struct* params, script::Script* script);         // 0x004ADC10
bool __cdecl cfunc_create_multiple_text_elements(script::Struct* params, script::Script* script); // 0x004ADC80

extern uint32_t g_unassigned_id;
} // namespace frontend
