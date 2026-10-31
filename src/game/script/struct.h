#pragma once

#include <cstdint>
#include "game/core/pool.h"

#include "game/core/pool.h"

// The script system's data: structures of named components (Scripts::CStruct - also every script function's
// parameters), and the global symbol table they can refer to.
//
// A component's name is the checksum of the name used in the script (0 for an unnamed one). A structure can
// include a global structure by an unnamed name component that names it: lookups search those too.
namespace Scripts {

class CArray;
class CStruct;
struct SParamScript;

// The types of components and symbols.
enum EQbValueKind : uint8_t {
    QBVAL_NONE = 0,
    QBVAL_INTEGER = 1,
    QBVAL_FLOAT = 2,
    QBVAL_STRING = 3,
    QBVAL_INLINE_TEXT = 4,
    QBVAL_PAIR = 5,
    QBVAL_VECTOR = 6,
    QBVAL_SCRIPT_BODY = 7,
    QBVAL_NATIVE_FUNC = 8,
    QBVAL_NATIVE_METHOD = 9,
    QBVAL_STRUCT = 10,
    QBVAL_ARRAY = 12,
    QBVAL_NAME = 13,
    // A component referring to a structure / array it doesn't own (made by DuplicateComponent when asked to).
    QBVAL_STRUCT_REF = 20,
    ESYMBOLTYPE_ARRAYPOINTER = 21,
};

// A script array: `size` elements of one type. A single element is kept in place of the pointer.
class CArray {
public:
    CArray* PrepareAll();
    void Clear();
    // The elements (for one element, the value stored in the array itself).
    uint32_t* FindArrayValue();
    // Make the array `size` zeroed elements of `type` (frees the old element block, not what it refers to).
    void ConfigureSizeKind(uint32_t size, EQbValueKind type);
    // Only the type (the elements are left as they are).
    void ChangeSpecies(EQbValueKind type);
    // Grow the array to `size` elements, keeping the old ones (the new ones are zero; shrinking isn't supported).
    void Rescale(uint32_t size);

    // The elements. Any 32-bit value (integer, checksum, structure, array...) by SetElement / FetchPart.
    void SetElement(uint32_t index, uint32_t value);
    void ChangeFloat(uint32_t index, float value);
    uint32_t FetchPart(uint32_t index);
    const char* FetchText(uint32_t index);
    const char* FindInlineText(uint32_t index);
    // An integer array's elements are converted.
    float FetchFloat(uint32_t index);

    uint8_t m_unknown0;
    EQbValueKind m_kindTag;
    uint16_t m_unknown2;
    uint32_t m_extent;
    union {
        uint32_t* m_elements;
        uint32_t m_single;
    };
};
static_assert(sizeof(CArray) == 0xC, "layout must match the original");

// Pair and vector values are separate objects; the coordinates start at +4.
// Their constructors clear only byte 0 of the first word.
struct CPair {
    CPair* PrepareAll();

    uint32_t unknown0;
    float x;
    float y;
};
struct CVector {
    CVector* PrepareAll();

    uint32_t unknown0;
    float x;
    float y;
    float z;
};
CPair* NewPair();
CVector* NewVector();

struct CComponent {
    uint8_t typeAndFlag; // the type in the top 7 bits
    uint8_t unknown1;
    uint16_t unknown2;
    uint32_t name;
    union {
        int32_t integer;
        float floatValue;
        const char* string;
        uint32_t checksum;
        CStruct* structure;
        CArray* array;
        CPair* pair;
        CVector* vector;
        uint32_t raw;
    };
    CComponent* next;

    EQbValueKind KindTag() const { return static_cast<EQbValueKind>(typeAndFlag >> 1); }
};
static_assert(sizeof(CComponent) == 0x10, "layout must match the original");

struct CSymbolTableEntry {
    CSymbolTableEntry* PrepareAll();

