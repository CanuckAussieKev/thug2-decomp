// Self-tests for the module manager (Mdl, game/sys/module): the original against ours on identical state, with the
// functions they call stood in for by recording code (standins.h).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/sys/module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

struct Originals {
    std::vector<std::unique_ptr<OriginalCode>> guards;
    Originals(bool original, std::initializer_list<uint32_t> addresses)
    {
        if (original) {
            for (uint32_t address : addresses)
                guards.push_back(std::make_unique<OriginalCode>(address));
        }
    }
};

template <typename F>
void* Pick(bool original, uint32_t address, F ours)
{
    return original ? OriginalFunction(address) : reinterpret_cast<void*>(AddressOf(ours));
}

alignas(16) uint8_t g_fakeMemoryManager[0x40];
uint32_t FakeManager() { return reinterpret_cast<uint32_t>(g_fakeMemoryManager); }

// The arena: the manager, eight modules, their nodes, a task.
alignas(16) uint8_t g_mdl[0x800];
constexpr uint32_t kManager = 0x000, kModules = 0x100, kNodes = 0x300, kTask = 0x500, kAlloc = 0x600;
uint8_t* At(uint32_t offset) { return g_mdl + offset; }
uint32_t Addr(uint32_t offset) { return reinterpret_cast<uint32_t>(g_mdl + offset); }
uint32_t Rel(uint32_t value)
{
    auto b = reinterpret_cast<uint32_t>(g_mdl);
    return value >= b && value < b + sizeof(g_mdl) ? 0xA0000000 + (value - b) : value;
}
void RecordArena(uint32_t tag)
{
    Record({tag});
    for (size_t i = 0; i < sizeof(g_mdl) / 4; ++i)
        Record({Rel(reinterpret_cast<uint32_t*>(g_mdl)[i])});
}

// The modules' and the task's vtables: recording thunks (set up by the test).
void* g_moduleVtable[3];
void* g_nodeVtable[3];
void* g_taskVtable[3];
uint32_t g_callocResult;
uint32_t g_processModules; // the task code each manager's constructor stores (theirs and ours differ)

using ThisCall0 = uint32_t(__thiscall*)(void*);
using ThisCall1 = uint32_t(__thiscall*)(void*, uint32_t);

// A list of `count` modules registered with the manager (by the code under test).
void BuildModules(bool original, int count, uint32_t seed)
{
    std::memset(g_mdl, 0, sizeof(g_mdl));
    auto* manager = reinterpret_cast<Mdl::Manager*>(At(kManager));
    manager->m_modules.data = reinterpret_cast<void*>(-1);
    manager->m_modules.next = manager->m_modules.prev = &manager->m_modules;
    manager->m_modules.vtable = reinterpret_cast<void**>(0x00647288);
    manager->m_task = At(kTask);
    *reinterpret_cast<void***>(At(kTask)) = g_taskVtable;
    auto registerModule = reinterpret_cast<ThisCall1>(Pick(original, 0x0044F3C0, &Mdl::Manager::RegisterModule));
    uint32_t x = seed;
    for (int i = 0; i < count; ++i) {
        auto* module = reinterpret_cast<Mdl::Module*>(At(kModules + i * 0x20));
        auto* node = reinterpret_cast<Mem::ListNode*>(At(kNodes + i * 0x20));
        x = x * 1103515245 + 12345;
        module->m_vtable = reinterpret_cast<void**>(g_moduleVtable);
        module->m_state = (x >> 8) % 3;     // stopped, running, or something else
        module->m_command = (x >> 12) % 5;  // none, start, stop, restart, or something else
        module->m_locked = ((x >> 16) & 7) == 0;
        module->m_node = node;
        node->data = module;
        node->vtable = g_nodeVtable;
        node->next = node->prev = node;
        registerModule(manager, reinterpret_cast<uint32_t>(module));
    }
}

