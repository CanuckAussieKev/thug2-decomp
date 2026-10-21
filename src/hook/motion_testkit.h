#pragma once

// Shared pieces of the motion component's self-tests (selftests_obj_motion*.cpp): original vs ours on one stand-in
// component in skater_core_testkit.h's arena, every callee a recording stand-in. The script structures the
// functions read are stand-ins too: their getters answer from g_params (a scenario per variant), so which
// parameters are given is chosen by the test; what's added to a structure is recorded.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/obj/motion.h"
#include "game/script/struct.h"
#include "hook/skater_core_testkit.h"

namespace MotionKit {

using namespace SkaterCoreKit;

// --- where things are in the arena ---
constexpr uint32_t kMotion = 0x0000;      // the component (0x24C)
constexpr uint32_t kObj = 0x0300;      // its object (0x110: +0x4C pos, +0x5C old pos, +0x6C matrix, +0xAC vel)
constexpr uint32_t kFollower = 0x0480;    // a path follower (0xE4)
constexpr uint32_t kLock = 0x0580;        // a lockobj component (0x90)
constexpr uint32_t kScript = 0x0640;      // a script (0x100: +0x14 its params, +0xA4 its wait type)
constexpr uint32_t kParams = 0x0760;      // the member function's parameter structure (a stand-in: never read)
constexpr uint32_t kScriptParams = 0x0780; // the script's parameter structure (a stand-in)
constexpr uint32_t kTrail = 0x0800;       // a follow trail (0x424)
constexpr uint32_t kOtherObj = 0x0C40;       // another object (0x110)
constexpr uint32_t kOtherMotion = 0x0D60; // its motion component (0x24C)
constexpr uint32_t kAlloc = 0x1000;       // what the allocation stand-in hands out (0x1000)
constexpr uint32_t kMine = 0x2000;        // each test's own (to 0x4000)

inline Obj::CMotionComponent* Motion() { return reinterpret_cast<Obj::CMotionComponent*>(ArenaPtr(kMotion)); }
inline Script::CStruct* Params() { return reinterpret_cast<Script::CStruct*>(ArenaPtr(kParams)); }
inline Script::CScript* TheScript() { return reinterpret_cast<Script::CScript*>(ArenaPtr(kScript)); }

// Floats everywhere, then the pointers: the motion's object, the script's params.
inline void FillMotionArena()
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        ArenaAt<float>(i) = RandomFloat(3.0f);
    ArenaAt<uint8_t*>(kMotion + 0x0C) = ArenaPtr(kObj);
    ArenaAt<uint8_t*>(kScript + 0x14) = ArenaPtr(kScriptParams);
}

// A float with all its mantissa bits in use (sums depend on their order), in [-range, range].
inline float RichFloat(float range)
{
    return static_cast<float>((static_cast<double>(Next()) / 4294967296.0 * 2.0 - 1.0) * range);
}
inline void RichVector(uint32_t offset, float range)
{
    for (int i = 0; i < 4; ++i)
        ArenaAt<float>(offset + 4 * i) = RichFloat(range);
}

// --- the parameters ---
enum class Type : uint8_t { Float, Integer, Checksum, Vector, Flag, Structure };
struct Param {
    uint32_t name;
    Type type;
    uint32_t value[3];
};
inline std::vector<Param> g_params;
inline void Give(uint32_t name, float value) { g_params.push_back({name, Type::Float, {Bits(value)}}); }
inline void GiveInteger(uint32_t name, int value) { g_params.push_back({name, Type::Integer, {uint32_t(value)}}); }
inline void GiveChecksum(uint32_t name, uint32_t value) { g_params.push_back({name, Type::Checksum, {value}}); }
inline void GiveVector(uint32_t name, float x, float y, float z)
{
    g_params.push_back({name, Type::Vector, {Bits(x), Bits(y), Bits(z)}});
}
inline void GiveFlag(uint32_t name) { g_params.push_back({name, Type::Flag, {}}); }
inline void GiveStructure(uint32_t name, uint32_t pointer) { g_params.push_back({name, Type::Structure, {pointer}}); }
inline const Param* Find(uint32_t name, Type type)
{
    for (const Param& p : g_params)
        if (p.name == name && p.type == type)
            return &p;
    return nullptr;
}
// Only the parameter structure at kParams answers; any other is empty.
inline bool IsParams(const Frame& f) { return f.ecx == reinterpret_cast<uint32_t>(ArenaPtr(kParams)); }

