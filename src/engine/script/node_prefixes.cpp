// The nodes by name prefix (node_prefixes.h).

#include "engine/script/node_prefixes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/key_map.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include "engine/core/cursor.h"
#include "engine/core/no_destroy.h"

namespace level_nodes {

using script::Array;

namespace {
constexpr size_t kNodeNamesCapacity = 1000;
constexpr int kSlotTableBuckets = 8;
// NOT ORIGINAL grouping - node_prefixes.cpp's state (the original's globals). Made on first use, never destroyed
// (node_prefixes_state()).
constexpr size_t kNodeNamesSize = 1000;
struct NodePrefixesState {
    std::array<uint32_t, kNodeNamesSize> node_names_{};
    PrefixTables* node_prefixes_{};
};

NodePrefixesState& node_prefixes_state()
{
    static core::NoDestroy<NodePrefixesState> state;
    return state.get();
}

constexpr uint32_t kName = 0xA1DC81F9; // name
constexpr int kGrowBy = 100;           // indices

std::array<uint32_t, kNodeNamesCapacity>& node_names() { return node_prefixes_state().node_names_; } // (room for kNodeNamesCapacity)

// The PrefixNodes stored under `prefix` (lists::KeyMap's lookup, inlined in the original).
PrefixNodes* get_find(lists::KeyMap* table, uint32_t prefix)
{
    return static_cast<PrefixNodes*>(table->fetch_entry(prefix));
}

} // namespace

PrefixTables*& node_prefixes() { return node_prefixes_state().node_prefixes_; }

// 0x0045BDA0
void PrefixNodes::append_one(uint16_t index)
{
    // (the block holds the count rounded up to the next kGrowBy)
    core::at(std::span<uint16_t>(indices_, static_cast<std::size_t>((num_ / kGrowBy + 1) * kGrowBy)), num_) = index;
    ++num_;
    if (num_ % kGrowBy == 0) {
        uint16_t* old = indices_;
        indices_ = static_cast<uint16_t*>(memory::alloc_block((num_ + kGrowBy) * sizeof(uint16_t)));
        std::ranges::copy(std::span<const uint16_t>(old, static_cast<std::size_t>(num_)),
                          std::span<uint16_t>(indices_, static_cast<std::size_t>(num_ + kGrowBy)).begin());
        memory::free_malloc(old);
    }
}

// 0x0045C5D0
PrefixTable::PrefixTable()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    slot_table_ = memory ? memory::construct_in<lists::KeyMap>(memory, kSlotTableBuckets) : nullptr;
}

