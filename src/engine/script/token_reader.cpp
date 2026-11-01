#include "engine/script/token_reader.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/maths/random.h"
#include "engine/script/expression.h"
#include "engine/script/qb.h"
#include "engine/script/cscript.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/movie.h"
#include "engine/sys/startup_init.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag

namespace script {

float g_parse_nudge{0.1f};
namespace {
float g_parse_random_scale{0.000030525032f};
} // namespace
StoredRandom* g_stored_randoms_newest;
namespace {
StoredRandom* g_stored_randoms_oldest;
int g_stored_random_count;
} // namespace
ValueResolver g_evaluator;
namespace {
Component g_parse_scratch;

float get_as_float(uint32_t bits)
{
    auto value = std::bit_cast<float>(bits);
    return value;
}

void change_species(Component* component, uint32_t type)
{
    component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (type << 1));
}

const uint8_t* skip_end_of_lines_in(const uint8_t* p)
{
    for (;;) {
        if (*p == 0x01)
            p += 1;
        else if (*p == 0x02)
            p += 5;
        else
            return p;
    }
}

Struct* new_init_struct()
{
    Struct* structure = spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}
Array* new_init_array()
{
    Array* array = alloc_script_array();
    return array ? memory::construct_in<script::Array>(array) : nullptr;
}

// The evaluator expressions are parsed with, and the component values go through on the way.
ValueResolver& get_evaluator() { return g_evaluator; }
Component& get_scratch() { return g_parse_scratch; }

// The stored randoms: newest first (next = older), at most 100.
StoredRandom*& stored_randoms_newest() { return g_stored_randoms_newest; }
StoredRandom*& stored_randoms_oldest() { return g_stored_randoms_oldest; }
int& stored_random_count() { return g_stored_random_count; }

// An expression parameter of a structure / call: merged if it gives a structure, added (unnamed) otherwise.
const uint8_t* add_expression(const uint8_t* token, Struct* dest, Struct* args)
{
    Component* value = new_component();
    token = parse_expression(token, args, value);
    if (value->kind_tag() == kQbvalStruct) {
        dest->append_structure_if_any(value->structure);
        release_component(value);
        free_component(value);
    } else if (value->kind_tag() != kQbvalNone) {
        value->name = 0;
        dest->attach_module(value);
    } else {
        free_component(value);
    }
    return token;
}

// A `name = (expression)` parameter.
const uint8_t* add_named_expression(const uint8_t* token, uint32_t name, Struct* dest, Struct* args)
{
    Component* value = new_component();
    token = parse_expression(token, args, value);
    if (value->type_and_flag_ & 0xFE) {
        value->name = name;
        dest->attach_module(value);
    } else {
        free_component(value);
    }
    return token;
}

} // namespace

// 0x004728C0
Script* get_running_script() { return g_running_script; }

// 0x004728D0
Script* step_script_list(Script* script) { return script ? *reinterpret_cast<Script**>(script) : g_script_list_head; }

// 0x0046F7A0
StoredRandom* look_up_saved_random(const uint8_t* table, uint32_t type, uint32_t count)
{
    auto script = reinterpret_cast<uint32_t>(get_running_script());
    for (StoredRandom* stored = stored_randoms_newest(); stored; stored = stored->succ_) {
        if (stored->lookup_table_ == table && stored->kind_tag_ == type && stored->num_ == count &&
            (stored->owner_ == script || stored->owner_ == 0))
            return stored;
    }
    return nullptr;
}

// 0x00470BB0
StoredRandom* new_stored_random()
{
    if (stored_random_count() >= 100) {
        StoredRandom* oldest = stored_randoms_oldest();
        StoredRandom* newer = oldest->previous_;
        --stored_random_count();
        free_stored_random(oldest);
        stored_randoms_oldest() = newer;
        if (newer)
            newer->succ_ = nullptr;
    }
    auto* stored = g_stored_random_pools.allocate<StoredRandom>();
    if (stored) {
        ++stored_random_count();
        stored->succ_ = nullptr;
        stored->previous_ = nullptr;
    }
    StoredRandom* newest = stored_randoms_newest();
    stored->previous_ = nullptr;
    stored->succ_ = newest;
    stored_randoms_newest() = stored;
    if (newest)
        newest->previous_ = stored;
    else
        stored_randoms_oldest() = stored;
    return stored;
}

