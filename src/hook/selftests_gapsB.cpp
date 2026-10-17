// Self-tests for the gaps next to Sys::Main, MainLoop and WinMain: the SingletonPtr instances (game/core/singleton),
// Mlp::Manager's other methods and its singleton, the window procedure, and the FlipRotate component's small
// methods. Each compares the original with ours on identical state, with everything they call stood in for by
// recording code (standins.h).

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/singleton.h"
#include "game/file/async.h"
#include "game/file/pre.h"
#include "game/mc/memcard.h"
#include "game/obj/flip_rotate.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/script/cache.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/mlp.h"
#include "game/sys/winmain.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

// Our function registered at a game address (from the replacements' own table).
void* OursAt(uint32_t address)
{
    static std::map<uint32_t, void*> ours;
    if (ours.empty()) {
        for (const ArgumentSizeCheck& c : CheckArgumentSizes()) {
            if (!c.replacement.fallback)
                ours[c.replacement.address] = c.replacement.function;
        }
    }
    auto it = ours.find(address);
    return it == ours.end() ? nullptr : it->second;
}
void* Code(bool original, uint32_t address) { return original ? OriginalFunction(address) : OursAt(address); }

// In the original's run, every call to these addresses (from inside originals too) runs the original.
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

using ThisCall0 = uint32_t(__thiscall*)(void*);
using ThisCall1 = uint32_t(__thiscall*)(void*, uint32_t);

alignas(16) uint8_t g_fakeMemoryManager[0x40];
uint32_t FakeManager() { return reinterpret_cast<uint32_t>(g_fakeMemoryManager); }

// Mem::Manager::Free (the game's and ours), recorded.
void StandInFree(StandIns& s)
{
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, r.ecx == FakeManager(), r.args[0]});
        return 0u;
    });
}

// --- SingletonPtr (game/core/singleton) ---------------------------------------------------------------------------

struct Instance {
    uint32_t destroyBody;
    uint32_t destroy;
    uint32_t vtable;
    uint32_t release;
    uint32_t oursRelease; // our Release function, if it has source
};
std::vector<Instance> Instances()
{
    return {
        {0x0044FD40, 0x0044FD60, 0x006472B4, 0x0047C6C0, 0},
        {0x0044FD50, 0x0044FD90, 0x006472B8, 0x005BDCF0, AddressOf(&Sio::ReleaseManager)},
        {0x004B2230, 0x004B2240, 0x0064A2EC, 0x0044F2B0, AddressOf(&Mlp::ReleaseManager)},
        {0x004E1980, 0x004E1B00, 0x0064AFB0, 0x005B9510, AddressOf(&Pre::DestroyManager)},
        {0x004E1990, 0x004E1B30, 0x0064AFB4, 0x00461FA0, AddressOf(&Obj::ReleaseTracker)},
        {0x004E19A0, 0x004E1B60, 0x0064AFB8, 0x0044F740, 0},
        {0x004E19B0, 0x004E1B90, 0x0064AFBC, 0x005BC950, AddressOf(&Mc::ReleaseManager)},
        {0x004E19C0, 0x004E1BC0, 0x0064AFC0, 0x0044E6B0, 0},
        {0x004E19D0, 0x004E1BF0, 0x0064AFC4, 0x005B6620, AddressOf(&File::DestroyAsyncLoaderModule)},
        {0x004E19E0, 0x004E1C20, 0x0064AFC8, 0x00475A00, AddressOf(&Script::ReleaseScriptCache)},
        {0x004E19F0, 0x004E1C50, 0x0064AFCC, 0x00407980, 0},
        {0x004E1A00, 0x004E1C80, 0x0064AFD0, 0x004558D0, 0},
        {0x004E1A10, 0x004E1CB0, 0x0064AFD4, 0x0045F1E0, AddressOf(&Obj::ReleaseCompositeObjectManager)},
        {0x004E1A20, 0x004E1CE0, 0x0064AFD8, 0x005880E0, 0},
        {0x004E1A30, 0x004E1D10, 0x0064AFDC, 0x00552F70, 0},
        {0x004E1A40, 0x004E1D40, 0x0064AFE0, 0x0057C120, 0},
        {0x004E1A50, 0x004E1D70, 0x0064AFE4, 0x0057FDB0, 0},
        {0x004E1A60, 0x004E1DA0, 0x0064AFE8, 0x00564C10, 0},
        {0x004E1A70, 0x004E1DD0, 0x0064AFEC, 0x0054EBE0, 0},
        {0x004E1A80, 0x004E1E00, 0x0064AFF0, 0x005395C0, 0},
        {0x004E1AC0, 0x004E1E30, 0x0064AFF4, 0x0058D290, 0},
        {0x004E1AD0, 0x004E1E60, 0x0064AFF8, 0x004AACF0, 0},
        {0x004E1AE0, 0x004E1E90, 0x0064AFFC, 0x0045D6E0, 0},
        {0x004E1AF0, 0x004E1EC0, 0x0064B000, 0x00574910, 0},
    };
}