    uint8_t unknown0; // bit 0: in use
    EQbValueKind type;
    uint16_t unknown2;
    uint32_t name;
    uint32_t unknown8;
    union {
        int32_t integer;
        float floatValue;
        CStruct* structure;
        uint32_t checksum;
        uint32_t raw;
    };
    CSymbolTableEntry* next;
};

// 0x00478CF0: the global symbol `name` (following names that just name another symbol), or null.
CSymbolTableEntry* ResolveSymbol(uint32_t name);
// The global symbol `name` itself, or null.
CSymbolTableEntry* FindSymbol(uint32_t name);
// Make the (empty) symbol table: 0x1000 buckets, from the current heap.
void CreateSymbolTable();
// A new symbol `name` (from the symbol pool), linked in.
CSymbolTableEntry* CreateSymbol(uint32_t name);
// Link `entry` in / take it out of the table (the entry is kept).
void LinkSymbol(CSymbolTableEntry* entry);
void UnlinkSymbol(CSymbolTableEntry* entry);
// Iterate over every symbol: pass null for the first, then the previous one; null at the end.
CSymbolTableEntry* GetNextSymbol(CSymbolTableEntry* previous);

// The values of global symbols (by checksum or name). Integers and floats convert to each other; missing ones
// (or the wrong type) give 0 / 0.0 / null; GetSymbolValue gives the raw value of any type; strings give "".
int FetchIntValue(uint32_t name);
int FetchIntValue(const char* name);
float FetchFloat(uint32_t name);
float FetchFloat(const char* name);
uint32_t GetSymbolValue(uint32_t name);
uint32_t GetSymbolValue(const char* name);
const char* FetchText(uint32_t name);
const char* FetchText(const char* name);
CStruct* ReadSubStruct(uint32_t name);
CStruct* ReadSubStruct(const char* name);
CArray* FetchList(uint32_t name);
CArray* FetchList(const char* name);

// Whether two components' values are equal (names standing for global symbols are looked up; integers and
// floats compare by value; structures and arrays only when they are the same one).
bool ComponentsEqual(CComponent* a, CComponent* b);

// Debug output of a structure's contents, indented by `indent` levels. Compiled out of this build (the empty
// function at 0x005480A0): the typed getters still call it on a miss when asked to assert.
inline void DumpToLog(const CStruct* /*structure*/, int /*indent*/ = 0) {}

// Strings owned by components: copied to the script heap (or, while the permanent string cache is on, shared
// in its block).
char* SpawnText(const char* string);
void FreeString(const char* string);
// While on, SpawnText puts strings in the permanent block, sharing equal ones (startup scripts).
void BeginPermanentStrings();
// Off again; the lookup table is freed (the strings stay).
void EndPermanentStrings();
// The script string slots (128 characters each), for the SetScriptString / GetScriptString commands.
// `string` is used as a format, as in the original (a '%' in it is interpreted).
int SetScriptString(int index, const char* string);
char* GetScriptString(int index);

struct CComponent;
// Free whatever a component's value owns and make it empty (keeps its name and flag bit).
void ReleaseComponent(CComponent* component);
// Free an array's elements (whatever they own too) and empty it.
void ReleaseArrayStorage(CArray* array);
// Make `dest` a deep copy of `source` (with `reference`, structures and arrays are referred to, not copied).
void DuplicateComponent(CComponent* dest, CComponent* source, int reference);
// Make `dest` a deep copy of `source`.
void DuplicateArray(CArray* dest, CArray* source);
// Deep copies of `count` elements of `type` (null elements are skipped).
void CopyArrayElements(uint32_t* dest, const uint32_t* source, uint32_t count, uint32_t type);

// The pooled allocations of the script's value objects.
CStruct* NewStruct();
void DeleteStruct(CStruct* structure);
CArray* NewArray();
void DeleteArray(CArray* array);
void FreeComponent(CComponent* component);
// A new, empty component (from the component pool).
CComponent* NewComponent();
// Construct an empty component in place (leaves the second byte alone).
CComponent* ConstructComponent(CComponent* component);

// A 4-component vector as scripts' vectors are returned (w = 1).
struct VectorValue {
    float x, y, z, w;
};

class CStruct {
public:
    CStruct* PrepareAll();
    // Initialise as a copy of `source` (the copy constructor).
    CStruct* InitCopy(CStruct* source);
    bool IsVacant();
    // The next component after `component` (null: the first).
    CComponent* StepToEntry(CComponent* component);
    // The component `name` - the last one there is, also in included structures.
    CComponent* FindLastComponent(uint32_t name);
    // The first component named `name`, or the flag `name`, also in included structures.
    CComponent* FindComponent(uint32_t name);
    // The value of the last component `name` of type `type` (integers are converted for floats; name components
    // naming a global symbol of the type count too), into `out` (4 bytes).
    bool Hunt(uint32_t name, uint32_t type, void* out);

    bool HasEntryNamed(uint32_t name);
    bool HasEntryNamed(const char* name);
    // Whether the bare name `flag` is among the components.
    bool HasBareFlag(uint32_t flag);
    bool HasBareFlag(const char* flag);
    // Whether `other` is this structure or one of its structure components (recursively).
    bool RefTally(CStruct* other);
    // Whether every component of this structure is in `other` too (by name and value).
    bool IsSubsetOf(CStruct* other);

    // Remove (and free) every component.
    void Clear();
    // Remove every component named `name` (of type `type`).
    void EraseModule(uint32_t name);
    void EraseModule(const char* name);
    void RemoveComponentOfType(uint32_t name, uint8_t type);
    // Remove the bare name `flag`.
    void RemoveFlag(uint32_t flag);

