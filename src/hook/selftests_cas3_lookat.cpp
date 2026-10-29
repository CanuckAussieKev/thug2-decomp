// Self-tests for the bones' look-at (src/game/gfx/bone_look_at.cpp) and Vector::RotateAboutZ (vector_rotate_z.cpp) -
// batch CAS3-4: each original against ours - the vector clamps and the Euler angle conversions over random values,
// the look-at made, configured by structures and member functions, updated and applied to a pose over random
// targets, axes, limits and weights, and destroyed; the skeleton's bone lookup, the object lookup and the memory
// stood in; the results, the look-at's bytes and the pose compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/bone_look_at.h"
#include "game/gfx/skeleton.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Gfx::CBoneLookAt;
using Mth::Quat;
using Mth::Vector;

alignas(16) uint8_t g_lookAt[sizeof(CBoneLookAt) + 0x10];
alignas(16) uint8_t g_object[0x120];   // the composite object (position +0x4C, matrix +0x6C)
alignas(16) uint8_t g_skelComp[0x20];  // its skeleton component (+0x18 the skeleton)
alignas(16) uint8_t g_skeleton[0x20];
alignas(16) uint8_t g_owner[0x70];     // (+0x64: the owner's object)
alignas(16) uint8_t g_targets[2][0x60];
Quat g_pose[8];
uint32_t g_seed;
bool g_noSkeleton;

float Random(float low, float high)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return low + (high - low) * static_cast<float>((g_seed >> 8) & 0xFFFF) / 65535.0f;
}
int RandomInt(int low, int high)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return low + static_cast<int>((g_seed >> 8) % static_cast<uint32_t>(high - low + 1));
}
uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}
void RecordFloats(const float* values, int count)
{
    for (int i = 0; i < count; ++i)
        g_trace.push_back(Bits(values[i]));
}
Quat RandomQuat()
{
    Quat q = {Random(-1, 1), Random(-1, 1), Random(-1, 1), Random(-1, 1)};
    float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}

void StandInLookAtCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx == reinterpret_cast<uint32_t>(g_object) ? 1u : r.ecx, r.args[0]});
        return g_noSkeleton ? 0u : reinterpret_cast<uint32_t>(g_skelComp);
    });
    s.At({0x004A2B60, AddressOf(&Gfx::CSkeleton::GetBoneIndex)}, 4, [](Regs& r) {
        Record({0x2B60, r.ecx == reinterpret_cast<uint32_t>(g_skeleton) ? 1u : r.ecx, r.args[0]});
        return r.args[0] >= 0xB0 && r.args[0] < 0xB8 ? r.args[0] - 0xB0 : 0xFFFFFFFFu;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.args[0]});
        return r.args[0] == 0x0B1 ? reinterpret_cast<uint32_t>(g_targets[0])
                                  : r.args[0] == 0x0B2 ? reinterpret_cast<uint32_t>(g_targets[1]) : 0u;
    });
}

// A look-at's settings as a structure.
struct Settings {
    Params p;
    Script::CVector vectors[4];
};

using This = void*;

