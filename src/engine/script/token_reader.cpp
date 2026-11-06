#include "engine/script/token_reader.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <span>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/slab_bank.h"
#include "base/maths/random.h"
#include "engine/script/cell.h"
#include "engine/script/expression.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/context.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/render_constants.h"
#include "engine/sys/startup_init.h"
#include "base/core/raw_view.h"
#include "engine/script/names.h"
#include "base/core/checked_index.h"
#include "engine/script/param_struct.h"
#include "base/core/no_destroy.h"
#include "engine/script/script_state.h"
#include <gsl/pointers>

namespace {

// Names (the checksums of their text).
constexpr uint32_t kAngles = 0x9D2D0915U; // Angles

} // namespace

namespace script {

namespace {
constexpr float kParseRandomScale{0.000030525032F};
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

void change_species(Component* component, QbValueKind type)
{
    set_value_kind(*component, type);
}

Tokens skip_end_of_lines_in(Tokens cursor)
{
    for (;;) {
        if (as_token(*cursor) == QbToken::kEndOfLine) {
            cursor += 1;
        } else if (as_token(*cursor) == QbToken::kEndOfLineNumbered) {
            cursor += kTokenWithWordBytes;
        } else {
            return cursor;
        }
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
    if (value_kind(*value) == QbValueKind::kStruct) {
        dest.append_structure_if_any(value->value.structure());
        release_component(value);
        free_component(value);
    } else if (value_kind(*value) != QbValueKind::kNone) {
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
    if ((value->type_and_flag_ & kComponentTypeBits) != 0) {
        value->name = name;
        dest.attach_module(value);
    } else {
        free_component(value);
    }
    return token;
}

} // namespace

// 0x004728C0
Script* get_running_script() { return host().get_running_script(); }

// 0x0046F7A0
StoredRandom* look_up_saved_random(const uint8_t* table, QbToken type, uint32_t count)
{
    const Script* script = get_running_script();
    for (StoredRandom* stored = script_state().stored_randoms_newest(); stored; stored = stored->succ_) {
        if (stored->lookup_table_ == table && stored->kind_tag_ == token_byte(type) && stored->num_ == count &&
            (stored->owner_ == script || stored->owner_ == nullptr)) {
            return stored;
        }
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
        if (newer) {
            newer->succ_ = nullptr;
        }
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
    if (newest) {
        newest->previous_ = stored;
    } else {
        stored_randoms_oldest() = stored;
    }
    return stored;
}

// 0x00470C60
Tokens resolve_random_jumps(Tokens token)
{
    for (;;) {
        switch (as_token(*token)) {
        case QbToken::kLongJump: // jump
            token += kTokenWithWordBytes + fetch_word32(token + 1);
            break;
        case QbToken::kRandom:   // Random(@a @b ...): weighted
        case QbToken::kRandomSideStream: { // the same, from the second random stream
            const uint32_t count = fetch_word32(token + 1);
            const Tokens weights = token + 5;
            int total = 0;
            for (uint32_t i = 0; i < count; ++i) {
                total += static_cast<int16_t>(fetch_word16(weights + 2 * i));
            }
            int pick = as_token(*token) == QbToken::kRandom ? maths::rnd(total) : maths::roll_side_stream(total);
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
        case QbToken::kRandomNoRepeat: { // RandomNoRepeat
            const uint32_t count = fetch_word32(token + 1);
            const Tokens table = token + 5 + 2 * count;
            StoredRandom* stored = look_up_saved_random(table.address(), QbToken::kRandomNoRepeat, count);
            if (!stored) {
                stored = new_stored_random();
                stored->lookup_table_ = table.address();
                stored->kind_tag_ = token_byte(QbToken::kRandomNoRepeat);
                stored->num_ = static_cast<uint16_t>(count);
                stored->last_ = static_cast<uint32_t>(maths::rnd(static_cast<int>(count)));
            }
            stored->owner_ = get_running_script();
            int pick = maths::rnd(static_cast<int>(count));
            for (int tries = 0; static_cast<uint16_t>(pick) == stored->last_;) {
                if (++tries > kNoRepeatMaxTries) {
                    break;
                }
                pick = maths::rnd(static_cast<int>(count));
            }
            const uint32_t choice = static_cast<uint16_t>(pick);
            stored->last_ = choice;
            const Tokens offset = table + 4 * choice;
            token = offset + fetch_word32(offset) + 4;
            break;
        }
        case QbToken::kRandomPermute: { // RandomPermute
            const uint32_t count = fetch_word32(token + 1);
            const Tokens table = token + 5 + 2 * count;
            StoredRandom* stored = look_up_saved_random(table.address(), QbToken::kRandomPermute, count);
            if (!stored) {
                stored = new_stored_random();
                stored->lookup_table_ = table.address();
                stored->kind_tag_ = token_byte(QbToken::kRandomPermute);
                stored->num_ = static_cast<uint16_t>(count);
                stored->ordinal_ = 0;
                for (uint16_t i = 0; i < stored->num_; ++i) {
                    core::at(stored->order_, i) = static_cast<uint8_t>(i);
                }
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
        case QbToken::kShortJump: // short jump
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
    switch (as_token(*token)) {
    case QbToken::kStructStart:
    case QbToken::kArrayStart: {
        const QbToken open = as_token(*token);
        const QbToken close = open == QbToken::kStructStart ? QbToken::kStructEnd : QbToken::kArrayEnd;
        int depth = 1;
        for (bool again = true; again; again = (depth != 0)) {
            token = step_over_token(token);
            if (as_token(*token) == open) {
                ++depth;
            } else if (as_token(*token) == close) {
                --depth;
            }
        }
        return token + 1;
    }
    case QbToken::kName:
    case QbToken::kInteger:
    case QbToken::kHexInteger:
    case QbToken::kFloat:
        return token + kTokenWithWordBytes;
    case QbToken::kString:
    case QbToken::kLocalString:
        return token + kTokenWithWordBytes + fetch_word32(token + 1);
    case QbToken::kVector:
        return token + kTokenWithVectorBytes;
    case QbToken::kPair:
        return token + kTokenWithPairBytes;
    default:
        return token;
    }
}

// 0x0046FBD0
Tokens parse_random_range(Tokens token, Component* out, bool deterministic)
{
    float from = get_as_float(fetch_word32(token + 1));
    float to = get_as_float(fetch_word32(token + kTokenWithWordBytes));
    // (rounded with the game's 0.1 nudge, as the x87 does it)
    const double nudge = kParseNudge;
    const double tolerance = gfx::kTinyLength;
    auto nearest = [&](float value) {
        return static_cast<int>(value >= 0.0F || std::isnan(value) ? static_cast<double>(value) + nudge
                                                                   : static_cast<double>(value) - nudge);
    };
    int low = nearest(from);
    int high = nearest(to);
    if (!(std::fabs(static_cast<double>(from) - low) > tolerance) &&
        !(std::fabs(static_cast<double>(to) - high) > tolerance)) {
        // Whole numbers: an integer in [low, high].
        if (low > high) {
            const int swapped = low;
            low = high;
            high = swapped;
        }
        const int range = high - low + 1;
        const int pick = deterministic ? maths::roll_side_stream(range) : maths::rnd(range);
        out->value.set_word(static_cast<uint32_t>(pick + low));
        change_species(out, QbValueKind::kInteger);
        return token + kTokenWithPairBytes;
    }
    if (from > to) {
        const float swapped = from;
        from = to;
        to = swapped;
    }
    const int pick = deterministic ? maths::roll_side_stream(0x7FF9) : maths::rnd(0x7FF9);
    change_species(out, QbValueKind::kFloat);
    const double scale = kParseRandomScale;
    out->value.set_real(static_cast<float>(((static_cast<double>(to) - from) * pick * scale) + from));
    return token + kTokenWithPairBytes;
}

// 0x00479730
void evaluate_name_token(Component* component)
{
    if (value_kind(*component) != QbValueKind::kName) {
        return;
    }
    const SymbolTableEntry* symbol = resolve_symbol(component->value.word());
    if (!symbol) {
        return;
    }
    switch (symbol->type()) {
    case QbValueKind::kInteger:
    case QbValueKind::kFloat:
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
    case QbValueKind::kPair:
    case QbValueKind::kVector:
    case QbValueKind::kStruct:
    case QbValueKind::kArray:
    case QbValueKind::kName:
        change_species(component, symbol->type());
        component->value = symbol->value();
        break;
    default:
        break;
    }
}

// 0x004706F0
Tokens parse_value(Tokens token, Struct* args, Component* out)
{
    Tokens cursor = token + 1;
    switch (as_token(*token)) {
    case QbToken::kStructStart: {
        gsl::owner<Struct*> structure = script::new_struct();
        cursor = parse_structure(structure, token, args);
        change_species(out, QbValueKind::kStruct);
        out->value.set_structure(structure);
        return cursor;
    }
    case QbToken::kArrayStart: {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        gsl::owner<Array*> array = script::new_array();
        memory::leave_heap_scope();
        cursor = parse_array(array, token, args);
        change_species(out, QbValueKind::kArray);
        out->value.set_array(array);
        return cursor;
    }
    case QbToken::kName:
        change_species(out, QbValueKind::kName);
        out->value.set_word(fetch_word32(cursor));
        return token + kTokenWithWordBytes;
    case QbToken::kInteger:
        change_species(out, QbValueKind::kInteger);
        out->value.set_word(fetch_word32(cursor));
        return token + kTokenWithWordBytes;
    case QbToken::kFloat:
        change_species(out, QbValueKind::kFloat);
        out->value.set_word(fetch_word32(cursor));
        return token + kTokenWithWordBytes;
    case QbToken::kString:
    case QbToken::kLocalString: {
        change_species(out, as_token(*token) == QbToken::kString ? QbValueKind::kString : QbValueKind::kInlineText);
        const uint32_t length = fetch_word32(cursor);
        out->value.set_text(spawn_text(raw::chars((token + kTokenWithWordBytes).address())));
        return token + kTokenWithWordBytes + length;
    }
    case QbToken::kVector: {
        change_species(out, QbValueKind::kVector);
        uint32_t x = fetch_word32(cursor);
        uint32_t y = fetch_word32(token + kTokenWithWordBytes);
        uint32_t z = fetch_word32(token + kTokenWithPairBytes);
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        out->value.set_vector(vector);
        std::memcpy(&vector->x(), &x, 4);
        std::memcpy(&out->value.vector()->y(), &y, 4);
        std::memcpy(&out->value.vector()->z(), &z, 4);
        return token + kTokenWithVectorBytes;
    }
    case QbToken::kPair: {
        change_species(out, QbValueKind::kPair);
        uint32_t x = fetch_word32(cursor);
        uint32_t y = fetch_word32(token + kTokenWithWordBytes);
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        out->value.set_pair(pair);
        std::memcpy(&pair->x(), &x, 4);
        std::memcpy(&out->value.pair()->y(), &y, 4);
        return token + kTokenWithPairBytes;
    }
    case QbToken::kScriptStart: { // a script: its name goes in the component's name
        out->name = fetch_word32(token + 2);
        const Tokens start = token + 6;
        Tokens end = skip_to_end_of_script(start);
        change_species(out, QbValueKind::kScriptBody);
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
    case QbToken::kAllArguments: // <...>: all the caller's parameters
        if (args) {
            gsl::owner<Struct*> structure = script::new_struct();
            structure->get_assign(args);
            change_species(out, QbValueKind::kStruct);
            out->value.set_structure(structure);
        }
        return token + 1;
    case QbToken::kLocalArgument: { // <name>: one of the caller's parameters
        const uint32_t name = fetch_word32(token + 2);
        if (args) {
            if (const Component* found = args->find_last_component(name)) {
                duplicate_component(out, found, 0);
            }
        }
        return token + kTokenWithNamedWordBytes;
    }
    case QbToken::kRandomRange:
        return parse_random_range(cursor, out, false);
    case QbToken::kRandomRangeSideStream:
        return parse_random_range(cursor, out, true);
    default:
        return token + 1;
    }
}

// 0x00470130
Tokens add_parameter(Tokens token, uint32_t name, Struct* dest, Struct* args)
{
    Tokens cursor = token + 1;
    bool deterministic = false;
    switch (as_token(*token)) {
    case QbToken::kName:
        dest->put_name_key(checksums::Name{name}, fetch_word32(cursor));
        return token + kTokenWithWordBytes;
    case QbToken::kInteger:
        dest->put_int(checksums::Name{name}, static_cast<int>(fetch_word32(cursor)));
        return token + kTokenWithWordBytes;
    case QbToken::kFloat:
        dest->push_float(checksums::Name{name}, get_as_float(fetch_word32(cursor)));
        return token + kTokenWithWordBytes;
    case QbToken::kVector: {
        const float x = get_as_float(fetch_word32(cursor));
        const float y = get_as_float(fetch_word32(token + 5));
        const float z = get_as_float(fetch_word32(token + 9));
        // (a zero vector named 0x9D2D0915 isn't added)
        const float tolerance = render::kOnePercent;
        if (name != kAngles || !(std::fabs(x) < tolerance) || !(std::fabs(y) < tolerance) ||
            !(std::fabs(z) < tolerance)) {
            dest->put_vec3(checksums::Name{name}, {.x = x, .y = y, .z = z});
        }
        return token + kTokenWithVectorBytes;
    }
    case QbToken::kPair:
        dest->insert_pair(checksums::Name{name}, {.x = get_as_float(fetch_word32(cursor)),
                                                  .y = get_as_float(fetch_word32(token + kTokenWithWordBytes))});
        return token + kTokenWithPairBytes;
    case QbToken::kString: {
        const uint32_t length = fetch_word32(cursor);
        dest->append_word(name, raw::chars((token + kTokenWithWordBytes).address()));
        return token + kTokenWithWordBytes + length;
    }
    case QbToken::kLocalString: {
        const uint32_t length = fetch_word32(cursor);
        dest->append_inline_text(name, raw::chars((token + kTokenWithWordBytes).address()));
        return token + kTokenWithWordBytes + length;
    }
    case QbToken::kStructStart: {
        gsl::owner<Struct*> structure = script::new_struct();
        cursor = parse_structure(structure, token, args);
        dest->append_struct_ref(name, structure);
        return cursor;
    }
    case QbToken::kArrayStart: {
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        gsl::owner<Array*> array = script::new_array();
        memory::leave_heap_scope();
        cursor = parse_array(array, token, args);
        dest->append_array_ref(name, array);
        return cursor;
    }
    case QbToken::kScriptStart: { // a script, under its own name
        const uint32_t script_name = fetch_word32(token + 2);
        const Tokens start = token + 6;
        Tokens end = skip_to_end_of_script(start);
        dest->append_script(script_name, code_between(start, end));
        return end;
    }
    case QbToken::kLocalArgument: { // <name>
        const uint32_t reference = fetch_word32(token + 2);
        cursor = token + kTokenWithNamedWordBytes;
        if (!args) {
            return cursor;
        }
        const Component* found = args->find_last_component(reference);
        if (!found) {
            return cursor;
        }
        if (name == 0 && value_kind(*found) == QbValueKind::kStruct) {
            dest->append_structure_if_any(found->value.structure());
            return cursor;
        }
        gsl::owner<Component*> copy = new_component();
        duplicate_component(copy, found, 0);
        copy->name = name;
        dest->attach_module(copy);
        return cursor;
    }
    case QbToken::kAllArguments: // <...>
        if (args) {
            if (name != 0U) {
                gsl::owner<Struct*> structure = script::new_struct();
                structure->get_assign(args);
                dest->append_struct_ref(name, structure);
            } else {
                dest->merge_structure(args);
            }
        }
        return cursor;
    case QbToken::kRandomRangeSideStream:
        deterministic = true;
        [[fallthrough]];
    case QbToken::kRandomRange: {
        gsl::owner<Component*> value = new_component();
        cursor = parse_random_range(cursor, value, deterministic);
        value->name = name;
        dest->attach_module(value);
        return cursor;
    }
    case QbToken::kComma:
        return cursor;
    default:
        return token;
    }
}

// 0x00470F30
Tokens parse_arguments(Struct* dest, Tokens token, Struct* args)
{
    for (;;) {
        token = resolve_random_jumps(token);
        const QbToken kind = as_token(*token);
        if (kind == QbToken::kEndOfLine || kind == QbToken::kEndOfLineNumbered || kind == QbToken::kEndOfFile ||
            kind == QbToken::kCloseParenthesis) {
            return token;
        }
        switch (kind) {
        case QbToken::kStructStart:
            token = parse_structure(dest, token, args);
            break;
        case QbToken::kComma:
            ++token;
            break;
        case QbToken::kAllArguments:
            ++token;
            if (args) {
                dest->merge_structure(args);
            }
            break;
        case QbToken::kLocalArgument:
            token = add_parameter(token, 0, dest, args);
            break;
        case QbToken::kName: {
            const uint32_t name = fetch_word32(token + 1);
            Tokens next = resolve_random_jumps(token + kTokenWithWordBytes);
            if (as_token(*next) != QbToken::kEquals) {
                // A bare name: a flag.
                token = add_parameter(token, 0, dest, args);
                break;
            }
            next = resolve_random_jumps(next + 1);
            if (as_token(*next) == QbToken::kOpenParenthesis) {
                token = add_named_expression(next, name, *dest, args);
            } else {
                token = add_parameter(next, name, dest, args);
            }
            break;
        }
        default:
            if (kind == QbToken::kOpenParenthesis) {
                token = add_expression(token, *dest, args);
            } else {
                token = add_parameter(token, 0, dest, args);
            }
            break;
        }
    }
}

// 0x00471860
Tokens parse_structure(Struct* dest, Tokens token, Struct* args)
{
    Tokens cursor = token + 1;
    for (;;) {
        cursor = skip_end_of_lines_in(cursor);
        cursor = resolve_random_jumps(cursor);
        const QbToken kind = as_token(*cursor);
        if (kind == QbToken::kStructEnd) {
            return cursor + 1;
        }
        switch (kind) {
        case QbToken::kEndOfLine:
        case QbToken::kEndOfLineNumbered:
            cursor = skip_end_of_lines_in(cursor);
            break;
        case QbToken::kStructStart:
            cursor = parse_structure(dest, cursor, args);
            break;
        case QbToken::kComma:
            ++cursor;
            break;
        case QbToken::kName: {
            const uint32_t name = fetch_word32(cursor + 1);
            Tokens next = resolve_random_jumps(skip_end_of_lines_in(cursor + kTokenWithWordBytes));
            if (as_token(*next) != QbToken::kEquals) {
                cursor = add_parameter(cursor, 0, dest, args);
                break;
            }
            next = resolve_random_jumps(skip_end_of_lines_in(next + 1));
            if (as_token(*next) == QbToken::kOpenParenthesis) {
                cursor = add_named_expression(next, name, *dest, args);
            } else {
                cursor = add_parameter(next, name, dest, args);
            }
            break;
        }
        default:
            if (kind == QbToken::kOpenParenthesis) {
                cursor = add_expression(cursor, *dest, args);
            } else {
                cursor = add_parameter(cursor, 0, dest, args);
            }
            break;
        }
    }
}

namespace {

// An array element's vector made from its three words at `cursor`.
Vector* read_vector_element(Tokens cursor)
{
    Vector* vector = alloc_script_vector();
    vector = vector ? new (vector) script::Vector : nullptr;
    uint32_t x = fetch_word32(cursor);
    uint32_t y = fetch_word32(cursor + 4);
    uint32_t z = fetch_word32(cursor + kVectorZOffset);
    std::memcpy(&vector->x(), &x, 4);
    std::memcpy(&vector->y(), &y, 4);
    std::memcpy(&vector->z(), &z, 4);
    return vector;
}

// An array element's pair made from its two words at `cursor`.
Pair* read_pair_element(Tokens cursor)
{
    Pair* pair = new_pair();
    pair = pair ? new (pair) script::Pair : nullptr;
    uint32_t x = fetch_word32(cursor);
    uint32_t y = fetch_word32(cursor + 4);
    std::memcpy(&pair->x(), &x, 4);
    std::memcpy(&pair->y(), &y, 4);
    return pair;
}

// parse_array's second pass: the array's elements of `type` read into `dest` from its first token on; the
// closing token returned.
Tokens read_elements(Array* dest, Tokens first, QbValueKind type, Struct* args)
{
    Tokens cursor = resolve_random_jumps(first);
    uint32_t element = 0; // (the next element written)
    while (as_token(*cursor) != QbToken::kArrayEnd) {
        const QbToken kind = as_token(*cursor);
        switch (type) {
        case QbValueKind::kStruct:
            // (no step first; unknown tokens don't move on: the original loops here)
            if (kind == QbToken::kEndOfLine || kind == QbToken::kComma) {
                ++cursor;
            } else if (kind == QbToken::kEndOfLineNumbered) {
                cursor += kTokenWithWordBytes;
            } else if (kind == QbToken::kStructStart) {
                gsl::owner<Struct*> structure = script::new_struct();
                cursor = parse_structure(structure, cursor, args);
                dest->set_cell(element++, Cell::of(structure));
            }
            break;
        case QbValueKind::kArray:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == QbToken::kArrayStart) {
                gsl::owner<Array*> array = script::new_array();
                cursor = parse_array(array, cursor - 1, args);
                dest->set_cell(element++, Cell::of(array));
            }
            break;
        case QbValueKind::kInteger:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == QbToken::kInteger || kind == QbToken::kHexInteger) {
                dest->set_cell(element++, Cell::of_word(fetch_word32(cursor)));
                cursor += 4;
            }
            break;
        case QbValueKind::kFloat:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == QbToken::kInteger || kind == QbToken::kHexInteger) {
                dest->set_cell(element++,
                               Cell::of_real(static_cast<float>(static_cast<int32_t>(fetch_word32(cursor)))));
                cursor += 4;
            } else if (kind == QbToken::kFloat) {
                dest->set_cell(element++, Cell::of_word(fetch_word32(cursor)));
                cursor += 4;
            }
            break;
        case QbValueKind::kName:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == QbToken::kName) {
                dest->set_cell(element++, Cell::of_word(fetch_word32(cursor)));
                cursor += 4;
            }
            break;
        case QbValueKind::kString:
        case QbValueKind::kInlineText:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == (type == QbValueKind::kString ? QbToken::kString : QbToken::kLocalString)) {
                const uint32_t length = fetch_word32(cursor);
                cursor += 4;
                dest->set_cell(element++, Cell::of_text(spawn_text(raw::chars(cursor.address()))));
                cursor += length;
            }
            break;
        case QbValueKind::kVector:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == QbToken::kVector) {
                Vector* vector = read_vector_element(cursor);
                cursor += kVectorPayloadBytes;
                dest->set_cell(element++, Cell::of(vector));
            }
            break;
        case QbValueKind::kPair:
            ++cursor;
            if (kind == QbToken::kEndOfLineNumbered) {
                cursor += 4;
            } else if (kind == QbToken::kPair) {
                Pair* pair = read_pair_element(cursor);
                cursor += kPairPayloadBytes;
                dest->set_cell(element++, Cell::of(pair));
            }
            break;
        default:
            break;
        }
        cursor = resolve_random_jumps(cursor);
    }
    return cursor;
}

} // namespace

// 0x00471B00
Tokens parse_array(Array* dest, Tokens token, Struct* args)
{
    const Tokens first = token + 1;
    Tokens cursor = resolve_random_jumps(first);
    QbValueKind type = QbValueKind::kNone;
    uint32_t count = 0;
    if (as_token(*cursor) == QbToken::kArrayEnd) {
        return cursor + 1;
    }
    for (bool again = true; again; again = as_token(*cursor) != QbToken::kArrayEnd) {
        const QbToken kind = as_token(*cursor);
        switch (kind) {
        case QbToken::kStructStart:
            type = QbValueKind::kStruct;
            break;
        case QbToken::kArrayStart:
            type = QbValueKind::kArray;
            break;
        case QbToken::kName:
            type = QbValueKind::kName;
            break;
        case QbToken::kInteger:
        case QbToken::kHexInteger:
            if (type != QbValueKind::kFloat) {
                type = QbValueKind::kInteger;
            }
            break;
        case QbToken::kFloat:
            type = QbValueKind::kFloat;
            break;
        case QbToken::kString:
            type = QbValueKind::kString;
            break;
        case QbToken::kLocalString:
            type = QbValueKind::kInlineText;
            break;
        case QbToken::kVector:
            type = QbValueKind::kVector;
            break;
        case QbToken::kPair:
            type = QbValueKind::kPair;
            break;
        default:
            break;
        }
        switch (kind) {
        case QbToken::kEndOfLine:
        case QbToken::kEndOfLineNumbered:
        case QbToken::kComma:
            cursor = step_over_token(cursor);
            break;
        case QbToken::kStructStart:
        case QbToken::kArrayStart:
        case QbToken::kName:
        case QbToken::kInteger:
        case QbToken::kHexInteger:
        case QbToken::kFloat:
        case QbToken::kString:
        case QbToken::kLocalString:
        case QbToken::kVector:
        case QbToken::kPair:
            ++count;
            cursor = skip_value(cursor);
            break;
        default:
            // (anything else doesn't move on: the original loops here)
            break;
        }
        cursor = resolve_random_jumps(cursor);
    }
    if (type == QbValueKind::kNone) {
        return cursor + 1;
    }

    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    dest->configure_size_kind(count, type);
    memory::leave_heap_scope();
    if (type == QbValueKind::kScriptBody || type == QbValueKind::kNativeFunc || type == QbValueKind::kNativeMethod ||
        type == QbValueKind::kStructHandedOver) {
        return token;
    }
    cursor = read_elements(dest, first, type, args);
    return cursor + 1;
}

namespace {

// parse_expression's name just read into `value`: a global value's (unless after a dot) - a native function
// called with the arguments that follow - or the name itself, pushed to the evaluator (a native method isn't).
Tokens push_name(Tokens cursor, Struct* args, Component& value, bool after_dot)
{
    ValueResolver& evaluator = script_state().evaluator();
    const SymbolTableEntry* symbol = after_dot ? nullptr : resolve_symbol(value.value.word());
    if (symbol) {
        bool push = true;
        switch (symbol->type()) {
        case QbValueKind::kInteger:
        case QbValueKind::kFloat:
        case QbValueKind::kString:
        case QbValueKind::kInlineText:
        case QbValueKind::kPair:
        case QbValueKind::kVector:
        case QbValueKind::kName:
            change_species(&value, symbol->type());
            value.value = symbol->value();
            break;
        case QbValueKind::kStruct:
            change_species(&value, QbValueKind::kStructRef);
            value.value = symbol->value();
            break;
        case QbValueKind::kArray:
            change_species(&value, QbValueKind::kArrayRef);
            value.value = symbol->value();
            break;
        case QbValueKind::kNativeFunc: {
            // Called now, with the arguments that follow.
            Script* script = get_running_script();
            change_species(&value, QbValueKind::kInteger);
            gsl::owner<Struct*> params = script::new_struct();
            cursor = parse_arguments(params, cursor, args);
            // (the byte the function returns, as it is)
            const auto routine = symbol->value().function();
            value.value.set_word(static_cast<uint32_t>(routine(params, Context(script))));
            if (params) {
                params->get_clear();
                delete_struct(params);
            }
            break;
        }
        case QbValueKind::kNativeMethod:
            push = false;
            break;
        default:
            break;
        }
        if (push) {
            evaluator.intake(&value);
        }
    } else {
        evaluator.intake(&value);
    }
    return cursor;
}

} // namespace

// 0x004712B0
Tokens parse_expression(Tokens token, Struct* args, Component* result)
{
    ValueResolver& evaluator = script_state().evaluator();
    Component& value = get_scratch();
    evaluator.reset_if_dirty();
    evaluator.set_read_cursor(token.address());
    Tokens cursor = token + 1;
    int parentheses = 0;
    bool after_dot = false;  // a name after '.' is a member name, not a global value
    bool have_value = false; // (for a negative number to be taken as a subtraction)
    bool expect_value = true;
    auto pushed = [&] {
        have_value = true;
        expect_value = false;
    };
    for (;;) {
        cursor = resolve_random_jumps(cursor);
        const QbToken kind = as_token(*cursor);
        switch (kind) {
        case QbToken::kEndOfLine:
        case QbToken::kArrayEnd:
            ++cursor;
            continue;
        case QbToken::kEndOfLineNumbered:
            cursor += kTokenWithWordBytes;
            continue;
        case QbToken::kStructStart:
        case QbToken::kString:
        case QbToken::kLocalString:
        case QbToken::kVector:
        case QbToken::kPair:
        case QbToken::kRandomRange:
            if (!expect_value) {
                break;
            }
            cursor = parse_value(cursor, args, &value);
            evaluator.intake(&value);
            release_component(&value);
            pushed();
            continue;
        case QbToken::kArrayStart:
        case QbToken::kEquals:
        case QbToken::kDot:
        case QbToken::kMinus:
        case QbToken::kPlus:
        case QbToken::kDivide:
        case QbToken::kMultiply:
        case QbToken::kLessThan:
        case QbToken::kGreaterThan:
        case QbToken::kOr:
        case QbToken::kAnd: // operators
            evaluator.intake(kind);
            ++cursor;
            after_dot = kind == QbToken::kDot;
            have_value = false;
            expect_value = true;
            continue;
        case QbToken::kOpenParenthesis:
            if (!expect_value) {
                break;
            }
            ++cursor;
            evaluator.open_bracket();
            ++parentheses;
            have_value = false;
            expect_value = true;
            continue;
        case QbToken::kCloseParenthesis:
            ++cursor;
            if (parentheses == 0) {
                break;
            }
            evaluator.close_bracket();
            expect_value = false;
            --parentheses;
            have_value = true;
            continue;
        case QbToken::kName: {
            if (!expect_value) {
                break;
            }
            cursor = parse_value(cursor, args, &value);
            cursor = push_name(cursor, args, value, after_dot);
            value.type_and_flag_ &= 1;
            value.value.clear();
            pushed();
            continue;
        }
        case QbToken::kInteger:
        case QbToken::kFloat: {
            if (expect_value) {
                cursor = parse_value(cursor, args, &value);
                evaluator.intake(&value);
                release_component(&value);
                pushed();
                continue;
            }
            // A negative number right after a value: a subtraction ("a -1" is compiled as "a" "-1").
            const Tokens next = parse_value(cursor, args, &value);
            const bool negative = kind == QbToken::kInteger ? value.value.integer() < 0 : value.value.real() < 0.0F;
            if (!negative || !have_value) {
                break;
            }
            evaluator.intake(QbToken::kMinus);
            if (kind == QbToken::kInteger) {
                value.value.set_integer(-value.value.integer());
            } else {
                value.value.set_real(-value.value.real());
            }
            evaluator.intake(&value);
            release_component(&value);
            cursor = next;
            pushed();
            continue;
        }
        case QbToken::kAllArguments: // <...>
            if (!expect_value) {
                break;
            }
            ++cursor;
            if (args) {
                change_species(&value, QbValueKind::kStructRef);
                value.value.set_structure(args);
                evaluator.intake(&value);
            }
            pushed();
            continue;
        case QbToken::kLocalArgument: { // <name>
            if (!expect_value) {
                break;
            }
            const uint32_t name = fetch_word32(cursor + 2);
            cursor += kTokenWithNamedWordBytes;
            const Component* found = args ? args->find_last_component(name) : nullptr;
            if (found) {
                copy_value_kind(value, *found);
                value.value = found->value;
                evaluate_name_token(&value);
                make_reference(&value);
                evaluator.intake(&value);
                value.type_and_flag_ &= 1;
                value.value.clear();
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
    return cursor;
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

// (NOT ORIGINAL: the operand is the entry's / function's number - names.h - where the original wrote its address)
const MemberFunctionEntry* member_entry_at(Tokens bytes) { return numbered_member_entry(fetch_word32(bytes)); }
NativeFunction native_function_at(Tokens bytes) { return numbered_native_function(fetch_word32(bytes)); }

} // namespace script
