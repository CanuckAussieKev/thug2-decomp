// Script commands (CFunctions) for the script system itself, 0x00449D00-0x0044B7E0 (see script_cfuncs.h).

#include "engine/script/script_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <string_view>

#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/tod_manager.h"
#include <gsl/pointers>
#include "engine/core/format.h"

namespace commands {

namespace {
constexpr float kScriptFrameMilliseconds{16.666666f};
bool g_script_printf_enabled{true};
bool g_first_time_not_called{true};
std::array<char, 1004> g_format_text_buffer;
script::Component g_format_text_value;
int g_script_warning_level;

using script::Component;
using script::SymbolTableEntry;

constexpr uint32_t kGlobalName = 0x60F6F242;    // (a guess: "globalname") Change's name of the global
constexpr uint32_t kNewValue = 0x4B40630D;      // "newValue"
constexpr uint32_t kResolve = 0x991A7BC3;       // "resolve"
constexpr uint32_t kRealTime = 0xC847F2B9;      // (name unknown) Wait: the game clock, not frame time
constexpr uint32_t kSeconds = 0xD029F619;       // "seconds"
constexpr uint32_t kSecond = 0x49E0EE96;        // "second"
constexpr uint32_t kGameFrame = 0xFC37DF05;     // "gameframe"
constexpr uint32_t kGameFrames = 0xB99AE3D6;    // "gameframes"
constexpr uint32_t kGame = 0xDCD4CE73;          // "game"
constexpr uint32_t kFrame = 0x4A07C332;         // "frame"
constexpr uint32_t kFrames = 0x019176C5;        // "frames"
constexpr uint32_t kOnePerFrame = 0xECAA3345;   // "one_per_frame"
constexpr uint32_t kA = 0x174841BC;             // "A"
constexpr uint32_t kB = 0x8E411006;             // "B"
constexpr uint32_t kIndex = 0x7F8C98FE;         // "index"
constexpr uint32_t kString = 0x61414D56;        // "string"
constexpr uint32_t kName = 0xA1DC81F9;          // "name"
constexpr uint32_t kType = 0x7321A8D6;          // "type"
constexpr uint32_t kTextName = 0xFF0DB407;      // "textname"
constexpr uint32_t kHashFieldName = 0x76FA7A8F; // "HashedLabel"
constexpr uint32_t kUseCommas = 0xF138085F;     // "UseCommas"
constexpr uint32_t kIntegerWidth = 0xADA5B338;  // "integer_width"
constexpr uint32_t kDecimalPlaces = 0xD8E2E09F; // "decimalplaces"
constexpr uint32_t kStructure = 0x90FEC815;     // "Structure"
constexpr uint32_t kStruct = 0x456D28D1;        // "struct"
constexpr uint32_t kUntilEvent = 0x2C3A27A4;    // "UntilEvent"
constexpr uint32_t kEx = 0xF8728BEC;            // "ex"
constexpr uint32_t kScr = 0xA6D2D890;           // "scr"
constexpr uint32_t kGroup = 0x923FBB3A;         // "Group"
constexpr uint32_t kDefaultGroup = 0x1CA1FF20;  // "default"
constexpr uint32_t kException = 0x80367192;     // "Exception"
constexpr uint32_t kParams = 0x7031F10C;        // "params"
constexpr uint32_t kLevel = 0x651533EC;         // "level"
constexpr uint32_t kLevelNone = 0x806FFF30;     // "none"
constexpr uint32_t kLevelWarn = 0xD6454EFA;     // (a guess: "warn")
constexpr uint32_t kLevelAssert = 0x4E10B054;   // (a guess: "assert")

// Whether FirstTimeThisIsCalled hasn't been called yet (0x0067D6D5, true at start).
bool& first_time_not_called() { return g_first_time_not_called; }

// Whether the scripts' printf prints (0x0067D6D4).
bool& script_printf_enabled() { return g_script_printf_enabled; }

// The value FormatText is writing (0x00696D2C, a component of its own: its flag bit is kept).
Component& format_text_value() { return g_format_text_value; }

// IfDebugOn / IfDebugOff's work on the calling script - compiled out of this build (an empty call, 0x005480A0).
inline void set_script_debug(script::Script* /*script*/) {}

// `text` appended at `used` of the buffer (with its terminator); returns the new end. (A text that does not fit ends
// the game with a report; the original wrote past the buffer.)
size_t get_append(std::array<char, 1004>& buffer, size_t used, const char* text)
{
    for (const char c : std::string_view(text))
        ::core::at(buffer, used++) = c;
    ::core::at(buffer, used) = '\0';
    return used;
}

// _ftol: truncated towards zero, through 64 bits.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// Wait `ms` milliseconds of the game clock or of frame time.
void wait_ms(script::Script* script, bool real_time, int ms)
{
    if (real_time)
        script->wait_real_time(ms);
    else
        script->lull(ms);
}

// GlobalExists' type names: the symbol type each stands for (0 for any other name).
script::QbValueKind type_from_name(uint32_t name)
{
    switch (name) {
    case 0x90FEC815: // "Structure"
    case 0x456D28D1: // "struct"
        return script::kQbvalStruct;
    case 0x21902065: // "checksum"
    case 0xA1DC81F9: // "name"
        return script::kQbvalName;
    case 0x365AA16A: // "float"
        return script::kQbvalFloat;
    case 0x5EF31148: // "array"
        return script::kQbvalArray;
    case 0x61414D56: // "string"
        return script::kQbvalString;
    case 0xE491B7A4: // "vector"
        return script::kQbvalVector;
    case 0xA5F054F4: // "Integer"
    case 0xEBAE254E: // "int"
        return script::kQbvalInteger;
    case 0xF6A5E196: // "pair"
        return script::kQbvalPair;
    default:
        return script::kQbvalNone;
    }
}

} // namespace

std::array<char, 1004>& format_text_buffer() { return g_format_text_buffer; }
int& script_warning_level() { return g_script_warning_level; }

// 0x0044A480
int format_text(script::Struct* results, script::Struct* params)
{
    uint32_t text_name = 0;
    params->read_hash_value(kTextName, &text_name, 0);
    uint32_t name_key_hash = 0;
    params->read_hash_value(kHashFieldName, &name_key_hash, 0);
    int result = 1;
    const char* format = "";
    if (!params->fetch_text(0u, &format, 0)) {
        debug::printf("Expected text format parameters to contain a format string\n");
        return 0;
    }
    std::array<char, 1004>& buffer = format_text_buffer();
    size_t used = 0;
    const std::string_view source(format);
    size_t p = 0;
    // (the format's character at i; its terminator and, for a lone '%' at the end, what the original read past it: 0)
    auto ch = [&](size_t i) { return i < source.size() ? ::core::at(source, i) : '\0'; };
    while (ch(p)) {
        if (ch(p) == '\\' && ch(p + 1) == '%') {
            ::core::at(buffer, used++) = '%';
            p += 2;
            continue;
        }
        if (ch(p) != '%') {
            ::core::at(buffer, used++) = ch(p++);
            continue;
        }
        // a parameter: "%x" or "%%name"
        std::array<char, 0x2C> name{};
        size_t n = 0;
        const char key = ch(p + 1);
        p += 2;
        if (key == '%') {
            while ((ch(p) >= 'a' && ch(p) <= 'z') || (ch(p) >= 'A' && ch(p) <= 'Z') || (ch(p) >= '0' && ch(p) <= '9') ||
                   ch(p) == '_')
                ::core::at(name, n++) = ch(p++);
        } else {
            ::core::at(name, n++) = key;
        }
        ::core::at(name, n) = '\0';
        Component* found = params->find_last_component(checksums::hash_from_text(name.data()));
        if (!found) {
            debug::printf("Missing text formatting parameter '%s'\n", name.data());
            return 0;
        }
        Component& value = format_text_value();
        value.type_and_flag_ = static_cast<uint8_t>((found->type_and_flag_ & ~1) | (value.type_and_flag_ & 1));
        value.value.set_word(found->value.word());
        script::evaluate_name_token(&value);
        std::array<char, 100> text{}; // (the original's sprintf into it is unbounded)
        switch (value.kind_tag()) {
        case script::kQbvalInteger:
            if (params->has_bare_flag(kUseCommas)) {
                text::format_to(text, "%s", text::format_with_separators(value.value.integer()));
            } else {
                int width = 0;
                if (params->fetch_int_value(kIntegerWidth, &width, 0))
                    text::format_to(text, "%0*d", width, value.value.integer());
                else
                    text::format_to(text, "%d", value.value.integer());
            }
            used = get_append(buffer, used, text.data());
            break;
        case script::kQbvalFloat: {
            int places = 3;
            params->fetch_int_value(kDecimalPlaces, &places, 0);
            text::format_to(text, "%.*f", places,
                            static_cast<double>(value.value.real())); // (the original: "%.<places>f")
            used = get_append(buffer, used, text.data());
            break;
        }
        case script::kQbvalString:
        case script::kQbvalInlineText:
            for (const char c : std::string_view(value.value.text()))
                ::core::at(buffer, used++) = c;
            break;
        case script::kQbvalPair:
            text::format_to(text, "(%.3f,%.3f)", static_cast<double>(value.value.pair()->x),
                            static_cast<double>(value.value.pair()->y));
            used = get_append(buffer, used, text.data());
            break;
        case script::kQbvalVector:
            text::format_to(text, "(%.3f,%.3f,%.3f)", static_cast<double>(value.value.vector()->x),
                            static_cast<double>(value.value.vector()->y), static_cast<double>(value.value.vector()->z));
            used = get_append(buffer, used, text.data());
            break;
        case script::kQbvalName:
            result = 2;
            text::format_to(text, "%s", script::find_checksum_name_or_unknown(value.value.word()));
            used = get_append(buffer, used, text.data());
            break;
        default:
            break;
        }
    }
    ::core::at(buffer, used) = '\0';
    if (text_name)
        results->append_word(text_name, buffer.data());
    if (name_key_hash)
        results->put_name_key(name_key_hash, checksums::hash_from_text(buffer.data()));
    return result;
}

// 0x00449D00: Change <name>=<value> (or globalname=<name> newValue=<value>) [resolve]: the global is made again
// with the new value (its +0x08 word kept). Integers, floats and names are stored as they are, structures and arrays
// copied; with `resolve`, a name standing for another global takes that global's value. Any other type leaves the
// global made again but empty. False if the name isn't a global, or is a script or a C / member function.
bool cfunc_change(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kGlobalName, &name, 0);
    Component* value = nullptr;
    if (name) {
        value = params->find_component(kNewValue);
        if (!value)
            return false;
    } else {
        value = params->step_to_entry(nullptr);
        if (!value)
            return false;
        name = value->name;
    }
    SymbolTableEntry* old = script::find_symbol(name);
    if (!old || old->type == script::kQbvalScriptBody || old->type == script::kQbvalNativeFunc ||
        old->type == script::kQbvalNativeMethod)
        return false;
    const uint32_t kept = old->unknown8;
    script::clean_up_and_remove_symbol(old);
    SymbolTableEntry* entry = script::create_symbol(name);
    entry->unknown8 = kept;

