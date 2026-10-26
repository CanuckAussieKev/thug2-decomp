// Self-tests for the animation range's first batch (AN-1): the angle helpers (src/game/mth/vector.cpp) and the task
// lists and stacks (src/game/core/task.cpp) - the original and ours on identical stand-in state, compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/vector.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;

// --- angles ------------------------------------------------------------------------------------------------------

// The C runtime's acos isn't the game's: the results may differ in the last bit or two (the arguments match exactly).
bool Close(double expected, double actual)
{
    if (std::isnan(expected) || std::isnan(actual))
        return std::isnan(expected) && std::isnan(actual);
    return std::fabs(expected - actual) <= 1e-13 * (1.0 + std::fabs(expected));
}

// GetAngle and GetAngleAbout on random vectors (zero ones, parallel, opposite, along the axis, tiny and huge).
void TestAnimAngles()
{
    using AngleFn = double(__cdecl*)(const Mth::Vector*, const Mth::Vector*);
    using AboutFn = double(__cdecl*)(const Mth::Vector*, const Mth::Vector*, const Mth::Vector*);
    Lcg random(0x41A6);
    auto randomVector = [&]() {
        auto component = [&]() { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 997.0f; };
        Mth::Vector v{component(), component(), component(), component()};
        switch (random.Below(12)) {
        case 0: v = {0, 0, 0, 0}; break;
        case 1: v.x = 0; break;
        case 2: v = {v.x * 1e-20f, v.y * 1e-20f, v.z * 1e-20f, 0}; break;
        case 3: v = {v.x * 1e15f, v.y, v.z, 1}; break;
        case 4: v = {v.x * 1e-35f, v.y * 1e-35f, v.z * 1e-35f, 0}; break; // (shorter than 1e-30)
        default: break;
        }
        return v;
    };
    int bad = 0, total = 0;
    for (int i = 0; i < 3000; ++i) {
        Mth::Vector a = randomVector();
        Mth::Vector b = randomVector();
        Mth::Vector axis = randomVector();
        switch (random.Below(6)) {
        case 0: b = a; break;                                  // parallel: the cosine rounds to (above) 1
        case 1: b = {-a.x * 3, -a.y * 3, -a.z * 3, a.w}; break; // opposite: -1
        case 2: a = axis; break;                               // along the axis: projected to zero
        case 3: b = {a.x + 1e-4f, a.y, a.z - 1e-4f, 0}; break;
        default: break;
        }
        double expected = Original<AngleFn>(0x00406460)(&a, &b);
        double actual = Mth::GetAngle(a, b);
        ++total;
        if (!Close(expected, actual)) {
            if (++bad <= 4)
                Log("  GetAngle %d: original %.17g ours %.17g", i, expected, actual);
        }
        expected = Original<AboutFn>(0x00406630)(&a, &b, &axis);
        actual = Mth::GetAngleAbout(a, b, axis);
        ++total;
        if (!Close(expected, actual)) {
            if (++bad <= 8)
                Log("  GetAngleAbout %d: original %.17g ours %.17g", i, expected, actual);
        }
    }
    Check(bad == 0, "Mth::GetAngle / GetAngleAbout (random, parallel, opposite, zero, along the axis)");
}
SELFTEST(Early, TestAnimAngles);

// --- tasks -------------------------------------------------------------------------------------------------------

constexpr int kTasks = 10;
struct TaskArena {
    alignas(16) Tsk::Stack stack;
    Tsk::Stack stack2;
    Tsk::List list;
    uint8_t tasks[kTasks][0x20];
    uint32_t searches[4][2];
    Mem::ListNode loose[3];
    uint8_t lone[0x24];
    uint8_t heap[0x1800];
};
TaskArena g_tasks;
uint32_t g_taskHeapUsed;
bool g_taskOriginal;
int g_taskPass;

template <typename F>
F At(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_taskOriginal, address));
}
Tsk::BaseTask* TaskAt(int i) { return reinterpret_cast<Tsk::BaseTask*>(g_tasks.tasks[i]); }
int TaskIndex(uint32_t task) { return static_cast<int>((task - reinterpret_cast<uint32_t>(g_tasks.tasks)) / 0x20); }

