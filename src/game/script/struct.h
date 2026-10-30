#pragma once

#include <cstdint>

// The script system's data: structures of named components (Script::CStruct - also every script function's
// parameters), and the global symbol table they can refer to.
//
// A component's name is the checksum of the name used in the script (0 for an unnamed one). A structure can
// include a global structure by an unnamed name component that names it: lookups search those too.
namespace Script {

class CArray;
class CStruct;
struct SStructScript;

// The types of components and symbols.
enum ESymbolType : uint8_t {
    ESYMBOLTYPE_NONE = 0,
    ESYMBOLTYPE_INTEGER = 1,
    ESYMBOLTYPE_FLOAT = 2,
    ESYMBOLTYPE_STRING = 3,
    ESYMBOLTYPE_LOCALSTRING = 4,
    ESYMBOLTYPE_PAIR = 5,
    ESYMBOLTYPE_VECTOR = 6,
    ESYMBOLTYPE_QSCRIPT = 7,
    ESYMBOLTYPE_CFUNCTION = 8,
    ESYMBOLTYPE_MEMBERFUNCTION = 9,
    ESYMBOLTYPE_STRUCTURE = 10,
    ESYMBOLTYPE_ARRAY = 12,
    ESYMBOLTYPE_NAME = 13,
    // A component referring to a structure / array it doesn't own (made by CopyComponent when asked to).
    ESYMBOLTYPE_STRUCTUREPOINTER = 20,
    ESYMBOLTYPE_ARRAYPOINTER = 21,
};

// A script array: `size` elements of one type. A single element is kept in place of the pointer.
class CArray {
public:
    CArray* Init();
    void Clear();
    // The elements (for one element, the value stored in the array itself).
    uint32_t* GetArrayPointer();
    // Make the array `size` zeroed elements of `type` (frees the old element block, not what it refers to).
    void SetSizeAndType(uint32_t size, ESymbolType type);
    // Only the type (the elements are left as they are).
    void SetType(ESymbolType type);
    // Grow the array to `size` elements, keeping the old ones (the new ones are zero; shrinking isn't supported).
    void Resize(uint32_t size);

    // The elements. Any 32-bit value (integer, checksum, structure, array...) by SetElement / GetElement.
    void SetElement(uint32_t index, uint32_t value);
    void SetFloat(uint32_t index, float value);
    uint32_t GetElement(uint32_t index);
    const char* GetString(uint32_t index);
    const char* GetLocalString(uint32_t index);
    // An integer array's elements are converted.
    float GetFloat(uint32_t index);

    uint8_t m_unknown0;
    ESymbolType m_type;
    uint16_t m_unknown2;
    uint32_t m_size;
    union {
        uint32_t* m_elements;
        uint32_t m_single;
    };
};
static_assert(sizeof(CArray) == 0xC, "layout must match the original");

// Pair and vector values are separate objects; the coordinates start at +4.
// Their constructors clear only byte 0 of the first word.
struct CPair {
    CPair* Init();

    uint32_t unknown0;
    float x;
    float y;
};
struct CVector {
    CVector* Init();

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

    ESymbolType Type() const { return static_cast<ESymbolType>(typeAndFlag >> 1); }
};
static_assert(sizeof(CComponent) == 0x10, "layout must match the original");

struct CSymbolTableEntry {
    CSymbolTableEntry* Init();

    uint8_t unknown0; // bit 0: in use
    ESymbolType type;
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
CSymbolTableEntry* LookUpSymbol(uint32_t name);
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
int GetInteger(uint32_t name);
int GetInteger(const char* name);
float GetFloat(uint32_t name);
float GetFloat(const char* name);
uint32_t GetSymbolValue(uint32_t name);
uint32_t GetSymbolValue(const char* name);
const char* GetString(uint32_t name);
const char* GetString(const char* name);
CStruct* GetStructure(uint32_t name);
CStruct* GetStructure(const char* name);
CArray* GetArray(uint32_t name);
CArray* GetArray(const char* name);

// Whether two components' values are equal (names standing for global symbols are looked up; integers and
// floats compare by value; structures and arrays only when they are the same one).
bool ComponentsEqual(CComponent* a, CComponent* b);

// Debug output of a structure's contents, indented by `indent` levels. Compiled out of this build (the empty
// function at 0x005480A0): the typed getters still call it on a miss when asked to assert.
inline void PrintContents(const CStruct* /*structure*/, int /*indent*/ = 0) {}

// Strings owned by components: copied to the script heap (or, while the permanent string cache is on, shared
// in its block).
char* CreateString(const char* string);
void FreeString(const char* string);
// While on, CreateString puts strings in the permanent block, sharing equal ones (startup scripts).
void BeginPermanentStrings();
// Off again; the lookup table is freed (the strings stay).
void EndPermanentStrings();
// The script string slots (128 characters each), for the SetScriptString / GetScriptString commands.
// `string` is used as a format, as in the original (a '%' in it is interpreted).
int SetScriptString(int index, const char* string);
char* GetScriptString(int index);

struct CComponent;
// Free whatever a component's value owns and make it empty (keeps its name and flag bit).
void CleanUpComponent(CComponent* component);
// Free an array's elements (whatever they own too) and empty it.
void CleanUpArray(CArray* array);
// Make `dest` a deep copy of `source` (with `reference`, structures and arrays are referred to, not copied).
void CopyComponent(CComponent* dest, CComponent* source, int reference);
// Make `dest` a deep copy of `source`.
void CopyArray(CArray* dest, CArray* source);
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
    CStruct* Init();
    // Initialise as a copy of `source` (the copy constructor).
    CStruct* InitCopy(CStruct* source);
    bool IsEmpty();
    // The next component after `component` (null: the first).
    CComponent* GetNextComponent(CComponent* component);
    // The component `name` - the last one there is, also in included structures.
    CComponent* FindLastComponent(uint32_t name);
    // The first component named `name`, or the flag `name`, also in included structures.
    CComponent* FindComponent(uint32_t name);
    // The value of the last component `name` of type `type` (integers are converted for floats; name components
    // naming a global symbol of the type count too), into `out` (4 bytes).
    bool Search(uint32_t name, uint32_t type, void* out);

