// level_nodes::CNodeManager: the level's node arrays and the hash from node names to nodes (node_manager.h).

#include "engine/script/node_manager.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "engine/assets/resource_entry.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/memory_sizes.h"
#include "engine/objects/path_manager.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include "engine/core/no_destroy.h"

namespace level_nodes {

memory::HeapUnique<NodeManager>& node_manager_instance()
{
    static core::NoDestroy<memory::HeapUnique<level_nodes::NodeManager>> node_manager;
    return node_manager.get();
}

using script::Array;
using script::Struct;

namespace {
// Node names logged as missing, at most.
constexpr int kLoggedNodeCount = 32;

constexpr uint32_t kName = 0xA1DC81F9;     // name
constexpr uint32_t kLinks = 0x2E7D5EE7;    // links
constexpr uint32_t kPosition = 0xB9D31B0A; // position
constexpr uint32_t kPos = 0x7F261953;      // pos

memory::HeapUnique<NodeManager>& sole() { return level_nodes::node_manager_instance(); }
uint32_t& ref_tally()
{
    static uint32_t node_manager_references{};
    return node_manager_references;
}

// NOT ORIGINAL - a node asked for that must exist (the release build's assertion did nothing) and doesn't: logged, the
// first time for each name. Many callers then use the null as a node and crash (e.g. IsAlive's 0x005A3D10 -> a crash
// in CStruct::read_hash_value: a converted level's script naming a node its NodeArray hasn't).
void log_missing_node(uint32_t name)
{
    static std::array<uint32_t, kLoggedNodeCount> logged;
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (core::at(logged, i) == name)
            return;
    if (count < kLoggedNodeCount)
        core::at(logged, count++) = name;
    const char* text = script::look_up_hash_name(name);
    get_log(
        "nodes: no node named 0x%08X%s%s%s in the level's node arrays - the caller expected one (it may crash on it)",
        name, text ? " (" : "", text ? text : "", text ? ")" : "");
}

constexpr uint32_t kClass = 0x12B4E660; // class
constexpr uint32_t kLink = 0xC953660E;  // link (a class= whose nodes aren't hashed)

// NOT ORIGINAL grouping - node_manager.cpp's state (the original's globals). Made on first use, never destroyed
// (node_array_hooks()).
struct NodeArrayHooks {
    // NOT ORIGINAL - the level editor's overlay array (see node_manager.h).
    uint32_t editor_array_{};
    NodeArrayRemovedHook removed_hook_{};
    NodeArrayAddedHook added_hook_{};
};

NodeArrayHooks& node_array_hooks()
{
    static core::NoDestroy<NodeArrayHooks> state;
    return state.get();
}

// NOT ORIGINAL - the name hash, bigger than the original's (memory::sizes::node_hash_slots): the game's node manager (the one
// create_node_manager made) keeps its slots here, node indices in 32 bits (a node array may hold more than 32,767), its
// walk too; its own slots_ / walk_ are left unused. Any other manager (the self-tests' own) uses its slots_ as the
// original does.
struct BigSlot {
    uint32_t element_list_;
    int32_t list_link_; // -1: empty
};
// NOT ORIGINAL grouping - the big node arrays: their slots, the walk and its owner (the original's globals). Made on
// first use, never destroyed (big_node_arrays()).
struct BigNodeArrays {
    std::vector<BigSlot> big_slots_{};
    BigSlot big_walk_ = {.element_list_ = 0, .list_link_ = -1};
    const NodeManager* big_owner_{};
};

BigNodeArrays& big_node_arrays()
{
    static core::NoDestroy<BigNodeArrays> state;
    return state.get();
}

bool is_big(const NodeManager* m) { return m == big_node_arrays().big_owner_ && !big_node_arrays().big_slots_.empty(); }

// The slots of a manager, whichever kind.
class Slots {
public:
    explicit Slots(NodeManager* m)
        : small_(m->slots()), big_(is_big(m) ? std::span<BigSlot>(big_node_arrays().big_slots_) : std::span<BigSlot>()),
          skip_bits_(!big_.empty() ? static_cast<uint32_t>(big_node_arrays().big_slots_.size()) - 1 : NodeManager::kSlots - 1)
    {}
    [[nodiscard]] uint32_t get_mask() const { return skip_bits_; }
    [[nodiscard]] bool vacant(uint32_t i) const { return elem(i) == -1; }
    [[nodiscard]] int elem(uint32_t i) const
    {
        return !big_.empty() ? core::at(big_, i).list_link_ : core::at(small_, i).list_link_;
    }
    [[nodiscard]] uint32_t get_array(uint32_t i) const
    {
        return !big_.empty() ? core::at(big_, i).element_list_ : core::at(small_, i).element_list_;
    }
    void assign_value(uint32_t i, uint32_t array, int node)
    {
        if (!big_.empty())
            core::at(big_, i) = {.element_list_ = array, .list_link_ = node};
        else
            core::at(small_, i) = {.element_list_ = static_cast<uint16_t>(array), .list_link_ = static_cast<int16_t>(node)};
    }
    void get_clear(uint32_t i) { assign_value(i, get_array(i), -1); }

private:
    std::span<NodeSlot> small_;
    std::span<BigSlot> big_;
    uint32_t skip_bits_;
};

// The walk's place, whichever kind (as the slots).
struct Walk {
    explicit Walk(NodeManager* m) : big_(is_big(m)), small(&m->walk()) {}
    [[nodiscard]] int elem() const { return big_ ? big_node_arrays().big_walk_.list_link_ : small->list_link_; }
    [[nodiscard]] uint32_t get_array() const { return big_ ? big_node_arrays().big_walk_.element_list_ : small->element_list_; }
    void assign_value(uint32_t array, int node)
    {
        if (big_)
            big_node_arrays().big_walk_ = {.element_list_ = array, .list_link_ = node};
        else
            *small = {.element_list_ = static_cast<uint16_t>(array), .list_link_ = static_cast<int16_t>(node)};
    }
    // The fields other code reads and sets.
    [[nodiscard]] bool big() const { return big_; }

private:
    bool big_;
    NodeSlot* small; // (the manager's, while the walk lives)
};

} // namespace

void set_editor_node_array(uint32_t name) { node_array_hooks().editor_array_ = name; }
uint32_t editor_node_array() { return node_array_hooks().editor_array_; }
void set_node_array_removed_hook(NodeArrayRemovedHook hook) { node_array_hooks().removed_hook_ = hook; }
void set_node_array_added_hook(NodeArrayAddedHook hook) { node_array_hooks().added_hook_ = hook; }

// 0x0045CE60
void NodeManager::hash_node(uint32_t array, uint32_t index, Struct* node)
{
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class == kLink)
        return;
    uint32_t name = 0;
    if (!node->read_hash_value(kName, &name, 0))
        return;
    Slots slots(this);
    uint32_t slot = name & slots.get_mask();
    while (!slots.vacant(slot)) {
        slot = (slot + 1) & slots.get_mask();
        // NOT ORIGINAL: a full hash - the original went round it for ever (a level of more named nodes than slots) -
        // logged, the node left out.
        if (slot == (name & slots.get_mask())) {
            static bool logged;
            if (!logged)
                get_log("nodes: the node name hash is full (%u slots - [memory] node_hash_slots): node %u of array %u "
                        "and later ones not hashed",
                        slots.get_mask() + 1, index, array);
            logged = true;
            return;
        }
    }
    slots.assign_value(slot, array, static_cast<int>(index));
}

