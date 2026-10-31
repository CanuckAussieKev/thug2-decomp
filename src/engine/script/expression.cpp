#include "engine/script/expression.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Scripts {

static_assert(offsetof(CValueResolver, m_valueTop) == 0x140, "layout must match the original");
static_assert(offsetof(CValueResolver, m_operators) == 0x144, "layout must match the original");
static_assert(offsetof(CValueResolver, m_operatorTop) == 0x1E4, "layout must match the original");
static_assert(offsetof(CValueResolver, m_hasOperators) == 0x1E8, "layout must match the original");
static_assert(offsetof(CValueResolver, m_token) == 0x1EC, "layout must match the original");
static_assert(offsetof(CValueResolver, m_fault) == 0x1F0, "layout must match the original");
static_assert(offsetof(CValueResolver, m_unknown1F4) == 0x1F4, "layout must match the original");

float g_evalDivisorMinimum{0.000001f};
CComponent g_evalResult;

namespace {

// The operators' precedences, by operator.
int g_operatorPrecedence[74]{
    -1, -1, -1, -1, -1, 100, -1, 76, 100, -1, 98, 98,
    99, 100, -1, -1, -1, -1, 80, 79, 78, 77, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 58, 60, 59, 90, 89, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1};
int Precedence(int op) { return g_operatorPrecedence[op]; }

// Where an operation's result is built (a global component: only its type and value are taken).
CComponent& Result() { return g_evalResult; }

// The error messages are the game's own strings.

void SetResultType(EQbValueKind type)
{
    Result().typeAndFlag = static_cast<uint8_t>((Result().typeAndFlag & 1) | (type << 1));
}

// Mixed integer / float arithmetic is done as the x87 does it: exactly, rounded to a float at the end.
double AsDouble(int32_t value) { return static_cast<double>(value); }
double AsDouble(float value) { return static_cast<double>(value); }
float Rounded(double value) { return static_cast<float>(value); }

void SetResultFloat(double value)
{
    SetResultType(QBVAL_FLOAT);
    Result().floatValue = Rounded(value);
}
void SetResultInteger(uint32_t value)
{
    SetResultType(QBVAL_INTEGER);
    Result().raw = value;
}

CPair* NewResultPair()
{
    SetResultType(QBVAL_PAIR);
    CPair* pair = NewPair();
    pair = pair ? pair->PrepareAll() : nullptr;
    Result().pair = pair;
    return pair;
}
CVector* NewResultVector()
{
    SetResultType(QBVAL_VECTOR);
    CVector* vector = AllocScriptVector();
    vector = vector ? vector->PrepareAll() : nullptr;
    Result().vector = vector;
    return vector;
}
CStruct* NewResultStructure()
{
    SetResultType(QBVAL_STRUCT);
    CStruct* structure = SpawnParamBag();
    structure = structure ? structure->PrepareAll() : nullptr;
    Result().structure = structure;
    return structure;
}

bool IsStructure(uint32_t type) { return type == QBVAL_STRUCT || type == QBVAL_STRUCT_REF; }
bool IsArray(uint32_t type) { return type == QBVAL_ARRAY || type == ESYMBOLTYPE_ARRAYPOINTER; }

// Two strings joined (at most 200 characters with the terminator), or null if too long.
bool JoinStrings(const char* a, const char* b, EQbValueKind type)
{
    size_t lengthA = std::strlen(a);
    size_t lengthB = std::strlen(b);
    if (lengthA + lengthB + 1 > 200)
        return false;
    char joined[200];
    std::memcpy(joined, a, lengthA);
    std::memcpy(joined + lengthA, b, lengthB + 1);
    SetResultType(type);
    Result().string = SpawnText(joined);
    return true;
}

} // namespace

