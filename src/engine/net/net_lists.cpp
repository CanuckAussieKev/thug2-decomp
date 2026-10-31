// The net library's list types' destructors. See net_lists.h.

#include "engine/net/net_lists.h"

#include "engine/core/manager.h"
#include "engine/core/code_address.h"

namespace net {

static VtableSlot g_conn_node_vtable_slots[] = {&net::ConnNode::teardown};
void** const kConnNodeVtable = reinterpret_cast<void**>(g_conn_node_vtable_slots);
static VtableSlot g_conn_head_vtable_slots[] = {&net::ConnNode::destroy_head};
void** const kConnHeadVtable = reinterpret_cast<void**>(g_conn_head_vtable_slots);
static VtableSlot g_banned_conn_node_vtable_slots[] = {&net::BannedConnNode::teardown};
void** const kBannedConnNodeVtable = reinterpret_cast<void**>(g_banned_conn_node_vtable_slots);
static VtableSlot g_banned_conn_vtable_slots[] = {&net::BlockedConnEntry::destroy_banned};
void** const kBannedConnVtable = reinterpret_cast<void**>(g_banned_conn_vtable_slots);
static VtableSlot g_msg_handler_node_vtable_slots[] = {&net::MsgHandlerNode::teardown};
void** const kMsgHandlerNodeVtable = reinterpret_cast<void**>(g_msg_handler_node_vtable_slots);
static VtableSlot g_conn_search_vtable_slots[] = {&net::ConnSearch::teardown};
void** const kConnSearchVtable = reinterpret_cast<void**>(g_conn_search_vtable_slots);
static VtableSlot g_banned_conn_search_vtable_slots[] = {&net::BannedConnSearch::teardown};
void** const kBannedConnSearchVtable = reinterpret_cast<void**>(g_banned_conn_search_vtable_slots);

namespace {

void dealloc(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

} // namespace

// 0x00457010
MsgHandlerNode* MsgHandlerNode::construct_head()
{
    data = reinterpret_cast<void*>(-1);
    priority = 0;
    next = this;
    prev = this;
    vtable = kMsgHandlerHeadVtable;
    return this;
}

// 0x00457030
void MsgHandlerNode::destroy_body() { unlink_as(kMsgHandlerNodeVtable); }

// 0x00457090
MsgHandlerNode* MsgHandlerNode::teardown(unsigned int flags)
{
    unlink_as(kMsgHandlerNodeVtable);
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00457260
void HandlerLists::teardown()
{
    for (int i = kCount - 1; i >= 0; --i)
        lists_[i].destroy_body();
}

// 0x00457050
void ConnNode::destroy_body() { unlink_as(kConnNodeVtable); }

// 0x004573A0
ConnNode* ConnNode::destroy_head(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00455BD0
ConnNode* ConnNode::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00457070
void BannedConnNode::destroy_body() { unlink_as(kBannedConnNodeVtable); }

// 0x004570D0
BannedConnNode* BannedConnNode::teardown(unsigned int flags)
{
    unlink_as(kBannedConnNodeVtable);
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00457460
BlockedConnEntry* BlockedConnEntry::destroy_banned(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00456B80
void ConnSearch::destroy_body() { vtable_ = kConnSearchVtable; }

// 0x00456FB0
ConnSearch* ConnSearch::teardown(unsigned int flags)
{
    vtable_ = kConnSearchVtable;
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00456B90
void BannedConnSearch::destroy_body() { vtable_ = kBannedConnSearchVtable; }

// 0x00456FE0
BannedConnSearch* BannedConnSearch::teardown(unsigned int flags)
{
    vtable_ = kBannedConnSearchVtable;
    if (flags & 1)
        dealloc(this);
    return this;
}

} // namespace net
