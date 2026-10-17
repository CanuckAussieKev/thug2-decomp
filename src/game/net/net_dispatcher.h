#pragma once

// The network library's message dispatcher (Net::Dispatcher, an App's +0x0C): handler lists per message id,
// and the context a handler gets.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/lst/head.h"

namespace Net {

class App;
class Conn;

// What a handler returns.
enum HandlerResult : int {
    kHandlerError = 0,         // stop: the rest of the packet is dropped
    kHandlerContinue = 1,      // on to the next handler
    kHandlerHalt = 2,          // stop: the rest of the packet is dropped
    kHandlerMsgDestroyed = 3,  // stop: the connection is gone (nothing of it may be touched)
    kHandlerMsgDone = 4,       // no more handlers for this message; the packet goes on
    kHandlerKeep = 5,          // no more handlers; a streamed message's buffers are kept
};

// Packet flags (MsgHandlerContext::m_PacketFlags; a handler gets a message when its flags include them all).
constexpr uint32_t kPacketForeign = 0x1;            // from an address that isn't a connection
constexpr uint32_t kPacketLate = 0x2;               // its timestamp is older than the newest
constexpr uint32_t kPacketChecksumMismatch = 0x4;   // a stream whose checksum doesn't match

// What a handler gets (0xA4 bytes, constructor 0x00454A20).
struct MsgHandlerContext {
    char* m_Msg;
    uint8_t m_MsgId;
    uint8_t pad5[3];
    uint32_t m_MsgLength;
    App* m_App;
    Conn* m_Conn;
    uint32_t m_PacketFlags;
    void* m_Data;                       // the handler's own data (MsgHandler::m_Data)
    uint32_t m_SequencesToAck[32];      // the timestamps to acknowledge after the packet (low 16 bits)
    int m_NumSequencesToAck;
    bool m_Acknowledge;                 // send those acks
    uint8_t padA1[3];
};
static_assert(offsetof(MsgHandlerContext, m_SequencesToAck) == 0x1C &&
              offsetof(MsgHandlerContext, m_NumSequencesToAck) == 0x9C && sizeof(MsgHandlerContext) == 0xA4);

using MsgHandlerCode = int(__cdecl*)(MsgHandlerContext* context);

// A message handler (0x20 bytes, from the network heap): an Lst::Node<MsgHandler> (data: itself, priority: the
// order in its list) with the code, its data and the packet flags it takes.
struct MsgHandler {
    Mem::ListNode m_node;   // vtable 0x006475E8
    MsgHandlerCode m_Code;
    void* m_Data;
    uint32_t m_Flags;

    // 0x0045A590 (scalar deleting destructor: the node unlinks itself)
    MsgHandler* Destroy(unsigned int flags);
};
static_assert(sizeof(MsgHandler) == 0x20);

// The destructors behind Lst::Search<MsgHandler>'s vtable (0x006475E4), like Lst::SearchDestructors.
struct MsgHandlerSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x0045A500 (only resets the vtable)
    void DestroyBody();
    // 0x0045A510 (scalar deleting)
    MsgHandlerSearch* Destroy(unsigned int flags);
};

class Dispatcher {
public:
    static constexpr int kNumMessageIds = 255;

    // 0x0045A640: every handler deleted (each list walked node by node).
    void Init();
    // 0x0045A690: every handler deleted (walked by item).
    void Deinit();
    // 0x0045A5C0: a handler for message `id`, in the network heap.
    MsgHandler* AddHandler(uint8_t id, MsgHandlerCode code, uint32_t flags, void* data, int priority);
    // 0x0045A750: the message's handlers, in order, until one stops; HandlerResult (4 comes back as 1).
    int DispatchMessage(MsgHandlerContext* context);
    // 0x0045A830: every message in the connection's read buffer, then the acks they asked for. 1, or 2 (a
    // handler stopped the packet), or 3 (the connection was destroyed).
    int DispatchMsgHandlers(Conn* conn, uint32_t flags);

    Lst::Head m_handlers[kNumMessageIds];
};
static_assert(sizeof(Dispatcher) == 0x13EC);

} // namespace Net
