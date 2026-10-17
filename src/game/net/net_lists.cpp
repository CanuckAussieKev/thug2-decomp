// The net library's list types' destructors. See net_lists.h.

#include "game/net/net_lists.h"

#include "game/core/manager.h"
#include "hook/hook.h"

namespace Net {

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

REPLACE(0x00457010, &Net::MsgHandlerNode::ConstructHead);
REPLACE(0x00457030, &Net::MsgHandlerNode::DestroyBody);
REPLACE(0x00457090, &Net::MsgHandlerNode::Destroy);
REPLACE(0x00457260, &Net::HandlerLists::Destroy);
REPLACE(0x00457050, &Net::ConnNode::DestroyBody);
REPLACE(0x004573A0, &Net::ConnNode::DestroyHead);
REPLACE(0x00455BD0, &Net::ConnNode::Destroy);
REPLACE(0x00457070, &Net::BannedConnNode::DestroyBody);
REPLACE(0x004570D0, &Net::BannedConnNode::Destroy);
REPLACE(0x00457460, &Net::BannedConn::DestroyBanned);
REPLACE(0x00456B80, &Net::ConnSearch::DestroyBody);
REPLACE(0x00456FB0, &Net::ConnSearch::Destroy);
REPLACE(0x00456B90, &Net::BannedConnSearch::DestroyBody);
REPLACE(0x00456FE0, &Net::BannedConnSearch::Destroy);
