#pragma once

#include "engine/core/mem.h"

// The game's std::list<T*> (MSVC 7.1's: an allocated sentinel node linked round with the elements, and a count; the
// nodes from the memory manager's current heap). One template for the lists the original kept as separate copies
// of the same code (0x005B4650 the constructor, 0x004964A0 insert, 0x004ECC30 erase): the pager's messages, the
// cutscene destroy list, the instance and spare object lists, the particle sets' systems, a model's geoms, the
// chain and anim blender lists. Their quirks are kept: a sentinel that couldn't be allocated is null (and the walks
// start from null), an insert without memory goes on through the null node.
namespace lists {

template <typename T>
struct PointerRing {
    struct Node {
        T* value;
        Node* next;
        Node* prev;
    };

    // The fields other code reads and sets.
    [[nodiscard]] Node* head() const { return head_; }
    void set_head(Node* value) { head_ = value; }
    int& size() { return size_; }
    [[nodiscard]] int size() const { return size_; }

private:
    Node* head_ = nullptr; // the sentinel
    int size_ = 0;

public:

    // 0x005B4650 (the constructor): no elements, a new sentinel linked to itself (null if out of memory).
    void make()
    {
        size_ = 0;
        auto* node = static_cast<Node*>(memory::alloc(sizeof(Node)));
        if (node) {
            node->next = node;
            node->prev = node;
        }
        head_ = node;
    }
    // The first node (the sentinel when empty; null without one).
    [[nodiscard]] Node* first() const { return head_ ? head_->next : nullptr; }
    // 0x004964A0: a new node holding `value` before `position`. Out of memory the original goes on through the null
    // node (and crashes) - kept.
    void insert(Node* position, T* value)
    {
        auto* node = static_cast<Node*>(memory::alloc(sizeof(Node)));
        if (node) {
            node->value = value;
            node->prev = position->prev;
            node->next = position;
        }
        ++size_;
        position->prev = node;
        node->prev->next = node; // (with no node, the original reads through null here)
    }
    void push_back(T* value) { insert(head_, value); }
    // 0x004ECC30: the nodes from `first` up to `last` unlinked and freed (the sentinel never); returns `last`.
    Node* erase(Node* first, Node* last)
    {
        while (first != last) {
            Node* node = first;
            first = node->next;
            if (node != head_) {
                node->prev->next = first;
                first->prev = node->prev;
                memory::dealloc(node);
                --size_;
            }
        }
        return first;
    }
    // Every element erased, then the sentinel freed (the destructor).
    void destroy()
    {
        erase(first(), head_);
        memory::dealloc(head_);
        head_ = nullptr;
    }
};

} // namespace lists