    // the value in a component of its own (the new component's flag bit kept)
    Component copy;
    script::construct_component(&copy);
    copy.type_and_flag_ = static_cast<uint8_t>((value->type_and_flag_ & ~1) | (copy.type_and_flag_ & 1));
    copy.value.set_word(value->value.word());
    if (copy.kind_tag() == script::kQbvalName && params->has_bare_flag(kResolve)) {
        if (SymbolTableEntry* resolved = script::resolve_symbol(copy.value.word())) {
            copy.type_and_flag_ = static_cast<uint8_t>((resolved->type << 1) | (copy.type_and_flag_ & 1));
            copy.value.set_word(resolved->value.word());
        }
    }
    switch (copy.kind_tag()) {
    case script::kQbvalInteger:
    case script::kQbvalFloat:
    case script::kQbvalName:
        entry->type = copy.kind_tag();
        entry->value.set_word(copy.value.word());
        break;
    case script::kQbvalStruct: {
        entry->type = script::kQbvalStruct;
        gsl::owner<script::Struct*> memory = script::spawn_param_bag();
        script::Struct* structure = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        entry->value.set_structure(structure);
        structure->append_structure_if_any(copy.value.structure());
        break;
    }
    case script::kQbvalArray: {
        entry->type = script::kQbvalArray;
        gsl::owner<script::Array*> memory = script::alloc_script_array();
        script::Array* array = memory ? memory::construct_in<script::Array>(memory) : nullptr;
        entry->value.set_array(array);
        script::duplicate_array(array, copy.value.array());
        break;
    }
    default:
        break;
    }
    return true;
}