std::vector<uint32_t> RunModules(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044F7E0, 0x0044F790, 0x0044F7B0, 0x0044F5E0, 0x0044F680, 0x0044F3C0, 0x0044F3E0,
                                0x0044F310, 0x0044F530, 0x0044F440, 0x0044F360, 0x0044F380, 0x0044F410, 0x0044F300,
                                0x0044F330});
    g_processModules = original ? 0x0044F440 : AddressOf(&Mdl::Manager::ProcessModules);
    // Module: constructor (with and without memory), destructors.
    for (uint32_t memory : {Addr(kAlloc), 0u}) {
        std::memset(g_mdl, 0xA5, sizeof(g_mdl));
        g_callocResult = memory;
        uint32_t r = reinterpret_cast<ThisCall0>(Pick(original, 0x0044F7E0, &Mdl::Module::Construct))(At(kModules));
        Record({0xF7E0, Rel(r)});
        RecordArena(0xF7E0);
    }
    for (uint32_t node : {Addr(kNodes), 0u}) {
        for (uint32_t flags : {0xFFFFFFFFu, 0u, 1u, 2u, 3u}) {
            std::memset(g_mdl, 0x5A, sizeof(g_mdl));
            reinterpret_cast<uint32_t*>(At(kModules))[4] = node;
            *reinterpret_cast<void***>(At(kNodes)) = g_nodeVtable;
            if (flags == 0xFFFFFFFF) {
                reinterpret_cast<ThisCall0>(Pick(original, 0x0044F790, &Mdl::Module::DestroyBody))(At(kModules));
            } else {
                uint32_t r = reinterpret_cast<ThisCall1>(Pick(original, 0x0044F7B0, &Mdl::Module::Destroy))(At(kModules), flags);
                Record({0xF7B0, Rel(r)});
            }
            RecordArena(0xF790);
        }
    }
    // Manager: constructor (with and without memory for the task).
    for (uint32_t memory : {Addr(kTask), 0u}) {
        std::memset(g_mdl, 0xC3, sizeof(g_mdl));
        g_callocResult = memory;
        uint32_t r = reinterpret_cast<ThisCall0>(Pick(original, 0x0044F5E0, &Mdl::Manager::Construct))(At(kManager));
        Record({0xF5E0, Rel(r), (memory && Resolve(reinterpret_cast<void*>(reinterpret_cast<uint32_t*>(At(kTask))[6])) == g_processModules)});
        reinterpret_cast<uint32_t*>(At(kTask))[6] = 0;
        RecordArena(0xF5E0);
    }
    // Register, unregister, start, stop all, process; the destructor.
    for (uint32_t seed = 1; seed < 40; ++seed) {
        int count = seed % 9;
        BuildModules(original, count, seed);
        RecordArena(0xF3C0);
        auto* manager = At(kManager);
        if (count > 2) {
            reinterpret_cast<ThisCall1>(Pick(original, 0x0044F3E0, &Mdl::Manager::UnregisterModule))(
                reinterpret_cast<void*>(0x12345678), Addr(kModules + (seed % count) * 0x20));
            RecordArena(0xF3E0);
        }
        if (count > 0) {
            for (int i = 0; i < count; i += 2)
                reinterpret_cast<ThisCall1>(Pick(original, 0x0044F310, &Mdl::Manager::StartModule))(
                    manager, Addr(kModules + i * 0x20));
            RecordArena(0xF310);
        }
        // The manager's task, its data the manager.
        uint32_t task[8] = {};
        task[7] = Addr(kManager);
        for (int pass = 0; pass < 2; ++pass) {
            At(kManager)[0x18] = static_cast<uint8_t>(pass == 0 ? (seed % 4 ? 1 : 0) : 1);
            reinterpret_cast<void(__cdecl*)(void*)>(Pick(original, 0x0044F440, &Mdl::Manager::ProcessModules))(task);
            RecordArena(0xF440);
        }
        BuildModules(original, count, seed * 7);
        if (seed % 2) {
            reinterpret_cast<ThisCall0>(Pick(original, 0x0044F530, &Mdl::Manager::StopAllModules))(manager);
            RecordArena(0xF530);
        } else {
            if (seed % 4 == 0)
                reinterpret_cast<uint32_t*>(manager)[0] = 0;
            reinterpret_cast<ThisCall0>(Pick(original, 0x0044F680, &Mdl::Manager::DestroyBody))(manager);
            RecordArena(0xF680);
        }
    }
    // The list types' destructors.
    for (uint32_t flags : {0xFFFFFFFFu, 0u, 1u, 3u}) {
        for (uint32_t which : {0x0044F360u, 0x0044F410u, 0x0044F300u}) {
            BuildModules(original, 4, flags + which);
            uint8_t* target = which == 0x0044F300 ? At(kAlloc) : At(kNodes + 0x40);
            if (which == 0x0044F410)
                target = At(kManager + 4);
            uint32_t r = 0;
            if (which == 0x0044F360) {
                if (flags == 0xFFFFFFFF)
                    reinterpret_cast<ThisCall0>(Pick(original, 0x0044F360, &Mdl::ModuleListDestructors::NodeDestroyBody))(target);
                else
                    r = reinterpret_cast<ThisCall1>(Pick(original, 0x0044F380, &Mdl::ModuleListDestructors::NodeDestroy))(target, flags);
            } else if (which == 0x0044F410) {
                if (flags != 0xFFFFFFFF)
                    r = reinterpret_cast<ThisCall1>(Pick(original, 0x0044F410, &Mdl::ModuleListDestructors::HeadDestroy))(target, flags);
            } else if (flags == 0xFFFFFFFF) {
                reinterpret_cast<ThisCall0>(Pick(original, 0x0044F300, &Mdl::ModuleListDestructors::SearchDestroyBody))(target);
            } else {
                r = reinterpret_cast<ThisCall1>(Pick(original, 0x0044F330, &Mdl::ModuleListDestructors::SearchDestroy))(target, flags);
            }
            Record({which, Rel(r)});
            RecordArena(which);
        }
    }
    return g_trace;
}