uint32_t g_object[4];

std::vector<uint32_t> RunSingletonPtrs(bool original)
{
    g_trace.clear();
    const uint32_t base = reinterpret_cast<uint32_t>(g_object);
    for (const Instance& in : Instances()) {
        Originals guards(original, {in.destroyBody, in.destroy});
        Record({0x1000, in.destroyBody});
        g_object[0] = 0xDEAD0000 ^ in.vtable;
        g_object[1] = 0x0B1EC7;
        reinterpret_cast<ThisCall0>(Code(original, in.destroyBody))(g_object);
        Record({g_object[0], g_object[1]});
        for (uint32_t flags : {0u, 1u, 2u, 3u, 0xFFFFFFFEu}) {
            g_object[0] = 0xDEAD0000 + flags;
            uint32_t result = reinterpret_cast<ThisCall1>(Code(original, in.destroy))(g_object, flags);
            Record({flags, result - base, g_object[0], g_object[1]});
        }
    }
    // The out-of-line constructor (SingletonPtr for 0x00590160's singleton) and the pointer member's constructor.
    Originals guards(original, {0x004E1A90, 0x004E1970});
    for (uint32_t create : {0u, 1u, 0x100u, 0xFFu}) {
        std::memset(g_object, 0xCD, sizeof(g_object));
        uint32_t result = reinterpret_cast<ThisCall1>(Code(original, 0x004E1A90))(g_object, create);
        Record({0x1A90, result - base, g_object[0], g_object[1], g_object[2]});
    }
    for (uint32_t pointer : {0u, 0x12345678u}) {
        std::memset(g_object, 0xCD, sizeof(g_object));
        uint32_t result = reinterpret_cast<ThisCall1>(Code(original, 0x004E1970))(g_object + 1, pointer);
        Record({0x1970, result - base, g_object[0], g_object[1], g_object[2]});
    }
    return g_trace;
}

void TestSingletonPtrs()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}};
    SaveAll(saved);
    GameVariable<uint32_t>(0x007D1800) = FakeManager();
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns();
        std::vector<Instance> instances = Instances();
        for (size_t i = 0; i < instances.size(); ++i) {
            Handler handler = [i](Regs&) {
                Record({0x5E1E, static_cast<uint32_t>(i)});
                return 0x77u;
            };
            if (instances[i].oursRelease)
                s->At({instances[i].release, instances[i].oursRelease}, 0, handler);
            else
                s->At({instances[i].release}, 0, handler);
        }
        s->At({0x00590160}, 0, [](Regs& r) {
            Record({0x0160, r.args[0] & 0xFF});
            return 0xC0DE0000u + (r.args[0] & 0xFF);
        });
        StandInFree(*s);
        traces[0] = RunSingletonPtrs(true);
        traces[1] = RunSingletonPtrs(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "SingletonPtr: 24 destructors and scalar destructors, the constructor 0x004E1A90");
}

// --- Mlp::Manager -------------------------------------------------------------------------------------------------