    // Add `component` at the end, replacing any component of the same name and compatible type (integers and
    // floats replace each other; a bare name only replaces the same name).
    void AttachModule(CComponent* component);
    void AppendWord(uint32_t name, const char* value);
    void AppendWord(const char* name, const char* value);
    void AppendInlineText(uint32_t name, const char* value);
    void AppendWord(uint32_t name, uint32_t type, const char* value); // string or local string
    void PutInt(uint32_t name, int value);
    void PutInt(const char* name, int value);
    void PushFloat(uint32_t name, float value);
    void PushFloat(const char* name, float value);
    void PutNameKey(uint32_t name, uint32_t value);
    void PutNameKey(const char* name, uint32_t value);
    void PutVec3(uint32_t name, float x, float y, float z);
    void PutVec3(const char* name, float x, float y, float z);
    void PutVec3(uint32_t name, VectorValue value);
    void PutVec3(const char* name, VectorValue value);
    void InsertPair(uint32_t name, float x, float y);
    // Arrays and structures added by pointer (the structure takes them over).
    void AppendArrayRef(uint32_t name, CArray* value);
    void AppendArrayRef(const char* name, CArray* value);
    void AppendStructRef(uint32_t name, CStruct* value);
    void AppendStructRef(const char* name, CStruct* value);
    // A copy of `value`.
    void PutSubStruct(uint32_t name, CStruct* value);
    void PutSubStruct(const char* name, CStruct* value);
    void AppendArrayValue(uint32_t name, CArray* value);
    void AppendArrayValue(const char* name, CArray* value);
    // A compiled script, copied to the script heap.
    void AppendScript(uint32_t name, const uint8_t* code, uint32_t size);
    // By type: integer, float, 11 structure pointer, 12 array pointer, 13 checksum (others are ignored).
    void AddComponentOfType(uint32_t name, uint32_t type, uint32_t value);

    // Copies of all `source`'s components, added.
    CStruct* MergeStructure(CStruct* source);
    void AppendStructureIfAny(CStruct* source);
    // Become a copy of `source`.
    CStruct* Assign(CStruct* source);
    // Copies of this structure's components added to `dest`, with included global structures expanded.
    void InlineInto(CStruct* dest, int depth);

    // The typed getters: false if missing. With `assert` set, a miss also prints the structure (DumpToLog -
    // compiled out of this build) before a compiled-out assert.
    bool FetchText(uint32_t name, const char** out, int assert);
    bool FetchText(const char* name, const char** out, int assert);
    bool FetchIntValue(uint32_t name, int* out, int assert);
    bool FetchIntValue(const char* name, int* out, int assert);
    bool FetchFloat(uint32_t name, float* out, int assert);
    bool FetchFloat(const char* name, float* out, int assert);
    bool FetchPair(uint32_t name, float* x, float* y, int assert);
    bool FetchPair(uint32_t name, CPair* out, bool assert);
    bool ReadSubStruct(uint32_t name, CStruct** out, int assert);
    bool ReadSubStruct(const char* name, CStruct** out, int assert);
    bool FetchList(uint32_t name, CArray** out, int assert);
    bool FetchList(const char* name, CArray** out, int assert);
    // A script component: its name and code into `out`.
    bool FetchScript(uint32_t name, SParamScript* out, int assert);
    bool ReadHashValue(uint32_t name, uint32_t* out, int assert);
    bool ReadHashValue(const char* name, uint32_t* out, int assert);
    // The checksum `name`, or else the checksum of the string `name`.
    bool HashFromNameOrText(uint32_t name, uint32_t* out, int assert);
    bool FetchVec(uint32_t name, VectorValue* out, int assert);
    bool FetchVec(const char* name, VectorValue* out, int assert);

    uint8_t m_unknown0;
    uint8_t m_unknown1;
    uint16_t m_unknown2;
    CComponent* m_head;
};
static_assert(sizeof(CStruct) == 8, "layout must match the original");

// The CStruct pool pair (script/struct.cpp).
extern MemSys::PoolPair g_structPools;
extern MemSys::PoolPair g_arrayPools;
extern MemSys::PoolPair g_componentPools;
extern char* g_permanentStringsStart;
extern char* g_permanentStringsNext;
extern uint32_t g_permanentStringsSize;
extern uint32_t g_permanentStringCount;
extern MemSys::PoolPair g_symbolPools;
extern MemSys::PoolPair g_pairPools;
extern MemSys::PoolPair g_vectorPools;

struct PermanentString;
extern PermanentString* g_permanentStringTable;
} // namespace Script
