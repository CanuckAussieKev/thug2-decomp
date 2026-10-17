// The net library's queued messages (QueuedMsg, SeqQueuedMsg), the links that queue them on a connection, and
// the Lst::Node<T> destructors of the message lists and the Manager's app lists. See net_messages.h.

#include "game/net/net_messages.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app_view.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Net {

namespace {

void ReleaseMemory(void* memory) { Mem::Manager::Get()->Free(memory); }

// -> 0x00454E30 (wt/netD): `data` compressed with `method` (1 RLE, 2 LZSS) into a new block that starts with a
// message header; null (and nothing changed) if that isn't smaller. Else *id = 0x12 and *length = its size.
char* CompressMessage(uint8_t* id, uint16_t* length, void* data, uint8_t method)
{
    return GameFunction<char*(__cdecl*)(uint8_t*, uint16_t*, void*, uint32_t)>(0x00454E30)(id, length, data, method);
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
void DestroyLinkBody(void* link, uint32_t address) { GameFunction<void(__thiscall*)(void*)>(address)(link); }

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
    DestroyLinkBody(this, 0x00455B50); // -> ~Lst::Node<MsgNormalLink> (wt/netF)
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x004557F0
MsgImpLink* MsgImpLink::Destroy(unsigned int flags)
{
    DestroyLinkBody(this, 0x00455B70); // -> ~Lst::Node<MsgImpLink> (wt/netF)
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455820
MsgSeqLink* MsgSeqLink::Destroy(unsigned int flags)
{
    DestroyLinkBody(this, 0x00455B90); // -> ~Lst::Node<MsgSeqLink> (wt/netF)
    if (flags & 1)
        ReleaseMemory(this);
    return this;
}

// 0x00455850
StreamLink* StreamLink::Destroy(unsigned int flags)
{
    DestroyLinkBody(this, 0x00455BB0); // -> ~Lst::Node<StreamLink> (wt/netF)
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

REPLACE(0x00454FA0, &Net::QueuedMsg::Construct);
REPLACE(0x00455040, &Net::SeqQueuedMsg::Construct);
REPLACE(0x004550F0, &Net::QueuedMsg::DestroyBody);
REPLACE(0x004555F0, &Net::MsgNormalLink::Construct);
REPLACE(0x00455620, &Net::MsgImpLink::Construct);
REPLACE(0x00455650, &Net::MsgSeqLink::Construct);
REPLACE(0x00455680, &Net::StreamLink::Construct);
REPLACE(0x004557C0, &Net::MsgNormalLink::Destroy);
REPLACE(0x004557F0, &Net::MsgImpLink::Destroy);
REPLACE(0x00455820, &Net::MsgSeqLink::Destroy);
REPLACE(0x00455850, &Net::StreamLink::Destroy);
REPLACE(0x00455130, &Net::StreamLinkNode::Destroy);
REPLACE(0x00455170, &Net::MsgNormalNode::Destroy);
REPLACE(0x004551B0, &Net::MsgImpNode::Destroy);
REPLACE(0x004551F0, &Net::MsgSeqNode::Destroy);
REPLACE(0x00455110, &Net::AppNode::DestroyBody);
REPLACE(0x00455230, &Net::AppNode::Destroy);
REPLACE(0x004556B0, &Net::AppNode::DestroyHead);
