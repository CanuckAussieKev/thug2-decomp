// The engine's imposter manager: see engine_imposters.h.

#include "engine/gfx/engine_imposters.h"

#include <cstdint>
#include <new>

#include "engine/core/mem.h"
#include "engine/gfx/primitive_states.h"
#include "engine/lists/key_map.h"

namespace render {

namespace {

// The table's iteration, inlined in the original: the first / next value (null at the end).
ImposterGroup* next_group(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return static_cast<ImposterGroup*>(table->iter_item_->value);
}
ImposterGroup* first_group(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    return next_group(table);
}

} // namespace

// 0x0048E3E0
ImposterManager::ImposterManager()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    groups_ptr_ = memory ? memory::construct_in<lists::KeyMap>(memory, 8) : nullptr;
}

// 0x0048E260
void ImposterManager::delete_all()
{
    for (const ImposterGroup* group = first_group(groups_ptr_); group;) {
        const ImposterGroup* next = next_group(groups_ptr_);
        delete group;
        group = next;
    }
    groups_ptr_->empty_table();
}

// 0x0048E2D0
void ImposterManager::tick_frame()
{
    int done = 0;
    for (ImposterGroup* group = first_group(groups_ptr_); group; group = next_group(groups_ptr_)) {
        group->tick_frame();
        const float distance = group->distance_to_view();
        if (distance <= group->near_range_) { // (NaN: as beyond it)
            group->drop_imposter();
        } else if (!group->has_imposter_) {
            if (done < max_per_frame_) {
                group->create_imposter();
                ++done;
            }
        } else if (done < max_per_frame_) {
            ++group->frame_count_;
            if (group->update_imposter())
                ++done;
        }
    }
}

// 0x0048E370
void ImposterManager::render()
{
    set_primitive_states();
    for (ImposterGroup* group = first_group(groups_ptr_); group; group = next_group(groups_ptr_)) {
        if (group->has_imposter_)
            group->draw_imposter();
    }
    reset_primitive_states();
}

} // namespace render
