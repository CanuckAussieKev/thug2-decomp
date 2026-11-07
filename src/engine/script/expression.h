#pragma once

#include <cstdint>
#include <array>

#include "base/core/layout_32bit.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"

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

// The operators are the parser's token bytes: = . - + / * < > || && and a[i] (the array start token), with the
// end-of-file byte for "no operator".
constexpr QbToken kNoOperator = QbToken::kEndOfFile;

constexpr size_t kValueResolverValueTopOffset = 0x140;
constexpr size_t kValueResolverOperatorsOffset = 0x144;
constexpr size_t kValueResolverOperatorTopOffset = 0x1E4;
constexpr size_t kValueResolverHasOperatorsOffset = 0x1E8;
constexpr size_t kValueResolverTokenOffset = 0x1EC;
constexpr size_t kValueResolverFaultOffset = 0x1F0;
constexpr size_t kValueResolverUnknown1f4Offset = 0x1F4;
constexpr int kValueStackDepth = 20;
constexpr int kOperatorStackDepth = 20;
constexpr size_t kValueStackBytes = 0x140;
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
    void intake(QbToken op);
    void open_bracket();
    // Runs the operators inside the parentheses.
    void close_bracket();
    // Run what's left, and copy the result to `result`. False (with an error) if the expression is incomplete.
    bool fetch_verdict(Component* result);

    // Run the top operator on the top two values, leaving the result in their place.
    void execute();
    void push_operator(QbToken op);

    struct Operator {
        QbToken op; // (the original's word: the rest is zero)
        std::array<uint8_t, 3> unknown1_;
        int open_parentheses_;
    };

    // The fields other code reads and sets.
    void set_unknown1f4(bool value) { unknown1f4_ = value; }

private:
    std::array<Component, kValueStackDepth> values_;
    int value_top_{};
    std::array<Operator, kOperatorStackDepth> operators_{};
    int operator_top_{};
    bool has_operators_{};
    const uint8_t* token_{};
    const char* fault_{};
    bool unknown1f4_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(ValueResolver, value_top_) == kValueResolverValueTopOffset), "layout must match the original");
        static_assert(core::layout_32bit(offsetof(ValueResolver, operators_) == kValueResolverOperatorsOffset), "layout must match the original");
        static_assert(core::layout_32bit(offsetof(ValueResolver, operator_top_) == kValueResolverOperatorTopOffset), "layout must match the original");
        static_assert(core::layout_32bit(offsetof(ValueResolver, has_operators_) == kValueResolverHasOperatorsOffset), "layout must match the original");
        static_assert(core::layout_32bit(offsetof(ValueResolver, token_) == kValueResolverTokenOffset), "layout must match the original");
        static_assert(core::layout_32bit(offsetof(ValueResolver, fault_) == kValueResolverFaultOffset), "layout must match the original");
        static_assert(core::layout_32bit(offsetof(ValueResolver, unknown1f4_) == kValueResolverUnknown1f4Offset), "layout must match the original");
    }
};
static_assert(core::layout_32bit(sizeof(Component) * kValueStackDepth == kValueStackBytes), "layout must match the original");

} // namespace script
