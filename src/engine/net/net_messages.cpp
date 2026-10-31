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

void init_node(memory::ListNode* node) { node->make_node(node); }

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
    init_node(&list_link_);
    shared_msg_ = msg;
    due_time_ = 0;
    return this;
}

// 0x00455620
ReliableMsgNode* ReliableMsgNode::make_new(PendingMsg* msg)
{
    init_node(&list_link_);
    shared_msg_ = msg;
    due_time_ = 0;
    retry_count_ = 0;
    return this;
}

// 0x00455650
OrderedMsgNode* OrderedMsgNode::make_new(SeqQueuedMsg* msg)
{
    init_node(&list_link_);
    bulk_msg_ = 0;
    due_time_ = 0;
    shared_msg_ = msg;
    retry_count_ = 0;
    return this;
}

// 0x00455680
BulkStreamNode* BulkStreamNode::make_new(StreamDesc* desc)
{
    init_node(&list_link_);
    msg_meta_ = desc;
    return this;
}

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
