// Self-tests for the Velocity, Vibration and WaitAndDie components (game/obj/velocity_component.cpp,
// vibration_component.cpp, wait_and_die.cpp) and the feeler's assignment (game/nx/feeler.cpp, 0x0043A930): the
// originals against ours over random components, every call they make recorded by stand-ins (hook/standins.h).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_line.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/velocity_component.h"
#include "game/obj/vibration_component.h"
#include "game/obj/wait_and_die.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    float Range(float low, float high) { return low + (high - low) * static_cast<float>(Below(10001)) / 10000.0f; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

alignas(16) uint8_t g_component[0x40];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_device[0x100];
alignas(16) uint8_t g_suspend[0x40];
alignas(16) uint8_t g_net[0x200];
alignas(16) uint8_t g_params[0x10];
alignas(16) uint8_t g_block[2][0x40];
alignas(16) uint32_t g_vtable[16]; // an object's vtable: slot 6 (MarkAsDead) recorded
alignas(16) uint8_t g_feelers[2][0xB0];
alignas(16) uint8_t g_refs[3][0x10];
int g_blockNext;

// This case's world.
bool g_inNet, g_suspended, g_found[4];
int g_clock, g_clockStep;
double g_frame;
float g_floats[4];
int g_integer;
uint32_t g_checksum;
bool g_objectFound;
Script::VectorValue g_vector;

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        return p >= b && p < b + size ? tag + (p - b) : 0;
    };
    if (!p)
        return 0;
    for (uint32_t r : {in(g_component, sizeof(g_component), 0x51000000), in(g_object, sizeof(g_object), 0x52000000),
                       in(g_device, sizeof(g_device), 0x53000000), in(g_suspend, sizeof(g_suspend), 0x54000000),
                       in(g_net, sizeof(g_net), 0x55000000), in(g_params, sizeof(g_params), 0x56000000),
                       in(g_block, sizeof(g_block), 0x57000000), in(g_feelers, sizeof(g_feelers), 0x58000000),
                       in(g_refs, sizeof(g_refs), 0x59000000), in(g_vtable, sizeof(g_vtable), 0x5A000000)})
        if (r)
            return r;
    return p;
}

double __cdecl StandInFrame()
{
    Record({0xF7A3});
    return g_frame;
}

// The names the params answer: 0 "found" names (index by name), with values.
int NameIndex(uint32_t name)
{
    switch (name) {
    case 0x9E497FC6: return 0; // percent
    case 0x79A07F3F: return 1; // duration
    case 0xC218CF77: return 2; // LifeTime
    case 0x114FC131: return 3; // actuator
    default: return -1;
    }
}

