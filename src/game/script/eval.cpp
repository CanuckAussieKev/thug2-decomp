#include "game/script/eval.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

static_assert(offsetof(CExpressionEvaluator, m_valueTop) == 0x140, "layout must match the original");
static_assert(offsetof(CExpressionEvaluator, m_operators) == 0x144, "layout must match the original");
static_assert(offsetof(CExpressionEvaluator, m_operatorTop) == 0x1E4, "layout must match the original");
static_assert(offsetof(CExpressionEvaluator, m_hasOperators) == 0x1E8, "layout must match the original");
static_assert(offsetof(CExpressionEvaluator, m_token) == 0x1EC, "layout must match the original");
static_assert(offsetof(CExpressionEvaluator, m_error) == 0x1F0, "layout must match the original");
static_assert(offsetof(CExpressionEvaluator, m_unknown1F4) == 0x1F4, "layout must match the original");

namespace {

// The operators' precedences, by operator.
int Precedence(int op) { return reinterpret_cast<const int*>(0x0067D908)[op]; }

// Where an operation's result is built (a global component: only its type and value are taken).
CComponent& Result() { return GameVariable<CComponent>(0x006F2348); }

// The error messages are the game's own strings.
const char* Message(uint32_t address) { return reinterpret_cast<const char*>(address); }

void SetResultType(ESymbolType type)
{
    Result().typeAndFlag = static_cast<uint8_t>((Result().typeAndFlag & 1) | (type << 1));
}

// Mixed integer / float arithmetic is done as the x87 does it: exactly, rounded to a float at the end.
double AsDouble(int32_t value) { return static_cast<double>(value); }
double AsDouble(float value) { return static_cast<double>(value); }
float Rounded(double value) { return static_cast<float>(value); }

void SetResultFloat(double value)
{
    SetResultType(ESYMBOLTYPE_FLOAT);
    Result().floatValue = Rounded(value);
}
void SetResultInteger(uint32_t value)
{
    SetResultType(ESYMBOLTYPE_INTEGER);
    Result().raw = value;
}

CPair* NewResultPair()
{
    SetResultType(ESYMBOLTYPE_PAIR);
    CPair* pair = NewPair();
    pair = pair ? pair->Init() : nullptr;
    Result().pair = pair;
    return pair;
}
CVector* NewResultVector()
{
    SetResultType(ESYMBOLTYPE_VECTOR);
    CVector* vector = NewVector();
    vector = vector ? vector->Init() : nullptr;
    Result().vector = vector;
    return vector;
}
CStruct* NewResultStructure()
{
    SetResultType(ESYMBOLTYPE_STRUCTURE);
    CStruct* structure = NewStruct();
    structure = structure ? structure->Init() : nullptr;
    Result().structure = structure;
    return structure;
}

bool IsStructure(uint32_t type) { return type == ESYMBOLTYPE_STRUCTURE || type == ESYMBOLTYPE_STRUCTUREPOINTER; }
bool IsArray(uint32_t type) { return type == ESYMBOLTYPE_ARRAY || type == ESYMBOLTYPE_ARRAYPOINTER; }

// Two strings joined (at most 200 characters with the terminator), or null if too long.
bool JoinStrings(const char* a, const char* b, ESymbolType type)
{
    size_t lengthA = std::strlen(a);
    size_t lengthB = std::strlen(b);
    if (lengthA + lengthB + 1 > 200)
        return false;
    char joined[200];
    std::memcpy(joined, a, lengthA);
    std::memcpy(joined + lengthA, b, lengthB + 1);
    SetResultType(type);
    Result().string = CreateString(joined);
    return true;
}

} // namespace

