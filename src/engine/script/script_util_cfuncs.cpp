// The engine's general script commands, 0x0044B7E0-0x0044CFFF (see script_util_cfuncs.h).

#include "engine/script/script_util_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>
#include <string_view>
#include <utility>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/format.h"
#include "engine/core/str.h"
#include "engine/maths/random.h"
#include "engine/script/cell.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/script_cfuncs.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <gsl/assert>


namespace commands {

namespace {
constexpr int kDefaultResolution = 1000;
constexpr size_t kResultSize = 100;
constexpr int kDefaultMaxLength = 15;

using script::Array;
using script::Cell;
using script::Component;
using script::Script;
using script::Struct;

constexpr uint32_t kName = 0xA1DC81F9;            // "name"
constexpr uint32_t kInteger = 0xA5F054F4;         // "integer"
constexpr uint32_t kA = 0x174841BC;               // "a"
constexpr uint32_t kB = 0x8E411006;               // "b"
constexpr uint32_t kResolution = 0x022CF075;      // "resolution"
constexpr uint32_t kArraySize = 0x26828C6E;       // "ArraySize"
constexpr uint32_t kIndex = 0x7F8C98FE;           // "index"
constexpr uint32_t kElement = 0xBEBFA1C6;         // "element"
constexpr uint32_t kParams = 0x7031F10C;          // "params"
constexpr uint32_t kMangledId = 0xFBA40626;       // "mangled_id"
constexpr uint32_t kBase = 0x3F4B019E;            // "base"
constexpr uint32_t kSuffixString = 0x94542EEA;    // "SuffixString"
constexpr uint32_t kAppendedId = 0xAFB911C6;      // "appended_id"
constexpr uint32_t kUpperCaseString = 0xFE366B7B; // "UpperCaseString"
constexpr uint32_t kText = 0xC4745838;            // "text"
constexpr uint32_t kMaxLength = 0xA6931595;       // (name unknown) AppendSuffix: the longest result
constexpr uint32_t kSuffix = 0x4A4F7821;          // "suffix"
constexpr uint32_t kResultName = 0x50060694;      // (name unknown) AppendSuffix: the result's name
constexpr uint32_t kDo = 0x62BA3F6A;              // "do"
constexpr uint32_t kTextName = 0xFF0DB407;        // "TextName"
constexpr uint32_t kPrintfText = 0xBC4AC08B;      // "PrintfText"
constexpr uint32_t kIndexArray = 0xAC3A91DE;      // "index_array"
constexpr uint32_t kStruct = 0x456D28D1;          // "struct"
constexpr uint32_t kField = 0xA40ABAA7;           // "field"
constexpr uint32_t kString = 0x61414D56;          // "string"
constexpr uint32_t kCharArray = 0x3C5EC9E9;       // "char_array"
constexpr uint32_t kArrayName = 0x8F61D602;       // "ArrayName"
constexpr uint32_t kNewValue = 0x4B40630D;        // "NewValue"
// add_component_of_type's type for a structure handed over by pointer.


// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

// strcpy, unbounded as the original's inlined copies are.
// (into a buffer: a text longer than it ends the game with a report, where the original overran it)
void copy_text(std::span<char> out, const char* text)
{
    size_t at = 0;
    for (const char c : std::string_view(text)) {
        core::at(out, at++) = c;
    }
    core::at(out, at) = '\0';
}

// 0x00479190: the type-name lookup, compiled out (its "(Hey, don't use this...)" text; the type is ignored).
const char* current_type_name(script::QbValueKind /*type*/) { return "(Hey, don't use GetTypeName on the CD!)"; }

// 0x0044A480 (script_cfuncs.cpp): FormatText's work - the text put together from `params` into `out`.
int format_text_into(Struct* out, Struct* params) { return format_text(*out, params); }

} // namespace

// 0x0044B7E0: RemoveParameter <name>: the script's parameter (every component of that name) and the flag of
// that name removed. Always true.
bool cfunc_remove_parameter(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    Struct* own = script->args().get();
    own->erase_module(name);
    own->remove_flag(name);
    return true;
}

// 0x0044B820: RemoveFlag <flag>: the flag removed from the script's parameters. Always true.
bool cfunc_remove_flag(Struct* params, Script* script)
{
    uint32_t flag = 0;
    params->read_hash_value(0U, &flag, 0);
    script->args()->remove_flag(flag);
    return true;
}

// 0x0044B850: GetRandomValue name=<result> a=<low> b=<high> [integer] [resolution=n (1000)]: with `integer`, a
// random integer from a to b (either order) inclusive; else a random float from a to b in `resolution` steps
// (a + Rnd(resolution) * (b - a) / (resolution - 1)). Put in the script's parameters under `name`. Always true.
bool cfunc_get_random_value(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 0);
    if (params->has_bare_flag(kInteger)) {
        int a = 0;
        int b = 0;
        params->fetch_int_value(kA, &a, 0);
        params->fetch_int_value(kB, &b, 0);
        if (a > b) {
            const int swap = a;
            a = b;
            b = swap;
        }
        script->args()->put_int(checksums::Name{name}, maths::rnd(b - a + 1) + a);
        return true;
    }
    int resolution = kDefaultResolution;
    params->fetch_int_value(kResolution, &resolution, 0);
    float a = 0.0F;
    float b = 0.0F;
    params->fetch_float(kA, &a, 0);
    params->fetch_float(kB, &b, 0);
    const int step = maths::rnd(resolution);
    // x87 at 53 bits: step * (b - a), / (resolution - 1), + a, each rounded to double; then to float
    double value = static_cast<double>(step) * (static_cast<double>(b) - static_cast<double>(a));
    value /= static_cast<double>(resolution - 1);
    value += static_cast<double>(a);
    script->args()->push_float(checksums::Name{name}, static_cast<float>(value));
    return true;
}

