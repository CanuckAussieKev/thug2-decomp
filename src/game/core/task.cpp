// Tasks, task lists and task list stacks (Tsk).

#include <cstdint>

#include "game/core/task.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/head.h"
#include "game/core/code_address.h"

namespace TaskList {

namespace {

extern void** const kBaseTaskVtable;
extern void** const kTaskNodeVtable;       // ListKit::Node<SchedulerTask>
extern void** const kTaskSearchVtable;     // ListKit::Search<SchedulerTask>
extern void** const kStackListSearchVtable; // ListKit::Search<StackList>
extern void** const kStackListNodeVtable;  // ListKit::Node<StackList>
extern void** const kStackListHeadVtable;  // ListKit::Head<StackList>

void* const kHeadData = reinterpret_cast<void*>(-1);

// A node's links taken out of its list and pointed at itself (the vtable kept).
void Unlink(MemSys::ListNode* node)
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
// The node after `node` (null at the head) - ListKit::Search's step.
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
    TaskNode* Destroy(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
};
struct StackListNode : MemSys::ListNode {
    // 0x00406D00
    void DestroyBody() { UnlinkAs(kStackListNodeVtable); }
    // 0x00406D20 (scalar deleting; the body inlined)
    StackListNode* Destroy(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
    // 0x00406E50: ListKit::Head<StackList>'s scalar deleting destructor (the node's body).
    StackListNode* DestroyHead(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
};
struct Search {
    void** m_vtable;
    MemSys::ListNode* m_listLink;

    // 0x004069C0
    void DestroyTaskSearchBody() { m_vtable = kTaskSearchVtable; }
    // 0x004069D0 (scalar deleting)
    Search* DestroyTaskSearch(uint8_t flags)
    {
        m_vtable = kTaskSearchVtable;
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
    // 0x00406CA0
    void DestroyStackListSearchBody() { m_vtable = kStackListSearchVtable; }
    // 0x00406CD0 (scalar deleting)
    Search* DestroyStackListSearch(uint8_t flags)
    {
        m_vtable = kStackListSearchVtable;
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
};

} // namespace

// 0x00406890
SchedulerTask* SchedulerTask::Construct(int priority)
{
    m_disabled = 0;
    m_vtable = kBaseTaskVtable;
    m_list = nullptr;
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
    Remove(); // (inlined)
    if (m_listLink)
        UnlinkListNode(m_listLink);
}

// 0x00406980
SchedulerTask* SchedulerTask::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004068F0
void SchedulerTask::Remove()
{
    if (!m_list)
        return;
    ++m_list->m_lock;
    m_list->MarkChanged();
    Unlink(m_listLink);
    --m_list->m_lock;
    m_list = nullptr;
}

// 0x00406850
bool SchedulerTask::IsOnTaskList() const
{
    return m_listLink->prev != m_listLink;
}

// 0x00406A50
List* List::Construct()
{
    m_lock = 0;
    SetupListSentinel(&m_head, kTaskNodeVtable);
    m_passCount = 0;
    m_changed = false;
    return this;
}

// 0x00406A20
void List::Destruct()
{
    m_head.UnlinkAs(kTaskNodeVtable);
}

// 0x004069B0
void List::MarkChanged()
{
    m_changed = true;
}

// 0x00406A80
void List::AddTask(SchedulerTask* task)
{
    ++m_lock;
    reinterpret_cast<ListKit::Head*>(&m_head)->AddNode(task->m_listLink);
    task->m_list = this;
    task->m_passCount = m_passCount - 1;
    m_changed = true;
    --m_lock;
}

// 0x00406AC0
void List::Process(void* /*unused*/, uint32_t mask)
{
    if (m_lock != 0)
        return;
    ++m_passCount;
    do {
        MemSys::ListNode* node = NextNode(&m_head);
        SchedulerTask* task = node ? static_cast<SchedulerTask*>(node->data) : nullptr;
        m_changed = false;
        while (task) {
            // (the next task is found before this one runs: it may take itself out)
            MemSys::ListNode* nextNode = NextNode(node);
            SchedulerTask* next = nextNode ? static_cast<SchedulerTask*>(nextNode->data) : nullptr;
            if (task->m_disabled == 0 && task->m_passCount != m_passCount) {
                task->m_passCount = m_passCount;
                if ((mask & task->m_skipBits) == 0)
                    reinterpret_cast<void(__thiscall*)(SchedulerTask*)>(task->m_vtable[0])(task);
            }
            if (m_changed)
                break;
            node = nextNode;
            task = next;
        }
    } while (m_changed);
}

// 0x00406BB0
void List::ClearJobList()
{
    ++m_lock;
    for (;;) {
        MemSys::ListNode* node = NextNode(&m_head);
        if (!node || !node->data)
            break;
        static_cast<SchedulerTask*>(node->data)->Remove();
        m_changed = true;
    }
    --m_lock;
}

// 0x00406D60
StackList* StackList::Construct()
{
    List::Construct();
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
    List::Destruct();
}

// 0x00406E80
Stack* Stack::Construct()
{
    m_lock = 0;
    SetupListSentinel(&m_lists, kStackListHeadVtable);
    SetupListSentinel(&m_poppedLists, kStackListHeadVtable);
    m_pushed = false;
    m_justPopped = false;
    Push();
    m_current = m_top;
    m_pushed = false;
    m_justPopped = false;
    return this;
}

// 0x004070E0
void Stack::Destruct()
{
    // (the original counts the lists each time round)
    for (;;) {
        MemSys::ListNode* first = m_lists.next;
        if (first->data == kHeadData || !first)
            break;
        Pop();
    }
    DeletePoppedLists();
    m_poppedLists.UnlinkAs(kStackListNodeVtable);
    m_lists.UnlinkAs(kStackListNodeVtable);
}

// 0x00406DE0
void Stack::Push()
{
    void* memory = MemSys::AllocZeroed(sizeof(StackList));
    auto* list = memory ? static_cast<StackList*>(memory)->Construct() : nullptr;
    m_top = list;
    InsertFront(&m_lists, list->m_listLink); // (a failed allocation crashes here, as in the original)
    m_pushed = true;
}

// 0x00406FB0
void Stack::Pop()
{
    StackList* old = m_top;
    MemSys::ListNode* first = NextNode(&m_lists);
    if (m_current == old) {
        // The current list becomes the one below (an empty stack would crash here, as in the original).
        MemSys::ListNode* below = NextNode(first);
        m_current = below ? static_cast<StackList*>(below->data) : nullptr;
    } else {
        m_pushed = false;
    }
    if (m_lock == 0) {
        if (old) {
            old->Destruct();
            MemSys::Manager::Get()->Free(old);
        }
    } else {
        Unlink(old->m_listLink);
        InsertFront(&m_poppedLists, old->m_listLink);
    }
    m_top = m_current;
    m_justPopped = true;
}

// 0x00406C90
void Stack::AddTask(SchedulerTask* task)
{
    m_current->AddTask(task);
}

// 0x00406CB0
void Stack::ClearJobList()
{
    (m_pushed ? m_top : m_current)->ClearJobList();
}

// 0x004070A0
void Stack::Process(void* unused)
{
    ++m_lock;
    m_current = m_top;
    m_top->Process(unused, m_skipBits);
    --m_lock;
    DeletePoppedLists();
    m_pushed = false;
    m_justPopped = false;
}

// 0x00406F10
void Stack::DeletePoppedLists()
{
    for (;;) {
        MemSys::ListNode* node = NextNode(&m_poppedLists);
        if (!node || !node->data)
            break;
        auto* list = static_cast<StackList*>(node->data);
        list->Destruct(); // (inlined; deleting the node takes it out of the popped lists)
        MemSys::Manager::Get()->Free(list);
    }
}

namespace {
static VtableSlot kBaseTaskVtable_slots[] = {&_purecall, &_purecall,
                &TaskList::SchedulerTask::ScalarDelete};
void** const kBaseTaskVtable = reinterpret_cast<void**>(kBaseTaskVtable_slots);
static VtableSlot kTaskNodeVtable_slots[] = {&TaskList::TaskNode::Destroy};
void** const kTaskNodeVtable = reinterpret_cast<void**>(kTaskNodeVtable_slots);
static VtableSlot kTaskSearchVtable_slots[] = {&TaskList::Search::DestroyTaskSearch};
void** const kTaskSearchVtable = reinterpret_cast<void**>(kTaskSearchVtable_slots);
static VtableSlot kStackListSearchVtable_slots[] = {&TaskList::Search::DestroyStackListSearch};
void** const kStackListSearchVtable = reinterpret_cast<void**>(kStackListSearchVtable_slots);
static VtableSlot kStackListNodeVtable_slots[] = {&TaskList::StackListNode::Destroy};
void** const kStackListNodeVtable = reinterpret_cast<void**>(kStackListNodeVtable_slots);
static VtableSlot kStackListHeadVtable_slots[] = {&TaskList::StackListNode::DestroyHead};
void** const kStackListHeadVtable = reinterpret_cast<void**>(kStackListHeadVtable_slots);
} // namespace

} // namespace Tsk