// 0x0045CEF0
void NodeManager::unhash_node_array(uint32_t array)
{
    Slots slots(this);
    for (uint32_t i = 0; i <= slots.get_mask(); ++i) {
        if (!slots.vacant(i) && slots.get_array(i) == array)
            slots.get_clear(i);
    }
    // (A node may land in a later slot and be met - and moved - again.)
    for (uint32_t i = 0; i <= slots.get_mask(); ++i) {
        if (slots.vacant(i))
            continue;
        const uint32_t moved_array = slots.get_array(i);
        const int moved_node = slots.elem(i);
        slots.get_clear(i);
        auto* node = core::at(arrays_, moved_array).node_entries_->struct_at(moved_node);
        uint32_t name = 0;
        node->read_hash_value(kName, &name, 1);
        uint32_t to = name & slots.get_mask();
        while (!slots.vacant(to))
            to = (to + 1) & slots.get_mask();
        slots.assign_value(to, moved_array, moved_node);
    }
}

// 0x0045CFC0
Struct* NodeManager::fetch_vertex(uint32_t name, bool assert)
{
    const Slots slots(this);
    // (NOT ORIGINAL: at most every slot once - a full hash without the name went round for ever)
    uint32_t tries = slots.get_mask() + 1;
    for (uint32_t slot = name & slots.get_mask(); tries-- && !slots.vacant(slot);
         slot = (slot + 1) & slots.get_mask()) {
        auto* node =
            core::at(arrays_, slots.get_array(slot)).node_entries_->struct_at(slots.elem(slot));
        uint32_t node_name = 0;
        node->read_hash_value(kName, &node_name, 1);
        if (node_name == name)
            return node;
    }
    if (assert)
        log_missing_node(name);
    return nullptr;
}

