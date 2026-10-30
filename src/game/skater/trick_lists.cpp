// The skater trick code's two list entry types. See trick_lists.h.

#include "game/skater/trick_lists.h"

#include <new>

#include "game/core/manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/skater/gap.h"

namespace Obj {

namespace {

extern void** const kListEntryVtable;
extern void** const kNameEntryVtable;

void InitNode(Mem::ListNode& node, void* data)
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
TrickListEntry* TrickListEntry::Construct()
{
    InitNode(m_node, this);
    m_node.vtable = kListEntryVtable;
    m_key = 0;
    m_unknown24 = 0;
    m_unknown30 = 0;
    m_unknown34 = false;
    Script::CArray* memory = Script::NewArray();
    m_array = memory ? memory->Init() : nullptr;
    m_unknown38 = 0;
    m_unknown3C = 0;
    return this;
}

// 0x00579570
void TrickListEntry::DestroyBody()
{
    m_node.vtable = kListEntryVtable;
    if (m_array)
        Script::CleanUpArray(m_array);
    if (Script::CArray* array = m_array) {
        array->Clear();
        Script::DeleteArray(array);
    }
    m_node.UnlinkAs(g_gapNodeVtable);
}

// 0x00579820
TrickListEntry* TrickListEntry::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005787F0
TrickNameEntry* TrickNameEntry::Construct()
{
    InitNode(m_node, this);
    m_node.vtable = kNameEntryVtable;
    new (&m_name) Str::String();
    return this;
}

// 0x00579630
void TrickNameEntry::DestroyBody()
{
    m_name.~String();
    m_node.UnlinkAs(g_gapTrickNodeVtable);
}

// 0x00579600
TrickNameEntry* TrickNameEntry::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

} // namespace Obj

REPLACE(0x00578670, &Obj::TrickListEntry::SetFlag);
REPLACE(0x00578740, &Obj::TrickListEntry::Construct);
REPLACE(0x005787F0, &Obj::TrickNameEntry::Construct);
REPLACE(0x00579570, &Obj::TrickListEntry::DestroyBody);
REPLACE(0x00579600, &Obj::TrickNameEntry::Destroy);
REPLACE(0x00579630, &Obj::TrickNameEntry::DestroyBody);
REPLACE(0x00579820, &Obj::TrickListEntry::Destroy);

namespace Obj {
namespace {

GAME_VTABLE(kListEntryVtable, 0x0064EC80,
    &Obj::TrickListEntry::Destroy);
GAME_VTABLE(kNameEntryVtable, 0x0064EC84,
    &Obj::TrickNameEntry::Destroy);

} // namespace
} // namespace Obj
