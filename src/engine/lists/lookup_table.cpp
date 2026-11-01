// lists::ResolveMap's code (see lookup_table.h): 0x00557F10, 0x00561430, 0x005628B0, 0x00562970, 0x0055B990.

#include <cstdint>

#include "engine/lists/lookup_table.h"

#include "engine/core/mem.h"

namespace lists {

namespace {} // namespace

// 0x00557F10
void* ResolveMap::fetch_entry_at(const int* index, uint32_t* key)
{
    if (cursor_ && cursor_index_ != *index) {
        if (cursor_index_ < *index) {
            do {
                ++cursor_index_;
                cursor_ = cursor_->next;
            } while (cursor_index_ < *index);
        } else {
            cursor_ = nullptr;
        }
    }
    if (!cursor_) {
        Elem* node = first_;
        for (int i = *index; i > 0; --i)
            node = node->next;
        cursor_index_ = *index;
        cursor_ = node;
    }
    if (key)
        *key = cursor_->key;
    return cursor_->item;
}

// 0x00561430
void* ResolveMap::fetch_entry(const uint32_t* key)
{
    cursor_ = nullptr;
    cursor_index_ = 0;
    for (Elem* node = first_; node; node = node->next) {
        if (node->key == *key) {
            cursor_ = node;
            return node->item;
        }
        ++cursor_index_;
    }
    return nullptr;
}

// 0x005628B0
bool ResolveMap::store_entry(const uint32_t* key, void* item)
{
    auto* node = static_cast<Elem*>(memory::alloc(sizeof(Elem)));
    if (node) {
        node->item = nullptr;
        node->next = nullptr;
    }
    node->item = item; // (a failed allocation writes through null, as the original)
    node->key = *key;
    if (!first_)
        first_ = node;
    else
        last_->next = node;
    last_ = node;
    ++num_;
    cursor_ = nullptr;
    return true;
}

// 0x00562970
void ResolveMap::erase_entry(const uint32_t* key)
{
    cursor_ = nullptr;
    Elem* previous = nullptr;
    for (Elem* node = first_; node; previous = node, node = node->next) {
        if (node->key != *key)
            continue;
        if (previous)
            previous->next = node->next;
        else
            first_ = node->next;
        if (last_ == node)
            last_ = previous;
        memory::dealloc(node);
        --num_;
        return;
    }
}

// 0x0055B990
void TableItemEraser::empty_table_items(void (*destroy)(void* item))
{
    for (int count = lookup_table_->num_; count > 0; --count) {
        const int index = 0;
        uint32_t key = 0;
        void* item = lookup_table_->fetch_entry_at(&index, &key);
        lookup_table_->erase_entry(&key);
        if (item)
            destroy(item);
    }
}

} // namespace lists
