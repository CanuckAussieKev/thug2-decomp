// The engine's imposter manager: see engine_imposters.h.

#include "engine/gfx/engine_imposters.h"

#include <cstdint>
#include <new>

#include "engine/core/mem.h"
#include "engine/gfx/primitive_states.h"
#include "engine/lists/key_map.h"

namespace render {

namespace {

// The groups' slots.
template <typename R = void>
R Virtual(void* group, int slot)
{
    return reinterpret_cast<R(__thiscall*)(void*)>((*static_cast<void***>(group))[slot])(group);
}
float span_to(void* group)
{
    return reinterpret_cast<float(__thiscall*)(void*)>((*static_cast<void***>(group))[5])(group);
}
float& imposter_near_range(void* group) { return *reinterpret_cast<float*>(static_cast<uint8_t*>(group) + 4); }
int& frame_count(void* group) { return *reinterpret_cast<int*>(static_cast<uint8_t*>(group) + 0x58); }
bool has_imposter(void* group) { return *(static_cast<uint8_t*>(group) + 0x5C) != 0; }

// The table's iteration, inlined in the original: the first / next value (null at the end).
void* next_group(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return table->iter_item_->value;
}
void* first_group(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    return next_group(table);
}

} // namespace

// 0x0048E3E0
ImposterManager* ImposterManager::make_new()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    groups_ptr_ = memory ? new (memory) lists::KeyMap(8) : nullptr;
    max_per_frame_ = 1;
    return this;
}

// 0x0048E260
void ImposterManager::delete_all()
{
    for (void* group = first_group(groups_ptr_); group;) {
        void* next = next_group(groups_ptr_);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(group))[0])(group, 1);
        group = next;
    }
    groups_ptr_->empty_table();
}

// 0x0048E2D0
void ImposterManager::tick_frame()
{
    int done = 0;
    for (void* group = first_group(groups_ptr_); group; group = next_group(groups_ptr_)) {
        Virtual(group, 6);
        float distance = span_to(group);
        if (distance <= imposter_near_range(group)) { // (NaN: as beyond it)
            Virtual(group, 2);
        } else if (!has_imposter(group)) {
            if (done < max_per_frame_) {
                Virtual(group, 1);
                ++done;
            }
        } else if (done < max_per_frame_) {
            ++frame_count(group);
            if (Virtual<bool>(group, 3))
                ++done;
        }
    }
}

// 0x0048E370
void ImposterManager::render()
{
    set_primitive_states();
    for (void* group = first_group(groups_ptr_); group; group = next_group(groups_ptr_)) {
        if (has_imposter(group))
            Virtual(group, 4);
    }
    reset_primitive_states();
}

} // namespace render
