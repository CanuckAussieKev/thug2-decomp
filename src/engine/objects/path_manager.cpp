// The path manager and its object groups (0x004694A0-0x00469AFE). See path_manager.h.

#include "engine/objects/path_manager.h"

#include <cstdint>
#include <new>
#include <ranges>
#include <span>
#include <gsl/assert>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace objects {

namespace {
constexpr int kGroupCount = 200;
constexpr int kGroupSize = 20;
constexpr int kExtraSize = 200;
constexpr uint32_t kRailNode = 0x8E6B02AD; // RailNode
constexpr uint32_t kClimbingNode = 0x30C19600; // ClimbingNode
constexpr uint32_t kWaypoint = 0x4C23A77E; // Waypoint
constexpr uint32_t kPedAI = 0xCBA10FFA; // PedAI
constexpr uint32_t kMaxLinks = 0x0C230B04; // MaxLinks
constexpr uint32_t kPathGroupSlots = 0xF6A4081D; // text unknown (AllocatePathManMemory: the slots in each group)
constexpr uint32_t kPathExtraSlots = 0x13AFFFF9; // text unknown (AllocatePathManMemory: the extra slots)

constexpr uint32_t kPathNum = 0x9C91C3CA; // PathNum
constexpr uint32_t kClass = 0x12B4E660;   // class
constexpr uint32_t kType = 0x7321A8D6;    // type

PathManager*& instance_pointer()
{
    static PathManager* path_manager{};
    return path_manager;
}

// An array of `count` elements of `size` bytes from the current heap. The original's was an array new[]: the count in
// a word before the elements. The count is the owner's member here (ObjectGroup::extent_, PathManager::group_count_);
// the word's room is still asked for, so the heap's use is the same.
void* alloc_script_array(int count, unsigned int size)
{
    return memory::alloc((static_cast<unsigned int>(count) * size) + sizeof(int));
}
void delete_array(void* elements) { memory::Hub::get_pull()->dealloc(elements); }

// A slot let go (and cleared again, as the original).
void release_slot(SharedRef<Entity>* slot)
{
    slot->get_release();
    slot->target_ = nullptr;
}

} // namespace

// 0x004694A0
ObjectGroup::~ObjectGroup()
{
    if (!slots_)
        return;
    const std::span<SharedRef<Entity>> slots = this->slots();
    for (auto& slot : std::views::reverse(slots))
        slot.get_release();
    delete_array(slots_);
    slots_ = nullptr;
}

// 0x004698C0
void ObjectGroup::prepare_all(int size)
{
    slots_ = static_cast<SharedRef<Entity>*>(alloc_script_array(size, sizeof(SharedRef<Entity>)));
    extent_ = size;
    for (SharedRef<Entity>& slot : slots())
        new (&slot) SharedRef<Entity>(); // (0x0042B580: all three words cleared)
    release_all();
}

// 0x00469540
void ObjectGroup::release_all() const
{
    for (int i = 0; i < extent_; ++i)
        release_slot(&core::at(slots(), i));
}

// 0x00469580
void ObjectGroup::drop_out(Entity* object) const
{
    for (int i = 0; i < extent_; ++i) {
        SharedRef<Entity>& slot = core::at(slots(), i);
        if (slot.get_pull() == object)
            release_slot(&slot);
    }
}

// 0x004695D0
void ObjectGroup::append_one(Entity* object) const
{
    for (int i = 0; i < extent_; ++i) {
        SharedRef<Entity>& slot = core::at(slots(), i);
        if (!slot.get_pull()) {
            slot.assign_value(object);
            return;
        }
    }
}

// 0x004694E0
PathManager* PathManager::sole()
{
    if (!instance_pointer()) {
        auto* manager = static_cast<PathManager*>(memory::alloc(sizeof(PathManager)));
        if (manager) {
            manager->path_count_ = 0;
            manager->group_count_ = 0;
            manager->groups_ = nullptr;
            manager->extra_ = nullptr;
        }
        instance_pointer() = manager;
    }
    return instance_pointer();
}

// 0x00469620
ObjectGroup* PathManager::add_to_path(Entity* object, uint32_t node) const
{
    script::Struct* params = level_nodes::fetch_vertex(node, true);
    int path = 0;
    params->fetch_int_value(kPathNum, &path, 0);
    ObjectGroup& group = core::at(groups(), path);
    group.append_one(object);
    return &group;
}

