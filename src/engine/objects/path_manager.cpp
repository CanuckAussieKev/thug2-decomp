// The path manager and its object groups (0x004694A0-0x00469AFE). See path_manager.h.

#include "engine/objects/path_manager.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/entity.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"

namespace objects {

namespace {
PathManager* g_path_manager;

constexpr uint32_t kPathNum = 0x9C91C3CA; // PathNum
constexpr uint32_t kClass = 0x12B4E660;   // class
constexpr uint32_t kType = 0x7321A8D6;    // type

PathManager*& instance_pointer() { return g_path_manager; }

// An array new[] of `count` elements of `size` bytes from the current heap: the count in the word before them.
void* alloc_script_array(int count, unsigned int size)
{
    auto* block = static_cast<int*>(memory::alloc(static_cast<unsigned int>(count) * size + 4));
    if (!block)
        return nullptr;
    block[0] = count;
    return block + 1;
}
void delete_array(void* elements) { memory::Hub::get_pull()->dealloc(static_cast<int*>(elements) - 1); }
int element_total(const void* elements) { return static_cast<const int*>(elements)[-1]; }

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
    for (int i = element_total(slots_) - 1; i >= 0; --i)
        slots_[i].get_release();
    delete_array(slots_);
    slots_ = nullptr;
}

// 0x004698C0
void ObjectGroup::prepare_all(int size)
{
    auto* slots = static_cast<SharedRef<Entity>*>(alloc_script_array(size, sizeof(SharedRef<Entity>)));
    if (slots) {
        for (int i = 0; i < size; ++i)
            new (&slots[i]) SharedRef<Entity>(); // (0x0042B580: all three words cleared)
    }
    slots_ = slots;
    extent_ = size;
    release_all();
}

// 0x00469540
void ObjectGroup::release_all()
{
    for (int i = 0; i < extent_; ++i)
        release_slot(&slots_[i]);
}

// 0x00469580
void ObjectGroup::drop_out(Entity* object)
{
    for (int i = 0; i < extent_; ++i) {
        if (slots_[i].get_pull() == object)
            release_slot(&slots_[i]);
    }
}

// 0x004695D0
void ObjectGroup::append_one(Entity* object)
{
    for (int i = 0; i < extent_; ++i) {
        if (!slots_[i].get_pull()) {
            slots_[i].assign_value(object);
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
ObjectGroup* PathManager::add_to_path(Entity* object, uint32_t node)
{
    script::Struct* params = level_nodes::fetch_vertex(node, true);
    int path = 0;
    params->fetch_int_value(kPathNum, &path, 0);
    groups_[path].append_one(object);
    return &groups_[path];
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
        groups_[i].release_all();
    path_count_ = 0;
    level_nodes::start_node_walk(0);
    for (script::Struct* start = level_nodes::get_next_node(); start; start = level_nodes::get_next_node()) {
        uint32_t node_class = 0;
        start->read_hash_value(kClass, &node_class, 0);
        if (node_class == 0x8E6B02AD || node_class == 0x30C19600)
            continue;
        uint32_t type = 0;
        if (node_class == 0x4C23A77E && start->read_hash_value(kType, &type, 0) && type == 0xCBA10FFA)
            continue;
        if (level_nodes::get_link_count(start) == 0 && node_class != 0x4C23A77E)
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
            node->put_int(kPathNum, path);
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
        groups_[i].release_all();
    if (ObjectGroup* groups = groups_) {
        for (int i = element_total(groups) - 1; i >= 0; --i)
            groups[i].~ObjectGroup();
        delete_array(groups);
    }
    ObjectGroup* extra = extra_;
    groups_ = nullptr;
    if (extra) {
        if (extra->slots_) {
            for (int i = element_total(extra->slots_) - 1; i >= 0; --i)
                extra->slots_[i].get_release();
            delete_array(extra->slots_);
            extra->slots_ = nullptr;
        }
        memory::Hub::get_pull()->dealloc(extra);
    }
    extra_ = nullptr;
}

// 0x00469960
void PathManager::carve(int groups, int group_size, int extra_size)
{
    group_count_ = groups;
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->bottom_up_heap_);
    auto* made = static_cast<ObjectGroup*>(alloc_script_array(group_count_, sizeof(ObjectGroup)));
    if (made) {
        for (int i = 0; i < group_count_; ++i)
            new (&made[i]) ObjectGroup;
    }
    groups_ = made;
    for (int i = 0; i < group_count_; ++i)
        groups_[i].prepare_all(group_size);
    void* block = memory::alloc(sizeof(ObjectGroup));
    ObjectGroup* extra = block ? memory::construct_in<ObjectGroup>(block) : nullptr;
    extra_ = extra;
    extra->prepare_all(extra_size);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00469A80
bool cfunc_allocate_path_man_memory(script::Struct* params, script::Script* /*script*/)
{
    PathManager* manager = PathManager::sole();
    int groups = 200;
    params->fetch_int_value(0x0C230B04, &groups, 0);
    int group_size = 20;
    params->fetch_int_value(0xF6A4081D, &group_size, 0);
    int extra_size = 200;
    params->fetch_int_value(0x13AFFFF9, &extra_size, 0);
    manager->carve(groups, group_size, extra_size);
    return true;
}

} // namespace objects
