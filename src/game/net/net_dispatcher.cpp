// The network library's message dispatcher (Net::Dispatcher). See net_dispatcher.h.

#include "game/net/net_dispatcher.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "hook/hook.h"

namespace Net {

namespace {

void** const kMsgHandlerSearchVtable = reinterpret_cast<void**>(0x006475E4);
void** const kMsgHandlerVtable = reinterpret_cast<void**>(0x006475E8);
constexpr uint32_t kFixedSizeCopy = 0x514; // what a message without a length field copies

void ConstructContext(MsgHandlerContext* context)
{
    GameFunction<MsgHandlerContext*(__thiscall*)(MsgHandlerContext*)>(0x00454A20)(context);
}

} // namespace

// 0x0045A590
MsgHandler* MsgHandler::Destroy(unsigned int flags)
{
    // -> Lst::Node<MsgHandler>::~Node (wt/netA): unlinks it
    GameFunction<void(__thiscall*)(MsgHandler*)>(0x00457030)(this);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0045A500
void MsgHandlerSearch::DestroyBody() { m_vtable = kMsgHandlerSearchVtable; }

// 0x0045A510
MsgHandlerSearch* MsgHandlerSearch::Destroy(unsigned int flags)
{
    m_vtable = kMsgHandlerSearchVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0045A640
void Dispatcher::Init()
{
    for (Lst::Head& list : m_handlers) {
        Mem::ListNode* node = list.m_node.next;
        // (the head check comes before the null check)
        if (node->data == reinterpret_cast<void*>(-1) || !node)
            continue;
        do {
            Mem::ListNode* next = node->next;
            if (next->data == reinterpret_cast<void*>(-1))
                next = nullptr;
            DeleteNode(node);
            node = next;
        } while (node);
    }
}

// 0x0045A690
void Dispatcher::Deinit()
{
    for (Lst::Head& list : m_handlers) {
        Lst::Search<MsgHandler> search;
        for (MsgHandler* handler = search.FirstItem(&list.m_node); handler;) {
            MsgHandler* next = search.NextItem();
            DeleteNode(handler);
            handler = next;
        }
    }
}

// 0x0045A5C0
MsgHandler* Dispatcher::AddHandler(uint8_t id, MsgHandlerCode code, uint32_t flags, void* data, int priority)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_networkHeap);
    auto* handler = static_cast<MsgHandler*>(Mem::Calloc(sizeof(MsgHandler)));
    if (handler) {
        handler->m_node.vtable = kMsgHandlerVtable;
        handler->m_node.data = handler;
        handler->m_node.priority = priority;
        handler->m_node.next = &handler->m_node;
        handler->m_node.prev = &handler->m_node;
        handler->m_Code = code;
        handler->m_Data = data;
        handler->m_Flags = flags;
    }
    // (a failed allocation reaches AddNode as null and crashes there, as in the original)
    m_handlers[id].AddNode(reinterpret_cast<Mem::ListNode*>(handler));
    Mem::Manager::Get()->PopContext();
    return handler;
}

// 0x0045A750
int Dispatcher::DispatchMsg(MsgHandlerContext* context)
{
    Lst::Search<MsgHandler> search;
    MsgHandler* next;
    for (MsgHandler* handler = search.FirstItem(&m_handlers[context->m_MsgId].m_node); handler; handler = next) {
        next = search.NextItem();
        if ((handler->m_Flags & context->m_PacketFlags) != context->m_PacketFlags)
            continue;
        context->m_Data = handler->m_Data;
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
int Dispatcher::DispatchMsgHandlers(Conn* conn, uint32_t flags)
{
    MsgHandlerContext context;
    char buffer[kConnReceiveBufferSize];
    ConstructContext(&context);
    context.m_App = Field<App*>(conn, ConnField::kApp);
    context.m_Conn = conn;
    context.m_PacketFlags = flags;
    context.m_Msg = buffer;
    context.m_MsgLength = 0;
    char*& readBuffer = Field<char*>(conn, ConnField::kReadBuffer);
    char*& readPtr = Field<char*>(conn, ConnField::kReadPtr);
    void* metrics = &Field<uint8_t>(conn, ConnField::kReceiveMetrics);

    for (char* p = readBuffer; p < readPtr; p += context.m_MsgLength) {
        context.m_MsgId = static_cast<uint8_t>(*p++);
        uint8_t* table = GetManager(Field<App*>(conn, ConnField::kApp));
        bool hasLength = !(MessageFlags(table, context.m_MsgId) & kMsgNoLength);
        if (hasLength) {
            context.m_MsgLength = *reinterpret_cast<uint16_t*>(p);
            p += 2;
        } // (else the length stays what the previous message had)
        if (context.m_MsgId == kMsgSequenced) // counted by the message inside
            AddMessageMetric(metrics, static_cast<uint8_t>(p[5]), context.m_MsgLength);
        if (hasLength) {
            if (context.m_MsgLength)
                std::memcpy(buffer, p, context.m_MsgLength);
        } else {
            std::memcpy(buffer, p, kFixedSizeCopy); // a whole buffer's worth, whatever the message's size
        }
        if (IsForeign(conn))
            context.m_PacketFlags |= kPacketForeign;
        int result = DispatchMsg(&context);
        if (result == kHandlerError || result == kHandlerHalt) {
            readPtr = readBuffer;
            return kHandlerHalt;
        }
        if (result == kHandlerMsgDestroyed)
            return kHandlerMsgDestroyed; // (the connection is gone)
        if (context.m_MsgId != kMsgSequenced)
            AddMessageMetric(metrics, context.m_MsgId, context.m_MsgLength);
        // (handlers may have changed the length: the timestamp and ack handlers set it)
    }

    if (context.m_Acknowledge) {
        for (int i = 0; i < context.m_NumSequencesToAck; ++i) {
            MsgDesc desc;
            ConstructMsgDesc(&desc);
            uint16_t sequence = static_cast<uint16_t>(context.m_SequencesToAck[i]);
            desc.m_Data = &sequence;
            desc.m_Length = sizeof(sequence);
            desc.m_Id = kMsgAck;
            EnqueueMessage(context.m_App, Field<int>(context.m_Conn, ConnField::kHandle), &desc);
        }
    }
    readPtr = readBuffer;
    return kHandlerContinue;
}

} // namespace Net

REPLACE(0x0045A500, &Net::MsgHandlerSearch::DestroyBody);
REPLACE(0x0045A510, &Net::MsgHandlerSearch::Destroy);
REPLACE(0x0045A590, &Net::MsgHandler::Destroy);
REPLACE(0x0045A5C0, &Net::Dispatcher::AddHandler);
REPLACE(0x0045A640, &Net::Dispatcher::Init);
REPLACE(0x0045A690, &Net::Dispatcher::Deinit);
REPLACE(0x0045A750, &Net::Dispatcher::DispatchMsg);
REPLACE(0x0045A830, &Net::Dispatcher::DispatchMsgHandlers);