inline StandIn GetFloat()
{
    using Fn = bool (Script::CStruct::*)(uint32_t, float*, int);
    return {0x004766D0, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetFloat))},
            [](Frame& f, Result& r) {
                const Param* p = IsParams(f) ? Find(f.args[0], Type::Float) : nullptr;
                r.eax = p ? 1 : 0;
                if (p)
                    std::memcpy(reinterpret_cast<void*>(f.args[1]), p->value, 4);
            }};
}
inline StandIn GetInteger()
{
    using Fn = bool (Script::CStruct::*)(uint32_t, int*, int);
    return {0x00476630, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetInteger))},
            [](Frame& f, Result& r) {
                const Param* p = IsParams(f) ? Find(f.args[0], Type::Integer) : nullptr;
                r.eax = p ? 1 : 0;
                if (p)
                    std::memcpy(reinterpret_cast<void*>(f.args[1]), p->value, 4);
            }};
}
inline StandIn GetChecksum()
{
    using Fn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    return {0x00476950, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetChecksum))},
            [](Frame& f, Result& r) {
                const Param* p = IsParams(f) ? Find(f.args[0], Type::Checksum) : nullptr;
                r.eax = p ? 1 : 0;
                if (p)
                    std::memcpy(reinterpret_cast<void*>(f.args[1]), p->value, 4);
            }};
}
inline StandIn GetVector()
{
    using Fn = bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int);
    return {0x00476FB0, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetVector))},
            [](Frame& f, Result& r) {
                const Param* p = IsParams(f) ? Find(f.args[0], Type::Vector) : nullptr;
                r.eax = p ? 1 : 0;
                if (p) {
                    std::memcpy(reinterpret_cast<void*>(f.args[1]), p->value, 12);
                    reinterpret_cast<float*>(f.args[1])[3] = 1.0f;
                }
            }};
}
inline StandIn GetStructure()
{
    using Fn = bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int);
    return {0x004767C0, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetStructure))},
            [](Frame& f, Result& r) {
                const Param* p = IsParams(f) ? Find(f.args[0], Type::Structure) : nullptr;
                r.eax = p ? 1 : 0;
                if (p)
                    std::memcpy(reinterpret_cast<void*>(f.args[1]), p->value, 4);
            }};
}
inline StandIn ContainsFlag()
{
    using Fn = bool (Script::CStruct::*)(uint32_t);
    return {0x00476B40, 4, 1, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::ContainsFlag))},
            [](Frame& f, Result& r) { r.eax = IsParams(f) && Find(f.args[0], Type::Flag) ? 1 : 0; }};
}
// (by name: the name's checksum recorded, not the pointer - the strings are the exe's in one run, ours in the other)
inline StandIn ContainsFlagNamed()
{
    using Fn = bool (Script::CStruct::*)(const char*);
    return {0x00476BA0, 4, 0, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::ContainsFlag))},
            [](Frame& f, Result& r) {
                uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(f.args[0]));
                Record(name);
                r.eax = IsParams(f) && Find(name, Type::Flag) ? 1 : 0;
            }};
}
inline StandIn AddInteger()
{
    using Fn = void (Script::CStruct::*)(uint32_t, int);
    return {0x00477B80, 8, 2, Ret::None, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::AddInteger))}};
}
inline StandIn AddFloatNamed()
{
    using Fn = void (Script::CStruct::*)(const char*, float);
    return {0x00477D10, 8, 0, Ret::None, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::AddFloat))},
            [](Frame& f, Result&) {
                Record(Crc::GenerateCRCFromString(reinterpret_cast<const char*>(f.args[0])));
                Record(f.args[1]);
            }};
}

// --- components and objects ---
struct ComponentAnswer {
    uint32_t object; // arena offset of the object asked
    uint32_t type;
    uint32_t component; // arena offset, or 0 for none
};
inline std::vector<ComponentAnswer> g_components;
inline StandIn GetComponent()
{
    return {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
            [](Frame& f, Result& r) {
                r.eax = 0;
                for (const ComponentAnswer& a : g_components)
                    if (f.ecx == reinterpret_cast<uint32_t>(ArenaPtr(a.object)) && f.args[0] == a.type)
                        r.eax = a.component ? reinterpret_cast<uint32_t>(ArenaPtr(a.component)) : 0;
            }};
}

// Mem::Calloc (0x00401CC0): blocks from kAlloc on (or none, when g_allocFails).
inline uint32_t g_allocNext = kAlloc;
inline bool g_allocFails = false;
inline StandIn Calloc(uint32_t ours)
{
    return {0x00401CC0, 0, 1, Ret::Int, false, 0, {ours}, [](Frame& f, Result& r) {
                if (g_allocFails) {
                    r.eax = 0;
                    return;
                }
                r.eax = reinterpret_cast<uint32_t>(ArenaPtr(g_allocNext));
                g_allocNext += (f.args[0] + 15) & ~15u;
            }};
}

} // namespace MotionKit