void StackAdd(Tsk::Stack* stack, int i) { At<void(__thiscall*)(Tsk::Stack*, Tsk::BaseTask*)>(0x00406C90)(stack, TaskAt(i)); }
void TaskRemove(int i) { At<void(__thiscall*)(Tsk::BaseTask*)>(0x004068F0)(TaskAt(i)); }
void StackPush(Tsk::Stack* stack) { At<void(__thiscall*)(Tsk::Stack*)>(0x00406DE0)(stack); }
void StackPop(Tsk::Stack* stack) { At<void(__thiscall*)(Tsk::Stack*)>(0x00406FB0)(stack); }
void StackProcess(Tsk::Stack* stack) { At<void(__thiscall*)(Tsk::Stack*, void*)>(0x004070A0)(stack, nullptr); }
void StackRemoveAll(Tsk::Stack* stack) { At<void(__thiscall*)(Tsk::Stack*)>(0x00406CB0)(stack); }

// What each task does when it runs, by pass: take tasks out, add some, push and pop lists.
uint32_t RunTask(Regs& r)
{
    int i = TaskIndex(r.ecx);
    Record({0x7A500000u + static_cast<uint32_t>(i), static_cast<uint32_t>(g_taskPass)});
    Tsk::Stack* stack = &g_tasks.stack;
    switch (g_taskPass * 100 + i) {
    case 101: TaskRemove(3); StackAdd(stack, 6); break;    // a later task out, a new one in
    case 104: TaskRemove(4); break;                        // itself
    case 106: StackAdd(stack, 3); break;                   // back in after it was taken out
    case 102: TaskRemove(0); break;                        // the next task out (it must not run)
    case 201: StackPush(stack); StackAdd(stack, 7); break; // pushed: tasks still go into the old list
    case 207: StackPop(stack); break;                      // popped while locked (not the current list)
    case 301: StackPush(stack); StackAdd(stack, 8); StackRemoveAll(stack); break; // (the pushed list emptied)
    case 504: StackPop(stack); break;                      // the current list popped while locked
    case 601: StackRemoveAll(stack); break;
    default: break;
    }
    return 0;
}

