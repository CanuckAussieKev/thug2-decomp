// The skater trick code's two list entry types. See trick_lists.h.

#include "game/rider/trick_lists.h"

#include <new>

#include "engine/core/manager.h"
#include "engine/script/param_struct.h"
#include "game/rider/gap_entry.h"

namespace objects {

namespace {

void init_node(memory::ListNode& node, void* data)
{
    node.data = data;
    node.priority = 0;
    node.next = &node;
    node.prev = &node;
}

} // namespace

// 0x00578670
void TrickListEntry::set_flag() { flag_2c_ = true; }

// 0x00578740
TrickListEntry* TrickListEntry::make_new()
{
    init_node(list_link_, this);
    lookup_key_ = 0;
    unknown24_ = 0;
    unknown30_ = 0;
    unknown34_ = false;
    script::Array* memory = script::alloc_script_array();
    element_list_ = memory ? memory->prepare_all() : nullptr;
    unknown38_ = 0;
    unknown3c_ = 0;
    return this;
}

// 0x00579570
void TrickListEntry::destroy_body()
{
    if (element_list_)
        script::release_array_storage(element_list_);
    if (script::Array* array = element_list_) {
        array->get_clear();
        script::delete_array(array);
    }
    list_link_.uncouple(); // (0x00511E50)
}

// 0x00579820
TrickListEntry* TrickListEntry::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x005787F0
TrickNameEntry* TrickNameEntry::make_new()
{
    init_node(list_link_, this);
    new (&label_) text::Chars();
    return this;
}

// 0x00579630
void TrickNameEntry::destroy_body()
{
    label_.~Chars();
    list_link_.uncouple(); // (0x00511E70)
}

// 0x00579600
TrickNameEntry* TrickNameEntry::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

} // namespace objects
