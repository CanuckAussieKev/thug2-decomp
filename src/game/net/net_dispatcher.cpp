// The network library's message dispatcher (NetLink::Dispatcher). See net_dispatcher.h.

#include "game/net/net_dispatcher.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app_messages.h"
#include "game/net/net_guards.h"
#include "game/core/code_address.h"

namespace NetLink {

static VtableSlot kMsgHandlerHeadVtable_slots[] = {&NetLink::MsgHandler::Destroy};
void** const kMsgHandlerHeadVtable = reinterpret_cast<void**>(kMsgHandlerHeadVtable_slots);

namespace {

static VtableSlot kMsgHandlerSearchVtable_slots[] = {&NetLink::MsgHandlerSearch::Destroy};
void** const kMsgHandlerSearchVtable = reinterpret_cast<void**>(kMsgHandlerSearchVtable_slots);
constexpr uint32_t kFixedSizeCopy = 0x514; // what a message without a length field copies

void ConstructContext(InboundMsgCtx* context) { context->Construct(); }

} // namespace

// 0x0045A590
MsgHandler* MsgHandler::Destroy(unsigned int flags)
{
    static_cast<MsgHandlerNode*>(&m_listLink)->DestroyBody(); // (ListKit::Node<MsgHandler>: unlinks it)
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0045A500
void MsgHandlerSearch::DestroyBody() { m_vtable = kMsgHandlerSearchVtable; }

// 0x0045A510
MsgHandlerSearch* MsgHandlerSearch::Destroy(unsigned int flags)
{
    m_vtable = kMsgHandlerSearchVtable;
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0045A640
void Dispatcher::Init()
{
    for (int id = 0; id < kNumMessageIds; ++id) {
        MemSys::ListNode* node = List(id).m_listLink.next;
        // (the head check comes before the null check)
        if (node->data == reinterpret_cast<void*>(-1) || !node)
            continue;
        do {
            MemSys::ListNode* next = node->next;
            if (next->data == reinterpret_cast<void*>(-1))
                next = nullptr;
            UnlinkListNode(node);
            node = next;
        } while (node);
    }
}

// 0x0045A690
void Dispatcher::TearDownState()
{
    for (int id = 0; id < kNumMessageIds; ++id) {
        ListKit::Search<MsgHandler> search;
        for (MsgHandler* handler = search.HeadElement(&List(id).m_listLink); handler;) {
            MsgHandler* next = search.NextItem();
            UnlinkListNode(handler);
            handler = next;
        }
    }
}

// 0x0045A5C0
MsgHandler* Dispatcher::AddHandler(uint8_t id, HandlerSlotValue code, uint32_t flags, void* data, int priority)
{
    MemSys::Manager* manager = MemSys::Manager::Get();
    manager->EnterHeapScope(manager->m_networkHeap);
    auto* handler = static_cast<MsgHandler*>(MemSys::AllocZeroed(sizeof(MsgHandler)));
    if (handler) {
        handler->m_listLink.vtable = kMsgHandlerHeadVtable;
        handler->m_listLink.data = handler;
        handler->m_listLink.priority = priority;
        handler->m_listLink.next = &handler->m_listLink;
        handler->m_listLink.prev = &handler->m_listLink;
        handler->m_Code = code;
        handler->m_data = data;
        handler->m_modeBits = flags;
    }
    // (a failed allocation reaches AddNode as null and crashes there, as in the original)
    List(id).AddNode(reinterpret_cast<MemSys::ListNode*>(handler));
    MemSys::Manager::Get()->LeaveHeapScope();
    return handler;
}

// 0x0045A750
int Dispatcher::DispatchMsg(InboundMsgCtx* context)
{
    // Not original - what THUGPRO players send that THUG2 hasn't, checked first (TP-R8, net_guards.h).
    int guarded = OnlinePlay::Guards::CheckReceivedMessage(context);
    if (guarded >= 0)
        return guarded;
    ListKit::Search<MsgHandler> search;
    MsgHandler* next;
    for (MsgHandler* handler = search.HeadElement(&List(context->m_typeCode).m_listLink); handler; handler = next) {
        next = search.NextItem();
        if ((handler->m_modeBits & context->m_wireOptions) != context->m_wireOptions)
            continue;
        context->m_data = handler->m_data;
        int result = handler->m_Code(context);
        switch (result) {
        case kHandlerError:
        case kHandlerHalt:
        case kHandlerKeep:
        case kHandlerMsgDestroyed:
            return result;
        case kHandlerMsgDone:
            return kHandlerContinue;
        default:
            break;
        }
    }
    return kHandlerContinue;
}

// 0x0045A830
int Dispatcher::InvokeHandlersForMsg(Conn* conn, uint32_t flags)
{
    InboundMsgCtx context;
    char buffer[kConnReceiveBufferSize];
    ConstructContext(&context);
    context.m_receiverApp = conn->m_ownerApp;
    context.m_senderConn = conn;
    context.m_wireOptions = flags;
    context.m_body = buffer;
    context.m_bodySize = 0;
    char*& readBuffer = conn->m_readBuffer;
    char*& readPtr = conn->m_readPtr;
    void* metrics = &conn->m_receiveMetrics;

    for (char* p = readBuffer; p < readPtr; p += context.m_bodySize) {
        context.m_typeCode = static_cast<uint8_t>(*p++);
        uint8_t* table = GetManager(conn->m_ownerApp);
        bool hasLength = !(MessageFlags(table, context.m_typeCode) & kMsgNoLength);
        if (hasLength) {
            context.m_bodySize = *reinterpret_cast<uint16_t*>(p);
            p += 2;
        } // (else the length stays what the previous message had)
        if (context.m_typeCode == kMsgSequenced) // counted by the message inside
            AddMessageMetric(metrics, static_cast<uint8_t>(p[5]), context.m_bodySize);
        if (hasLength) {
            if (context.m_bodySize)
                std::memcpy(buffer, p, context.m_bodySize);
        } else {
            std::memcpy(buffer, p, kFixedSizeCopy); // a whole buffer's worth, whatever the message's size
        }
        if (IsOutsider(conn))
            context.m_wireOptions |= kPacketForeign;
        int result = DispatchMsg(&context);
        if (result == kHandlerError || result == kHandlerHalt) {
            readPtr = readBuffer;
            return kHandlerHalt;
        }
        if (result == kHandlerMsgDestroyed)
            return kHandlerMsgDestroyed; // (the connection is gone)
        if (context.m_typeCode != kMsgSequenced)
            AddMessageMetric(metrics, context.m_typeCode, context.m_bodySize);
        // (handlers may have changed the length: the timestamp and ack handlers set it)
    }

    if (context.m_Acknowledge) {
        for (int i = 0; i < context.m_NumSequencesToAck; ++i) {
            NetMessageInfo desc;
            ConstructMsgDesc(&desc);
            uint16_t sequence = static_cast<uint16_t>(context.m_SequencesToAck[i]);
            desc.m_data = &sequence;
            desc.m_byteCount = sizeof(sequence);
            desc.m_id = kMsgAck;
            PutOnOutbox(context.m_receiverApp, context.m_senderConn->m_handle, &desc);
        }
    }
    readPtr = readBuffer;
    return kHandlerContinue;
}

} // namespace Net
