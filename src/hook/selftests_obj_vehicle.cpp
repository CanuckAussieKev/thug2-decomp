// Self-tests for the Vehicle component (game/obj/vehicle_component.cpp, vehicle_physics.cpp, vehicle_collision.cpp):
// the originals against ours over random vehicles, every call into other code recorded by stand-ins
// (hook/standins.h); pure helpers (matrix and vector maths) run for real.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/mth/slerp.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_rect.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/obj/smtptr.h"
#include "game/obj/vehicle_component.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CVehicleComponent;
using Wheel = Obj::CVehicleComponent::Wheel;

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
void RecordDouble(double d)
{
    uint32_t u[2];
    std::memcpy(u, &d, 8);
    Record({u[0], u[1]});
}

constexpr int kWheels = 4;
alignas(16) uint8_t g_vehicle[sizeof(CVehicleComponent)];
alignas(16) uint8_t g_wheels[kWheels][sizeof(Wheel)];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_params[0x10];
alignas(16) uint8_t g_refs[2][0x10];

float g_scriptFloats[16];
float g_randoms[8];
int g_randomNext;
Mth::Vector g_vectors[4];
bool g_vectorFound;

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, size_t each, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        if (p >= b && p < b + size)
            return tag + static_cast<uint32_t>((p - b) / each) * 0x1000 + static_cast<uint32_t>((p - b) % each);
        return 0;
    };
    if (!p)
        return 0;
    for (auto r : {in(g_vehicle, sizeof(g_vehicle), sizeof(g_vehicle), 0x51000000),
                   in(g_wheels, sizeof(g_wheels), sizeof(g_wheels[0]), 0x52000000),
                   in(g_object, sizeof(g_object), sizeof(g_object), 0x53000000),
                   in(g_params, sizeof(g_params), sizeof(g_params), 0x54000000),
                   in(g_refs, sizeof(g_refs), sizeof(g_refs[0]), 0x55000000)})
        if (r)
            return r;
    return p;
}

// A pointer that may be to the stack (a local feeler): the same in both runs.
uint32_t Stable(uint32_t p)
{
    uint32_t id = Id(p);
    return id == p && p >= 0x10000 ? 0xF00Du : id;
}

float __cdecl StandInScriptFloat(uint32_t name)
{
    Record({0xF100, name});
    return g_scriptFloats[(name * 2654435761u) >> 28];
}
float __cdecl StandInPlusOrMinus(float range)
{
    Record({0x3E80, Bits(range)});
    return g_randoms[g_randomNext++ & 7] * range;
}

