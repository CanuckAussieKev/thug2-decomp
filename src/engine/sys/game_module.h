#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "engine/core/arena_alloc.h"
#include "engine/core/task_list.h"
#include <gsl/pointers>
#include <array>

// Modules: the engine's parts that are started and stopped as a whole - the front end, the
// async file loader, the game's own modules. The module manager (the singleton 0x0044F6F0 makes) keeps them in a
// list; starting, stopping or restarting one is a command its task carries out at the start of the next frame.
namespace modules {

// The base of every module (0x14 bytes, vtable 0x0064728C): started and stopped by the manager's task, each module
// overriding what starting and stopping do. Zeroed memory from the current heap, as memory::HeapObject gives (declared
// here, not inherited: an empty base under a polymorphic class gives MSVC multiple-inheritance member pointers, and
// the network code still calls the skate module's methods by address).
// The original's layout, as checked inside the class.
constexpr size_t kPartUnitBytes = 0x14;
constexpr size_t kPartUnitLockedOffset = 0x0C;
constexpr size_t kPartUnitListLinkOffset = 0x10;

class PartUnit {
public:
    static void* operator new(std::size_t size) noexcept { return memory::HeapObject::operator new(size); }
    static void operator delete(void* memory) { memory::HeapObject::operator delete(memory); }

    enum class State : uint8_t { kStopped = 0, kRunning = 1 };
    enum class OrderCode : uint8_t { kNone = 0, kStart = 1, kStop = 2, kRestart = 3 };

    // 0x0044F7E0: stopped, unlocked, with a list node of its own (none if out of memory).
    PartUnit();
    // 0x0044F790 / 0x0044F7B0: the node deleted.
    virtual ~PartUnit();
    PartUnit(const PartUnit&) = delete;
    PartUnit& operator=(const PartUnit&) = delete;

    // v_start_cb / v_stop_cb (slots 1 and 2; pure in the base).
    virtual void start_callback() = 0;
    virtual void stop_callback() = 0;

    // The fields other code reads and sets.
    [[nodiscard]] State cur_phase() const { return cur_phase_; }
    void set_cur_phase(State value) { cur_phase_ = value; }
    [[nodiscard]] OrderCode queued_op() const { return queued_op_; }
    void set_queued_op(OrderCode value) { queued_op_ = value; }
    [[nodiscard]] bool locked() const { return locked_; }
    [[nodiscard]] memory::ListNode* list_link() const { return list_link_; }

private:
    State cur_phase_{State::kStopped}; // +0x04 (the original's int: the rest is zero)
    std::array<uint8_t, 3> unknown05_{};
    OrderCode queued_op_{}; // +0x08 (not set by the constructor; the original's int)
    std::array<uint8_t, 3> unknown09_{};
    bool locked_{false}; // +0x0C: the manager neither starts nor stops it
    std::array<uint8_t, 3> pad0d_{};
    memory::ListNode* list_link_; // +0x10: in the manager's list (lists::Elem<Module>)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(PartUnit) == kPartUnitBytes && offsetof(PartUnit, locked_) == kPartUnitLockedOffset &&
                      offsetof(PartUnit, list_link_) == kPartUnitListLinkOffset);
    }
};

struct Hub {
    // The fields other code reads and sets.
    std::unique_ptr<tasks::Task<Hub>>& task() { return task_; }
    [[nodiscard]] const std::unique_ptr<tasks::Task<Hub>>& task() const { return task_; }

private:
    std::unique_ptr<tasks::Task<Hub>> task_; // priority -1000, running tick_modules
    memory::ListNode modules_{};             // +0x04: the list's head (lists::Foremost<Module>)
    bool command_pending_{false}; // +0x14 (the original's +0x18): a module was given a command since the task last ran

public:

    // 0x0044F5E0: the empty list and the task (not added to a task list here).
    Hub();
    // 0x0044F680: every module stopped (halt_every_module), the task deleted, the list's head unlinked.
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    // 0x0044F3C0: `module` onto the end of the list.
    void enrol_unit(PartUnit* module);
    // 0x0044F3E0: `module` out of the list (doesn't use the manager).
    static void remove_module(PartUnit* module);
    // 0x0044F310: unless it's locked, `module` is started (restarted if running) when the task next runs.
    void launch_module(PartUnit* module);
    // 0x0044F530: every running module that isn't locked stopped now (its command dropped).
    void halt_every_module();

    // 0x0044F440 (the task's code, the manager its data): if a command is pending, each module's carried out -
    // a stopped one started by kStart or kRestart; a running one stopped by kStop, stopped and started again by
    // kRestart - and the command cleared.
    static void tick_modules(tasks::Task<Hub>* task);
};

// The singleton (0x00696D78, references 0x00696D7C). 0x0044F6F0: the first call with `create` makes it, every call
// takes a reference and returns it (null if there is none).
Hub* create_manager(bool create);
// The manager create_manager made (null before / after).
Hub* get_module_manager();
// 0x0044F740: drops a reference; the last one destroys and frees the manager.
void release_manager();

} // namespace modules
