// The nodes by name prefix (node_prefixes.h).

#include "engine/script/node_prefixes.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/key_map.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"

namespace level_nodes {

using script::Array;
using script::Struct;

static uint32_t g_node_names[1000];
static PrefixTables* g_node_prefixes;

namespace {

constexpr uint32_t kName = 0xA1DC81F9; // name
constexpr int kGrowBy = 100;           // indices

uint32_t* node_names() { return g_node_names; } // (room for 1000)

// The PrefixNodes stored under `prefix` (lists::KeyMap's lookup, inlined in the original).
PrefixNodes* get_find(lists::KeyMap* table, uint32_t prefix)
{
    return static_cast<PrefixNodes*>(table->fetch_entry(prefix));
}

} // namespace

PrefixTables*& node_prefixes() { return g_node_prefixes; }

// 0x0045BDA0
void PrefixNodes::append_one(uint16_t index)
{
    indices_[num_] = index;
    ++num_;
    if (num_ % kGrowBy == 0) {
        uint16_t* old = indices_;
        indices_ = static_cast<uint16_t*>(memory::alloc_block((num_ + kGrowBy) * sizeof(uint16_t)));
        for (int i = 0; i < num_; ++i)
            indices_[i] = old[i];
        memory::free_malloc(old);
    }
}

// 0x0045C5D0
PrefixTable::PrefixTable()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    slot_table_ = memory ? new (memory) lists::KeyMap(8) : nullptr;
}

// 0x0045CD30
PrefixTable::~PrefixTable()
{
    slot_table_->iter_bucket_ = -1;
    slot_table_->iter_item_ = nullptr;
    PrefixNodes* nodes = nullptr;
    if (slot_table_->step_iterator() && slot_table_->iter_item_)
        nodes = static_cast<PrefixNodes*>(slot_table_->iter_item_->value);
    while (nodes) {
        // (the next one is found before this one is taken out of the table)
        PrefixNodes* next = nullptr;
        if (slot_table_->step_iterator() && slot_table_->iter_item_)
            next = static_cast<PrefixNodes*>(slot_table_->iter_item_->value);
        uint32_t prefix = nodes->name_prefix_;
        if (nodes->indices_)
            memory::free_malloc(nodes->indices_);
        memory::Hub::get_pull()->dealloc(nodes);
        slot_table_->erase_entry(prefix);
        nodes = next;
    }
    if (lists::KeyMap* table = slot_table_) {
        table->~KeyMap();
        memory::Hub::get_pull()->dealloc(table);
    }
}

// 0x0045C650
void PrefixTable::append_one(uint32_t prefix, uint16_t index)
{
    if (PrefixNodes* nodes = get_find(slot_table_, prefix)) {
        nodes->append_one(index);
        return;
    }
    auto* nodes = static_cast<PrefixNodes*>(memory::alloc_zeroed(sizeof(PrefixNodes)));
    if (nodes) {
        nodes->name_prefix_ = 0;
        nodes->num_ = 0;
        nodes->indices_ = static_cast<uint16_t*>(memory::alloc_block(kGrowBy * sizeof(uint16_t)));
    }
    nodes->name_prefix_ = prefix; // (no check that the allocation worked)
    nodes->append_one(index);
    slot_table_->store_entry(prefix, nodes);
}

// 0x0045C240
void PrefixTable::compact()
{
    slot_table_->iter_bucket_ = -1;
    slot_table_->iter_item_ = nullptr;
    while (slot_table_->step_iterator() && slot_table_->iter_item_) {
        auto* nodes = static_cast<PrefixNodes*>(slot_table_->iter_item_->value);
        if (!nodes)
            break;
        uint16_t* old = nodes->indices_;
        nodes->indices_ = static_cast<uint16_t*>(memory::alloc_block(nodes->num_ * sizeof(uint16_t)));
        for (int i = 0; i < nodes->num_; ++i)
            nodes->indices_[i] = old[i];
        memory::free_malloc(old);
    }
}

