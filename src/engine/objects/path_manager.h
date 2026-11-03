#pragma once

// The path manager (0x004694A0-0x00469AFE; the instance at 0x006F2264): numbers the level's paths (chains of
// linked nodes; each node gets its path's number as PathNum, 0x9C91C3CA) and keeps, for each path, the objects
// on it (smart pointers), plus one extra group. AllocatePathManMemory (a script command) sizes it.

#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/objects/smtptr.h"

namespace script {
class Struct;
class Script;
} // namespace script

namespace objects {

class Entity;

// A fixed number of slots for objects (8 bytes; the slots are an array new[] of smart pointers).
class ObjectGroup {
public:
    // 0x00414D70: no slots.
    ObjectGroup() = default;
    // 0x004694A0 (not a Ghidra function: only the array helpers point at it): the slots' smart pointers let go
    // (last first) and the array freed.
    ~ObjectGroup();
    ObjectGroup(const ObjectGroup&) = delete;
    ObjectGroup& operator=(const ObjectGroup&) = delete;
    // 0x004698C0: `size` empty slots (from the current heap).
    void prepare_all(int size);
    // 0x00469540: every slot let go.
    void release_all() const;
    // 0x00469580: every slot holding `object` let go.
    void drop_out(Entity* object) const;
    // 0x004695D0: `object` into the first empty slot (nothing if they're all taken).
    void append_one(Entity* object) const;

    // NOT ORIGINAL - the slots as a run of their count.
    [[nodiscard]] std::span<SharedRef<Entity>> slots() const
    {
        return {slots_, slots_ ? static_cast<std::size_t>(extent_) : 0};
    }

    // The fields other code reads and sets.
    [[nodiscard]] SharedRef<Entity>* get_slots() const { return slots_; }
    void set_slots(SharedRef<Entity>* value) { slots_ = value; }
    [[nodiscard]] int extent() const { return extent_; }

private:
    SharedRef<Entity>* slots_{nullptr};
    int extent_{0}; // the slots' count (the original also kept it in the word before them, as new[] does)
};

class PathManager {
public:
    // 0x004694E0: the instance, made on first use (empty).
    static PathManager* sole();
    // 0x00469620: `object` into the group of the path its node `node` is on; returns that group.
    ObjectGroup* add_to_path(Entity* object, uint32_t node) const;
    // 0x00469670: `object` into the extra group (the node's path is read, not used); returns the extra group.
    ObjectGroup* add_to_extra_group(Entity* object, uint32_t node);
    // 0x004696C0: the groups emptied and every path numbered again (nodes of a path's class with links - or of
    // class 0x4C23A77E - that have no PathNum start one and give it to every node down their first links; a
    // chain that runs into a numbered node takes that number instead). Nothing if not allocated.
    void number_paths();
    // 0x00469820: the groups and the extra group freed.
    void dealloc();
    // The path pool's group count, a group's size and the extra size.
    struct PathPoolSizes {
        int groups{};
        int group_size_{};
        int extra_size_{};
    };

    // 0x00469960: `groups` groups of `group_size` slots and an extra group of `extraSize` (bottom-up heap).
    void carve(PathPoolSizes pool_sizes);

    // NOT ORIGINAL - the groups as a run of their count.
    [[nodiscard]] std::span<ObjectGroup> groups() const
    {
        return {groups_, groups_ ? static_cast<std::size_t>(group_count_) : 0};
    }

private:
    ObjectGroup* groups_; // +0x00 (the original's new[]: the count also in the word before them)
    ObjectGroup* extra_;  // +0x04
    int path_count_;      // +0x08
    int group_count_;     // +0x0C
};

// 0x00469A80: the script command AllocatePathManMemory (0x0C230B04 groups, default 200; 0xF6A4081D slots each,
// default 20; 0x13AFFFF9 extra slots, default 200). True.
bool cfunc_allocate_path_man_memory(script::Struct* params, script::Script* script);

} // namespace objects