// 0x0044B9A0: SizeOf <array>: the array's size (0 without one) as the script's ArraySize. Always true.
bool cfunc_size_of(Struct* params, Script* script)
{
    Array* array = nullptr;
    params->fetch_list(0U, &array, 0);
    const uint32_t size = array ? array->extent() : 0;
    script->args()->add_component_of_type(kArraySize, script::QbValueKind::kInteger, size);
    return true;
}

// 0x0044B9E0: GetNextArrayElement <array> [index=n]: the element after `index` (the first without one, or for a
// negative one) as the script's element, and its index as index (both taken out of the script's parameters
// first). False at the end of the array (or without one). Integers, floats, strings, local strings, structures,
// arrays and names; another type prints an error (and is still true, with only the index set).
bool cfunc_get_next_array_element(Struct* params, Script* script)
{
    Array* array = nullptr;
    params->fetch_list(0U, &array, 0);
    int index = -1;
    script->args()->fetch_int_value(kIndex, &index, 0);
    script->args()->erase_module(kIndex);
    script->args()->erase_module(kElement);
    if (!array) {
        return false;
    }
    const uint32_t size = array->extent();
    if (index < 0) {
        index = 0;
        if (size == 0) {
            return false;
        }
    } else {
        ++index;
        if (index >= ints::as_signed(size)) {
            return false;
        }
    }
    script->args()->put_int(checksums::Name{kIndex}, index);
    Struct* own = script->args().get();
    switch (array->kind_tag()) {
    case script::QbValueKind::kInteger:
        own->put_int(checksums::Name{kElement}, static_cast<int>(array->fetch_part(index)));
        break;
    case script::QbValueKind::kFloat:
        own->push_float(checksums::Name{kElement}, array->fetch_float(index));
        break;
    case script::QbValueKind::kString:
        own->append_word(kElement, array->find_inline_text(index)); // (0x0046CB70: the same element read)
        break;
    case script::QbValueKind::kInlineText:
        own->append_inline_text(kElement, array->fetch_text(index));
        break;
    case script::QbValueKind::kStruct:
        own->put_sub_struct(kElement, array->struct_at(index));
        break;
    case script::QbValueKind::kArray:
        own->append_array_value(kElement, array->array_at(index));
        break;
    case script::QbValueKind::kName:
        own->put_name_key(checksums::Name{kElement}, array->fetch_part(index));
        break;
    default:
        debug::printf("GetNextArrayElement does not support arrays of type '%s'\n",
                      current_type_name(array->kind_tag())); // "... does not support ..."
        break;
    }
    return true;
}