std::vector<uint32_t> RunMdlSingleton(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044F6F0, 0x0044F740});
    auto create = [&](bool flag, uint32_t memory) {
        g_callocResult = memory;
        void* code = Pick(original, 0x0044F6F0, &Mdl::CreateManager);
        uint32_t r = reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(code)(flag ? 0xAB01u : 0xAB00u);
        Record({0xF6F0, flag, r, GameVariable<uint32_t>(0x00696D78), GameVariable<uint32_t>(0x00696D7C)});
    };
    auto release = [&]() {
        void* code = Pick(original, 0x0044F740, &Mdl::ReleaseManager);
        reinterpret_cast<void(__cdecl*)()>(code)();
        Record({0xF740, GameVariable<uint32_t>(0x00696D78), GameVariable<uint32_t>(0x00696D7C)});
    };
    const uint32_t memory = Addr(0);
    GameVariable<uint32_t>(0x00696D78) = 0;
    GameVariable<uint32_t>(0x00696D7C) = 0;
    create(false, memory);
    create(true, 0);
    create(true, memory);
    create(true, memory + 16);
    create(false, 0);
    for (int i = 0; i < 6; ++i)
        release();
    GameVariable<uint32_t>(0x00696D78) = memory + 32;
    release();
    GameVariable<uint32_t>(0x00696D7C) = 0x80000002;
    GameVariable<uint32_t>(0x00696D78) = memory + 48;
    release();
    return g_trace;
}

void TestModules()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x00696D78, 8, {}}};
    SaveAll(saved);
    GameVariable<uint32_t>(0x007D1800) = FakeManager();
    std::vector<uint32_t> traces[2], singleton[2];
    {
        auto* s = new StandIns();
        s->At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
            Record({0xCC0, r.args[0]});
            return g_callocResult;
        });
        s->At({0x00406890}, 4, [](Regs& r) {
            Record({0x6890, Rel(r.ecx), r.args[0]});
            return r.ecx;
        });
        s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
            Record({0xF4EE, r.ecx == FakeManager(), Rel(r.args[0])});
            return 0u;
        });
        // The modules' callbacks, the nodes' and the task's deleting destructors.
        g_moduleVtable[0] = s->Thunk(4, [](Regs& r) { Record({0xBAD0, Rel(r.ecx)}); return 0u; });
        g_moduleVtable[1] = s->Thunk(0, [](Regs& r) { Record({0x51A7, Rel(r.ecx)}); return 0u; });
        g_moduleVtable[2] = s->Thunk(0, [](Regs& r) { Record({0x5709, Rel(r.ecx)}); return 0u; });
        g_nodeVtable[0] = s->Thunk(4, [](Regs& r) { Record({0xD0D0, Rel(r.ecx), r.args[0]}); return r.ecx; });
        g_nodeVtable[1] = g_nodeVtable[2] = s->Thunk(0, [](Regs&) { Record({0xBAD1}); return 0u; });
        g_taskVtable[0] = g_taskVtable[1] = s->Thunk(0, [](Regs&) { Record({0xBAD2}); return 0u; });
        g_taskVtable[2] = s->Thunk(4, [](Regs& r) { Record({0x7A5C, Rel(r.ecx), r.args[0]}); return r.ecx; });
        traces[0] = RunModules(true);
        traces[1] = RunModules(false);
        delete s;
    }
    {
        auto* s = new StandIns();
        s->At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
              [](Regs& r) {
                  Record({0xD40, r.args[0], r.args[1], r.args[2]});
                  return g_callocResult;
              });
        s->At({0x0044F5E0, AddressOf(&Mdl::Manager::Construct)}, 0, [](Regs& r) {
            Record({0xF5E0, Rel(r.ecx)});
            return r.ecx + 4;
        });
        s->At({0x0044F680, AddressOf(&Mdl::Manager::DestroyBody)}, 0, [](Regs& r) {
            Record({0xF680, Rel(r.ecx)});
            return 0u;
        });
        s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
            Record({0xF4EE, r.ecx == FakeManager(), Rel(r.args[0])});
            return 0u;
        });
        singleton[0] = RunMdlSingleton(true);
        singleton[1] = RunMdlSingleton(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "Mdl: modules, the manager, its task, the list types' destructors");
    Compare(singleton[0], singleton[1], "Mdl::Manager's singleton: create and release");
}
SELFTEST(Early, TestModules, RemoveStandIns);

} // namespace
