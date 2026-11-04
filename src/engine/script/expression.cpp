#include "engine/script/expression.h"

#include "engine/script/qb.h"
#include "engine/core/enum_ops.h"
#include "engine/memory/mem.h"
#include "engine/core/checked_index.h"
#include "engine/script/param_struct.h"
#include "engine/core/no_destroy.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <gsl/pointers>
#include <span>
#include "engine/core/span_copy.h"

namespace script {


namespace {
// Longest joined string (with its terminator); the evaluator's value and operator stacks.
constexpr size_t kMaxJoinedLength = 200;
constexpr float kEvalDivisorMinimum{0.000001F};

// The operators' precedences, by operator.
constexpr std::array<int, 74> kOperatorPrecedence{{-1, -1, -1, -1, -1, 100, -1, 76, 100, -1, 98, 98, 99, 100, -1, -1, -1, -1, 80,
                              79, 78, 77, -1, -1, -1,  -1, -1, -1,  -1, -1, -1, -1, -1,  -1, -1, -1, -1, -1,
                              -1, -1, -1, -1, -1, -1,  -1, -1, -1,  -1, -1, -1, 58, 60,  59, 90, 89, -1, -1,
                              -1, -1, -1, -1, -1, -1,  -1, -1, -1,  -1, -1, -1, -1, -1,  -1, -1, -1}};
int precedence(QbToken op) { return core::at(kOperatorPrecedence, core::to_underlying(op)); }

// Where an operation's result is built (a global component: only its type and value are taken).
Component& get_result()
{
    static core::NoDestroy<Component> eval_result;
    return eval_result.get();
}

// The error messages are the game's own strings.

void set_result_type(QbValueKind type)
{
    set_value_kind(get_result(), type);
}

// Mixed integer / float arithmetic is done as the x87 does it: exactly, rounded to a float at the end.
double as_double(int32_t value) { return static_cast<double>(value); }
double as_double(float value) { return static_cast<double>(value); }
float get_rounded(double value) { return static_cast<float>(value); }

void set_result_float(double value)
{
    set_result_type(QbValueKind::kFloat);
    get_result().value.set_real(get_rounded(value));
}
void set_result_integer(uint32_t value)
{
    set_result_type(QbValueKind::kInteger);
    get_result().value.set_word(value);
}

Pair* new_result_pair()
{
    set_result_type(QbValueKind::kPair);
    Pair* pair = new_pair();
    pair = pair ? new (pair) script::Pair : nullptr;
    get_result().value.set_pair(pair);
    return pair;
}
Vector* new_result_vector()
{
    set_result_type(QbValueKind::kVector);
    Vector* vector = alloc_script_vector();
    vector = vector ? new (vector) script::Vector : nullptr;
    get_result().value.set_vector(vector);
    return vector;
}
Struct* new_result_structure()
{
    set_result_type(QbValueKind::kStruct);
    gsl::owner<Struct*> structure = script::new_struct();
    get_result().value.set_structure(structure); // (the result's now)
    Struct* const made = structure;
    return made;
}

bool is_structure(QbValueKind type) { return type == QbValueKind::kStruct || type == QbValueKind::kStructRef; }
bool is_array(QbValueKind type) { return type == QbValueKind::kArray || type == QbValueKind::kArrayRef; }

// Two strings joined (at most 200 characters with the terminator), or null if too long.
bool join_strings(const char* a, const char* b, QbValueKind type)
{
    const size_t length_a = std::strlen(a);
    const size_t length_b = std::strlen(b);
    if (length_a + length_b + 1 > kMaxJoinedLength) {
        return false;
    }
    constexpr size_t kJoinedSize = 200;
    std::array<char, kJoinedSize> joined{};
    core::copy_into(std::span<const char>(a, length_a), std::span<char>(joined));
    core::copy_into(std::span<const char>(b, length_b + 1), std::span<char>(joined).subspan(length_a)); // (with b's terminator)
    set_result_type(type);
    get_result().value.set_text(spawn_text(joined.data()));
    return true;
}

} // namespace

// 0x0047A910
void copy_array_element_to_component(Array* array, uint32_t index, Component* to, int reference)
{
    set_value_kind(*to, array->kind_tag());
    switch (array->kind_tag()) {
    case QbValueKind::kInteger:
    case QbValueKind::kName:
        to->value.set_word(array->fetch_part(index));
        break;
    case QbValueKind::kFloat:
        to->value.set_real(array->fetch_float(index));
        break;
    case QbValueKind::kString:
        to->value.set_text(spawn_text(array->find_inline_text(index)));
        break;
    case QbValueKind::kPair: {
        auto* from = array->pair_at(index);
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        to->value.set_pair(pair);
        pair->x() = from->x();
        to->value.pair()->y() = from->y();
        break;
    }
    case QbValueKind::kVector: {
        auto* from = array->vector_at(index);
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        to->value.set_vector(vector);
        vector->x() = from->x();
        to->value.vector()->y() = from->y();
        to->value.vector()->z() = from->z();
        break;
    }
    case QbValueKind::kStruct:
        if (reference != 0) {
            set_value_kind(*to, QbValueKind::kStructRef);
            to->value.set_word(array->fetch_part(index));
        } else {
            gsl::owner<Struct*> structure = spawn_param_bag();
            to->value.set_structure(structure ? memory::construct_in<script::Struct>(structure) : nullptr);
            to->value.structure()->append_structure_if_any(array->struct_at(index));
        }
        break;
    case QbValueKind::kArray:
        if (reference != 0) {
            set_value_kind(*to, QbValueKind::kArrayRef);
            to->value.set_word(array->fetch_part(index));
        } else {
            gsl::owner<Array*> copy = alloc_script_array();
            to->value.set_array(copy ? memory::construct_in<script::Array>(copy) : nullptr);
            duplicate_array(to->value.array(), array->array_at(index));
        }
        break;
    default:
        // (local strings and the rest can't be taken out of arrays: the component is left empty)
        to->type_and_flag_ &= 1;
        to->value.set_word(0);
        break;
    }
}

namespace {

// A component's value and type, a name standing for a global symbol counting as the symbol's.
void resolved_value(const Component* component, QbValueKind* type, Cell* value)
{
    *type = value_kind(*component);
    *value = component->value;
    if (*type == QbValueKind::kName) {
        if (const SymbolTableEntry* symbol = resolve_symbol(value->word())) {
            *value = symbol->value();
            *type = symbol->type();
        }
    }
}

double get_as_float(Cell value) { return value.real(); }
double get_as_integer(Cell value) { return value.integer(); }

// a < b: numbers by value; strings by length (!); structures: a is a subset of b.
bool get_less(QbValueKind type_a, Cell a, QbValueKind type_b, Cell b)
{
    switch (type_a) {
    case QbValueKind::kInteger:
        if (type_b == QbValueKind::kFloat) {
            return get_as_integer(a) < get_as_float(b);
        }
        return type_b == QbValueKind::kInteger && a.integer() < b.integer();
    case QbValueKind::kFloat:
        if (type_b == QbValueKind::kInteger) {
            return get_as_float(a) < get_as_integer(b);
        }
        return type_b == QbValueKind::kFloat && get_as_float(a) < get_as_float(b);
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        return type_b == type_a && std::strlen(a.text()) < std::strlen(b.text());
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        return (type_b == QbValueKind::kStruct || type_b == QbValueKind::kStructRef) && a.structure()->is_subset_of(b.structure());
    default:
        return false;
    }
}

} // namespace

// 0x00479410 (a member of the first component in the game)
bool less_than(Component* a, Component* b)
{
    QbValueKind type_a = QbValueKind::kNone;
    QbValueKind type_b = QbValueKind::kNone;
    Cell value_a;
    Cell value_b;
    resolved_value(a, &type_a, &value_a);
    resolved_value(b, &type_b, &value_b);
    return get_less(type_a, value_a, type_b, value_b);
}

// 0x004795A0
bool greater_than(Component* a, Component* b)
{
    QbValueKind type_a = QbValueKind::kNone;
    QbValueKind type_b = QbValueKind::kNone;
    Cell value_a;
    Cell value_b;
    resolved_value(a, &type_a, &value_a);
    resolved_value(b, &type_b, &value_b);
    // (the same cases, decided by a's type)
    switch (type_a) {
    case QbValueKind::kInteger:
    case QbValueKind::kFloat:
        if (type_b != QbValueKind::kInteger && type_b != QbValueKind::kFloat) {
            return false;
        }
        if (type_a == QbValueKind::kInteger && type_b == QbValueKind::kInteger) {
            return value_a.integer() > value_b.integer();
        }
        return (type_a == QbValueKind::kInteger ? get_as_integer(value_a) : get_as_float(value_a)) >
               (type_b == QbValueKind::kInteger ? get_as_integer(value_b) : get_as_float(value_b));
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        return type_b == type_a && std::strlen(value_a.text()) > std::strlen(value_b.text());
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        // b is a subset of a
        return (type_b == QbValueKind::kStruct || type_b == QbValueKind::kStructRef) &&
               value_b.structure()->is_subset_of(value_a.structure());
    default:
        return false;
    }
}

// 0x0046D160
bool is_reference(const Component* component)
{
    return value_kind(*component) == QbValueKind::kStructRef || value_kind(*component) == QbValueKind::kArrayRef;
}

// 0x0046D180
void make_reference(Component* component)
{
    if (value_kind(*component) == QbValueKind::kStruct) {
        set_value_kind(*component, QbValueKind::kStructRef);
    } else if (value_kind(*component) == QbValueKind::kArray) {
        set_value_kind(*component, QbValueKind::kArrayRef);
    }
}

// 0x0046D1A0
void make_owning(Component* component)
{
    if (value_kind(*component) == QbValueKind::kStructRef) {
        set_value_kind(*component, QbValueKind::kStruct);
    } else if (value_kind(*component) == QbValueKind::kArrayRef) {
        set_value_kind(*component, QbValueKind::kArray);
    }
}

// 0x0046D280
int fetch_flag(const Component* component)
{
    if (value_kind(*component) == QbValueKind::kInteger) {
        return component->value.integer();
    }
    if (value_kind(*component) == QbValueKind::kFloat && component->value.real() != 0.0F) {
        return 1;
    }
    return 0;
}

// 0x0046D380
ValueResolver* ValueResolver::prepare_all()
{
    for (Component& value : values_) {
        construct_component(&value);
    }
    get_clear();
    unknown1f4_ = true;
    return this;
}

// 0x0046D1C0
void ValueResolver::get_clear()
{
    for (Component& value : values_) {
        release_component(&value);
    }
    value_top_ = 0;
    operator_top_ = 0;
    for (Operator& op : operators_) {
        op.op = kNoOperator;
        op.open_parentheses_ = 0;
    }
    has_operators_ = false;
    fault_ = nullptr;
}

// 0x0046D220
void ValueResolver::reset_if_dirty()
{
    if ((value_top_ != 0) || ((core::at(values_, 0).type_and_flag_ & kComponentTypeBits) != 0) ||
        (operator_top_ != 0) || fault_) {
        get_clear();
    }
}

// 0x0046D260
void ValueResolver::change_fault(const char* error)
{
    get_clear();
    fault_ = error;
}

// 0x0046D2B0
void ValueResolver::push_operator(QbToken op)
{
    if (++operator_top_ >= kOperatorStackDepth) {
        get_clear();
        fault_ = "Operator stack overflow"; // "Operator stack overflow"
        return;
    }
    core::at(operators_, operator_top_).op = op;
    core::at(operators_, operator_top_).open_parentheses_ = 0;
    has_operators_ = true;
}

// 0x0046D310
void ValueResolver::intake(const Component* value)
{
    // (the first value goes in the bottom slot)
    if ((value_top_ != 0) || ((core::at(values_, 0).type_and_flag_ & kComponentTypeBits) != 0)) {
        if (++value_top_ >= kValueStackDepth) {
            get_clear();
            fault_ = "Value stack overflow"; // "Value stack overflow"
            return;
        }
    }
    duplicate_component(&core::at(values_, value_top_), value, 0);
}

// 0x0046D370
void ValueResolver::open_bracket() { ++core::at(operators_, operator_top_).open_parentheses_; }

// 0x0046EB70
void ValueResolver::intake(QbToken op)
{
    if (has_operators_) {
        while (core::at(operators_, operator_top_).open_parentheses_ == 0) {
            const QbToken pending = core::at(operators_, operator_top_).op;
            if (op != pending && precedence(op) > precedence(pending)) {
                break;
            }
            execute();
            if (!has_operators_) {
                break;
            }
        }
    }
    push_operator(op);
}

// 0x0046EBF0
void ValueResolver::close_bracket()
{
    while (core::at(operators_, operator_top_).open_parentheses_ == 0) {
        execute();
        if (fault_) {
            return;
        }
    }
    --core::at(operators_, operator_top_).open_parentheses_;
}

// 0x0046EC40
bool ValueResolver::fetch_verdict(Component* result)
{
    while (core::at(operators_, operator_top_).open_parentheses_ == 0) {
        if (value_top_ == 0) {
            if (core::at(operators_, 0).op != kNoOperator || operator_top_ != 0) {
                get_clear();
                fault_ = "Too many operators in expression"; // "Too many operators in expression"
                return false;
            }
            // A reference result is copied as what it refers to.
            if (is_reference(&core::at(values_, 0))) {
                make_owning(&core::at(values_, 0));
                duplicate_component(result, &core::at(values_, 0), 0);
                make_reference(&core::at(values_, 0));
            } else {
                duplicate_component(result, &core::at(values_, 0), 0);
            }
            release_component(&core::at(values_, 0));
            return true;
        }
        execute();
        if (fault_) {
            return false;
        }
    }
    get_clear();
    fault_ = "Not enough close parentheses"; // "Not enough close parentheses"
    return false;
}

namespace {

// The two values an operator works on, and their kinds.
struct Operands {
    const Component* a{};
    const Component* b{};
    QbValueKind type_a{};
    QbValueKind type_b{};
};

// Two arrays joined (an empty one: a copy of the other; the elements taken to be of the first array's type).
void join_arrays(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_b = operands.type_b;
    Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    if (!is_array(type_b)) {
        error(
            "Array add operator requires 2nd arg to be an array as well"); // "Array add operator requires 2nd arg to be an array as well"
        return;
    }
    gsl::owner<Array*> array = script::new_array();
    set_result_type(QbValueKind::kArray);
    result.value.set_array(array);
    const uint32_t size_a = operand_a->value.array()->extent();
    const uint32_t size_b = operand_b->value.array()->extent();
    if (size_a == 0) {
        duplicate_array(array, operand_b->value.array());
    } else if (size_b == 0) {
        duplicate_array(array, operand_a->value.array());
    } else {
        // (the elements are all taken to be of the first array's type)
        const QbValueKind type = operand_a->value.array()->kind_tag();
        array->configure_size_kind(size_a + size_b, type);
        copy_array_elements(array->cells(), operand_a->value.array()->cells(), type);
        copy_array_elements(array->cells().subspan(size_a), operand_b->value.array()->cells(), type);
    }
}

// Addition: numbers, checksums, vectors, pairs, strings, structures (merged), arrays (joined).
void add_values(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_a = operands.type_a;
    const QbValueKind type_b = operands.type_b;
    Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    switch (type_a) {
    case QbValueKind::kInteger:
        if (type_b == QbValueKind::kInteger) {
            set_result_integer(static_cast<uint32_t>(operand_a->value.integer()) +
                               static_cast<uint32_t>(operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            set_result_float(as_double(operand_a->value.integer()) + as_double(operand_b->value.real()));
        } else {
            error("Second arg cannot be added to an integer"); // "Second arg cannot be added to an integer"
        }
        break;
    case QbValueKind::kFloat:
        if (type_b == QbValueKind::kInteger) {
            set_result_float(as_double(operand_b->value.integer()) + as_double(operand_a->value.real()));
        } else if (type_b == QbValueKind::kFloat) {
            set_result_float(as_double(operand_a->value.real()) + as_double(operand_b->value.real()));
        } else {
            error("Second arg cannot be added to a float"); // "Second arg cannot be added to a float"
        }
        break;
    case QbValueKind::kName:
        // A checksum plus an integer or checksum is a checksum.
        if (type_b == QbValueKind::kInteger || type_b == QbValueKind::kName) {
            set_result_type(QbValueKind::kName);
            result.value.set_word(operand_b->value.word() + operand_a->value.word());
        } else {
            error("Second arg cannot be added to a checksum"); // "Second arg cannot be added to a checksum"
        }
        break;
    case QbValueKind::kVector:
        if (type_b == QbValueKind::kVector) {
            Vector* vector = new_result_vector();
            vector->x() = operand_a->value.vector()->x() + operand_b->value.vector()->x();
            result.value.vector()->y() = operand_a->value.vector()->y() + operand_b->value.vector()->y();
            result.value.vector()->z() = operand_a->value.vector()->z() + operand_b->value.vector()->z();
        } else {
            error("Second arg cannot be added to a vector"); // "Second arg cannot be added to a vector"
        }
        break;
    case QbValueKind::kPair:
        if (type_b == QbValueKind::kPair) {
            Pair* pair = new_result_pair();
            pair->x() = operand_a->value.pair()->x() + operand_b->value.pair()->x();
            result.value.pair()->y() = operand_a->value.pair()->y() + operand_b->value.pair()->y();
        } else {
            error("Second arg cannot be added to a pair"); // "Second arg cannot be added to a pair"
        }
        break;
    case QbValueKind::kString:
        if (type_b != QbValueKind::kString) {
            error("Second arg cannot be added to a string"); // "Second arg cannot be added to a string"
        } else if (!join_strings(operand_a->value.text(), operand_b->value.text(), QbValueKind::kString)) {
            error("Result of string addition too long"); // "Result of string addition too long"
        }
        break;
    case QbValueKind::kInlineText:
        if (type_b != QbValueKind::kInlineText) {
            error("Second arg cannot be added to a local-string"); // "Second arg cannot be added to a local-string"
        } else if (!join_strings(operand_a->value.text(), operand_b->value.text(), QbValueKind::kInlineText)) {
            error("Result of local-string addition too long"); // "Result of local-string addition too long"
        }
        break;
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        if (is_structure(type_b)) {
            Struct* structure = new_result_structure();
            structure->append_structure_if_any(operand_a->value.structure());
            result.value.structure()->append_structure_if_any(operand_b->value.structure());
        } else {
            error(
                "Structure add operator requires 2nd arg to be a structure"); // "Structure add operator requires 2nd arg to be a structure"
        }
        break;
    case QbValueKind::kArray:
    case QbValueKind::kArrayRef:
        join_arrays(resolver, operands);
        break;
    default:
        error("Addition not supported for this type"); // "Addition not supported for this type"
        break;
    }
}

// A structure without a name (as a parameter or a flag), each name of an array of names, or another structure's
// parameters (by name and type) and flags.
void subtract_from_structure(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_b = operands.type_b;
    const Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    if (type_b == QbValueKind::kName) {
        // Without the name (as a parameter or a flag).
        Struct* structure = new_result_structure();
        structure->append_structure_if_any(operand_a->value.structure());
        result.value.structure()->erase_module(operand_b->value.word());
        result.value.structure()->remove_flag(operand_b->value.word());
    } else if (is_array(type_b)) {
        // Without each name in the array.
        Array* names = operand_b->value.array();
        if (names->kind_tag() != QbValueKind::kName && names->kind_tag() != QbValueKind::kNone) {
            // "Subtracting an array from a structure requires the array to be an array of names"
            error("Subtracting an array from a structure requires the array to be an array of names");
            return;
        }
        Struct* structure = new_result_structure();
        structure->append_structure_if_any(operand_a->value.structure());
        for (uint32_t i = 0; i < names->extent(); ++i) {
            result.value.structure()->erase_module(names->fetch_part(i));
            result.value.structure()->remove_flag(names->fetch_part(i));
        }
    } else if (is_structure(type_b)) {
        // Without each of the other structure's parameters (by name and type) and flags.
        Struct* structure = new_result_structure();
        structure->append_structure_if_any(operand_a->value.structure());
        Struct* other = operand_b->value.structure();
        for (Component* each = other->step_to_entry(nullptr); each; each = other->step_to_entry(each)) {
            if (each->name != 0U) {
                result.value.structure()->remove_component_of_type(each->name, value_kind(*each));
            } else if (value_kind(*each) == QbValueKind::kName) {
                result.value.structure()->erase_module(each->value.word());
                result.value.structure()->remove_flag(each->value.word());
            }
        }
    } else {
        // "Structure minus operator requires 2nd arg to be a name, an array of names, or a structure"
        error("Structure minus operator requires 2nd arg to be a name, an array of names, or a structure");
    }
}

// Subtraction: numbers, pairs, vectors; a structure without a name, an array of names or another structure's members.
void subtract_values(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_a = operands.type_a;
    const QbValueKind type_b = operands.type_b;
    const Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    switch (type_a) {
    case QbValueKind::kInteger:
        if (type_b == QbValueKind::kInteger) {
            set_result_integer(static_cast<uint32_t>(operand_a->value.integer()) -
                               static_cast<uint32_t>(operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            set_result_float(as_double(operand_a->value.integer()) - as_double(operand_b->value.real()));
        } else {
            error(
                "Second arg cannot be subtracted from an integer"); // "Second arg cannot be subtracted from an integer"
        }
        break;
    case QbValueKind::kFloat:
        if (type_b == QbValueKind::kInteger) {
            set_result_float(as_double(operand_a->value.real()) - as_double(operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            set_result_float(as_double(operand_a->value.real()) - as_double(operand_b->value.real()));
        } else {
            error("Second arg cannot be subtracted from a float"); // "Second arg cannot be subtracted from a float"
        }
        break;
    case QbValueKind::kPair:
        if (type_b == QbValueKind::kPair) {
            Pair* pair = new_result_pair();
            pair->x() = operand_a->value.pair()->x() - operand_b->value.pair()->x();
            result.value.pair()->y() = operand_a->value.pair()->y() - operand_b->value.pair()->y();
        } else {
            error("Second arg cannot be subtracted from a pair"); // "Second arg cannot be subtracted from a pair"
        }
        break;
    case QbValueKind::kVector:
        if (type_b == QbValueKind::kVector) {
            Vector* vector = new_result_vector();
            vector->x() = operand_a->value.vector()->x() - operand_b->value.vector()->x();
            result.value.vector()->y() = operand_a->value.vector()->y() - operand_b->value.vector()->y();
            result.value.vector()->z() = operand_a->value.vector()->z() - operand_b->value.vector()->z();
        } else {
            error("Second arg cannot be subtracted from a vector"); // "Second arg cannot be subtracted from a vector"
        }
        break;
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        subtract_from_structure(resolver, operands);
        break;
    default:
        error("Subtraction not supported for this type"); // "Subtraction not supported for this type"
        break;
    }
}

// Multiplication: numbers, a vector or pair scaled, the cross product.
void multiply_values(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_a = operands.type_a;
    const QbValueKind type_b = operands.type_b;
    const Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    switch (type_a) {
    case QbValueKind::kInteger:
        switch (type_b) {
        case QbValueKind::kInteger:
            set_result_integer(static_cast<uint32_t>(operand_b->value.integer()) *
                               static_cast<uint32_t>(operand_a->value.integer()));
            break;
        case QbValueKind::kFloat:
            set_result_float(as_double(operand_a->value.integer()) * as_double(operand_b->value.real()));
            break;
        case QbValueKind::kVector: {
            Vector* vector = new_result_vector();
            vector->x() =
                get_rounded(as_double(operand_a->value.integer()) * as_double(operand_b->value.vector()->x()));
            result.value.vector()->y() =
                get_rounded(as_double(operand_a->value.integer()) * as_double(operand_b->value.vector()->y()));
            result.value.vector()->z() =
                get_rounded(as_double(operand_a->value.integer()) * as_double(operand_b->value.vector()->z()));
            break;
        }
        case QbValueKind::kPair: {
            Pair* pair = new_result_pair();
            pair->x() = get_rounded(as_double(operand_a->value.integer()) * as_double(operand_b->value.pair()->x()));
            result.value.pair()->y() =
                get_rounded(as_double(operand_a->value.integer()) * as_double(operand_b->value.pair()->y()));
            break;
        }
        default:
            error("Second arg cannot be multiplied by an integer"); // "Second arg cannot be multiplied by an integer"
            break;
        }
        break;
    case QbValueKind::kFloat:
        switch (type_b) {
        case QbValueKind::kInteger:
            set_result_float(as_double(operand_b->value.integer()) * as_double(operand_a->value.real()));
            break;
        case QbValueKind::kFloat:
            set_result_float(as_double(operand_b->value.real()) * as_double(operand_a->value.real()));
            break;
        case QbValueKind::kVector: {
            Vector* vector = new_result_vector();
            vector->x() = operand_b->value.vector()->x() * operand_a->value.real();
            result.value.vector()->y() = operand_b->value.vector()->y() * operand_a->value.real();
            result.value.vector()->z() = operand_b->value.vector()->z() * operand_a->value.real();
            break;
        }
        case QbValueKind::kPair: {
            Pair* pair = new_result_pair();
            pair->x() = operand_b->value.pair()->x() * operand_a->value.real();
            result.value.pair()->y() = operand_b->value.pair()->y() * operand_a->value.real();
            break;
        }
        default:
            error("Second arg cannot be multiplied by a float"); // "Second arg cannot be multiplied by a float"
            break;
        }
        break;
    case QbValueKind::kPair:
        if (type_b == QbValueKind::kInteger) {
            Pair* pair = new_result_pair();
            pair->x() = get_rounded(as_double(operand_b->value.integer()) * as_double(operand_a->value.pair()->x()));
            result.value.pair()->y() =
                get_rounded(as_double(operand_b->value.integer()) * as_double(operand_a->value.pair()->y()));
        } else if (type_b == QbValueKind::kFloat) {
            Pair* pair = new_result_pair();
            pair->x() = operand_a->value.pair()->x() * operand_b->value.real();
            result.value.pair()->y() = operand_a->value.pair()->y() * operand_b->value.real();
        } else {
            error("Pair cannot be multiplied by second arg"); // "Pair cannot be multiplied by second arg"
        }
        break;
    case QbValueKind::kVector:
        if (type_b == QbValueKind::kInteger) {
            Vector* vector = new_result_vector();
            vector->x() =
                get_rounded(as_double(operand_b->value.integer()) * as_double(operand_a->value.vector()->x()));
            result.value.vector()->y() =
                get_rounded(as_double(operand_b->value.integer()) * as_double(operand_a->value.vector()->y()));
            result.value.vector()->z() =
                get_rounded(as_double(operand_b->value.integer()) * as_double(operand_a->value.vector()->z()));
        } else if (type_b == QbValueKind::kFloat) {
            Vector* vector = new_result_vector();
            vector->x() = operand_a->value.vector()->x() * operand_b->value.real();
            result.value.vector()->y() = operand_a->value.vector()->y() * operand_b->value.real();
            result.value.vector()->z() = operand_a->value.vector()->z() * operand_b->value.real();
        } else if (type_b == QbValueKind::kVector) {
            // The cross product (each coordinate computed as the x87 does: in one go, then rounded).
            const Vector* u = operand_a->value.vector();
            const Vector* v = operand_b->value.vector();
            Vector* vector = new_result_vector();
            vector->x() = get_rounded((as_double(v->z()) * u->y()) - (as_double(v->y()) * u->z()));
            result.value.vector()->y() = get_rounded((as_double(v->x()) * u->z()) - (as_double(v->z()) * u->x()));
            result.value.vector()->z() = get_rounded((as_double(v->y()) * u->x()) - (as_double(v->x()) * u->y()));
        } else {
            error("Vector cannot be multiplied by second arg"); // "Vector cannot be multiplied by second arg"
        }
        break;
    default:
        error("Multiplication not supported for this type"); // "Multiplication not supported for this type"
        break;
    }
}

// Division (by zero refused): numbers, a vector or pair scaled down.
void divide_values(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_a = operands.type_a;
    const QbValueKind type_b = operands.type_b;
    const Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    if (type_b == QbValueKind::kInteger) {
        if (operand_b->value.integer() == 0) {
            error("Integer division by zero"); // "Integer division by zero"
            return;
        }
    } else if (type_b == QbValueKind::kFloat) {
        if (std::fabs(operand_b->value.real()) < kEvalDivisorMinimum) {
            error("Float division by zero"); // "Float division by zero"
            return;
        }
    }
    switch (type_a) {
    case QbValueKind::kInteger:
        if (type_b == QbValueKind::kInteger) {
            set_result_integer(static_cast<uint32_t>(operand_a->value.integer() / operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            set_result_float(as_double(operand_a->value.integer()) / as_double(operand_b->value.real()));
        } else {
            error("Integer cannot be divided by second arg"); // "Integer cannot be divided by second arg"
        }
        break;
    case QbValueKind::kFloat:
        if (type_b == QbValueKind::kInteger) {
            set_result_float(as_double(operand_a->value.real()) / as_double(operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            set_result_float(as_double(operand_a->value.real()) / as_double(operand_b->value.real()));
        } else {
            error("Float cannot be divided by second arg"); // "Float cannot be divided by second arg"
        }
        break;
    case QbValueKind::kPair:
        if (type_b == QbValueKind::kInteger) {
            Pair* pair = new_result_pair();
            pair->x() = get_rounded(as_double(operand_a->value.pair()->x()) / as_double(operand_b->value.integer()));
            result.value.pair()->y() =
                get_rounded(as_double(operand_a->value.pair()->y()) / as_double(operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            Pair* pair = new_result_pair();
            pair->x() = operand_a->value.pair()->x() / operand_b->value.real();
            result.value.pair()->y() = operand_a->value.pair()->y() / operand_b->value.real();
        } else {
            error("Pair cannot be divided by second arg"); // "Pair cannot be divided by second arg"
        }
        break;
    case QbValueKind::kVector:
        if (type_b == QbValueKind::kInteger) {
            Vector* vector = new_result_vector();
            vector->x() =
                get_rounded(as_double(operand_a->value.vector()->x()) / as_double(operand_b->value.integer()));
            result.value.vector()->y() =
                get_rounded(as_double(operand_a->value.vector()->y()) / as_double(operand_b->value.integer()));
            result.value.vector()->z() =
                get_rounded(as_double(operand_a->value.vector()->z()) / as_double(operand_b->value.integer()));
        } else if (type_b == QbValueKind::kFloat) {
            Vector* vector = new_result_vector();
            vector->x() = operand_a->value.vector()->x() / operand_b->value.real();
            result.value.vector()->y() = operand_a->value.vector()->y() / operand_b->value.real();
            result.value.vector()->z() = operand_a->value.vector()->z() / operand_b->value.real();
        } else {
            error("Vector cannot be divided by second arg"); // "Vector cannot be divided by second arg"
        }
        break;
    default:
        error("Division not supported for this type"); // "Division not supported for this type"
        break;
    }
}

// The dot product of vectors or pairs; a structure's member.
void dot_values(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_a = operands.type_a;
    const QbValueKind type_b = operands.type_b;
    Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    switch (type_a) {
    case QbValueKind::kVector:
        if (type_b == QbValueKind::kVector) {
            const Vector* u = operand_a->value.vector();
            const Vector* v = operand_b->value.vector();
            set_result_float((as_double(v->z()) * u->z()) + (as_double(v->y()) * u->y()) +
                             (as_double(v->x()) * u->x()));
        } else {
            error(
                "Vector can only be dot producted with another vector"); // "Vector can only be dot producted with another vector"
        }
        break;
    case QbValueKind::kPair:
        if (type_b == QbValueKind::kPair) {
            const Pair* u = operand_a->value.pair();
            const Pair* v = operand_b->value.pair();
            set_result_float((as_double(v->y()) * u->y()) + (as_double(v->x()) * u->x()));
        } else {
            error(
                "Pair can only be dot producted with another pair"); // "Pair can only be dot producted with another pair"
        }
        break;
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        // A structure's member: copied (a referenced structure's members are referred to).
        if (type_b == QbValueKind::kName) {
            if (const Component* member = operand_a->value.structure()->find_last_component(operand_b->value.word())) {
                duplicate_component(&result, member, static_cast<int>(type_a == QbValueKind::kStructRef));
            }
        } else {
            error(
                "Structure dot operator requires 2nd arg to be a name"); // "Structure dot operator requires 2nd arg to be a name"
        }
        break;
    default:
        error("Dot product not supported for this type"); // "Dot product not supported for this type"
        break;
    }
}

// An array's element ([]).
void index_array(ValueResolver& resolver, const Operands& operands)
{
    const Component* operand_a = operands.a;
    const Component* operand_b = operands.b;
    const QbValueKind type_a = operands.type_a;
    const QbValueKind type_b = operands.type_b;
    Component& result = get_result();
    auto error = [&resolver](const char* message) { resolver.change_fault(message); };
    if (!is_array(type_a)) {
        error(
            "Array element operator not supported for this type"); // "Array element operator not supported for this type"
    } else if (type_b != QbValueKind::kInteger) {
        error("[] index must be an integer"); // "[] index must be an integer"
    } else if (operand_b->value.word() >= operand_a->value.array()->extent()) {
        error("[] index out of range"); // "[] index out of range"
    } else {
        copy_array_element_to_component(operand_a->value.array(), operand_b->value.word(), &result,
                                        static_cast<int>(type_a == QbValueKind::kArrayRef));
        if ((result.type_and_flag_ & kComponentTypeBits) == 0) {
            error("Array type cannot be accessed using [] yet ..."); // "Array type cannot be accessed using [] yet ..."
        }
    }
}

} // namespace

// 0x0046D450
void ValueResolver::execute()
{
    if (value_top_ < 1) {
        get_clear();
        fault_ = "Not enough values in stack to execute operation"; // "Not enough values in stack to execute operation"
        return;
    }
    if (core::at(operators_, operator_top_).open_parentheses_ != 0) {
        get_clear();
        fault_ = "Non-zero parentheses count"; // "Non-zero parentheses count"
        return;
    }
    const QbToken op = core::at(operators_, operator_top_).op;
    if (op == kNoOperator) {
        get_clear();
        fault_ = "Tried to execute with no operator"; // "Tried to execute with no operator"
        return;
    }

    Component* operand_b = &core::at(values_, value_top_);
    Component* operand_a = &core::at(values_, value_top_ - 1);
    Component& result = get_result();
    result.type_and_flag_ &= 1;
    result.value.set_word(0);
    const QbValueKind type_a = value_kind(*operand_a);
    const QbValueKind type_b = value_kind(*operand_b);

    const Operands operands{.a = operand_a, .b = operand_b, .type_a = type_a, .type_b = type_b};
    switch (op) {
    case QbToken::kOr:
        set_result_type(QbValueKind::kInteger);
        result.value.set_word(static_cast<uint32_t>(fetch_flag(operand_b) | fetch_flag(operand_a)));
        break;
    case QbToken::kAnd:
        set_result_type(QbValueKind::kInteger);
        result.value.set_word(static_cast<uint32_t>(fetch_flag(operand_b) & fetch_flag(operand_a)));
        break;
    case QbToken::kEquals:
        set_result_type(QbValueKind::kInteger);
        result.value.set_word(components_equal(operand_a, operand_b) ? 1 : 0);
        break;
    case QbToken::kLessThan:
        set_result_type(QbValueKind::kInteger);
        result.value.set_word(less_than(operand_a, operand_b) ? 1 : 0);
        break;
    case QbToken::kGreaterThan:
        set_result_type(QbValueKind::kInteger);
        result.value.set_word(greater_than(operand_a, operand_b) ? 1 : 0);
        break;

    case QbToken::kPlus:
        add_values(*this, operands);
        break;

    case QbToken::kMinus:
        subtract_values(*this, operands);
        break;

    case QbToken::kMultiply:
        multiply_values(*this, operands);
        break;

    case QbToken::kDivide:
        divide_values(*this, operands);
        break;

    case QbToken::kDot:
        dot_values(*this, operands);
        break;

    case QbToken::kArrayStart:
        index_array(*this, operands);
        break;

    default:
        get_clear();
        fault_ = "Operator not supported"; // "Operator not supported"
        break;
    }

    // The result replaces the two values (also after an error, which has emptied the stacks).
    release_component(operand_a);
    release_component(operand_b);
    copy_value_kind(*operand_a, result);
    operand_a->value.set_word(result.value.word());
    result.type_and_flag_ &= 1;
    result.value.set_word(0);
    --value_top_;
    core::at(operators_, operator_top_).op = kNoOperator;
    if (operator_top_ == 0) {
        has_operators_ = false;
    } else {
        --operator_top_;
    }
}

} // namespace script