// 0x0045D370
NodeManager::NodeManager()
{
    for (NodeSlot& slot : slots_)
        slot = {.element_list_ = 0, .list_link_ = -1};
    walk_.list_link_ = -1;
}

// 0x0045D050
void NodeManager::start_walk(uint32_t name)
{
    Walk walk(this);
    walk.assign_value(0, 0);
    if (!name)
        return;
    const Slots slots(this);
    uint32_t slot = name & slots.get_mask();
    for (;;) {
        auto* node =
            core::at(arrays_, slots.get_array(slot)).node_entries_->struct_at(slots.elem(slot));
        uint32_t node_name = 0;
        node->read_hash_value(kName, &node_name, 1);
        if (node_name == name)
            break;
        slot = (slot + 1) & slots.get_mask();
    }
    walk.assign_value(slots.get_array(slot), slots.elem(slot));
}

// 0x0045D110
void NodeManager::end_walk()
{
    Walk walk(this);
    walk.assign_value(walk.get_array(), -1);
}

// 0x0045D3F0
Struct* NodeManager::get_next_node()
{
    Walk walk(this);
    if (walk.elem() == -1)
        return nullptr;
    uint32_t array = walk.get_array();
    Array* nodes = core::at(arrays_, array).node_entries_;
    auto* node = nodes->struct_at(walk.elem());
    int next = walk.elem() + 1;
    if (!walk.big())
        next = static_cast<int16_t>(next); // (the original's 16-bit count)
    if (next >= ints::as_signed(nodes->extent())) {
        ++array;
        next = 0;
        if (array >= num_)
            next = -1;
    }
    walk.assign_value(array, next);
    return node;
}

// 0x0045D120
Array* NodeManager::get_last_node_array()
{
    // NOT ORIGINAL: the level editor's overlay array (set_editor_node_array) is passed over - callers mean the level's.
    uint32_t i = num_;
    while (i && node_array_hooks().editor_array_ && core::at(arrays_, i - 1).label_ == node_array_hooks().editor_array_)
        --i;
    return i ? core::at(arrays_, i - 1).node_entries_ : nullptr;
}

// 0x0045D150
uint32_t NodeManager::get_last_node_array_name()
{
    // NOT ORIGINAL: as get_last_node_array, the editor's overlay array passed over.
    uint32_t i = num_;
    while (i && node_array_hooks().editor_array_ && core::at(arrays_, i - 1).label_ == node_array_hooks().editor_array_)
        --i;
    return i ? core::at(arrays_, i - 1).label_ : 0;
}

// 0x0045D140
void NodeManager::set_owner(assets::NodeTableResource* owner) { owner_ptr_ = owner; }

// 0x0045D470
script::Array* NodeManager::get_owner_nodes() { return owner_ptr_->nodes(); }

// 0x0045D270
bool NodeManager::has_node_array(uint32_t name)
{
    for (uint32_t i = 0; i < num_; ++i) {
        if (core::at(arrays_, i).label_ == name)
            return true;
    }
    return false;
}

// 0x0045D170
bool NodeManager::hash_node_in(checksums::Name array, uint32_t index, script::Struct* node)
{
    const uint32_t name = array.value();
    for (uint32_t i = 0; i < num_; ++i) {
        if (core::at(arrays_, i).label_ == name) {
            hash_node(i, index, node);
            return true;
        }
    }
    return false;
}

// 0x0045D3A0
void NodeManager::hash_nodes(uint32_t index)
{
    Array* nodes = core::at(arrays_, index).node_entries_;
    for (uint32_t i = 0; i < nodes->extent(); ++i)
        hash_node(index, i, nodes->struct_at(i));
}

// 0x0045D480
void NodeManager::resolve_links(Array* nodes)
{
    const uint32_t count = nodes->extent();
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = nodes->struct_at(i);
        Array* links = nullptr;
        if (!node->fetch_list(kLinks, &links, 0) || !links->extent())
            continue;
        const uint32_t link_count = links->extent();
        for (uint32_t j = 0; j < link_count; ++j) {
            auto* linked = nodes->struct_at(links->fetch_part(j));
            links->set_element(j, get_node_name(linked));
        }
        links->change_species(script::kQbvalName);
    }
}