// 0x0044A0D0: Wait <n> [seconds | second | gameframe(s) | game | frame(s) | one_per_frame] [0xC847F2B9]: seconds
// (times 1000) and frames (times 16.666666) wait as milliseconds, of the game clock with the 0xC847F2B9 flag or of
// frame time otherwise; game frames count Updates; one_per_frame counts Updates one script a frame; a bare number is
// milliseconds. Each count truncated.
bool cfunc_wait(script::Struct* params, script::Script* script)
{
    float value = 0.0f;
    params->fetch_float(0u, &value, 0);
    const bool real_time = params->has_bare_flag(kRealTime);
    if (params->has_bare_flag(kSeconds) || params->has_bare_flag(kSecond)) {
        wait_ms(script, real_time, truncate(static_cast<double>(value) * gfx::kThousand)); // 1000
    } else if (params->has_bare_flag(kGameFrame) || params->has_bare_flag(kGameFrames) ||
               params->has_bare_flag(kGame)) {
        script->get_stall(truncate(value));
    } else if (params->has_bare_flag(kFrame) || params->has_bare_flag(kFrames)) {
        wait_ms(script, real_time, truncate(static_cast<double>(value) * kScriptFrameMilliseconds)); // 16.67
    } else if (params->has_bare_flag(kOnePerFrame)) {
        script->suspend_until_next_frame(truncate(value));
    } else {
        wait_ms(script, real_time, truncate(value));
    }
    return true;
}