    bool ContainsComponentNamed(uint32_t name);
    bool ContainsComponentNamed(const char* name);
    // Whether the bare name `flag` is among the components.
    bool ContainsFlag(uint32_t flag);
    bool ContainsFlag(const char* flag);
    // Whether `other` is this structure or one of its structure components (recursively).
    bool References(CStruct* other);
    // Whether every component of this structure is in `other` too (by name and value).
    bool IsSubsetOf(CStruct* other);

    // Remove (and free) every component.
    void Clear();
    // Remove every component named `name` (of type `type`).
    void RemoveComponent(uint32_t name);
    void RemoveComponent(const char* name);
    void RemoveComponentOfType(uint32_t name, uint8_t type);
    // Remove the bare name `flag`.
    void RemoveFlag(uint32_t flag);

    // Add `component` at the end, replacing any component of the same name and compatible type (integers and
    // floats replace each other; a bare name only replaces the same name).
    void AddComponent(CComponent* component);
    void AddString(uint32_t name, const char* value);
    void AddString(const char* name, const char* value);
    void AddLocalString(uint32_t name, const char* value);
    void AddString(uint32_t name, uint32_t type, const char* value); // string or local string
    void AddInteger(uint32_t name, int value);
    void AddInteger(const char* name, int value);
    void AddFloat(uint32_t name, float value);
    void AddFloat(const char* name, float value);
    void AddChecksum(uint32_t name, uint32_t value);
    void AddChecksum(const char* name, uint32_t value);
    void AddVector(uint32_t name, float x, float y, float z);
    void AddVector(const char* name, float x, float y, float z);
    void AddVector(uint32_t name, VectorValue value);
    void AddVector(const char* name, VectorValue value);
    void AddPair(uint32_t name, float x, float y);
    // Arrays and structures added by pointer (the structure takes them over).
    void AddArrayPointer(uint32_t name, CArray* value);
    void AddArrayPointer(const char* name, CArray* value);
    void AddStructurePointer(uint32_t name, CStruct* value);
    void AddStructurePointer(const char* name, CStruct* value);
    // A copy of `value`.
    void AddStructure(uint32_t name, CStruct* value);
    void AddStructure(const char* name, CStruct* value);
    void AddArray(uint32_t name, CArray* value);
    void AddArray(const char* name, CArray* value);
    // A compiled script, copied to the script heap.
    void AddScript(uint32_t name, const uint8_t* code, uint32_t size);
    // By type: integer, float, 11 structure pointer, 12 array pointer, 13 checksum (others are ignored).
    void AddComponentOfType(uint32_t name, uint32_t type, uint32_t value);

    // Copies of all `source`'s components, added.
    CStruct* AppendStructure(CStruct* source);
    void AppendStructureIfAny(CStruct* source);
    // Become a copy of `source`.
    CStruct* Assign(CStruct* source);
    // Copies of this structure's components added to `dest`, with included global structures expanded.
    void ExpandInto(CStruct* dest, int depth);

    // The typed getters: false if missing. With `assert` set, a miss also prints the structure (PrintContents -
    // compiled out of this build) before a compiled-out assert.
    bool GetString(uint32_t name, const char** out, int assert);
    bool GetString(const char* name, const char** out, int assert);
    bool GetInteger(uint32_t name, int* out, int assert);
    bool GetInteger(const char* name, int* out, int assert);
    bool GetFloat(uint32_t name, float* out, int assert);
    bool GetFloat(const char* name, float* out, int assert);
    bool GetPair(uint32_t name, float* x, float* y, int assert);
    bool GetPair(uint32_t name, CPair* out, bool assert);
    bool GetStructure(uint32_t name, CStruct** out, int assert);
    bool GetStructure(const char* name, CStruct** out, int assert);
    bool GetArray(uint32_t name, CArray** out, int assert);
    bool GetArray(const char* name, CArray** out, int assert);
    // A script component: its name and code into `out`.
    bool GetScript(uint32_t name, SStructScript* out, int assert);
    bool GetChecksum(uint32_t name, uint32_t* out, int assert);
    bool GetChecksum(const char* name, uint32_t* out, int assert);
    // The checksum `name`, or else the checksum of the string `name`.
    bool GetChecksumOrStringChecksum(uint32_t name, uint32_t* out, int assert);
    bool GetVector(uint32_t name, VectorValue* out, int assert);
    bool GetVector(const char* name, VectorValue* out, int assert);

    uint8_t m_unknown0;
    uint8_t m_unknown1;
    uint16_t m_unknown2;
    CComponent* m_head;
};
static_assert(sizeof(CStruct) == 8, "layout must match the original");

} // namespace Script