alignas(16) uint8_t g_mlpBuffer[0x100];
uint32_t MlpOffset(uint32_t p) { return p - reinterpret_cast<uint32_t>(g_mlpBuffer); }
void* g_device[1];
void* g_deviceVtable[20];
HRESULT g_cooperative;
HRESULT g_reset;

bool& DeviceLost() { return GameVariable<bool>(0x00786AB4); }

std::vector<uint32_t> RunMlpMethods(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044EDC0, 0x0044EE30, 0x0044F220, 0x0044F240, 0x0044EE90, 0x0044F250});
    // The constructor and destructor on dirty memory.
    std::memset(g_mlpBuffer, 0xA5, sizeof(g_mlpBuffer));
    uint32_t result = reinterpret_cast<ThisCall0>(Code(original, 0x0044EDC0))(g_mlpBuffer);
    Record({0xEDC0, MlpOffset(result), HashBytes(g_mlpBuffer, sizeof(g_mlpBuffer))});
    reinterpret_cast<ThisCall0>(Code(original, 0x0044EE30))(g_mlpBuffer);
    Record({0xEE30, HashBytes(g_mlpBuffer, sizeof(g_mlpBuffer))});
    // ProfileTasks, ResetEngine.
    for (uint32_t frames : {0u, 7u, 0xFFFFFFFFu}) {
        std::memset(g_mlpBuffer, 0x5A, sizeof(g_mlpBuffer));
        reinterpret_cast<ThisCall1>(Code(original, 0x0044F220))(g_mlpBuffer, frames);
        Record({0xF220, HashBytes(g_mlpBuffer, sizeof(g_mlpBuffer))});
    }
    std::memset(g_mlpBuffer, 0x5A, sizeof(g_mlpBuffer));
    reinterpret_cast<ThisCall0>(Code(original, 0x0044F240))(g_mlpBuffer);
    Record({0xF240, HashBytes(g_mlpBuffer, sizeof(g_mlpBuffer))});
    // The device reset: not lost; lost and still lost; resettable, Reset failing; resettable (other codes), Reset
    // working. Through the function and through the jump at 0x0044F250 (ours: the one function).
    struct Case {
        bool lost;
        HRESULT cooperative;
        HRESULT reset;
    };
    const Case cases[] = {
        {false, D3D_OK, D3D_OK},
        {true, D3DERR_DEVICELOST, D3D_OK},
        {true, D3DERR_DEVICENOTRESET, static_cast<HRESULT>(0x80000001)},
        {true, D3DERR_DEVICENOTRESET, D3D_OK},
        {true, D3D_OK, 1},
        {true, static_cast<HRESULT>(0x88760869), D3DERR_INVALIDCALL},
    };
    for (uint32_t via : {0x0044EE90u, 0x0044F250u}) {
        for (const Case& c : cases) {
            DeviceLost() = c.lost;
            g_cooperative = c.cooperative;
            g_reset = c.reset;
            void* code = original ? OriginalFunction(via) : reinterpret_cast<void*>(&Mlp::Manager::ResetDeviceIfLost);
            reinterpret_cast<void(__cdecl*)()>(code)();
            Record({0xEE90, DeviceLost()});
        }
    }
    return g_trace;
}

uint32_t g_callocResult;

