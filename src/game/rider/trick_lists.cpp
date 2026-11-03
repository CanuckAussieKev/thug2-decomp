// The skater trick code's two list entry types. See trick_lists.h.

#include "game/rider/trick_lists.h"

#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/param_struct.h"
#include "game/rider/gap_entry.h"
#include <gsl/pointers>

namespace objects {

namespace {

void init_node(memory::ListNode& node, void* data)
{
    node.set_data(data);
    node.set_priority(0);
    node.set_next(&node);
    node.set_prev(&node);
}

} // namespace

// 0x00578670
void TrickNameEntry::set_flag() { done_ = true; }

// 0x00578740
TrickListEntry::TrickListEntry()
{
    init_node(list_link_, this);
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    element_list_ = memory ? memory::construct_in<script::Array>(memory) : nullptr;
}

// 0x00579570
TrickListEntry::~TrickListEntry()
{
    if (element_list_) {
        script::release_array_storage(element_list_);
    }
    if (script::Array* array = element_list_) {
        array->get_clear();
        script::delete_array(array);
    }
    list_link_.uncouple(); // (0x00511E50)
}

// 0x00579820
TrickListEntry* TrickListEntry::teardown(unsigned int flags)
{
    this->~TrickListEntry();
    if (flags & 1) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x005787F0
TrickNameEntry::TrickNameEntry()
{
    init_node(list_link_, this);
}

// 0x00579630
TrickNameEntry::~TrickNameEntry()
{
    list_link_.uncouple(); // (then the name: its destructor) // (0x00511E70)
}

// 0x00579600
TrickNameEntry* TrickNameEntry::teardown(unsigned int flags)
{
    this->~TrickNameEntry();
    if (flags & 1) {
        memory::Hub::dealloc(this);
    }
    return this;
}

} // namespace objects
