// Tasks, task lists and task list stacks (Tsk).

#include <cstdint>

#include "engine/core/task_list.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/core/code_address.h"

namespace Scheduling {

namespace {

extern void** const kBaseTaskVtable;
extern void** const kTaskNodeVtable;       // ListKit::Elem<SchedulerTask>
extern void** const kTaskSearchVtable;     // ListKit::Hunt<SchedulerTask>
extern void** const kStackListSearchVtable; // ListKit::Hunt<StackList>
extern void** const kStackListNodeVtable;  // ListKit::Elem<StackList>
extern void** const kStackListHeadVtable;  // ListKit::Foremost<StackList>

void* const kHeadData = reinterpret_cast<void*>(-1);

// A node's links taken out of its list and pointed at itself (the vtable kept).
void Uncouple(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
// After `head`: the first node of its list.
void InsertFront(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}
// The node after `node` (null at the head) - ListKit::Hunt's step.
MemSys::ListNode* NextNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == kHeadData ? nullptr : next;
}
void SetupListSentinel(MemSys::ListNode* head, void** vtable)
{
    head->data = kHeadData;
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
// A node's scalar deleting destructor (vtable slot 0), with 1: unlinked and freed.
void UnlinkListNode(MemSys::ListNode* node)
{
    reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, int)>(node->vtable[0])(node, 1);
}

// The destructors behind the nodes', heads' and local searches' vtables.
struct TaskNode : MemSys::ListNode {
    // 0x00406A00
    void DestroyBody() { UnlinkAs(kTaskNodeVtable); }
    // 0x00406860 (scalar deleting)
    TaskNode* Teardown(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Hub::Pull()->Dealloc(this);
        return this;
    }
};
struct StackListNode : MemSys::ListNode {
    // 0x00406D00
    void DestroyBody() { UnlinkAs(kStackListNodeVtable); }
    // 0x00406D20 (scalar deleting; the body inlined)
    StackListNode* Teardown(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Hub::Pull()->Dealloc(this);
        return this;
    }
    // 0x00406E50: ListKit::Foremost<StackList>'s scalar deleting destructor (the node's body).
    StackListNode* DestroyHead(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Hub::Pull()->Dealloc(this);
        return this;
    }
};
struct Hunt {
    void** m_vtable;
    MemSys::ListNode* m_listLink;

    // 0x004069C0
    void DestroyTaskSearchBody() { m_vtable = kTaskSearchVtable; }
    // 0x004069D0 (scalar deleting)
    Hunt* DestroyTaskSearch(uint8_t flags)
    {
        m_vtable = kTaskSearchVtable;
        if (flags & 1)
            MemSys::Hub::Pull()->Dealloc(this);
        return this;
    }
    // 0x00406CA0
    void DestroyStackListSearchBody() { m_vtable = kStackListSearchVtable; }
    // 0x00406CD0 (scalar deleting)
    Hunt* DestroyStackListSearch(uint8_t flags)
    {
        m_vtable = kStackListSearchVtable;
        if (flags & 1)
            MemSys::Hub::Pull()->Dealloc(this);
        return this;
    }
};

} // namespace

// 0x00406890
SchedulerTask* SchedulerTask::MakeNew(int priority)
{
    m_disabled = 0;
    m_vtable = kBaseTaskVtable;
    m_sequence = nullptr;
    m_passCount = 0;
    m_skipBits = 0;
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kTaskNodeVtable;
        node->priority = priority;
        node->next = node;
        node->prev = node;
    }
    m_listLink = node;
    return this;
}

// 0x00406930
void SchedulerTask::Destruct()
{
    m_vtable = kBaseTaskVtable;
    DropOut(); // (inlined)
    if (m_listLink)
        UnlinkListNode(m_listLink);
}

// 0x00406980
SchedulerTask* SchedulerTask::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004068F0
void SchedulerTask::DropOut()
{
    if (!m_sequence)
        return;
    ++m_sequence->m_lock;
    m_sequence->MarkChanged();
    Uncouple(m_listLink);
    --m_sequence->m_lock;
    m_sequence = nullptr;
}

// 0x00406850
bool SchedulerTask::IsOnTaskList() const
{
    return m_listLink->prev != m_listLink;
}

// 0x00406A50
LinkChain* LinkChain::MakeNew()
{
    m_lock = 0;
    SetupListSentinel(&m_head, kTaskNodeVtable);
    m_passCount = 0;
    m_altered = false;
    return this;
}

// 0x00406A20
void LinkChain::Destruct()
{
    m_head.UnlinkAs(kTaskNodeVtable);
}

// 0x004069B0
void LinkChain::MarkChanged()
{
    m_altered = true;
}

// 0x00406A80
void LinkChain::QueueJob(SchedulerTask* task)
{
    ++m_lock;
    reinterpret_cast<ListKit::Foremost*>(&m_head)->AppendVertex(task->m_listLink);
    task->m_sequence = this;
    task->m_passCount = m_passCount - 1;
    m_altered = true;
    --m_lock;
}

// 0x00406AC0
void LinkChain::WorkOn(void* /*unused*/, uint32_t mask)
{
    if (m_lock != 0)
        return;
    ++m_passCount;
    do {
        MemSys::ListNode* node = NextNode(&m_head);
        SchedulerTask* task = node ? static_cast<SchedulerTask*>(node->data) : nullptr;
        m_altered = false;
        while (task) {
            // (the next task is found before this one runs: it may take itself out)
            MemSys::ListNode* nextNode = NextNode(node);
            SchedulerTask* next = nextNode ? static_cast<SchedulerTask*>(nextNode->data) : nullptr;
            if (task->m_disabled == 0 && task->m_passCount != m_passCount) {
                task->m_passCount = m_passCount;
                if ((mask & task->m_skipBits) == 0)
                    reinterpret_cast<void(__thiscall*)(SchedulerTask*)>(task->m_vtable[0])(task);
            }
            if (m_altered)
                break;
            node = nextNode;
            task = next;
        }
    } while (m_altered);
}