std::vector<uint32_t> RunMlpSingleton(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044F260, 0x0044F2B0});
    auto create = [&](bool flag, uint32_t memory) {
        g_callocResult = memory;
        void* code = original ? OriginalFunction(0x0044F260) : reinterpret_cast<void*>(&Mlp::CreateManager);
        uint32_t result = reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(code)(flag ? 0xAB01u : 0xAB00u);
        Record({0xF260, flag, result, GameVariable<uint32_t>(0x00696D68), GameVariable<uint32_t>(0x00696D6C)});
    };
    auto release = [&]() {
        void* code = original ? OriginalFunction(0x0044F2B0) : reinterpret_cast<void*>(&Mlp::ReleaseManager);
        reinterpret_cast<void(__cdecl*)()>(code)();
        Record({0xF2B0, GameVariable<uint32_t>(0x00696D68), GameVariable<uint32_t>(0x00696D6C)});
    };
    const uint32_t memory = reinterpret_cast<uint32_t>(g_mlpBuffer);
    GameVariable<uint32_t>(0x00696D68) = 0;
    GameVariable<uint32_t>(0x00696D6C) = 0;
    create(false, memory);     // none: a reference to nothing
    create(true, 0);           // out of memory
    create(true, memory);      // made
    create(true, memory + 16); // there already
    create(false, 0);
    release();                 // 5 -> 4
    release();
    release();
    release();                 // 2 -> 1
    release();                 // the last: destroyed and freed
    release();                 // none
    GameVariable<uint32_t>(0x00696D68) = memory + 32; // an instance with no references counted
    release();
    GameVariable<uint32_t>(0x00696D6C) = 0x80000002; // (an unsigned count)
    GameVariable<uint32_t>(0x00696D68) = memory + 48;
    release();
    GameVariable<uint32_t>(0x00696D6C) = 0xFFFFFFFF;
    create(false, 0); // the count wraps round
    return g_trace;
}

void TestMlpManager()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x00786AB4, 1, {}}, {0x007D69E0, 4, {}},
                                {0x00696D68, 8, {}}};
    SaveAll(saved);
    GameVariable<uint32_t>(0x007D1800) = FakeManager();
    std::vector<uint32_t> traces[2];
    std::vector<uint32_t> singletonTraces[2];
    {
        auto* s = new StandIns();
        s->At({0x00406E80}, 0, [](Regs& r) { Record({0x6E80, MlpOffset(r.ecx)}); return r.ecx; });
        s->At({0x004070E0}, 0, [](Regs& r) { Record({0x70E0, MlpOffset(r.ecx)}); return 0u; });
        for (uint32_t address : {0x004E0320u, 0x004D67F0u, 0x004D7AF0u, 0x004E0440u, 0x004D68C0u, 0x004D8420u}) {
            s->At({address}, 0, [address](Regs&) {
                Record({address});
                return 0u;
            });
        }
        for (void*& entry : g_deviceVtable)
            entry = s->Thunk(0, [](Regs&) {
                Record({0xBAD});
                return 0u;
            });
        g_deviceVtable[3] = s->Thunk(4, [](Regs& r) {
            Record({0xC00B, r.args[0] == reinterpret_cast<uint32_t>(g_device)});
            return static_cast<uint32_t>(g_cooperative);
        });
        g_deviceVtable[16] = s->Thunk(8, [](Regs& r) {
            Record({0x5E5E, r.args[0] == reinterpret_cast<uint32_t>(g_device), r.args[1]});
            return static_cast<uint32_t>(g_reset);
        });
        g_device[0] = g_deviceVtable;
        GameVariable<void*>(0x007D69E0) = g_device;
        traces[0] = RunMlpMethods(true);
        traces[1] = RunMlpMethods(false);
        delete s;
    }
    {
        auto* s = new StandIns();
        s->At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
              [](Regs& r) {
                  Record({0xCA11, r.args[0], r.args[1], r.args[2]});
                  return g_callocResult;
              });
        s->At({0x0044EDC0, AddressOf(&Mlp::Manager::Construct)}, 0, [](Regs& r) {
            Record({0xEDC0, MlpOffset(r.ecx)});
            return r.ecx + 4; // (what the constructor returns is what's stored)
        });
        s->At({0x0044EE30, AddressOf(&Mlp::Manager::DestroyBody)}, 0, [](Regs& r) {
            Record({0xEE30, MlpOffset(r.ecx)});
            return 0u;
        });
        StandInFree(*s);
        singletonTraces[0] = RunMlpSingleton(true);
        singletonTraces[1] = RunMlpSingleton(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "Mlp::Manager: constructor, destructor, ProfileTasks, ResetEngine, the device reset");
    Compare(singletonTraces[0], singletonTraces[1], "Mlp::Manager's singleton: create and release");
}

// --- the window procedure (0x004E16B0) ----------------------------------------------------------------------------

