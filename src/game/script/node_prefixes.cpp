// The nodes by name prefix (node_prefixes.h).

#include "game/script/node_prefixes.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/hashtable.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/struct.h"

namespace SkateScript {

using Script::CArray;
using Script::CStruct;

uint32_t g_nodeNames[1000];
PrefixTables* g_nodePrefixes;

namespace {

constexpr uint32_t kName = 0xA1DC81F9; // name
constexpr int kGrowBy = 100;           // indices

uint32_t* NodeNames() { return g_nodeNames; } // (room for 1000)

// The PrefixNodes stored under `prefix` (Lst::HashTable's lookup, inlined in the original).
PrefixNodes* Find(Lst::HashTable* table, uint32_t prefix) { return static_cast<PrefixNodes*>(table->GetItem(prefix)); }

} // namespace

PrefixTables*& NodePrefixes() { return g_nodePrefixes; }

// 0x0045BDA0
void PrefixNodes::Add(uint16_t index)
{
    mp_indices[m_count] = index;
    ++m_count;
    if (m_count % kGrowBy == 0) {
        uint16_t* old = mp_indices;
        mp_indices = static_cast<uint16_t*>(Mem::Malloc((m_count + kGrowBy) * sizeof(uint16_t)));
        for (int i = 0; i < m_count; ++i)
            mp_indices[i] = old[i];
        Mem::FreeMalloc(old);
    }
}

// 0x0045C5D0
PrefixTable* PrefixTable::Construct()
{
    void* memory = Mem::New(sizeof(Lst::HashTable));
    mp_table = memory ? new (memory) Lst::HashTable(8) : nullptr;
    m_nodeArray = 0;
    mp_next = nullptr;
    return this;
}

// 0x0045CD30
void PrefixTable::Destruct()
{
    mp_table->m_iterBucket = -1;
    mp_table->m_iterItem = nullptr;
    PrefixNodes* nodes = nullptr;
    if (mp_table->IterateNext() && mp_table->m_iterItem)
        nodes = static_cast<PrefixNodes*>(mp_table->m_iterItem->value);
    while (nodes) {
        // (the next one is found before this one is taken out of the table)
        PrefixNodes* next = nullptr;
        if (mp_table->IterateNext() && mp_table->m_iterItem)
            next = static_cast<PrefixNodes*>(mp_table->m_iterItem->value);
        uint32_t prefix = nodes->m_prefix;
        if (nodes->mp_indices)
            Mem::FreeMalloc(nodes->mp_indices);
        Mem::Manager::Get()->Free(nodes);
        mp_table->FlushItem(prefix);
        nodes = next;
    }
    if (Lst::HashTable* table = mp_table) {
        table->~HashTable();
        Mem::Manager::Get()->Free(table);
    }
}

// 0x0045C650
void PrefixTable::Add(uint32_t prefix, uint16_t index)
{
    if (PrefixNodes* nodes = Find(mp_table, prefix)) {
        nodes->Add(index);
        return;
    }
    auto* nodes = static_cast<PrefixNodes*>(Mem::Calloc(sizeof(PrefixNodes)));
    if (nodes) {
        nodes->m_prefix = 0;
        nodes->m_count = 0;
        nodes->mp_indices = static_cast<uint16_t*>(Mem::Malloc(kGrowBy * sizeof(uint16_t)));
    }
    nodes->m_prefix = prefix; // (no check that the allocation worked)
    nodes->Add(index);
    mp_table->PutItem(prefix, nodes);
}

// 0x0045C240
void PrefixTable::Compact()
{
    mp_table->m_iterBucket = -1;
    mp_table->m_iterItem = nullptr;
    while (mp_table->IterateNext() && mp_table->m_iterItem) {
        auto* nodes = static_cast<PrefixNodes*>(mp_table->m_iterItem->value);
        if (!nodes)
            break;
        uint16_t* old = nodes->mp_indices;
        nodes->mp_indices = static_cast<uint16_t*>(Mem::Malloc(nodes->m_count * sizeof(uint16_t)));
        for (int i = 0; i < nodes->m_count; ++i)
            nodes->mp_indices[i] = old[i];
        Mem::FreeMalloc(old);
    }
}

// 0x0045C700
void PrefixTables::AddNodeArray(uint32_t name)
{
    // (The original first walks to the end of the list, and does nothing with it.)
    void* memory = Mem::Calloc(sizeof(PrefixTable));
    PrefixTable* table = memory ? static_cast<PrefixTable*>(memory)->Construct() : nullptr;
    table->mp_next = mp_first;
    mp_first = table;
    table->m_nodeArray = name;
    CArray* nodes = Script::GetArray(name);
    uint32_t count = nodes->m_size;
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<CStruct*>(nodes->GetElement(i));
        uint32_t nodeName = 0;
        node->GetChecksum(kName, &nodeName, 0);
        const char* text = Script::FindScriptChecksumName(nodeName);
        if (!text || !*text)
            continue;
        // The name is copied as it is read (the copy isn't terminated until a prefix is cut off): at each '_', the
        // part before it is a prefix (unless it's "TRG"), and both it and it with the '_' become known names; a
        // name ending in digits has the part before them as a prefix too.
        char buffer[1000];
        char* digits = nullptr;
        for (const char* p = text; *p; ++p) {
            char* at = buffer + (p - text);
            *at = *p;
            char c = *p;
            if (c < '0' || c > '9')
                digits = nullptr;
            else if (!digits)
                digits = at;
            if (c == '_') {
                *at = '\0';
                if (_stricmp(buffer, "TRG") != 0)
                    table->Add(Crc::GenerateCRCFromString(buffer), static_cast<uint16_t>(i));
                Script::AddChecksumName(Crc::GenerateCRCFromString(buffer), buffer);
                at[0] = '_';
                at[1] = '\0';
                Script::AddChecksumName(Crc::GenerateCRCFromString(buffer), buffer);
            }
        }
        if (digits) {
            *digits = '\0';
            uint32_t prefix = Crc::GenerateCRCFromString(buffer);
            table->Add(prefix, static_cast<uint16_t>(i));
            Script::AddChecksumName(prefix, buffer);
        }
    }
    table->Compact();
}