void TestBoneLookAt()
{
    Setup();
    g_saved.push_back({0x006F0FB8, 4, {}});
    g_saved.back().bytes.resize(4);
    std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(0x006F0FB8), 4);
    auto* standIns = new StandIns;
    StandInLookAtCalls(*standIns);
    static uint8_t manager[0x40];
    GameVariable<uint8_t*>(0x006F0FB8) = manager;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        g_seed = 0xC0FFEE;
        g_noSkeleton = false;

        // The vector clamps, the Euler angles <-> quaternions, the turn about z
        for (int k = 0; k < 60; ++k) {
            Vector v = {Random(-4, 4), Random(-4, 4), Random(-4, 4), Random(-4, 4)};
            Vector min = {Random(-3, 0), Random(-3, 0), Random(-3, 0), Random(-3, 0)};
            Vector max = {Random(0, 3), Random(0, 3), Random(0, 3), Random(0, 3)};
            if (k == 5)
                v.y = std::numeric_limits<float>::quiet_NaN();
            if (k == 6)
                max.x = min.x - 1.0f; // (crossed limits)
            Vector a = v;
            Vector* r = original ? Original<Vector*(__thiscall*)(This, const Vector*, const Vector*)>(0x00487E00)(&a, &min,
                                                                                                                &max)
                                 : static_cast<Gfx::SClampVector&>(a).Clamp(&min, &max);
            Record({r == &a ? 1u : 0u});
            RecordFloats(&a.x, 4);
            Vector out = {9, 9, 9, 9};
            r = original ? Original<Vector*(__cdecl*)(Vector*, const Vector*, const Vector*, const Vector*)>(0x00487EE0)(
                               &out, &v, &min, &max)
                         : Gfx::ClampedVector(&out, &v, &min, &max);
            Record({r == &out ? 1u : 0u});
            RecordFloats(&out.x, 4);
            Vector angles = {Random(-3.5f, 3.5f), Random(-1.6f, 1.6f), Random(-3.5f, 3.5f), 7.0f};
            Quat q = {1, 2, 3, 4};
            Quat* qr = original ? Original<Quat*(__cdecl*)(Quat*, const Vector*)>(0x00487F40)(&q, &angles)
                                : Gfx::EulerToQuat(&q, &angles);
            Record({qr == &q ? 1u : 0u});
            RecordFloats(&q.x, 4);
            Quat from = k % 7 == 3 ? Quat{0, 0, 0, 1} : RandomQuat();
            Vector e = {5, 5, 5, 5};
            Vector* er = original ? Original<Vector*(__cdecl*)(Vector*, const Quat*)>(0x00488020)(&e, &from)
                                  : Gfx::QuatToEuler(&e, &from);
            Record({er == &e ? 1u : 0u});
            RecordFloats(&e.x, 4);
            Vector z = v;
            float angle = Random(-7, 7);
            Vector* zr = original ? Original<Vector*(__thiscall*)(This, float)>(0x004882D0)(&z, angle)
                                  : &z.RotateAboutZ(angle);
            Record({zr == &z ? 1u : 0u});
            RecordFloats(&z.x, 4);
        }

        // The look-at
        auto* lookAt = reinterpret_cast<CBoneLookAt*>(g_lookAt);
        std::memset(g_lookAt, 0xA5, sizeof(g_lookAt));
        std::memset(g_owner, 0, sizeof(g_owner));
        *reinterpret_cast<uint8_t**>(g_owner + 0x64) = g_object;
        auto dump = [&] { RecordFloats(reinterpret_cast<float*>(g_lookAt), sizeof(g_lookAt) / 4); };
        // Made with an owner, then without
        CBoneLookAt* made = original ? Original<CBoneLookAt*(__thiscall*)(This, void*, void*)>(0x00488300)(lookAt, g_owner,
                                                                                                       g_targets[0])
                                     : lookAt->Construct(g_owner, g_targets[0]);
        Record({made == lookAt ? 1u : 0u});
        dump();
        std::memset(g_lookAt, 0x5A, sizeof(g_lookAt));
        made = original ? Original<CBoneLookAt*(__thiscall*)(This, void*, void*)>(0x00488300)(lookAt, nullptr, g_object)
                        : lookAt->Construct(nullptr, g_object);
        dump();

        // Configured: the settings (bone 0), then by member functions on bones by index / id / all
        static Settings settings[6];
        for (auto& s : settings)
            s = Settings();
        settings[0].vectors[0] = {0, 10.0f, 20.0f, -5.0f};   // min_angles (degrees)
        settings[0].vectors[1] = {0, 30.0f, 45.0f, 60.0f};   // max_angles
        settings[0].p.Checksum(0xCAB94088, 0xB2).Checksum(0x4ACEE3F7, 0x0B1).Integer(0x7AF07905, 2)
            .Float(0xF832AABE, 0.75f).Vector(0x0B405328, &settings[0].vectors[0]).Vector(0x0A4CC99C, &settings[0].vectors[1])
            .Flag(0x0C5F37CD);
        if (original)
            Original<void(__thiscall*)(This, Script::CStruct*)>(0x00488B20)(lookAt, settings[0].p.Struct());
        else
            lookAt->InitFromStructure(settings[0].p.Struct());
        dump();
        auto member = [&](uint32_t name, Params& p) {
            int r = original ? Original<int(__thiscall*)(This, uint32_t, Script::CStruct*, void*)>(0x00488B40)(
                                   lookAt, name, p.Struct(), nullptr)
                             : lookAt->CallMemberFunction(name, p.Struct(), nullptr);
            Record({0xB40, name, static_cast<uint32_t>(r)});
            dump();
        };
        {
            Params all0;
            all0.Flag(0xC4E78E22);
            member(0x027B2B1C, all0); // (bones without a name don't count)
            static Script::CVector point = {0, 4.0f, 5.0f, 6.0f};
            Params both; // (a point, then an offset: following again)
            both.Integer(0x7F8C98FE, 1).Vector(0xDC741BAD, &point).Vector(0x1F07CF31, &point);
            member(0xB040E943, both);
        }
        // bone 1: a fixed point, axis -1, no clamp; bone 2: following object 2 with an offset, axis 3; bone 3: axis
        // -2 and limits for all
        settings[1].vectors[0] = {0, 100.0f, 50.0f, -20.0f};
        settings[1].p.Integer(0x7F8C98FE, 1).Checksum(0xCAB94088, 0xB5).Vector(0xDC741BAD, &settings[1].vectors[0])
            .Integer(0x7AF07905, -1).Flag(0xD4A8CD5F).Flag(0x0C5F37CD);
        member(0xB040E943, settings[1].p);
        settings[2].vectors[0] = {0, 1.0f, 2.0f, 3.0f};
        settings[2].p.Integer(0x7F8C98FE, 2).Checksum(0xCAB94088, 0xB7).Checksum(0x4ACEE3F7, 0x0B2)
            .Vector(0x1F07CF31, &settings[2].vectors[0]).Integer(0x7AF07905, 3).Float(0xF832AABE, 1.5f);
        member(0xB040E943, settings[2].p);
        settings[3].vectors[0] = {0, -30.0f, -40.0f, -50.0f};
        settings[3].vectors[1] = {0, 30.0f, 40.0f, 50.0f};
        settings[3].p.Integer(0x7F8C98FE, 3).Checksum(0xCAB94088, 0xB3).Integer(0x7AF07905, -2);
        member(0xB040E943, settings[3].p);
        settings[4].p.Flag(0xC4E78E22).Vector(0x0B405328, &settings[3].vectors[0])
            .Vector(0x0A4CC99C, &settings[3].vectors[1]).Flag(0x0C5F37CD);
        member(0xB040E943, settings[4].p);
        Params byId;
        byId.Checksum(0x40C698AF, 0xB3);
        Params byIndex;
        byIndex.Integer(0x7F8C98FE, 2);
        Params all;
        all.Flag(0xC4E78E22);
        Params none;
        member(0x027B2B1C, all);
        member(0x4599FB80, byId);
        member(0x027B2B1C, byId);
        member(0x027B2B1C, all);
        member(0x20D24E9A, byIndex);
        member(0x4599FB80, all);
        member(0x027B2B1C, byIndex);
        member(0x20D24E9A, all);
        member(0x027B2B1C, all);
        member(0x4599FB80, none);
        member(0x027B2B1C, none);
        member(0x20D24E9A, none);
        member(0x12345678, none);
        member(0x20D24E9A, byId);
        // Configure directly: off for all, a bone's settings
        settings[5].p.Flag(0xB0BAB2AE).Float(0xF832AABE, 2.0f);
        for (bool everyOne : {false, true}) {
            if (original)
                Original<void(__thiscall*)(This, bool, int, Script::CStruct*)>(0x00488950)(lookAt, everyOne, 1,
                                                                                           settings[5].p.Struct());
            else
                lookAt->Configure(everyOne, 1, settings[5].p.Struct());
            dump();
            member(0x20D24E9A, all);
        }
        // The member functions' helpers
        for (uint32_t bone : {0xB3u, 0xB7u, 0xB9u, 0xFFFFFFFFu}) {
            int r = original ? Original<int(__thiscall*)(This, uint32_t)>(0x00487D60)(lookAt, bone) : lookAt->FindBone(bone);
            Record({0xD60, static_cast<uint32_t>(r)});
        }
        uint32_t limit[4] = {Bits(0.5f), Bits(-0.5f), Bits(0.25f), Bits(2.0f)};
        if (original) {
            Original<void(__thiscall*)(This, const void*)>(0x00488170)(lookAt, limit);
            Original<void(__thiscall*)(This, const void*)>(0x00488220)(lookAt, limit);
            Original<void(__thiscall*)(This)>(0x00487DE0)(lookAt);
        } else {
            lookAt->SetAllMin(reinterpret_cast<const Vector*>(limit));
            lookAt->SetAllMax(reinterpret_cast<const Vector*>(limit));
            lookAt->DeactivateAll();
        }
        bool any = original ? Original<bool(__thiscall*)(This)>(0x00487D90)(lookAt) : lookAt->AllActive();
        Record({any ? 1u : 0u});
        if (original)
            Original<void(__thiscall*)(This)>(0x00487DC0)(lookAt);
        else
            lookAt->ActivateAll();
        any = original ? Original<bool(__thiscall*)(This)>(0x00487D90)(lookAt) : lookAt->AllActive();
        Record({any ? 1u : 0u});
        dump();

        // Updated and applied, many times: targets moving, the object turning, random limits, weights and axes
        member(0xB040E943, settings[0].p);
        member(0xB040E943, settings[1].p);
        member(0xB040E943, settings[2].p);
        member(0xB040E943, settings[3].p);
        for (int round = 0; round < 80; ++round) {
            std::memset(g_object, 0, sizeof(g_object));
            std::memset(g_skelComp, 0, sizeof(g_skelComp));
            *reinterpret_cast<uint8_t**>(g_skelComp + 0x18) = g_skeleton;
            auto* object = reinterpret_cast<float*>(g_object);
            for (int i = 0; i < 4; ++i)
                object[0x4C / 4 + i] = Random(-50, 50);
            Quat facing = RandomQuat();
            Vector at = {2 * (facing.x * facing.z + facing.w * facing.y), 2 * (facing.y * facing.z - facing.w * facing.x),
                         1 - 2 * (facing.x * facing.x + facing.y * facing.y), 0};
            object[0x8C / 4] = at.x;
            object[0x90 / 4] = at.y;
            object[0x94 / 4] = at.z;
            for (auto& target : g_targets) {
                std::memset(target, 0, sizeof(target));
                for (int i = 0; i < 4; ++i)
                    reinterpret_cast<float*>(target)[0x4C / 4 + i] = Random(-60, 60);
            }
            for (Quat& q : g_pose)
                q = RandomQuat();
            for (int b = 0; b < 4; ++b) {
                CBoneLookAt::SBone& bone = lookAt->m_bones[b];
                if (round % 4 == 1) {
                    int axis = RandomInt(-5, 5);
                    bone.m_axis = axis == 0 ? 3 : axis;
                    bone.m_clamp = RandomInt(0, 1) != 0;
                    bone.m_weight = Random(-1, 2);
                    bone.m_min = {Random(-2, 0), Random(-2, 0), Random(-2, 0), Random(-2, 0)};
                    bone.m_max = {Random(0, 2), Random(0, 2), Random(0, 2), Random(0, 2)};
                }
                if (round % 9 == 4)
                    bone.m_rotation = RandomQuat();
            }
            if (round == 50)
                lookAt->m_bones[3].m_bone = 0xB9; // (a bone the skeleton hasn't)
            if (round == 30)
                lookAt->m_bones[1].m_targetPos = {object[0x4C / 4], object[0x50 / 4], object[0x54 / 4], 1}; // (no distance)
            g_noSkeleton = false;
            bool r = original ? Original<bool(__thiscall*)(This, Quat*)>(0x00488410)(lookAt, g_pose)
                              : lookAt->Apply(g_pose);
            Record({0x8410, r ? 1u : 0u});
            RecordFloats(&g_pose[0].x, 32);
            dump();
            if (round % 10 == 7) {
                for (auto& bone : lookAt->m_bones)
                    bone.m_targetPos = {Random(-9, 9), Random(-9, 9), Random(-9, 9), Random(-9, 9)};
                if (original)
                    Original<void(__thiscall*)(This)>(0x00488100)(lookAt);
                else
                    lookAt->Update();
                dump();
            }
        }

        // Destroyed (kept, then freed)
        for (unsigned flags : {0u, 1u}) {
            CBoneLookAt* r = original ? Original<CBoneLookAt*(__thiscall*)(This, unsigned)>(0x004880D0)(lookAt, flags)
                                      : lookAt->Destroy(flags);
            Record({r == lookAt ? 1u : 0u, reinterpret_cast<uint32_t>(lookAt->vtable)});
        }
        if (original)
            Original<void(__thiscall*)(This)>(0x00487D40)(lookAt);
        else
            lookAt->DestroyBody();
        Record({reinterpret_cast<uint32_t>(lookAt->vtable)});
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 15000, "bone look-at: the original reached its calls");
    Compare(traces[0], traces[1], "the bones' look-at and the vector helpers (0x00487D40-0x00488DA1)");
}

} // namespace

SELFTEST(Early, TestBoneLookAt, Restore);
