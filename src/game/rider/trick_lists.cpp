// The skater trick code's two list entry types. See trick_lists.h.

#include "game/rider/trick_lists.h"

#include <new>

#include "engine/core/manager.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "game/rider/gap_entry.h"

namespace objects {

namespace {

extern void** const kListEntryVtable;
extern void** const kNameEntryVtable;

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
    list_link_.vtable = kListEntryVtable;
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
    list_link_.vtable = kListEntryVtable;
    if (element_list_)
        script::release_array_storage(element_list_);
    if (script::Array* array = element_list_) {
        array->get_clear();
        script::delete_array(array);
    }
    list_link_.unlink_as(kGapNodeVtable);
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
    list_link_.vtable = kNameEntryVtable;
    new (&label_) text::Chars();
    return this;
}

// 0x00579630
void TrickNameEntry::destroy_body()
{
    label_.~Chars();
    list_link_.unlink_as(kGapTrickNodeVtable);
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

namespace objects {
namespace {

static VtableSlot g_list_entry_vtable_slots[] = {&objects::TrickListEntry::teardown};
void** const kListEntryVtable = reinterpret_cast<void**>(g_list_entry_vtable_slots);
static VtableSlot g_name_entry_vtable_slots[] = {&objects::TrickNameEntry::teardown};
void** const kNameEntryVtable = reinterpret_cast<void**>(g_name_entry_vtable_slots);

} // namespace
} // namespace objects