// 0x0044BBD0: GetRandomArrayElement <array>: a random element as the script's element and its index as index
// (both taken out of the script's parameters first). False for no array or an empty one. Integers, floats,
// structures, arrays and names; another type (strings too) prints an error (still true, only the index set).
bool cfunc_get_random_array_element(Struct* params, Script* script)
{
    script->args()->erase_module(kElement);
    script->args()->erase_module(kIndex);
    Array* array = nullptr;
    params->fetch_list(0U, &array, 0);
    if (!array || array->extent() == 0) {
        return false;
    }
    const int index = maths::rnd(static_cast<int>(array->extent()));
    script->args()->put_int(checksums::Name{kIndex}, index);
    Struct* own = script->args().get();
    switch (array->kind_tag()) {
    case script::QbValueKind::kInteger:
        own->put_int(checksums::Name{kElement}, static_cast<int>(array->fetch_part(index)));
        break;
    case script::QbValueKind::kFloat:
        own->push_float(checksums::Name{kElement}, array->fetch_float(index));
        break;
    case script::QbValueKind::kStruct:
        own->put_sub_struct(kElement, array->struct_at(index));
        break;
    case script::QbValueKind::kArray:
        own->append_array_value(kElement, array->array_at(index));
        break;
    case script::QbValueKind::kName:
        own->put_name_key(checksums::Name{kElement}, array->fetch_part(index));
        break;
    default:
        debug::printf("GetRandomArrayElement does not support arrays of type '%s'\n",
                      current_type_name(array->kind_tag())); // "... does not support ..."
        break;
    }
    return true;
}

// 0x0044BD30: GotParam <name>: whether the script's parameters have a component (or flag) of that name.
bool cfunc_got_param(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    return script->args()->has_entry_named(name);
}

// 0x0044BD60: Goto <script> [params={...}]: the script continues as that script (on the same object). Always
// true.
bool cfunc_goto(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    script->bind_script(name, script_params, script->entity().get_pull());
    return true;
}

// 0x0044BDC0: GotoRandomScript [<names>]: the script continues as one of the scripts named in the array (picked at
// random; no parameters). Nothing without a non-empty array of names. Always true.
bool cfunc_goto_random_script(Struct* params, Script* script)
{
    Array* array = nullptr;
    params->fetch_list(0U, &array, 0);
    if (array && array->extent() && array->kind_tag() == script::QbValueKind::kName) {
        objects::Entity* object = script->entity().get_pull();
        const uint32_t name = array->fetch_part(maths::rnd(static_cast<int>(array->extent())));
        script->bind_script(name, nullptr, object);
    }
    return true;
}

// 0x0044BE20: FormatText: the text put together into the script's parameters (0x0044A480). Always true.
bool cfunc_format_text(Struct* params, Script* script)
{
    format_text_into(script->args().get(), params);
    return true;
}

// 0x0044BE40: MangleChecksums a=<checksum or integer> b=<...>: the sum a + b as the script's mangled_id. Always
// true. (Original quirk: a value given as neither is left uninitialised; here 0.)
bool cfunc_mangle_checksums(Struct* params, Script* script)
{
    uint32_t a = 0;
    uint32_t b = 0;
    if (!params->read_hash_value(kA, &a, 0)) {
        params->fetch_int_value(kA, &a, 1);
    }
    if (!params->read_hash_value(kB, &b, 0)) {
        params->fetch_int_value(kB, &b, 1);
    }
    script->args()->put_name_key(checksums::Name{kMangledId}, a + b);
    return true;
}

// 0x0044BEC0: AppendSuffixToChecksum base=<checksum> SuffixString=<text>: the checksum continued with the text
// (as if the name had the text appended) as the script's appended_id. Always true. (Original quirk: missing ones
// are left uninitialised; here 0 / null.)
bool cfunc_append_suffix_to_checksum(Struct* params, Script* script)
{
    uint32_t base = 0;
    const char* suffix = nullptr;
    params->read_hash_value(kBase, &base, 1);
    params->fetch_text(kSuffixString, &suffix, 1);
    script->args()->put_name_key(checksums::Name{kAppendedId}, checksums::fold_text_into_checksum(base, suffix));
    return true;
}