// 0x0045C700
void PrefixTables::add_node_array(uint32_t name)
{
    // (The original first walks to the end of the list, and does nothing with it.)
    void* memory = memory::alloc_zeroed(sizeof(PrefixTable));
    PrefixTable* table = memory ? new (memory) PrefixTable : nullptr;
    table->following_ = first_ptr_;
    first_ptr_ = table;
    table->node_array_ = name;
    Array* nodes = script::fetch_list(name);
    uint32_t count = nodes->extent_;
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<Struct*>(nodes->fetch_part(i));
        uint32_t node_name = 0;
        node->read_hash_value(kName, &node_name, 0);
        const char* text = script::find_script_checksum_name(node_name);
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
                    table->append_one(checksums::hash_from_text(buffer), static_cast<uint16_t>(i));
                script::register_name_hash(checksums::hash_from_text(buffer), buffer);
                at[0] = '_';
                at[1] = '\0';
                script::register_name_hash(checksums::hash_from_text(buffer), buffer);
            }
        }
        if (digits) {
            *digits = '\0';
            uint32_t prefix = checksums::hash_from_text(buffer);
            table->append_one(prefix, static_cast<uint16_t>(i));
            script::register_name_hash(prefix, buffer);
        }
    }
    table->compact();
}

// 0x0045CDF0
void PrefixTables::remove_node_array(uint32_t name)
{
    PrefixTable* previous = nullptr;
    for (PrefixTable* table = first_ptr_; table;) {
        PrefixTable* next = table->following_;
        if (table->node_array_ == name) {
            if (previous)
                previous->following_ = next;
            else
                first_ptr_ = next;
            table->~PrefixTable();
            memory::Hub::get_pull()->dealloc(table);
        } else {
            previous = table;
        }
        table = next;
    }
}

// 0x0045BE90
uint32_t* PrefixTables::get_nodes(uint32_t prefix, uint16_t* count)
{
    uint32_t* names = node_names();
    int found = 0;
    for (PrefixTable* table = first_ptr_; table; table = table->following_) {
        PrefixNodes* nodes = get_find(table->slot_table_, prefix);
        if (!nodes)
            continue;
        auto n = static_cast<uint16_t>(nodes->num_);
        const uint16_t* indices = nodes->indices_;
        if (!n)
            continue;
        Array* array = script::fetch_list(table->node_array_);
        for (int i = 0; i < n; ++i) // (the buffer's size isn't checked)
            names[found++] = get_node_name(reinterpret_cast<Struct*>(array->fetch_part(indices[i])));
    }
    *count = static_cast<uint16_t>(found);
    return names;
}

// 0x0045C900
void add_node_array_prefixes(uint32_t name)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    if (!node_prefixes()) {
        auto* tables = static_cast<PrefixTables*>(memory::alloc_zeroed(sizeof(PrefixTables)));
        if (tables)
            tables->first_ptr_ = nullptr;
        node_prefixes() = tables;
    }
    node_prefixes()->add_node_array(name);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0045CE40
void remove_node_array_prefixes(uint32_t name) { node_prefixes()->remove_node_array(name); }

// 0x0045BF90
uint32_t* collect_nodes_by_prefix(uint32_t prefix, uint16_t* count)
{
    return node_prefixes()->get_nodes(prefix, count);
}

// 0x0045BFB0
uint32_t* collect_nodes_by_prefix(const char* prefix, uint16_t* count)
{
    char buffer[100];
    size_t length = std::strlen(prefix);
    if (length && prefix[length - 1] == '_') {
        std::memcpy(buffer, prefix, length + 1); // (unchecked: the original's too)
        buffer[length - 1] = '\0';
        prefix = buffer;
    }
    return node_prefixes()->get_nodes(checksums::hash_from_text(prefix), count);
}

} // namespace level_nodes