// 0x00469670
ObjectGroup* PathManager::add_to_extra_group(Entity* object, uint32_t node)
{
    script::Struct* params = level_nodes::fetch_vertex(node, true);
    int path = 0;
    params->fetch_int_value(kPathNum, &path, 0); // (not used)
    extra_->append_one(object);
    return extra_;
}

// 0x004696C0
void PathManager::number_paths()
{
    if (!groups_)
        return;
    for (int i = 0; i < group_count_; ++i)
        core::at(groups(), i).release_all();
    path_count_ = 0;
    level_nodes::start_node_walk(0);
    for (script::Struct* start = level_nodes::get_next_node(); start; start = level_nodes::get_next_node()) {
        uint32_t node_class = 0;
        start->read_hash_value(kClass, &node_class, 0);
        if (node_class == kRailNode || node_class == kClimbingNode)
            continue;
        uint32_t type = 0;
        if (node_class == kWaypoint && start->read_hash_value(kType, &type, 0) && type == kPedAI)
            continue;
        if (level_nodes::get_link_count(start) == 0 && node_class != kWaypoint)
            continue;
        if (start->has_entry_named(kPathNum))
            continue;
        // down the first links, numbering; a node numbered already gives its number to the chain, from the start
        int path = path_count_;
        script::Struct* node = start;
        for (;;) {
            int existing = 0;
            if (node->fetch_int_value(kPathNum, &existing, 0)) {
                if (existing == path)
                    break;
                path = existing;
                node = start;
            }
            node->put_int(checksums::Name{kPathNum}, path);
            if (level_nodes::get_link_count(node) == 0)
                break;
            node = level_nodes::fetch_neighbour_node(node, 0);
        }
        if (path == path_count_)
            ++path_count_;
    }
    level_nodes::end_node_walk();
}

// 0x00469820
void PathManager::dealloc()
{
    if (!groups_)
        return;
    for (int i = 0; i < group_count_; ++i)
        core::at(groups(), i).release_all();
    if (groups_) {
        const std::span<ObjectGroup> groups = this->groups();
        for (auto& group : std::views::reverse(groups))
            group.~ObjectGroup();
        delete_array(groups_);
    }
    ObjectGroup* extra = extra_;
    groups_ = nullptr;
    if (extra) {
        if (extra->get_slots()) {
            const std::span<SharedRef<Entity>> slots = extra->slots();
            for (auto& slot : std::views::reverse(slots))
                slot.get_release();
            delete_array(extra->get_slots());
            extra->set_slots(nullptr);
        }
        memory::Hub::get_pull()->dealloc(extra);
    }
    extra_ = nullptr;
}

// 0x00469960
void PathManager::carve(PathPoolSizes pool_sizes)
{
    const auto groups = pool_sizes.groups;
    const auto group_size = pool_sizes.group_size_;
    const auto extra_size = pool_sizes.extra_size_;
    group_count_ = groups;
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->bottom_up_heap());
    groups_ = static_cast<ObjectGroup*>(alloc_script_array(group_count_, sizeof(ObjectGroup)));
    for (ObjectGroup& group : this->groups())
        new (&group) ObjectGroup;
    for (int i = 0; i < group_count_; ++i)
        core::at(this->groups(), i).prepare_all(group_size);
    void* block = memory::alloc(sizeof(ObjectGroup));
    ObjectGroup* extra = block ? memory::construct_in<ObjectGroup>(block) : nullptr;
    extra_ = extra;
    Expects(extra != nullptr);
    extra->prepare_all(extra_size);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00469A80
bool cfunc_allocate_path_man_memory(script::Struct* params, script::Script* /*script*/)
{
    PathManager* manager = PathManager::sole();
    int groups = kGroupCount;
    params->fetch_int_value(kMaxLinks, &groups, 0);
    int group_size = kGroupSize;
    params->fetch_int_value(kPathGroupSlots, &group_size, 0);
    int extra_size = kExtraSize;
    params->fetch_int_value(kPathExtraSlots, &extra_size, 0);
    manager->carve({.groups = groups, .group_size_ = group_size, .extra_size_ = extra_size});
    return true;
}

} // namespace objects