void StandInAll(StandIns& s)
{
    using Script::CStruct;
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInScriptFloat)));
    for (uint32_t address : {0x00403E80u, AddressOf(Mth::PlusOrMinus)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInPlusOrMinus)));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x6F10, Id(r.ecx), r.args[0], Stable(r.args[1]), r.args[2]});
             *reinterpret_cast<float*>(r.args[1]) = g_scriptFloats[(r.args[0] * 2654435761u) >> 28];
             return 1u;
         });
    s.At({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [](Regs& r) {
             Record({0x6FB0, Id(r.ecx), r.args[0], r.args[2]});
             bool found = g_vectorFound || r.args[2]; // (asserted: always there)
             if (found)
                 *reinterpret_cast<Mth::Vector*>(r.args[1]) = g_vectors[r.args[0] & 3];
             return static_cast<uint32_t>(found);
         });
    s.At({0x00477010,
          AddressOf(static_cast<bool (CStruct::*)(const char*, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [](Regs& r) {
             Record({0x7010, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<Mth::Vector*>(r.args[1]) = g_vectors[(r.args[0] >> 3) & 3];
             return 1u;
         });
    s.At({0x00401C30, AddressOf(Dbg::Printf)}, 0, [](Regs& r) {
        Record({0xDB6, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Id(r.ecx), Stable(r.args[0])});
        return 0u;
    });
}

// A random vehicle: every word a float of a plausible size, then the counts, flags and pointers.
void BuildVehicle(Random& r)
{
    for (size_t i = 0; i < sizeof(g_vehicle); i += 4)
        At<float>(g_vehicle, static_cast<uint32_t>(i)) = r.Range(-60.0f, 60.0f);
    for (auto& raw : g_wheels)
        for (size_t i = 0; i < sizeof(raw); i += 4)
            At<float>(raw, static_cast<uint32_t>(i)) = r.Range(-60.0f, 60.0f);
    auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
    v->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    v->m_awake = r.Bit();
    v->m_stillFrames = static_cast<int>(r.Below(4));
    v->m_contactCount = static_cast<int>(r.Below(5));
    v->m_wheelCount = r.OneIn(6) ? static_cast<int>(r.Below(kWheels)) : kWheels;
    v->m_wheels = reinterpret_cast<Wheel*>(g_wheels);
    v->m_wheelsInContact = static_cast<int>(r.Below(kWheels + 1));
    if (r.OneIn(3))
        v->m_wheelsInContact = v->m_wheelCount;
    v->m_driveWheelCount = 1 + static_cast<int>(r.Below(kWheels));
    v->m_gearCount = 1 + static_cast<int>(r.Below(6));
    v->m_loadIndex = static_cast<int8_t>(r.Below(10));
    v->m_interrupted = r.OneIn(4);
    v->m_righting = r.OneIn(4);
    v->m_throttle = r.Bit();
    v->m_brake = r.OneIn(3);
    v->m_handbrake = r.OneIn(3);
    v->m_reverse = r.OneIn(3);
    v->m_skaterVisible = r.Bit();
    v->m_handbrakeActive = r.OneIn(4);
    v->m_exitable = r.Bit();
    v->m_noHandbrake = r.OneIn(4);
    v->m_passive = r.OneIn(5);
    v->m_debugMode = static_cast<int>(r.Below(3));
    v->m_invMass = r.Range(0.05f, 3.0f);
    for (float& m : v->m_invMoments)
        m = r.Range(0.001f, 0.5f);
    v->m_timestep = r.OneIn(3) ? 1.0f / 60.0f : r.Range(0.001f, 0.034f);
    if (r.OneIn(12)) // (a frame of nothing)
        v->m_timestep = 0.0f;
    v->m_kickTime = r.OneIn(3) ? 0.0f : r.Range(-1.0f, 1.0f);
    v->m_upshiftSpeed = r.Range(0.0f, 600.0f);
    v->m_differentialRatio = r.Range(0.5f, 5.0f);
    for (float& ratio : v->m_gearRatios)
        ratio = r.Range(0.3f, 4.0f);
    v->m_steering = r.OneIn(4) ? 0.0f : r.Range(-1.0f, 1.0f);
    v->m_gravityScale = r.Range(0.0f, 2.0f);
    for (auto& raw : g_wheels) {
        auto& wheel = *reinterpret_cast<Wheel*>(raw);
        wheel.state = static_cast<int>(r.Below(8));
        wheel.steeringSide = static_cast<int>(r.Below(3));
        wheel.drive = r.Bit();
        wheel.feeler.m_object.mp_object = r.OneIn(3) ? reinterpret_cast<Obj::CCompositeObject*>(g_refs[r.Below(2)]) : nullptr;
        // Grip speeds in order, and ties at them now and then.
        float a = r.Range(0.0f, 200.0f), b = r.Range(0.0f, 200.0f), c = r.Range(0.0f, 200.0f);
        if (a > b)
            std::swap(a, b);
        if (b > c)
            std::swap(b, c);
        if (a > b)
            std::swap(a, b);
        wheel.minStaticGripVel = a;
        wheel.maxStaticGripVel = b;
        wheel.minDynamicGripVel = c;
        if (r.OneIn(10))
            wheel.minStaticGripVel = wheel.maxStaticGripVel; // (empty ranges)
        if (r.OneIn(10))
            wheel.maxStaticGripVel = wheel.minDynamicGripVel;
    }
    for (float& f : g_scriptFloats)
        f = r.Range(-50.0f, 50.0f);
    for (float& f : g_randoms)
        f = r.Range(-1.0f, 1.0f);
    g_randomNext = 0;
    for (auto& vec : g_vectors)
        vec = {r.Range(-80.0f, 80.0f), r.Range(-80.0f, 80.0f), r.Range(-80.0f, 80.0f), r.Range(-2.0f, 2.0f)};
    if (r.OneIn(6))
        g_vectors[r.Below(4)].x = g_vectors[0].z = 0.0f;
    g_vectorFound = !r.OneIn(8);
}

// NaNs as one value: their sign and payload depend on the order of the x87's operands against SSE's (degenerate
// cases - a step of no time - fill the state with them).
void NormalizeNaNs(std::vector<uint32_t>& trace)
{
    for (uint32_t& word : trace)
        if ((word & 0x7F800000u) == 0x7F800000u && (word & 0x007FFFFFu) != 0)
            word = 0x7FC00000u;
}

// On a mismatch, the case it's in (the last 0xCA5E marker before it) and the word's offset after the marker.
void ReportCase(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what)
{
    size_t i = 0;
    while (i < expected.size() && i < actual.size() && expected[i] == actual[i])
        ++i;
    if (i == expected.size() && i == actual.size())
        return;
    size_t marker = i;
    while (marker > 0 && expected[marker] != 0xCA5E)
        --marker;
    unsigned number = 0;
    for (size_t k = 0; k < marker; ++k)
        number += expected[k] == 0xCA5E;
    Log("  %s: first difference %u words into case %u (trace word %u, case number %u)", what,
        static_cast<unsigned>(i - marker), marker + 1 < expected.size() ? expected[marker + 1] : 0xFFFFFFFFu,
        static_cast<unsigned>(i), number);
    size_t next = i;
    while (next < expected.size() && expected[next] != 0xCA5E)
        ++next;
    Log("    (the case is %u words long in the original)", static_cast<unsigned>(next - marker));
    // (and the case's other differences, up to eight)
    int shown = 0;
    for (size_t k = i; k < expected.size() && k < actual.size() && expected[k] != 0xCA5E && shown < 8; ++k)
        if (expected[k] != actual[k]) {
            Log("    word %u: %08X / %08X", static_cast<unsigned>(k - marker), expected[k], actual[k]);
            ++shown;
        }
    // (and the cases of the next differences while the traces stay in step, up to eight)
    shown = 0;
    size_t lastMarker = marker;
    for (size_t k = i + 1; k < expected.size() && k < actual.size() && shown < 8; ++k) {
        if (expected[k] == 0xCA5E)
            lastMarker = k;
        if (expected[k] != actual[k] && lastMarker != marker) {
            Log("    then case %u, word %u: %08X / %08X", expected[lastMarker + 1], static_cast<unsigned>(k - lastMarker),
                expected[k], actual[k]);
            marker = lastMarker;
            ++shown;
        }
    }
}

void TraceVehicle()
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_vehicle);
    for (size_t i = 0x18 / 4; i < 0x30C / 4; ++i)
        g_trace.push_back(Id(w[i]));
    for (size_t i = 0x12D8 / 4; i < sizeof(g_vehicle) / 4; ++i)
        g_trace.push_back(Id(w[i]));
    for (auto& raw : g_wheels) {
        const uint32_t* ww = reinterpret_cast<const uint32_t*>(raw);
        for (size_t i = 0; i < sizeof(raw) / 4; ++i)
            g_trace.push_back(Id(ww[i]));
    }
}

// --- the small methods and member functions ---

void TestVehicleMethods()
{
    std::vector<uint32_t> traces[2];
    int friction[8] = {};
    {
        auto* s = new StandIns;
        StandInAll(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x435250};
            for (int n = 0; n < 6000; ++n) {
                BuildVehicle(r);
                auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
                auto* params = reinterpret_cast<Script::CStruct*>(g_params);
                Mth::Vector a{r.Range(-90.0f, 90.0f), r.Range(-90.0f, 90.0f), r.Range(-90.0f, 90.0f), r.Range(-2.0f, 2.0f)};
                Mth::Vector b{r.Range(-90.0f, 90.0f), r.Range(-90.0f, 90.0f), r.Range(-90.0f, 90.0f), r.Range(-2.0f, 2.0f)};
                uint32_t which = r.Below(20);
                Record({0xCA5E, which});
                switch (which) {
                case 0: {
                    // Wheels' roll angles, some near the wrap points.
                    for (auto& raw : g_wheels) {
                        auto& wheel = *reinterpret_cast<Wheel*>(raw);
                        uint32_t pick = r.Below(4);
                        wheel.rotation = pick == 0 ? r.Range(620.0f, 640.0f) : pick == 1 ? r.Range(-5.0f, 1.0f)
                                                                                         : r.Range(-700.0f, 700.0f);
                        wheel.spin = r.Range(-300.0f, 300.0f);
                        if (r.OneIn(5)) { // (exactly at the wrap: 200 pi, 0x006466AC)
                            wheel.spin = wheel.torque = 0.0f;
                            wheel.rotation = std::bit_cast<float>(0x441D1463u);
                        }
                    }
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x004351C0)(v);
                    else
                        v->UpdateWheelRotations();
                    break;
                }
                case 1:
                case 2: {
                    Wheel* wheel = &v->m_wheels[r.Below(kWheels)];
                    float slip = r.Range(0.0f, 220.0f);
                    uint32_t edge = r.Below(5);
                    if (edge == 1)
                        slip = wheel->minStaticGripVel;
                    else if (edge == 2)
                        slip = wheel->maxStaticGripVel;
                    else if (edge == 3)
                        slip = wheel->minDynamicGripVel;
                    double result = original ? Original<double(__thiscall*)(void*, void*, float)>(0x00435250)(v, wheel, slip)
                                             : v->GetTyreFriction(wheel, slip);
                    RecordDouble(result);
                    if (original)
                        ++friction[wheel->state & 7];
                    break;
                }
                case 3: {
                    float spin = r.Range(-50.0f, 300.0f);
                    if (r.OneIn(4))
                        spin = v->m_upshiftSpeed / (v->m_gearRatios[r.Below(2)] * v->m_differentialRatio);
                    int gear = original ? Original<int(__thiscall*)(void*, float)>(0x00435380)(v, spin) : v->GetGear(spin);
                    Record({static_cast<uint32_t>(gear)});
                    break;
                }
                case 4:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x00435520)(v);
                    else
                        v->UpdateVelocities();
                    break;
                case 5: {
                    Mth::Vector out{};
                    void* result = original ? Original<void*(__thiscall*)(void*, void*, void*)>(0x004355B0)(v, &out, &a)
                                            : v->GetPointVelocity(&out, &a);
                    Record({result == &out, Bits(out.x), Bits(out.y), Bits(out.z), Bits(out.w)});
                    break;
                }
                case 6:
                    if (original)
                        Original<void(__thiscall*)(void*, void*, void*, uint32_t)>(0x00435640)(v, &a, &b, 0xFF00FF00);
                    else
                        v->AddForceAtPoint(&a, &b, 0xFF00FF00);
                    break;
                case 7:
                    if (original)
                        Original<void(__thiscall*)(void*, void*, void*)>(0x00435710)(v, &a, &b);
                    else
                        v->AddForceAtOffset(&a, &b);
                    break;
                case 8:
                    if (original)
                        Original<void(__thiscall*)(void*, void*, void*)>(0x004357C0)(v, &a, &b);
                    else
                        v->ApplyImpulseAtOffset(&a, &b);
                    break;
                case 9: {
                    bool kicked = original ? Original<bool(__thiscall*)(void*)>(0x00435850)(v) : v->IsKicked();
                    Record({kicked});
                    break;
                }
                case 10:
                    if (r.OneIn(4))
                        a.x = a.z = 0.0f;
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x00435890)(v, &a);
                    else
                        v->KickRandomlyAlong(&a);
                    break;
                case 11: {
                    Mth::Vector out{};
                    void* result = original ? Original<void*(__cdecl*)(void*, void*, void*)>(0x004354C0)(&out, &a, &b)
                                            : Obj::QuatProduct(&out, &a, &b);
                    Record({result == &out, Bits(out.x), Bits(out.y), Bits(out.z), Bits(out.w)});
                    break;
                }
                case 12: {
                    // The wheel's constructor and destructor.
                    auto* wheel = &v->m_wheels[r.Below(kWheels)];
                    void* result = wheel;
                    if (original) {
                        if (r.Bit())
                            result = Original<void*(__thiscall*)(void*)>(0x00435870)(wheel);
                        else
                            Original<void(__thiscall*)(void*)>(0x00437CF0)(wheel);
                    } else if (r.Bit()) {
                        result = wheel->Construct();
                    } else {
                        wheel->Destruct();
                    }
                    Record({Id(reinterpret_cast<uint32_t>(result))});
                    break;
                }
                default: {
                    // The member functions.
                    static const uint32_t members[] = {0x00437A60, 0x004353F0, 0x00435410, 0x00435480,
                                                       0x00435490, 0x00437BC0, 0x004354B0};
                    uint32_t member = which - 13;
                    bool result;
                    if (original) {
                        result = Original<bool(__thiscall*)(void*, void*, void*)>(members[member])(v, params, nullptr);
                    } else {
                        switch (member) {
                        case 0: result = v->Kick(params, nullptr); break;
                        case 1: result = v->Wake(params, nullptr); break;
                        case 2: result = v->AdjustGravity(params, nullptr); break;
                        case 3: result = v->HandbrakeActive(params, nullptr); break;
                        case 4: result = v->AllWheelsAreInContact(params, nullptr); break;
                        case 5: result = v->KickRandomly(params, nullptr); break;
                        default: result = v->IsSkaterVisible(params, nullptr); break;
                        }
                    }
                    Record({result});
                    break;
                }
                }
                TraceVehicle();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    Log("  vehicle methods: tyre states %d %d %d %d %d %d %d %d", friction[0], friction[1], friction[2], friction[3],
        friction[4], friction[5], friction[6], friction[7]);
    ReportCase(traces[0], traces[1], "vehicle methods");
    Compare(traces[0], traces[1], "vehicle: wheels' roll, tyre friction, gears, forces, impulses, kicks, member functions");
}

// --- a physics step's parts ---

alignas(16) uint8_t g_input[0x240];
alignas(16) uint8_t g_skater[0x200];
alignas(16) uint8_t g_core[0x1600];
alignas(16) uint8_t g_coreFlags[0x40];
alignas(16) uint8_t g_blender[0x40];
alignas(16) uint8_t g_trigger[0x40];
alignas(16) uint8_t g_level[0x80];

int g_clock, g_clockStep;
bool g_hits[16];
int g_hitNext;
float g_hitDistance[16];
uint32_t g_hitObject[16];
int g_interruptAt; // the trigger call that sets m_interrupted (-1: none)
int g_triggerCalls;
bool g_inNet;
float g_animDuration;

uint32_t PhysicsId(uint32_t p)
{
    auto in = [&](const void* base, size_t size, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        return p >= b && p < b + size ? tag + (p - b) : 0;
    };
    for (auto r : {in(g_input, sizeof(g_input), 0x61000000), in(g_skater, sizeof(g_skater), 0x62000000),
                   in(g_core, sizeof(g_core), 0x63000000), in(g_blender, sizeof(g_blender), 0x64000000),
                   in(g_trigger, sizeof(g_trigger), 0x65000000)})
        if (r)
            return r;
    return Id(p);
}

float __fastcall StandInAnimValue(void* blender, void*, uint32_t anim)
{
    Record({0x9AF0, PhysicsId(reinterpret_cast<uint32_t>(blender)), anim});
    return g_animDuration;
}

void StandInPhysics(StandIns& s)
{
    s.At({0x0044ECE0, AddressOf(Tmr::GameClockElapsed)}, 0, [](Regs&) {
        g_clock += g_clockStep;
        Record({0xC10C, static_cast<uint32_t>(g_clock)});
        return static_cast<uint32_t>(g_clock);
    });
    s.At({0x0040ABA0, AddressOf(&Nx::CCollCache::Update)}, 4, [](Regs& r) {
        const uint32_t* box = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xABA0, Id(r.ecx), box[0], box[1], box[2], box[4], box[5], box[6]});
        return 0u;
    });
    // The suspension lines' hits (scripted), each filling the feeler as GetCollision does.
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        const uint32_t* line = reinterpret_cast<const uint32_t*>(&feeler->m_line);
        Record({0x72F0, Stable(r.ecx), r.args[0] & 0xFF, r.args[1] & 0xFF, feeler->m_hit.ignoreFlags, feeler->m_hit.requireFlags,
                line[0], line[1], line[2], line[3], line[4], line[5], line[6], line[7]});
        int k = g_hitNext++ & 15;
        // (the local feeler's fields the constructor leaves alone - stack garbage in the original - made the same)
        uint32_t* words = reinterpret_cast<uint32_t*>(feeler);
        uint32_t flags = words[0x4C / 4]; // (the ignore / require flags kept)
        for (int i = 0x24 / 4; i < 0x98 / 4; ++i)
            words[i] = 0x1000 + static_cast<uint32_t>(i);
        words[0x4C / 4] = flags;
        words[0x20 / 4] = 0;
        words[0xA4 / 4] = 0x2A4;
        words[0xA8 / 4] = 0x2A8;
        if (!g_hits[k])
            return 0u;
        feeler->m_distance = g_hitDistance[k];
        feeler->m_point = {g_hitDistance[k] * 3.0f, -g_hitDistance[k], 7.0f, 1.0f};
        feeler->m_normal = {0.1f * k, 0.9f, -0.2f, k % 3 == 0 ? 0.37f : 1.0f};
        feeler->m_hit.object = reinterpret_cast<Nx::CCollObj*>(g_hitObject[k]);
        return 1u;
    });
    s.At({0x00434BF0}, 8, [](Regs& r) {
        Record({0x4BF0, PhysicsId(r.ecx), r.args[0], Stable(r.args[1])});
        if (g_triggerCalls++ == g_interruptAt) {
            if (g_interruptAt & 1)
                reinterpret_cast<CVehicleComponent*>(g_vehicle)->m_interrupted = true;
            else
                At<uint32_t>(g_object, 0x8) |= 1;
        }
        return 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, Id(r.ecx), Stable(r.args[0])});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0x4E4F, r.ecx});
        return static_cast<uint32_t>(g_inNet);
    });
    for (uint32_t address : {0x00419AF0u, AddressOf(&Obj::CAnimBlenderComponent::GetValue)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInAnimValue)));
    s.At({0x00419B50, AddressOf(&Obj::CAnimBlenderComponent::PlayAnimationEx)}, 0x1C, [](Regs& r) {
        Record({0x9B50, PhysicsId(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        return 0u;
    });
    s.At({0x00460A20, AddressOf(&Obj::CObject::BroadcastEvent)}, 12, [](Regs& r) {
        Record({0x0A20, Id(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
}

void BuildPhysics(Random& r)
{
    BuildVehicle(r);
    auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
    // A proper orientation (a unit quaternion), rows from it, plausible momenta.
    float q[4] = {r.Range(-1.0f, 1.0f), r.Range(-1.0f, 1.0f), r.Range(-1.0f, 1.0f), r.Range(-1.0f, 1.0f)};
    float qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    v->m_orientation = {q[0] / qn, q[1] / qn, q[2] / qn, q[3] / qn};
    if (r.OneIn(4))
        v->m_orientation = {0.0f, r.Range(-0.3f, 0.3f), 0.0f, 1.0f};
    GameFunction<void(__thiscall*)(const Mth::Vector*, Mth::Matrix*)>(0x0042BC40)(&v->m_orientation, &v->m_matrix);
    v->m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (r.OneIn(5))
        v->m_matrix.row[1].y = r.Bit() ? 0.5f : 0.4f;
    if (r.OneIn(4)) // (a fourth row left over)
        v->m_matrix.row[3] = {r.Range(-5.0f, 5.0f), r.Range(-5.0f, 5.0f), r.Range(-5.0f, 5.0f), r.Range(-5.0f, 5.0f)};
    v->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    At<uint32_t>(g_object, 0x8) = 0;
    v->mp_input = g_input;
    v->mp_skater = reinterpret_cast<Obj::CCompositeObject*>(g_skater);
    v->mp_skaterCorePhysics = reinterpret_cast<Obj::CBaseComponent*>(g_core);
    v->mp_animBlender = reinterpret_cast<Obj::CBaseComponent*>(g_blender);
    v->mp_trigger = reinterpret_cast<Obj::CBaseComponent*>(g_trigger);
    v->m_rightingStart = static_cast<int>(r.Below(2000));
    v->m_skaterAnim = 0x5C00 + r.Below(3);
    for (int i = 0; i < 2; ++i)
        for (float& len : v->m_colliders[i].lengths)
            len = r.Range(1.0f, 80.0f);
    for (auto& raw : g_wheels) {
        auto& wheel = *reinterpret_cast<Wheel*>(raw);
        wheel.radius = r.Range(8.0f, 20.0f);
        wheel.invMoment = r.Range(0.01f, 2.0f);
        wheel.spin = r.OneIn(6) ? 0.0f : r.Range(-60.0f, 60.0f);
        wheel.friction = r.Range(0.0f, 2.0f);
        wheel.load = r.Range(0.0f, 500.0f);
        wheel.brakeExcess = r.OneIn(3) ? 0.0f : r.Range(-50.0f, 50.0f);
        wheel.normal = {r.Range(-0.3f, 0.3f), r.Range(0.8f, 1.0f), r.Range(-0.3f, 0.3f), r.Bit() ? 1.0f : 0.0f};
        wheel.feeler.m_hit.object = reinterpret_cast<Nx::CCollObj*>(0x7000 + r.Below(3));
        if (r.OneIn(6)) { // (a slip of nothing, and of almost nothing)
            wheel.contactVel = {0.0f, 0.0f, 0.0f, wheel.contactVel.w};
            wheel.spin = 0.0f;
        } else if (r.OneIn(6)) {
            wheel.contactVel = {r.Range(-5e-4f, 5e-4f), r.Range(-5e-4f, 5e-4f), r.Range(-5e-4f, 5e-4f), wheel.contactVel.w};
            wheel.spin = 0.0f;
        }
        if (r.OneIn(6))
            wheel.torque = -0.0f;
        if (r.OneIn(6)) // (nearly stopped: what stops it is small, but not nothing)
            wheel.spin = r.Range(-0.01f, 0.01f);
    }
    // (the steering exactly at its dead zone, a wheelbase of nothing, no spin)
    const float deadZone = std::bit_cast<float>(0x3A83126Fu);
    if (r.OneIn(8))
        v->m_steering = r.Bit() ? deadZone : -deadZone;
    if (r.OneIn(8))
        v->m_displaySteering = r.Bit() ? deadZone : -deadZone;
    if (r.OneIn(8))
        v->m_wheelbase = 0.0f;
    if (r.OneIn(6)) { // (upright on all wheels, not steering, turning: the turning damped)
        v->m_steering = 0.0f;
        v->m_wheelsInContact = v->m_wheelCount;
        v->m_matrix.row[1].y = 0.9f;
        v->m_rotmom.y = r.Range(-3.0f, 3.0f);
    }
    if (r.OneIn(6)) { // (in the air but resting on two or three contacts)
        v->m_wheelsInContact = 0;
        v->m_contactCount = 2 + static_cast<int>(r.Below(2));
    }
    if (r.OneIn(8))
        v->m_rotmom = {0.0f, 0.0f, 0.0f, r.Range(-2.0f, 2.0f)};
    if (r.OneIn(3)) { // (nearly still: going to sleep; exactly at the limit; not moving at all)
        v->m_vel = {r.Range(-0.6f, 0.6f), r.Range(-0.6f, 0.6f), r.Range(-0.6f, 0.6f), r.Range(-1.0f, 1.0f)};
        v->m_rotvel = {r.Range(-0.006f, 0.006f), r.Range(-0.006f, 0.006f), r.Range(-0.006f, 0.006f), 1.0f};
        if (r.OneIn(4))
            v->m_vel = {0.0f, 1.0f, 0.0f, 0.0f};
        if (r.OneIn(6))
            v->m_vel = {0.0f, 0.0f, 0.0f, 0.0f};
        if (r.OneIn(6))
            v->m_rotvel = {0.01f, 0.0f, 0.0f, 0.0f};
    }
    // (now and then a NaN somewhere: the compares must take the original's way with one)
    if (r.OneIn(8)) {
        float nan = std::bit_cast<float>(r.Bit() ? 0x7FC00000u : 0xFFC00000u);
        float* fields[] = {&v->m_rotmom.x, &v->m_rotmom.y, &v->m_mom.z, &v->m_vel.x, &v->m_vel.z, &v->m_rotvel.y,
                           &v->m_steering, &v->m_collisionImpulse, &v->m_matrix.row[1].y, &v->m_kickTime,
                           &v->m_pos.y, &v->m_freeFallTime};
        *fields[r.Below(sizeof(fields) / sizeof(fields[0]))] = nan;
        auto& wheel = *reinterpret_cast<Wheel*>(g_wheels[r.Below(kWheels)]);
        float* wheelFields[] = {&wheel.spin, &wheel.y, &wheel.load, &wheel.brakeExcess, &wheel.contactVel.x, &wheel.friction};
        if (r.Bit())
            *wheelFields[r.Below(sizeof(wheelFields) / sizeof(wheelFields[0]))] = nan;
    }
    for (uint8_t& b : g_input)
        b = r.OneIn(3) ? 1 : 0;
    At<float>(g_input, 0x238) = r.OneIn(3) ? 0.0f : r.Range(-1.0f, 1.0f);
    std::memset(g_skater, 0, sizeof(g_skater));
    for (size_t i = 0x4C; i < 0xBC; i += 4)
        At<float>(g_object, static_cast<uint32_t>(i)) = r.Range(-100.0f, 100.0f);
    std::memset(g_core, 0, sizeof(g_core));
    At<uint8_t*>(g_core, 0xC) = g_skater;
    At<uint8_t*>(g_core, 0x1528) = g_coreFlags;
    g_coreFlags[0x28] = r.Bit();
    At<uint32_t>(g_level, 0x40) = r.OneIn(3) ? 0xD443A2BC : 0x1234;
    g_clock = static_cast<int>(r.Below(5000));
    g_clockStep = static_cast<int>(r.Below(200));
    for (int k = 0; k < 16; ++k) {
        g_hits[k] = !r.OneIn(3);
        g_hitDistance[k] = r.OneIn(24) ? std::bit_cast<float>(0x7FC00000u) : r.Range(0.0f, 1.0f);
        g_hitObject[k] = 0x7000 + r.Below(3);
    }
    g_hitNext = 0;
    g_interruptAt = r.OneIn(4) ? static_cast<int>(r.Below(6)) : -1;
    g_triggerCalls = 0;
    g_inNet = r.OneIn(3);
    g_animDuration = r.Range(0.0f, 3.0f);
}

void TracePhysics()
{
    TraceVehicle();
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_skater);
    g_trace.insert(g_trace.end(), w + 0x4C / 4, w + 0xBC / 4);
    w = reinterpret_cast<const uint32_t*>(g_core);
    g_trace.insert(g_trace.end(), w + 0x18 / 4, w + 0x58 / 4);
    g_trace.push_back(At<uint32_t>(g_object, 0x8));
}

std::vector<Saved> PhysicsGlobals()
{
    return {{0x00696D60, 4, {}}, {0x007CE5D8, 4, {}}, {0x007CCE14, 4, {}}, {0x006951C4, 4, {}}};
}

void TestVehiclePhysics()
{
    std::vector<Saved> saved = PhysicsGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInAll(*s);
        StandInPhysics(*s);
        GameVariable<void*>(0x007CE5D8) = g_level;
        GameVariable<void*>(0x007CCE14) = g_skater; // (any manager: InNetGame is stood in for)
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x438AE0};
            GameVariable<int>(0x006951C4) = 12;
            for (int n = 0; n < 5000; ++n) {
                BuildPhysics(r);
                GameVariable<int>(0x00696D60) = static_cast<int>(r.Below(3)) * 100;
                auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
                if (r.OneIn(6)) // (righting for 299, 300 or 301 ms at the first clock reading)
                    v->m_rightingStart = g_clock + g_clockStep + GameVariable<int>(0x00696D60) - 300 +
                                         static_cast<int>(r.Below(3)) - 1;
                uint32_t which = r.Below(19);
                Record({0xCA5E, which});
                static const uint32_t functions[] = {0x00435B60, 0x00435DA0, 0x00435F00, 0x004360C0, 0x00436280,
                                                     0x00436340, 0x004364B0, 0x004367E0, 0x00436900, 0x00436A90,
                                                     0x00436B60, 0x00436C20, 0x00438A20, 0x00438AE0, 0x0043AE10,
                                                     0x0043B240, 0x00437550, 0x004376B0, 0x004378B0};
                float scale = r.Range(0.0f, 1.0f);
                if (original) {
                    if (which == 10)
                        Original<void(__thiscall*)(void*, float)>(functions[which])(v, scale);
                    else
                        Original<void(__thiscall*)(void*)>(functions[which])(v);
                } else {
                    switch (which) {
                    case 0: v->UpdateWheelFriction(); break;
                    case 1: v->UpdateCollisionCache(); break;
                    case 2: v->UpdateSteering(); break;
                    case 3: v->ApplyDamping(); break;
                    case 4: v->ApplyGravity(); break;
                    case 5: v->ApplySuspension(); break;
                    case 6: v->ApplyTyreForces(); break;
                    case 7: v->ApplyEngineBraking(); break;
                    case 8: v->ApplyDriveTorque(); break;
                    case 9: v->ApplyBrakes(); break;
                    case 10: v->ApplyHandbrake(scale); break;
                    case 11: v->ApplyBrakeExcess(); break;
                    case 12: v->UpdateInertia(); break;
                    case 13: v->ApplyWheelForces(); break;
                    case 14: v->UpdateDependents(); break;
                    case 15: v->UpdateWheelContacts(); break;
                    case 16: v->CheckSleep(); break;
                    case 17: v->ReadControls(); break;
                    default: v->UpdateSkater(); break;
                    }
                }
                TracePhysics();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    NormalizeNaNs(traces[0]);
    NormalizeNaNs(traces[1]);
    ReportCase(traces[0], traces[1], "vehicle physics");
    Compare(traces[0], traces[1], "vehicle: steering, suspension, tyres, drive, brakes, damping, contacts, controls, skater");
}

// --- collision and integration ---

alignas(16) uint8_t g_rect[sizeof(Nx::CRectFeeler)];
alignas(16) uint8_t g_model[0x40];
constexpr uint32_t kContactBytes = 0x920; // (0x006951C8 up to the next variable)

double g_frame;
bool g_queryHits[4];
int g_queryNext;
int g_joinNext;
// The joined hits the stand-in JoinHits makes: their ends on a small grid, so that some meet.
struct JoinedSet {
    int count;
    Nx::RectHit hits[4];
};
JoinedSet g_joinedSets[4];

double __cdecl StandInCollisionFrame()
{
    Record({0xF7A3});
    return g_frame;
}

uint32_t HashWords(const void* p, int count)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < count; ++i) {
        uint32_t word = static_cast<const uint32_t*>(p)[i];
        if ((word & 0x7F800000u) == 0x7F800000u && (word & 0x007FFFFFu) != 0)
            word = 0x7FC00000u; // (NaNs as one, as NormalizeNaNs)
        hash = (hash ^ word) * 16777619u;
    }
    return hash;
}

Mth::Vector GridPoint(Random& r)
{
    return {static_cast<float>(static_cast<int>(r.Below(5)) - 2) * 10.0f, static_cast<float>(r.Below(3)) * 5.0f,
            static_cast<float>(static_cast<int>(r.Below(5)) - 2) * 10.0f, 1.0f};
}

Mth::Vector UnitVector(Random& r)
{
    float x = r.Range(-1.0f, 1.0f), y = r.Range(-1.0f, 1.0f), z = r.Range(-1.0f, 1.0f);
    float n = std::sqrt(x * x + y * y + z * z);
    if (n < 0.01f)
        return {0.0f, 1.0f, 0.0f, r.Bit() ? 1.0f : 0.0f};
    float w = r.OneIn(4) ? r.Range(-1.0f, 1.0f) : (r.Bit() ? 1.0f : 0.0f);
    return {x / n, y / n, z / n, w};
}

void BuildJoined(Random& r, JoinedSet& set)
{
    set.count = static_cast<int>(r.Below(5));
    for (int k = 0; k < 4; ++k) {
        Nx::RectHit& hit = set.hits[k];
        std::memset(&hit, 0, sizeof(hit));
        for (auto& end : hit.ends) {
            end.point = GridPoint(r);
            if (r.OneIn(3))
                end.point.x += r.Range(-0.15f, 0.15f);
            end.edge = UnitVector(r);
            end.onTriangleEdge = !r.OneIn(3);
        }
        hit.normal = UnitVector(r);
        // (now and then exactly or nearly opposite an earlier hit, meeting it)
        if (k > 0 && r.OneIn(2)) {
            const Nx::RectHit& other = set.hits[r.Below(static_cast<uint32_t>(k))];
            hit.normal = {-other.normal.x, -other.normal.y, -other.normal.z, other.normal.w};
            if (r.Bit()) {
                Mth::Vector tilt = UnitVector(r);
                float amount = r.Range(0.0f, 0.8f);
                Mth::Vector n{hit.normal.x + tilt.x * amount, hit.normal.y + tilt.y * amount, hit.normal.z + tilt.z * amount, 0.0f};
                float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (length > 0.01f)
                    hit.normal = {n.x / length, n.y / length, n.z / length, hit.normal.w};
            }
            hit.ends[r.Below(2)].point = other.ends[r.Below(2)].point;
        }
        hit.checksum = 0x7100 + static_cast<uint32_t>(k);
    }
}

void StandInCollision(StandIns& s)
{
    for (uint32_t address : {0x005BEAA0u, AddressOf(Tmr::GetSlomoFrameSeconds)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInCollisionFrame)));
    s.At({0x00417840, AddressOf(&Nx::CRectFeeler::Query)}, 4, [](Regs& r) {
        auto* rect = reinterpret_cast<Nx::CRectFeeler*>(r.ecx);
        const uint32_t* words = reinterpret_cast<const uint32_t*>(&rect->m_rect);
        Record({0x7840, Stable(r.ecx), Id(reinterpret_cast<uint32_t>(rect->mp_cache)), rect->m_hits.ignoreFlags,
                rect->m_hits.requireFlags});
        g_trace.insert(g_trace.end(), words, words + 12);
        return static_cast<uint32_t>(g_queryHits[g_queryNext++ & 3]);
    });
    s.At({0x00417C00, AddressOf(&Nx::CRectFeeler::JoinHits)}, 0, [](Regs& r) {
        auto* rect = reinterpret_cast<Nx::CRectFeeler*>(r.ecx);
        Record({0x7C00, Stable(r.ecx)});
        const JoinedSet& set = g_joinedSets[g_joinNext++ & 3];
        rect->m_joinedCount = set.count;
        for (int k = 0; k < 4; ++k)
            rect->m_joined[k] = set.hits[k];
        return 0u;
    });
    s.At({0x00417860, AddressOf(&Nx::CRectFeeler::DebugDraw)}, 16, [](Regs& r) {
        Record({0x7860, Stable(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x00417150, AddressOf(&Nx::CFeeler::DebugDraw)}, 16, [](Regs& r) {
        Record({0x7150, Stable(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x00434CE0}, 8, [](Regs& r) {
        Record({0x4CE0, PhysicsId(r.ecx), r.args[0], Stable(r.args[1])});
        if (g_triggerCalls++ == g_interruptAt) {
            if (g_interruptAt & 1)
                reinterpret_cast<CVehicleComponent*>(g_vehicle)->m_interrupted = true;
            else
                At<uint32_t>(g_object, 0x8) |= 1;
        }
        return 0u;
    });
    // The slerps (the original's x87 code and ours differ in the last bit now and then): recorded, and a simple
    // stand-in result - the angle from the matrices' bits, rows blended linearly.
    s.At({0x00405990, AddressOf(&Mth::SlerpInterpolator::SetMatrices)}, 8, [](Regs& r) {
        auto* slerp = reinterpret_cast<Mth::SlerpInterpolator*>(r.ecx);
        slerp->start = *reinterpret_cast<const Mth::Matrix*>(r.args[0]);
        slerp->end = *reinterpret_cast<const Mth::Matrix*>(r.args[1]);
        slerp->end.row[3] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the righting's target's is the original's stack)
        uint32_t hash = HashWords(&slerp->start, 16) * 31 + HashWords(&slerp->end, 16);
        Record({0x5990, Stable(r.ecx), Stable(r.args[0]), Stable(r.args[1]), hash});
        slerp->axis = {0.0f, 1.0f, 0.0f, 1.0f};
        slerp->angle = 0.05f + static_cast<float>(hash % 300) / 100.0f;
        slerp->small_rotation = (hash & 8) != 0;
        return static_cast<uint32_t>(slerp->small_rotation);
    });
    s.At({0x00405A20, AddressOf(&Mth::SlerpInterpolator::GetMatrix)}, 8, [](Regs& r) {
        auto* slerp = reinterpret_cast<const Mth::SlerpInterpolator*>(r.ecx);
        auto& out = *reinterpret_cast<Mth::Matrix*>(r.args[0]);
        float t;
        std::memcpy(&t, &r.args[1], 4);
        Record({0x5A20, Stable(r.ecx), Stable(r.args[0]), r.args[1]});
        float u = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float a = (&slerp->start.row[i].x)[j], b = (&slerp->end.row[i].x)[j];
                (&out.row[i].x)[j] = a + (b - a) * u;
            }
        return 0u;
    });
    s.At({0x00420670}, 12, [](Regs& r) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.args[0]);
        Record({0x0670, Id(r.ecx), Stable(r.args[0]), r.args[1] & 0xFF, r.args[2], Bits(feeler->m_distance)});
        return 0u;
    });
}

void BuildCollision(Random& r)
{
    BuildPhysics(r);
    auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
    v->mp_model = reinterpret_cast<Obj::CBaseComponent*>(g_model);
    v->m_pos = {r.Range(-30.0f, 30.0f), r.Range(-10.0f, 20.0f), r.Range(-30.0f, 30.0f), 1.0f};
    v->m_collisionImpulse = r.OneIn(3) ? 0.0f : r.Range(0.0f, 100.0f);
    v->m_freeFallTime = r.Range(0.0f, 2.0f);
    v->m_alignTime = r.Range(0.1f, 2.0f);
    v->m_alignRate = r.Range(0.0f, 5.0f);
    v->m_alignSpeed = r.Range(0.0f, 400.0f);
    v->m_alignWhenSlow = r.Bit();
    v->m_restitution = r.Range(0.0f, 1.0f);
    v->m_friction = r.Range(0.0f, 2.0f);
    v->m_tippedFriction = r.Range(0.0f, 2.0f);
    v->m_depthCorrection = r.Range(0.0f, 50.0f);
    v->m_wallSteering = r.Range(0.0f, 5.0f);
    if (r.OneIn(3)) { // (slow and still: righting)
        v->m_vel = {r.Range(-5.0f, 5.0f), r.Range(-5.0f, 5.0f), r.Range(-5.0f, 5.0f), 0.0f};
        v->m_rotvel = {r.Range(-0.3f, 0.3f), r.Range(-0.3f, 0.3f), r.Range(-0.3f, 0.3f), 0.0f};
    }
    // The inverse inertia: symmetric, plausible.
    Mth::Matrix& inertia = v->m_invInertia;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j <= i; ++j)
            (&inertia.row[i].x)[j] = (&inertia.row[j].x)[i] = i == j ? r.Range(0.001f, 0.1f) : r.Range(-0.01f, 0.01f);
    for (auto& c : v->m_colliders) {
        c.worldCorner = GridPoint(r);
        for (int e = 0; e < 2; ++e) {
            c.directions[e] = UnitVector(r);
            c.lengths[e] = r.Range(5.0f, 60.0f);
            c.worldEdges[e] = {c.directions[e].x * c.lengths[e], c.directions[e].y * c.lengths[e],
                               c.directions[e].z * c.lengths[e], c.directions[e].w};
        }
    }
    for (bool& hit : g_queryHits)
        hit = !r.OneIn(3);
    g_queryNext = 0;
    for (auto& set : g_joinedSets)
        BuildJoined(r, set);
    g_joinNext = 0;
    g_frame = r.OneIn(4) ? 0.0 : static_cast<double>(r.Range(0.0f, 0.1f));
    // The contacts (as left by an earlier step).
    auto* contacts = reinterpret_cast<uint32_t*>(0x006951C8);
    for (uint32_t i = 0; i < kContactBytes / 4; ++i)
        contacts[i] = Bits(r.Range(-20.0f, 20.0f));
    if (r.OneIn(8))
        contacts[r.Below(kContactBytes / 4)] = 0x7FC00000u;
    // The rect for FindEdgeContact.
    auto* rect = reinterpret_cast<Nx::CRectFeeler*>(g_rect);
    const JoinedSet& set = g_joinedSets[r.Below(4)];
    rect->m_joinedCount = set.count;
    for (int k = 0; k < 4; ++k)
        rect->m_joined[k] = set.hits[k];
}

void TraceCollision()
{
    TracePhysics();
    const uint32_t* w = reinterpret_cast<const uint32_t*>(0x006951C8);
    g_trace.insert(g_trace.end(), w, w + kContactBytes / 4);
}

void TestVehicleCollision()
{
    std::vector<Saved> saved = PhysicsGlobals();
    saved.push_back({0x006951C8, kContactBytes, {}});
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInAll(*s);
        StandInPhysics(*s);
        StandInCollision(*s);
        GameVariable<void*>(0x007CE5D8) = g_level;
        GameVariable<void*>(0x007CCE14) = g_skater;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x438BB0};
            GameVariable<int>(0x006951C4) = 12;
            for (int n = 0; n < 4000; ++n) {
                BuildCollision(r);
                GameVariable<int>(0x00696D60) = static_cast<int>(r.Below(3)) * 100;
                auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
                if (r.OneIn(5)) // (righting for 300 or 5300 ms, give or take one, at the clock's next reading)
                    v->m_rightingStart = g_clock + g_clockStep + GameVariable<int>(0x00696D60) - (r.Bit() ? 300 : 5300) +
                                         static_cast<int>(r.Below(3)) - 1;
                auto* contacts = reinterpret_cast<CVehicleComponent::Contact*>(0x006951C8);
                auto* contact = &contacts[r.Below(8)];
                const CVehicleComponent::Collider* collider = &v->m_colliders[r.Below(2)];
                auto* rect = reinterpret_cast<Nx::CRectFeeler*>(g_rect);
                int hit = static_cast<int>(r.Below(4)), end = static_cast<int>(r.Below(2));
                if (r.Bit()) // (a contact at a joined hit's end)
                    contact->point = rect->m_joined[hit].ends[end].point;
                Mth::Vector a{r.Range(-40.0f, 40.0f), r.Range(-40.0f, 40.0f), r.Range(-40.0f, 40.0f), r.Range(-1.0f, 1.0f)};
                Mth::Vector b = UnitVector(r);
                uint32_t which = r.Below(9);
                Record({0xCA5E, which});
                switch (which) {
                case 0: {
                    if (r.OneIn(4)) { // (a contact half way along an edge: the nearest edge a tie)
                        auto* c = &v->m_colliders[0];
                        c->worldCorner = {0.0f, 0.0f, 0.0f, 1.0f};
                        c->directions[0] = {1.0f, 0.0f, 0.0f, 1.0f};
                        c->directions[1] = {0.0f, 0.0f, 1.0f, 1.0f};
                        c->lengths[0] = 8.0f;
                        c->lengths[1] = 30.0f;
                        contact->point = {4.0f, 1.0f, static_cast<float>(r.Below(8)) + 5.0f, 1.0f};
                        collider = c;
                    }
                    double d = original ? Original<double(__stdcall*)(const void*, const void*)>(0x00436CF0)(contact, collider)
                                        : CVehicleComponent::GetPenetration(contact, collider);
                    RecordDouble(d);
                    break;
                }
                case 1: {
                    bool found = original ? Original<bool(__thiscall*)(void*, void*, void*, const void*, int, int)>(0x00436E50)(
                                                v, contact, rect, collider, hit, end)
                                          : v->FindEdgeContact(contact, rect, collider, hit, end);
                    Record({0xB001, static_cast<uint32_t>(found)});
                    break;
                }
                case 2: {
                    bool away = original ? Original<bool(__thiscall*)(void*, const void*)>(0x00437060)(v, contact)
                                         : v->NormalFacesAway(contact);
                    Record({0xB002, static_cast<uint32_t>(away)});
                    break;
                }
                case 3:
                    if (original)
                        Original<void(__thiscall*)(void*, const Mth::Vector*)>(0x00437120)(v, &a);
                    else
                        v->MoveBy(&a);
                    break;
                case 4: {
                    double d = original ? Original<double(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*)>(0x00437450)(
                                              v, &a, &b)
                                        : v->GetImpulseDenominator(&a, &b);
                    RecordDouble(d);
                    break;
                }
                case 5:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x00438770)(v);
                    else
                        v->UpdateRighting();
                    v->m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the original's slerp target row 3 is its stack)
                    break;
                case 6:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x00439D90)(v);
                    else
                        v->AlignInAir();
                    break;
                case 7:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043BEE0)(v);
                    else
                        v->Integrate();
                    break;
                default:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x00438BB0)(v);
                    else
                        v->Collide();
                    break;
                }
                TraceCollision();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    NormalizeNaNs(traces[0]);
    NormalizeNaNs(traces[1]);
    ReportCase(traces[0], traces[1], "vehicle collision");
    Compare(traces[0], traces[1], "vehicle: penetration, edge contacts, moves, impulses, righting, aligning, a step, collision");
}

// --- the component's life ---

alignas(16) uint8_t g_wheelBlock[4 + kWheels * sizeof(Wheel)];
alignas(16) uint8_t g_model2[0x210];
alignas(16) uint8_t g_geometry[0x240];
alignas(16) uint8_t g_skeletonData[0x10];
alignas(16) Mth::Matrix g_bones[8];
alignas(16) uint8_t g_camera[0x80];
alignas(16) uint8_t g_node[0x10];
void* g_fakeVtable[16];

struct FakeArray {
    uint32_t type;
    uint32_t m_size;
    uint32_t elements[8];
};
FakeArray g_colliderArray, g_pointArrays[2], g_wheelArray, g_ratioArray;
alignas(16) float g_points[2][3][4];

uint32_t g_present;   // which names are there (by a hash of the name)
int g_boneCount;
bool g_allocFails;
bool g_haveCamera;
int g_linkCount;
uint32_t g_restartLinks;

uint32_t LifeId(uint32_t p)
{
    auto in = [&](const void* base, size_t size, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        return p >= b && p < b + size ? tag + (p - b) : 0;
    };
    for (auto r : {in(g_wheelBlock, sizeof(g_wheelBlock), 0x71000000), in(g_model2, sizeof(g_model2), 0x72000000),
                   in(g_geometry, sizeof(g_geometry), 0x73000000), in(g_bones, sizeof(g_bones), 0x74000000),
                   in(g_camera, sizeof(g_camera), 0x75000000), in(&g_colliderArray, sizeof(FakeArray), 0x76000000),
                   in(g_pointArrays, sizeof(g_pointArrays), 0x77000000), in(&g_wheelArray, sizeof(FakeArray), 0x78000000),
                   in(&g_ratioArray, sizeof(FakeArray), 0x79000000), in(g_node, sizeof(g_node), 0x7A000000)})
        if (r)
            return r;
    return PhysicsId(p);
}

bool Present(uint32_t name) { return ((g_present >> ((name * 2654435761u) >> 27)) & 1) != 0; }

float __fastcall StandInArrayFloat(FakeArray* array, void*, uint32_t index)
{
    Record({0xCBF0, LifeId(reinterpret_cast<uint32_t>(array)), index});
    return g_scriptFloats[index & 15];
}

void __fastcall StandInVehicleUpdate(CVehicleComponent* v, void*)
{
    // (the passive updates MoveToNode makes: what they see, and a little movement)
    std::vector<uint32_t> seen{0x1D47, static_cast<uint32_t>(v->m_passive), Bits(v->m_pos.y)};
    for (int i = 0; i < kWheels; ++i)
        seen.push_back(Bits(reinterpret_cast<Wheel*>(g_wheels[i])->spin));
    g_trace.insert(g_trace.end(), seen.begin(), seen.end());
    v->m_pos.y += 0.25f;
    v->m_vel.x += 1.0f;
    for (auto& raw : g_wheels)
        reinterpret_cast<Wheel*>(raw)->spin = 3.0f;
}

void StandInLife(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, LifeId(r.ecx), r.args[0]});
        return static_cast<uint32_t>(Present(r.args[0]));
    });
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [](Regs& r) {
             Record({0x6AF0, LifeId(r.ecx), r.args[0]});
             return static_cast<uint32_t>(Present(r.args[0]) || Present(r.args[0] ^ 1));
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))}, 12,
         [](Regs& r) {
             Record({0x6860, LifeId(r.ecx), r.args[0], r.args[2]});
             FakeArray* array = r.args[0] == 0xFC8C4AC6 ? &g_colliderArray
                                : r.args[0] == 0xB3F8557E ? &g_wheelArray
                                                          : &g_ratioArray;
             *reinterpret_cast<FakeArray**>(r.args[1]) = array;
             return 1u;
         });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [](Regs& r) {
             Record({0x67C0, LifeId(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<void**>(r.args[1]) = g_refs[r.args[0] & 1];
             return 1u;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6950, LifeId(r.ecx), r.args[0], r.args[2]});
             bool found = Present(r.args[0] ^ 2) || r.args[2];
             if (found)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = 0x5000 + (r.args[0] & 0xFF);
             return static_cast<uint32_t>(found);
         });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, LifeId(r.ecx), r.args[0]});
        return reinterpret_cast<FakeArray*>(r.ecx)->elements[r.args[0] & 7];
    });
    for (uint32_t address : {0x0046CBF0u, AddressOf(&Script::CArray::GetFloat)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInArrayFloat)));
    s.At({0x00566520}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return reinterpret_cast<uint32_t>(g_skater);
    });
    s.At({0x005802C0, AddressOf(&Obj::CSkater::Set42BD60)}, 4, [](Regs& r) {
        Record({0x02C0, LifeId(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return g_allocFails ? 0u : reinterpret_cast<uint32_t>(g_wheelBlock);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, LifeId(r.args[0])});
        return 0u;
    });
    s.At({0x004E1950, AddressOf(Mem::EnsureManager)}, 0, [](Regs&) {
        Record({0x1950});
        return 1u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        return g_allocFails ? 0u : reinterpret_cast<uint32_t>(g_vehicle);
    });
    s.At({0x00435870, AddressOf(&Wheel::Construct)}, 0, [](Regs& r) {
        Record({0x5870, LifeId(r.ecx)});
        return r.ecx;
    });
    s.At({0x00437CF0, AddressOf(&Wheel::Destruct)}, 0, [](Regs& r) {
        Record({0x7CF0, LifeId(r.ecx)});
        return 0u;
    });
    s.At({0x00434F20}, 8, [](Regs& r) {
        Record({0x4F20, LifeId(r.args[0]), LifeId(r.args[1])});
        return 0u;
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, LifeId(r.ecx)});
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, LifeId(r.ecx), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
        return 0u;
    });
    s.At({0x0040AB50, AddressOf(&Nx::CCollCache::Construct)}, 0, [](Regs& r) {
        Record({0xAB50, LifeId(r.ecx)});
        return r.ecx;
    });
    s.At({0x0040AB40, AddressOf(&Nx::CCollCache::Destruct)}, 0, [](Regs& r) {
        Record({0xAB40, LifeId(r.ecx)});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, LifeId(r.ecx), r.args[0]});
        switch (r.args[0]) {
        case 0x27D7CD28: return reinterpret_cast<uint32_t>(g_input);
        case 0x286A8D26: return reinterpret_cast<uint32_t>(g_model2);
        case 0x222756D5: return reinterpret_cast<uint32_t>(g_skeletonData);
        case 0x5BD63B29: return reinterpret_cast<uint32_t>(g_core);
        case 0xE594F0A2: return reinterpret_cast<uint32_t>(g_trigger);
        case 0x7149B753: return reinterpret_cast<uint32_t>(g_blender);
        default: return 0u;
        }
    });
    s.At({0x00493040}, 0, [](Regs& r) {
        Record({0x3040, LifeId(r.ecx)});
        return reinterpret_cast<uint32_t>(g_geometry);
    });
    s.At({0x0045D990, AddressOf(&Obj::CCompositeObject::Hide)}, 4, [](Regs& r) {
        Record({0xD990, LifeId(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x005480F0}, 0, [](Regs& r) {
        Record({0x80F0, LifeId(r.ecx)});
        return reinterpret_cast<uint32_t>(g_skeletonData) + 4;
    });
    s.At({0x005480C0}, 0, [](Regs& r) {
        Record({0x80C0, LifeId(r.ecx)});
        return reinterpret_cast<uint32_t>(g_bones);
    });
    s.At({0x004A2BA0}, 0, [](Regs& r) {
        Record({0x2BA0, LifeId(r.ecx)});
        return static_cast<uint32_t>(g_boneCount);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Record({0xC050, LifeId(r.args[0])});
             *reinterpret_cast<Mth::Vector*>(r.args[1]) = g_vectors[1];
             return 1u;
         });
    s.At({0x0045C180, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetAngles))}, 0,
         [](Regs& r) {
             Record({0xC180, LifeId(r.args[0])});
             Mth::Vector angles{g_vectors[2].x * 0.01f, g_vectors[2].y * 0.03f, g_vectors[2].z * 0.01f, 1.0f};
             *reinterpret_cast<Mth::Vector*>(r.args[1]) = angles;
             return 1u;
         });
    s.At({0x0045E4C0, AddressOf(&Obj::CCompositeObject::SetTeleported)}, 4, [](Regs& r) {
        Record({0xE4C0, LifeId(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    // (the rotation about an axis: ours is float code, the original's x87 - the last bits differ; one stand-in for both)
    s.At({0x00404C60, AddressOf(static_cast<Mth::Matrix& (*)(Mth::Matrix&, const Mth::Vector&, float)>(
                          &Mth::CreateRotateMatrix))},
         0, [](Regs& r) {
             const auto& axis = *reinterpret_cast<const Mth::Vector*>(r.args[1]);
             Record({0x4C60, Bits(axis.x), Bits(axis.y), Bits(axis.z), r.args[2]});
             float angle;
             std::memcpy(&angle, &r.args[2], 4);
             float c = std::cos(angle), sn = std::sin(angle);
             auto& m = *reinterpret_cast<Mth::Matrix*>(r.args[0]);
             m.row[0] = {c, 0.0f, -sn, 0.0f};
             m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
             m.row[2] = {sn, 0.0f, c, 0.0f};
             m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
             return r.args[0];
         });
    // (the bones' rotations about x and z: the same - stand-ins whose angle is recorded)
    for (uint32_t axis = 0; axis < 2; ++axis) {
        uint32_t original = axis == 0 ? 0x00404DD0u : 0x00404E70u;
        uint32_t ours = axis == 0 ? AddressOf(Mth::CreateRotateXMatrix) : AddressOf(Mth::CreateRotateZMatrix);
        s.At({original, ours}, 0, [axis](Regs& r) {
            Record({0x4D00 + axis, r.args[1]});
            float angle;
            std::memcpy(&angle, &r.args[1], 4);
            float c = std::cos(std::fmod(angle, 6.2831855f)), sn = std::sin(std::fmod(angle, 6.2831855f));
            auto& m = *reinterpret_cast<Mth::Matrix*>(r.args[0]);
            m.row[0] = axis == 0 ? Mth::Vector{1.0f, 0.0f, 0.0f, 0.0f} : Mth::Vector{c, sn, 0.0f, 0.0f};
            m.row[1] = axis == 0 ? Mth::Vector{0.0f, c, sn, 0.0f} : Mth::Vector{-sn, c, 0.0f, 0.0f};
            m.row[2] = axis == 0 ? Mth::Vector{0.0f, -sn, c, 0.0f} : Mth::Vector{0.0f, 0.0f, 1.0f, 0.0f};
            m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            return r.args[0];
        });
    }
    s.At({0x004A09C0}, 0, [](Regs& r) {
        Record({0x09C0, r.args[0]});
        return g_haveCamera ? reinterpret_cast<uint32_t>(g_camera) : 0u;
    });
    s.At({0x00485A00}, 0, [](Regs& r) {
        Record({0x5A00, LifeId(r.ecx)});
        return r.ecx + 0x40;
    });
    s.At({0x00485A10}, 0, [](Regs& r) {
        Record({0x5A10, LifeId(r.ecx)});
        return r.ecx;
    });
}

void BuildLife(Random& r)
{
    BuildCollision(r);
    auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
    g_fakeVtable[1] = reinterpret_cast<void*>(&StandInVehicleUpdate);
    v->m_vtable = g_fakeVtable;
    v->mp_skeleton = reinterpret_cast<Obj::CBaseComponent*>(g_skeletonData);
    v->m_wheelCount = kWheels;
    if (r.OneIn(6))
        v->m_wheelCount = 1 + static_cast<int>(r.Below(kWheels));
    for (auto& raw : g_wheels) {
        auto& wheel = *reinterpret_cast<Wheel*>(raw);
        wheel.springRate = r.Range(1.0f, 20.0f);
        wheel.maxDrawnY = r.Range(-20.0f, 20.0f);
        if (r.OneIn(4))
            wheel.maxDrawnY = wheel.y;
        wheel.rotation = r.Range(-3.2f, 3.2f);
    }
    if (r.OneIn(5)) // (no fixed wheel)
        for (auto& raw : g_wheels)
            reinterpret_cast<Wheel*>(raw)->steeringSide = 1 + static_cast<int>(r.Below(2));
    v->m_gravityTime = r.OneIn(2) ? 0.0f : r.Range(-0.1f, 0.2f);
    v->m_awake = r.OneIn(4) ? 0 : 1;
    // The object: a rotation for its matrix, a position.
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    for (size_t i = 0xC0; i < sizeof(g_object); i += 4) // (its display matrix and the rest as left)
        At<float>(g_object, static_cast<uint32_t>(i)) = r.Range(-9.0f, 9.0f);
    object->m_matrix = v->m_matrix;
    object->m_pos = {r.Range(-100.0f, 100.0f), r.Range(-100.0f, 100.0f), r.Range(-100.0f, 100.0f), 1.0f};
    // The structure's contents: which names are there, the arrays.
    g_present = r.Next() ^ (r.Next() << 16);
    g_colliderArray.m_size = r.OneIn(8) ? 0 : 1 + r.Below(2);
    for (int k = 0; k < 2; ++k) {
        g_colliderArray.elements[k] = reinterpret_cast<uint32_t>(&g_pointArrays[k]);
        g_pointArrays[k].m_size = 3;
        for (int i = 0; i < 3; ++i) {
            for (float& f : g_points[k][i])
                f = r.Range(-40.0f, 40.0f);
            g_pointArrays[k].elements[i] = reinterpret_cast<uint32_t>(g_points[k][i]);
        }
    }
    g_wheelArray.m_size = 1 + r.Below(kWheels);
    for (int i = 0; i < 8; ++i)
        g_wheelArray.elements[i] = 0x5300 + static_cast<uint32_t>(i);
    g_ratioArray.m_size = r.Below(7);
    g_allocFails = r.OneIn(8);
    if (g_allocFails)
        g_wheelArray.m_size = 0; // (no wheels to read into nothing: the original would crash too)
    GameVariable<bool>(0x007CC979) = r.Bit();
    GameVariable<bool>(0x007D1EC0) = r.OneIn(8);
    // The model: its geometry (wheel places, a matrix), its skeleton.
    At<void*>(g_model2, 0x208) = g_model2 + 4;
    for (size_t i = 0; i < sizeof(g_geometry); i += 4)
        At<float>(g_geometry, static_cast<uint32_t>(i)) = r.Range(-50.0f, 50.0f);
    g_boneCount = static_cast<int>(r.Below(7));
    for (auto& bone : g_bones)
        for (int i = 0; i < 4; ++i)
            bone.row[i] = {r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f)};
    // The camera: its matrix (a rotation) and position.
    auto* view = reinterpret_cast<Mth::Matrix*>(g_camera);
    *view = v->m_matrix;
    view->row[0].w = r.Range(-1.0f, 1.0f);
    view->row[1].w = r.Range(-1.0f, 1.0f);
    At<Mth::Vector>(g_camera, 0x40) = {r.Range(-100.0f, 100.0f), r.Range(-100.0f, 100.0f), r.Range(-100.0f, 100.0f), 1.0f};
    g_haveCamera = !r.OneIn(5);
    // The frame: steps at the limit (exactly 1/30 s) and more than six. (Not a frame of nothing: the state goes to
    // infinities and NaNs - the original's x87 compares them differently from SSE, and the game never has one.)
    if (g_frame == 0.0)
        g_frame = 1.0 / 60.0;
    if (r.OneIn(6))
        g_frame = static_cast<double>(std::bit_cast<float>(0x3D088889u));
    else if (r.OneIn(6))
        g_frame = r.Range(0.1f, 0.3f);
    // The block a new wheel array goes in.
    for (size_t i = 0; i < sizeof(g_wheelBlock); i += 4)
        At<float>(g_wheelBlock, static_cast<uint32_t>(i)) = r.Range(-30.0f, 30.0f);
    At<int>(g_wheelBlock, 0) = static_cast<int>(1 + r.Below(kWheels));
}

void TraceLife()
{
    TraceCollision();
    auto words = [](const void* p, size_t size) {
        const uint32_t* w = static_cast<const uint32_t*>(p);
        for (size_t i = 0; i < size / 4; ++i)
            g_trace.push_back(LifeId(w[i]));
    };
    g_trace.push_back(0x0B1EC7);
    words(g_object, sizeof(g_object));
    g_trace.push_back(0x0B10C4);
    words(g_wheelBlock, sizeof(g_wheelBlock));
    g_trace.push_back(0x0B0E5);
    words(g_bones, sizeof(g_bones));
}

std::vector<Saved> LifeGlobals()
{
    std::vector<Saved> saved = PhysicsGlobals();
    saved.push_back({0x006951C8, kContactBytes, {}});
    saved.push_back({0x007CC979, 1, {}});
    saved.push_back({0x007D1EC0, 1, {}});
    return saved;
}



void TestVehicleLife()
{
    std::vector<Saved> saved = LifeGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInAll(*s);
        StandInPhysics(*s);
        StandInCollision(*s);
        StandInLife(*s);
        GameVariable<void*>(0x007CE5D8) = g_level;
        GameVariable<void*>(0x007CCE14) = g_skater;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x43C1D0};
            GameVariable<int>(0x006951C4) = 12;
            for (int n = 0; n < 3000; ++n) {
                BuildLife(r);
                GameVariable<int>(0x00696D60) = static_cast<int>(r.Below(3)) * 100;
                auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
                uint32_t which = r.Below(12);
                uint32_t flags = r.Below(4);
                int spoke = static_cast<int>(r.Below(13));
                bool outer = r.Bit();
                Mth::Vector point{};
                const Wheel* wheel = reinterpret_cast<const Wheel*>(g_wheels[r.Below(kWheels)]);
                Record({0xCA5E, which});
                switch (which) {
                case 0: {
                    void* made = original ? Original<void*(__cdecl*)()>(0x0043AA30)() : CVehicleComponent::Create();
                    Record({0xC0, LifeId(reinterpret_cast<uint32_t>(made))});
                    break;
                }
                case 1:
                    if (original)
                        Original<void*(__thiscall*)(void*)>(0x00437C00)(v);
                    else
                        v->Construct();
                    break;
                case 2:
                case 3: {
                    // (a wheel array as InitFromStructure makes it, or none)
                    v->m_wheels = r.Bit() ? reinterpret_cast<Wheel*>(g_wheelBlock + 4) : nullptr;
                    if (which == 2) {
                        if (original)
                            Original<void(__thiscall*)(void*)>(0x0043B990)(v);
                        else
                            v->DestroyBody();
                    } else {
                        void* result = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x0043C1A0)(v, flags)
                                                : v->Destroy(flags);
                        Record({0xC1, LifeId(reinterpret_cast<uint32_t>(result))});
                    }
                    break;
                }
                case 4:
                    if (r.Bit()) { // (a new component: the wheels made here)
                        v->m_wheels = nullptr;
                        v->m_wheelCount = 0;
                    }
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x00437D10)(v, g_params);
                    else
                        v->InitFromStructure(reinterpret_cast<Script::CStruct*>(g_params));
                    break;
                case 5:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043BA20)(v);
                    else
                        v->Finalize();
                    break;
                case 6:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043C1D0)(v);
                    else
                        v->Update();
                    break;
                case 7:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043A070)(v);
                    else
                        v->UpdateSkeleton();
                    break;
                case 8: {
                    Mth::Vector* out = original
                                           ? Original<Mth::Vector*(__thiscall*)(void*, Mth::Vector*, const void*, int, int)>(
                                                 0x0043A2C0)(v, &point, wheel, spoke, outer ? 1 : 0)
                                           : v->GetWheelRimPoint(&point, wheel, spoke, outer);
                    Record({0xC8, static_cast<uint32_t>(out == &point), Bits(point.x), Bits(point.y), Bits(point.z),
                            Bits(point.w)});
                    break;
                }
                case 9:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043A400)(v);
                    else
                        v->DebugDraw();
                    break;
                case 10:
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0043AA90)(v, g_node);
                    else
                        v->MoveToNode(reinterpret_cast<Script::CStruct*>(g_node));
                    break;
                default: {
                    bool done = original ? Original<bool(__thiscall*)(void*, void*, void*)>(0x0043B720)(v, g_params, nullptr)
                                         : v->PlaceBeforeCamera(reinterpret_cast<Script::CStruct*>(g_params), nullptr);
                    Record({0xCB, static_cast<uint32_t>(done)});
                    break;
                }
                }
                TraceLife();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    NormalizeNaNs(traces[0]);
    NormalizeNaNs(traces[1]);
    ReportCase(traces[0], traces[1], "vehicle life");
    Compare(traces[0], traces[1], "vehicle: create, destroy, parameters, finalize, update, bones, rim points, moves, camera");
}