// 0x00406BB0
void LinkChain::ClearJobList()
{
    ++m_lock;
    for (;;) {
        MemSys::ListNode* node = NextNode(&m_head);
        if (!node || !node->data)
            break;
        static_cast<SchedulerTask*>(node->data)->DropOut();
        m_altered = true;
    }
    --m_lock;
}

// 0x00406D60
StackList* StackList::MakeNew()
{
    LinkChain::MakeNew();
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kStackListNodeVtable;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_listLink = node;
    return this;
}

// 0x00406C30
void StackList::Destruct()
{
    if (m_listLink)
        UnlinkListNode(m_listLink);
    LinkChain::Destruct();
}

// 0x00406E80
PileUp* PileUp::MakeNew()
{
    m_lock = 0;
    SetupListSentinel(&m_lists, kStackListHeadVtable);
    SetupListSentinel(&m_poppedLists, kStackListHeadVtable);
    m_pushed = false;
    m_justPopped = false;
    StackOn();
    m_ongoing = m_topSide;
    m_pushed = false;
    m_justPopped = false;
    return this;
}

// 0x004070E0
void PileUp::Destruct()
{
    // (the original counts the lists each time round)
    for (;;) {
        MemSys::ListNode* first = m_lists.next;
        if (first->data == kHeadData || !first)
            break;
        StackOff();
    }
    DeletePoppedLists();
    m_poppedLists.UnlinkAs(kStackListNodeVtable);
    m_lists.UnlinkAs(kStackListNodeVtable);
}

// 0x00406DE0
void PileUp::StackOn()
{
    void* memory = MemSys::AllocZeroed(sizeof(StackList));
    auto* list = memory ? static_cast<StackList*>(memory)->MakeNew() : nullptr;
    m_topSide = list;
    InsertFront(&m_lists, list->m_listLink); // (a failed allocation crashes here, as in the original)
    m_pushed = true;
}

// 0x00406FB0
void PileUp::StackOff()
{
    StackList* old = m_topSide;
    MemSys::ListNode* first = NextNode(&m_lists);
    if (m_ongoing == old) {
        // The current list becomes the one below (an empty stack would crash here, as in the original).
        MemSys::ListNode* below = NextNode(first);
        m_ongoing = below ? static_cast<StackList*>(below->data) : nullptr;
    } else {
        m_pushed = false;
    }
    if (m_lock == 0) {
        if (old) {
            old->Destruct();
            MemSys::Hub::Pull()->Dealloc(old);
        }
    } else {
        Uncouple(old->m_listLink);
        InsertFront(&m_poppedLists, old->m_listLink);
    }
    m_topSide = m_ongoing;
    m_justPopped = true;
}

// 0x00406C90
void PileUp::QueueJob(SchedulerTask* task)
{
    m_ongoing->QueueJob(task);
}

// 0x00406CB0
void PileUp::ClearJobList()
{
    (m_pushed ? m_topSide : m_ongoing)->ClearJobList();
}

// 0x004070A0
void PileUp::WorkOn(void* unused)
{
    ++m_lock;
    m_ongoing = m_topSide;
    m_topSide->WorkOn(unused, m_skipBits);
    --m_lock;
    DeletePoppedLists();
    m_pushed = false;
    m_justPopped = false;
}

// 0x00406F10
void PileUp::DeletePoppedLists()
{
    for (;;) {
        MemSys::ListNode* node = NextNode(&m_poppedLists);
        if (!node || !node->data)
            break;
        auto* list = static_cast<StackList*>(node->data);
        list->Destruct(); // (inlined; deleting the node takes it out of the popped lists)
        MemSys::Hub::Pull()->Dealloc(list);
    }
}

namespace {
static VtableSlot kBaseTaskVtable_slots[] = {&_purecall, &_purecall,
                &Scheduling::SchedulerTask::ScalarDelete};
void** const kBaseTaskVtable = reinterpret_cast<void**>(kBaseTaskVtable_slots);
static VtableSlot kTaskNodeVtable_slots[] = {&Scheduling::TaskNode::Teardown};
void** const kTaskNodeVtable = reinterpret_cast<void**>(kTaskNodeVtable_slots);
static VtableSlot kTaskSearchVtable_slots[] = {&Scheduling::Hunt::DestroyTaskSearch};
void** const kTaskSearchVtable = reinterpret_cast<void**>(kTaskSearchVtable_slots);
static VtableSlot kStackListSearchVtable_slots[] = {&Scheduling::Hunt::DestroyStackListSearch};
void** const kStackListSearchVtable = reinterpret_cast<void**>(kStackListSearchVtable_slots);
static VtableSlot kStackListNodeVtable_slots[] = {&Scheduling::StackListNode::Teardown};
void** const kStackListNodeVtable = reinterpret_cast<void**>(kStackListNodeVtable_slots);
static VtableSlot kStackListHeadVtable_slots[] = {&Scheduling::StackListNode::DestroyHead};
void** const kStackListHeadVtable = reinterpret_cast<void**>(kStackListHeadVtable_slots);
} // namespace

} // namespace Tsk
