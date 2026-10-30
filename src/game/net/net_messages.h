#pragma once

// The net library's queued messages and their links (the types: QueuedMsg / SeqQueuedMsg in net_app_view.h,
// the links in net_access_c.h; their functions in net_messages.cpp), and the Lst::Node<T> types of the message
// lists and of the Manager's app lists: each has its own vtable holding only its scalar deleting destructor.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Net {

struct QueuedMsg;
struct SeqQueuedMsg;
struct StreamDesc;

extern void** const kStreamLinkNodeVtable; // Lst::Node<StreamLink>
extern void** const kMsgNormalNodeVtable;  // Lst::Node<MsgNormalLink>
extern void** const kMsgImpNodeVtable;     // Lst::Node<MsgImpLink>
extern void** const kMsgSeqNodeVtable;     // Lst::Node<MsgSeqLink>
extern void** const kAppListNodeVtable;        // Lst::Node<App>
extern void** const kAppListHeadVtable;        // Lst::Head<App>
extern void** const kMsgNormalLinkVtable;
extern void** const kMsgImpLinkVtable;
extern void** const kMsgSeqLinkVtable;
extern void** const kStreamLinkVtable;

// The queues' links (Lst::Node<T>: the node, then the link's fields).
struct MsgNormalLink {
    // 0x004555F0 (vtable 0x00647578; send time 0)
    MsgNormalLink* Construct(QueuedMsg* msg);
    // 0x004557C0: the scalar deleting destructor.
    MsgNormalLink* Destroy(unsigned int flags);

    Mem::ListNode m_node;
    QueuedMsg* m_QMsg;
    uint32_t m_SendTime;           // not before this time
};
struct MsgImpLink {
    // 0x00455620 (vtable 0x0064757C; no resends, send time 0 - the timestamps aren't set)
    MsgImpLink* Construct(QueuedMsg* msg);
    // 0x004557F0: the scalar deleting destructor.
    MsgImpLink* Destroy(unsigned int flags);

    Mem::ListNode m_node;
    uint32_t m_Timestamp;          // when it was last sent (0: never)
    QueuedMsg* m_QMsg;
    uint32_t m_Packetstamp;        // the connection's timestamp it went out with (the ack names it)
    uint8_t m_NumResends;
    uint8_t pad21[3];
    uint32_t m_SendTime;
};
struct MsgSeqLink {
    // 0x00455650 (vtable 0x00647580; no resends, +0x16 and send time 0 - the group, the timestamps and the
    // sequence id aren't set)
    MsgSeqLink* Construct(SeqQueuedMsg* msg);
    // 0x00455820: the scalar deleting destructor.
    MsgSeqLink* Destroy(unsigned int flags);

    Mem::ListNode m_node;          // priority: the sequence number
    uint8_t m_NumResends;
    uint8_t m_GroupId;
    uint8_t m_StreamMessage;       // a piece of a streamed message
    uint8_t pad17;
    uint32_t m_Packetstamp;
    uint32_t m_Timestamp;          // when it was last sent (0: never)
    uint32_t m_SequenceId;
    uint32_t m_SendTime;
    SeqQueuedMsg* m_QMsg;
};
static_assert(offsetof(MsgImpLink, m_SendTime) == 0x24 && offsetof(MsgSeqLink, m_QMsg) == 0x28 &&
              offsetof(MsgNormalLink, m_SendTime) == 0x18 && sizeof(MsgSeqLink) == 0x2C);

// A stream being received in pieces: StreamDesc (net_app.h); its link (0x18).
struct StreamLink {
    // 0x00455680 (vtable 0x00647584)
    StreamLink* Construct(StreamDesc* desc);
    // 0x00455850: the scalar deleting destructor.
    StreamLink* Destroy(unsigned int flags);

    Mem::ListNode m_node;
    StreamDesc* m_Desc;
};
static_assert(sizeof(StreamLink) == 0x18);

// The connection's queue heads (and the links' base) - Lst::Node<T> of each link type. (The destructor bodies,
// 0x00455B50-0x00455BB0, are in Conn's range: net_conn.cpp.)
struct StreamLinkNode : Mem::ListNode {
    // 0x00455BB0: the destructor body: out of its list (and linked to itself).
    void DestroyBody();
    // 0x00455130: the scalar deleting destructor.
    StreamLinkNode* Destroy(unsigned int flags);
};
struct MsgNormalNode : Mem::ListNode {
    // 0x00455B50: the destructor body.
    void DestroyBody();
    // 0x00455170
    MsgNormalNode* Destroy(unsigned int flags);
};
struct MsgImpNode : Mem::ListNode {
    // 0x00455B70: the destructor body.
    void DestroyBody();
    // 0x004551B0
    MsgImpNode* Destroy(unsigned int flags);
};
struct MsgSeqNode : Mem::ListNode {
    // 0x00455B90: the destructor body (also each of Conn's received queues', from its destructor).
    void DestroyBody();
    // 0x004551F0
    MsgSeqNode* Destroy(unsigned int flags);
};

// An App's node in its Manager's server or client list (App +0x1414), and those lists' heads.
struct AppNode : Mem::ListNode {
    // 0x00455110: the destructor body: out of its list.
    void DestroyBody();
    // 0x00455230: the scalar deleting destructor.
    AppNode* Destroy(unsigned int flags);
    // 0x004556B0: Lst::Head<App>'s scalar deleting destructor.
    AppNode* DestroyHead(unsigned int flags);
};

} // namespace Net