// Vehicle_MoveToRestart: which node it goes to (MoveToNode stood in).
alignas(16) uint8_t g_script[0x100];

void TestVehicleRestart()
{
    std::vector<Saved> saved = LifeGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInAll(*s);
        StandInLife(*s);
        s->At({0x0043AA90, AddressOf(&CVehicleComponent::MoveToNode)}, 4, [](Regs& r) {
            Record({0xAA90, LifeId(r.ecx), r.args[0]});
            return 0u;
        });
        s->At({0x0045C1B0, AddressOf(SkateScript::GetNode)}, 0, [](Regs& r) {
            Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
            return 0x6000u + (r.args[0] & 0xFFF);
        });
        s->At({0x0045C3A0, AddressOf(static_cast<uint32_t (*)(uint32_t)>(&SkateScript::GetLinkCount))}, 0, [](Regs& r) {
            Record({0xC3A0, r.args[0]});
            return static_cast<uint32_t>(g_linkCount);
        });
        s->At({0x0045C3E0, AddressOf(static_cast<uint32_t (*)(uint32_t, int)>(&SkateScript::GetLink))}, 0, [](Regs& r) {
            Record({0xC3E0, r.args[0], r.args[1]});
            return 0x700u + r.args[1];
        });
        s->At({0x0046B680, AddressOf(Obj::IsRestartNode)}, 0, [](Regs& r) {
            Record({0xB680, r.args[0]});
            return (g_restartLinks >> (r.args[0] & 7)) & 1;
        });
        s->At({0x00566100}, 4, [](Regs& r) {
            Record({0x6100, r.ecx, r.args[0]});
            return 0x7777u;
        });
        GameVariable<void*>(0x007CE5D8) = g_level;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x43B670};
            for (int n = 0; n < 500; ++n) {
                g_present = r.Next() ^ (r.Next() << 16);
                g_linkCount = static_cast<int>(r.Below(4));
                g_restartLinks = r.Next();
                At<uint32_t>(g_level, 0x40) = r.OneIn(3) ? 0xD443A2BC : 0x1234;
                At<uint32_t>(g_script, 0xB8) = 0x4400 + r.Below(16);
                auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
                Record({0xCA5E, static_cast<uint32_t>(n)});
                bool done = original ? Original<bool(__thiscall*)(void*, void*, void*)>(0x0043B670)(v, g_params, g_script)
                                     : v->MoveToRestart(reinterpret_cast<Script::CStruct*>(g_params),
                                                        reinterpret_cast<Script::CScript*>(g_script));
                Record({0xD0, static_cast<uint32_t>(done)});
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    NormalizeNaNs(traces[0]);
    NormalizeNaNs(traces[1]);
    ReportCase(traces[0], traces[1], "vehicle restart");
    Compare(traces[0], traces[1], "vehicle: Vehicle_MoveToRestart");
}

} // namespace

SELFTEST(Early, TestVehicleMethods, RemoveStandIns);
SELFTEST(Early, TestVehiclePhysics, RemoveStandIns);
SELFTEST(Early, TestVehicleCollision, RemoveStandIns);
SELFTEST(Early, TestVehicleLife, RemoveStandIns);
SELFTEST(Early, TestVehicleRestart, RemoveStandIns);
