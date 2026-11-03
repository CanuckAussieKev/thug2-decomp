#include "engine/script/expression.h"

#include "engine/core/mem.h"
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

namespace script {


namespace {
constexpr float kEvalDivisorMinimum{0.000001f};

// The operators' precedences, by operator.
constexpr std::array<int, 74> kOperatorPrecedence{{-1, -1, -1, -1, -1, 100, -1, 76, 100, -1, 98, 98, 99, 100, -1, -1, -1, -1, 80,
                              79, 78, 77, -1, -1, -1,  -1, -1, -1,  -1, -1, -1, -1, -1,  -1, -1, -1, -1, -1,
                              -1, -1, -1, -1, -1, -1,  -1, -1, -1,  -1, -1, -1, 58, 60,  59, 90, 89, -1, -1,
                              -1, -1, -1, -1, -1, -1,  -1, -1, -1,  -1, -1, -1, -1, -1,  -1, -1, -1}};
int precedence(int op) { return core::at(kOperatorPrecedence, op); }

// Where an operation's result is built (a global component: only its type and value are taken).
Component& get_result()
{
    static core::NoDestroy<Component> eval_result;
    return eval_result.get();
}

// The error messages are the game's own strings.

void set_result_type(QbValueKind type)
{
    get_result().type_and_flag_ = static_cast<uint8_t>((get_result().type_and_flag_ & 1) | (type << 1));
}

// Mixed integer / float arithmetic is done as the x87 does it: exactly, rounded to a float at the end.
double as_double(int32_t value) { return static_cast<double>(value); }
double as_double(float value) { return static_cast<double>(value); }
float get_rounded(double value) { return static_cast<float>(value); }

void set_result_float(double value)
{
    set_result_type(kQbvalFloat);
    get_result().value.set_real(get_rounded(value));
}
void set_result_integer(uint32_t value)
{
    set_result_type(kQbvalInteger);
    get_result().value.set_word(value);
}

Pair* new_result_pair()
{
    set_result_type(kQbvalPair);
    Pair* pair = new_pair();
    pair = pair ? new (pair) script::Pair : nullptr;
    get_result().value.set_pair(pair);
    return pair;
}
Vector* new_result_vector()
{
    set_result_type(kQbvalVector);
    Vector* vector = alloc_script_vector();
    vector = vector ? new (vector) script::Vector : nullptr;
    get_result().value.set_vector(vector);
    return vector;
}
Struct* new_result_structure()
{
    set_result_type(kQbvalStruct);
    gsl::owner<Struct*> structure = script::new_struct();
    get_result().value.set_structure(structure); // (the result's now)
    Struct* const made = structure;
    return made;
}

bool is_structure(uint32_t type) { return type == kQbvalStruct || type == kQbvalStructRef; }
bool is_array(uint32_t type) { return type == kQbvalArray || type == kEsymboltypeArraypointer; }

// Two strings joined (at most 200 characters with the terminator), or null if too long.
bool join_strings(const char* a, const char* b, QbValueKind type)
{
    const size_t length_a = std::strlen(a);
    const size_t length_b = std::strlen(b);
    if (length_a + length_b + 1 > 200)
        return false;
    std::array<char, 200> joined{};
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result): the terminator follows with the next copy
    std::memcpy(joined.data(), a, length_a);
    std::memcpy(std::span<char>(joined).subspan(length_a).data(), b, length_b + 1);
    set_result_type(type);
    get_result().value.set_text(spawn_text(joined.data()));
    return true;
}

} // namespace