// 0x0047A910
void CopyArrayElementToComponent(CArray* array, uint32_t index, CComponent* to, int reference)
{
    to->typeAndFlag = static_cast<uint8_t>((to->typeAndFlag & 1) | (array->m_kindTag << 1));
    switch (array->m_kindTag) {
    case QBVAL_INTEGER:
    case QBVAL_NAME:
        to->raw = array->FetchPart(index);
        break;
    case QBVAL_FLOAT:
        to->floatValue = array->FetchFloat(index);
        break;
    case QBVAL_STRING:
        to->string = SpawnText(array->FindInlineText(index));
        break;
    case QBVAL_PAIR: {
        auto* from = reinterpret_cast<const CPair*>(array->FetchPart(index));
        CPair* pair = NewPair();
        pair = pair ? pair->PrepareAll() : nullptr;
        to->pair = pair;
        pair->x = from->x;
        to->pair->y = from->y;
        break;
    }
    case QBVAL_VECTOR: {
        auto* from = reinterpret_cast<const CVector*>(array->FetchPart(index));
        CVector* vector = AllocScriptVector();
        vector = vector ? vector->PrepareAll() : nullptr;
        to->vector = vector;
        vector->x = from->x;
        to->vector->y = from->y;
        to->vector->z = from->z;
        break;
    }
    case QBVAL_STRUCT:
        if (reference) {
            to->typeAndFlag = static_cast<uint8_t>((to->typeAndFlag & 1) | (QBVAL_STRUCT_REF << 1));
            to->raw = array->FetchPart(index);
        } else {
            CStruct* structure = SpawnParamBag();
            to->structure = structure ? structure->PrepareAll() : nullptr;
            to->structure->AppendStructureIfAny(reinterpret_cast<CStruct*>(array->FetchPart(index)));
        }
        break;
    case QBVAL_ARRAY:
        if (reference) {
            to->typeAndFlag = static_cast<uint8_t>((to->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAYPOINTER << 1));
            to->raw = array->FetchPart(index);
        } else {
            CArray* copy = AllocScriptArray();
            to->array = copy ? copy->PrepareAll() : nullptr;
            DuplicateArray(to->array, reinterpret_cast<CArray*>(array->FetchPart(index)));
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
    *type = component->KindTag();
    *value = component->raw;
    if (*type == QBVAL_NAME) {
        if (CSymbolTableEntry* symbol = ResolveSymbol(*value)) {
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
    case QBVAL_INTEGER:
        if (typeB == QBVAL_FLOAT)
            return AsInteger(a) < AsFloat(b);
        return typeB == QBVAL_INTEGER && static_cast<int32_t>(a) < static_cast<int32_t>(b);
    case QBVAL_FLOAT:
        if (typeB == QBVAL_INTEGER)
            return AsFloat(a) < AsInteger(b);
        return typeB == QBVAL_FLOAT && AsFloat(a) < AsFloat(b);
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT:
        return typeB == typeA &&
               std::strlen(reinterpret_cast<const char*>(a)) < std::strlen(reinterpret_cast<const char*>(b));
    case QBVAL_STRUCT:
    case QBVAL_STRUCT_REF:
        return (typeB == QBVAL_STRUCT || typeB == QBVAL_STRUCT_REF) &&
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
    case QBVAL_INTEGER:
    case QBVAL_FLOAT:
        if (typeB != QBVAL_INTEGER && typeB != QBVAL_FLOAT)
            return false;
        if (typeA == QBVAL_INTEGER && typeB == QBVAL_INTEGER)
            return static_cast<int32_t>(valueA) > static_cast<int32_t>(valueB);
        return (typeA == QBVAL_INTEGER ? AsInteger(valueA) : AsFloat(valueA)) >
               (typeB == QBVAL_INTEGER ? AsInteger(valueB) : AsFloat(valueB));
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT:
        return typeB == typeA && std::strlen(reinterpret_cast<const char*>(valueA)) >
                                     std::strlen(reinterpret_cast<const char*>(valueB));
    case QBVAL_STRUCT:
    case QBVAL_STRUCT_REF:
        // b is a subset of a
        return (typeB == QBVAL_STRUCT || typeB == QBVAL_STRUCT_REF) &&
               reinterpret_cast<CStruct*>(valueB)->IsSubsetOf(reinterpret_cast<CStruct*>(valueA));
    default:
        return false;
    }
}

// 0x0046D160
bool IsReference(const CComponent* component)
{
    return component->KindTag() == QBVAL_STRUCT_REF || component->KindTag() == ESYMBOLTYPE_ARRAYPOINTER;
}

// 0x0046D180
void MakeReference(CComponent* component)
{
    if (component->KindTag() == QBVAL_STRUCT)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (QBVAL_STRUCT_REF << 1));
    else if (component->KindTag() == QBVAL_ARRAY)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAYPOINTER << 1));
}

