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

namespace LevelNodes {

using Scripts::CArray;
using Scripts::CStruct;

uint32_t g_nodeNames[1000];
PrefixTables* g_nodePrefixes;

namespace {

constexpr uint32_t kName = 0xA1DC81F9; // name
constexpr int kGrowBy = 100;           // indices

uint32_t* NodeNames() { return g_nodeNames; } // (room for 1000)

// The PrefixNodes stored under `prefix` (ListKit::KeyMap's lookup, inlined in the original).
PrefixNodes* Find(ListKit::KeyMap* table, uint32_t prefix) { return static_cast<PrefixNodes*>(table->FetchEntry(prefix)); }

} // namespace

PrefixTables*& NodePrefixes() { return g_nodePrefixes; }

// 0x0045BDA0
void PrefixNodes::AppendOne(uint16_t index)
{
    mp_indices[m_num] = index;
    ++m_num;
    if (m_num % kGrowBy == 0) {
        uint16_t* old = mp_indices;
        mp_indices = static_cast<uint16_t*>(MemSys::AllocBlock((m_num + kGrowBy) * sizeof(uint16_t)));
        for (int i = 0; i < m_num; ++i)
            mp_indices[i] = old[i];
        MemSys::FreeMalloc(old);
    }
}

// 0x0045C5D0
PrefixTable* PrefixTable::MakeNew()
{
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    m_slotTable = memory ? new (memory) ListKit::KeyMap(8) : nullptr;
    m_nodeArray = 0;
    m_following = nullptr;
    return this;
}

// 0x0045CD30
void PrefixTable::Destruct()
{
    m_slotTable->m_iterBucket = -1;
    m_slotTable->m_iterItem = nullptr;
    PrefixNodes* nodes = nullptr;
    if (m_slotTable->StepIterator() && m_slotTable->m_iterItem)
        nodes = static_cast<PrefixNodes*>(m_slotTable->m_iterItem->value);
    while (nodes) {
        // (the next one is found before this one is taken out of the table)
        PrefixNodes* next = nullptr;
        if (m_slotTable->StepIterator() && m_slotTable->m_iterItem)
            next = static_cast<PrefixNodes*>(m_slotTable->m_iterItem->value);
        uint32_t prefix = nodes->m_namePrefix;
        if (nodes->mp_indices)
            MemSys::FreeMalloc(nodes->mp_indices);
        MemSys::Hub::Pull()->Dealloc(nodes);
        m_slotTable->EraseEntry(prefix);
        nodes = next;
    }
    if (ListKit::KeyMap* table = m_slotTable) {
        table->~KeyMap();
        MemSys::Hub::Pull()->Dealloc(table);
    }
}

// 0x0045C650
void PrefixTable::AppendOne(uint32_t prefix, uint16_t index)
{
    if (PrefixNodes* nodes = Find(m_slotTable, prefix)) {
        nodes->AppendOne(index);
        return;
    }
    auto* nodes = static_cast<PrefixNodes*>(MemSys::AllocZeroed(sizeof(PrefixNodes)));
    if (nodes) {
        nodes->m_namePrefix = 0;
        nodes->m_num = 0;
        nodes->mp_indices = static_cast<uint16_t*>(MemSys::AllocBlock(kGrowBy * sizeof(uint16_t)));
    }
    nodes->m_namePrefix = prefix; // (no check that the allocation worked)
    nodes->AppendOne(index);
    m_slotTable->StoreEntry(prefix, nodes);
}

// 0x0045C240
void PrefixTable::Compact()
{
    m_slotTable->m_iterBucket = -1;
    m_slotTable->m_iterItem = nullptr;
    while (m_slotTable->StepIterator() && m_slotTable->m_iterItem) {
        auto* nodes = static_cast<PrefixNodes*>(m_slotTable->m_iterItem->value);
        if (!nodes)
            break;
        uint16_t* old = nodes->mp_indices;
        nodes->mp_indices = static_cast<uint16_t*>(MemSys::AllocBlock(nodes->m_num * sizeof(uint16_t)));
        for (int i = 0; i < nodes->m_num; ++i)
            nodes->mp_indices[i] = old[i];
        MemSys::FreeMalloc(old);
    }
}

// 0x0045C700
void PrefixTables::AddNodeArray(uint32_t name)
{
    // (The original first walks to the end of the list, and does nothing with it.)
    void* memory = MemSys::AllocZeroed(sizeof(PrefixTable));
    PrefixTable* table = memory ? static_cast<PrefixTable*>(memory)->MakeNew() : nullptr;
    table->m_following = mp_first;
    mp_first = table;
    table->m_nodeArray = name;
    CArray* nodes = Scripts::FetchList(name);
    uint32_t count = nodes->m_extent;
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<CStruct*>(nodes->FetchPart(i));
        uint32_t nodeName = 0;
        node->ReadHashValue(kName, &nodeName, 0);
        const char* text = Scripts::FindScriptChecksumName(nodeName);
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
                    table->AppendOne(Checksums::HashFromText(buffer), static_cast<uint16_t>(i));
                Scripts::RegisterNameHash(Checksums::HashFromText(buffer), buffer);
                at[0] = '_';
                at[1] = '\0';
                Scripts::RegisterNameHash(Checksums::HashFromText(buffer), buffer);
            }
        }
        if (digits) {
            *digits = '\0';
            uint32_t prefix = Checksums::HashFromText(buffer);
            table->AppendOne(prefix, static_cast<uint16_t>(i));
            Scripts::RegisterNameHash(prefix, buffer);
        }
    }
    table->Compact();
}

