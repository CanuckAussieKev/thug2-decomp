// The net library's queued messages (QueuedMsg, SeqQueuedMsg), the links that queue them on a connection, and
// the Lst::Node<T> destructors of the message lists and the Manager's app lists. See net_messages.h.

#include "game/net/net_messages.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app_view.h"
#include "game/net/net_compress.h"
#include "game/core/code_address.h"

namespace Net {

// The queue links' and app lists' vtables (each holds only the scalar deleting destructor).
static VtableSlot kStreamLinkNodeVtable_slots[] = {&Net::StreamLinkNode::Destroy};
void** const kStreamLinkNodeVtable = reinterpret_cast<void**>(kStreamLinkNodeVtable_slots);
static VtableSlot kMsgNormalNodeVtable_slots[] = {&Net::MsgNormalNode::Destroy};
void** const kMsgNormalNodeVtable = reinterpret_cast<void**>(kMsgNormalNodeVtable_slots);
static VtableSlot kMsgImpNodeVtable_slots[] = {&Net::MsgImpNode::Destroy};
void** const kMsgImpNodeVtable = reinterpret_cast<void**>(kMsgImpNodeVtable_slots);
static VtableSlot kMsgSeqNodeVtable_slots[] = {&Net::MsgSeqNode::Destroy};
void** const kMsgSeqNodeVtable = reinterpret_cast<void**>(kMsgSeqNodeVtable_slots);
static VtableSlot kAppListNodeVtable_slots[] = {&Net::AppNode::Destroy};
void** const kAppListNodeVtable = reinterpret_cast<void**>(kAppListNodeVtable_slots);
static VtableSlot kAppListHeadVtable_slots[] = {&Net::AppNode::DestroyHead};
void** const kAppListHeadVtable = reinterpret_cast<void**>(kAppListHeadVtable_slots);
static VtableSlot kMsgNormalLinkVtable_slots[] = {&Net::MsgNormalLink::Destroy};
void** const kMsgNormalLinkVtable = reinterpret_cast<void**>(kMsgNormalLinkVtable_slots);
static VtableSlot kMsgImpLinkVtable_slots[] = {&Net::MsgImpLink::Destroy};
void** const kMsgImpLinkVtable = reinterpret_cast<void**>(kMsgImpLinkVtable_slots);
static VtableSlot kMsgSeqLinkVtable_slots[] = {&Net::MsgSeqLink::Destroy};
void** const kMsgSeqLinkVtable = reinterpret_cast<void**>(kMsgSeqLinkVtable_slots);
static VtableSlot kStreamLinkVtable_slots[] = {&Net::StreamLink::Destroy};
void** const kStreamLinkVtable = reinterpret_cast<void**>(kStreamLinkVtable_slots);

namespace {

void ReleaseMemory(void* memory) { Mem::Manager::Get()->Free(memory); }

// `data` compressed with `method` (1 RLE, 2 LZSS) into a new block that starts with a message header; null (and
// nothing changed) if that isn't smaller. Else *id = 0x12 and *length = its size. (net_compress.cpp)
char* CompressMessage(uint8_t* id, uint16_t* length, void* data, uint8_t method)
{
    return reinterpret_cast<char*>(Net::CompressMessage(id, length, data, static_cast<char>(method)));
}

// The data a queued message keeps: a copy, compressed if asked and smaller (then the id and length change).
char* CopyData(uint8_t* id, uint16_t* length, void* data, uint8_t compression)
{
    char* compressed = nullptr;
    if (compression) {
        compressed = CompressMessage(id, length, data, compression);
        if (compressed)
            data = compressed;
    }
    auto* copy = static_cast<char*>(Mem::New(*length));
    std::memcpy(copy, data, *length);
    if (compressed)
        ReleaseMemory(compressed);
    return copy;
}

void InitNode(Mem::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}

// -> the links' destructor bodies (Lst::Node<T>'s, Conn's range: wt/netF)

} // namespace

// --- Queued messages -----------------------------------------------------------------------------------------

// 0x00454FA0
QueuedMsg* QueuedMsg::Construct(uint8_t id, uint16_t length, void* data, uint8_t compression)
{
    m_refCount = 0;
    m_data = nullptr;
    if (length > 0)
        m_data = CopyData(&id, &length, data, compression);
    m_id = id;
    m_length = length;
    return this;
}

// 0x00455040
SeqQueuedMsg* SeqQueuedMsg::Construct(uint8_t id, uint16_t length, void* data, uint8_t byte8, uint8_t compression)
{
    m_refCount = 0;
    m_data = nullptr;
    if (length > 0)
        m_data = CopyData(&id, &length, data, compression);
    m_id = id;
    m_length = length;
    m_byte8 = byte8;
    return this;
}

// 0x004550F0
void QueuedMsg::DestroyBody()
{
    if (m_data)
        ReleaseMemory(m_data);
}

// --- Links -----------------------------------------------------------------------------------------------------

// 0x004555F0
MsgNormalLink* MsgNormalLink::Construct(QueuedMsg* msg)
{
    InitNode(&m_node, kMsgNormalLinkVtable);
    m_QMsg = msg;
    m_SendTime = 0;
    return this;
}

// 0x00455620
MsgImpLink* MsgImpLink::Construct(QueuedMsg* msg)
{
    InitNode(&m_node, kMsgImpLinkVtable);
    m_QMsg = msg;
    m_SendTime = 0;
    m_NumResends = 0;
    return this;
}

// 0x00455650
MsgSeqLink* MsgSeqLink::Construct(SeqQueuedMsg* msg)
{
    InitNode(&m_node, kMsgSeqLinkVtable);
    m_StreamMessage = 0;
    m_SendTime = 0;
    m_QMsg = msg;
    m_NumResends = 0;
    return this;
}

// 0x00455680
StreamLink* StreamLink::Construct(StreamDesc* desc)
{
    InitNode(&m_node, kStreamLinkVtable);
    m_Desc = desc;
    return this;
}

// 0x004557C0
MsgNormalLink* MsgNormalLink::Destroy(unsigned int flags)
{
    reinterpret_cast<MsgNormalNode*>(&m_node)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004557F0
MsgImpLink* MsgImpLink::Destroy(unsigned int flags)
{
    reinterpret_cast<MsgImpNode*>(&m_node)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455820
MsgSeqLink* MsgSeqLink::Destroy(unsigned int flags)
{
    reinterpret_cast<MsgSeqNode*>(&m_node)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455850
StreamLink* StreamLink::Destroy(unsigned int flags)
{
    reinterpret_cast<StreamLinkNode*>(&m_node)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// --- Lst::Node<T> --------------------------------------------------------------------------------------------

// 0x00455130
StreamLinkNode* StreamLinkNode::Destroy(unsigned int flags)
{
    UnlinkAs(kStreamLinkNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455170
MsgNormalNode* MsgNormalNode::Destroy(unsigned int flags)
{
    UnlinkAs(kMsgNormalNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004551B0
MsgImpNode* MsgImpNode::Destroy(unsigned int flags)
{
    UnlinkAs(kMsgImpNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004551F0
MsgSeqNode* MsgSeqNode::Destroy(unsigned int flags)
{
    UnlinkAs(kMsgSeqNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455110
void AppNode::DestroyBody() { UnlinkAs(kAppListNodeVtable); }

// 0x00455230
AppNode* AppNode::Destroy(unsigned int flags)
{
    UnlinkAs(kAppListNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004556B0
AppNode* AppNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

} // namespace Net