// 0x0046D1A0
void MakeOwning(CComponent* component)
{
    if (component->KindTag() == QBVAL_STRUCT_REF)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (QBVAL_STRUCT << 1));
    else if (component->KindTag() == ESYMBOLTYPE_ARRAYPOINTER)
        component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (QBVAL_ARRAY << 1));
}

// 0x0046D280
int FetchFlag(const CComponent* component)
{
    if (component->KindTag() == QBVAL_INTEGER)
        return component->integer;
    if (component->KindTag() == QBVAL_FLOAT && component->floatValue != 0.0f)
        return 1;
    return 0;
}

// 0x0046D380
CValueResolver* CValueResolver::PrepareAll()
{
    for (CComponent& value : m_values)
        ConstructComponent(&value);
    Clear();
    m_unknown1F4 = true;
    return this;
}

// 0x0046D1C0
void CValueResolver::Clear()
{
    for (CComponent& value : m_values)
        ReleaseComponent(&value);
    m_valueTop = 0;
    m_operatorTop = 0;
    for (Operator& op : m_operators) {
        op.op = OPERATOR_NONE;
        op.openParentheses = 0;
    }
    m_hasOperators = false;
    m_fault = nullptr;
}

// 0x0046D220
void CValueResolver::ResetIfDirty()
{
    if (m_valueTop || (m_values[0].typeAndFlag & 0xFE) || m_operatorTop || m_fault)
        Clear();
}

// 0x0046D260
void CValueResolver::ChangeFault(const char* error)
{
    Clear();
    m_fault = error;
}

// 0x0046D2B0
void CValueResolver::PushOperator(int op)
{
    if (++m_operatorTop >= 20) {
        Clear();
        m_fault = "Operator stack overflow"; // "Operator stack overflow"
        return;
    }
    m_operators[m_operatorTop].op = op;
    m_operators[m_operatorTop].openParentheses = 0;
    m_hasOperators = true;
}

// 0x0046D310
void CValueResolver::Intake(const CComponent* value)
{
    // (the first value goes in the bottom slot)
    if (m_valueTop || (m_values[0].typeAndFlag & 0xFE)) {
        if (++m_valueTop >= 20) {
            Clear();
            m_fault = "Value stack overflow"; // "Value stack overflow"
            return;
        }
    }
    DuplicateComponent(&m_values[m_valueTop], const_cast<CComponent*>(value), 0);
}

// 0x0046D370
void CValueResolver::OpenBracket()
{
    ++m_operators[m_operatorTop].openParentheses;
}

// 0x0046EB70
void CValueResolver::Intake(int op)
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
void CValueResolver::CloseBracket()
{
    while (m_operators[m_operatorTop].openParentheses == 0) {
        Execute();
        if (m_fault)
            return;
    }
    --m_operators[m_operatorTop].openParentheses;
}

// 0x0046EC40
bool CValueResolver::FetchVerdict(CComponent* result)
{
    while (m_operators[m_operatorTop].openParentheses == 0) {
        if (m_valueTop == 0) {
            if (m_operators[0].op != OPERATOR_NONE || m_operatorTop != 0) {
                Clear();
                m_fault = "Too many operators in expression"; // "Too many operators in expression"
                return false;
            }
            // A reference result is copied as what it refers to.
            if (IsReference(&m_values[0])) {
                MakeOwning(&m_values[0]);
                DuplicateComponent(result, &m_values[0], 0);
                MakeReference(&m_values[0]);
            } else {
                DuplicateComponent(result, &m_values[0], 0);
            }
            ReleaseComponent(&m_values[0]);
            return true;
        }
        Execute();
        if (m_fault)
            return false;
    }
    Clear();
    m_fault = "Not enough close parentheses"; // "Not enough close parentheses"
    return false;
}

