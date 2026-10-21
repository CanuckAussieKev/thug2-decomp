// Self-tests for the motion component's Obj_Rotate (src/game/obj/motion.cpp): the original against ours, with the
// turn's start (0x00425520) replaced by a stand-in that records the target orientation it's given.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/matrix.h"
#include "game/obj/motion.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using Obj::CMotionComponent;
using Script::CComponent;
using Script::CStruct;
using Script::CVector;

constexpr uint32_t kRelative = 0x91A4C826, kAbsolute = 0x592089A9, kConvert = 0x26AF9DC9;

// The targets the stand-in for 0x00425520 was given (thiscall, RET 8: params, target).
struct Started {
    CMotionComponent* motion;
    CStruct* params;
    Mth::Matrix target;
};
std::vector<Started> g_started;

void __fastcall StandInStartRotation(CMotionComponent* motion, void* /*edx*/, CStruct* params, const Mth::Matrix* target)
{
    g_started.push_back({motion, params, *target});
}

// A parameter structure made by hand (no script pools needed): an optional vector and an optional flag.
struct Params {
    CStruct structure{};
    CComponent components[3]{};
    CVector vector{}, second{};

    // (with `both`, an absolute = (z, x, y) follows a relative vector)
    Params(uint32_t vectorName, float x, float y, float z, bool convert, bool both = false)
    {
        CComponent* last = nullptr;
        auto add = [&](CComponent& c) {
            if (last)
                last->next = &c;
            else
                structure.m_head = &c;
            last = &c;
        };
        if (vectorName) {
            vector = {0, x, y, z};
            components[0].typeAndFlag = Script::ESYMBOLTYPE_VECTOR << 1;
            components[0].name = vectorName;
            components[0].vector = &vector;
            add(components[0]);
        }
        if (both) {
            second = {0, z, x, y};
            components[2].typeAndFlag = Script::ESYMBOLTYPE_VECTOR << 1;
            components[2].name = kAbsolute;
            components[2].vector = &second;
            add(components[2]);
        }
        if (convert) {
            components[1].typeAndFlag = Script::ESYMBOLTYPE_NAME << 1;
            components[1].checksum = kConvert;
            add(components[1]);
        }
    }
};

// What each version hands to the turn's start for the same component, object and parameters.
bool Run(bool original, Params& params, std::vector<Started>* out)
{
    alignas(16) uint8_t object[0x200] = {};
    Mth::Matrix& matrix = *reinterpret_cast<Mth::Matrix*>(object + 0x6C);
    matrix.row[0] = {0.36f, 0.48f, -0.8f, 0.0f}; // a rotation (rows orthonormal), not the identity
    matrix.row[1] = {-0.8f, 0.6f, 0.0f, 0.0f};
    matrix.row[2] = {0.48f, 0.64f, 0.6f, 0.0f};
    matrix.row[3] = {1.0f, 2.0f, 3.0f, 1.0f};
    alignas(16) uint8_t component[0x260] = {};
    auto* motion = reinterpret_cast<CMotionComponent*>(component);
    motion->m_object = reinterpret_cast<Obj::CCompositeObject*>(object);

    // An empty symbol table if there is none (offline): a flag in a parameter structure is also looked up as a global
    // structure it might include.
    static Script::CSymbolTableEntry* empty[0x1000] = {};
    Script::CSymbolTableEntry**& table = GameVariable<Script::CSymbolTableEntry**>(0x006F3624);
    Script::CSymbolTableEntry** savedTable = table;
    if (!table)
        table = empty;
    g_started.clear();
    {
        TestStandIns::Patch start(0x00425520, TestStandIns::Branch(0xE9, 0x00425520,
                                                                  reinterpret_cast<void*>(&StandInStartRotation)));
        if (original)
            Original<void(__thiscall*)(CMotionComponent*, CStruct*)>(0x00425600)(motion, &params.structure);
        else
            motion->RotateTo(&params.structure);
    }
    table = savedTable;
    for (Started& s : g_started) {
        if (s.motion != motion || s.params != &params.structure)
            return false;
    }
    *out = g_started;
    return true;
}

// Within 2 units in the last place: Mth::Matrix::SetFromEulerAngles itself gives results 1 ulp apart for the same
// (bit-identical) angles from one call to the next here (the CRT's sinf / cosf and the FPU's state; seen with the
// order of the two runs swapped), so the absolute case can't be compared bit for bit.
bool SameMatrix(const Mth::Matrix& a, const Mth::Matrix& b)
{
    const float* x = &a.row[0].x;
    const float* y = &b.row[0].x;
    for (int i = 0; i < 16; ++i) {
        int32_t p, q;
        std::memcpy(&p, &x[i], 4);
        std::memcpy(&q, &y[i], 4);
        if ((p < 0) != (q < 0) ? (p & 0x7FFFFFFF) + (q & 0x7FFFFFFF) > 2 : (p > q ? p - q : q - p) > 2)
            return false;
    }
    return true;
}

void TestMotionRotateTo()
{
    struct Case {
        uint32_t name;
        float x, y, z;
        bool convert;
        bool both = false;
    };
    const Case cases[] = {
        {kRelative, 10.0f, 20.0f, 30.0f, false}, {kRelative, 0.0f, 0.0f, 22.5f, true},
        {kAbsolute, 10.0f, 20.0f, 30.0f, false}, {kAbsolute, 0.0f, 0.0f, -45.0f, false},
        {kAbsolute, 5.0f, -70.0f, 120.0f, true}, {0, 0, 0, 0, false},
        {0, 0, 0, 0, true}, {kRelative, 30.0f, -10.0f, 45.0f, false, true},
    };
    bool ok = true;
    for (const Case& c : cases) {
        Params params(c.name, c.x, c.y, c.z, c.convert, c.both);
        std::vector<Started> expected, actual;
        bool ran = Run(true, params, &expected) && Run(false, params, &actual);
        bool same = ran && expected.size() == actual.size() && expected.size() == (c.name ? 1u : 0u);
        for (size_t i = 0; same && i < expected.size(); ++i)
            same = SameMatrix(expected[i].target, actual[i].target);
        if (!same) {
            ok = false;
            Log("  Obj_Rotate %08X (%g %g %g) convert %d: %u / %u turns started", c.name, c.x, c.y, c.z,
                c.convert, static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
            if (!expected.empty() && !actual.empty())
                for (int r = 0; r < 4; ++r)
                    for (int k = 0; k < 4; ++k) {
                        uint32_t e, a;
                        std::memcpy(&e, &(&expected[0].target.row[r].x)[k], 4);
                        std::memcpy(&a, &(&actual[0].target.row[r].x)[k], 4);
                        if (e != a)
                            Log("    [%d][%d] %08X / %08X", r, k, e, a);
                    }
        }
    }
    Check(ok, "Obj_Rotate: original vs ours (relative, absolute, converted, neither)");
}

} // namespace

SELFTEST(Early, TestMotionRotateTo);
