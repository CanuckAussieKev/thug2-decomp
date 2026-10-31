#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

// Modules (Neversoft's Mdl): the engine's parts that are started and stopped as a whole - the front end, the
// async file loader, the game's own modules. The module manager (the singleton 0x0044F6F0 makes) keeps them in a
// list; starting, stopping or restarting one is a command its task carries out at the start of the next frame.
namespace modules {

// The base of every module (0x14 bytes, vtable 0x0064728C: the scalar deleting destructor, then v_start_cb and
// v_stop_cb, which each module overrides).
struct PartUnit {
    enum State : int { kStopped = 0, kRunning = 1 };
    enum OrderCode : int { kNone = 0, kStart = 1, kStop = 2, kRestart = 3 };

    void** vtable_;
    int cur_phase_; // +0x04
    int queued_op_; // +0x08 (not set by the constructor)
    bool locked_;   // +0x0C: the manager neither starts nor stops it
    uint8_t pad0d_[3];
    memory::ListNode* list_link_; // +0x10: in the manager's list (lists::Elem<Module>, vtable 0x00647284)

    // 0x0044F7E0: stopped, unlocked, with a list node of its own (none if out of memory). Returns this.
    PartUnit* make_new();
    // 0x0044F790: the node deleted.
    void destroy_body();
    // 0x0044F7B0 (scalar deleting destructor)
    PartUnit* teardown(unsigned int flags);

    void start_callback() { reinterpret_cast<void(__thiscall*)(PartUnit*)>(vtable_[1])(this); }
    void stop_callback() { reinterpret_cast<void(__thiscall*)(PartUnit*)>(vtable_[2])(this); }
};
static_assert(sizeof(PartUnit) == 0x14 && offsetof(PartUnit, locked_) == 0x0C &&
              offsetof(PartUnit, list_link_) == 0x10);

struct Hub {
    void* task_;               // tasks::Task<Manager> (priority -1000) running tick_modules
    memory::ListNode modules_; // +0x04: the list's head (lists::Foremost<Module>, vtable 0x00647288)
    bool command_pending_;     // +0x18: a module was given a command since the task last ran

    // 0x0044F5E0: the empty list and the task (not added to a task list here). Returns this.
    Hub* make_new();
    // 0x0044F680: every module stopped (halt_every_module), the task deleted, the list's head unlinked.
    void destroy_body();

    // 0x0044F3C0: `module` onto the end of the list.
    void enrol_unit(PartUnit* module);
    // 0x0044F3E0: `module` out of the list (doesn't use the manager).
    void remove_module(PartUnit* module);
    // 0x0044F310: unless it's locked, `module` is started (restarted if running) when the task next runs.
    void launch_module(PartUnit* module);
    // 0x0044F530: every running module that isn't locked stopped now (its command dropped).
    void halt_every_module();

    // 0x0044F440 (the task's code, the manager its data): if a command is pending, each module's carried out -
    // a stopped one started by kStart or kRestart; a running one stopped by kStop, stopped and started again by
    // kRestart - and the command cleared.
    static void __cdecl tick_modules(void* task);
};
static_assert(sizeof(Hub) == 0x1C && offsetof(Hub, command_pending_) == 0x18);

// The singleton (0x00696D78, references 0x00696D7C). 0x0044F6F0: the first call with `create` makes it, every call
// takes a reference and returns it (null if there is none).
Hub* create_manager(bool create);
// 0x0044F740: drops a reference; the last one destroys and frees the manager.
void release_manager();

// The destructors of the module list's types (each Lst type has its own vtable).
struct ModuleListDestructors {
    // 0x0044F360 / 0x0044F380: lists::Elem<Module>'s (unlinked; the scalar deleting one frees it if flags & 1).
    void node_destroy_body();
    memory::ListNode* node_destroy(unsigned int flags);
    // 0x0044F410: lists::Foremost<Module>'s scalar deleting destructor (the node's body, then freed if flags & 1).
    memory::ListNode* head_destroy(unsigned int flags);
    // 0x0044F300 / 0x0044F330: lists::Hunt<Module>'s (only its vtable, 0x00647280, set).
    void search_destroy_body();
    ModuleListDestructors* search_destroy(unsigned int flags);
};

} // namespace modules