// 0x00470C60
const uint8_t* resolve_random_jumps(const uint8_t* token)
{
    for (;;) {
        switch (*token) {
        case 0x2E: // jump
            token += 5 + fetch_word32(token + 1);
            break;
        case 0x2F:   // Random(@a @b ...): weighted
        case 0x37: { // the same, from the second random stream
            const uint32_t count = fetch_word32(token + 1);
            const uint8_t* weights = token + 5;
            int total = 0;
            for (uint32_t i = 0; i < count; ++i)
                total += static_cast<int16_t>(fetch_word16(weights + 2 * i));
            int pick = *token == 0x2F ? maths::rnd(total) : maths::roll_side_stream(total);
            int choice = 0;
            pick -= static_cast<int16_t>(fetch_word16(weights));
            while (pick >= 0) {
                ++choice;
                pick -= static_cast<int16_t>(fetch_word16(weights + 2 * choice));
            }
            const uint8_t* offset = weights + 2 * count + 4 * choice;
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case 0x40: { // RandomNoRepeat
            const uint32_t count = fetch_word32(token + 1);
            const uint8_t* table = token + 5 + 2 * count;
            StoredRandom* stored = look_up_saved_random(table, 0x40, count);
            if (!stored) {
                stored = new_stored_random();
                stored->lookup_table_ = table;
                stored->kind_tag_ = 0x40;
                stored->num_ = static_cast<uint16_t>(count);
                stored->last_ = static_cast<uint32_t>(maths::rnd(static_cast<int>(count)));
            }
            stored->owner_ = reinterpret_cast<uint32_t>(get_running_script());
            int pick = maths::rnd(static_cast<int>(count));
            for (int tries = 0; static_cast<uint16_t>(pick) == stored->last_;) {
                if (++tries > 100)
                    break;
                pick = maths::rnd(static_cast<int>(count));
            }
            const uint32_t choice = static_cast<uint16_t>(pick);
            stored->last_ = choice;
            const uint8_t* offset = table + 4 * choice;
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case 0x41: { // RandomPermute
            const uint32_t count = fetch_word32(token + 1);
            const uint8_t* table = token + 5 + 2 * count;
            StoredRandom* stored = look_up_saved_random(table, 0x41, count);
            if (!stored) {
                stored = new_stored_random();
                stored->lookup_table_ = table;
                stored->kind_tag_ = 0x41;
                stored->num_ = static_cast<uint16_t>(count);
                stored->ordinal_ = 0;
                for (uint16_t i = 0; i < stored->num_; ++i)
                    stored->order_[i] = static_cast<uint8_t>(i);
                stored->shuffle(false);
            }
            stored->owner_ = reinterpret_cast<uint32_t>(get_running_script());
            if (++stored->ordinal_ >= stored->num_) {
                stored->shuffle(true);
                stored->ordinal_ = 0;
            }
            const uint8_t* offset = table + 4 * stored->order_[stored->ordinal_];
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case 0x49: // short jump
            token += 1 + fetch_word16(token + 1);
            break;
        default:
            return token;
        }
    }
}

// 0x0046FD60
const uint8_t* skip_value(const uint8_t* token)
{
    switch (*token) {
    case 0x03:
    case 0x05: {
        const uint8_t open = *token;
        const auto close = static_cast<uint8_t>(open + 1);
        int depth = 1;
        do {
            token = step_over_token(token);
            if (*token == open)
                ++depth;
            else if (*token == close)
                --depth;
        } while (depth);
        return token + 1;
    }
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x1A:
        return token + 5;
    case 0x1B:
    case 0x1C:
        return token + 5 + fetch_word32(token + 1);
    case 0x1E:
        return token + 13;
    case 0x1F:
        return token + 9;
    default:
        return token;
    }
}

// 0x0046FBD0
const uint8_t* parse_random_range(const uint8_t* token, Component* out, bool deterministic)
{
    float a = get_as_float(fetch_word32(token + 1));
    float b = get_as_float(fetch_word32(token + 5));
    // (rounded with the game's 0.1 nudge, as the x87 does it)
    const double nudge = g_parse_nudge;
    const double tolerance = gfx::g_tiny_length;
    auto nearest = [&](float f) {
        return static_cast<int>(f >= 0.0f || std::isnan(f) ? static_cast<double>(f) + nudge
                                                           : static_cast<double>(f) - nudge);
    };
    int low = nearest(a);
    int high = nearest(b);
    if (!(std::fabs(static_cast<double>(a) - low) > tolerance) &&
        !(std::fabs(static_cast<double>(b) - high) > tolerance)) {
        // Whole numbers: an integer in [low, high].
        if (low > high) {
            const int swapped = low;
            low = high;
            high = swapped;
        }
        const int range = high - low + 1;
        const int pick = deterministic ? maths::roll_side_stream(range) : maths::rnd(range);
        out->raw = static_cast<uint32_t>(pick + low);
        change_species(out, kQbvalInteger);
        return token + 9;
    }
    if (a > b) {
        const float swapped = a;
        a = b;
        b = swapped;
    }
    const int pick = deterministic ? maths::roll_side_stream(0x7FF9) : maths::rnd(0x7FF9);
    change_species(out, kQbvalFloat);
    const double scale = g_parse_random_scale;
    out->float_value_ = static_cast<float>((static_cast<double>(b) - a) * pick * scale + a);
    return token + 9;
}

// 0x00479730
void evaluate_name_token(Component* component)
{
    if ((component->type_and_flag_ & 0xFE) != (kQbvalName << 1))
        return;
    const SymbolTableEntry* symbol = resolve_symbol(component->checksum);
    if (!symbol)
        return;
    switch (symbol->type) {
    case kQbvalInteger:
    case kQbvalFloat:
    case kQbvalString:
    case kQbvalInlineText:
    case kQbvalPair:
    case kQbvalVector:
    case kQbvalStruct:
    case kQbvalArray:
    case kQbvalName:
        change_species(component, symbol->type);
        component->raw = symbol->raw;
        break;
    default:
        break;
    }
}

// 0x004706F0
const uint8_t* parse_value(const uint8_t* token, Struct* args, Component* out)
{
    const uint8_t* p = token + 1;
    switch (*token) {
    case 0x03: {
        Struct* structure = new_init_struct();
        p = parse_structure(structure, token, args);
        change_species(out, kQbvalStruct);
        out->structure = structure;
        return p;
    }
    case 0x05: {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
        Array* array = new_init_array();
        memory::leave_heap_scope();
        p = parse_array(array, token, args);
        change_species(out, kQbvalArray);
        out->array = array;
        return p;
    }
    case 0x16:
        change_species(out, kQbvalName);
        out->raw = fetch_word32(p);
        return token + 5;
    case 0x17:
        change_species(out, kQbvalInteger);
        out->raw = fetch_word32(p);
        return token + 5;
    case 0x1A:
        change_species(out, kQbvalFloat);
        out->raw = fetch_word32(p);
        return token + 5;
    case 0x1B:
    case 0x1C: {
        change_species(out, *token == 0x1B ? kQbvalString : kQbvalInlineText);
        const uint32_t length = fetch_word32(p);
        out->string = spawn_text(reinterpret_cast<const char*>(token + 5));
        return token + 5 + length;
    }
    case 0x1E: {
        change_species(out, kQbvalVector);
        uint32_t x = fetch_word32(p), y = fetch_word32(token + 5), z = fetch_word32(token + 9);
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        out->vector = vector;
        std::memcpy(&vector->x, &x, 4);
        std::memcpy(&out->vector->y, &y, 4);
        std::memcpy(&out->vector->z, &z, 4);
        return token + 13;
    }
    case 0x1F: {
        change_species(out, kQbvalPair);
        uint32_t x = fetch_word32(p), y = fetch_word32(token + 5);
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        out->pair = pair;
        std::memcpy(&pair->x, &x, 4);
        std::memcpy(&out->pair->y, &y, 4);
        return token + 9;
    }
    case 0x23: { // a script: its name goes in the component's name
        out->name = fetch_word32(token + 2);
        const uint8_t* start = token + 6;
        const uint8_t* end = skip_to_end_of_script(start);
        change_species(out, kQbvalScriptBody);
        auto size = static_cast<uint32_t>(end - start);
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
        auto* code = static_cast<uint8_t*>(memory::alloc_block(size));
        memory::leave_heap_scope();
        for (uint32_t i = 0; i < size; ++i)
            code[i] = start[i];
        out->raw = reinterpret_cast<uint32_t>(code);
        return end;
    }
    case 0x2C: // <...>: all the caller's parameters
        if (args) {
            Struct* structure = new_init_struct();
            structure->get_assign(args);
            change_species(out, kQbvalStruct);
            out->structure = structure;
        }
        return token + 1;
    case 0x2D: { // <name>: one of the caller's parameters
        const uint32_t name = fetch_word32(token + 2);
        if (args) {
            if (const Component* found = args->find_last_component(name))
                duplicate_component(out, found, 0);
        }
        return token + 6;
    }
    case 0x30:
        return parse_random_range(p, out, false);
    case 0x38:
        return parse_random_range(p, out, true);
    default:
        return token + 1;
    }
}

// 0x00470130
const uint8_t* add_parameter(const uint8_t* token, uint32_t name, Struct* dest, Struct* args)
{
    const uint8_t* p = token + 1;
    bool deterministic = false;
    switch (*token) {
    case 0x16:
        dest->put_name_key(name, fetch_word32(p));
        return token + 5;
    case 0x17:
        dest->put_int(name, static_cast<int>(fetch_word32(p)));
        return token + 5;
    case 0x1A:
        dest->push_float(name, get_as_float(fetch_word32(p)));
        return token + 5;
    case 0x1E: {
        const float x = get_as_float(fetch_word32(p));
        const float y = get_as_float(fetch_word32(token + 5));
        const float z = get_as_float(fetch_word32(token + 9));
        // (a zero vector named 0x9D2D0915 isn't added)
        const float tolerance = render::g_one_percent;
        if (name != 0x9D2D0915 || !(std::fabs(x) < tolerance) || !(std::fabs(y) < tolerance) ||
            !(std::fabs(z) < tolerance))
            dest->put_vec3(name, x, y, z);
        return token + 13;
    }
    case 0x1F:
        dest->insert_pair(name, get_as_float(fetch_word32(p)), get_as_float(fetch_word32(token + 5)));
        return token + 9;
    case 0x1B: {
        const uint32_t length = fetch_word32(p);
        dest->append_word(name, reinterpret_cast<const char*>(token + 5));
        return token + 5 + length;
    }
    case 0x1C: {
        const uint32_t length = fetch_word32(p);
        dest->append_inline_text(name, reinterpret_cast<const char*>(token + 5));
        return token + 5 + length;
    }
    case 0x03: {
        Struct* structure = new_init_struct();
        p = parse_structure(structure, token, args);
        dest->append_struct_ref(name, structure);
        return p;
    }
    case 0x05: {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
        Array* array = new_init_array();
        memory::leave_heap_scope();
        p = parse_array(array, token, args);
        dest->append_array_ref(name, array);
        return p;
    }
    case 0x23: { // a script, under its own name
        const uint32_t script_name = fetch_word32(token + 2);
        const uint8_t* start = token + 6;
        const uint8_t* end = skip_to_end_of_script(start);
        dest->append_script(script_name, start, static_cast<uint32_t>(end - start));
        return end;
    }
    case 0x2D: { // <name>
        const uint32_t reference = fetch_word32(token + 2);
        p = token + 6;
        if (!args)
            return p;
        const Component* found = args->find_last_component(reference);
        if (!found)
            return p;
        if (name == 0 && (found->type_and_flag_ & 0xFE) == (kQbvalStruct << 1)) {
            dest->append_structure_if_any(found->structure);
            return p;
        }
        Component* copy = new_component();
        duplicate_component(copy, found, 0);
        copy->name = name;
        dest->attach_module(copy);
        return p;
    }
    case 0x2C: // <...>
        if (args) {
            if (name) {
                Struct* structure = new_init_struct();
                structure->get_assign(args);
                dest->append_struct_ref(name, structure);
            } else {
                dest->merge_structure(args);
            }
        }
        return p;
    case 0x38:
        deterministic = true;
        [[fallthrough]];
    case 0x30: {
        Component* value = new_component();
        p = parse_random_range(p, value, deterministic);
        value->name = name;
        dest->attach_module(value);
        return p;
    }
    case 0x09:
        return p;
    default:
        return token;
    }
}

// 0x00470F30
const uint8_t* parse_arguments(Struct* dest, const uint8_t* token, Struct* args)
{
    for (;;) {
        token = resolve_random_jumps(token);
        const uint8_t t = *token;
        if (t == 0x01 || t == 0x02 || t == 0x00 || t == 0x0F)
            return token;
        switch (t) {
        case 0x03:
            token = parse_structure(dest, token, args);
            break;
        case 0x09:
            ++token;
            break;
        case 0x2C:
            ++token;
            if (args)
                dest->merge_structure(args);
            break;
        case 0x2D:
            token = add_parameter(token, 0, dest, args);
            break;
        case 0x16: {
            const uint32_t name = fetch_word32(token + 1);
            const uint8_t* next = resolve_random_jumps(token + 5);
            if (*next != 0x07) {
                // A bare name: a flag.
                token = add_parameter(token, 0, dest, args);
                break;
            }
            next = resolve_random_jumps(next + 1);
            if (*next == 0x0E)
                token = add_named_expression(next, name, dest, args);
            else
                token = add_parameter(next, name, dest, args);
            break;
        }
        default:
            if (t == 0x0E)
                token = add_expression(token, dest, args);
            else
                token = add_parameter(token, 0, dest, args);
            break;
        }
    }
}

// 0x00471860
const uint8_t* parse_structure(Struct* dest, const uint8_t* token, Struct* args)
{
    const uint8_t* p = token + 1;
    for (;;) {
        p = skip_end_of_lines_in(p);
        p = resolve_random_jumps(p);
        const uint8_t t = *p;
        if (t == 0x04)
            return p + 1;
        switch (t) {
        case 0x01:
        case 0x02:
            p = skip_end_of_lines_in(p);
            break;
        case 0x03:
            p = parse_structure(dest, p, args);
            break;
        case 0x09:
            ++p;
            break;
        case 0x16: {
            const uint32_t name = fetch_word32(p + 1);
            const uint8_t* next = resolve_random_jumps(skip_end_of_lines_in(p + 5));
            if (*next != 0x07) {
                p = add_parameter(p, 0, dest, args);
                break;
            }
            next = resolve_random_jumps(skip_end_of_lines_in(next + 1));
            if (*next == 0x0E)
                p = add_named_expression(next, name, dest, args);
            else
                p = add_parameter(next, name, dest, args);
            break;
        }
        default:
            if (t == 0x0E)
                p = add_expression(p, dest, args);
            else
                p = add_parameter(p, 0, dest, args);
            break;
        }
    }
}

// 0x00471B00
const uint8_t* parse_array(Array* dest, const uint8_t* token, Struct* args)
{
    const uint8_t* first = token + 1;
    const uint8_t* p = resolve_random_jumps(first);
    uint32_t type = 0;
    uint32_t count = 0;
    if (*p == 0x06)
        return p + 1;
    do {
        const uint8_t t = *p;
        switch (t) {
        case 0x03:
            type = kQbvalStruct;
            break;
        case 0x05:
            type = kQbvalArray;
            break;
        case 0x16:
            type = kQbvalName;
            break;
        case 0x17:
        case 0x18:
            if (type != kQbvalFloat)
                type = kQbvalInteger;
            break;
        case 0x1A:
            type = kQbvalFloat;
            break;
        case 0x1B:
            type = kQbvalString;
            break;
        case 0x1C:
            type = kQbvalInlineText;
            break;
        case 0x1E:
            type = kQbvalVector;
            break;
        case 0x1F:
            type = kQbvalPair;
            break;
        default:
            break;
        }
        switch (t) {
        case 0x01:
        case 0x02:
        case 0x09:
            p = step_over_token(p);
            break;
        case 0x03:
        case 0x05:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1E:
        case 0x1F:
            ++count;
            p = skip_value(p);
            break;
        default:
            // (anything else doesn't move on: the original loops here)
            break;
        }
        p = resolve_random_jumps(p);
    } while (*p != 0x06);
    if (type == 0)
        return p + 1;

    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    dest->configure_size_kind(count, static_cast<QbValueKind>(type));
    memory::leave_heap_scope();
    if (type == 7 || type == 8 || type == 9 || type == 11)
        return token;
    uint32_t* element = dest->find_array_value();
    p = resolve_random_jumps(first);
    while (*p != 0x06) {
        const uint8_t t = *p;
        switch (type) {
        case kQbvalStruct:
            // (no step first; unknown tokens don't move on: the original loops here)
            if (t == 0x01 || t == 0x09) {
                ++p;
            } else if (t == 0x02) {
                p += 5;
            } else if (t == 0x03) {
                Struct* structure = new_init_struct();
                p = parse_structure(structure, p, args);
                *element = reinterpret_cast<uint32_t>(structure);
                ++element;
            }
            break;
        case kQbvalArray:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x05) {
                Array* array = new_init_array();
                p = parse_array(array, p - 1, args);
                *element = reinterpret_cast<uint32_t>(array);
                ++element;
            }
            break;
        case kQbvalInteger:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x17 || t == 0x18) {
                *element = fetch_word32(p);
                ++element;
                p += 4;
            }
            break;
        case kQbvalFloat:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x17 || t == 0x18) {
                auto value = static_cast<float>(static_cast<int32_t>(fetch_word32(p)));
                std::memcpy(element, &value, 4);
                ++element;
                p += 4;
            } else if (t == 0x1A) {
                *element = fetch_word32(p);
                ++element;
                p += 4;
            }
            break;
        case kQbvalName:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x16) {
                *element = fetch_word32(p);
                ++element;
                p += 4;
            }
            break;
        case kQbvalString:
        case kQbvalInlineText:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == (type == kQbvalString ? 0x1B : 0x1C)) {
                const uint32_t length = fetch_word32(p);
                p += 4;
                *element = reinterpret_cast<uint32_t>(spawn_text(reinterpret_cast<const char*>(p)));
                ++element;
                p += length;
            }
            break;
        case kQbvalVector:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x1E) {
                Vector* vector = alloc_script_vector();
                vector = vector ? new (vector) script::Vector : nullptr;
                uint32_t x = fetch_word32(p), y = fetch_word32(p + 4), z = fetch_word32(p + 8);
                std::memcpy(&vector->x, &x, 4);
                std::memcpy(&vector->y, &y, 4);
                std::memcpy(&vector->z, &z, 4);
                p += 12;
                *element = reinterpret_cast<uint32_t>(vector);
                ++element;
            }
            break;
        case kQbvalPair:
            ++p;
            if (t == 0x02) {
                p += 4;
            } else if (t == 0x1F) {
                Pair* pair = new_pair();
                pair = pair ? new (pair) script::Pair : nullptr;
                uint32_t x = fetch_word32(p), y = fetch_word32(p + 4);
                std::memcpy(&pair->x, &x, 4);
                std::memcpy(&pair->y, &y, 4);
                p += 8;
                *element = reinterpret_cast<uint32_t>(pair);
                ++element;
            }
            break;
        default:
            break;
        }
        p = resolve_random_jumps(p);
    }
    return p + 1;
}

