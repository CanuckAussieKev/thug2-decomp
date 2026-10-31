// The net library's queued messages (PendingMsg, SeqQueuedMsg), the links that queue them on a connection, and
// the lists::Elem<T> destructors of the message lists and the Manager's app lists. See net_messages.h.

#include "engine/net/net_messages.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_compress.h"
#include "engine/core/code_address.h"

namespace net {

// The queue links' and app lists' vtables (each holds only the scalar deleting destructor).
static VtableSlot g_stream_link_node_vtable_slots[] = {&net::StreamLinkNode::teardown};
void** const kStreamLinkNodeVtable = reinterpret_cast<void**>(g_stream_link_node_vtable_slots);
static VtableSlot g_msg_normal_node_vtable_slots[] = {&net::MsgNormalNode::teardown};
void** const kMsgNormalNodeVtable = reinterpret_cast<void**>(g_msg_normal_node_vtable_slots);
static VtableSlot g_msg_imp_node_vtable_slots[] = {&net::MsgImpNode::teardown};
void** const kMsgImpNodeVtable = reinterpret_cast<void**>(g_msg_imp_node_vtable_slots);
static VtableSlot g_msg_seq_node_vtable_slots[] = {&net::MsgSeqNode::teardown};
void** const kMsgSeqNodeVtable = reinterpret_cast<void**>(g_msg_seq_node_vtable_slots);
static VtableSlot g_app_list_node_vtable_slots[] = {&net::AppNode::teardown};
void** const kAppListNodeVtable = reinterpret_cast<void**>(g_app_list_node_vtable_slots);
static VtableSlot g_app_list_head_vtable_slots[] = {&net::AppNode::destroy_head};
void** const kAppListHeadVtable = reinterpret_cast<void**>(g_app_list_head_vtable_slots);
static VtableSlot g_msg_normal_link_vtable_slots[] = {&net::MsgNormalLink::teardown};
void** const kMsgNormalLinkVtable = reinterpret_cast<void**>(g_msg_normal_link_vtable_slots);
static VtableSlot g_msg_imp_link_vtable_slots[] = {&net::ReliableMsgNode::teardown};
void** const kMsgImpLinkVtable = reinterpret_cast<void**>(g_msg_imp_link_vtable_slots);
static VtableSlot g_msg_seq_link_vtable_slots[] = {&net::OrderedMsgNode::teardown};
void** const kMsgSeqLinkVtable = reinterpret_cast<void**>(g_msg_seq_link_vtable_slots);
static VtableSlot g_stream_link_vtable_slots[] = {&net::BulkStreamNode::teardown};
void** const kStreamLinkVtable = reinterpret_cast<void**>(g_stream_link_vtable_slots);

namespace {

void release_memory(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

// `data` compressed with `method` (1 RLE, 2 LZSS) into a new block that starts with a message header; null (and
// nothing changed) if that isn't smaller. Else *id = 0x12 and *length = its size. (net_compress.cpp)
char* compress_message(uint8_t* id, uint16_t* length, void* data, uint8_t method)
{
    return reinterpret_cast<char*>(net::compress_message(id, length, data, static_cast<char>(method)));
}

// The data a queued message keeps: a copy, compressed if asked and smaller (then the id and length change).
char* copy_data(uint8_t* id, uint16_t* length, void* data, uint8_t compression)
{
    char* compressed = nullptr;
    if (compression) {
        compressed = compress_message(id, length, data, compression);
        if (compressed)
            data = compressed;
    }
    auto* copy = static_cast<char*>(memory::alloc(*length));
    std::memcpy(copy, data, *length);
    if (compressed)
        release_memory(compressed);
    return copy;
}

void init_node(memory::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}

// -> the links' destructor bodies (lists::Elem<T>'s, Conn's range: wt/netF)

} // namespace

// --- Queued messages -----------------------------------------------------------------------------------------

// 0x00454FA0
PendingMsg* PendingMsg::make_new(uint8_t id, uint16_t length, void* data, uint8_t compression)
{
    share_count_ = 0;
    content_ = nullptr;
    if (length > 0)
        content_ = copy_data(&id, &length, data, compression);
    ident_ = id;
    byte_count_ = length;
    return this;
}

// 0x00455040
SeqQueuedMsg* SeqQueuedMsg::make_new(uint8_t id, uint16_t length, void* data, uint8_t byte8, uint8_t compression)
{
    share_count_ = 0;
    content_ = nullptr;
    if (length > 0)
        content_ = copy_data(&id, &length, data, compression);
    ident_ = id;
    byte_count_ = length;
    byte8_ = byte8;
    return this;
}

// 0x004550F0
void PendingMsg::destroy_body()
{
    if (content_)
        release_memory(content_);
}

// --- Links -----------------------------------------------------------------------------------------------------

// 0x004555F0
MsgNormalLink* MsgNormalLink::make_new(PendingMsg* msg)
{
    init_node(&list_link_, kMsgNormalLinkVtable);
    shared_msg_ = msg;
    due_time_ = 0;
    return this;
}

// 0x00455620
ReliableMsgNode* ReliableMsgNode::make_new(PendingMsg* msg)
{
    init_node(&list_link_, kMsgImpLinkVtable);
    shared_msg_ = msg;
    due_time_ = 0;
    retry_count_ = 0;
    return this;
}

// 0x00455650
OrderedMsgNode* OrderedMsgNode::make_new(SeqQueuedMsg* msg)
{
    init_node(&list_link_, kMsgSeqLinkVtable);
    bulk_msg_ = 0;
    due_time_ = 0;
    shared_msg_ = msg;
    retry_count_ = 0;
    return this;
}

// 0x00455680
BulkStreamNode* BulkStreamNode::make_new(StreamDesc* desc)
{
    init_node(&list_link_, kStreamLinkVtable);
    msg_meta_ = desc;
    return this;
}

// 0x004557C0
MsgNormalLink* MsgNormalLink::teardown(unsigned int flags)
{
    reinterpret_cast<MsgNormalNode*>(&list_link_)->destroy_body();
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x004557F0
ReliableMsgNode* ReliableMsgNode::teardown(unsigned int flags)
{
    reinterpret_cast<MsgImpNode*>(&list_link_)->destroy_body();
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x00455820
OrderedMsgNode* OrderedMsgNode::teardown(unsigned int flags)
{
    reinterpret_cast<MsgSeqNode*>(&list_link_)->destroy_body();
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x00455850
BulkStreamNode* BulkStreamNode::teardown(unsigned int flags)
{
    reinterpret_cast<StreamLinkNode*>(&list_link_)->destroy_body();
    if (flags & 1)
        release_memory(this);
    return this;
}

// --- lists::Elem<T> --------------------------------------------------------------------------------------------

// 0x00455130
StreamLinkNode* StreamLinkNode::teardown(unsigned int flags)
{
    unlink_as(kStreamLinkNodeVtable);
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x00455170
MsgNormalNode* MsgNormalNode::teardown(unsigned int flags)
{
    unlink_as(kMsgNormalNodeVtable);
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x004551B0
MsgImpNode* MsgImpNode::teardown(unsigned int flags)
{
    unlink_as(kMsgImpNodeVtable);
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x004551F0
MsgSeqNode* MsgSeqNode::teardown(unsigned int flags)
{
    unlink_as(kMsgSeqNodeVtable);
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x00455110
void AppNode::destroy_body() { unlink_as(kAppListNodeVtable); }

// 0x00455230
AppNode* AppNode::teardown(unsigned int flags)
{
    unlink_as(kAppListNodeVtable);
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x004556B0
AppNode* AppNode::destroy_head(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        release_memory(this);
    return this;
}

} // namespace Net