// 0x0047A910
void CopyArrayElementToComponent(CArray* array, uint32_t index, CComponent* to, int reference)
{
    to->typeAndFlag = static_cast<uint8_t>((to->typeAndFlag & 1) | (array->m_type << 1));
    switch (array->m_type) {
    case ESYMBOLTYPE_INTEGER:
    case ESYMBOLTYPE_NAME:
        to->raw = array->GetElement(index);
        break;
    case ESYMBOLTYPE_FLOAT:
        to->floatValue = array->GetFloat(index);
        break;
    case ESYMBOLTYPE_STRING:
        to->string = CreateString(array->GetLocalString(index));
        break;
    case ESYMBOLTYPE_PAIR: {
        auto* from = reinterpret_cast<const CPair*>(array->GetElement(index));
        CPair* pair = NewPair();
        pair = pair ? pair->Init() : nullptr;
        to->pair = pair;
        pair->x = from->x;
        to->pair->y = from->y;
        break;
    }
    case ESYMBOLTYPE_VECTOR: {
        auto* from = reinterpret_cast<const CVector*>(array->GetElement(index));
        CVector* vector = NewVector();
        vector = vector ? vector->Init() : nullptr;
        to->vector = vector;
        vector->x = from->x;
        to->vector->y = from->y;
        to->vector->z = from->z;
        break;
    }
    case ESYMBOLTYPE_STRUCTURE:
        if (reference) {
            to->typeAndFlag = static_cast<uint8_t>((to->typeAndFlag & 1) | (ESYMBOLTYPE_STRUCTUREPOINTER << 1));
            to->raw = array->GetElement(index);
        } else {
            CStruct* structure = NewStruct();
            to->structure = structure ? structure->Init() : nullptr;
            to->structure->AppendStructureIfAny(reinterpret_cast<CStruct*>(array->GetElement(index)));
        }
        break;
    case ESYMBOLTYPE_ARRAY:
        if (reference) {
            to->typeAndFlag = static_cast<uint8_t>((to->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAYPOINTER << 1));
            to->raw = array->GetElement(index);
        } else {
            CArray* copy = NewArray();
            to->array = copy ? copy->Init() : nullptr;
            CopyArray(to->array, reinterpret_cast<CArray*>(array->GetElement(index)));
        }
        break;
    default:
        // (local strings and the rest can't be taken out of arrays: the component is left empty)
        to->typeAndFlag &= 1;
        to->raw = 0;
        break;
    }
}

namespace {

// A component's value and type, a name standing for a global symbol counting as the symbol's.
void ResolvedValue(const CComponent* component, uint32_t* type, uint32_t* value)
{
    *type = component->Type();
    *value = component->raw;
    if (*type == ESYMBOLTYPE_NAME) {
        if (CSymbolTableEntry* symbol = LookUpSymbol(*value)) {
            *value = symbol->raw;
            *type = symbol->type;
        }
    }
}

double AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
double AsInteger(uint32_t bits) { return static_cast<double>(static_cast<int32_t>(bits)); }

// a < b: numbers by value; strings by length (!); structures: a is a subset of b.
bool Less(uint32_t typeA, uint32_t a, uint32_t typeB, uint32_t b)
{
    switch (typeA) {
    case ESYMBOLTYPE_INTEGER:
        if (typeB == ESYMBOLTYPE_FLOAT)
            return AsInteger(a) < AsFloat(b);
        return typeB == ESYMBOLTYPE_INTEGER && static_cast<int32_t>(a) < static_cast<int32_t>(b);
    case ESYMBOLTYPE_FLOAT:
        if (typeB == ESYMBOLTYPE_INTEGER)
            return AsFloat(a) < AsInteger(b);
        return typeB == ESYMBOLTYPE_FLOAT && AsFloat(a) < AsFloat(b);
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        return typeB == typeA &&
               std::strlen(reinterpret_cast<const char*>(a)) < std::strlen(reinterpret_cast<const char*>(b));
    case ESYMBOLTYPE_STRUCTURE:
    case ESYMBOLTYPE_STRUCTUREPOINTER:
        return (typeB == ESYMBOLTYPE_STRUCTURE || typeB == ESYMBOLTYPE_STRUCTUREPOINTER) &&
               reinterpret_cast<CStruct*>(a)->IsSubsetOf(reinterpret_cast<CStruct*>(b));
    default:
        return false;
    }
}

} // namespace

// 0x00479410 (a member of the first component in the game)
bool LessThan(CComponent* a, CComponent* b)
{
    uint32_t typeA, valueA, typeB, valueB;
    ResolvedValue(a, &typeA, &valueA);
    ResolvedValue(b, &typeB, &valueB);
    return Less(typeA, valueA, typeB, valueB);
}

// 0x004795A0
bool GreaterThan(CComponent* a, CComponent* b)
{
    uint32_t typeA, valueA, typeB, valueB;
    ResolvedValue(a, &typeA, &valueA);
    ResolvedValue(b, &typeB, &valueB);
    // (the same cases, decided by a's type)
    switch (typeA) {
    case ESYMBOLTYPE_INTEGER:
    case ESYMBOLTYPE_FLOAT:
        if (typeB != ESYMBOLTYPE_INTEGER && typeB != ESYMBOLTYPE_FLOAT)
            return false;
        if (typeA == ESYMBOLTYPE_INTEGER && typeB == ESYMBOLTYPE_INTEGER)
            return static_cast<int32_t>(valueA) > static_cast<int32_t>(valueB);
        return (typeA == ESYMBOLTYPE_INTEGER ? AsInteger(valueA) : AsFloat(valueA)) >
               (typeB == ESYMBOLTYPE_INTEGER ? AsInteger(valueB) : AsFloat(valueB));
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        return typeB == typeA && std::strlen(reinterpret_cast<const char*>(valueA)) >
                                     std::strlen(reinterpret_cast<const char*>(valueB));
    case ESYMBOLTYPE_STRUCTURE:
    case ESYMBOLTYPE_STRUCTUREPOINTER:
        // b is a subset of a
        return (typeB == ESYMBOLTYPE_STRUCTURE || typeB == ESYMBOLTYPE_STRUCTUREPOINTER) &&
               reinterpret_cast<CStruct*>(valueB)->IsSubsetOf(reinterpret_cast<CStruct*>(valueA));
    default:
        return false;
    }
}

