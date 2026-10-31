// ListKit::ResolveMap's code (see lookup_table.h): 0x00557F10, 0x00561430, 0x005628B0, 0x00562970, 0x0055B990.

#include <cstdint>

#include "game/lists/lookup_table.h"

#include "game/core/mem.h"

namespace ListKit {

namespace {

// The item's deleting destructor (its vtable's first entry).
void DeleteItem(void* item)
{
    void** vtable = *static_cast<void***>(item);
    reinterpret_cast<void*(__thiscall*)(void*, uint8_t)>(vtable[0])(item, 1);
}

} // namespace

// 0x00557F10
void* ResolveMap::FetchEntryAt(const int* index, uint32_t* key)
{
    if (m_cursor && m_cursorIndex != *index) {
        if (m_cursorIndex < *index) {
            do {
                ++m_cursorIndex;
                m_cursor = m_cursor->next;
            } while (m_cursorIndex < *index);
        } else {
            m_cursor = nullptr;
        }
    }
    if (!m_cursor) {
        Elem* node = m_first;
        for (int i = *index; i > 0; --i)
            node = node->next;
        m_cursorIndex = *index;
        m_cursor = node;
    }
    if (key)
        *key = m_cursor->key;
    return m_cursor->item;
}

// 0x00561430
void* ResolveMap::FetchEntry(const uint32_t* key)
{
    m_cursor = nullptr;
    m_cursorIndex = 0;
    for (Elem* node = m_first; node; node = node->next) {
        if (node->key == *key) {
            m_cursor = node;
            return node->item;
        }
        ++m_cursorIndex;
    }
    return nullptr;
}

// 0x005628B0
bool ResolveMap::StoreEntry(const uint32_t* key, void* item)
{
    auto* node = static_cast<Elem*>(MemSys::Alloc(sizeof(Elem)));
    if (node) {
        node->item = nullptr;
        node->next = nullptr;
    }
    node->item = item; // (a failed allocation writes through null, as the original)
    node->key = *key;
    if (!m_first)
        m_first = node;
    else
        m_last->next = node;
    m_last = node;
    ++m_num;
    m_cursor = nullptr;
    return true;
}

// 0x00562970
void ResolveMap::EraseEntry(const uint32_t* key)
{
    m_cursor = nullptr;
    Elem* previous = nullptr;
    for (Elem* node = m_first; node; previous = node, node = node->next) {
        if (node->key != *key)
            continue;
        if (previous)
            previous->next = node->next;
        else
            m_first = node->next;
        if (m_last == node)
            m_last = previous;
        MemSys::Dealloc(node);
        --m_num;
        return;
    }
}

// 0x0055B990
void TableItemEraser::EmptyTableItems()
{
    for (int count = m_lookupTable->m_num; count > 0; --count) {
        int index = 0;
        uint32_t key;
        void* item = m_lookupTable->FetchEntryAt(&index, &key);
        m_lookupTable->EraseEntry(&key);
        if (item)
            DeleteItem(item);
    }
}

} // namespace Lst