// 0x0044BF20: GetUpperCaseString <text>: the text in capitals as the script's UpperCaseString. Always true.
// (Original quirks: text of 128 characters or more overruns the buffer; no text is read through an
// uninitialised pointer - here null.)
bool cfunc_get_upper_case_string(Struct* params, Script* script)
{
    const char* text = nullptr;
    params->fetch_text(0U, &text, 1);
    constexpr size_t kUpperSize = 0x80;
    std::array<char, kUpperSize> upper{};
    copy_text(upper, text);
    text::to_capitals(upper.data());
    script->args()->append_word(kUpperCaseString, upper.data());
    return true;
}

// 0x0044BFA0: AddFlag <flag>: the flag added to the script's parameters. Always true. (Original quirk: no flag
// leaves the name uninitialised; here 0.)
bool cfunc_add_flag(Struct* params, Script* script)
{
    uint32_t flag = 0;
    params->read_hash_value(0U, &flag, 0);
    script->args()->put_name_key(checksums::Name{0U}, flag);
    return true;
}

// 0x0044BFD0: AppendSuffix text=<text> suffix=<n (1)> [0xA6931595=<longest (15)>] [0x50060694=<result name>]: the
// text with a space and the number appended (sprintf " %d"), the text cut so the result is at most the longest,
// put in the script's parameters under the result name. Always true. (Original quirks: a text of 100 characters
// or more overruns the buffer; a number longer than the limit cuts before the result - into the number's buffer,
// which lies just before it, as here.)
bool cfunc_append_suffix(Struct* params, Script* script)
{
    const char* text = ""; // ""
    params->fetch_text(kText, &text, 0);
    const size_t text_length = std::strlen(text);
    // (the number's 20 characters, then the result's 100 - one buffer: a cut can land in the number's part)
    constexpr size_t kNumberSize = 20;
    std::array<char, kNumberSize + kResultSize> buffer{};
    auto& number = buffer;
    const std::span<char> result = std::span<char>(buffer).subspan(kNumberSize);
    copy_text(result, text);
    int max_length = kDefaultMaxLength;
    params->fetch_int_value(kMaxLength, &max_length, 0);
    int suffix = 1;
    params->fetch_int_value(kSuffix, &suffix, 0);
    get_sprintf(number.data(), " %d", suffix); // " %d"
    const int number_length = static_cast<int>(std::strlen(number.data()));
    if (number_length + static_cast<int>(text_length) > max_length) {
        core::at(buffer, static_cast<int>(kNumberSize) + max_length - number_length) = 0;
    }
    copy_text(result.subspan(std::strlen(result.data())), number.data());
    uint32_t name = 0;
    params->read_hash_value(kResultName, &name, 0);
    script->args()->append_word(name, result.data());
    return true;
}

// 0x0044C120: IsObjectScript: whether the script runs on an object.
bool cfunc_is_object_script(Struct* /*params*/, Script* script) { return script->entity().get_pull() != nullptr; }

// 0x0044C290: ForEachIn <array> do=<script> [params={...}]: the script run (now, on this script's object) for each
// element of the array - with params, on a structure made of the element (a structure; none for null) and those
// params, else on the element itself as its parameters. Always true. (Original quirk: no array crashes.)
bool cfunc_for_each_in(Struct* params, Script* script)
{
    uint32_t each = 0;
    params->read_hash_value(kDo, &each, 0);
    Array* array = nullptr;
    params->fetch_list(0U, &array, 0);
    Struct* extra = nullptr;
    params->read_sub_struct(kParams, &extra, 0);
    if (extra) {
        gsl::owner<Struct*> element_params = script::spawn_param_bag();
        if (element_params) {
            element_params = new (element_params) script::Struct;
        }
        for (uint32_t i = 0; i < array->extent(); ++i) {
            Expects(element_params != nullptr);
            element_params->get_clear();
            if (auto* element = array->struct_at(i)) {
                element_params->merge_structure(element);
            }
            if (extra) {
                element_params->merge_structure(extra);
            }
            script::launch_script(each, element_params, script->entity().get_pull(), false, nullptr);
        }
        if (element_params) {
            element_params->get_clear();
            script::delete_struct(element_params);
        }
        return true;
    }
    for (uint32_t i = 0; i < array->extent(); ++i) {
        objects::Entity* object = script->entity().get_pull();
        script::launch_script(each, array->struct_at(i), object, false, nullptr);
    }
    return true;
}