// 0x0046D160
bool IsReference(const CComponent* component)
{
    return component->Type() == ESYMBOLTYPE_STRUCTUREPOINTER || component->Type() == ESYMBOLTYPE_ARRAYPOINTER;
}

// 0x0046D180
void MakeReference(CComponent* component)
{
    if (component->Type() == ESYMBOLTYPE_STRUCTURE)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (ESYMBOLTYPE_STRUCTUREPOINTER << 1));
    else if (component->Type() == ESYMBOLTYPE_ARRAY)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAYPOINTER << 1));
}

// 0x0046D1A0
void MakeOwning(CComponent* component)
{
    if (component->Type() == ESYMBOLTYPE_STRUCTUREPOINTER)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (ESYMBOLTYPE_STRUCTURE << 1));
    else if (component->Type() == ESYMBOLTYPE_ARRAYPOINTER)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAY << 1));
}

// 0x0046D280
int GetBool(const CComponent* component)
{
    if (component->Type() == ESYMBOLTYPE_INTEGER)
        return component->integer;
    if (component->Type() == ESYMBOLTYPE_FLOAT && component->floatValue != 0.0f)
        return 1;
    return 0;
}

// 0x0046D380
CExpressionEvaluator* CExpressionEvaluator::Init()
{
    for (CComponent& value : m_values)
        ConstructComponent(&value);
    Clear();
    m_unknown1F4 = true;
    return this;
}

// 0x0046D1C0
void CExpressionEvaluator::Clear()
{
    for (CComponent& value : m_values)
        CleanUpComponent(&value);
    m_valueTop = 0;
    m_operatorTop = 0;
    for (Operator& op : m_operators) {
        op.op = OPERATOR_NONE;
        op.openParentheses = 0;
    }
    m_hasOperators = false;
    m_error = nullptr;
}

// 0x0046D220
void CExpressionEvaluator::ClearIfNeeded()
{
    if (m_valueTop || (m_values[0].typeAndFlag & 0xFE) || m_operatorTop || m_error)
        Clear();
}

// 0x0046D260
void CExpressionEvaluator::SetError(const char* error)
{
    Clear();
    m_error = error;
}

// 0x0046D2B0
void CExpressionEvaluator::PushOperator(int op)
{
    if (++m_operatorTop >= 20) {
        Clear();
        m_error = Message(0x00647B50); // "Operator stack overflow"
        return;
    }
    m_operators[m_operatorTop].op = op;
    m_operators[m_operatorTop].openParentheses = 0;
    m_hasOperators = true;
}

// 0x0046D310
void CExpressionEvaluator::Input(const CComponent* value)
{
    // (the first value goes in the bottom slot)
    if (m_valueTop || (m_values[0].typeAndFlag & 0xFE)) {
        if (++m_valueTop >= 20) {
            Clear();
            m_error = Message(0x00647B68); // "Value stack overflow"
            return;
        }
    }
    CopyComponent(&m_values[m_valueTop], const_cast<CComponent*>(value), 0);
}

// 0x0046D370
void CExpressionEvaluator::OpenParenthesis()
{
    ++m_operators[m_operatorTop].openParentheses;
}

// 0x0046EB70
void CExpressionEvaluator::Input(int op)
{
    if (m_hasOperators) {
        while (m_operators[m_operatorTop].openParentheses == 0) {
            int pending = m_operators[m_operatorTop].op;
            if (op != pending && Precedence(op) > Precedence(pending))
                break;
            Execute();
            if (!m_hasOperators)
                break;
        }
    }
    PushOperator(op);
}

// 0x0046EBF0
void CExpressionEvaluator::CloseParenthesis()
{
    while (m_operators[m_operatorTop].openParentheses == 0) {
        Execute();
        if (m_error)
            return;
    }
    --m_operators[m_operatorTop].openParentheses;
}