// 0x0044A200: IntegerEquals A=<int> B=<int> (missing: 0).
bool cfunc_integer_equals(script::Struct* params, script::Script*)
{
    int a = 0;
    params->fetch_int_value(kA, &a, 0);
    int b = 0;
    params->fetch_int_value(kB, &b, 0);
    return a == b;
}

// 0x0044A260: ChecksumEquals A=<name> B=<name> (missing: 0).
bool cfunc_checksum_equals(script::Struct* params, script::Script*)
{
    uint32_t a = 0;
    params->read_hash_value(kA, &a, 0);
    uint32_t b = 0;
    params->read_hash_value(kB, &b, 0);
    return a == b;
}

// 0x0044A2C0: StringEquals A="..." B="...": compared by checksum (so case is ignored). Both asserted (missing: the
// original's pointer is undefined - here "").
bool cfunc_string_equals(script::Struct* params, script::Script*)
{
    const char* a = "";
    const char* b = "";
    params->fetch_text(kA, &a, 1);
    params->fetch_text(kB, &b, 1);
    const uint32_t checksum_b = checksums::hash_from_text(b);
    return checksums::hash_from_text(a) == checksum_b;
}

// 0x0044A320: SetScriptString index=<n> (0) string="..." ("").
bool cfunc_set_script_string(script::Struct* params, script::Script*)
{
    int index = 0;
    params->fetch_int_value(kIndex, &index, 0);
    const char* string = "";
    params->fetch_text(kString, &string, 0);
    script::set_script_string(index, string);
    return true;
}

// 0x0044A380: GlobalExists name=<name> [type=<type name>]: whether the global exists with that type (Structure /
// struct, checksum / name, float, array, string, vector, Integer / int, pair; no or another type name: type 0,
// which no global has).
bool cfunc_global_exists(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 0);
    uint32_t type_name = 0;
    params->read_hash_value(kType, &type_name, 0);
    const script::QbValueKind type = type_from_name(type_name);
    SymbolTableEntry* entry = script::find_symbol(name);
    return entry && entry->type == type;
}

// 0x0044A900: EnableScriptPrintf <0 / 1> (1): the scripts' printf on or off.
bool cfunc_enable_script_printf(script::Struct* params, script::Script*)
{
    int enable = 1;
    params->fetch_int_value(0u, &enable, 0);
    script_printf_enabled() = enable != 0;
    return true;
}

