#include "engine/script/token_reader.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/maths/random.h"
#include "engine/script/cell.h"
#include "engine/script/expression.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/cscript.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/movie.h"
#include "engine/sys/startup_init.h"
#include "engine/core/raw_view.h"
#include "platform/code_address.h"
#include "engine/core/checked_index.h"
#include "engine/script/param_struct.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/no_destroy.h"
#include "engine/script/script_state.h"
#include <gsl/pointers>

namespace {

// Names (the checksums of their text).
constexpr uint32_t kAngles = 0x9D2D0915u; // Angles

} // namespace

namespace script {

namespace {
constexpr float kParseRandomScale{0.000030525032f};
// Stored randoms kept (the oldest is dropped past it); tries at a choice that isn't the last one's.
constexpr int kStoredRandomLimit = 100;
constexpr int kNoRepeatMaxTries = 100;
// A vector's floats follow the token: x at +1, y at +5, z at +9; their bytes 12 together, a pair's 8.
constexpr int kVectorZOffset = 8;
constexpr int kVectorPayloadBytes = 12;
constexpr int kPairPayloadBytes = 8;
} // namespace

namespace {
// NOT ORIGINAL grouping - token_reader.cpp's state (the original's globals). Made on first use, never destroyed
// (token_reader_state()).
struct TokenReaderState {
    StoredRandom* stored_randoms_oldest_{};
    int stored_random_count_{};
    Component parse_scratch_{};
};

TokenReaderState& token_reader_state()
{
    static core::NoDestroy<TokenReaderState> state;
    return state.get();
}

} // namespace
namespace {

float get_as_float(uint32_t bits)
{
    auto value = std::bit_cast<float>(bits);
    return value;
}

void change_species(Component* component, uint32_t type)
{
    component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (type << 1));
}

Tokens skip_end_of_lines_in(Tokens p)
{
    for (;;) {
        if (*p == kTokenEndOfLine)
            p += 1;
        else if (*p == kTokenEndOfLineNumbered)
            p += kTokenWithWordBytes;
        else
            return p;
    }
}

// The evaluator expressions are parsed with, and the component values go through on the way.

Component& get_scratch() { return token_reader_state().parse_scratch_; }

// The stored randoms: newest first (next = older), at most 100.

StoredRandom*& stored_randoms_oldest() { return token_reader_state().stored_randoms_oldest_; }
int& stored_random_count() { return token_reader_state().stored_random_count_; }

// An expression parameter of a structure / call: merged if it gives a structure, added (unnamed) otherwise.
Tokens add_expression(Tokens token, Struct& dest, Struct* args)
{
    gsl::owner<Component*> value = new_component();
    token = parse_expression(token, args, value);
    if (value_kind(*value) == kQbvalStruct) {
        dest.append_structure_if_any(value->value.structure());
        release_component(value);
        free_component(value);
    } else if (value_kind(*value) != kQbvalNone) {
        value->name = 0;
        dest.attach_module(value);
    } else {
        free_component(value);
    }
    return token;
}

// A `name = (expression)` parameter.
Tokens add_named_expression(Tokens token, uint32_t name, Struct& dest, Struct* args)
{
    gsl::owner<Component*> value = new_component();
    token = parse_expression(token, args, value);
    if (value->type_and_flag_ & kComponentTypeBits) {
        value->name = name;
        dest.attach_module(value);
    } else {
        free_component(value);
    }
    return token;
}

} // namespace

// 0x004728C0
Script* get_running_script() { return current_script_var(); }

// 0x004728D0
Script* step_script_list(Script* script) { return script ? *raw::as<Script*>(script) : script_list_head(); }

// 0x0046F7A0
StoredRandom* look_up_saved_random(const uint8_t* table, uint32_t type, uint32_t count)
{
    const Script* script = get_running_script();
    for (StoredRandom* stored = script_state().stored_randoms_newest(); stored; stored = stored->succ_) {
        if (stored->lookup_table_ == table && stored->kind_tag_ == type && stored->num_ == count &&
            (stored->owner_ == script || stored->owner_ == nullptr))
            return stored;
    }
    return nullptr;
}

