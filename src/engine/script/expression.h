#pragma once

#include <cstdint>
#include <array>

#include "engine/script/param_struct.h"

// The script expression evaluator: a stack machine for the expressions in scripts' parentheses. Values and
// operators are fed in as the expression is read; operators of higher precedence run first.
namespace script {

// Whether a component refers to a structure / array it doesn't own (types 20 / 21).
bool is_reference(const Component* component);
// Turn a structure / array component into a reference to it, and back.
void make_reference(Component* component);
void make_owning(Component* component);
// A component's truth value: an integer's value itself, 1 for a non-zero float, else 0.
int fetch_flag(const Component* component);
// a < b and a > b: numbers by value, strings by their lengths, structures by one being a subset of the other
// (names standing for global symbols are looked up).
bool less_than(Component* a, Component* b);
bool greater_than(Component* a, Component* b);
// A copy of array element `index` in `to` (with `reference`, structures and arrays are referred to). Local
// strings and other types leave `to` empty.
void copy_array_element_to_component(Array* array, uint32_t index, Component* to, int reference);

// The operators (the token values the parser gives them).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
enum Operator : uint8_t {
    kOperatorNone = 0,
    kOperatorArrayElement = 5, // a[i]
    kOperatorEquals = 7,
    kOperatorDot = 8, // dot product, or a structure's member
    kOperatorMinus = 10,
    kOperatorAdd = 11,
    kOperatorDivide = 12,
    kOperatorMultiply = 13, // also the cross product of vectors
    kOperatorLessThan = 18,
    kOperatorGreaterThan = 20,
    kOperatorOr = 50,
    kOperatorAnd = 51,
};

constexpr size_t kValueResolverValueTopOffset = 0x140;
constexpr size_t kValueResolverOperatorsOffset = 0x144;
constexpr size_t kValueResolverOperatorTopOffset = 0x1E4;
constexpr size_t kValueResolverHasOperatorsOffset = 0x1E8;
constexpr size_t kValueResolverTokenOffset = 0x1EC;
constexpr size_t kValueResolverFaultOffset = 0x1F0;
constexpr size_t kValueResolverUnknown1f4Offset = 0x1F4;
class ValueResolver {
public:
    ValueResolver* prepare_all(); // the constructor

    // Free the values and empty both stacks (and the error).
    void get_clear();
    void reset_if_dirty();
    void set_read_cursor(const uint8_t* token) { token_ = token; }
    void change_fault(const char* error);

    void intake(const Component* value);
    // An operator: pending operators of higher or equal precedence (in the same parentheses) run first.
    void intake(int op);
    void open_bracket();
    // Runs the operators inside the parentheses.
    void close_bracket();
    // Run what's left, and copy the result to `result`. False (with an error) if the expression is incomplete.
    bool fetch_verdict(Component* result);

    // Run the top operator on the top two values, leaving the result in their place.
    void execute();
    void push_operator(int op);

    struct Operator {
        int op;
        int open_parentheses_;
    };

    // The fields other code reads and sets.
    void set_unknown1f4(bool value) { unknown1f4_ = value; }

private:
    std::array<Component, 20> values_;
    int value_top_{};
    std::array<Operator, 20> operators_{};
    int operator_top_{};
    bool has_operators_{};
    const uint8_t* token_{};
    const char* fault_{};
    bool unknown1f4_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ValueResolver, value_top_) == kValueResolverValueTopOffset, "layout must match the original");
        static_assert(offsetof(ValueResolver, operators_) == kValueResolverOperatorsOffset, "layout must match the original");
        static_assert(offsetof(ValueResolver, operator_top_) == kValueResolverOperatorTopOffset, "layout must match the original");
        static_assert(offsetof(ValueResolver, has_operators_) == kValueResolverHasOperatorsOffset, "layout must match the original");
        static_assert(offsetof(ValueResolver, token_) == kValueResolverTokenOffset, "layout must match the original");
        static_assert(offsetof(ValueResolver, fault_) == kValueResolverFaultOffset, "layout must match the original");
        static_assert(offsetof(ValueResolver, unknown1f4_) == kValueResolverUnknown1f4Offset, "layout must match the original");
    }
};
static_assert(sizeof(Component) * 20 == 0x140, "layout must match the original");

} // namespace script
