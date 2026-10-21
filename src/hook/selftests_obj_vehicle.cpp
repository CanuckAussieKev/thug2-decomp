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
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/vehicle_component.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
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
             Record({0x6F10, Id(r.ecx), r.args[0], Id(r.args[1]), r.args[2]});
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
    Log("  %s: first difference %u words into case %u (%u)", what, static_cast<unsigned>(i - marker),
        marker + 1 < expected.size() ? expected[marker + 1] : 0xFFFFFFFFu, static_cast<unsigned>(i));
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
        feeler->m_normal = {0.1f * k, 0.9f, -0.2f, 1.0f};
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
        }
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
        g_hitDistance[k] = r.Range(0.0f, 1.0f);
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
    ReportCase(traces[0], traces[1], "vehicle physics");
    Compare(traces[0], traces[1], "vehicle: steering, suspension, tyres, drive, brakes, damping, contacts, controls, skater");
}

} // namespace

SELFTEST(Early, TestVehicleMethods, RemoveStandIns);
SELFTEST(Early, TestVehiclePhysics, RemoveStandIns);
