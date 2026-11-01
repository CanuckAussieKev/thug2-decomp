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
NetMessageInfo::NetMessageInfo()
{
    ident_ = 0;
    feed_ = 0;
    byte_count_ = 0;
    content_ = nullptr;
    rank_ = 0x80;
    queue_ = kLaneNormal;
    group_id_ = 0;
    singular_ = false;
    delay_ = 0;
    fixed_seq_no_ = 0;
    compression_ = 0;
}

// 0x004549D0
StreamDesc::StreamDesc()
{
    extent_ = 0;
    content_ = nullptr;
    succ_ = nullptr;
    group_id_ = 0;
    borrowed_payload_ = false;
}

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
    body_ = nullptr;
    type_code_ = 0;
    body_size_ = 0;
    receiver_app_ = nullptr;
    sender_conn_ = nullptr;
    wire_options_ = 0;
    content_ = nullptr;
    num_sequences_to_ack_ = 0;
    acknowledge_ = false;
    for (uint32_t& sequence : sequences_to_ack_)
        sequence = 0;
}

} // namespace net
