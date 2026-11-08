// NOT ORIGINAL - the bigger node name hash (node_hash.h).

#include "retail_limits/node_hash.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "retail_limits/exe_image.h"
#include "retail_limits/limits.h"
#include "retail_limits/log.h"

namespace retail_limits::node_hash {

namespace {

constexpr uint32_t kClass = 0x12B4E660; // class
constexpr uint32_t kLink = 0xC953660E;  // link (a class= whose nodes aren't hashed)
constexpr uint32_t kName = 0xA1DC81F9;  // name
constexpr uint32_t kEmptyInlineSlot = 0xFFFF0000; // the original's empty slot: array 0, node -1
constexpr uint32_t kMask = kNodeHashSlots - 1;
static_assert((kNodeHashSlots & kMask) == 0, "a power of two");

// A slot: the node's array and its index in it (node -1: empty).
struct Slot {
    uint32_t array = 0;
    int32_t node = -1;
};

// (until use_exe_calls: nothing found, nothing named)
void* __fastcall no_element(void* /*array*/, void* /*edx*/, int /*index*/) { return nullptr; }
bool __fastcall no_checksum(void* /*node*/, void* /*edx*/, uint32_t /*key*/, uint32_t* /*out*/, int /*assert_found*/) { return false; }
void __cdecl no_prefixes(uint32_t /*name*/) {}

struct State {
    ExeCalls calls = {.get_element = &no_element, .get_checksum = &no_checksum, .remove_prefixes = &no_prefixes};
    std::vector<Slot> slots;
    Slot walk;
    const void* owner = nullptr; // the manager the table belongs to
    Fill fill = {.used = 0, .peak = 0, .left_out = 0};
    bool told_full = false;
    bool told_other = false;
};

State& state()
{
    static State state;
    return state;
}

ManagerLayout& layout(void* manager) { return *static_cast<ManagerLayout*>(manager); }

uint32_t element_count(const void* array) { return static_cast<const ArrayHead*>(array)->count; }

void* node_at(ManagerLayout& manager, const Slot& slot)
{
    return state().calls.get_element(manager.arrays.at(slot.array).nodes, nullptr, slot.node);
}

uint32_t name_of(void* node)
{
    uint32_t name = 0;
    state().calls.get_checksum(node, nullptr, kName, &name, 1);
    return name;
}

// The table of `manager` (made on first use; a manager other than the one it was made for is told once - the game has
// one node manager at a time).
std::vector<Slot>& slots_of(const void* manager)
{
    State& hash = state();
    if (hash.slots.empty()) {
        hash.slots.assign(kNodeHashSlots, Slot{});
        hash.owner = manager;
    }
    if (manager != hash.owner && !hash.told_other) {
        hash.told_other = true;
        log_line("node name hash: a second node manager at " + hex_word(std::bit_cast<uint32_t>(manager)) + " (the table is " +
                 hex_word(std::bit_cast<uint32_t>(hash.owner)) + "'s)");
    }
    return hash.slots;
}

void occupy(Slot& slot, uint32_t array, int32_t node)
{
    if (slot.node == -1) {
        State& hash = state();
        ++hash.fill.used;
        hash.fill.peak = std::max(hash.fill.peak, hash.fill.used);
    }
    slot = {.array = array, .node = node};
}

void vacate(Slot& slot)
{
    if (slot.node != -1) {
        --state().fill.used;
    }
    slot.node = -1;
}

// The first empty slot from `name`'s on; false when the table is full.
bool empty_slot_for(std::vector<Slot>& slots, uint32_t name, uint32_t* found)
{
    uint32_t slot = name & kMask;
    for (uint32_t tries = 0; tries < kNodeHashSlots; ++tries) {
        if (slots.at(slot).node == -1) {
            *found = slot;
            return true;
        }
        slot = (slot + 1) & kMask;
    }
    return false;
}

// The slot holding the node named `name` (false: none - an empty slot met or the whole table searched).
bool slot_of_name(ManagerLayout& manager, std::vector<Slot>& slots, uint32_t name, uint32_t* found)
{
    uint32_t slot = name & kMask;
    for (uint32_t tries = 0; tries < kNodeHashSlots && slots.at(slot).node != -1; ++tries) {
        if (name_of(node_at(manager, slots.at(slot))) == name) {
            *found = slot;
            return true;
        }
        slot = (slot + 1) & kMask;
    }
    return false;
}

void tell_full(uint32_t array, uint32_t index)
{
    State& hash = state();
    ++hash.fill.left_out;
    if (!hash.told_full) {
        hash.told_full = true;
        log_line("node name hash: full (" + std::to_string(kNodeHashSlots) + " slots): node " + std::to_string(index) + " of node array " +
                 std::to_string(array) + " and later ones are not hashed - the level has too many named nodes");
    }
}

} // namespace

void use_exe_calls(const ExeCalls& calls) { state().calls = calls; }

Fill fill() { return state().fill; }

void* __fastcall construct(void* manager, void* /*edx*/)
{
    ManagerLayout& mgr = layout(manager);
    mgr.array_count = 0;
    mgr.slots.fill(kEmptyInlineSlot);
    mgr.walk_node = -1;
    State& hash = state();
    if (hash.fill.peak != 0) {
        log_line("node name hash: the last level's nodes filled " + std::to_string(hash.fill.peak) + " of " + std::to_string(kNodeHashSlots) +
                 " slots at most (the original game's table: " + std::to_string(kOriginalNodeHashSlots) + ")" +
                 (hash.fill.left_out != 0 ? ", " + std::to_string(hash.fill.left_out) + " nodes left out" : ""));
    }
    hash.slots.assign(kNodeHashSlots, Slot{});
    hash.owner = manager;
    hash.walk = Slot{};
    hash.fill = {.used = 0, .peak = 0, .left_out = 0};
    hash.told_full = false;
    return manager;
}

void __fastcall hash_node(void* manager, void* /*edx*/, uint32_t array, uint32_t index, void* node)
{
    uint32_t node_class = 0;
    state().calls.get_checksum(node, nullptr, kClass, &node_class, 0);
    if (node_class == kLink) {
        return;
    }
    uint32_t name = 0;
    if (!state().calls.get_checksum(node, nullptr, kName, &name, 0)) {
        return;
    }
    std::vector<Slot>& slots = slots_of(manager);
    uint32_t slot = 0;
    if (!empty_slot_for(slots, name, &slot)) {
        tell_full(array, index);
        return;
    }
    occupy(slots.at(slot), array, static_cast<int32_t>(index));
}

void __fastcall unhash_node_array(void* manager, void* /*edx*/, uint32_t array)
{
    ManagerLayout& mgr = layout(manager);
    std::vector<Slot>& slots = slots_of(manager);
    for (Slot& slot : slots) {
        if (slot.node != -1 && slot.array == array) {
            vacate(slot);
        }
    }
    // (as the original: each node taken out and put back from its name's slot on - a node may land in a later slot
    // and be met again)
    for (uint32_t i = 0; i < kNodeHashSlots; ++i) {
        const Slot moved = slots.at(i);
        if (moved.node == -1) {
            continue;
        }
        vacate(slots.at(i));
        uint32_t to = 0;
        empty_slot_for(slots, name_of(node_at(mgr, moved)), &to); // (the slot just emptied at least)
        occupy(slots.at(to), moved.array, moved.node);
    }
}

void* __fastcall find_node(void* manager, void* /*edx*/, uint32_t name, uint32_t /*unused*/)
{
    ManagerLayout& mgr = layout(manager);
    std::vector<Slot>& slots = slots_of(manager);
    uint32_t slot = 0;
    return slot_of_name(mgr, slots, name, &slot) ? node_at(mgr, slots.at(slot)) : nullptr;
}

void __fastcall start_walk(void* manager, void* /*edx*/, uint32_t name)
{
    State& hash = state();
    hash.walk = {.array = 0, .node = 0};
    if (name == 0) {
        return;
    }
    ManagerLayout& mgr = layout(manager);
    std::vector<Slot>& slots = slots_of(manager);
    uint32_t slot = 0;
    // (a name that isn't hashed: the walk starts at the first node - the original read past the empty slot)
    if (slot_of_name(mgr, slots, name, &slot)) {
        hash.walk = slots.at(slot);
    }
}

void __fastcall end_walk(void* /*manager*/, void* /*edx*/) { state().walk.node = -1; }

void* __fastcall next_node(void* manager, void* /*edx*/)
{
    State& hash = state();
    if (hash.walk.node == -1) {
        return nullptr;
    }
    ManagerLayout& mgr = layout(manager);
    void* nodes = mgr.arrays.at(hash.walk.array).nodes;
    void* node = hash.calls.get_element(nodes, nullptr, hash.walk.node);
    ++hash.walk.node;
    if (static_cast<uint32_t>(hash.walk.node) >= element_count(nodes)) {
        ++hash.walk.array;
        hash.walk.node = 0;
        if (hash.walk.array >= mgr.array_count) {
            hash.walk.node = -1;
        }
    }
    return node;
}

bool __fastcall remove_node_array(void* manager, void* /*edx*/, uint32_t name)
{
    state().calls.remove_prefixes(name);
    ManagerLayout& mgr = layout(manager);
    uint32_t index = 0;
    while (index < mgr.array_count && mgr.arrays.at(index).name != name) {
        ++index;
    }
    if (index >= mgr.array_count) {
        return false;
    }
    unhash_node_array(manager, nullptr, index);
    --mgr.array_count;
    if (index != mgr.array_count) {
        mgr.arrays.at(index) = mgr.arrays.at(mgr.array_count);
        // (as the original: every slot naming the moved array, empty or not)
        for (Slot& slot : slots_of(manager)) {
            if (slot.array == mgr.array_count) {
                slot.array = index;
            }
        }
    }
    return true;
}

} // namespace retail_limits::node_hash