// 0x0044A930: IfDebugOn / IfDebugOff (one function): compiled out - true.
bool cfunc_if_debug_on(script::Struct*, script::Script* script)
{
    set_script_debug(script);
    return true;
}

// 0x0044A940: StructureContains Structure={...} <name>: whether the structure has a component or a flag of that
// name. False without a structure.
bool cfunc_structure_contains(script::Struct* params, script::Script*)
{
    script::Struct* structure = nullptr;
    if (!params->read_sub_struct(kStructure, &structure, 0))
        return false;
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    if (structure->has_entry_named(name))
        return true;
    return structure->has_bare_flag(name);
}

// 0x0044A9B0: ClearStruct struct=<global>: the global structure emptied.
bool cfunc_clear_struct(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kStruct, &name, 1);
    script::read_sub_struct(name)->get_clear();
    return true;
}

// 0x0044A9E0: ScriptExists <name>: whether the name (following names of names) is a script.
bool cfunc_script_exists(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    SymbolTableEntry* entry = script::resolve_symbol(name);
    return entry && entry->type == script::kQbvalScriptBody;
}

// 0x0044AA20: IsArray <value>: whether the unnamed value is an array.
bool cfunc_is_array(script::Struct* params, script::Script*)
{
    script::Array* array = nullptr;
    return params->fetch_list(0u, &array, 0);
}

// 0x0044AA50: Block [type=<event> | UntilEvent=<event>]: the script waits until an event of that type (none: until
// unblocked).
bool cfunc_block(script::Struct* params, script::Script* script)
{
    uint32_t event_type = 0;
    if (!params->is_vacant()) {
        if (!params->read_hash_value(kType, &event_type, 0))
            params->read_hash_value(kUntilEvent, &event_type, 0);
    }
    script->gate_off(event_type);
    return true;
}

// 0x0044AAB0: IsTrue <integer>: whether it is non-zero (missing: false).
bool cfunc_is_true(script::Struct* params, script::Script*)
{
    int value = 0;
    params->fetch_int_value(0u, &value, 0);
    return value != 0;
}

// 0x0044AAE0: CleanUpAndRemoveSymbol <name>: the global freed and taken out of the table. False if there is none.
bool cfunc_clean_up_and_remove_symbol(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 1);
    SymbolTableEntry* entry = script::find_symbol(name);
    if (!entry)
        return false;
    script::clean_up_and_remove_symbol(entry);
    return true;
}

// 0x0044AB20: SetEventHandler ex=<event> scr=<script> [Group=<name> (default)] [Exception] [params={...}]: an
// event handler of the calling script.
bool cfunc_set_event_handler(script::Struct* params, script::Script* script)
{
    uint32_t event_type = 0;
    params->read_hash_value(kEx, &event_type, 1);
    uint32_t handler = 0;
    params->read_hash_value(kScr, &handler, 1);
    uint32_t group = kDefaultGroup;
    params->read_hash_value(kGroup, &group, 0);
    const bool exception = params->has_bare_flag(kException);
    script::Struct* handler_params = nullptr;
    params->read_sub_struct(kParams, &handler_params, 0);
    script->set_event_handler(event_type, handler, group, exception, handler_params);
    return true;
}

// 0x0044ABD0: SetExceptionHandler / SetException (one function): SetEventHandler, always as an exception.
bool cfunc_set_exception_handler(script::Struct* params, script::Script* script)
{
    uint32_t event_type = 0;
    params->read_hash_value(kEx, &event_type, 1);
    uint32_t handler = 0;
    params->read_hash_value(kScr, &handler, 1);
    uint32_t group = kDefaultGroup;
    params->read_hash_value(kGroup, &group, 0);
    script::Struct* handler_params = nullptr;
    params->read_sub_struct(kParams, &handler_params, 0);
    script->set_event_handler(event_type, handler, group, true, handler_params);
    return true;
}

// 0x0044AC70: ResetEventHandlersFromTable <global array> [Group=<name> (default)].
bool cfunc_reset_event_handlers_from_table(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 1);
    script::Array* table = script::fetch_list(name);
    uint32_t group = kDefaultGroup;
    params->read_hash_value(kGroup, &group, 0);
    script->reset_event_handlers_from_table(table, group);
    return true;
}

