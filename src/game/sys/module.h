#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

// Modules (Neversoft's Mdl): the engine's parts that are started and stopped as a whole - the front end, the
// async file loader, the game's own modules. The module manager (the singleton 0x0044F6F0 makes) keeps them in a
// list; starting, stopping or restarting one is a command its task carries out at the start of the next frame.
namespace Mdl {

// The base of every module (0x14 bytes, vtable 0x0064728C: the scalar deleting destructor, then v_start_cb and
// v_stop_cb, which each module overrides).
struct Module {
    enum State : int { kStopped = 0, kRunning = 1 };
    enum Command : int { kNone = 0, kStart = 1, kStop = 2, kRestart = 3 };

    void** m_vtable;
    int m_state;       // +0x04
    int m_command;     // +0x08 (not set by the constructor)
    bool m_locked;     // +0x0C: the manager neither starts nor stops it
    uint8_t pad0D[3];
    Mem::ListNode* m_node; // +0x10: in the manager's list (Lst::Node<Module>, vtable 0x00647284)

    // 0x0044F7E0: stopped, unlocked, with a list node of its own (none if out of memory). Returns this.
    Module* Construct();
    // 0x0044F790: the node deleted.
    void DestroyBody();
    // 0x0044F7B0 (scalar deleting destructor)
    Module* Destroy(unsigned int flags);

    void StartCallback() { reinterpret_cast<void(__thiscall*)(Module*)>(m_vtable[1])(this); }
    void StopCallback() { reinterpret_cast<void(__thiscall*)(Module*)>(m_vtable[2])(this); }
};
static_assert(sizeof(Module) == 0x14 && offsetof(Module, m_locked) == 0x0C && offsetof(Module, m_node) == 0x10);

struct Manager {
    void* m_task;            // Tsk::Task<Manager> (priority -1000) running ProcessModules
    Mem::ListNode m_modules; // +0x04: the list's head (Lst::Head<Module>, vtable 0x00647288)
    bool m_commandPending;   // +0x18: a module was given a command since the task last ran

    // 0x0044F5E0: the empty list and the task (not added to a task list here). Returns this.
    Manager* Construct();
    // 0x0044F680: every module stopped (StopAllModules), the task deleted, the list's head unlinked.
    void DestroyBody();

    // 0x0044F3C0: `module` onto the end of the list.
    void RegisterModule(Module* module);
    // 0x0044F3E0: `module` out of the list (doesn't use the manager).
    void UnregisterModule(Module* module);
    // 0x0044F310: unless it's locked, `module` is started (restarted if running) when the task next runs.
    void StartModule(Module* module);
    // 0x0044F530: every running module that isn't locked stopped now (its command dropped).
    void StopAllModules();

    // 0x0044F440 (the task's code, the manager its data): if a command is pending, each module's carried out -
    // a stopped one started by kStart or kRestart; a running one stopped by kStop, stopped and started again by
    // kRestart - and the command cleared.
    static void __cdecl ProcessModules(void* task);
};
static_assert(sizeof(Manager) == 0x1C && offsetof(Manager, m_commandPending) == 0x18);

// The singleton (0x00696D78, references 0x00696D7C). 0x0044F6F0: the first call with `create` makes it, every call
// takes a reference and returns it (null if there is none).
Manager* CreateManager(bool create);
// 0x0044F740: drops a reference; the last one destroys and frees the manager.
void ReleaseManager();

// The destructors of the module list's types (each Lst type has its own vtable).
struct ModuleListDestructors {
    // 0x0044F360 / 0x0044F380: Lst::Node<Module>'s (unlinked; the scalar deleting one frees it if flags & 1).
    void NodeDestroyBody();
    Mem::ListNode* NodeDestroy(unsigned int flags);
    // 0x0044F410: Lst::Head<Module>'s scalar deleting destructor (the node's body, then freed if flags & 1).
    Mem::ListNode* HeadDestroy(unsigned int flags);
    // 0x0044F300 / 0x0044F330: Lst::Search<Module>'s (only its vtable, 0x00647280, set).
    void SearchDestroyBody();
    ModuleListDestructors* SearchDestroy(unsigned int flags);
};

} // namespace Mdl
