// The network library's small message structures: NetMessageInfo (net_app.h), StreamDesc (net_app.h) and
// InboundMsgCtx (net_dispatcher.h) - their constructors and StreamDesc's destructor.

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"

namespace NetLink {

// 0x004549F0
NetMessageInfo* NetMessageInfo::MakeNew()
{
    m_ident = 0;
    m_feed = 0;
    m_byteCount = 0;
    m_content = nullptr;
    m_rank = 0x80;
    m_queue = kLaneNormal;
    m_groupId = 0;
    m_singular = false;
    m_delay = 0;
    m_fixedSeqNo = 0;
    m_compression = 0;
    return this;
}

// 0x004549D0
StreamDesc* StreamDesc::MakeNew()
{
    m_extent = 0;
    m_content = nullptr;
    m_succ = nullptr;
    m_groupId = 0;
    m_borrowedPayload = false;
    return this;
}

// 0x00454E00
void StreamDesc::DestroyBody()
{
    if (m_content && !m_borrowedPayload) {
        DebugOut::Printf("Destroying StreamDesc's Data\n");
        MemSys::Dealloc(m_content);
    }
}

// 0x00454A20
InboundMsgCtx* InboundMsgCtx::MakeNew()
{
    m_body = nullptr;
    m_typeCode = 0;
    m_bodySize = 0;
    m_receiverApp = nullptr;
    m_senderConn = nullptr;
    m_wireOptions = 0;
    m_content = nullptr;
    m_NumSequencesToAck = 0;
    m_Acknowledge = false;
    for (uint32_t& sequence : m_SequencesToAck)
        sequence = 0;
    return this;
}

} // namespace Net
