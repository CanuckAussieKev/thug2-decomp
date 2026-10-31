// The net library's queued messages (PendingMsg, SeqQueuedMsg), the links that queue them on a connection, and
// the ListKit::Elem<T> destructors of the message lists and the Manager's app lists. See net_messages.h.

#include "engine/net/net_messages.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_compress.h"
#include "engine/core/code_address.h"

namespace NetLink {

// The queue links' and app lists' vtables (each holds only the scalar deleting destructor).
static VtableSlot kStreamLinkNodeVtable_slots[] = {&NetLink::StreamLinkNode::Teardown};
void** const kStreamLinkNodeVtable = reinterpret_cast<void**>(kStreamLinkNodeVtable_slots);
static VtableSlot kMsgNormalNodeVtable_slots[] = {&NetLink::MsgNormalNode::Teardown};
void** const kMsgNormalNodeVtable = reinterpret_cast<void**>(kMsgNormalNodeVtable_slots);
static VtableSlot kMsgImpNodeVtable_slots[] = {&NetLink::MsgImpNode::Teardown};
void** const kMsgImpNodeVtable = reinterpret_cast<void**>(kMsgImpNodeVtable_slots);
static VtableSlot kMsgSeqNodeVtable_slots[] = {&NetLink::MsgSeqNode::Teardown};
void** const kMsgSeqNodeVtable = reinterpret_cast<void**>(kMsgSeqNodeVtable_slots);
static VtableSlot kAppListNodeVtable_slots[] = {&NetLink::AppNode::Teardown};
void** const kAppListNodeVtable = reinterpret_cast<void**>(kAppListNodeVtable_slots);
static VtableSlot kAppListHeadVtable_slots[] = {&NetLink::AppNode::DestroyHead};
void** const kAppListHeadVtable = reinterpret_cast<void**>(kAppListHeadVtable_slots);
static VtableSlot kMsgNormalLinkVtable_slots[] = {&NetLink::MsgNormalLink::Teardown};
void** const kMsgNormalLinkVtable = reinterpret_cast<void**>(kMsgNormalLinkVtable_slots);
static VtableSlot kMsgImpLinkVtable_slots[] = {&NetLink::ReliableMsgNode::Teardown};
void** const kMsgImpLinkVtable = reinterpret_cast<void**>(kMsgImpLinkVtable_slots);
static VtableSlot kMsgSeqLinkVtable_slots[] = {&NetLink::OrderedMsgNode::Teardown};
void** const kMsgSeqLinkVtable = reinterpret_cast<void**>(kMsgSeqLinkVtable_slots);
static VtableSlot kStreamLinkVtable_slots[] = {&NetLink::BulkStreamNode::Teardown};
void** const kStreamLinkVtable = reinterpret_cast<void**>(kStreamLinkVtable_slots);

namespace {

void ReleaseMemory(void* memory) { MemSys::Hub::Pull()->Dealloc(memory); }

// `data` compressed with `method` (1 RLE, 2 LZSS) into a new block that starts with a message header; null (and
// nothing changed) if that isn't smaller. Else *id = 0x12 and *length = its size. (net_compress.cpp)
char* CompressMessage(uint8_t* id, uint16_t* length, void* data, uint8_t method)
{
    return reinterpret_cast<char*>(NetLink::CompressMessage(id, length, data, static_cast<char>(method)));
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
    auto* copy = static_cast<char*>(MemSys::Alloc(*length));
    std::memcpy(copy, data, *length);
    if (compressed)
        ReleaseMemory(compressed);
    return copy;
}

void InitNode(MemSys::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}

// -> the links' destructor bodies (ListKit::Elem<T>'s, Conn's range: wt/netF)

} // namespace

// --- Queued messages -----------------------------------------------------------------------------------------

// 0x00454FA0
PendingMsg* PendingMsg::MakeNew(uint8_t id, uint16_t length, void* data, uint8_t compression)
{
    m_shareCount = 0;
    m_content = nullptr;
    if (length > 0)
        m_content = CopyData(&id, &length, data, compression);
    m_ident = id;
    m_byteCount = length;
    return this;
}

// 0x00455040
SeqQueuedMsg* SeqQueuedMsg::MakeNew(uint8_t id, uint16_t length, void* data, uint8_t byte8, uint8_t compression)
{
    m_shareCount = 0;
    m_content = nullptr;
    if (length > 0)
        m_content = CopyData(&id, &length, data, compression);
    m_ident = id;
    m_byteCount = length;
    m_byte8 = byte8;
    return this;
}

// 0x004550F0
void PendingMsg::DestroyBody()
{
    if (m_content)
        ReleaseMemory(m_content);
}

// --- Links -----------------------------------------------------------------------------------------------------

// 0x004555F0
MsgNormalLink* MsgNormalLink::MakeNew(PendingMsg* msg)
{
    InitNode(&m_listLink, kMsgNormalLinkVtable);
    m_sharedMsg = msg;
    m_dueTime = 0;
    return this;
}

// 0x00455620
ReliableMsgNode* ReliableMsgNode::MakeNew(PendingMsg* msg)
{
    InitNode(&m_listLink, kMsgImpLinkVtable);
    m_sharedMsg = msg;
    m_dueTime = 0;
    m_retryCount = 0;
    return this;
}

// 0x00455650
OrderedMsgNode* OrderedMsgNode::MakeNew(SeqQueuedMsg* msg)
{
    InitNode(&m_listLink, kMsgSeqLinkVtable);
    m_bulkMsg = 0;
    m_dueTime = 0;
    m_sharedMsg = msg;
    m_retryCount = 0;
    return this;
}

// 0x00455680
BulkStreamNode* BulkStreamNode::MakeNew(StreamDesc* desc)
{
    InitNode(&m_listLink, kStreamLinkVtable);
    m_msgMeta = desc;
    return this;
}

// 0x004557C0
MsgNormalLink* MsgNormalLink::Teardown(unsigned int flags)
{
    reinterpret_cast<MsgNormalNode*>(&m_listLink)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004557F0
ReliableMsgNode* ReliableMsgNode::Teardown(unsigned int flags)
{
    reinterpret_cast<MsgImpNode*>(&m_listLink)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455820
OrderedMsgNode* OrderedMsgNode::Teardown(unsigned int flags)
{
    reinterpret_cast<MsgSeqNode*>(&m_listLink)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455850
BulkStreamNode* BulkStreamNode::Teardown(unsigned int flags)
{
    reinterpret_cast<StreamLinkNode*>(&m_listLink)->DestroyBody();
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// --- ListKit::Elem<T> --------------------------------------------------------------------------------------------

// 0x00455130
StreamLinkNode* StreamLinkNode::Teardown(unsigned int flags)
{
    UnlinkAs(kStreamLinkNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455170
MsgNormalNode* MsgNormalNode::Teardown(unsigned int flags)
{
    UnlinkAs(kMsgNormalNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004551B0
MsgImpNode* MsgImpNode::Teardown(unsigned int flags)
{
    UnlinkAs(kMsgImpNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004551F0
MsgSeqNode* MsgSeqNode::Teardown(unsigned int flags)
{
    UnlinkAs(kMsgSeqNodeVtable);
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455110
void AppNode::DestroyBody() { UnlinkAs(kAppListNodeVtable); }

// 0x00455230
AppNode* AppNode::Teardown(unsigned int flags)
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
