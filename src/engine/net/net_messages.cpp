// The net library's queued messages (PendingMsg, SeqQueuedMsg), the links that queue them on a connection, and
// See net_messages.h.

#include "engine/net/net_messages.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_compress.h"

namespace net {

namespace {

void release_memory(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

// `data` compressed with `method` (1 RLE, 2 LZSS) into a new block that starts with a message header; null (and
// nothing changed) if that isn't smaller. Else *id = 0x12 and *length = its size. (net_compress.cpp)
char* compress_message(uint8_t* id, uint16_t* length, const void* data, uint8_t method)
{
    return reinterpret_cast<char*>(net::compress_message(id, length, data, static_cast<char>(method)));
}

// The data a queued message keeps: a copy, compressed if asked and smaller (then the id and length change).
char* copy_data(uint8_t* id, uint16_t* length, const void* data, uint8_t compression)
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

void init_node(memory::ListNode* node) { node->make_node(node); }

} // namespace

// --- Queued messages -----------------------------------------------------------------------------------------

// 0x00454FA0
PendingMsg::PendingMsg(uint8_t id, uint16_t length, const void* data, uint8_t compression)
{
    if (length > 0)
        content_ = copy_data(&id, &length, data, compression);
    ident_ = id;
    byte_count_ = length;
}

// 0x00455040
SeqQueuedMsg::SeqQueuedMsg(uint8_t id, uint16_t length, const void* data, uint8_t byte8, uint8_t compression)
{
    if (length > 0)
        content_ = copy_data(&id, &length, data, compression);
    ident_ = id;
    byte_count_ = length;
    byte8_ = byte8;
}

// 0x004550F0
PendingMsg::~PendingMsg()
{
    if (content_)
        release_memory(content_);
}

// --- Links -----------------------------------------------------------------------------------------------------

// 0x004555F0
MsgNormalLink::MsgNormalLink(PendingMsg* msg) : shared_msg_(msg) { init_node(&list_link_); }

// 0x00455620
ReliableMsgNode::ReliableMsgNode(PendingMsg* msg) : shared_msg_(msg) { init_node(&list_link_); }

// 0x00455650
OrderedMsgNode::OrderedMsgNode(SeqQueuedMsg* msg) : shared_msg_(msg) { init_node(&list_link_); }

// 0x00455680
BulkStreamNode::BulkStreamNode(StreamDesc* desc) : msg_meta_(desc) { init_node(&list_link_); }

// 0x004557C0
MsgNormalLink* MsgNormalLink::teardown(unsigned int flags)
{
    list_link_.uncouple();
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x004557F0
ReliableMsgNode* ReliableMsgNode::teardown(unsigned int flags)
{
    list_link_.uncouple();
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x00455820
OrderedMsgNode* OrderedMsgNode::teardown(unsigned int flags)
{
    list_link_.uncouple();
    if (flags & 1)
        release_memory(this);
    return this;
}

// 0x00455850
BulkStreamNode* BulkStreamNode::teardown(unsigned int flags)
{
    list_link_.uncouple();
    if (flags & 1)
        release_memory(this);
    return this;
}

} // namespace net
