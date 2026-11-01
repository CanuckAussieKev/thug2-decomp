#pragma once

#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"

// Tasks: callbacks kept in priority-ordered lists that the main loop runs each frame (the main loop's logic and
// display task stacks, the input handlers, the file and network managers' tasks...).
namespace tasks {

class LinkChain;

// A task (0x18 bytes, vtable 0x0064548C): run() is what the list calls each pass; Task<T> adds a code and an owner.
// Its node (allocated by the constructor) carries the priority and links it into a List.
class SchedulerTask : public memory::HeapObject {
public:
    // 0x00406890: not in a list; the node (lists::Elem<SchedulerTask>, 0x14 bytes) is allocated with `priority`
    // (null if that fails).
    explicit SchedulerTask(int priority);
    // 0x00406930 / 0x00406980: out of its list, and the node deleted.
    virtual ~SchedulerTask();
    SchedulerTask(const SchedulerTask&) = delete;
    SchedulerTask& operator=(const SchedulerTask&) = delete;

    // Slot 0: the task's work (pure in the base). (Slot 1 - pure in the base, the code's getter in Task<T>, 0 in
    // the input handlers - is never called: left out.)
    virtual void run() = 0;

    // 0x004068F0: out of its list (if in one; the list is told it changed).
    void drop_out();
    // 0x00406850: whether it's in a list (its node isn't linked to itself).
    bool is_on_task_list() const;

    int disabled_;                // +0x04 not run while nonzero
    memory::ListNode* list_link_; // +0x08 data = this
    LinkChain* sequence_;         // +0x0C the list it's in
    int pass_count_;              // +0x10 the list's stamp when it last ran
    uint32_t skip_bits_;          // +0x14 not run when it shares a bit with the mask the list is processed with
};

// tasks::Task<Owner> (0x20 bytes; every instance shared the vtable 0x006476F0 - the linker folded them): a task
// running a cdecl function given the task, whose owner is the object it works for.
template <typename Owner>
class Task final : public SchedulerTask {
public:
    using Code = void (*)(Task* task);

    Task(Code code, Owner* owner, int priority) : SchedulerTask(priority), code_value_(code), content_(owner) {}

    // 0x00564DB0 (slot 0 of every Task<T>): the code, given the task.
    void run() override { code_value_(this); }

    Code code_value_; // +0x18
    Owner* content_;  // +0x1C the object it works for
};

// A list of tasks (0x20 bytes in the original), highest priority first.
class LinkChain {
public:
    // 0x00406A50: empty.
    LinkChain();
    // 0x00406A20: the head unlinked.
    ~LinkChain();
    LinkChain(const LinkChain&) = delete;
    LinkChain& operator=(const LinkChain&) = delete;
    // 0x00406A80: in by priority (it runs in the current pass too, if one is going on).
    void queue_job(SchedulerTask* task);
    // 0x00406AC0: run each enabled task once whose mask has no bit of `mask` (the first argument is unused).
    // Tasks may add or remove tasks as they run: the pass then starts over from the first task, skipping the
    // ones that already ran (their stamp). Does nothing while the list is locked.
    void work_on(void* unused, uint32_t mask);
    // 0x00406BB0: every task taken out.
    void clear_job_list();
    // 0x004069B0
    void mark_changed();

    int lock_;              // +0x00 nonzero while being changed or run
    memory::ListNode head_; // +0x04 (lists::Foremost<SchedulerTask>)
    int pass_count_;        // +0x14 counts the passes
    bool altered_;          // +0x18 a task was added or removed
};

// A list on a Stack (0x24 bytes in the original): its node in the stack (lists::Elem<StackList>).
class StackList : public LinkChain {
public:
    StackList(const StackList&) = delete;
    StackList& operator=(const StackList&) = delete;
    // 0x00406D60 (a null node if the allocation fails)
    StackList();
    // 0x00406C30: the node deleted, then the list's own destructor.
    ~StackList();

    memory::ListNode* list_link_; // +0x1C
};

// A stack of task lists (0x3C bytes in the original): tasks go into the current list; Push starts a new list that hides the
// ones below it until Pop. The main loop's task lists are Stacks.
class PileUp {
public:
    // 0x00406E80: one list pushed. The mask (+0x04) isn't set.
    PileUp();
    // 0x004070E0: every list popped, the popped lists deleted, the heads unlinked.
    ~PileUp();
    PileUp(const PileUp&) = delete;
    PileUp& operator=(const PileUp&) = delete;
    // 0x00406DE0: a new list on top. The current list stays where tasks are added until the next Process.
    void stack_on();
    // 0x00406FB0: the top list taken off (deleted at once, or after the pass if the stack is being run).
    void stack_off();
    // 0x00406C90: into the current list.
    void queue_job(SchedulerTask* task);
    // 0x00406CB0: every task of the top list taken out if one was pushed during this pass, else of the current
    // list.
    void clear_job_list();
    // 0x004070A0: run the top list with the stack's mask, then delete the lists popped meanwhile.
    void work_on(void* unused);
    // 0x00406F10: delete the popped lists.
    void delete_popped_lists();

    int lock_;                      // +0x00
    uint32_t skip_bits_;            // +0x04
    memory::ListNode lists_;        // +0x08 newest first (lists::Foremost<StackList>)
    memory::ListNode popped_lists_; // +0x18 lists popped while the stack was locked
    StackList* top_side_;           // +0x28
    StackList* ongoing_;            // +0x2C where queue_job puts tasks
    bool pushed_;                   // +0x30
    bool just_popped_;              // +0x31
};

} // namespace tasks