std::vector<uint32_t> RunWindowProc(bool original)
{
    g_trace.clear();
    const uint32_t messages[] = {0x0000, 0x0001, 0x0002, 0x0003, 0x0005, 0x0006, 0x0007, 0x000E, 0x000F, 0x0010,
                                 0x0011, 0x0012, 0x00FF, 0x0100, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0111,
                                 0x0112, 0x0113, 0x0217, 0x0218, 0x0219, 0x0400, 0xFFFFFFFF};
    const uint32_t wParams[] = {0, 1, 2, 3, 0x00010001, 0x00020000, 0x79, 0x0179, 0xF100, 0xF101, 0x0001F100};
    using Proc = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
    Proc proc = original ? reinterpret_cast<Proc>(OriginalFunction(0x004E16B0)) : &Sys::WindowProc;
    uint32_t n = 0;
    for (uint32_t message : messages) {
        for (uint32_t wParam : wParams) {
            ++n;
            GameVariable<uint8_t>(0x007D6A2C) = static_cast<uint8_t>(n * 3);
            GameVariable<uint8_t>(0x007CCBE4) = static_cast<uint8_t>(n * 5 + 2);
            GameVariable<uint8_t>(0x00786AB4) = static_cast<uint8_t>(n * 7 + 1);
            GameVariable<uint32_t>(0x007D6A28) = 0x6A280000 + n;
            LRESULT result = proc(reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1D0000 + n)), message, wParam,
                                  static_cast<LPARAM>(0x40000000 + n));
            Record({message, wParam, static_cast<uint32_t>(result), GameVariable<uint8_t>(0x007D6A2C),
                    GameVariable<uint8_t>(0x007CCBE4), GameVariable<uint8_t>(0x00786AB4)});
        }
    }
    return g_trace;
}

void TestWindowProc()
{
    std::vector<Saved> saved = {{0x007D6A2C, 1, {}}, {0x007CCBE4, 1, {}}, {0x00786AB4, 1, {}}, {0x007D6A28, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns();
        StandInApi(*s, 0x0064525C, reinterpret_cast<const void*>(&PostQuitMessage), 4, [](Regs& r) {
            Record({0x9017, r.args[0]});
            return 0u;
        });
        StandInApi(*s, 0x00645228, reinterpret_cast<const void*>(&ShowWindow), 8, [](Regs& r) {
            Record({0x5A0, r.args[0], r.args[1]});
            return 0u;
        });
        StandInApi(*s, 0x00645230, reinterpret_cast<const void*>(&InvalidateRect), 12, [](Regs& r) {
            Record({0x1A7, r.args[0], r.args[1], r.args[2]});
            return 1u;
        });
        StandInApi(*s, 0x00645234, reinterpret_cast<const void*>(&ValidateRect), 8, [](Regs& r) {
            Record({0x7A7, r.args[0], r.args[1]});
            return 1u;
        });
        StandInApi(*s, 0x00645208, reinterpret_cast<const void*>(&DefWindowProcA), 16, [](Regs& r) {
            Record({0xDEF, r.args[0], r.args[1], r.args[2], r.args[3]});
            return 0xDEF00000u + r.args[1];
        });
        s->At({0x005BDE70, AddressOf(&Sio::KeyInput)}, 0, [](Regs& r) {
            Record({0xDE70, r.args[0], r.args[1]});
            return 0x55u;
        });
        s->At({0x005BDF30, AddressOf(&Sio::SpecialKeyInput)}, 0, [](Regs& r) {
            Record({0xDF30, r.args[0], r.args[1]});
            return 0x66u;
        });
        traces[0] = RunWindowProc(true);
        traces[1] = RunWindowProc(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "WindowProc: every message it handles (and neighbours), every wParam case");
}

// --- the FlipRotate component -------------------------------------------------------------------------------------

alignas(16) uint8_t g_component[0x20];
uint32_t ComponentOffset(uint32_t p) { return p - reinterpret_cast<uint32_t>(g_component); }
// The parameters' answers: per name, whether it's there and its value.
struct Answer {
    bool found;
    int value;
};
Answer g_flipped;
Answer g_rotated;

std::vector<uint32_t> RunFlipRotate(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x004E2C50, 0x004E2C80, 0x004E2C90, 0x004E2CF0});
    std::memset(g_component, 0xC3, sizeof(g_component));
    uint32_t result = reinterpret_cast<ThisCall1>(Code(original, 0x004E2C50))(g_component, 0x0B0B0000);
    Record({0x2C50, ComponentOffset(result), HashBytes(g_component, sizeof(g_component))});
    const Answer answers[] = {{false, 1}, {true, 0}, {true, 5}, {true, -1}, {true, 0x100}};
    for (const Answer& flipped : answers) {
        for (const Answer& rotated : answers) {
            g_flipped = flipped;
            g_rotated = rotated;
            for (int before : {0, 1, 0xFE}) {
                g_component[0x14] = static_cast<uint8_t>(before);
                g_component[0x15] = static_cast<uint8_t>(before ^ 1);
                reinterpret_cast<ThisCall1>(Code(original, 0x004E2C90))(g_component, 0x57500000);
                Record({0x2C90, g_component[0x14], g_component[0x15], HashBytes(g_component, sizeof(g_component))});
            }
        }
    }
    std::memset(g_component, 0xC3, sizeof(g_component));
    reinterpret_cast<ThisCall0>(Code(original, 0x004E2C80))(g_component);
    Record({0x2C80, HashBytes(g_component, sizeof(g_component))});
    for (uint32_t flags : {0u, 1u, 2u, 3u}) {
        std::memset(g_component, 0xC3, sizeof(g_component));
        result = reinterpret_cast<ThisCall1>(Code(original, 0x004E2CF0))(g_component, flags);
        Record({0x2CF0, ComponentOffset(result), HashBytes(g_component, sizeof(g_component))});
    }
    return g_trace;
}

