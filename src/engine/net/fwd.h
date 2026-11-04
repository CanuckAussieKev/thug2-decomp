#pragma once

// NOT ORIGINAL - forward declarations of the networking subsystem's player and message types, for files that only name
// them by pointer or reference (include the real header where a member is used).
namespace net {
struct InboundMsgCtx;
} // namespace net
namespace online {
class PlayerRecord;
} // namespace online