// 0x0046D450
void CValueResolver::Execute()
{
    if (m_valueTop < 1) {
        Clear();
        m_fault = "Not enough values in stack to execute operation"; // "Not enough values in stack to execute operation"
        return;
    }
    if (m_operators[m_operatorTop].openParentheses != 0) {
        Clear();
        m_fault = "Non-zero parentheses count"; // "Non-zero parentheses count"
        return;
    }
    int op = m_operators[m_operatorTop].op;
    if (op == OPERATOR_NONE) {
        Clear();
        m_fault = "Tried to execute with no operator"; // "Tried to execute with no operator"
        return;
    }

    CComponent* b = &m_values[m_valueTop];
    CComponent* a = b - 1;
    CComponent& result = Result();
    result.typeAndFlag &= 1;
    result.raw = 0;
    const uint32_t typeA = a->KindTag();
    const uint32_t typeB = b->KindTag();
    auto error = [&](const char* message) { ChangeFault(message); };

    switch (op) {
    case OPERATOR_OR:
        SetResultType(QBVAL_INTEGER);
        result.raw = static_cast<uint32_t>(FetchFlag(b) | FetchFlag(a));
        break;
    case OPERATOR_AND:
        SetResultType(QBVAL_INTEGER);
        result.raw = static_cast<uint32_t>(FetchFlag(b) & FetchFlag(a));
        break;
    case OPERATOR_EQUALS:
        SetResultType(QBVAL_INTEGER);
        result.raw = ComponentsEqual(a, b) ? 1 : 0;
        break;
    case OPERATOR_LESS_THAN:
        SetResultType(QBVAL_INTEGER);
        result.raw = LessThan(a, b) ? 1 : 0;
        break;
    case OPERATOR_GREATER_THAN:
        SetResultType(QBVAL_INTEGER);
        result.raw = GreaterThan(a, b) ? 1 : 0;
        break;

    case OPERATOR_ADD:
        switch (typeA) {
        case QBVAL_INTEGER:
            if (typeB == QBVAL_INTEGER)
                SetResultInteger(static_cast<uint32_t>(a->integer) + static_cast<uint32_t>(b->integer));
            else if (typeB == QBVAL_FLOAT)
                SetResultFloat(AsDouble(a->integer) + AsDouble(b->floatValue));
            else
                error("Second arg cannot be added to an integer"); // "Second arg cannot be added to an integer"
            break;
        case QBVAL_FLOAT:
            if (typeB == QBVAL_INTEGER)
                SetResultFloat(AsDouble(b->integer) + AsDouble(a->floatValue));
            else if (typeB == QBVAL_FLOAT)
                SetResultFloat(AsDouble(a->floatValue) + AsDouble(b->floatValue));
            else
                error("Second arg cannot be added to a float"); // "Second arg cannot be added to a float"
            break;
        case QBVAL_NAME:
            // A checksum plus an integer or checksum is a checksum.
            if (typeB == QBVAL_INTEGER || typeB == QBVAL_NAME) {
                SetResultType(QBVAL_NAME);
                result.raw = b->raw + a->raw;
            } else {
                error("Second arg cannot be added to a checksum"); // "Second arg cannot be added to a checksum"
            }
            break;
        case QBVAL_VECTOR:
            if (typeB == QBVAL_VECTOR) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x + b->vector->x;
                result.vector->y = a->vector->y + b->vector->y;
                result.vector->z = a->vector->z + b->vector->z;
            } else {
                error("Second arg cannot be added to a vector"); // "Second arg cannot be added to a vector"
            }
            break;
        case QBVAL_PAIR:
            if (typeB == QBVAL_PAIR) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x + b->pair->x;
                result.pair->y = a->pair->y + b->pair->y;
            } else {
                error("Second arg cannot be added to a pair"); // "Second arg cannot be added to a pair"
            }
            break;
        case QBVAL_STRING:
            if (typeB != QBVAL_STRING)
                error("Second arg cannot be added to a string"); // "Second arg cannot be added to a string"
            else if (!JoinStrings(a->string, b->string, QBVAL_STRING))
                error("Result of string addition too long"); // "Result of string addition too long"
            break;
        case QBVAL_INLINE_TEXT:
            if (typeB != QBVAL_INLINE_TEXT)
                error("Second arg cannot be added to a local-string"); // "Second arg cannot be added to a local-string"
            else if (!JoinStrings(a->string, b->string, QBVAL_INLINE_TEXT))
                error("Result of local-string addition too long"); // "Result of local-string addition too long"
            break;
        case QBVAL_STRUCT:
        case QBVAL_STRUCT_REF:
            if (IsStructure(typeB)) {
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                result.structure->AppendStructureIfAny(b->structure);
            } else {
                error("Structure add operator requires 2nd arg to be a structure"); // "Structure add operator requires 2nd arg to be a structure"
            }
            break;
        case QBVAL_ARRAY:
        case ESYMBOLTYPE_ARRAYPOINTER: {
            if (!IsArray(typeB)) {
                error("Array add operator requires 2nd arg to be an array as well"); // "Array add operator requires 2nd arg to be an array as well"
                break;
            }
            CArray* array = AllocScriptArray();
            array = array ? array->PrepareAll() : nullptr;
            SetResultType(QBVAL_ARRAY);
            result.array = array;
            uint32_t sizeA = a->array->m_extent;
            uint32_t sizeB = b->array->m_extent;
            if (sizeA == 0) {
                DuplicateArray(array, b->array);
            } else if (sizeB == 0) {
                DuplicateArray(array, a->array);
            } else {
                // (the elements are all taken to be of the first array's type)
                EQbValueKind type = a->array->m_kindTag;
                array->ConfigureSizeKind(sizeA + sizeB, type);
                CopyArrayElements(array->FindArrayValue(), a->array->FindArrayValue(), sizeA, type);
                CopyArrayElements(array->FindArrayValue() + sizeA, b->array->FindArrayValue(), sizeB, type);
            }
            break;
        }
        default:
            error("Addition not supported for this type"); // "Addition not supported for this type"
            break;
        }
        break;

    case OPERATOR_MINUS:
        switch (typeA) {
        case QBVAL_INTEGER:
            if (typeB == QBVAL_INTEGER)
                SetResultInteger(static_cast<uint32_t>(a->integer) - static_cast<uint32_t>(b->integer));
            else if (typeB == QBVAL_FLOAT)
                SetResultFloat(AsDouble(a->integer) - AsDouble(b->floatValue));
            else
                error("Second arg cannot be subtracted from an integer"); // "Second arg cannot be subtracted from an integer"
            break;
        case QBVAL_FLOAT:
            if (typeB == QBVAL_INTEGER)
                SetResultFloat(AsDouble(a->floatValue) - AsDouble(b->integer));
            else if (typeB == QBVAL_FLOAT)
                SetResultFloat(AsDouble(a->floatValue) - AsDouble(b->floatValue));
            else
                error("Second arg cannot be subtracted from a float"); // "Second arg cannot be subtracted from a float"
            break;
        case QBVAL_PAIR:
            if (typeB == QBVAL_PAIR) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x - b->pair->x;
                result.pair->y = a->pair->y - b->pair->y;
            } else {
                error("Second arg cannot be subtracted from a pair"); // "Second arg cannot be subtracted from a pair"
            }
            break;
        case QBVAL_VECTOR:
            if (typeB == QBVAL_VECTOR) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x - b->vector->x;
                result.vector->y = a->vector->y - b->vector->y;
                result.vector->z = a->vector->z - b->vector->z;
            } else {
                error("Second arg cannot be subtracted from a vector"); // "Second arg cannot be subtracted from a vector"
            }
            break;
        case QBVAL_STRUCT:
        case QBVAL_STRUCT_REF:
            if (typeB == QBVAL_NAME) {
                // Without the name (as a parameter or a flag).
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                result.structure->EraseModule(b->checksum);
                result.structure->RemoveFlag(b->checksum);
            } else if (IsArray(typeB)) {
                // Without each name in the array.
                CArray* names = b->array;
                if (names->m_kindTag != QBVAL_NAME && names->m_kindTag != QBVAL_NONE) {
                    // "Subtracting an array from a structure requires the array to be an array of names"
                    error("Subtracting an array from a structure requires the array to be an array of names");
                    break;
                }
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                for (uint32_t i = 0; i < names->m_extent; ++i) {
                    result.structure->EraseModule(names->FetchPart(i));
                    result.structure->RemoveFlag(names->FetchPart(i));
                }
            } else if (IsStructure(typeB)) {
                // Without each of the other structure's parameters (by name and type) and flags.
                CStruct* structure = NewResultStructure();
                structure->AppendStructureIfAny(a->structure);
                CStruct* other = b->structure;
                for (CComponent* each = other->StepToEntry(nullptr); each; each = other->StepToEntry(each)) {
                    if (each->name) {
                        result.structure->RemoveComponentOfType(each->name, each->KindTag());
                    } else if (each->KindTag() == QBVAL_NAME) {
                        result.structure->EraseModule(each->checksum);
                        result.structure->RemoveFlag(each->checksum);
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

    case OPERATOR_MULTIPLY:
        switch (typeA) {
        case QBVAL_INTEGER:
            switch (typeB) {
            case QBVAL_INTEGER:
                SetResultInteger(static_cast<uint32_t>(b->integer) * static_cast<uint32_t>(a->integer));
                break;
            case QBVAL_FLOAT:
                SetResultFloat(AsDouble(a->integer) * AsDouble(b->floatValue));
                break;
            case QBVAL_VECTOR: {
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(a->integer) * AsDouble(b->vector->x));
                result.vector->y = Rounded(AsDouble(a->integer) * AsDouble(b->vector->y));
                result.vector->z = Rounded(AsDouble(a->integer) * AsDouble(b->vector->z));
                break;
            }
            case QBVAL_PAIR: {
                CPair* pair = NewResultPair();
                pair->x = Rounded(AsDouble(a->integer) * AsDouble(b->pair->x));
                result.pair->y = Rounded(AsDouble(a->integer) * AsDouble(b->pair->y));
                break;
            }
            default:
                error("Second arg cannot be multiplied by an integer"); // "Second arg cannot be multiplied by an integer"
                break;
            }
            break;
        case QBVAL_FLOAT:
            switch (typeB) {
            case QBVAL_INTEGER:
                SetResultFloat(AsDouble(b->integer) * AsDouble(a->floatValue));
                break;
            case QBVAL_FLOAT:
                SetResultFloat(AsDouble(b->floatValue) * AsDouble(a->floatValue));
                break;
            case QBVAL_VECTOR: {
                CVector* vector = NewResultVector();
                vector->x = b->vector->x * a->floatValue;
                result.vector->y = b->vector->y * a->floatValue;
                result.vector->z = b->vector->z * a->floatValue;
                break;
            }
            case QBVAL_PAIR: {
                CPair* pair = NewResultPair();
                pair->x = b->pair->x * a->floatValue;
                result.pair->y = b->pair->y * a->floatValue;
                break;
            }
            default:
                error("Second arg cannot be multiplied by a float"); // "Second arg cannot be multiplied by a float"
                break;
            }
            break;
        case QBVAL_PAIR:
            if (typeB == QBVAL_INTEGER) {
                CPair* pair = NewResultPair();
                pair->x = Rounded(AsDouble(b->integer) * AsDouble(a->pair->x));
                result.pair->y = Rounded(AsDouble(b->integer) * AsDouble(a->pair->y));
            } else if (typeB == QBVAL_FLOAT) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x * b->floatValue;
                result.pair->y = a->pair->y * b->floatValue;
            } else {
                error("Pair cannot be multiplied by second arg"); // "Pair cannot be multiplied by second arg"
            }
            break;
        case QBVAL_VECTOR:
            if (typeB == QBVAL_INTEGER) {
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(b->integer) * AsDouble(a->vector->x));
                result.vector->y = Rounded(AsDouble(b->integer) * AsDouble(a->vector->y));
                result.vector->z = Rounded(AsDouble(b->integer) * AsDouble(a->vector->z));
            } else if (typeB == QBVAL_FLOAT) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x * b->floatValue;
                result.vector->y = a->vector->y * b->floatValue;
                result.vector->z = a->vector->z * b->floatValue;
            } else if (typeB == QBVAL_VECTOR) {
                // The cross product (each coordinate computed as the x87 does: in one go, then rounded).
                const CVector* u = a->vector;
                const CVector* v = b->vector;
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(v->z) * u->y - AsDouble(v->y) * u->z);
                result.vector->y = Rounded(AsDouble(v->x) * u->z - AsDouble(v->z) * u->x);
                result.vector->z = Rounded(AsDouble(v->y) * u->x - AsDouble(v->x) * u->y);
            } else {
                error("Vector cannot be multiplied by second arg"); // "Vector cannot be multiplied by second arg"
            }
            break;
        default:
            error("Multiplication not supported for this type"); // "Multiplication not supported for this type"
            break;
        }
        break;

    case OPERATOR_DIVIDE:
        if (typeB == QBVAL_INTEGER) {
            if (b->integer == 0) {
                error("Integer division by zero"); // "Integer division by zero"
                break;
            }
        } else if (typeB == QBVAL_FLOAT) {
            if (std::fabs(b->floatValue) < g_evalDivisorMinimum) {
                error("Float division by zero"); // "Float division by zero"
                break;
            }
        }
        switch (typeA) {
        case QBVAL_INTEGER:
            if (typeB == QBVAL_INTEGER)
                SetResultInteger(static_cast<uint32_t>(a->integer / b->integer));
            else if (typeB == QBVAL_FLOAT)
                SetResultFloat(AsDouble(a->integer) / AsDouble(b->floatValue));
            else
                error("Integer cannot be divided by second arg"); // "Integer cannot be divided by second arg"
            break;
        case QBVAL_FLOAT:
            if (typeB == QBVAL_INTEGER)
                SetResultFloat(AsDouble(a->floatValue) / AsDouble(b->integer));
            else if (typeB == QBVAL_FLOAT)
                SetResultFloat(AsDouble(a->floatValue) / AsDouble(b->floatValue));
            else
                error("Float cannot be divided by second arg"); // "Float cannot be divided by second arg"
            break;
        case QBVAL_PAIR:
            if (typeB == QBVAL_INTEGER) {
                CPair* pair = NewResultPair();
                pair->x = Rounded(AsDouble(a->pair->x) / AsDouble(b->integer));
                result.pair->y = Rounded(AsDouble(a->pair->y) / AsDouble(b->integer));
            } else if (typeB == QBVAL_FLOAT) {
                CPair* pair = NewResultPair();
                pair->x = a->pair->x / b->floatValue;
                result.pair->y = a->pair->y / b->floatValue;
            } else {
                error("Pair cannot be divided by second arg"); // "Pair cannot be divided by second arg"
            }
            break;
        case QBVAL_VECTOR:
            if (typeB == QBVAL_INTEGER) {
                CVector* vector = NewResultVector();
                vector->x = Rounded(AsDouble(a->vector->x) / AsDouble(b->integer));
                result.vector->y = Rounded(AsDouble(a->vector->y) / AsDouble(b->integer));
                result.vector->z = Rounded(AsDouble(a->vector->z) / AsDouble(b->integer));
            } else if (typeB == QBVAL_FLOAT) {
                CVector* vector = NewResultVector();
                vector->x = a->vector->x / b->floatValue;
                result.vector->y = a->vector->y / b->floatValue;
                result.vector->z = a->vector->z / b->floatValue;
            } else {
                error("Vector cannot be divided by second arg"); // "Vector cannot be divided by second arg"
            }
            break;
        default:
            error("Division not supported for this type"); // "Division not supported for this type"
            break;
        }
        break;

    case OPERATOR_DOT:
        switch (typeA) {
        case QBVAL_VECTOR:
            if (typeB == QBVAL_VECTOR) {
                const CVector* u = a->vector;
                const CVector* v = b->vector;
                SetResultFloat(AsDouble(v->z) * u->z + AsDouble(v->y) * u->y + AsDouble(v->x) * u->x);
            } else {
                error("Vector can only be dot producted with another vector"); // "Vector can only be dot producted with another vector"
            }
            break;
        case QBVAL_PAIR:
            if (typeB == QBVAL_PAIR) {
                const CPair* u = a->pair;
                const CPair* v = b->pair;
                SetResultFloat(AsDouble(v->y) * u->y + AsDouble(v->x) * u->x);
            } else {
                error("Pair can only be dot producted with another pair"); // "Pair can only be dot producted with another pair"
            }
            break;
        case QBVAL_STRUCT:
        case QBVAL_STRUCT_REF:
            // A structure's member: copied (a referenced structure's members are referred to).
            if (typeB == QBVAL_NAME) {
                if (CComponent* member = a->structure->FindLastComponent(b->checksum))
                    DuplicateComponent(&result, member, typeA == QBVAL_STRUCT_REF);
            } else {
                error("Structure dot operator requires 2nd arg to be a name"); // "Structure dot operator requires 2nd arg to be a name"
            }
            break;
        default:
            error("Dot product not supported for this type"); // "Dot product not supported for this type"
            break;
        }
        break;

    case OPERATOR_ARRAY_ELEMENT:
        if (!IsArray(typeA)) {
            error("Array element operator not supported for this type"); // "Array element operator not supported for this type"
        } else if (typeB != QBVAL_INTEGER) {
            error("[] index must be an integer"); // "[] index must be an integer"
        } else if (b->raw >= a->array->m_extent) {
            error("[] index out of range"); // "[] index out of range"
        } else {
            CopyArrayElementToComponent(a->array, b->raw, &result, typeA == ESYMBOLTYPE_ARRAYPOINTER);
            if ((result.typeAndFlag & 0xFE) == 0)
                error("Array type cannot be accessed using [] yet ..."); // "Array type cannot be accessed using [] yet ..."
        }
        break;

    default:
        Clear();
        m_fault = "Operator not supported"; // "Operator not supported"
        break;
    }

    // The result replaces the two values (also after an error, which has emptied the stacks).
    ReleaseComponent(a);
    ReleaseComponent(b);
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

using Scripts::CValueResolver;

struct ComponentAdapters {
    bool LessThan(Scripts::CComponent* other) { return Scripts::LessThan(reinterpret_cast<Scripts::CComponent*>(this), other); }
    bool GreaterThan(Scripts::CComponent* other)
    {
        return Scripts::GreaterThan(reinterpret_cast<Scripts::CComponent*>(this), other);
    }
    bool IsReference() { return Scripts::IsReference(reinterpret_cast<Scripts::CComponent*>(this)); }
    void MakeReference() { Scripts::MakeReference(reinterpret_cast<Scripts::CComponent*>(this)); }
    void MakeOwning() { Scripts::MakeOwning(reinterpret_cast<Scripts::CComponent*>(this)); }
    int FetchFlag() { return Scripts::FetchFlag(reinterpret_cast<Scripts::CComponent*>(this)); }
};

} // namespace