// 0x0047A910
void copy_array_element_to_component(Array* array, uint32_t index, Component* to, int reference)
{
    to->type_and_flag_ = static_cast<uint8_t>((to->type_and_flag_ & 1) | (array->kind_tag() << 1));
    switch (array->kind_tag()) {
    case kQbvalInteger:
    case kQbvalName:
        to->value.set_word(array->fetch_part(index));
        break;
    case kQbvalFloat:
        to->value.set_real(array->fetch_float(index));
        break;
    case kQbvalString:
        to->value.set_text(spawn_text(array->find_inline_text(index)));
        break;
    case kQbvalPair: {
        auto* from = array->pair_at(index);
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        to->value.set_pair(pair);
        pair->x() = from->x();
        to->value.pair()->y() = from->y();
        break;
    }
    case kQbvalVector: {
        auto* from = array->vector_at(index);
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        to->value.set_vector(vector);
        vector->x() = from->x();
        to->value.vector()->y() = from->y();
        to->value.vector()->z() = from->z();
        break;
    }
    case kQbvalStruct:
        if (reference) {
            to->type_and_flag_ = static_cast<uint8_t>((to->type_and_flag_ & 1) | (kQbvalStructRef << 1));
            to->value.set_word(array->fetch_part(index));
        } else {
            gsl::owner<Struct*> structure = spawn_param_bag();
            to->value.set_structure(structure ? memory::construct_in<script::Struct>(structure) : nullptr);
            to->value.structure()->append_structure_if_any(array->struct_at(index));
        }
        break;
    case kQbvalArray:
        if (reference) {
            to->type_and_flag_ = static_cast<uint8_t>((to->type_and_flag_ & 1) | (kEsymboltypeArraypointer << 1));
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
void resolved_value(const Component* component, uint32_t* type, Cell* value)
{
    *type = component->kind_tag();
    *value = component->value;
    if (*type == kQbvalName) {
        if (const SymbolTableEntry* symbol = resolve_symbol(value->word())) {
            *value = symbol->value();
            *type = symbol->type();
        }
    }
}

double get_as_float(Cell value) { return value.real(); }
double get_as_integer(Cell value) { return value.integer(); }

// a < b: numbers by value; strings by length (!); structures: a is a subset of b.
bool get_less(uint32_t type_a, Cell a, uint32_t type_b, Cell b)
{
    switch (type_a) {
    case kQbvalInteger:
        if (type_b == kQbvalFloat)
            return get_as_integer(a) < get_as_float(b);
        return type_b == kQbvalInteger && a.integer() < b.integer();
    case kQbvalFloat:
        if (type_b == kQbvalInteger)
            return get_as_float(a) < get_as_integer(b);
        return type_b == kQbvalFloat && get_as_float(a) < get_as_float(b);
    case kQbvalString:
    case kQbvalInlineText:
        return type_b == type_a && std::strlen(a.text()) < std::strlen(b.text());
    case kQbvalStruct:
    case kQbvalStructRef:
        return (type_b == kQbvalStruct || type_b == kQbvalStructRef) && a.structure()->is_subset_of(b.structure());
    default:
        return false;
    }
}

} // namespace

// 0x00479410 (a member of the first component in the game)
bool less_than(Component* a, Component* b)
{
    uint32_t type_a = 0, type_b = 0;
    Cell value_a, value_b;
    resolved_value(a, &type_a, &value_a);
    resolved_value(b, &type_b, &value_b);
    return get_less(type_a, value_a, type_b, value_b);
}

// 0x004795A0
bool greater_than(Component* a, Component* b)
{
    uint32_t type_a = 0, type_b = 0;
    Cell value_a, value_b;
    resolved_value(a, &type_a, &value_a);
    resolved_value(b, &type_b, &value_b);
    // (the same cases, decided by a's type)
    switch (type_a) {
    case kQbvalInteger:
    case kQbvalFloat:
        if (type_b != kQbvalInteger && type_b != kQbvalFloat)
            return false;
        if (type_a == kQbvalInteger && type_b == kQbvalInteger)
            return value_a.integer() > value_b.integer();
        return (type_a == kQbvalInteger ? get_as_integer(value_a) : get_as_float(value_a)) >
               (type_b == kQbvalInteger ? get_as_integer(value_b) : get_as_float(value_b));
    case kQbvalString:
    case kQbvalInlineText:
        return type_b == type_a && std::strlen(value_a.text()) > std::strlen(value_b.text());
    case kQbvalStruct:
    case kQbvalStructRef:
        // b is a subset of a
        return (type_b == kQbvalStruct || type_b == kQbvalStructRef) &&
               value_b.structure()->is_subset_of(value_a.structure());
    default:
        return false;
    }
}

// 0x0046D160
bool is_reference(const Component* component)
{
    return component->kind_tag() == kQbvalStructRef || component->kind_tag() == kEsymboltypeArraypointer;
}

// 0x0046D180
void make_reference(Component* component)
{
    if (component->kind_tag() == kQbvalStruct)
        component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (kQbvalStructRef << 1));
    else if (component->kind_tag() == kQbvalArray)
        component->type_and_flag_ =
            static_cast<uint8_t>((component->type_and_flag_ & 1) | (kEsymboltypeArraypointer << 1));
}

// 0x0046D1A0
void make_owning(Component* component)
{
    if (component->kind_tag() == kQbvalStructRef)
        component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (kQbvalStruct << 1));
    else if (component->kind_tag() == kEsymboltypeArraypointer)
        component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (kQbvalArray << 1));
}