// 0x0046EC40
bool CExpressionEvaluator::GetResult(CComponent* result)
{
    while (m_operators[m_operatorTop].openParentheses == 0) {
        if (m_valueTop == 0) {
            if (m_operators[0].op != OPERATOR_NONE || m_operatorTop != 0) {
                Clear();
                m_error = Message(0x006482F8); // "Too many operators in expression"
                return false;
            }
            // A reference result is copied as what it refers to.
            if (IsReference(&m_values[0])) {
                MakeOwning(&m_values[0]);
                CopyComponent(result, &m_values[0], 0);
                MakeReference(&m_values[0]);
            } else {
                CopyComponent(result, &m_values[0], 0);
            }
            CleanUpComponent(&m_values[0]);
            return true;
        }
        Execute();
        if (m_error)
            return false;
    }
    Clear();
    m_error = Message(0x0064831C); // "Not enough close parentheses"
    return false;
}

// 0x0046D450
void CExpressionEvaluator::Execute()
{
    if (m_valueTop < 1) {
        Clear();
        m_error = Message(0x006482C8); // "Not enough values in stack to execute operation"
        return;
    }
    if (m_operators[m_operatorTop].openParentheses != 0) {
        Clear();
        m_error = Message(0x006482AC); // "Non-zero parentheses count"
        return;
    }
    int op = m_operators[m_operatorTop].op;
    if (op == OPERATOR_NONE) {
        Clear();
        m_error = Message(0x00648288); // "Tried to execute with no operator"
        return;
    }

    CComponent* b = &m_values[m_valueTop];
    CComponent* a = b - 1;
    CComponent& result = Result();
    result.typeAndFlag &= 1;
    result.raw = 0;
    const uint32_t typeA = a->Type();
    const uint32_t typeB = b->Type();
    auto error = [&](uint32_t message) { SetError(Message(message)); };

    switch (op) {
    case OPERATOR_OR:
        SetResultType(ESYMBOLTYPE_INTEGER);
        result.raw = static_cast<uint32_t>(GetBool(b) | GetBool(a));
        break;
    case OPERATOR_AND:
        SetResultType(ESYMBOLTYPE_INTEGER);
        result.raw = static_cast<uint32_t>(GetBool(b) & GetBool(a));
        break;
    case OPERATOR_EQUALS:
        SetResultType(ESYMBOLTYPE_INTEGER);
        result.raw = ComponentsEqual(a, b) ? 1 : 0;
        break;
    case OPERATOR_LESS_THAN:
        SetResultType(ESYMBOLTYPE_INTEGER);
        result.raw = LessThan(a, b) ? 1 : 0;
        break;
    case OPERATOR_GREATER_THAN:
        SetResultType(ESYMBOLTYPE_INTEGER);
        result.raw = GreaterThan(a, b) ? 1 : 0;
        break;

    case OPERATOR_ADD:
        switch (typeA) {
        case ESYMBOLTYPE_INTEGER:
            if (typeB == ESYMBOLTYPE_INTEGER)
                SetResultInteger(static_cast<uint32_t>(a->integer) + static_cast<uint32_t>(b->integer));
            else if (typeB == ESYMBOLTYPE_FLOAT)
                SetResultFloat(AsDouble(a->integer) + AsDouble(b->floatValue));
            else
                error(0x0064825C); // "Second arg cannot be added to an integer"
            break;
        case ESYMBOLTYPE_FLOAT:
            if (typeB == ESYMBOLTYPE_INTEGER)
                SetResultFloat(AsDouble(b->integer) + AsDouble(a->floatValue));
            else if (typeB == ESYMBOLTYPE_FLOAT)
                SetResultFloat(AsDouble(a->floatValue) + AsDouble(b->floatValue));
            else
                error(0x00648234); // "Second arg cannot be added to a float"
            break;
        case ESYMBOLTYPE_NAME:
            // A checksum plus an integer or checksum is a checksum.
            if (typeB == ESYMBOLTYPE_INTEGER || typeB == ESYMBOLTYPE_NAME) {
                SetResultType(ESYMBOLTYPE_NAME);
                result.raw = b->raw + a->raw;
            } else {
                error(0x00648208); // "Second arg cannot be added to a checksum"
            }
            break;
        case ESYMBOLTYPE_VECTOR:
            if (typeB == ESYMBOLTYPE_VECTOR) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x + b->vector->x;
                result.vector->y = a->vector->y + b->vector->y;
                result.vector->z = a->vector->z + b->vector->z;
            } else {
                error(0x006481E0); // "Second arg cannot be added to a vector"
            }
            break;
        case ESYMBOLTYPE_PAIR:
            if (typeB == ESYMBOLTYPE_PAIR) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x + b->pair->x;
                result.pair->y = a->pair->y + b->pair->y;
            } else {
                error(0x006481B8); // "Second arg cannot be added to a pair"
            }
            break;
        case ESYMBOLTYPE_STRING:
            if (typeB != ESYMBOLTYPE_STRING)
                error(0x0064816C); // "Second arg cannot be added to a string"
            else if (!JoinStrings(a->string, b->string, ESYMBOLTYPE_STRING))
                error(0x00648194); // "Result of string addition too long"
            break;
        case ESYMBOLTYPE_LOCALSTRING:
            if (typeB != ESYMBOLTYPE_LOCALSTRING)
                error(0x00648110); // "Second arg cannot be added to a local-string"
            else if (!JoinStrings(a->string, b->string, ESYMBOLTYPE_LOCALSTRING))
                error(0x00648140); // "Result of local-string addition too long"
            break;
        case ESYMBOLTYPE_STRUCTURE:
        case ESYMBOLTYPE_STRUCTUREPOINTER:
            if (IsStructure(typeB)) {
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                result.structure->AppendStructureIfAny(b->structure);
            } else {
                error(0x006480D4); // "Structure add operator requires 2nd arg to be a structure"
            }
            break;
        case ESYMBOLTYPE_ARRAY:
        case ESYMBOLTYPE_ARRAYPOINTER: {
            if (!IsArray(typeB)) {
                error(0x00648098); // "Array add operator requires 2nd arg to be an array as well"
                break;
            }
            CArray* array = NewArray();
            array = array ? array->Init() : nullptr;
            SetResultType(ESYMBOLTYPE_ARRAY);
            result.array = array;
            uint32_t sizeA = a->array->m_size;
            uint32_t sizeB = b->array->m_size;
            if (sizeA == 0) {
                CopyArray(array, b->array);
            } else if (sizeB == 0) {
                CopyArray(array, a->array);
            } else {
                // (the elements are all taken to be of the first array's type)
                ESymbolType type = a->array->m_type;
                array->SetSizeAndType(sizeA + sizeB, type);
                CopyArrayElements(array->GetArrayPointer(), a->array->GetArrayPointer(), sizeA, type);
                CopyArrayElements(array->GetArrayPointer() + sizeA, b->array->GetArrayPointer(), sizeB, type);
            }
            break;
        }
        default:
            error(0x00648070); // "Addition not supported for this type"
            break;
        }
        break;

    case OPERATOR_MINUS:
        switch (typeA) {
        case ESYMBOLTYPE_INTEGER:
            if (typeB == ESYMBOLTYPE_INTEGER)
                SetResultInteger(static_cast<uint32_t>(a->integer) - static_cast<uint32_t>(b->integer));
            else if (typeB == ESYMBOLTYPE_FLOAT)
                SetResultFloat(AsDouble(a->integer) - AsDouble(b->floatValue));
            else
                error(0x00648040); // "Second arg cannot be subtracted from an integer"
            break;
        case ESYMBOLTYPE_FLOAT:
            if (typeB == ESYMBOLTYPE_INTEGER)
                SetResultFloat(AsDouble(a->floatValue) - AsDouble(b->integer));
            else if (typeB == ESYMBOLTYPE_FLOAT)
                SetResultFloat(AsDouble(a->floatValue) - AsDouble(b->floatValue));
            else
                error(0x00648010); // "Second arg cannot be subtracted from a float"
            break;
        case ESYMBOLTYPE_PAIR:
            if (typeB == ESYMBOLTYPE_PAIR) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x - b->pair->x;
                result.pair->y = a->pair->y - b->pair->y;
            } else {
                error(0x00647FB4); // "Second arg cannot be subtracted from a pair"
            }
            break;
        case ESYMBOLTYPE_VECTOR:
            if (typeB == ESYMBOLTYPE_VECTOR) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x - b->vector->x;
                result.vector->y = a->vector->y - b->vector->y;
                result.vector->z = a->vector->z - b->vector->z;
            } else {
                error(0x00647FE0); // "Second arg cannot be subtracted from a vector"
            }
            break;
        case ESYMBOLTYPE_STRUCTURE:
        case ESYMBOLTYPE_STRUCTUREPOINTER:
            if (typeB == ESYMBOLTYPE_NAME) {
                // Without the name (as a parameter or a flag).
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                result.structure->RemoveComponent(b->checksum);
                result.structure->RemoveFlag(b->checksum);
            } else if (IsArray(typeB)) {
                // Without each name in the array.
                CArray* names = b->array;
                if (names->m_type != ESYMBOLTYPE_NAME && names->m_type != ESYMBOLTYPE_NONE) {
                    // "Subtracting an array from a structure requires the array to be an array of names"
                    error(0x00647F00);
                    break;
                }
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                for (uint32_t i = 0; i < names->m_size; ++i) {
                    result.structure->RemoveComponent(names->GetElement(i));
                    result.structure->RemoveFlag(names->GetElement(i));
                }
            } else if (IsStructure(typeB)) {
                // Without each of the other structure's parameters (by name and type) and flags.
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                CStruct* other = b->structure;
                for (CComponent* each = other->GetNextComponent(nullptr); each; each = other->GetNextComponent(each)) {
                    if (each->name) {
                        result.structure->RemoveComponentOfType(each->name, each->Type());
                    } else if (each->Type() == ESYMBOLTYPE_NAME) {
                        result.structure->RemoveComponent(each->checksum);
                        result.structure->RemoveFlag(each->checksum);
                    }
                }
            } else {
                // "Structure minus operator requires 2nd arg to be a name, an array of names, or a structure"
                error(0x00647F58);
            }
            break;
        default:
            error(0x00647ED8); // "Subtraction not supported for this type"
            break;
        }
        break;

    case OPERATOR_MULTIPLY:
        switch (typeA) {
        case ESYMBOLTYPE_INTEGER:
            switch (typeB) {
            case ESYMBOLTYPE_INTEGER:
                SetResultInteger(static_cast<uint32_t>(b->integer) * static_cast<uint32_t>(a->integer));
                break;
            case ESYMBOLTYPE_FLOAT:
                SetResultFloat(AsDouble(a->integer) * AsDouble(b->floatValue));
                break;
            case ESYMBOLTYPE_VECTOR: {
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(a->integer) * AsDouble(b->vector->x));
                result.vector->y = Rounded(AsDouble(a->integer) * AsDouble(b->vector->y));
                result.vector->z = Rounded(AsDouble(a->integer) * AsDouble(b->vector->z));
                break;
            }
            case ESYMBOLTYPE_PAIR: {
                CPair* pair = NewResultPair();
                pair->x = Rounded(AsDouble(a->integer) * AsDouble(b->pair->x));
                result.pair->y = Rounded(AsDouble(a->integer) * AsDouble(b->pair->y));
                break;
            }
            default:
                error(0x00647EA8); // "Second arg cannot be multiplied by an integer"
                break;
            }
            break;
        case ESYMBOLTYPE_FLOAT:
            switch (typeB) {
            case ESYMBOLTYPE_INTEGER:
                SetResultFloat(AsDouble(b->integer) * AsDouble(a->floatValue));
                break;
            case ESYMBOLTYPE_FLOAT:
                SetResultFloat(AsDouble(b->floatValue) * AsDouble(a->floatValue));
                break;
            case ESYMBOLTYPE_VECTOR: {
                CVector* vector = NewResultVector();
                vector->x = b->vector->x * a->floatValue;
                result.vector->y = b->vector->y * a->floatValue;
                result.vector->z = b->vector->z * a->floatValue;
                break;
            }
            case ESYMBOLTYPE_PAIR: {
                CPair* pair = NewResultPair();
                pair->x = b->pair->x * a->floatValue;
                result.pair->y = b->pair->y * a->floatValue;
                break;
            }
            default:
                error(0x00647E7C); // "Second arg cannot be multiplied by a float"
                break;
            }
            break;
        case ESYMBOLTYPE_PAIR:
            if (typeB == ESYMBOLTYPE_INTEGER) {
                CPair* pair = NewResultPair();
                pair->x = Rounded(AsDouble(b->integer) * AsDouble(a->pair->x));
                result.pair->y = Rounded(AsDouble(b->integer) * AsDouble(a->pair->y));
            } else if (typeB == ESYMBOLTYPE_FLOAT) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x * b->floatValue;
                result.pair->y = a->pair->y * b->floatValue;
            } else {
                error(0x00647E28); // "Pair cannot be multiplied by second arg"
            }
            break;
        case ESYMBOLTYPE_VECTOR:
            if (typeB == ESYMBOLTYPE_INTEGER) {
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(b->integer) * AsDouble(a->vector->x));
                result.vector->y = Rounded(AsDouble(b->integer) * AsDouble(a->vector->y));
                result.vector->z = Rounded(AsDouble(b->integer) * AsDouble(a->vector->z));
            } else if (typeB == ESYMBOLTYPE_FLOAT) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x * b->floatValue;
                result.vector->y = a->vector->y * b->floatValue;
                result.vector->z = a->vector->z * b->floatValue;
            } else if (typeB == ESYMBOLTYPE_VECTOR) {
                // The cross product (each coordinate computed as the x87 does: in one go, then rounded).
                const CVector* u = a->vector;
                const CVector* v = b->vector;
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(v->z) * u->y - AsDouble(v->y) * u->z);
                result.vector->y = Rounded(AsDouble(v->x) * u->z - AsDouble(v->z) * u->x);
                result.vector->z = Rounded(AsDouble(v->y) * u->x - AsDouble(v->x) * u->y);
            } else {
                error(0x00647E50); // "Vector cannot be multiplied by second arg"
            }
            break;
        default:
            error(0x00647DFC); // "Multiplication not supported for this type"
            break;
        }
        break;

    case OPERATOR_DIVIDE:
        if (typeB == ESYMBOLTYPE_INTEGER) {
            if (b->integer == 0) {
                error(0x00647DE0); // "Integer division by zero"
                break;
            }
        } else if (typeB == ESYMBOLTYPE_FLOAT) {
            if (std::fabs(b->floatValue) < GameVariable<float>(0x00647DDC)) {
                error(0x00647DC4); // "Float division by zero"
                break;
            }
        }
        switch (typeA) {
        case ESYMBOLTYPE_INTEGER:
            if (typeB == ESYMBOLTYPE_INTEGER)
                SetResultInteger(static_cast<uint32_t>(a->integer / b->integer));
            else if (typeB == ESYMBOLTYPE_FLOAT)
                SetResultFloat(AsDouble(a->integer) / AsDouble(b->floatValue));
            else
                error(0x00647D9C); // "Integer cannot be divided by second arg"
            break;
        case ESYMBOLTYPE_FLOAT:
            if (typeB == ESYMBOLTYPE_INTEGER)
                SetResultFloat(AsDouble(a->floatValue) / AsDouble(b->integer));
            else if (typeB == ESYMBOLTYPE_FLOAT)
                SetResultFloat(AsDouble(a->floatValue) / AsDouble(b->floatValue));
            else
                error(0x00647D74); // "Float cannot be divided by second arg"
            break;
        case ESYMBOLTYPE_PAIR:
            if (typeB == ESYMBOLTYPE_INTEGER) {
                CPair* pair = NewResultPair();
                pair->x = Rounded(AsDouble(a->pair->x) / AsDouble(b->integer));
                result.pair->y = Rounded(AsDouble(a->pair->y) / AsDouble(b->integer));
            } else if (typeB == ESYMBOLTYPE_FLOAT) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x / b->floatValue;
                result.pair->y = a->pair->y / b->floatValue;
            } else {
                error(0x00647D24); // "Pair cannot be divided by second arg"
            }
            break;
        case ESYMBOLTYPE_VECTOR:
            if (typeB == ESYMBOLTYPE_INTEGER) {
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(a->vector->x) / AsDouble(b->integer));
                result.vector->y = Rounded(AsDouble(a->vector->y) / AsDouble(b->integer));
                result.vector->z = Rounded(AsDouble(a->vector->z) / AsDouble(b->integer));
            } else if (typeB == ESYMBOLTYPE_FLOAT) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x / b->floatValue;
                result.vector->y = a->vector->y / b->floatValue;
                result.vector->z = a->vector->z / b->floatValue;
            } else {
                error(0x00647D4C); // "Vector cannot be divided by second arg"
            }
            break;
        default:
            error(0x00647CFC); // "Division not supported for this type"
            break;
        }
        break;

    case OPERATOR_DOT:
        switch (typeA) {
        case ESYMBOLTYPE_VECTOR:
            if (typeB == ESYMBOLTYPE_VECTOR) {
                const CVector* u = a->vector;
                const CVector* v = b->vector;
                SetResultFloat(AsDouble(v->z) * u->z + AsDouble(v->y) * u->y + AsDouble(v->x) * u->x);
            } else {
                error(0x00647CC4); // "Vector can only be dot producted with another vector"
            }
            break;
        case ESYMBOLTYPE_PAIR:
            if (typeB == ESYMBOLTYPE_PAIR) {
                const CPair* u = a->pair;
                const CPair* v = b->pair;
                SetResultFloat(AsDouble(v->y) * u->y + AsDouble(v->x) * u->x);
            } else {
                error(0x00647C90); // "Pair can only be dot producted with another pair"
            }
            break;
        case ESYMBOLTYPE_STRUCTURE:
        case ESYMBOLTYPE_STRUCTUREPOINTER:
            // A structure's member: copied (a referenced structure's members are referred to).
            if (typeB == ESYMBOLTYPE_NAME) {
                if (CComponent* member = a->structure->FindLastComponent(b->checksum))
                    CopyComponent(&result, member, typeA == ESYMBOLTYPE_STRUCTUREPOINTER);
            } else {
                error(0x00647C58); // "Structure dot operator requires 2nd arg to be a name"
            }
            break;
        default:
            error(0x00647C30); // "Dot product not supported for this type"
            break;
        }
        break;

    case OPERATOR_ARRAY_ELEMENT:
        if (!IsArray(typeA)) {
            error(0x00647BFC); // "Array element operator not supported for this type"
        } else if (typeB != ESYMBOLTYPE_INTEGER) {
            error(0x00647BE0); // "[] index must be an integer"
        } else if (b->raw >= a->array->m_size) {
            error(0x00647BC8); // "[] index out of range"
        } else {
            CopyArrayElementToComponent(a->array, b->raw, &result, typeA == ESYMBOLTYPE_ARRAYPOINTER);
            if ((result.typeAndFlag & 0xFE) == 0)
                error(0x00647B98); // "Array type cannot be accessed using [] yet ..."
        }
        break;

    default:
        Clear();
        m_error = Message(0x00647B80); // "Operator not supported"
        break;
    }

    // The result replaces the two values (also after an error, which has emptied the stacks).
    CleanUpComponent(a);
    CleanUpComponent(b);
    a->typeAndFlag = static_cast<uint8_t>((a->typeAndFlag & 1) | (result.typeAndFlag & 0xFE));
    a->raw = result.raw;
    result.typeAndFlag &= 1;
    result.raw = 0;
    --m_valueTop;
    m_operators[m_operatorTop].op = OPERATOR_NONE;
    if (m_operatorTop == 0)
        m_hasOperators = false;
    else
        --m_operatorTop;
}

} // namespace Script