// 0x0044C430: fetch_part <array> Index=n: element n as the script's element (taken out first): integers, a copy of
// a structure (handed over), arrays (copied); another type prints 0x00647110 (still true). False if the index is
// outside the array; true without an array.
bool cfunc_get_element(Struct* params, Script* script)
{
    Array* array = nullptr;
    params->fetch_list(0U, &array, 0);
    int index = 0;
    params->fetch_int_value("Index", &index, 0); // "Index"
    script->args()->erase_module(kElement);
    if (!array) {
        return true;
    }
    if (index < 0 || index >= ints::as_signed(array->extent())) {
        return false;
    }
    switch (array->kind_tag()) {
    case script::QbValueKind::kInteger:
        script->args()->add_component_of_type(kElement, script::QbValueKind::kInteger, array->fetch_part(index));
        break;
    case script::QbValueKind::kStruct: {
        gsl::owner<Struct*> copy = script::spawn_param_bag();
        if (copy) {
            copy = new (copy) script::Struct;
        }
        Expects(copy != nullptr);
        copy->append_structure_if_any(array->struct_at(index));
        script->args()->append_struct_ref(kElement, copy);
        break;
    }
    case script::QbValueKind::kArray:
        script->args()->append_array_value(kElement, array->array_at(index));
        break;
    default:
        debug::printf(
            "GetElement currently only supports arrays of structures, arrays or integers ... \n(Requested type=%d)\n",
            array->kind_tag()); // "... only supports ..."
        break;
    }
    return true;
}

// 0x0044C5A0: PermuteArray Array=<array> (NewArrayName=<name> | PermuteSource) [MakeNewFirstDifferFromOldLast]:
// the elements shuffled (size * 10 random swaps) - in a copy put in the script's parameters under the new name, or
// with PermuteSource in the array itself. With MakeNewFirstDifferFromOldLast, a first element equal to the old last
// one is swapped with a random other. False without the array or (making a copy) a name.
bool cfunc_permute_array(Struct* params, Script* script)
{
    Array* source = nullptr;
    params->fetch_list("Array", &source, 0); // "Array"
    if (!source) {
        return false;
    }
    const bool make_copy = !params->has_bare_flag("PermuteSource"); // "PermuteSource"
    uint32_t new_name = 0;
    gsl::owner<Array*> copy = nullptr;
    Array* array = source;
    if (make_copy) {
        params->read_hash_value("NewArrayName", &new_name, 0); // "NewArrayName"
        if (!new_name) {
            return false;
        }
        gsl::owner<Array*> made = script::new_array(); // (the script's parameters take it below)
        array = made;
        copy = made;
        script::duplicate_array(array, source);
    }
    const int size = static_cast<int>(array->extent());
    if (size) {
        const std::span<Cell> elements = array->cells();
        const uint32_t old_last = core::at(elements, size - 1).word();
        for (int swaps = size * script::kShuffleSwapsPerChoice; swaps > 0; --swaps) {
            const int a = maths::rnd(size);
            const int b = maths::rnd(size);
            std::swap(core::at(elements, a), core::at(elements, b));
        }
        if (params->has_bare_flag("MakeNewFirstDifferFromOldLast") && size > 1 && core::at(elements, 0).word() == old_last) {
            // "MakeNewFirstDifferFromOldLast"
            const int other = maths::rnd(size - 1) + 1;
            std::swap(core::at(elements, 0), core::at(elements, other));
        }
    }
    if (make_copy) {
        script->args()->append_array_ref(new_name, copy);
    }
    return true;
}

