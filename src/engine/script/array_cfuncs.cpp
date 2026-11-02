// Script commands (CFunctions) for arrays, 0x0044AF50-0x0044B530 (see array_cfuncs.h).

#include "engine/script/array_cfuncs.h"

#include <cstdint>
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"

namespace commands {

namespace {

using script::Array;
using script::Struct;

constexpr uint32_t kArray = 0x5EF31148;     // "array"
constexpr uint32_t kContains = 0x71059581;  // "contains"
constexpr uint32_t kIndex1 = 0x0BA453B9;    // "Index1"
constexpr uint32_t kIndex2 = 0x92AD0203;    // "index2"
constexpr uint32_t kIndex3 = 0xE5AA3295;    // "index3"
constexpr uint32_t kArraySize = 0x7F5B3D4D; // "array_size"
constexpr uint32_t kArrayName = 0x8F61D602; // "arrayName"
constexpr uint32_t kIndices = 0xFFC8E484;   // "indices"
constexpr uint32_t kVal = 0x6820459A;       // "val"

Array* element_array(Array* array, uint32_t index) { return array->array_at(index); }

// The array named `name`: one in the calling script's parameters, else the global one.
Array* find_array(script::Script* script, uint32_t name)
{
    Array* array = nullptr;
    if (!script->args_->fetch_list(name, &array, 0))
        array = script::fetch_list(name);
    return array;
}

// `array`'s element `index` added to `results` as val, by the array's type (strings through find_inline_text; types
// with no case - scripts, functions... - add nothing).
void add_element(Struct* results, Array* array, uint32_t index)
{
    switch (array->kind_tag_) {
    case script::kQbvalInteger:
        results->put_int(kVal, static_cast<int>(array->fetch_part(index)));
        break;
    case script::kQbvalFloat:
        results->push_float(kVal, array->fetch_float(index));
        break;
    case script::kQbvalString:
    case script::kQbvalInlineText:
        results->append_word(kVal, array->find_inline_text(index));
        break;
    case script::kQbvalPair: {
        auto* pair = array->pair_at(index);
        results->insert_pair(kVal, pair->x, pair->y);
        break;
    }
    case script::kQbvalVector: {
        auto* vector = array->vector_at(index);
        results->put_vec3(kVal, vector->x, vector->y, vector->z);
        break;
    }
    case script::kQbvalStruct:
        results->put_sub_struct(kVal, array->struct_at(index));
        break;
    case script::kQbvalArray:
        results->append_array_value(kVal, element_array(array, index));
        break;
    case script::kQbvalName:
        results->put_name_key(kVal, array->fetch_part(index));
        break;
    default:
        break;
    }
}

} // namespace

// 0x0044AF50: ArrayContains array=[...] contains=<value>: whether an element equals the value - integer arrays by
// value, string arrays ignoring case, name arrays by checksum (any other array: false).
bool cfunc_array_contains(Struct* params, script::Script*)
{
    Array* array = nullptr;
    params->fetch_list(kArray, &array, 1);
    const script::QbValueKind type = array->kind_tag_;
    for (uint32_t i = 0; i < array->extent_; ++i) {
        switch (type) {
        case script::kQbvalInteger: {
            int value = 0;
            params->fetch_int_value(kContains, &value, 1);
            if (static_cast<uint32_t>(value) == array->fetch_part(i))
                return true;
            break;
        }
        case script::kQbvalString: {
            const char* value = "";
            params->fetch_text(kContains, &value, 1);
            if (_stricmp(value, array->find_inline_text(i)) == 0)
                return true;
            break;
        }
        case script::kQbvalName: {
            uint32_t value = 0;
            params->read_hash_value(kContains, &value, 1);
            if (value == array->fetch_part(i))
                return true;
            break;
        }
        default:
            break;
        }
    }
    return false;
}

// 0x0044B030: GetArraySize <array> [Index1=i [index2=j [index3=k]]]: array_size = the size of the array, or of the
// element array the indices lead to.
bool cfunc_get_array_size(Struct* params, script::Script* script)
{
    Array* array = nullptr;
    params->fetch_list(0u, &array, 1);
    int index = 0;
    if (params->fetch_int_value(kIndex1, &index, 0)) {
        array = element_array(array, static_cast<uint32_t>(index));
        if (params->fetch_int_value(kIndex2, &index, 0)) {
            array = element_array(array, static_cast<uint32_t>(index));
            if (params->fetch_int_value(kIndex3, &index, 0))
                array = element_array(array, static_cast<uint32_t>(index));
        }
    }
    // (NOT ORIGINAL: no array - its parameters had none, or an index led nowhere - is logged before the original's
    // crash on it, with the script's name: e.g. a converted level's script naming an array THUG2 hasn't)
    if (!array)
        get_log("GetArraySize: no array in %s's parameters (or an index leading to none) - the next line crashes",
                script::find_checksum_name_or_unknown(script->script_title_));
    script->args_->put_int(kArraySize, static_cast<int>(array->extent_));
    return true;
}

// 0x0044B0F0: Get2DArrayData arrayName=<name> Index1=i index2=j: val = array[i][j] (the array the calling script's
// own parameters name, else the global).
bool cfunc_get_2d_array_data(Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kArrayName, &name, 1);
    Array* array = find_array(script, name);
    int i = 0;
    params->fetch_int_value(kIndex1, &i, 1);
    int j = 0;
    params->fetch_int_value(kIndex2, &j, 1);
    add_element(script->args_.get(), element_array(array, static_cast<uint32_t>(i)), static_cast<uint32_t>(j));
    return true;
}

// 0x0044B300: Get3DArrayData arrayName=<name> Index1=i index2=j index3=k: val = array[i][j][k].
bool cfunc_get_3d_array_data(Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kArrayName, &name, 1);
    Array* array = find_array(script, name);
    int i = 0;
    params->fetch_int_value(kIndex1, &i, 1);
    int j = 0;
    params->fetch_int_value(kIndex2, &j, 1);
    int k = 0;
    params->fetch_int_value(kIndex3, &k, 1);
    Array* plane = element_array(element_array(array, static_cast<uint32_t>(i)), static_cast<uint32_t>(j));
    add_element(script->args_.get(), plane, static_cast<uint32_t>(k));
    return true;
}

// 0x0044B530: GetNDArrayData arrayName=<name>: val = the element the calling script's own `indices` array leads
// to (read from the script's parameters, not the command's). An empty indices array runs off the end, as in the
// original.
bool cfunc_get_nd_array_data(Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kArrayName, &name, 1);
    Array* array = find_array(script, name);
    Array* indices = nullptr;
    script->args_->fetch_list(kIndices, &indices, 1);
    uint32_t i = 0;
    for (; i < indices->extent_ - 1; ++i)
        array = element_array(array, indices->fetch_part(i));
    add_element(script->args_.get(), array, indices->fetch_part(i));
    return true;
}

} // namespace commands