void TestFlipRotateComponent()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}};
    SaveAll(saved);
    GameVariable<uint32_t>(0x007D1800) = FakeManager();
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns();
        s->At({0x00481710}, 4, [](Regs& r) {
            Record({0x1710, ComponentOffset(r.ecx), r.args[0]});
            auto* p = reinterpret_cast<uint32_t*>(r.ecx);
            p[0] = 0x0064877C;
            p[1] = r.args[0];
            p[2] = p[3] = p[4] = 0;
            return r.ecx;
        });
        s->At({0x00481730}, 0, [](Regs& r) {
            Record({0x1730, ComponentOffset(r.ecx), *reinterpret_cast<uint32_t*>(r.ecx)});
            *reinterpret_cast<uint32_t*>(r.ecx) = 0x0064877C;
            return 0u;
        });
        s->At({0x00481740}, 4, [](Regs& r) {
            Record({0x1740, ComponentOffset(r.ecx), r.args[0]});
            reinterpret_cast<uint32_t*>(r.ecx)[4] = 0x40C60000;
            return 0u;
        });
        s->At({0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(
                               &Script::CStruct::GetInteger))},
              12, [](Regs& r) {
                  Record({0x6630, r.ecx, r.args[0], r.args[2]});
                  const Answer& answer = r.args[0] == 0x0C7A712C ? g_flipped : g_rotated;
                  if (answer.found)
                      *reinterpret_cast<int*>(r.args[1]) = answer.value;
                  return answer.found ? 0xFFFFFF01u : 0xFFFFFF00u; // (only AL counts)
              });
        StandInFree(*s);
        traces[0] = RunFlipRotate(true);
        traces[1] = RunFlipRotate(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "FlipRotate component: constructor, InitFromStructure (flipped/rotated), destructors");
}

} // namespace

SELFTEST(Early, TestSingletonPtrs, RemoveStandIns);
SELFTEST(Early, TestMlpManager, RemoveStandIns);
SELFTEST(Early, TestWindowProc, RemoveStandIns);
SELFTEST(Early, TestFlipRotateComponent, RemoveStandIns);