// 0x0044C730: GotoPreserveParams <script> [params={...}]: Goto, the script's own parameters (and the params) going
// with it. Always true.
bool cfunc_goto_preserve_params(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    gsl::owner<Struct*> script_params = script::spawn_param_bag();
    if (script_params) {
        script_params = new (script_params) script::Struct;
    }
    Expects(script_params != nullptr);
    script_params->append_structure_if_any(script->args().get());
    Struct* extra = nullptr;
    params->read_sub_struct(kParams, &extra, 0);
    script_params->append_structure_if_any(extra);
    script->bind_script(name, script_params, script->entity().get_pull());
    if (script_params) {
        script_params->get_clear();
        script::delete_struct(script_params);
    }
    return true;
}

// 0x0044C840
bool format_printf_text(char* out, Struct* params, Script* /*script*/)
{
    const char* text = "Default printf text"; // "Default printf text"
    if (params->fetch_text(0U, &text, 0)) {
        gsl::owner<Struct*> format = script::spawn_param_bag();
        if (format) {
            format = new (format) script::Struct;
        }
        Expects(format != nullptr);
        format->put_name_key(checksums::Name{kTextName}, kPrintfText);
        format->append_structure_if_any(params);
        if (format_text_into(format, format)) {
            format->fetch_text("PrintfText", &text, 0); // "PrintfText"
        } else {
            text = "";                  // ""
        }
        get_sprintf(out, "%s\n", text); // "%s\n"
        if (format) {
            format->get_clear();
            script::delete_struct(format);
        }
        return true;
    }
    int value = 0;
    if (params->fetch_int_value(0U, &value, 0)) {
        get_sprintf(out, "%d\n", value); // "%d\n"
    }
    return true;
}

// 0x0044C9B0: Dump <name>: the script's parameter of that name written up ("name=%a" with a copy of it as a, or
// "name=[!! DOES NOT EXIST !!]") for printing - the printing is compiled out, so it's only made and freed. Always
// true. (Original quirk: no name leaves it uninitialised; here 0.)
bool cfunc_dump(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 1);
    gsl::owner<Struct*> dump = script::spawn_param_bag();
    if (dump) {
        dump = new (dump) script::Struct;
    }
    constexpr size_t kTextSize = 0x400;
    std::array<char, kTextSize> text{};
    Component* component = script->args()->find_component(name);
    if (!component) {
        get_sprintf(text.data(), "%s=[!! DOES NOT EXIST !!]", script::find_checksum_name_or_unknown(name));
    } else {
        get_sprintf(text.data(), "%s=%%a", script::find_checksum_name_or_unknown(name)); // "%s=%%a"
        gsl::owner<Component*> value = script::new_component();
        script::duplicate_component(value, component, 0);
        value->name = kA;
        Expects(dump != nullptr);
        dump->attach_module(value);
    }
    Expects(dump != nullptr);
    dump->append_word(0U, text.data());
    if (dump) {
        dump->get_clear();
        script::delete_struct(dump);
    }
    return true;
}

// 0x0044CB70: ScriptAssert <text or integer>: the message made (format_printf_text); the assert itself is compiled
// out. Always true.
bool cfunc_script_assert(Struct* params, Script* script)
{
    constexpr size_t kMessageSize = 0x400;
    std::array<char, kMessageSize> message{};
    format_printf_text(message.data(), params, script);
    return true;
}

// 0x0044CBC0: CreateIndexArray <n>: the array 0, 1 .. n-1 as the script's index_array. Always true. (Original
// quirk: no n leaves it uninitialised; here 0.)
bool cfunc_create_index_array(Struct* params, Script* script)
{
    int size = 0;
    params->fetch_int_value(0U, &size, 1);
    gsl::owner<Array*> indexes = script::alloc_script_array();
    if (indexes) {
        indexes = new (indexes) script::Array;
    }
    Expects(indexes != nullptr);
    indexes->configure_size_kind(static_cast<uint32_t>(size), script::QbValueKind::kInteger);
    for (int i = 0; i < size; ++i) {
        indexes->set_element(i, static_cast<uint32_t>(i));
    }
    script->args()->append_array_value(kIndexArray, indexes);
    script::release_array_storage(indexes);
    if (indexes) {
        indexes->get_clear();
        script::delete_array(indexes);
    }
    return true;
}