void StandInAll(StandIns& s)
{
    using Script::CStruct;
    for (uint32_t address : {0x005BEAA0u, AddressOf(Tmr::GetSlomoFrameSeconds)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInFrame)));
    s.At({0x0044ECE0, AddressOf(Tmr::GameClockElapsed)}, 0, [](Regs&) {
        g_clock += g_clockStep;
        Record({0xC10C, static_cast<uint32_t>(g_clock)});
        return static_cast<uint32_t>(g_clock);
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0x4E4F, Id(r.ecx)});
        return static_cast<uint32_t>(g_inNet);
    });
    s.At({0x00433E90}, 0, [](Regs& r) {
        Record({0x433E, Id(r.ecx)});
        return static_cast<uint32_t>(g_suspended);
    });
    s.At({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [](Regs& r) {
             Record({0x6FB0, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<Script::VectorValue*>(r.args[1]) = g_vector;
             return 1u;
         });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x6F10, Id(r.ecx), r.args[0], r.args[2], *reinterpret_cast<uint32_t*>(r.args[1])});
             int i = NameIndex(r.args[0]);
             if (i < 0 || !g_found[i])
                 return 0u;
             *reinterpret_cast<float*>(r.args[1]) = g_floats[i];
             return 1u;
         });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Record({0x6630, Id(r.ecx), r.args[0], r.args[2], *reinterpret_cast<uint32_t*>(r.args[1])});
             if (!g_found[3])
                 return 0u;
             *reinterpret_cast<int*>(r.args[1]) = g_integer;
             return 1u;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6950, Id(r.ecx), r.args[0], Id(r.args[1]), r.args[2]});
             if (!g_checksum)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = g_checksum;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Id(r.ecx), r.args[0]});
        return static_cast<uint32_t>(g_found[0] && g_found[1] && r.args[0] == 0xD443A2BC);
    });
    s.At({0x00460AD0, AddressOf(Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return g_objectFound ? reinterpret_cast<uint32_t>(g_refs[0]) : 0u;
    });
    // The device's motors (EnableActuators, 0x005BCCB0, runs for real: one byte).
    s.At({0x005BDA20, AddressOf(&Sio::Device::ActivateActuator)}, 8, [](Regs& r) {
        Record({0xDA20, Id(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005BDC60, AddressOf(&Sio::Device::StopActuators)}, 0, [](Regs& r) { Record({0xDC60, Id(r.ecx)}); return 0u; });
    s.At({0x005BDAD0, AddressOf(&Sio::Device::DisableActuators)}, 0, [](Regs& r) { Record({0xDAD0, Id(r.ecx)}); return 0u; });
    s.At({0x005BDB30, AddressOf(&Sio::Device::ResetActuators)}, 0, [](Regs& r) { Record({0xDB30, Id(r.ecx)}); return 0u; });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0x4E6, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA11, r.args[0]});
        return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF5EE, Id(r.args[0])});
        return 0u;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD7, Id(r.ecx), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    // The object's MarkAsDead (vtable slot 6).
    g_vtable[6] = reinterpret_cast<uint32_t>(s.Thunk(0, [](Regs& r) {
        Record({0xDEAD, Id(r.ecx)});
        return 0u;
    }));
}

void Build(Random& r)
{
    for (uint8_t& b : g_component)
        b = static_cast<uint8_t>(r.Next());
    for (uint8_t& b : g_device)
        b = static_cast<uint8_t>(r.Next());
    std::memset(g_object, 0, sizeof(g_object));
    auto* c = reinterpret_cast<Obj::CBaseComponent*>(g_component);
    c->m_vtable = reinterpret_cast<void**>(r.Bit() ? 0x00646864 : 0x00646830); // (slot 7: the base's Suspend)
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    At<uint32_t*>(g_object, 0) = g_vtable;
    At<uint32_t>(g_object, 0x104) = r.Below(4);
    float pos[4] = {r.Range(-5000.0f, 5000.0f), r.Range(-500.0f, 500.0f), r.Range(-5000.0f, 5000.0f),
                    r.OneIn(3) ? 1.0f : r.Range(-3.0f, 3.0f)};
    std::memcpy(g_object + 0x4C, pos, 16);
    float vel[4] = {r.Range(-1200.0f, 1200.0f), r.Range(-300.0f, 300.0f), r.Range(-1200.0f, 1200.0f), r.Range(-5.0f, 5.0f)};
    std::memcpy(g_object + 0xAC, vel, 16);
    g_inNet = r.Bit();
    g_suspended = r.Bit();
    g_clock = static_cast<int>(r.Below(100000)) - 10000;
    if (r.OneIn(8))
        g_clock = 0x7FFFFF00;
    g_clockStep = static_cast<int>(r.Below(3));
    g_frame = r.OneIn(4) ? 1.0 / 60.0 : r.Range(0.0f, 0.1f);
    for (bool& f : g_found)
        f = !r.OneIn(3);
    for (float& f : g_floats)
        f = r.Range(-20.0f, 120.0f);
    g_floats[1] = r.Range(0.0f, 4.0f);
    if (r.OneIn(5))
        g_floats[1] = static_cast<float>(r.Below(5)) * 0.001f; // whole milliseconds
    g_integer = static_cast<int>(r.Below(2));
    g_checksum = r.Bit() ? 0 : 0x1000 + r.Below(100);
    g_objectFound = r.Bit();
    g_vector = {r.Range(-100.0f, 100.0f), r.Range(-100.0f, 100.0f), r.Range(-100.0f, 100.0f), r.Range(-2.0f, 2.0f)};
    GameVariable<int>(0x00696D60) = static_cast<int>(r.Below(5000));
    GameVariable<bool>(0x006FA7D4) = r.OneIn(4);
    // A vibration component's fields (+0x18 on, the runs, the device).
    g_component[0x18] = r.Bit();
    for (int i = 0; i < 2; ++i) {
        uint8_t* timer = g_component + 0x1C + i * 0xC;
        timer[0] = r.Bit();
        At<int>(timer, 4) = g_clock + GameVariable<int>(0x00696D60) - static_cast<int>(r.Below(3000));
        At<uint32_t>(timer, 8) = r.Below(3000);
        if (r.OneIn(3)) // (exactly over: the clock's next value, offset and start)
            At<uint32_t>(timer, 8) = static_cast<uint32_t>(g_clock + g_clockStep + GameVariable<int>(0x00696D60) - At<int>(timer, 4));
    }
    At<uint8_t*>(g_component, 0x34) = r.OneIn(6) ? nullptr : g_device;
}

