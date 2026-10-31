// Tasks, task lists and task list stacks (Tsk).

#include <cstdint>

#include "game/core/task.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/head.h"
#include "game/core/code_address.h"

namespace Tsk {

namespace {

extern void** const kBaseTaskVtable;
extern void** const kTaskNodeVtable;       // Lst::Node<BaseTask>
extern void** const kTaskSearchVtable;     // Lst::Search<BaseTask>
extern void** const kStackListSearchVtable; // Lst::Search<StackList>
extern void** const kStackListNodeVtable;  // Lst::Node<StackList>
extern void** const kStackListHeadVtable;  // Lst::Head<StackList>

void* const kHeadData = reinterpret_cast<void*>(-1);

// A node's links taken out of its list and pointed at itself (the vtable kept).
void Unlink(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
// After `head`: the first node of its list.
void InsertFront(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}
// The node after `node` (null at the head) - Lst::Search's step.
Mem::ListNode* NextNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == kHeadData ? nullptr : next;
}
void InitHead(Mem::ListNode* head, void** vtable)
{
    head->data = kHeadData;
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
// A node's scalar deleting destructor (vtable slot 0), with 1: unlinked and freed.
void DeleteNode(Mem::ListNode* node)
{
    reinterpret_cast<void(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1);
}

// The destructors behind the nodes', heads' and local searches' vtables.
struct TaskNode : Mem::ListNode {
    // 0x00406A00
    void DestroyBody() { UnlinkAs(kTaskNodeVtable); }
    // 0x00406860 (scalar deleting)
    TaskNode* Destroy(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
};
struct StackListNode : Mem::ListNode {
    // 0x00406D00
    void DestroyBody() { UnlinkAs(kStackListNodeVtable); }
    // 0x00406D20 (scalar deleting; the body inlined)
    StackListNode* Destroy(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
    // 0x00406E50: Lst::Head<StackList>'s scalar deleting destructor (the node's body).
    StackListNode* DestroyHead(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
};
struct Search {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x004069C0
    void DestroyTaskSearchBody() { m_vtable = kTaskSearchVtable; }
    // 0x004069D0 (scalar deleting)
    Search* DestroyTaskSearch(uint8_t flags)
    {
        m_vtable = kTaskSearchVtable;
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
    // 0x00406CA0
    void DestroyStackListSearchBody() { m_vtable = kStackListSearchVtable; }
    // 0x00406CD0 (scalar deleting)
    Search* DestroyStackListSearch(uint8_t flags)
    {
        m_vtable = kStackListSearchVtable;
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
};

} // namespace

// 0x00406890
BaseTask* BaseTask::Construct(int priority)
{
    m_disabled = 0;
    m_vtable = kBaseTaskVtable;
    m_list = nullptr;
    m_stamp = 0;
    m_mask = 0;
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kTaskNodeVtable;
        node->priority = priority;
        node->next = node;
        node->prev = node;
    }
    m_node = node;
    return this;
}

// 0x00406930
void BaseTask::Destruct()
{
    m_vtable = kBaseTaskVtable;
    Remove(); // (inlined)
    if (m_node)
        DeleteNode(m_node);
}

// 0x00406980
BaseTask* BaseTask::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004068F0
void BaseTask::Remove()
{
    if (!m_list)
        return;
    ++m_list->m_lock;
    m_list->MarkChanged();
    Unlink(m_node);
    --m_list->m_lock;
    m_list = nullptr;
}

// 0x00406850
bool BaseTask::InList() const
{
    return m_node->prev != m_node;
}

// 0x00406A50
List* List::Construct()
{
    m_lock = 0;
    InitHead(&m_head, kTaskNodeVtable);
    m_stamp = 0;
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
void List::AddTask(BaseTask* task)
{
    ++m_lock;
    reinterpret_cast<Lst::Head*>(&m_head)->AddNode(task->m_node);
    task->m_list = this;
    task->m_stamp = m_stamp - 1;
    m_changed = true;
    --m_lock;
}

// 0x00406AC0
void List::Process(void* /*unused*/, uint32_t mask)
{
    if (m_lock != 0)
        return;
    ++m_stamp;
    do {
        Mem::ListNode* node = NextNode(&m_head);
        BaseTask* task = node ? static_cast<BaseTask*>(node->data) : nullptr;
        m_changed = false;
        while (task) {
            // (the next task is found before this one runs: it may take itself out)
            Mem::ListNode* nextNode = NextNode(node);
            BaseTask* next = nextNode ? static_cast<BaseTask*>(nextNode->data) : nullptr;
            if (task->m_disabled == 0 && task->m_stamp != m_stamp) {
                task->m_stamp = m_stamp;
                if ((mask & task->m_mask) == 0)
                    reinterpret_cast<void(__thiscall*)(BaseTask*)>(task->m_vtable[0])(task);
            }
            if (m_changed)
                break;
            node = nextNode;
            task = next;
        }
    } while (m_changed);
}

// 0x00406BB0
void List::RemoveAllTasks()
{
    ++m_lock;
    for (;;) {
        Mem::ListNode* node = NextNode(&m_head);
        if (!node || !node->data)
            break;
        static_cast<BaseTask*>(node->data)->Remove();
        m_changed = true;
    }
    --m_lock;
}

// 0x00406D60
StackList* StackList::Construct()
{
    List::Construct();
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kStackListNodeVtable;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_node = node;
    return this;
}

// 0x00406C30
void StackList::Destruct()
{
    if (m_node)
        DeleteNode(m_node);
    List::Destruct();
}

// 0x00406E80
Stack* Stack::Construct()
{
    m_lock = 0;
    InitHead(&m_lists, kStackListHeadVtable);
    InitHead(&m_poppedLists, kStackListHeadVtable);
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
        Mem::ListNode* first = m_lists.next;
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
    void* memory = Mem::Calloc(sizeof(StackList));
    auto* list = memory ? static_cast<StackList*>(memory)->Construct() : nullptr;
    m_top = list;
    InsertFront(&m_lists, list->m_node); // (a failed allocation crashes here, as in the original)
    m_pushed = true;
}

// 0x00406FB0
void Stack::Pop()
{
    StackList* old = m_top;
    Mem::ListNode* first = NextNode(&m_lists);
    if (m_current == old) {
        // The current list becomes the one below (an empty stack would crash here, as in the original).
        Mem::ListNode* below = NextNode(first);
        m_current = below ? static_cast<StackList*>(below->data) : nullptr;
    } else {
        m_pushed = false;
    }
    if (m_lock == 0) {
        if (old) {
            old->Destruct();
            Mem::Manager::Get()->Free(old);
        }
    } else {
        Unlink(old->m_node);
        InsertFront(&m_poppedLists, old->m_node);
    }
    m_top = m_current;
    m_justPopped = true;
}

// 0x00406C90
void Stack::AddTask(BaseTask* task)
{
    m_current->AddTask(task);
}

// 0x00406CB0
void Stack::RemoveAllTasks()
{
    (m_pushed ? m_top : m_current)->RemoveAllTasks();
}

// 0x004070A0
void Stack::Process(void* unused)
{
    ++m_lock;
    m_current = m_top;
    m_top->Process(unused, m_mask);
    --m_lock;
    DeletePoppedLists();
    m_pushed = false;
    m_justPopped = false;
}

// 0x00406F10
void Stack::DeletePoppedLists()
{
    for (;;) {
        Mem::ListNode* node = NextNode(&m_poppedLists);
        if (!node || !node->data)
            break;
        auto* list = static_cast<StackList*>(node->data);
        list->Destruct(); // (inlined; deleting the node takes it out of the popped lists)
        Mem::Manager::Get()->Free(list);
    }
}

namespace {
static VtableSlot kBaseTaskVtable_slots[] = {&_purecall, &_purecall,
                &Tsk::BaseTask::ScalarDelete};
void** const kBaseTaskVtable = reinterpret_cast<void**>(kBaseTaskVtable_slots);
static VtableSlot kTaskNodeVtable_slots[] = {&Tsk::TaskNode::Destroy};
void** const kTaskNodeVtable = reinterpret_cast<void**>(kTaskNodeVtable_slots);
static VtableSlot kTaskSearchVtable_slots[] = {&Tsk::Search::DestroyTaskSearch};
void** const kTaskSearchVtable = reinterpret_cast<void**>(kTaskSearchVtable_slots);
static VtableSlot kStackListSearchVtable_slots[] = {&Tsk::Search::DestroyStackListSearch};
void** const kStackListSearchVtable = reinterpret_cast<void**>(kStackListSearchVtable_slots);
static VtableSlot kStackListNodeVtable_slots[] = {&Tsk::StackListNode::Destroy};
void** const kStackListNodeVtable = reinterpret_cast<void**>(kStackListNodeVtable_slots);
static VtableSlot kStackListHeadVtable_slots[] = {&Tsk::StackListNode::DestroyHead};
void** const kStackListHeadVtable = reinterpret_cast<void**>(kStackListHeadVtable_slots);
} // namespace

} // namespace Tsk
