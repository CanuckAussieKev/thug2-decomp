// The goal editor's nodes added to the level's node array (park_editing::add_goal_editor_nodes): see goal_editor.h. From the
// disassembly of 0x004E8F50-0x004E93B7.

#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/format.h"
#include "engine/core/manager.h"
#include "engine/script/cell.h"
#include "game/park_editing/goal_editor.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"

namespace park_editing {

namespace {

template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

} // namespace

// 0x004E8F50
void add_goal_editor_nodes(script::Array* nodes, int* index, memory::Arena* heap)
{
    debug::printf("M:%s:%i: %s\n", "mponents\\GoalEditorComponent.cpp", 0x152, "Inserting Goal Editor Nodes");
    bool in_place = true;
    uint32_t array_name = 0;
    level_nodes::NodeManager* manager = nullptr;
    int count = 0;
    if (!nodes) {
        // Added to the node manager's last array, grown for them, and hashed there.
        nodes = level_nodes::get_node_owner_nodes();
        manager = level_nodes::node_manager_instance().get();
        array_name = level_nodes::get_last_node_array_name();
        in_place = false;
        count = static_cast<int>(nodes->extent());
        index = &count;
        if (!count)
            nodes->configure_size_kind(0x82, script::kQbvalStruct);
        else
            nodes->rescale(nodes->extent() + 0x82);
    }
    memory::Hub::get_pull()->enter_heap_scope(static_cast<memory::MemSource*>(heap));
    std::array<char, 100> name{};
    for (int goal = 0; goal < 10; ++goal) {
        // The pro, the restart, then eleven objects.
        for (int which = 0; which < 13; ++which) {
            script::Struct* node = nullptr;
            if (in_place) {
                node = nodes->struct_at(static_cast<uint32_t>(*index));
            } else {
                gsl::owner<script::Struct*> made = script::new_struct(); // (the node array's, below)
                node = made;
            }
            static const std::array<uint32_t, 3> kKinds = {{0x8F0F67D7, 0xC9119673, 0x000B52B3}};
            node->append_structure_if_any(script::read_sub_struct(core::at(kKinds, which < 2 ? which : 2)));
            core::at(name, 0) = '\0';
            if (which == 0)
                get_sprintf(name.data(), "TRG_EditedGoal%d_Pro", goal);
            else if (which == 1)
                get_sprintf(name.data(), "TRG_EditedGoal%d_Restart", goal);
            else
                get_sprintf(name.data(), "TRG_EditedGoal%d_Object_%d", goal, which - 2);
            node->put_name_key(checksums::Name{0xA1DC81F9}, checksums::hash_from_text(name.data()));
            node->put_int(checksums::Name{0xE50D6573}, *index);
            if (!in_place) {
                nodes->set_cell(static_cast<uint32_t>(*index), script::Cell::of(node));
                manager->hash_node_in(array_name, static_cast<uint32_t>(*index), node);
            }
            ++*index;
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

} // namespace park_editing
