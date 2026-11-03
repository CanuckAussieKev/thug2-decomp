#pragma once

#include <array>
#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) for the script system itself, 0x00449D00-0x0044B7E0: changing globals, waiting,
// comparisons, the script strings, checking globals and structures, blocking, event handlers, stopping scripts,
// the warning level, integer conversions, adding and removing parameters. All are cdecl (params, script) and return
// whether the script's `if` should be taken.
namespace commands {

bool cfunc_change(script::Struct* params, script::Script* script);                          // 0x00449D00
bool cfunc_wait(script::Struct* params, script::Script* script);                            // 0x0044A0D0
bool cfunc_integer_equals(script::Struct* params, script::Script* script);                  // 0x0044A200
bool cfunc_checksum_equals(script::Struct* params, script::Script* script);                 // 0x0044A260
bool cfunc_string_equals(script::Struct* params, script::Script* script);                   // 0x0044A2C0
bool cfunc_set_script_string(script::Struct* params, script::Script* script);               // 0x0044A320
bool cfunc_global_exists(script::Struct* params, script::Script* script);                   // 0x0044A380
bool cfunc_enable_script_printf(script::Struct* params, script::Script* script);            // 0x0044A900
bool cfunc_if_debug_on(script::Struct* params, script::Script* script);                     // 0x0044A930
bool cfunc_structure_contains(script::Struct* params, script::Script* script);              // 0x0044A940
bool cfunc_clear_struct(script::Struct* params, script::Script* script);                    // 0x0044A9B0
bool cfunc_script_exists(script::Struct* params, script::Script* script);                   // 0x0044A9E0
bool cfunc_is_array(script::Struct* params, script::Script* script);                        // 0x0044AA20
bool cfunc_block(script::Struct* params, script::Script* script);                           // 0x0044AA50
bool cfunc_is_true(script::Struct* params, script::Script* script);                         // 0x0044AAB0
bool cfunc_clean_up_and_remove_symbol(script::Struct* params, script::Script* script);      // 0x0044AAE0
bool cfunc_set_event_handler(script::Struct* params, script::Script* script);               // 0x0044AB20
bool cfunc_set_exception_handler(script::Struct* params, script::Script* script);           // 0x0044ABD0
bool cfunc_reset_event_handlers_from_table(script::Struct* params, script::Script* script); // 0x0044AC70
bool cfunc_clear_event_handler(script::Struct* params, script::Script* script);             // 0x0044ACD0
bool cfunc_clear_event_handler_group(script::Struct* params, script::Script* script);       // 0x0044AD00
bool cfunc_on_exception_run(script::Struct* params, script::Script* script);                // 0x0044AD30
bool cfunc_on_exit_run(script::Struct* params, script::Script* script);                     // 0x0044AD60
bool cfunc_print_event_handler_table(script::Struct* params, script::Script* script);       // 0x0044AD90
bool cfunc_stop_all_scripts(script::Struct* params, script::Script* script);                // 0x0044ADA0
bool cfunc_first_time_this_is_called(script::Struct* params, script::Script* script);       // 0x0044ADB0
bool cfunc_set_script_warning_level(script::Struct* params, script::Script* script);        // 0x0044ADD0
bool cfunc_cast_to_integer(script::Struct* params, script::Script* script);                 // 0x0044AE30
bool cfunc_string_to_integer(script::Struct* params, script::Script* script);               // 0x0044AEB0
bool cfunc_add_params(script::Struct* params, script::Script* script);                      // 0x0044B720
bool cfunc_remove_component(script::Struct* params, script::Script* script);                // 0x0044B740
bool cfunc_get_script_string(script::Struct* params, script::Script* script);               // 0x0044B790

// The script warning level (0x006F3534): 0 none, 1 warn, 2 assert (SetScriptWarningLevel).
int& script_warning_level();

// 0x0044A480 (cdecl): FormatText's work (its callers 0x0044BE2D and 0x0044C8F5). The unnamed string of `params` is
// the format: "%x" (one character) and "%%name" (letters, digits, _) stand for the parameter of that name, written
// as text - integers ("%d"; integer_width: "%0*d"; UseCommas: with the language's thousands separators), floats
// (decimalplaces, default 3), strings, pairs and vectors ("(%.3f,%.3f[,%.3f])"), names (their text - the result
// is then 2); "\%" is a literal '%'. The text goes to a shared buffer (0x00696940) and, as textname=<name> and
// HashedLabel=<name> (its checksum), into `results`. Returns 0 with no format or a missing parameter (printed),
// else 1 (2 if a name was written).
int format_text(script::Struct& results, script::Struct* params);
// FormatText's text buffer (0x00696940; its end, 0x00696D2C, isn't checked).
std::array<char, 1004>& format_text_buffer();

} // namespace commands