// 0x00470BB0
StoredRandom* new_stored_random()
{
    if (stored_random_count() >= kStoredRandomLimit) {
        StoredRandom* oldest = stored_randoms_oldest();
        StoredRandom* newer = oldest->previous_;
        --stored_random_count();
        free_stored_random(oldest);
        stored_randoms_oldest() = newer;
        if (newer)
            newer->succ_ = nullptr;
    }
    auto* stored = script_state().stored_random_pools().allocate<StoredRandom>();
    if (stored) {
        ++stored_random_count();
        stored->succ_ = nullptr;
        stored->previous_ = nullptr;
    }
    StoredRandom* newest = script_state().stored_randoms_newest();
    stored->previous_ = nullptr;
    stored->succ_ = newest;
    script_state().set_stored_randoms_newest(stored);
    if (newest)
        newest->previous_ = stored;
    else
        stored_randoms_oldest() = stored;
    return stored;
}

// 0x00470C60
Tokens resolve_random_jumps(Tokens token)
{
    for (;;) {
        switch (*token) {
        case kTokenLongJump: // jump
            token += kTokenWithWordBytes + fetch_word32(token + 1);
            break;
        case kTokenRandom:   // Random(@a @b ...): weighted
        case kTokenRandomSideStream: { // the same, from the second random stream
            const uint32_t count = fetch_word32(token + 1);
            const Tokens weights = token + 5;
            int total = 0;
            for (uint32_t i = 0; i < count; ++i)
                total += static_cast<int16_t>(fetch_word16(weights + 2 * i));
            int pick = *token == kTokenRandom ? maths::rnd(total) : maths::roll_side_stream(total);
            int choice = 0;
            pick -= static_cast<int16_t>(fetch_word16(weights));
            while (pick >= 0) {
                ++choice;
                pick -= static_cast<int16_t>(fetch_word16(weights + 2 * choice));
            }
            const Tokens offset = weights + 2 * count + 4 * choice;
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case kTokenRandomNoRepeat: { // RandomNoRepeat
            const uint32_t count = fetch_word32(token + 1);
            const Tokens table = token + 5 + 2 * count;
            StoredRandom* stored = look_up_saved_random(table.address(), kTokenRandomNoRepeat, count);
            if (!stored) {
                stored = new_stored_random();
                stored->lookup_table_ = table.address();
                stored->kind_tag_ = kTokenRandomNoRepeat;
                stored->num_ = static_cast<uint16_t>(count);
                stored->last_ = static_cast<uint32_t>(maths::rnd(static_cast<int>(count)));
            }
            stored->owner_ = get_running_script();
            int pick = maths::rnd(static_cast<int>(count));
            for (int tries = 0; static_cast<uint16_t>(pick) == stored->last_;) {
                if (++tries > kNoRepeatMaxTries)
                    break;
                pick = maths::rnd(static_cast<int>(count));
            }
            const uint32_t choice = static_cast<uint16_t>(pick);
            stored->last_ = choice;
            const Tokens offset = table + 4 * choice;
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case kTokenRandomPermute: { // RandomPermute
            const uint32_t count = fetch_word32(token + 1);
            const Tokens table = token + 5 + 2 * count;
            StoredRandom* stored = look_up_saved_random(table.address(), kTokenRandomPermute, count);
            if (!stored) {
                stored = new_stored_random();
                stored->lookup_table_ = table.address();
                stored->kind_tag_ = kTokenRandomPermute;
                stored->num_ = static_cast<uint16_t>(count);
                stored->ordinal_ = 0;
                for (uint16_t i = 0; i < stored->num_; ++i)
                    core::at(stored->order_, i) = static_cast<uint8_t>(i);
                shuffle(*stored, false);
            }
            stored->owner_ = get_running_script();
            if (++stored->ordinal_ >= stored->num_) {
                shuffle(*stored, true);
                stored->ordinal_ = 0;
            }
            const Tokens offset = table + 4 * core::at(stored->order_, stored->ordinal_);
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case kTokenShortJump: // short jump
            token += 1 + fetch_word16(token + 1);
            break;
        default:
            return token;
        }
    }
}

// 0x0046FD60
Tokens skip_value(Tokens token)
{
    switch (*token) {
    case kTokenStructStart:
    case kTokenArrayStart: {
        const uint8_t open = *token;
        const auto close = static_cast<uint8_t>(open + 1);
        int depth = 1;
        for (bool again = true; again; again = depth) {
            token = step_over_token(token);
            if (*token == open)
                ++depth;
            else if (*token == close)
                --depth;
        }
        return token + 1;
    }
    case kTokenName:
    case kTokenInteger:
    case kTokenHexInteger:
    case kTokenFloat:
        return token + kTokenWithWordBytes;
    case kTokenString:
    case kTokenLocalString:
        return token + kTokenWithWordBytes + fetch_word32(token + 1);
    case kTokenVector:
        return token + kTokenWithVectorBytes;
    case kTokenPair:
        return token + kTokenWithPairBytes;
    default:
        return token;
    }
}

// 0x0046FBD0
Tokens parse_random_range(Tokens token, Component* out, bool deterministic)
{
    float a = get_as_float(fetch_word32(token + 1));
    float b = get_as_float(fetch_word32(token + kTokenWithWordBytes));
    // (rounded with the game's 0.1 nudge, as the x87 does it)
    const double nudge = kParseNudge;
    const double tolerance = gfx::kTinyLength;
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
        out->value.set_word(static_cast<uint32_t>(pick + low));
        change_species(out, kQbvalInteger);
        return token + kTokenWithPairBytes;
    }
    if (a > b) {
        const float swapped = a;
        a = b;
        b = swapped;
    }
    const int pick = deterministic ? maths::roll_side_stream(0x7FF9) : maths::rnd(0x7FF9);
    change_species(out, kQbvalFloat);
    const double scale = kParseRandomScale;
    out->value.set_real(static_cast<float>((static_cast<double>(b) - a) * pick * scale + a));
    return token + kTokenWithPairBytes;
}

// 0x00479730
void evaluate_name_token(Component* component)
{
    if ((component->type_and_flag_ & kComponentTypeBits) != (kQbvalName << 1))
        return;
    const SymbolTableEntry* symbol = resolve_symbol(component->value.word());
    if (!symbol)
        return;
    switch (symbol->type()) {
    case kQbvalInteger:
    case kQbvalFloat:
    case kQbvalString:
    case kQbvalInlineText:
    case kQbvalPair:
    case kQbvalVector:
    case kQbvalStruct:
    case kQbvalArray:
    case kQbvalName:
        change_species(component, symbol->type());
        component->value.set_word(symbol->value().word());
        break;
    default:
        break;
    }
}

// 0x004706F0
Tokens parse_value(Tokens token, Struct* args, Component* out)
{
    Tokens p = token + 1;
    switch (*token) {
    case kTokenStructStart: {
        gsl::owner<Struct*> structure = script::new_struct();
        p = parse_structure(structure, token, args);
        change_species(out, kQbvalStruct);
        out->value.set_structure(structure);
        return p;
    }
    case kTokenArrayStart: {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        gsl::owner<Array*> array = script::new_array();
        memory::leave_heap_scope();
        p = parse_array(array, token, args);
        change_species(out, kQbvalArray);
        out->value.set_array(array);
        return p;
    }
    case kTokenName:
        change_species(out, kQbvalName);
        out->value.set_word(fetch_word32(p));
        return token + kTokenWithWordBytes;
    case kTokenInteger:
        change_species(out, kQbvalInteger);
        out->value.set_word(fetch_word32(p));
        return token + kTokenWithWordBytes;
    case kTokenFloat:
        change_species(out, kQbvalFloat);
        out->value.set_word(fetch_word32(p));
        return token + kTokenWithWordBytes;
    case kTokenString:
    case kTokenLocalString: {
        change_species(out, *token == kTokenString ? kQbvalString : kQbvalInlineText);
        const uint32_t length = fetch_word32(p);
        out->value.set_text(spawn_text(raw::chars((token + kTokenWithWordBytes).address())));
        return token + kTokenWithWordBytes + length;
    }
    case kTokenVector: {
        change_species(out, kQbvalVector);
        uint32_t x = fetch_word32(p), y = fetch_word32(token + kTokenWithWordBytes), z = fetch_word32(token + kTokenWithPairBytes);
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        out->value.set_vector(vector);
        std::memcpy(&vector->x(), &x, 4);
        std::memcpy(&out->value.vector()->y(), &y, 4);
        std::memcpy(&out->value.vector()->z(), &z, 4);
        return token + kTokenWithVectorBytes;
    }
    case kTokenPair: {
        change_species(out, kQbvalPair);
        uint32_t x = fetch_word32(p), y = fetch_word32(token + kTokenWithWordBytes);
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        out->value.set_pair(pair);
        std::memcpy(&pair->x(), &x, 4);
        std::memcpy(&out->value.pair()->y(), &y, 4);
        return token + kTokenWithPairBytes;
    }
    case kTokenScriptStart: { // a script: its name goes in the component's name
        out->name = fetch_word32(token + 2);
        const Tokens start = token + 6;
        Tokens end = skip_to_end_of_script(start);
        change_species(out, kQbvalScriptBody);
        const std::span<const uint8_t> script = code_between(start, end);
        const auto size = static_cast<uint32_t>(script.size());
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        auto* code = static_cast<uint8_t*>(memory::alloc_block(size));
        memory::leave_heap_scope();
        const std::span<uint8_t> copy(code, size);
        note_code_block(copy);
        std::ranges::copy(script, copy.begin());
        out->value.set_code(code);
        return end;
    }
    case kTokenAllArguments: // <...>: all the caller's parameters
        if (args) {
            gsl::owner<Struct*> structure = script::new_struct();
            structure->get_assign(args);
            change_species(out, kQbvalStruct);
            out->value.set_structure(structure);
        }
        return token + 1;
    case kTokenLocalArgument: { // <name>: one of the caller's parameters
        const uint32_t name = fetch_word32(token + 2);
        if (args) {
            if (const Component* found = args->find_last_component(name))
                duplicate_component(out, found, 0);
        }
        return token + kTokenWithNamedWordBytes;
    }
    case kTokenRandomRange:
        return parse_random_range(p, out, false);
    case kTokenRandomRangeSideStream:
        return parse_random_range(p, out, true);
    default:
        return token + 1;
    }
}

// 0x00470130
Tokens add_parameter(Tokens token, uint32_t name, Struct* dest, Struct* args)
{
    Tokens p = token + 1;
    bool deterministic = false;
    switch (*token) {
    case kTokenName:
        dest->put_name_key(checksums::Name{name}, fetch_word32(p));
        return token + kTokenWithWordBytes;
    case kTokenInteger:
        dest->put_int(checksums::Name{name}, static_cast<int>(fetch_word32(p)));
        return token + kTokenWithWordBytes;
    case kTokenFloat:
        dest->push_float(checksums::Name{name}, get_as_float(fetch_word32(p)));
        return token + kTokenWithWordBytes;
    case kTokenVector: {
        const float x = get_as_float(fetch_word32(p));
        const float y = get_as_float(fetch_word32(token + 5));
        const float z = get_as_float(fetch_word32(token + 9));
        // (a zero vector named 0x9D2D0915 isn't added)
        const float tolerance = render::kOnePercent;
        if (name != kAngles || !(std::fabs(x) < tolerance) || !(std::fabs(y) < tolerance) ||
            !(std::fabs(z) < tolerance))
            dest->put_vec3(checksums::Name{name}, {.x = x, .y = y, .z = z});
        return token + kTokenWithVectorBytes;
    }
    case kTokenPair:
        dest->insert_pair(checksums::Name{name}, {.x = get_as_float(fetch_word32(p)), .y = get_as_float(fetch_word32(token + kTokenWithWordBytes))});
        return token + kTokenWithPairBytes;
    case kTokenString: {
        const uint32_t length = fetch_word32(p);
        dest->append_word(name, raw::chars((token + kTokenWithWordBytes).address()));
        return token + kTokenWithWordBytes + length;
    }
    case kTokenLocalString: {
        const uint32_t length = fetch_word32(p);
        dest->append_inline_text(name, raw::chars((token + kTokenWithWordBytes).address()));
        return token + kTokenWithWordBytes + length;
    }
    case kTokenStructStart: {
        gsl::owner<Struct*> structure = script::new_struct();
        p = parse_structure(structure, token, args);
        dest->append_struct_ref(name, structure);
        return p;
    }
    case kTokenArrayStart: {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        gsl::owner<Array*> array = script::new_array();
        memory::leave_heap_scope();
        p = parse_array(array, token, args);
        dest->append_array_ref(name, array);
        return p;
    }
    case kTokenScriptStart: { // a script, under its own name
        const uint32_t script_name = fetch_word32(token + 2);
        const Tokens start = token + 6;
        Tokens end = skip_to_end_of_script(start);
        dest->append_script(script_name, code_between(start, end));
        return end;
    }
    case kTokenLocalArgument: { // <name>
        const uint32_t reference = fetch_word32(token + 2);
        p = token + kTokenWithNamedWordBytes;
        if (!args)
            return p;
        const Component* found = args->find_last_component(reference);
        if (!found)
            return p;
        if (name == 0 && (found->type_and_flag_ & kComponentTypeBits) == (kQbvalStruct << 1)) {
            dest->append_structure_if_any(found->value.structure());
            return p;
        }
        gsl::owner<Component*> copy = new_component();
        duplicate_component(copy, found, 0);
        copy->name = name;
        dest->attach_module(copy);
        return p;
    }
    case kTokenAllArguments: // <...>
        if (args) {
            if (name) {
                gsl::owner<Struct*> structure = script::new_struct();
                structure->get_assign(args);
                dest->append_struct_ref(name, structure);
            } else {
                dest->merge_structure(args);
            }
        }
        return p;
    case kTokenRandomRangeSideStream:
        deterministic = true;
        [[fallthrough]];
    case kTokenRandomRange: {
        gsl::owner<Component*> value = new_component();
        p = parse_random_range(p, value, deterministic);
        value->name = name;
        dest->attach_module(value);
        return p;
    }
    case kTokenComma:
        return p;
    default:
        return token;
    }
}

// 0x00470F30
Tokens parse_arguments(Struct* dest, Tokens token, Struct* args)
{
    for (;;) {
        token = resolve_random_jumps(token);
        const uint8_t t = *token;
        if (t == kTokenEndOfLine || t == kTokenEndOfLineNumbered || t == kTokenEndOfFile || t == kTokenCloseParenthesis)
            return token;
        switch (t) {
        case kTokenStructStart:
            token = parse_structure(dest, token, args);
            break;
        case kTokenComma:
            ++token;
            break;
        case kTokenAllArguments:
            ++token;
            if (args)
                dest->merge_structure(args);
            break;
        case kTokenLocalArgument:
            token = add_parameter(token, 0, dest, args);
            break;
        case kTokenName: {
            const uint32_t name = fetch_word32(token + 1);
            Tokens next = resolve_random_jumps(token + kTokenWithWordBytes);
            if (*next != kTokenEquals) {
                // A bare name: a flag.
                token = add_parameter(token, 0, dest, args);
                break;
            }
            next = resolve_random_jumps(next + 1);
            if (*next == kTokenOpenParenthesis)
                token = add_named_expression(next, name, *dest, args);
            else
                token = add_parameter(next, name, dest, args);
            break;
        }
        default:
            if (t == kTokenOpenParenthesis)
                token = add_expression(token, *dest, args);
            else
                token = add_parameter(token, 0, dest, args);
            break;
        }
    }
}

// 0x00471860
Tokens parse_structure(Struct* dest, Tokens token, Struct* args)
{
    Tokens p = token + 1;
    for (;;) {
        p = skip_end_of_lines_in(p);
        p = resolve_random_jumps(p);
        const uint8_t t = *p;
        if (t == kTokenStructEnd)
            return p + 1;
        switch (t) {
        case kTokenEndOfLine:
        case kTokenEndOfLineNumbered:
            p = skip_end_of_lines_in(p);
            break;
        case kTokenStructStart:
            p = parse_structure(dest, p, args);
            break;
        case kTokenComma:
            ++p;
            break;
        case kTokenName: {
            const uint32_t name = fetch_word32(p + 1);
            Tokens next = resolve_random_jumps(skip_end_of_lines_in(p + kTokenWithWordBytes));
            if (*next != kTokenEquals) {
                p = add_parameter(p, 0, dest, args);
                break;
            }
            next = resolve_random_jumps(skip_end_of_lines_in(next + 1));
            if (*next == kTokenOpenParenthesis)
                p = add_named_expression(next, name, *dest, args);
            else
                p = add_parameter(next, name, dest, args);
            break;
        }
        default:
            if (t == kTokenOpenParenthesis)
                p = add_expression(p, *dest, args);
            else
                p = add_parameter(p, 0, dest, args);
            break;
        }
    }
}

// 0x00471B00
Tokens parse_array(Array* dest, Tokens token, Struct* args)
{
    const Tokens first = token + 1;
    Tokens p = resolve_random_jumps(first);
    uint32_t type = 0;
    uint32_t count = 0;
    if (*p == kTokenArrayEnd)
        return p + 1;
    for (bool again = true; again; again = *p != kTokenArrayEnd) {
        const uint8_t t = *p;
        switch (t) {
        case kTokenStructStart:
            type = kQbvalStruct;
            break;
        case kTokenArrayStart:
            type = kQbvalArray;
            break;
        case kTokenName:
            type = kQbvalName;
            break;
        case kTokenInteger:
        case kTokenHexInteger:
            if (type != kQbvalFloat)
                type = kQbvalInteger;
            break;
        case kTokenFloat:
            type = kQbvalFloat;
            break;
        case kTokenString:
            type = kQbvalString;
            break;
        case kTokenLocalString:
            type = kQbvalInlineText;
            break;
        case kTokenVector:
            type = kQbvalVector;
            break;
        case kTokenPair:
            type = kQbvalPair;
            break;
        default:
            break;
        }
        switch (t) {
        case kTokenEndOfLine:
        case kTokenEndOfLineNumbered:
        case kTokenComma:
            p = step_over_token(p);
            break;
        case kTokenStructStart:
        case kTokenArrayStart:
        case kTokenName:
        case kTokenInteger:
        case kTokenHexInteger:
        case kTokenFloat:
        case kTokenString:
        case kTokenLocalString:
        case kTokenVector:
        case kTokenPair:
            ++count;
            p = skip_value(p);
            break;
        default:
            // (anything else doesn't move on: the original loops here)
            break;
        }
        p = resolve_random_jumps(p);
    }
    if (type == 0)
        return p + 1;

    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    dest->configure_size_kind(count, static_cast<QbValueKind>(type));
    memory::leave_heap_scope();
    if (type == kQbvalScriptBody || type == kQbvalNativeFunc || type == kQbvalNativeMethod || type == kQbvalStructHandedOver)
        return token;
    uint32_t element = 0; // (the next element written)
    p = resolve_random_jumps(first);
    while (*p != kTokenArrayEnd) {
        const uint8_t t = *p;
        switch (type) {
        case kQbvalStruct:
            // (no step first; unknown tokens don't move on: the original loops here)
            if (t == kTokenEndOfLine || t == kTokenComma) {
                ++p;
            } else if (t == kTokenEndOfLineNumbered) {
                p += kTokenWithWordBytes;
            } else if (t == kTokenStructStart) {
                gsl::owner<Struct*> structure = script::new_struct();
                p = parse_structure(structure, p, args);
                dest->set_cell(element++, Cell::of(structure));
            }
            break;
        case kQbvalArray:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == kTokenArrayStart) {
                gsl::owner<Array*> array = script::new_array();
                p = parse_array(array, p - 1, args);
                dest->set_cell(element++, Cell::of(array));
            }
            break;
        case kQbvalInteger:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == kTokenInteger || t == kTokenHexInteger) {
                dest->set_cell(element++, Cell::of_word(fetch_word32(p)));
                p += 4;
            }
            break;
        case kQbvalFloat:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == kTokenInteger || t == kTokenHexInteger) {
                dest->set_cell(element++, Cell::of_real(static_cast<float>(static_cast<int32_t>(fetch_word32(p)))));
                p += 4;
            } else if (t == kTokenFloat) {
                dest->set_cell(element++, Cell::of_word(fetch_word32(p)));
                p += 4;
            }
            break;
        case kQbvalName:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == kTokenName) {
                dest->set_cell(element++, Cell::of_word(fetch_word32(p)));
                p += 4;
            }
            break;
        case kQbvalString:
        case kQbvalInlineText:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == (type == kQbvalString ? kTokenString : kTokenLocalString)) {
                const uint32_t length = fetch_word32(p);
                p += 4;
                dest->set_cell(element++, Cell::of_text(spawn_text(raw::chars(p.address()))));
                p += length;
            }
            break;
        case kQbvalVector:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == kTokenVector) {
                Vector* vector = alloc_script_vector();
                vector = vector ? new (vector) script::Vector : nullptr;
                uint32_t x = fetch_word32(p), y = fetch_word32(p + 4), z = fetch_word32(p + kVectorZOffset);
                std::memcpy(&vector->x(), &x, 4);
                std::memcpy(&vector->y(), &y, 4);
                std::memcpy(&vector->z(), &z, 4);
                p += kVectorPayloadBytes;
                dest->set_cell(element++, Cell::of(vector));
            }
            break;
        case kQbvalPair:
            ++p;
            if (t == kTokenEndOfLineNumbered) {
                p += 4;
            } else if (t == kTokenPair) {
                Pair* pair = new_pair();
                pair = pair ? new (pair) script::Pair : nullptr;
                uint32_t x = fetch_word32(p), y = fetch_word32(p + 4);
                std::memcpy(&pair->x(), &x, 4);
                std::memcpy(&pair->y(), &y, 4);
                p += kPairPayloadBytes;
                dest->set_cell(element++, Cell::of(pair));
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
Tokens parse_expression(Tokens token, Struct* args, Component* result)
{
    ValueResolver& evaluator = script_state().evaluator();
    Component& value = get_scratch();
    evaluator.reset_if_dirty();
    evaluator.set_read_cursor(token.address());
    Tokens p = token + 1;
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
        case kTokenEndOfLine:
        case kTokenArrayEnd:
            ++p;
            continue;
        case kTokenEndOfLineNumbered:
            p += kTokenWithWordBytes;
            continue;
        case kTokenStructStart:
        case kTokenString:
        case kTokenLocalString:
        case kTokenVector:
        case kTokenPair:
        case kTokenRandomRange:
            if (!expect_value)
                break;
            p = parse_value(p, args, &value);
            evaluator.intake(&value);
            release_component(&value);
            pushed();
            continue;
        case kTokenArrayStart:
        case kTokenEquals:
        case kTokenDot:
        case kTokenMinus:
        case kTokenPlus:
        case kTokenDivide:
        case kTokenMultiply:
        case kTokenLessThan:
        case kTokenGreaterThan:
        case kTokenOr:
        case kTokenAnd: // operators
            evaluator.intake(static_cast<int>(t));
            ++p;
            after_dot = t == kTokenDot;
            have_value = false;
            expect_value = true;
            continue;
        case kTokenOpenParenthesis:
            if (!expect_value)
                break;
            ++p;
            evaluator.open_bracket();
            ++parentheses;
            have_value = false;
            expect_value = true;
            continue;
        case kTokenCloseParenthesis:
            ++p;
            if (parentheses == 0)
                break;
            evaluator.close_bracket();
            expect_value = false;
            --parentheses;
            have_value = true;
            continue;
        case kTokenName: {
            if (!expect_value)
                break;
            p = parse_value(p, args, &value);
            const SymbolTableEntry* symbol = after_dot ? nullptr : resolve_symbol(value.value.word());
            if (symbol) {
                bool push = true;
                switch (symbol->type()) {
                case kQbvalInteger:
                case kQbvalFloat:
                case kQbvalString:
                case kQbvalInlineText:
                case kQbvalPair:
                case kQbvalVector:
                case kQbvalName:
                    change_species(&value, symbol->type());
                    value.value.set_word(symbol->value().word());
                    break;
                case kQbvalStruct:
                    change_species(&value, kQbvalStructRef);
                    value.value.set_word(symbol->value().word());
                    break;
                case kQbvalArray:
                    change_species(&value, kEsymboltypeArraypointer);
                    value.value.set_word(symbol->value().word());
                    break;
                case kQbvalNativeFunc: {
                    // Called now, with the arguments that follow.
                    Script* script = get_running_script();
                    change_species(&value, kQbvalInteger);
                    gsl::owner<Struct*> params = script::new_struct();
                    p = parse_arguments(params, p, args);
                    // (the byte the function returns, as it is)
                    using Routine = uint8_t (*)(Struct*, Script*);
                    const auto routine = symbol->value().function();
                    value.value.set_word(routine(params, script));
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
            value.value.set_word(0);
            pushed();
            continue;
        }
        case kTokenInteger:
        case kTokenFloat: {
            if (expect_value) {
                p = parse_value(p, args, &value);
                evaluator.intake(&value);
                release_component(&value);
                pushed();
                continue;
            }
            // A negative number right after a value: a subtraction ("a -1" is compiled as "a" "-1").
            const Tokens next = parse_value(p, args, &value);
            const bool negative = t == 0x17 ? value.value.integer() < 0 : value.value.real() < 0.0f;
            if (!negative || !have_value)
                break;
            evaluator.intake(static_cast<int>(kOperatorMinus));
            if (t == kTokenInteger)
                value.value.set_integer(-value.value.integer());
            else
                value.value.set_real(-value.value.real());
            evaluator.intake(&value);
            release_component(&value);
            p = next;
            pushed();
            continue;
        }
        case kTokenAllArguments: // <...>
            if (!expect_value)
                break;
            ++p;
            if (args) {
                change_species(&value, kQbvalStructRef);
                value.value.set_structure(args);
                evaluator.intake(&value);
            }
            pushed();
            continue;
        case kTokenLocalArgument: { // <name>
            if (!expect_value)
                break;
            const uint32_t name = fetch_word32(p + 2);
            p += kTokenWithNamedWordBytes;
            const Component* found = args ? args->find_last_component(name) : nullptr;
            if (found) {
                value.type_and_flag_ =
                    static_cast<uint8_t>((value.type_and_flag_ & 1) | (found->type_and_flag_ & kComponentTypeBits));
                value.value.set_word(found->value.word());
                evaluate_name_token(&value);
                make_reference(&value);
                evaluator.intake(&value);
                value.type_and_flag_ &= 1;
                value.value.set_word(0);
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

Tokens read_name_token(Tokens token, uint32_t* checksum, const char** name)
{
    *checksum = fetch_word32(token + 1);
    *name = raw::chars((token + kTokenWithWordBytes).address());
    return token + kTokenWithWordBytes + std::strlen(*name) + 1;
}

// 0x00640A70 (a static initialiser): the expression evaluator, cleared.
void construct_evaluator() { script_state().evaluator().prepare_all(); }

const MemberFunctionEntry* member_entry_at(Tokens p) { return cell::pointer<const MemberFunctionEntry>(fetch_word32(p)); }
NativeFunction native_function_at(Tokens p)
{
    return function_at<NativeFunction>(cell::pointer<const void>(fetch_word32(p)));
}

} // namespace script
