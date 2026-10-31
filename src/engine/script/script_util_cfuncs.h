#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// The engine's general script commands (CFunctions), 0x0044B7E0-0x0044CFFF: the script's own parameters, random
// values, arrays (stepping through, random elements, ForEachIn, building and changing them), jumping to other
// scripts, checksums and text. All are cdecl (params, script) and return whether the script's `if` should be
// taken; results are left in the calling script's parameters (CScript::args_).
namespace commands {

bool __cdecl cfunc_remove_parameter(script::Struct* params, script::Script* script);          // 0x0044B7E0
bool __cdecl cfunc_remove_flag(script::Struct* params, script::Script* script);               // 0x0044B820
bool __cdecl cfunc_get_random_value(script::Struct* params, script::Script* script);          // 0x0044B850
bool __cdecl cfunc_size_of(script::Struct* params, script::Script* script);                   // 0x0044B9A0
bool __cdecl cfunc_get_next_array_element(script::Struct* params, script::Script* script);    // 0x0044B9E0
bool __cdecl cfunc_get_random_array_element(script::Struct* params, script::Script* script);  // 0x0044BBD0
bool __cdecl cfunc_got_param(script::Struct* params, script::Script* script);                 // 0x0044BD30
bool __cdecl cfunc_goto(script::Struct* params, script::Script* script);                      // 0x0044BD60
bool __cdecl cfunc_goto_random_script(script::Struct* params, script::Script* script);        // 0x0044BDC0
bool __cdecl cfunc_format_text(script::Struct* params, script::Script* script);               // 0x0044BE20
bool __cdecl cfunc_mangle_checksums(script::Struct* params, script::Script* script);          // 0x0044BE40
bool __cdecl cfunc_append_suffix_to_checksum(script::Struct* params, script::Script* script); // 0x0044BEC0
bool __cdecl cfunc_get_upper_case_string(script::Struct* params, script::Script* script);     // 0x0044BF20
bool __cdecl cfunc_add_flag(script::Struct* params, script::Script* script);                  // 0x0044BFA0
bool __cdecl cfunc_append_suffix(script::Struct* params, script::Script* script);             // 0x0044BFD0
bool __cdecl cfunc_is_object_script(script::Struct* params, script::Script* script);          // 0x0044C120
bool __cdecl cfunc_for_each_in(script::Struct* params, script::Script* script);               // 0x0044C290
bool __cdecl cfunc_get_element(script::Struct* params, script::Script* script);               // 0x0044C430
bool __cdecl cfunc_permute_array(script::Struct* params, script::Script* script);             // 0x0044C5A0
bool __cdecl cfunc_goto_preserve_params(script::Struct* params, script::Script* script);      // 0x0044C730
bool __cdecl cfunc_dump(script::Struct* params, script::Script* script);                      // 0x0044C9B0
bool __cdecl cfunc_script_assert(script::Struct* params, script::Script* script);             // 0x0044CB70
bool __cdecl cfunc_create_index_array(script::Struct* params, script::Script* script);        // 0x0044CBC0
bool __cdecl cfunc_append_struct(script::Struct* params, script::Script* script);             // 0x0044CCC0
bool __cdecl cfunc_string_to_char_array(script::Struct* params, script::Script* script);      // 0x0044CDB0
bool __cdecl cfunc_set_array_element(script::Struct* params, script::Script* script);         // 0x0044CEC0

// 0x0044C840: the text a printf-like command shows, into `out` (followed by a newline): an unnamed string put
// together as FormatText would (with TextName=PrintfText, the parameters added; "" if that fails), else an
// unnamed integer; nothing is written without either. (`script` unused.) Always true.
bool __cdecl format_printf_text(char* out, script::Struct* params, script::Script* script);

} // namespace commands