// 0x0045D520
bool NodeManager::add_node_array(uint32_t name, bool flag)
{
    if (has_node_array(name))
        return false;
    Array* nodes = script::fetch_list(name);
    core::at(arrays_, num_).label_ = name;
    core::at(arrays_, num_).node_entries_ = nodes;
    core::at(arrays_, num_).flag_ = flag;
    ++num_;
    hash_nodes(num_ - 1);
    add_node_array_prefixes(name);
    const int count = static_cast<int>(nodes->extent());
    for (int i = 0; i < count; ++i) {
        auto* node = nodes->struct_at(i);
        script::VectorValue position = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        if (node->fetch_vec(kPosition, &position, 0)) {
            node->erase_module(kPosition);
            node->put_vec3(checksums::Name{kPos}, {.x = position.x, .y = position.y, .z = -position.z});
        }
    }
    resolve_links(nodes);
    if (flag) {
        objects::PathManager::sole()->number_paths();
    }
    // NOT ORIGINAL: the level editor's edited copy (see node_manager.h).
    if (node_array_hooks().added_hook_)
        node_array_hooks().added_hook_(name);
    return true;
}

// 0x0045D1C0
bool NodeManager::remove_node_array(uint32_t name)
{
    remove_node_array_prefixes(name);
    uint32_t index = 0;
    while (index < num_ && core::at(arrays_, index).label_ != name)
        ++index;
    if (index >= num_)
        return false;
    unhash_node_array(index);
    --num_;
    if (index != num_) {
        core::at(arrays_, index) = core::at(arrays_, num_);
        Slots slots(this);
        for (uint32_t i = 0; i <= slots.get_mask(); ++i) {
            if (slots.get_array(i) == num_)
                slots.assign_value(i, index, slots.elem(i));
        }
    }
    // NOT ORIGINAL: the level editor's overlay array leaves with the level it was added after.
    if (node_array_hooks().removed_hook_)
        node_array_hooks().removed_hook_(name);
    return true;
}

// 0x0045D670
bool NodeManager::remove_node_array_of_file(const char* path) { return remove_node_array(node_array_checksum(path)); }

// 0x0045D2A0
bool make_node_array_name(const char* path, std::span<char> out)
{
    // The file's name without its folders (a path without one: the original walked back before the path's start;
    // here the whole path is the name), cut at its first '.', then "_nodearray".
    const std::string_view full(path);
    const std::size_t slash = full.find_last_of("\\/");
    const std::string_view base = slash == std::string_view::npos ? full : full.substr(slash + 1);
    const std::string_view stem = base.substr(0, base.find('.'));
    constexpr std::string_view kSuffix = "_nodearray";
    core::copy_into(std::span<const char>(stem.data(), stem.size()), out.first(stem.size()));
    const std::span<char> tail = out.subspan(stem.size(), kSuffix.size() + 1); // (the terminator too)
    core::copy_into(std::span<const char>(kSuffix.data(), kSuffix.size()), tail.first(kSuffix.size()));
    core::at(tail, kSuffix.size()) = '\0';
    return true;
}

// 0x0045D310
uint32_t node_array_checksum(const char* path)
{
    constexpr size_t kNameSize = 256;
    std::array<char, kNameSize> name = {};
    make_node_array_name(path, name);
    return checksums::hash_from_text(name.data());
}

// NOT ORIGINAL (see BigSlot)
void use_big_name_hash(NodeManager* manager, uint32_t slots)
{
    big_node_arrays().big_owner_ = manager;
    std::vector<BigSlot>().swap(big_node_arrays().big_slots_);
    if (manager && slots)
        big_node_arrays().big_slots_.assign(slots, BigSlot{.element_list_ = 0, .list_link_ = -1});
    big_node_arrays().big_walk_ = {.element_list_ = 0, .list_link_ = -1};
}

// 0x0045D690
NodeManager* create_node_manager(bool create)
{
    if (!sole() && create) {
        void* memory = memory::alloc_zeroed(sizeof(NodeManager), nullptr, 1);
        sole() = memory::HeapUnique<NodeManager>(memory ? memory::construct_in<NodeManager>(memory) : nullptr);
        // NOT ORIGINAL: its name hash bigger than the original's (memory::sizes::node_hash_slots - see BigSlot).
        if (sole() && memory::sizes::node_hash_slots() > static_cast<uint32_t>(NodeManager::kSlots))
            use_big_name_hash(sole().get(), memory::sizes::node_hash_slots());
    }
    ++ref_tally();
    return sole().get();
}

// 0x0045D6E0
void release_node_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    NodeManager* const manager = sole().release(); // (no destructor: its block given back)
    if (manager)
        memory::Hub::get_pull()->dealloc(manager);
    // NOT ORIGINAL: its bigger name hash goes with it.
    if (manager == big_node_arrays().big_owner_)
        use_big_name_hash(nullptr, 0);
    ref_tally() = 0;
}

} // namespace level_nodes
