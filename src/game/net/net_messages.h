#pragma once

// The net library's queued messages and their links (the types: PendingMsg / SeqQueuedMsg in net_app_view.h,
// the links in net_access_c.h; their functions in net_messages.cpp), and the ListKit::Elem<T> types of the message
// lists and of the Manager's app lists: each has its own vtable holding only its scalar deleting destructor.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace NetLink {

struct PendingMsg;
struct SeqQueuedMsg;
struct StreamDesc;

extern void** const kStreamLinkNodeVtable; // ListKit::Elem<BulkStreamNode>
extern void** const kMsgNormalNodeVtable;  // ListKit::Elem<MsgNormalLink>
extern void** const kMsgImpNodeVtable;     // ListKit::Elem<ReliableMsgNode>
extern void** const kMsgSeqNodeVtable;     // ListKit::Elem<OrderedMsgNode>
extern void** const kAppListNodeVtable;        // ListKit::Elem<App>
extern void** const kAppListHeadVtable;        // ListKit::Foremost<App>
extern void** const kMsgNormalLinkVtable;
extern void** const kMsgImpLinkVtable;
extern void** const kMsgSeqLinkVtable;
extern void** const kStreamLinkVtable;

// The queues' links (ListKit::Elem<T>: the node, then the link's fields).
struct MsgNormalLink {
    // 0x004555F0 (vtable 0x00647578; send time 0)
    MsgNormalLink* MakeNew(PendingMsg* msg);
    // 0x004557C0: the scalar deleting destructor.
    MsgNormalLink* Teardown(unsigned int flags);

    MemSys::ListNode m_listLink;
    PendingMsg* m_sharedMsg;
    uint32_t m_dueTime;           // not before this time
};
struct ReliableMsgNode {
    // 0x00455620 (vtable 0x0064757C; no resends, send time 0 - the timestamps aren't set)
    ReliableMsgNode* MakeNew(PendingMsg* msg);
    // 0x004557F0: the scalar deleting destructor.
    ReliableMsgNode* Teardown(unsigned int flags);

    MemSys::ListNode m_listLink;
    uint32_t m_stampMs;          // when it was last sent (0: never)
    PendingMsg* m_sharedMsg;
    uint32_t m_packetClock;        // the connection's timestamp it went out with (the ack names it)
    uint8_t m_retryCount;
    uint8_t pad21[3];
    uint32_t m_dueTime;
};
struct OrderedMsgNode {
    // 0x00455650 (vtable 0x00647580; no resends, +0x16 and send time 0 - the group, the timestamps and the
    // sequence id aren't set)
    OrderedMsgNode* MakeNew(SeqQueuedMsg* msg);
    // 0x00455820: the scalar deleting destructor.
    OrderedMsgNode* Teardown(unsigned int flags);

    MemSys::ListNode m_listLink;          // priority: the sequence number
    uint8_t m_retryCount;
    uint8_t m_bundleId;
    uint8_t m_bulkMsg;       // a piece of a streamed message
    uint8_t pad17;
    uint32_t m_packetClock;
    uint32_t m_stampMs;          // when it was last sent (0: never)
    uint32_t m_orderIndex;
    uint32_t m_dueTime;
    SeqQueuedMsg* m_sharedMsg;
};
static_assert(offsetof(ReliableMsgNode, m_dueTime) == 0x24 && offsetof(OrderedMsgNode, m_sharedMsg) == 0x28 &&
              offsetof(MsgNormalLink, m_dueTime) == 0x18 && sizeof(OrderedMsgNode) == 0x2C);

// A stream being received in pieces: StreamDesc (net_app.h); its link (0x18).
struct BulkStreamNode {
    // 0x00455680 (vtable 0x00647584)
    BulkStreamNode* MakeNew(StreamDesc* desc);
    // 0x00455850: the scalar deleting destructor.
    BulkStreamNode* Teardown(unsigned int flags);

    MemSys::ListNode m_listLink;
    StreamDesc* m_msgMeta;
};
static_assert(sizeof(BulkStreamNode) == 0x18);

// The connection's queue heads (and the links' base) - ListKit::Elem<T> of each link type. (The destructor bodies,
// 0x00455B50-0x00455BB0, are in Conn's range: net_conn.cpp.)
struct StreamLinkNode : MemSys::ListNode {
    // 0x00455BB0: the destructor body: out of its list (and linked to itself).
    void DestroyBody();
    // 0x00455130: the scalar deleting destructor.
    StreamLinkNode* Teardown(unsigned int flags);
};
struct MsgNormalNode : MemSys::ListNode {
    // 0x00455B50: the destructor body.
    void DestroyBody();
    // 0x00455170
    MsgNormalNode* Teardown(unsigned int flags);
};
struct MsgImpNode : MemSys::ListNode {
    // 0x00455B70: the destructor body.
    void DestroyBody();
    // 0x004551B0
    MsgImpNode* Teardown(unsigned int flags);
};
struct MsgSeqNode : MemSys::ListNode {
    // 0x00455B90: the destructor body (also each of Conn's received queues', from its destructor).
    void DestroyBody();
    // 0x004551F0
    MsgSeqNode* Teardown(unsigned int flags);
};

// An App's node in its Manager's server or client list (App +0x1414), and those lists' heads.
struct AppNode : MemSys::ListNode {
    // 0x00455110: the destructor body: out of its list.
    void DestroyBody();
    // 0x00455230: the scalar deleting destructor.
    AppNode* Teardown(unsigned int flags);
    // 0x004556B0: ListKit::Foremost<App>'s scalar deleting destructor.
    AppNode* DestroyHead(unsigned int flags);
};

} // namespace Net