// 0x004712B0
const uint8_t* parse_expression(const uint8_t* token, Struct* args, Component* result)
{
    ValueResolver& evaluator = get_evaluator();
    Component& value = get_scratch();
    evaluator.reset_if_dirty();
    evaluator.set_read_cursor(token);
    const uint8_t* p = token + 1;
    int parentheses = 0;
    bool after_dot = false;  // a name after '.' is a member name, not a global value
    bool have_value = false; // (for a negative number to be taken as a subtraction)
    bool expect_value = true;
    auto pushed = [&] {
        have_value = true;
        expect_value = false;
    };
    for (;;) {
        p = resolve_random_jumps(p);
        const uint8_t t = *p;
        switch (t) {
        case 0x01:
        case 0x06:
            ++p;
            continue;
        case 0x02:
            p += 5;
            continue;
        case 0x03:
        case 0x1B:
        case 0x1C:
        case 0x1E:
        case 0x1F:
        case 0x30:
            if (!expect_value)
                break;
            p = parse_value(p, args, &value);
            evaluator.intake(&value);
            release_component(&value);
            pushed();
            continue;
        case 0x05:
        case 0x07:
        case 0x08:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x12:
        case 0x14:
        case 0x32:
        case 0x33: // operators
            evaluator.intake(static_cast<int>(t));
            ++p;
            after_dot = t == 0x08;
            have_value = false;
            expect_value = true;
            continue;
        case 0x0E:
            if (!expect_value)
                break;
            ++p;
            evaluator.open_bracket();
            ++parentheses;
            have_value = false;
            expect_value = true;
            continue;
        case 0x0F:
            ++p;
            if (parentheses == 0)
                break;
            evaluator.close_bracket();
            expect_value = false;
            --parentheses;
            have_value = true;
            continue;
        case 0x16: {
            if (!expect_value)
                break;
            p = parse_value(p, args, &value);
            const SymbolTableEntry* symbol = after_dot ? nullptr : resolve_symbol(value.checksum);
            if (symbol) {
                bool push = true;
                switch (symbol->type) {
                case kQbvalInteger:
                case kQbvalFloat:
                case kQbvalString:
                case kQbvalInlineText:
                case kQbvalPair:
                case kQbvalVector:
                case kQbvalName:
                    change_species(&value, symbol->type);
                    value.raw = symbol->raw;
                    break;
                case kQbvalStruct:
                    change_species(&value, kQbvalStructRef);
                    value.raw = symbol->raw;
                    break;
                case kQbvalArray:
                    change_species(&value, kEsymboltypeArraypointer);
                    value.raw = symbol->raw;
                    break;
                case kQbvalNativeFunc: {
                    // Called now, with the arguments that follow.
                    Script* script = get_running_script();
                    change_species(&value, kQbvalInteger);
                    Struct* params = new_init_struct();
                    p = parse_arguments(params, p, args);
                    // (the byte the function returns, as it is)
                    using Routine = uint8_t (*)(Struct*, Script*);
                    const auto routine = reinterpret_cast<Routine>(symbol->raw);
                    value.raw = routine(params, script);
                    if (params) {
                        params->get_clear();
                        delete_struct(params);
                    }
                    break;
                }
                case kQbvalNativeMethod:
                    push = false;
                    break;
                default:
                    break;
                }
                if (push)
                    evaluator.intake(&value);
            } else {
                evaluator.intake(&value);
            }
            value.type_and_flag_ &= 1;
            value.raw = 0;
            pushed();
            continue;
        }
        case 0x17:
        case 0x1A: {
            if (expect_value) {
                p = parse_value(p, args, &value);
                evaluator.intake(&value);
                release_component(&value);
                pushed();
                continue;
            }
            // A negative number right after a value: a subtraction ("a -1" is compiled as "a" "-1").
            const uint8_t* next = parse_value(p, args, &value);
            const bool negative = t == 0x17 ? value.integer < 0 : value.float_value_ < 0.0f;
            if (!negative || !have_value)
                break;
            evaluator.intake(static_cast<int>(kOperatorMinus));
            if (t == 0x17)
                value.integer = -value.integer;
            else
                value.float_value_ = -value.float_value_;
            evaluator.intake(&value);
            release_component(&value);
            p = next;
            pushed();
            continue;
        }
        case 0x2C: // <...>
            if (!expect_value)
                break;
            ++p;
            if (args) {
                change_species(&value, kQbvalStructRef);
                value.structure = args;
                evaluator.intake(&value);
            }
            pushed();
            continue;
        case 0x2D: { // <name>
            if (!expect_value)
                break;
            const uint32_t name = fetch_word32(p + 2);
            p += 6;
            const Component* found = args ? args->find_last_component(name) : nullptr;
            if (found) {
                value.type_and_flag_ =
                    static_cast<uint8_t>((value.type_and_flag_ & 1) | (found->type_and_flag_ & 0xFE));
                value.raw = found->raw;
                evaluate_name_token(&value);
                make_reference(&value);
                evaluator.intake(&value);
                value.type_and_flag_ &= 1;
                value.raw = 0;
            }
            pushed();
            continue;
        }
        default:
            // (other tokens don't move on: the original loops here)
            continue;
        }
        break; // the end of the expression
    }
    evaluator.fetch_verdict(result);
    return p;
}

} // namespace script

namespace script {

// 0x00640A70 (a static initialiser): the expression evaluator, cleared.
void construct_evaluator() { g_evaluator.prepare_all(); }

} // namespace script
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