namespace {

using Script::CExpressionEvaluator;

struct ComponentAdapters {
    bool LessThan(Script::CComponent* other) { return Script::LessThan(reinterpret_cast<Script::CComponent*>(this), other); }
    bool GreaterThan(Script::CComponent* other)
    {
        return Script::GreaterThan(reinterpret_cast<Script::CComponent*>(this), other);
    }
    bool IsReference() { return Script::IsReference(reinterpret_cast<Script::CComponent*>(this)); }
    void MakeReference() { Script::MakeReference(reinterpret_cast<Script::CComponent*>(this)); }
    void MakeOwning() { Script::MakeOwning(reinterpret_cast<Script::CComponent*>(this)); }
    int GetBool() { return Script::GetBool(reinterpret_cast<Script::CComponent*>(this)); }
};

} // namespace

REPLACE(0x0046D160, &ComponentAdapters::IsReference);
REPLACE(0x0046D180, &ComponentAdapters::MakeReference);
REPLACE(0x0046D1A0, &ComponentAdapters::MakeOwning);
REPLACE(0x0046D280, &ComponentAdapters::GetBool);
REPLACE(0x0046D380, &CExpressionEvaluator::Init);
REPLACE(0x0046D1C0, &CExpressionEvaluator::Clear);
REPLACE_SOURCE_ONLY(0x0046D3C0, &CExpressionEvaluator::Clear); // the destructor (the static evaluator's, at exit): a jump to 0x0046D1C0
REPLACE(0x0046D220, &CExpressionEvaluator::ClearIfNeeded);
REPLACE(0x0046D250, &CExpressionEvaluator::SetTokenPointer);
REPLACE(0x0046D260, &CExpressionEvaluator::SetError);
REPLACE(0x0046D2B0, &CExpressionEvaluator::PushOperator);
REPLACE(0x0046D310, static_cast<void (CExpressionEvaluator::*)(const Script::CComponent*)>(&CExpressionEvaluator::Input));
REPLACE(0x0046D370, &CExpressionEvaluator::OpenParenthesis);
REPLACE(0x0046EB70, static_cast<void (CExpressionEvaluator::*)(int)>(&CExpressionEvaluator::Input));
REPLACE(0x0046EBF0, &CExpressionEvaluator::CloseParenthesis);
REPLACE(0x0046EC40, &CExpressionEvaluator::GetResult);
REPLACE(0x0046D450, &CExpressionEvaluator::Execute);
REPLACE(0x00479410, &ComponentAdapters::LessThan);
REPLACE(0x004795A0, &ComponentAdapters::GreaterThan);
REPLACE(0x0047A910, Script::CopyArrayElementToComponent);