// 0x0046D280
int fetch_flag(const Component* component)
{
    if (component->kind_tag() == kQbvalInteger)
        return component->value.integer();
    if (component->kind_tag() == kQbvalFloat && component->value.real() != 0.0f)
        return 1;
    return 0;
}

// 0x0046D380
ValueResolver* ValueResolver::prepare_all()
{
    for (Component& value : values_)
        construct_component(&value);
    get_clear();
    unknown1f4_ = true;
    return this;
}

// 0x0046D1C0
void ValueResolver::get_clear()
{
    for (Component& value : values_)
        release_component(&value);
    value_top_ = 0;
    operator_top_ = 0;
    for (Operator& op : operators_) {
        op.op = kOperatorNone;
        op.open_parentheses_ = 0;
    }
    has_operators_ = false;
    fault_ = nullptr;
}

// 0x0046D220
void ValueResolver::reset_if_dirty()
{
    if (value_top_ || (core::at(values_, 0).type_and_flag_ & 0xFE) || operator_top_ || fault_)
        get_clear();
}

// 0x0046D260
void ValueResolver::change_fault(const char* error)
{
    get_clear();
    fault_ = error;
}

// 0x0046D2B0
void ValueResolver::push_operator(int op)
{
    if (++operator_top_ >= 20) {
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
    if (value_top_ || (core::at(values_, 0).type_and_flag_ & 0xFE)) {
        if (++value_top_ >= 20) {
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
void ValueResolver::intake(int op)
{
    if (has_operators_) {
        while (core::at(operators_, operator_top_).open_parentheses_ == 0) {
            const int pending = core::at(operators_, operator_top_).op;
            if (op != pending && precedence(op) > precedence(pending))
                break;
            execute();
            if (!has_operators_)
                break;
        }
    }
    push_operator(op);
}

// 0x0046EBF0
void ValueResolver::close_bracket()
{
    while (core::at(operators_, operator_top_).open_parentheses_ == 0) {
        execute();
        if (fault_)
            return;
    }
    --core::at(operators_, operator_top_).open_parentheses_;
}

// 0x0046EC40
bool ValueResolver::fetch_verdict(Component* result)
{
    while (core::at(operators_, operator_top_).open_parentheses_ == 0) {
        if (value_top_ == 0) {
            if (core::at(operators_, 0).op != kOperatorNone || operator_top_ != 0) {
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
        if (fault_)
            return false;
    }
    get_clear();
    fault_ = "Not enough close parentheses"; // "Not enough close parentheses"
    return false;
}

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
    const int op = core::at(operators_, operator_top_).op;
    if (op == kOperatorNone) {
        get_clear();
        fault_ = "Tried to execute with no operator"; // "Tried to execute with no operator"
        return;
    }

    Component* b = &core::at(values_, value_top_);
    Component* a = &core::at(values_, value_top_ - 1);
    Component& result = get_result();
    result.type_and_flag_ &= 1;
    result.value.set_word(0);
    const uint32_t type_a = a->kind_tag();
    const uint32_t type_b = b->kind_tag();
    auto error = [&](const char* message) { change_fault(message); };

    switch (op) {
    case kOperatorOr:
        set_result_type(kQbvalInteger);
        result.value.set_word(static_cast<uint32_t>(fetch_flag(b) | fetch_flag(a)));
        break;
    case kOperatorAnd:
        set_result_type(kQbvalInteger);
        result.value.set_word(static_cast<uint32_t>(fetch_flag(b) & fetch_flag(a)));
        break;
    case kOperatorEquals:
        set_result_type(kQbvalInteger);
        result.value.set_word(components_equal(a, b) ? 1 : 0);
        break;
    case kOperatorLessThan:
        set_result_type(kQbvalInteger);
        result.value.set_word(less_than(a, b) ? 1 : 0);
        break;
    case kOperatorGreaterThan:
        set_result_type(kQbvalInteger);
        result.value.set_word(greater_than(a, b) ? 1 : 0);
        break;

    case kOperatorAdd:
        switch (type_a) {
        case kQbvalInteger:
            if (type_b == kQbvalInteger)
                set_result_integer(static_cast<uint32_t>(a->value.integer()) + static_cast<uint32_t>(b->value.integer()));
            else if (type_b == kQbvalFloat)
                set_result_float(as_double(a->value.integer()) + as_double(b->value.real()));
            else
                error("Second arg cannot be added to an integer"); // "Second arg cannot be added to an integer"
            break;
        case kQbvalFloat:
            if (type_b == kQbvalInteger)
                set_result_float(as_double(b->value.integer()) + as_double(a->value.real()));
            else if (type_b == kQbvalFloat)
                set_result_float(as_double(a->value.real()) + as_double(b->value.real()));
            else
                error("Second arg cannot be added to a float"); // "Second arg cannot be added to a float"
            break;
        case kQbvalName:
            // A checksum plus an integer or checksum is a checksum.
            if (type_b == kQbvalInteger || type_b == kQbvalName) {
                set_result_type(kQbvalName);
                result.value.set_word(b->value.word() + a->value.word());
            } else {
                error("Second arg cannot be added to a checksum"); // "Second arg cannot be added to a checksum"
            }
            break;
        case kQbvalVector:
            if (type_b == kQbvalVector) {
                Vector* vector = new_result_vector();
                vector->x() = a->value.vector()->x() + b->value.vector()->x();
                result.value.vector()->y() = a->value.vector()->y() + b->value.vector()->y();
                result.value.vector()->z() = a->value.vector()->z() + b->value.vector()->z();
            } else {
                error("Second arg cannot be added to a vector"); // "Second arg cannot be added to a vector"
            }
            break;
        case kQbvalPair:
            if (type_b == kQbvalPair) {
                Pair* pair = new_result_pair();
                pair->x() = a->value.pair()->x() + b->value.pair()->x();
                result.value.pair()->y() = a->value.pair()->y() + b->value.pair()->y();
            } else {
                error("Second arg cannot be added to a pair"); // "Second arg cannot be added to a pair"
            }
            break;
        case kQbvalString:
            if (type_b != kQbvalString)
                error("Second arg cannot be added to a string"); // "Second arg cannot be added to a string"
            else if (!join_strings(a->value.text(), b->value.text(), kQbvalString))
                error("Result of string addition too long"); // "Result of string addition too long"
            break;
        case kQbvalInlineText:
            if (type_b != kQbvalInlineText)
                error("Second arg cannot be added to a local-string"); // "Second arg cannot be added to a local-string"
            else if (!join_strings(a->value.text(), b->value.text(), kQbvalInlineText))
                error("Result of local-string addition too long"); // "Result of local-string addition too long"
            break;
        case kQbvalStruct:
        case kQbvalStructRef:
            if (is_structure(type_b)) {
                Struct* structure = new_result_structure();
                structure->append_structure_if_any(a->value.structure());
                result.value.structure()->append_structure_if_any(b->value.structure());
            } else {
                error(
                    "Structure add operator requires 2nd arg to be a structure"); // "Structure add operator requires 2nd arg to be a structure"
            }
            break;
        case kQbvalArray:
        case kEsymboltypeArraypointer: {
            if (!is_array(type_b)) {
                error(
                    "Array add operator requires 2nd arg to be an array as well"); // "Array add operator requires 2nd arg to be an array as well"
                break;
            }
            gsl::owner<Array*> array = script::new_array();
            set_result_type(kQbvalArray);
            result.value.set_array(array);
            const uint32_t size_a = a->value.array()->extent();
            const uint32_t size_b = b->value.array()->extent();
            if (size_a == 0) {
                duplicate_array(array, b->value.array());
            } else if (size_b == 0) {
                duplicate_array(array, a->value.array());
            } else {
                // (the elements are all taken to be of the first array's type)
                const QbValueKind type = a->value.array()->kind_tag();
                array->configure_size_kind(size_a + size_b, type);
                copy_array_elements(array->cells(), a->value.array()->cells(), type);
                copy_array_elements(array->cells().subspan(size_a), b->value.array()->cells(), type);
            }
            break;
        }
        default:
            error("Addition not supported for this type"); // "Addition not supported for this type"
            break;
        }
        break;

    case kOperatorMinus:
        switch (type_a) {
        case kQbvalInteger:
            if (type_b == kQbvalInteger)
                set_result_integer(static_cast<uint32_t>(a->value.integer()) - static_cast<uint32_t>(b->value.integer()));
            else if (type_b == kQbvalFloat)
                set_result_float(as_double(a->value.integer()) - as_double(b->value.real()));
            else
                error(
                    "Second arg cannot be subtracted from an integer"); // "Second arg cannot be subtracted from an integer"
            break;
        case kQbvalFloat:
            if (type_b == kQbvalInteger)
                set_result_float(as_double(a->value.real()) - as_double(b->value.integer()));
            else if (type_b == kQbvalFloat)
                set_result_float(as_double(a->value.real()) - as_double(b->value.real()));
            else
                error("Second arg cannot be subtracted from a float"); // "Second arg cannot be subtracted from a float"
            break;
        case kQbvalPair:
            if (type_b == kQbvalPair) {
                Pair* pair = new_result_pair();
                pair->x() = a->value.pair()->x() - b->value.pair()->x();
                result.value.pair()->y() = a->value.pair()->y() - b->value.pair()->y();
            } else {
                error("Second arg cannot be subtracted from a pair"); // "Second arg cannot be subtracted from a pair"
            }
            break;
        case kQbvalVector:
            if (type_b == kQbvalVector) {
                Vector* vector = new_result_vector();
                vector->x() = a->value.vector()->x() - b->value.vector()->x();
                result.value.vector()->y() = a->value.vector()->y() - b->value.vector()->y();
                result.value.vector()->z() = a->value.vector()->z() - b->value.vector()->z();
            } else {
                error(
                    "Second arg cannot be subtracted from a vector"); // "Second arg cannot be subtracted from a vector"
            }
            break;
        case kQbvalStruct:
        case kQbvalStructRef:
            if (type_b == kQbvalName) {
                // Without the name (as a parameter or a flag).
                Struct* structure = new_result_structure();
                structure->append_structure_if_any(a->value.structure());
                result.value.structure()->erase_module(b->value.word());
                result.value.structure()->remove_flag(b->value.word());
            } else if (is_array(type_b)) {
                // Without each name in the array.
                Array* names = b->value.array();
                if (names->kind_tag() != kQbvalName && names->kind_tag() != kQbvalNone) {
                    // "Subtracting an array from a structure requires the array to be an array of names"
                    error("Subtracting an array from a structure requires the array to be an array of names");
                    break;
                }
                Struct* structure = new_result_structure();
                structure->append_structure_if_any(a->value.structure());
                for (uint32_t i = 0; i < names->extent(); ++i) {
                    result.value.structure()->erase_module(names->fetch_part(i));
                    result.value.structure()->remove_flag(names->fetch_part(i));
                }
            } else if (is_structure(type_b)) {
                // Without each of the other structure's parameters (by name and type) and flags.
                Struct* structure = new_result_structure();
                structure->append_structure_if_any(a->value.structure());
                Struct* other = b->value.structure();
                for (Component* each = other->step_to_entry(nullptr); each; each = other->step_to_entry(each)) {
                    if (each->name) {
                        result.value.structure()->remove_component_of_type(each->name, each->kind_tag());
                    } else if (each->kind_tag() == kQbvalName) {
                        result.value.structure()->erase_module(each->value.word());
                        result.value.structure()->remove_flag(each->value.word());
                    }
                }
            } else {
                // "Structure minus operator requires 2nd arg to be a name, an array of names, or a structure"
                error("Structure minus operator requires 2nd arg to be a name, an array of names, or a structure");
            }
            break;
        default:
            error("Subtraction not supported for this type"); // "Subtraction not supported for this type"
            break;
        }
        break;

    case kOperatorMultiply:
        switch (type_a) {
        case kQbvalInteger:
            switch (type_b) {
            case kQbvalInteger:
                set_result_integer(static_cast<uint32_t>(b->value.integer()) * static_cast<uint32_t>(a->value.integer()));
                break;
            case kQbvalFloat:
                set_result_float(as_double(a->value.integer()) * as_double(b->value.real()));
                break;
            case kQbvalVector: {
                Vector* vector = new_result_vector();
                vector->x() = get_rounded(as_double(a->value.integer()) * as_double(b->value.vector()->x()));
                result.value.vector()->y() = get_rounded(as_double(a->value.integer()) * as_double(b->value.vector()->y()));
                result.value.vector()->z() = get_rounded(as_double(a->value.integer()) * as_double(b->value.vector()->z()));
                break;
            }
            case kQbvalPair: {
                Pair* pair = new_result_pair();
                pair->x() = get_rounded(as_double(a->value.integer()) * as_double(b->value.pair()->x()));
                result.value.pair()->y() = get_rounded(as_double(a->value.integer()) * as_double(b->value.pair()->y()));
                break;
            }
            default:
                error(
                    "Second arg cannot be multiplied by an integer"); // "Second arg cannot be multiplied by an integer"
                break;
            }
            break;
        case kQbvalFloat:
            switch (type_b) {
            case kQbvalInteger:
                set_result_float(as_double(b->value.integer()) * as_double(a->value.real()));
                break;
            case kQbvalFloat:
                set_result_float(as_double(b->value.real()) * as_double(a->value.real()));
                break;
            case kQbvalVector: {
                Vector* vector = new_result_vector();
                vector->x() = b->value.vector()->x() * a->value.real();
                result.value.vector()->y() = b->value.vector()->y() * a->value.real();
                result.value.vector()->z() = b->value.vector()->z() * a->value.real();
                break;
            }
            case kQbvalPair: {
                Pair* pair = new_result_pair();
                pair->x() = b->value.pair()->x() * a->value.real();
                result.value.pair()->y() = b->value.pair()->y() * a->value.real();
                break;
            }
            default:
                error("Second arg cannot be multiplied by a float"); // "Second arg cannot be multiplied by a float"
                break;
            }
            break;
        case kQbvalPair:
            if (type_b == kQbvalInteger) {
                Pair* pair = new_result_pair();
                pair->x() = get_rounded(as_double(b->value.integer()) * as_double(a->value.pair()->x()));
                result.value.pair()->y() = get_rounded(as_double(b->value.integer()) * as_double(a->value.pair()->y()));
            } else if (type_b == kQbvalFloat) {
                Pair* pair = new_result_pair();
                pair->x() = a->value.pair()->x() * b->value.real();
                result.value.pair()->y() = a->value.pair()->y() * b->value.real();
            } else {
                error("Pair cannot be multiplied by second arg"); // "Pair cannot be multiplied by second arg"
            }
            break;
        case kQbvalVector:
            if (type_b == kQbvalInteger) {
                Vector* vector = new_result_vector();
                vector->x() = get_rounded(as_double(b->value.integer()) * as_double(a->value.vector()->x()));
                result.value.vector()->y() = get_rounded(as_double(b->value.integer()) * as_double(a->value.vector()->y()));
                result.value.vector()->z() = get_rounded(as_double(b->value.integer()) * as_double(a->value.vector()->z()));
            } else if (type_b == kQbvalFloat) {
                Vector* vector = new_result_vector();
                vector->x() = a->value.vector()->x() * b->value.real();
                result.value.vector()->y() = a->value.vector()->y() * b->value.real();
                result.value.vector()->z() = a->value.vector()->z() * b->value.real();
            } else if (type_b == kQbvalVector) {
                // The cross product (each coordinate computed as the x87 does: in one go, then rounded).
                const Vector* u = a->value.vector();
                const Vector* v = b->value.vector();
                Vector* vector = new_result_vector();
                vector->x() = get_rounded(as_double(v->z()) * u->y() - as_double(v->y()) * u->z());
                result.value.vector()->y() = get_rounded(as_double(v->x()) * u->z() - as_double(v->z()) * u->x());
                result.value.vector()->z() = get_rounded(as_double(v->y()) * u->x() - as_double(v->x()) * u->y());
            } else {
                error("Vector cannot be multiplied by second arg"); // "Vector cannot be multiplied by second arg"
            }
            break;
        default:
            error("Multiplication not supported for this type"); // "Multiplication not supported for this type"
            break;
        }
        break;

    case kOperatorDivide:
        if (type_b == kQbvalInteger) {
            if (b->value.integer() == 0) {
                error("Integer division by zero"); // "Integer division by zero"
                break;
            }
        } else if (type_b == kQbvalFloat) {
            if (std::fabs(b->value.real()) < kEvalDivisorMinimum) {
                error("Float division by zero"); // "Float division by zero"
                break;
            }
        }
        switch (type_a) {
        case kQbvalInteger:
            if (type_b == kQbvalInteger)
                set_result_integer(static_cast<uint32_t>(a->value.integer() / b->value.integer()));
            else if (type_b == kQbvalFloat)
                set_result_float(as_double(a->value.integer()) / as_double(b->value.real()));
            else
                error("Integer cannot be divided by second arg"); // "Integer cannot be divided by second arg"
            break;
        case kQbvalFloat:
            if (type_b == kQbvalInteger)
                set_result_float(as_double(a->value.real()) / as_double(b->value.integer()));
            else if (type_b == kQbvalFloat)
                set_result_float(as_double(a->value.real()) / as_double(b->value.real()));
            else
                error("Float cannot be divided by second arg"); // "Float cannot be divided by second arg"
            break;
        case kQbvalPair:
            if (type_b == kQbvalInteger) {
                Pair* pair = new_result_pair();
                pair->x() = get_rounded(as_double(a->value.pair()->x()) / as_double(b->value.integer()));
                result.value.pair()->y() = get_rounded(as_double(a->value.pair()->y()) / as_double(b->value.integer()));
            } else if (type_b == kQbvalFloat) {
                Pair* pair = new_result_pair();
                pair->x() = a->value.pair()->x() / b->value.real();
                result.value.pair()->y() = a->value.pair()->y() / b->value.real();
            } else {
                error("Pair cannot be divided by second arg"); // "Pair cannot be divided by second arg"
            }
            break;
        case kQbvalVector:
            if (type_b == kQbvalInteger) {
                Vector* vector = new_result_vector();
                vector->x() = get_rounded(as_double(a->value.vector()->x()) / as_double(b->value.integer()));
                result.value.vector()->y() = get_rounded(as_double(a->value.vector()->y()) / as_double(b->value.integer()));
                result.value.vector()->z() = get_rounded(as_double(a->value.vector()->z()) / as_double(b->value.integer()));
            } else if (type_b == kQbvalFloat) {
                Vector* vector = new_result_vector();
                vector->x() = a->value.vector()->x() / b->value.real();
                result.value.vector()->y() = a->value.vector()->y() / b->value.real();
                result.value.vector()->z() = a->value.vector()->z() / b->value.real();
            } else {
                error("Vector cannot be divided by second arg"); // "Vector cannot be divided by second arg"
            }
            break;
        default:
            error("Division not supported for this type"); // "Division not supported for this type"
            break;
        }
        break;

    case kOperatorDot:
        switch (type_a) {
        case kQbvalVector:
            if (type_b == kQbvalVector) {
                const Vector* u = a->value.vector();
                const Vector* v = b->value.vector();
                set_result_float(as_double(v->z()) * u->z() + as_double(v->y()) * u->y() + as_double(v->x()) * u->x());
            } else {
                error(
                    "Vector can only be dot producted with another vector"); // "Vector can only be dot producted with another vector"
            }
            break;
        case kQbvalPair:
            if (type_b == kQbvalPair) {
                const Pair* u = a->value.pair();
                const Pair* v = b->value.pair();
                set_result_float(as_double(v->y()) * u->y() + as_double(v->x()) * u->x());
            } else {
                error(
                    "Pair can only be dot producted with another pair"); // "Pair can only be dot producted with another pair"
            }
            break;
        case kQbvalStruct:
        case kQbvalStructRef:
            // A structure's member: copied (a referenced structure's members are referred to).
            if (type_b == kQbvalName) {
                if (const Component* member = a->value.structure()->find_last_component(b->value.word()))
                    duplicate_component(&result, member, type_a == kQbvalStructRef);
            } else {
                error(
                    "Structure dot operator requires 2nd arg to be a name"); // "Structure dot operator requires 2nd arg to be a name"
            }
            break;
        default:
            error("Dot product not supported for this type"); // "Dot product not supported for this type"
            break;
        }
        break;

    case kOperatorArrayElement:
        if (!is_array(type_a)) {
            error(
                "Array element operator not supported for this type"); // "Array element operator not supported for this type"
        } else if (type_b != kQbvalInteger) {
            error("[] index must be an integer"); // "[] index must be an integer"
        } else if (b->value.word() >= a->value.array()->extent()) {
            error("[] index out of range"); // "[] index out of range"
        } else {
            copy_array_element_to_component(a->value.array(), b->value.word(), &result, type_a == kEsymboltypeArraypointer);
            if ((result.type_and_flag_ & 0xFE) == 0)
                error(
                    "Array type cannot be accessed using [] yet ..."); // "Array type cannot be accessed using [] yet ..."
        }
        break;

    default:
        get_clear();
        fault_ = "Operator not supported"; // "Operator not supported"
        break;
    }

    // The result replaces the two values (also after an error, which has emptied the stacks).
    release_component(a);
    release_component(b);
    a->type_and_flag_ = static_cast<uint8_t>((a->type_and_flag_ & 1) | (result.type_and_flag_ & 0xFE));
    a->value.set_word(result.value.word());
    result.type_and_flag_ &= 1;
    result.value.set_word(0);
    --value_top_;
    core::at(operators_, operator_top_).op = kOperatorNone;
    if (operator_top_ == 0)
        has_operators_ = false;
    else
        --operator_top_;
}

} // namespace script

