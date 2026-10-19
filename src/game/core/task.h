#pragma once

#include <cstdint>

#include "game/core/alloc.h"

// Tasks: callbacks kept in priority-ordered lists that the main loop runs each frame (the main loop's logic and
// display task stacks, the input handlers, the file and network managers' tasks...).
namespace Tsk {

class List;

// A task (0x18 bytes; Task<T> adds its callback and data at +0x18 / +0x1C). Slot 0 of its vtable runs it.
// Its node (allocated by the constructor) carries the priority and links it into a List.
class BaseTask {
public:
    // 0x00406890: vtable 0x0064548C, not in a list; the node (Lst::Node<BaseTask>, 0x14 bytes) is allocated with
    // `priority` (null if that fails). Returns this.
    BaseTask* Construct(int priority);
    // 0x00406930: out of its list, and the node deleted. 0x00406980: the scalar deleting destructor (vtable
    // slot 2).
    void Destruct();
    BaseTask* ScalarDelete(uint8_t flags);
    // 0x004068F0: out of its list (if in one; the list is told it changed).
    void Remove();
    // 0x00406850: whether it's in a list (its node isn't linked to itself).
    bool InList() const;

    void** m_vtable;
    int m_disabled;           // +0x04 not run while nonzero
    Mem::ListNode* m_node;    // +0x08 data = this
    List* m_list;             // +0x0C the list it's in
    int m_stamp;              // +0x10 the list's stamp when it last ran
    uint32_t m_mask;          // +0x14 not run when it shares a bit with the mask the list is processed with
};
static_assert(sizeof(BaseTask) == 0x18, "layout must match the original");

// A list of tasks (0x20 bytes), highest priority first.
class List {
public:
    // 0x00406A50: empty (returns this).
    List* Construct();
    // 0x00406A20: the head unlinked.
    void Destruct();
    // 0x00406A80: in by priority (it runs in the current pass too, if one is going on).
    void AddTask(BaseTask* task);
    // 0x00406AC0: run each enabled task once whose mask has no bit of `mask` (the first argument is unused).
    // Tasks may add or remove tasks as they run: the pass then starts over from the first task, skipping the
    // ones that already ran (their stamp). Does nothing while the list is locked.
    void Process(void* unused, uint32_t mask);
    // 0x00406BB0: every task taken out.
    void RemoveAllTasks();
    // 0x004069B0
    void MarkChanged();

    int m_lock;               // +0x00 nonzero while being changed or run
    Mem::ListNode m_head;     // +0x04 (Lst::Head<BaseTask>, node vtable 0x0064549C)
    int m_stamp;              // +0x18 counts the passes
    bool m_changed;           // +0x1C a task was added or removed
};
static_assert(sizeof(List) == 0x20, "layout must match the original");

// A list on a Stack (0x24 bytes): its node in the stack (Lst::Node<StackList>, vtable 0x006454A4).
class StackList : public List {
public:
    // 0x00406D60 (returns this; a null node if the allocation fails)
    StackList* Construct();
    // 0x00406C30: the node deleted, then the list's own destructor.
    void Destruct();

    Mem::ListNode* m_node;    // +0x20
};
static_assert(sizeof(StackList) == 0x24, "layout must match the original");

// A stack of task lists (0x3C bytes): tasks go into the current list; Push starts a new list that hides the
// ones below it until Pop. The main loop's task lists are Stacks.
class Stack {
public:
    // 0x00406E80: one list pushed (returns this). The mask (+0x04) isn't set.
    Stack* Construct();
    // 0x004070E0: every list popped, the popped lists deleted, the heads unlinked.
    void Destruct();
    // 0x00406DE0: a new list on top. The current list stays where tasks are added until the next Process.
    void Push();
    // 0x00406FB0: the top list taken off (deleted at once, or after the pass if the stack is being run).
    void Pop();
    // 0x00406C90: into the current list.
    void AddTask(BaseTask* task);
    // 0x00406CB0: every task of the top list taken out if one was pushed during this pass, else of the current
    // list.
    void RemoveAllTasks();
    // 0x004070A0: run the top list with the stack's mask, then delete the lists popped meanwhile.
    void Process(void* unused);
    // 0x00406F10: delete the popped lists.
    void DeletePoppedLists();

    int m_lock;               // +0x00
    uint32_t m_mask;          // +0x04
    Mem::ListNode m_lists;    // +0x08 newest first (Lst::Head<StackList>, vtable 0x006454A8)
    Mem::ListNode m_poppedLists; // +0x1C lists popped while the stack was locked
    StackList* m_top;         // +0x30
    StackList* m_current;     // +0x34 where AddTask puts tasks
    bool m_pushed;            // +0x38
    bool m_justPopped;        // +0x39
};
static_assert(sizeof(Stack) == 0x3C, "layout must match the original");

} // namespace Tsk