// 0x0045CD30
PrefixTable::~PrefixTable()
{
    slot_table_->set_iter_bucket(-1);
    slot_table_->set_iter_item(nullptr);
    PrefixNodes* nodes = nullptr;
    if (slot_table_->step_iterator() && slot_table_->iter_item())
        nodes = static_cast<PrefixNodes*>(slot_table_->iter_item()->value);
    while (nodes) {
        // (the next one is found before this one is taken out of the table)
        PrefixNodes* next = nullptr;
        if (slot_table_->step_iterator() && slot_table_->iter_item())
            next = static_cast<PrefixNodes*>(slot_table_->iter_item()->value);
        const uint32_t prefix = nodes->name_prefix();
        if (nodes->indices())
            memory::free_malloc(nodes->indices());
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
void PrefixTable::append_one(checksums::Name prefix_name, uint16_t index)
{
    const uint32_t prefix = prefix_name.value();
    if (PrefixNodes* nodes = get_find(slot_table_, prefix)) {
        nodes->append_one(index);
        return;
    }
    auto* nodes = static_cast<PrefixNodes*>(memory::alloc_zeroed(sizeof(PrefixNodes)));
    if (nodes) {
        nodes->set_name_prefix(0);
        nodes->set_num(0);
        nodes->set_indices(static_cast<uint16_t*>(memory::alloc_block(kGrowBy * sizeof(uint16_t))));
    }
    nodes->set_name_prefix(prefix); // (no check that the allocation worked)
    nodes->append_one(index);
    slot_table_->store_entry(prefix, nodes);
}

// 0x0045C240
void PrefixTable::compact()
{
    slot_table_->set_iter_bucket(-1);
    slot_table_->set_iter_item(nullptr);
    while (slot_table_->step_iterator() && slot_table_->iter_item()) {
        auto* nodes = static_cast<PrefixNodes*>(slot_table_->iter_item()->value);
        if (!nodes)
            break;
        uint16_t* old = nodes->indices();
        nodes->set_indices(static_cast<uint16_t*>(memory::alloc_block(nodes->num() * sizeof(uint16_t))));
        std::ranges::copy(std::span<const uint16_t>(old, static_cast<std::size_t>(nodes->num())), nodes->index_run().begin());
        memory::free_malloc(old);
    }
}

// 0x0045C700
void PrefixTables::add_node_array(uint32_t name)
{
    // (The original first walks to the end of the list, and does nothing with it.)
    void* memory = memory::alloc_zeroed(sizeof(PrefixTable));
    PrefixTable* table = memory ? memory::construct_in<PrefixTable>(memory) : nullptr;
    table->set_following(first_ptr_);
    first_ptr_ = table;
    table->set_node_array(name);
    Array* nodes = script::fetch_list(name);
    const uint32_t count = nodes->extent();
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_name = 0;
        node->read_hash_value(kName, &node_name, 0);
        const char* text = script::find_script_checksum_name(node_name);
        if (!text || !*text)
            continue;
        // The name is copied as it is read (the copy isn't terminated until a prefix is cut off): at each '_', the
        // part before it is a prefix (unless it's "TRG"), and both it and it with the '_' become known names; a
        // name ending in digits has the part before them as a prefix too.
        constexpr size_t kBufferSize = 1000;
        std::array<char, kBufferSize> buffer{};
        std::size_t digits = buffer.size(); // (where the trailing digits start; the size: none)
        std::size_t place = 0;
        for (core::CStringCursor p(text); *p; ++p, ++place) {
            char& at = core::at(buffer, place);
            at = *p;
            const char c = *p;
            if (c < '0' || c > '9')
                digits = buffer.size();
            else if (digits == buffer.size())
                digits = place;
            if (c == '_') {
                at = '\0';
                if (_stricmp(buffer.data(), "TRG") != 0)
                    table->append_one(checksums::Name{checksums::hash_from_text(buffer.data())}, static_cast<uint16_t>(i));
                script::register_name_hash(checksums::hash_from_text(buffer.data()), buffer.data());
                at = '_';
                core::at(buffer, place + 1) = '\0';
                script::register_name_hash(checksums::hash_from_text(buffer.data()), buffer.data());
            }
        }
        if (digits != buffer.size()) {
            core::at(buffer, digits) = '\0';
            const uint32_t prefix = checksums::hash_from_text(buffer.data());
            table->append_one(checksums::Name{prefix}, static_cast<uint16_t>(i));
            script::register_name_hash(prefix, buffer.data());
        }
    }
    table->compact();
}

// 0x0045CDF0
void PrefixTables::remove_node_array(uint32_t name)
{
    PrefixTable* previous = nullptr;
    for (PrefixTable* table = first_ptr_; table;) {
        PrefixTable* next = table->following();
        if (table->node_array() == name) {
            if (previous)
                previous->set_following(next);
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
std::span<const uint32_t> PrefixTables::get_nodes(uint32_t prefix)
{
    auto& names = node_names();
    int found = 0;
    for (const PrefixTable* table = first_ptr_; table; table = table->following()) {
        const PrefixNodes* nodes = get_find(table->slot_table(), prefix);
        if (!nodes)
            continue;
        auto n = static_cast<uint16_t>(nodes->num());
        const std::span<uint16_t> indices = nodes->index_run();
        if (!n)
            continue;
        Array* array = script::fetch_list(table->node_array());
        for (int i = 0; std::cmp_less(i, n); ++i) // (the buffer's size is checked)
            core::at(names, found++) = get_node_name(array->struct_at(core::at(indices, i)));
    }
    return std::span<const uint32_t>(names).first(static_cast<std::size_t>(found));
}

// 0x0045C900
void add_node_array_prefixes(uint32_t name)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    if (!node_prefixes()) {
        auto* tables = static_cast<PrefixTables*>(memory::alloc_zeroed(sizeof(PrefixTables)));
        if (tables)
            tables->set_first_ptr(nullptr);
        node_prefixes() = tables;
    }
    node_prefixes()->add_node_array(name);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0045CE40
void remove_node_array_prefixes(uint32_t name) { node_prefixes()->remove_node_array(name); }

// 0x0045BF90
std::span<const uint32_t> collect_nodes_by_prefix(uint32_t prefix)
{
    return node_prefixes()->get_nodes(prefix);
}

// 0x0045BFB0
std::span<const uint32_t> collect_nodes_by_prefix(const char* prefix)
{
    constexpr size_t kBufferSize = 100;
    std::array<char, kBufferSize> buffer{};
    const std::string_view prefix_text(prefix);
    const size_t length = prefix_text.size();
    if (length && core::at(prefix_text, length - 1) == '_') {
        core::copy_into(std::span<const char>(prefix, length + 1), std::span<char>(buffer).first(length + 1)); // (the original's unchecked)
        core::at(buffer, length - 1) = '\0';
        prefix = buffer.data();
    }
    return node_prefixes()->get_nodes(checksums::hash_from_text(prefix));
}

} // namespace level_nodes