// 0x0045CDF0
void PrefixTables::RemoveNodeArray(uint32_t name)
{
    PrefixTable* previous = nullptr;
    for (PrefixTable* table = mp_first; table;) {
        PrefixTable* next = table->mp_next;
        if (table->m_nodeArray == name) {
            if (previous)
                previous->mp_next = next;
            else
                mp_first = next;
            table->Destruct();
            Mem::Manager::Get()->Free(table);
        } else {
            previous = table;
        }
        table = next;
    }
}

// 0x0045BE90
uint32_t* PrefixTables::GetNodes(uint32_t prefix, uint16_t* count)
{
    uint32_t* names = NodeNames();
    int found = 0;
    for (PrefixTable* table = mp_first; table; table = table->mp_next) {
        PrefixNodes* nodes = Find(table->mp_table, prefix);
        if (!nodes)
            continue;
        uint16_t n = static_cast<uint16_t>(nodes->m_count);
        const uint16_t* indices = nodes->mp_indices;
        if (!n)
            continue;
        CArray* array = Script::GetArray(table->m_nodeArray);
        for (int i = 0; i < n; ++i) // (the buffer's size isn't checked)
            names[found++] = GetNodeName(reinterpret_cast<CStruct*>(array->GetElement(indices[i])));
    }
    *count = static_cast<uint16_t>(found);
    return names;
}

// 0x0045C900
void AddNodeArrayPrefixes(uint32_t name)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    if (!NodePrefixes()) {
        auto* tables = static_cast<PrefixTables*>(Mem::Calloc(sizeof(PrefixTables)));
        if (tables)
            tables->mp_first = nullptr;
        NodePrefixes() = tables;
    }
    NodePrefixes()->AddNodeArray(name);
    Mem::Manager::Get()->PopContext();
}

// 0x0045CE40
void RemoveNodeArrayPrefixes(uint32_t name) { NodePrefixes()->RemoveNodeArray(name); }

// 0x0045BF90
uint32_t* GetPrefixedNodes(uint32_t prefix, uint16_t* count) { return NodePrefixes()->GetNodes(prefix, count); }

// 0x0045BFB0
uint32_t* GetPrefixedNodes(const char* prefix, uint16_t* count)
{
    char buffer[100];
    size_t length = std::strlen(prefix);
    if (length && prefix[length - 1] == '_') {
        std::memcpy(buffer, prefix, length + 1); // (unchecked: the original's too)
        buffer[length - 1] = '\0';
        prefix = buffer;
    }
    return NodePrefixes()->GetNodes(Crc::GenerateCRCFromString(prefix), count);
}

} // namespace SkateScript
