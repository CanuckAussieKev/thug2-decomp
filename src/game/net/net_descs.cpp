// The network library's small message structures: MsgDesc (net_app.h), StreamDesc (net_app.h) and
// MsgHandlerContext (net_dispatcher.h) - their constructors and StreamDesc's destructor.

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"

namespace Net {

// 0x004549F0
MsgDesc* MsgDesc::Construct()
{
    m_id = 0;
    m_stream = 0;
    m_length = 0;
    m_data = nullptr;
    m_priority = 0x80;
    m_queue = kQueueDefault;
    m_groupId = 0;
    m_singular = false;
    m_delay = 0;
    m_forcedSequenceId = 0;
    m_compression = 0;
    return this;
}

// 0x004549D0
StreamDesc* StreamDesc::Construct()
{
    m_size = 0;
    m_data = nullptr;
    m_next = nullptr;
    m_groupId = 0;
    m_sendInPlace = false;
    return this;
}

// 0x00454E00
void StreamDesc::DestroyBody()
{
    if (m_data && !m_sendInPlace) {
        Dbg::Printf("Destroying StreamDesc's Data\n");
        Mem::Free(m_data);
    }
}

// 0x00454A20
MsgHandlerContext* MsgHandlerContext::Construct()
{
    m_Msg = nullptr;
    m_MsgId = 0;
    m_MsgLength = 0;
    m_App = nullptr;
    m_Conn = nullptr;
    m_PacketFlags = 0;
    m_data = nullptr;
    m_NumSequencesToAck = 0;
    m_Acknowledge = false;
    for (uint32_t& sequence : m_SequencesToAck)
        sequence = 0;
    return this;
}

} // namespace Net
