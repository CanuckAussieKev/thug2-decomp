// The net library's list types' destructors. See net_lists.h.

#include "game/net/net_lists.h"

#include "game/core/manager.h"
#include "game/core/code_address.h"

namespace Net {

static VtableSlot kConnNodeVtable_slots[] = {&Net::ConnNode::Destroy};
void** const kConnNodeVtable = reinterpret_cast<void**>(kConnNodeVtable_slots);
static VtableSlot kConnHeadVtable_slots[] = {&Net::ConnNode::DestroyHead};
void** const kConnHeadVtable = reinterpret_cast<void**>(kConnHeadVtable_slots);
static VtableSlot kBannedConnNodeVtable_slots[] = {&Net::BannedConnNode::Destroy};
void** const kBannedConnNodeVtable = reinterpret_cast<void**>(kBannedConnNodeVtable_slots);
static VtableSlot kBannedConnVtable_slots[] = {&Net::BannedConn::DestroyBanned};
void** const kBannedConnVtable = reinterpret_cast<void**>(kBannedConnVtable_slots);
static VtableSlot kMsgHandlerNodeVtable_slots[] = {&Net::MsgHandlerNode::Destroy};
void** const kMsgHandlerNodeVtable = reinterpret_cast<void**>(kMsgHandlerNodeVtable_slots);
static VtableSlot kConnSearchVtable_slots[] = {&Net::ConnSearch::Destroy};
void** const kConnSearchVtable = reinterpret_cast<void**>(kConnSearchVtable_slots);
static VtableSlot kBannedConnSearchVtable_slots[] = {&Net::BannedConnSearch::Destroy};
void** const kBannedConnSearchVtable = reinterpret_cast<void**>(kBannedConnSearchVtable_slots);

namespace {

void Free(void* memory) { Mem::Manager::Get()->Free(memory); }

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
MsgHandlerNode* MsgHandlerNode::Destroy(unsigned int flags)
{
    UnlinkAs(kMsgHandlerNodeVtable);
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00457260
void HandlerLists::Destroy()
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
        Free(this);
    return this;
}

// 0x00455BD0
ConnNode* ConnNode::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00457070
void BannedConnNode::DestroyBody() { UnlinkAs(kBannedConnNodeVtable); }

// 0x004570D0
BannedConnNode* BannedConnNode::Destroy(unsigned int flags)
{
    UnlinkAs(kBannedConnNodeVtable);
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00457460
BannedConn* BannedConn::DestroyBanned(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00456B80
void ConnSearch::DestroyBody() { m_vtable = kConnSearchVtable; }

// 0x00456FB0
ConnSearch* ConnSearch::Destroy(unsigned int flags)
{
    m_vtable = kConnSearchVtable;
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00456B90
void BannedConnSearch::DestroyBody() { m_vtable = kBannedConnSearchVtable; }

// 0x00456FE0
BannedConnSearch* BannedConnSearch::Destroy(unsigned int flags)
{
    m_vtable = kBannedConnSearchVtable;
    if (flags & 1)
        Free(this);
    return this;
}

} // namespace Net