void Trace()
{
    for (const uint8_t* block : {g_component, g_object, g_device}) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(block);
        size_t words = (block == g_component ? sizeof(g_component) : block == g_object ? 0x110 : sizeof(g_device)) / 4;
        for (size_t i = 0; i < words; ++i)
            g_trace.push_back(Id(w[i]));
    }
}

std::vector<Saved> SavedGlobals()
{
    return {{0x006FA7D0, 4, {}}, {0x00696D60, 4, {}}, {0x006FA7D4, 1, {}}, {0x007D1800, 4, {}}};
}

template <typename Ours>
void Call(bool original, uint32_t address, void* self, Ours ours)
{
    if (original)
        Original<void(__thiscall*)(void*)>(address)(self);
    else
        ours();
}

void TestSmallComponents()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInAll(*s);
        GameVariable<void*>(0x006FA7D0) = g_net;
        GameVariable<void*>(0x007D1800) = g_block[1]; // (the manager Free is called on)
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{4357};
            for (int n = 0; n < 4000; ++n) {
                Build(r);
                auto* velocity = reinterpret_cast<Obj::CVelocityComponent*>(g_component);
                auto* vibration = reinterpret_cast<Obj::CVibrationComponent*>(g_component);
                auto* waitAndDie = reinterpret_cast<Obj::CWaitAndDieComponent*>(g_component);
                auto* params = reinterpret_cast<Script::CStruct*>(g_params);
                uint32_t which = r.Below(19);
                Record({0xCA5E, which});
                switch (which) {
                case 0:
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0043D2F0)(velocity, params);
                    else
                        velocity->InitFromStructure(params);
                    break;
                case 1:
                case 2:
                    Call(original, 0x0043D340, velocity, [&] { velocity->Update(); });
                    break;
                case 3:
                case 4:
                    Call(original, 0x0043D610, vibration, [&] { vibration->Update(); });
                    break;
                case 5:
                    Call(original, 0x0043D520, vibration, [&] { vibration->StopAll(); });
                    break;
                case 6:
                    if (At<void*>(g_component, 0x34))
                        Call(original, 0x0043D550, vibration, [&] { vibration->ResetDevice(); });
                    break;
                case 7: {
                    bool active = r.Bit();
                    if (original)
                        Original<void(__thiscall*)(void*, bool)>(0x0043D560)(vibration, active);
                    else
                        vibration->SetActive(active);
                    break;
                }
                case 8: {
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0043D5C0)(vibration, g_device);
                    else
                        vibration->SetDevice(reinterpret_cast<Sio::Device*>(g_device));
                    break;
                }
                case 9:
                case 10:
                case 11: {
                    if (!At<void*>(g_component, 0x34))
                        break;
                    bool result = original ? Original<bool(__thiscall*)(void*, void*, void*)>(0x0043D6F0)(vibration, params, nullptr)
                                           : vibration->Vibrate(params, nullptr);
                    Record({0x3E5, result});
                    break;
                }
                case 12:
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0043D880)(waitAndDie, params);
                    else
                        waitAndDie->InitFromStructure(params);
                    break;
                case 13:
                case 14:
                    At<uint32_t>(g_component, 0x24) = r.Bit() ? 0 : 0x77 + r.Below(5);
                    At<uint32_t>(g_component, 0x20) = r.OneIn(3) ? 0xFFFFFFF0 : r.Below(4000);
                    if (r.OneIn(3)) // (exactly the lifetime)
                        At<uint32_t>(g_component, 0x20) = static_cast<uint32_t>(
                            g_clock + g_clockStep + GameVariable<int>(0x00696D60) - At<int>(g_component, 0x18));
                    Call(original, 0x0043D8E0, waitAndDie, [&] { waitAndDie->Update(); });
                    break;
                case 15: {
                    // The components' Create (Calloc over dirty memory, or none) and destructors.
                    std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                    g_blockNext = r.OneIn(4) ? -1 : 0;
                    uint32_t kind = r.Below(2);
                    static const uint32_t creates[] = {0x0043D430, 0x0043D670};
                    void* made = original ? Original<void*(__cdecl*)()>(creates[kind])()
                                 : kind == 0 ? Obj::CVelocityComponent::Create()
                                             : Obj::CVibrationComponent::Create();
                    Record({Id(reinterpret_cast<uint32_t>(made))});
                    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_block[0]);
                    g_trace.insert(g_trace.end(), w, w + sizeof(g_block[0]) / 4);
                    break;
                }
                case 16: {
                    unsigned flags = r.Below(4);
                    uint32_t kind = r.Below(3);
                    static const uint32_t destroys[] = {0x0043D2C0, 0x0043D5E0, 0x0043D850};
                    void* result;
                    if (original)
                        result = Original<void*(__thiscall*)(void*, unsigned)>(destroys[kind])(g_component, flags);
                    else if (kind == 0)
                        result = velocity->Destroy(flags);
                    else if (kind == 1)
                        result = vibration->Destroy(flags);
                    else
                        result = waitAndDie->Destroy(flags);
                    Record({Id(reinterpret_cast<uint32_t>(result))});
                    break;
                }
                case 17: {
                    uint32_t kind = r.Below(3);
                    static const uint32_t bodies[] = {0x0043D2B0, 0x0043D4B0, 0x0043D840};
                    if (original)
                        Original<void(__thiscall*)(void*)>(bodies[kind])(g_component);
                    else if (kind == 0)
                        velocity->DestroyBody();
                    else if (kind == 1)
                        vibration->DestroyBody();
                    else
                        waitAndDie->DestroyBody();
                    break;
                }
                default:
                    if (original)
                        Original<void(__cdecl*)()>(0x0043D490)();
                    else
                        Obj::CVibrationComponent::RegisterMemberFunctions();
                    break;
                }
                Trace();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "velocity / vibration / wait-and-die components");
}

