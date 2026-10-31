// The net library's list types' destructors. See net_lists.h.

#include "game/net/net_lists.h"

#include "game/core/manager.h"
#include "game/core/code_address.h"

namespace NetLink {

static VtableSlot kConnNodeVtable_slots[] = {&NetLink::ConnNode::Teardown};
void** const kConnNodeVtable = reinterpret_cast<void**>(kConnNodeVtable_slots);
static VtableSlot kConnHeadVtable_slots[] = {&NetLink::ConnNode::DestroyHead};
void** const kConnHeadVtable = reinterpret_cast<void**>(kConnHeadVtable_slots);
static VtableSlot kBannedConnNodeVtable_slots[] = {&NetLink::BannedConnNode::Teardown};
void** const kBannedConnNodeVtable = reinterpret_cast<void**>(kBannedConnNodeVtable_slots);
static VtableSlot kBannedConnVtable_slots[] = {&NetLink::BlockedConnEntry::DestroyBanned};
void** const kBannedConnVtable = reinterpret_cast<void**>(kBannedConnVtable_slots);
static VtableSlot kMsgHandlerNodeVtable_slots[] = {&NetLink::MsgHandlerNode::Teardown};
void** const kMsgHandlerNodeVtable = reinterpret_cast<void**>(kMsgHandlerNodeVtable_slots);
static VtableSlot kConnSearchVtable_slots[] = {&NetLink::ConnSearch::Teardown};
void** const kConnSearchVtable = reinterpret_cast<void**>(kConnSearchVtable_slots);
static VtableSlot kBannedConnSearchVtable_slots[] = {&NetLink::BannedConnSearch::Teardown};
void** const kBannedConnSearchVtable = reinterpret_cast<void**>(kBannedConnSearchVtable_slots);

namespace {

void Dealloc(void* memory) { MemSys::Hub::Pull()->Dealloc(memory); }

} // namespace

// 0x00457010
MsgHandlerNode* MsgHandlerNode::ConstructHead()
{
    data = reinterpret_cast<void*>(-1);
    priority = 0;
    next = this;
    prev = this;
    vtable = kMsgHandlerHeadVtable;
    return this;
}

// 0x00457030
void MsgHandlerNode::DestroyBody() { UnlinkAs(kMsgHandlerNodeVtable); }

// 0x00457090
MsgHandlerNode* MsgHandlerNode::Teardown(unsigned int flags)
{
    UnlinkAs(kMsgHandlerNodeVtable);
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x00457260
void HandlerLists::Teardown()
{
    for (int i = kCount - 1; i >= 0; --i)
        m_lists[i].DestroyBody();
}

// 0x00457050
void ConnNode::DestroyBody() { UnlinkAs(kConnNodeVtable); }

// 0x004573A0
ConnNode* ConnNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x00455BD0
ConnNode* ConnNode::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x00457070
void BannedConnNode::DestroyBody() { UnlinkAs(kBannedConnNodeVtable); }

// 0x004570D0
BannedConnNode* BannedConnNode::Teardown(unsigned int flags)
{
    UnlinkAs(kBannedConnNodeVtable);
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x00457460
BlockedConnEntry* BlockedConnEntry::DestroyBanned(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x00456B80
void ConnSearch::DestroyBody() { m_vtable = kConnSearchVtable; }

// 0x00456FB0
ConnSearch* ConnSearch::Teardown(unsigned int flags)
{
    m_vtable = kConnSearchVtable;
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x00456B90
void BannedConnSearch::DestroyBody() { m_vtable = kBannedConnSearchVtable; }

// 0x00456FE0
BannedConnSearch* BannedConnSearch::Teardown(unsigned int flags)
{
    m_vtable = kBannedConnSearchVtable;
    if (flags & 1)
        Dealloc(this);
    return this;
}

} // namespace Net