// 0x0044ACD0: ClearEventHandler <event>.
bool cfunc_clear_event_handler(script::Struct* params, script::Script* script)
{
    uint32_t event_type = 0;
    params->read_hash_value(0u, &event_type, 1);
    script->clear_event_handler(event_type);
    return true;
}

// 0x0044AD00: ClearEventHandlerGroup [<group> (default)].
bool cfunc_clear_event_handler_group(script::Struct* params, script::Script* script)
{
    uint32_t group = kDefaultGroup;
    params->read_hash_value(0u, &group, 0);
    script->clear_event_handler_group(group);
    return true;
}

// 0x0044AD30: OnExceptionRun [<script>] (none: 0 - cleared).
bool cfunc_on_exception_run(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    script->set_on_exception_script(name);
    return true;
}

// 0x0044AD60: OnExitRun [<script>] (none: 0 - cleared).
bool cfunc_on_exit_run(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    script->set_on_exit_script(name);
    return true;
}

// 0x0044AD90: PrintEventHandlerTable.
bool cfunc_print_event_handler_table(script::Struct*, script::Script* script)
{
    script->print_event_handler_table();
    return true;
}

// 0x0044ADA0: StopAllScripts.
bool cfunc_stop_all_scripts(script::Struct*, script::Script*)
{
    script::stop_all_scripts();
    return true;
}

// 0x0044ADB0: FirstTimeThisIsCalled: true the first time only (for the whole game).
bool cfunc_first_time_this_is_called(script::Struct*, script::Script*)
{
    if (!first_time_not_called())
        return false;
    first_time_not_called() = false;
    return true;
}

// 0x0044ADD0: SetScriptWarningLevel [level=none (default) | warn | assert]: 0 / 1 / 2; another name changes nothing.
bool cfunc_set_script_warning_level(script::Struct* params, script::Script*)
{
    uint32_t level = kLevelNone;
    params->read_hash_value(kLevel, &level, 0);
    if (level == kLevelAssert)
        script_warning_level() = 2;
    else if (level == kLevelNone)
        script_warning_level() = 0;
    else if (level == kLevelWarn)
        script_warning_level() = 1;
    return true;
}

// 0x0044AE30: CastToInteger <name>: the calling script's float parameter of that name made an integer (truncated).
// False if there's no name or no such float.
bool cfunc_cast_to_integer(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    if (!params->read_hash_value(0u, &name, 0))
        return false;
    float value = 0.0f;
    if (!script->args_->fetch_float(name, &value, 0))
        return false;
    script->args_->erase_module(name);
    script->args_->put_int(name, truncate(value));
    return true;
}

// 0x0044AEB0: StringToInteger <name>: the calling script's string parameter of that name made an integer (each
// character taken as a digit: c - '0', no checks). False if there's no name or no such string. (The string is read
// after its component was removed - freed memory, as in the original.)
bool cfunc_string_to_integer(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    if (!params->read_hash_value(0u, &name, 0))
        return false;
    const char* string = "";
    if (!script->args_->fetch_text(name, &string, 0))
        return false;
    script->args_->erase_module(name);
    int value = 0;
    for (const char digit : std::string_view(string))
        value = value * 10 + (digit - '0');
    script->args_->put_int(name, value);
    return true;
}

// 0x0044B720: AddParams {...}: the command's parameters added to the calling script's.
bool cfunc_add_params(script::Struct* params, script::Script* script)
{
    script->args_->append_structure_if_any(params);
    return true;
}

// 0x0044B740: erase_module name=<name> (or the unnamed name): taken out of the calling script's parameters.
bool cfunc_remove_component(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    if (!params->read_hash_value(kName, &name, 0))
        params->read_hash_value(0u, &name, 1);
    script->args_->erase_module(name);
    return true;
}

// 0x0044B790: GetScriptString index=<n> (0): string = the script string.
bool cfunc_get_script_string(script::Struct* params, script::Script* script)
{
    int index = 0;
    params->fetch_int_value(kIndex, &index, 0);
    script->args_->append_word(kString, script::get_script_string(index));
    return true;
}

} // namespace commands