// 0x0045CDF0
void PrefixTables::RemoveNodeArray(uint32_t name)
{
    PrefixTable* previous = nullptr;
    for (PrefixTable* table = mp_first; table;) {
        PrefixTable* next = table->m_following;
        if (table->m_nodeArray == name) {
            if (previous)
                previous->m_following = next;
            else
                mp_first = next;
            table->Destruct();
            MemSys::Hub::Pull()->Dealloc(table);
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
    for (PrefixTable* table = mp_first; table; table = table->m_following) {
        PrefixNodes* nodes = Find(table->m_slotTable, prefix);
        if (!nodes)
            continue;
        uint16_t n = static_cast<uint16_t>(nodes->m_num);
        const uint16_t* indices = nodes->mp_indices;
        if (!n)
            continue;
        CArray* array = Scripts::FetchList(table->m_nodeArray);
        for (int i = 0; i < n; ++i) // (the buffer's size isn't checked)
            names[found++] = GetNodeName(reinterpret_cast<CStruct*>(array->FetchPart(indices[i])));
    }
    *count = static_cast<uint16_t>(found);
    return names;
}

// 0x0045C900
void AddNodeArrayPrefixes(uint32_t name)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_scriptHeap);
    if (!NodePrefixes()) {
        auto* tables = static_cast<PrefixTables*>(MemSys::AllocZeroed(sizeof(PrefixTables)));
        if (tables)
            tables->mp_first = nullptr;
        NodePrefixes() = tables;
    }
    NodePrefixes()->AddNodeArray(name);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x0045CE40
void RemoveNodeArrayPrefixes(uint32_t name) { NodePrefixes()->RemoveNodeArray(name); }

// 0x0045BF90
uint32_t* CollectNodesByPrefix(uint32_t prefix, uint16_t* count) { return NodePrefixes()->GetNodes(prefix, count); }

// 0x0045BFB0
uint32_t* CollectNodesByPrefix(const char* prefix, uint16_t* count)
{
    char buffer[100];
    size_t length = std::strlen(prefix);
    if (length && prefix[length - 1] == '_') {
        std::memcpy(buffer, prefix, length + 1); // (unchecked: the original's too)
        buffer[length - 1] = '\0';
        prefix = buffer;
    }
    return NodePrefixes()->GetNodes(Checksums::HashFromText(prefix), count);
}

} // namespace LevelNodes