// The tasks, lists and stacks: construct, add (priorities in and out of order), run with a mask (tasks skipped by
// mask and when disabled, tasks adding / removing tasks and pushing / popping lists while they run), take out, the
// destructors (tasks in and out of lists, scalar deleting with and without freeing), and the nodes', heads' and
// searches' destructors. The allocations, frees, the calls in order and the whole arena compared.
void TestAnimTasks()
{
    StandIns& s = *new StandIns();
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        uint32_t size = r.args[0];
        Record({0xCA110C00u, size});
        uint32_t at = reinterpret_cast<uint32_t>(g_tasks.heap) + g_taskHeapUsed;
        g_taskHeapUsed += (size + 7) & ~7u;
        if (g_taskHeapUsed > sizeof(g_tasks.heap))
            return 0u;
        std::memset(reinterpret_cast<void*>(at), 0xCD, size);
        std::memset(reinterpret_cast<void*>(at), 0, size);
        return at;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        std::memset(reinterpret_cast<void*>(r.args[0]), 0xDD, 8); // (freed: its vtable and first field spoilt)
        return 0u;
    });
    void* run = s.Thunk(0, RunTask);
    static void* taskVtable[3];
    taskVtable[0] = run;
    taskVtable[1] = reinterpret_cast<void*>(0x00623771);
    taskVtable[2] = reinterpret_cast<void*>(0x00406980);

    auto runOnce = [&](bool original) {
        g_taskOriginal = original;
        g_trace.clear();
        std::memset(&g_tasks, 0xEE, sizeof(g_tasks));
        g_taskHeapUsed = 0;
        g_taskPass = 0;
        if (!original) {
            // As with the hooks installed: the vtables' destructors are ours.
            auto patch = [](uint32_t slot, const void* function) {
                RunPatch(slot, Dword(Resolve(function)));
            };
            patch(0x0064549C, OursAt(0x00406860));
            patch(0x006454A4, OursAt(0x00406D20));
            patch(0x006454A8, OursAt(0x00406E50));
            patch(0x00645494, OursAt(0x00406980));
            patch(0x00645498, OursAt(0x004069D0));
            patch(0x006454A0, OursAt(0x00406CD0));
            taskVtable[2] = OursAt(0x00406980);
        } else {
            taskVtable[2] = reinterpret_cast<void*>(0x00406980);
        }
        Tsk::Stack* stack = &g_tasks.stack;
        Record({reinterpret_cast<uint32_t>(At<Tsk::Stack*(__thiscall*)(Tsk::Stack*)>(0x00406E80)(stack))});
        for (int k = 0; k < 15; ++k)
            Record({reinterpret_cast<uint32_t*>(stack)[k]}); // (as constructed: the flags, the heads)
        stack->m_mask = 0x4;
        const int priorities[kTasks] = {5, 0, 5, -3, 10, 0, 7, 2, 5, -1};
        for (int i = 0; i < kTasks; ++i) {
            Tsk::BaseTask* task = TaskAt(i);
            Record({reinterpret_cast<uint32_t>(At<Tsk::BaseTask*(__thiscall*)(Tsk::BaseTask*, int)>(0x00406890)(task, priorities[i]))});
            task->m_vtable = taskVtable;
        }
        TaskAt(5)->m_mask = 0x6; // shares the stack's mask bit: skipped
        TaskAt(9)->m_mask = 0x1;
        TaskAt(9)->m_disabled = 1;
        for (int i : {0, 1, 2, 3, 4, 5, 9})
            StackAdd(stack, i);
        auto inList = At<bool(__thiscall*)(Tsk::BaseTask*)>(0x00406850);
        for (int i = 0; i < kTasks; ++i)
            Record({0x1A000000u + inList(TaskAt(i))});
        for (g_taskPass = 1; g_taskPass <= 6; ++g_taskPass) {
            if (g_taskPass == 5)
                StackAdd(stack, 4); // into the list pushed in pass 3 (the only task it runs)
            StackProcess(stack);
            Record({0x9A550000u + static_cast<uint32_t>(g_taskPass)});
        }
        // (task 4 is left pointing at its deleted list: not used again)
        // RemoveAllTasks with nothing pushed: the current list.
        StackRemoveAll(stack);
        for (int i : {1, 3, 9})
            StackAdd(stack, i);
        g_taskPass = 9;
        StackProcess(stack);

        // A list on its own: processed with masks, locked (nothing runs), emptied.
        Tsk::List* list = &g_tasks.list;
        Record({reinterpret_cast<uint32_t>(At<Tsk::List*(__thiscall*)(Tsk::List*)>(0x00406A50)(list))});
        auto listAdd = At<void(__thiscall*)(Tsk::List*, Tsk::BaseTask*)>(0x00406A80);
        auto listProcess = At<void(__thiscall*)(Tsk::List*, void*, uint32_t)>(0x00406AC0);
        TaskRemove(5);
        TaskRemove(8);
        listAdd(list, TaskAt(5));
        listAdd(list, TaskAt(8));
        TaskRemove(0);
        listAdd(list, TaskAt(0));
        g_taskPass = 10;
        listProcess(list, nullptr, 0);
        listProcess(list, nullptr, 0x2);
        list->m_lock = 1;
        listProcess(list, nullptr, 0);
        list->m_lock = 0;
        TaskRemove(8); // (outside a pass: the list is marked changed)
        Record({0xC4A60000u + list->m_changed});
        listAdd(list, TaskAt(8));
        At<void(__thiscall*)(Tsk::List*)>(0x004069B0)(list);
        At<void(__thiscall*)(Tsk::List*)>(0x00406BB0)(list);
        listProcess(list, nullptr, 0);

        // Destructors: a task in a list, one not in one; deleting with and without freeing.
        StackAdd(stack, 6);
        At<void(__thiscall*)(Tsk::BaseTask*)>(0x00406930)(TaskAt(6));
        At<void(__thiscall*)(Tsk::BaseTask*)>(0x00406930)(TaskAt(5));
        StackAdd(stack, 7);
        Record({reinterpret_cast<uint32_t>(At<Tsk::BaseTask*(__thiscall*)(Tsk::BaseTask*, uint8_t)>(0x00406980)(TaskAt(7), 0))});
        Record({reinterpret_cast<uint32_t>(At<Tsk::BaseTask*(__thiscall*)(Tsk::BaseTask*, uint8_t)>(0x00406980)(TaskAt(8), 1))});
        At<void(__thiscall*)(Tsk::List*)>(0x00406A20)(list);

        // A second stack: pushed and popped unlocked, then destroyed with lists on it and popped ones waiting.
        Tsk::Stack* other = &g_tasks.stack2;
        At<Tsk::Stack*(__thiscall*)(Tsk::Stack*)>(0x00406E80)(other);
        other->m_mask = 0;
        StackPush(other);
        g_taskPass = 11;
        StackProcess(other); // (the pushed list becomes the current one)
        StackAdd(other, 2);
        StackPop(other);
        StackPush(other);
        other->m_lock = 1;
        StackPop(other);
        other->m_lock = 0;
        TaskRemove(9);
        StackAdd(other, 9);
        At<void(__thiscall*)(Tsk::Stack*)>(0x004070E0)(other);
        At<void(__thiscall*)(Tsk::Stack*)>(0x00406F10)(stack);
        At<void(__thiscall*)(Tsk::Stack*)>(0x004070E0)(stack);

        // The searches', nodes' and heads' destructors on their own.
        for (int k = 0; k < 4; ++k)
            g_tasks.searches[k][0] = 0x5EA00000u + k;
        At<void(__thiscall*)(void*)>(0x004069C0)(g_tasks.searches[0]);
        At<void(__thiscall*)(void*)>(0x00406CA0)(g_tasks.searches[1]);
        Record({reinterpret_cast<uint32_t>(At<void*(__thiscall*)(void*, uint8_t)>(0x004069D0)(g_tasks.searches[2], 0))});
        Record({reinterpret_cast<uint32_t>(At<void*(__thiscall*)(void*, uint8_t)>(0x00406CD0)(g_tasks.searches[3], 1))});
        Mem::ListNode* loose = g_tasks.loose;
        for (int k = 0; k < 3; ++k) {
            loose[k] = {reinterpret_cast<void**>(0x11110000u + k), nullptr, k, &loose[(k + 1) % 3], &loose[(k + 2) % 3]};
        }
        At<void(__thiscall*)(void*)>(0x00406A00)(&loose[0]);
        Record({reinterpret_cast<uint32_t>(loose[0].vtable)});
        Record({reinterpret_cast<uint32_t>(At<void*(__thiscall*)(void*, uint8_t)>(0x00406E50)(&loose[1], 1))});
        At<void(__thiscall*)(void*)>(0x00406D00)(&loose[2]);
        Record({reinterpret_cast<uint32_t>(At<void*(__thiscall*)(void*, uint8_t)>(0x00406D20)(&loose[2], 0))});
        Record({reinterpret_cast<uint32_t>(At<void*(__thiscall*)(void*, uint8_t)>(0x00406860)(&loose[0], 1))});
        // A stack list constructed and destroyed on its own.
        auto* lone = reinterpret_cast<Tsk::StackList*>(g_tasks.lone);
        At<Tsk::StackList*(__thiscall*)(Tsk::StackList*)>(0x00406D60)(lone);
        At<void(__thiscall*)(Tsk::StackList*)>(0x00406C30)(lone);

        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_tasks);
        trace.insert(trace.end(), words, words + sizeof(g_tasks) / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Tsk tasks, lists and stacks (add / run / remove / push / pop / destroy)");
}
SELFTEST(Early, TestAnimTasks, TestStandIns::RemoveStandIns);

} // namespace
