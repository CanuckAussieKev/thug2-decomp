// The network library's small message structures: NetMessageInfo (net_app.h), StreamDesc (net_app.h) and
// InboundMsgCtx (net_dispatcher.h) - their constructors and StreamDesc's destructor.

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"

namespace net {

// 0x004549F0
NetMessageInfo::NetMessageInfo() = default;

// 0x004549D0
StreamDesc::StreamDesc() = default;

// 0x00454E00
StreamDesc::~StreamDesc()
{
    if (content_ && !borrowed_payload_) {
        debug::printf("Destroying StreamDesc's Data\n");
        memory::dealloc(content_);
    }
}

// 0x00454A20
InboundMsgCtx::InboundMsgCtx()
{
    for (uint32_t& sequence : sequences_to_ack_)
        sequence = 0;
}

} // namespace net