// 0x0044CCC0: AppendStruct struct=<global structure> field=<name> params={...}: a copy of params added to the
// global structure as `field`. Always true. (Original quirks: missing names are uninitialised - here 0 - and a
// missing global structure crashes.)
bool cfunc_append_struct(Struct* params, Script* /*script*/)
{
    uint32_t block_name = 0;
    uint32_t field = 0;
    Struct* field_params = nullptr;
    params->read_hash_value(kStruct, &block_name, 1);
    params->read_hash_value(kField, &field, 1);
    params->read_sub_struct(kParams, &field_params, 1);
    gsl::owner<Struct*> copy = script::spawn_param_bag();
    if (copy) {
        copy = new (copy) script::Struct;
    }
    Expects(copy != nullptr);
    copy->append_structure_if_any(field_params);
    script::read_sub_struct(block_name)->append_struct_ref(field, copy);
    return true;
}

// 0x0044CDB0: StringToCharArray string=<text>: an array of one-character strings as the script's char_array.
// Always true.
bool cfunc_string_to_char_array(Struct* params, Script* script)
{
    const char* string = nullptr;
    params->fetch_text(kString, &string, 1);
    auto length = static_cast<uint32_t>(std::strlen(string));
    gsl::owner<Array*> characters = script::alloc_script_array();
    if (characters) {
        characters = new (characters) script::Array;
    }
    Expects(characters != nullptr);
    characters->configure_size_kind(length, script::QbValueKind::kString);
    std::array<char, 2> one{};
    core::at(one, 1) = '\0';
    const std::string_view letters(string);
    for (uint32_t i = 0; i < length; ++i) {
        core::at(one, 0) = core::at(letters, i);
        characters->set_text(i, script::spawn_text(one.data()));
    }
    script->args()->append_array_ref(kCharArray, characters);
    return true;
}

// 0x0044CEC0: SetArrayElement ArrayName=<the script's array> index=n NewValue=<value>: element n of the script's
// array set to a copy of the value (a name naming a global resolved first); what the element owned before
// (string, pair, vector, structure, array) is freed. Float arrays take integers too; other array types are left
// alone. Always true. (Original quirks: missing parameters crash or use uninitialised values; the value isn't
// checked against the array's type.)
bool cfunc_set_array_element(Struct* params, Script* script)
{
    uint32_t array_name = 0;
    params->read_hash_value(kArrayName, &array_name, 1);
    Array* array = nullptr;
    script->args()->fetch_list(array_name, &array, 1);
    int index = 0;
    params->fetch_int_value(kIndex, &index, 1);
    Component* new_value = params->find_last_component(kNewValue);
    script::evaluate_name_token(new_value);
    gsl::owner<Component*> value = script::new_component();
    script::duplicate_component(value, new_value, 0);
    bool store = true; // the element becomes the value (whose own copy is handed over)
    switch (array->kind_tag()) {
    case script::QbValueKind::kInteger:
    case script::QbValueKind::kName:
        array->set_element(index, value->value.word());
        store = false;
        break;
    case script::QbValueKind::kFloat:
        if (value_kind(*value) == script::QbValueKind::kFloat) {
            array->change_float(index, value->value.real());
        } else {
            array->change_float(index, static_cast<float>(value->value.integer()));
        }
        store = false;
        break;
    case script::QbValueKind::kString:
    case script::QbValueKind::kInlineText:
        script::free_string(array->find_inline_text(index));
        break;
    case script::QbValueKind::kPair:
        script::free_pair(array->pair_at(index));
        break;
    case script::QbValueKind::kVector:
        script::free_vector(array->vector_at(index));
        break;
    case script::QbValueKind::kStruct:
        if (auto* old = array->struct_at(index)) {
            old->get_clear();
            script::delete_struct(old);
        }
        break;
    case script::QbValueKind::kArray: {
        auto* old = array->array_at(index);
        script::release_array_storage(old);
        if (old) {
            old->get_clear();
            script::delete_array(old);
        }
        break;
    }
    default:
        store = false;
        break;
    }
    if (store) {
        array->set_cell(index, value->value);
    }
    value->value.clear();
    script::free_component(value);
    return true;
}

} // namespace commands
