// The skater trick code's two list entry types. See trick_lists.h.

#include "game/skater/trick_lists.h"

#include <new>

#include "game/core/manager.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/skater/gap.h"

namespace GameObjects {

namespace {

extern void** const kListEntryVtable;
extern void** const kNameEntryVtable;

void InitNode(MemSys::ListNode& node, void* data)
{
    node.data = data;
    node.priority = 0;
    node.next = &node;
    node.prev = &node;
}

} // namespace

// 0x00578670
void TrickListEntry::SetFlag() { m_flag2C = true; }

// 0x00578740
TrickListEntry* TrickListEntry::MakeNew()
{
    InitNode(m_listLink, this);
    m_listLink.vtable = kListEntryVtable;
    m_lookupKey = 0;
    m_unknown24 = 0;
    m_unknown30 = 0;
    m_unknown34 = false;
    Scripts::CArray* memory = Scripts::NewArray();
    m_elementList = memory ? memory->PrepareAll() : nullptr;
    m_unknown38 = 0;
    m_unknown3C = 0;
    return this;
}

// 0x00579570
void TrickListEntry::DestroyBody()
{
    m_listLink.vtable = kListEntryVtable;
    if (m_elementList)
        Scripts::ReleaseArrayStorage(m_elementList);
    if (Scripts::CArray* array = m_elementList) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
    m_listLink.UnlinkAs(g_gapNodeVtable);
}

// 0x00579820
TrickListEntry* TrickListEntry::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005787F0
TrickNameEntry* TrickNameEntry::MakeNew()
{
    InitNode(m_listLink, this);
    m_listLink.vtable = kNameEntryVtable;
    new (&m_label) TextUtil::Chars();
    return this;
}

// 0x00579630
void TrickNameEntry::DestroyBody()
{
    m_label.~Chars();
    m_listLink.UnlinkAs(g_gapTrickNodeVtable);
}

// 0x00579600
TrickNameEntry* TrickNameEntry::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kListEntryVtable_slots[] = {&GameObjects::TrickListEntry::Teardown};
void** const kListEntryVtable = reinterpret_cast<void**>(kListEntryVtable_slots);
static VtableSlot kNameEntryVtable_slots[] = {&GameObjects::TrickNameEntry::Teardown};
void** const kNameEntryVtable = reinterpret_cast<void**>(kNameEntryVtable_slots);

} // namespace
} // namespace Obj
