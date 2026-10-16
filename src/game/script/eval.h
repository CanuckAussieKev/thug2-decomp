#pragma once

#include <cstdint>

#include "game/script/struct.h"

// The script expression evaluator: a stack machine for the expressions in scripts' parentheses. Values and
// operators are fed in as the expression is read; operators of higher precedence run first.
namespace Script {

// Whether a component refers to a structure / array it doesn't own (types 20 / 21).
bool IsReference(const CComponent* component);
// Turn a structure / array component into a reference to it, and back.
void MakeReference(CComponent* component);
void MakeOwning(CComponent* component);
// A component's truth value: an integer's value itself, 1 for a non-zero float, else 0.
int GetBool(const CComponent* component);

// The operators (the token values the parser gives them).
enum EOperator : int {
    OPERATOR_NONE = 0,
    OPERATOR_ARRAY_ELEMENT = 5, // a[i]
    OPERATOR_EQUALS = 7,
    OPERATOR_DOT = 8,           // dot product, or a structure's member
    OPERATOR_MINUS = 10,
    OPERATOR_ADD = 11,
    OPERATOR_DIVIDE = 12,
    OPERATOR_MULTIPLY = 13,     // also the cross product of vectors
    OPERATOR_LESS_THAN = 18,
    OPERATOR_GREATER_THAN = 20,
    OPERATOR_OR = 50,
    OPERATOR_AND = 51,
};

class CExpressionEvaluator {
public:
    CExpressionEvaluator* Init(); // the constructor

    // Free the values and empty both stacks (and the error).
    void Clear();
    void ClearIfNeeded();
    void SetTokenPointer(const uint8_t* token) { m_token = token; }
    void SetError(const char* error);

    void Input(const CComponent* value);
    // An operator: pending operators of higher or equal precedence (in the same parentheses) run first.
    void Input(int op);
    void OpenParenthesis();
    // Runs the operators inside the parentheses.
    void CloseParenthesis();
    // Run what's left, and copy the result to `result`. False (with an error) if the expression is incomplete.
    bool GetResult(CComponent* result);

    // Run the top operator on the top two values, leaving the result in their place.
    void Execute();
    void PushOperator(int op);

    struct Operator {
        int op;
        int openParentheses;
    };

    CComponent m_values[20];
    int m_valueTop;
    Operator m_operators[20];
    int m_operatorTop;
    bool m_hasOperators;
    const uint8_t* m_token;
    const char* m_error;
    bool m_unknown1F4;
};
static_assert(sizeof(CComponent) * 20 == 0x140, "layout must match the original");

} // namespace Script