// The feeler's assignment: random feelers, the object pointers some null, some the same, a feeler onto itself.
void TestFeelerAssign()
{
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInAll(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0xA930};
            for (int n = 0; n < 500; ++n) {
                for (auto& feeler : g_feelers)
                    for (uint8_t& b : feeler)
                        b = static_cast<uint8_t>(r.Next());
                for (auto& feeler : g_feelers) {
                    uint32_t pick = r.Below(4);
                    At<uint8_t*>(feeler, 0x98) = pick == 3 ? nullptr : g_refs[pick];
                }
                uint8_t* to = g_feelers[0];
                uint8_t* from = r.OneIn(6) ? g_feelers[0] : g_feelers[1];
                void* result = original ? Original<void*(__thiscall*)(void*, void*)>(0x0043A930)(to, from)
                                        : reinterpret_cast<Nx::CFeeler*>(to)->Assign(*reinterpret_cast<Nx::CFeeler*>(from));
                Record({Id(reinterpret_cast<uint32_t>(result))});
                for (auto& feeler : g_feelers) {
                    const uint32_t* w = reinterpret_cast<const uint32_t*>(feeler);
                    for (int i = 0; i < 0xB0 / 4; ++i)
                        g_trace.push_back(Id(w[i]));
                }
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    Compare(traces[0], traces[1], "feeler assignment (0x0043A930)");
}

} // namespace

SELFTEST(Early, TestSmallComponents, RemoveStandIns);
SELFTEST(Early, TestFeelerAssign, RemoveStandIns);
