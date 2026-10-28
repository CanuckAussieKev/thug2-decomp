// Self-tests for skeletons (src/game/gfx/skeleton.cpp, RND5-5): the original and ours over skeleton assets loaded
// from made-up .ske data (random hierarchies, rotations, translations, mirror bones, LOD masks) and the skeletons
// made from them, driven at random - scales, activity, LOD, bone matrices and positions, updates from poses, the
// collapse of bones scaled to nothing, copies, the debug drawing. D3DXMatrixMultiply (a library function) stood in by
// a double-precision product; the script array, vector and integer getters stood in; the arena compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/anim_blend.h"
#include "game/gfx/skeleton.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;

// The script array 0x20D9AC2F's elements (none: no array).
std::vector<uint32_t> g_exceptions;
bool g_noArray;
uint32_t g_fakeArray[4];

void StandInSkeletonCalls(StandIns& s)
{
    StandInMemory(s);
    StandInScriptStructure(s);
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        float product[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                product[i * 4 + j] = static_cast<float>(sum);
            }
        }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), product, sizeof(product));
        Record({0xFA8C});
        return r.args[0];
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return g_noArray ? 0u : reinterpret_cast<uint32_t>(g_fakeArray);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        return r.args[0] < g_exceptions.size() ? g_exceptions[r.args[0]] : 0u;
    });
    s.At({0x00476FB0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int)>(
                          &Script::CStruct::GetVector))},
         12, [](Regs& r) {
             uint32_t name = r.args[0];
             Record({0x6FB0, name});
             if ((name & 3) == 0)
                 return 0u;
             auto* out = reinterpret_cast<float*>(r.args[1]);
             out[0] = static_cast<float>(name & 0xFF) / 37.0f;
             out[1] = (name & 0x100) ? 0.0f : 1.5f;
             out[2] = static_cast<float>((name >> 9) & 0x7F) / 11.0f;
             out[3] = 1.0f;
             return 1u;
         });
}

void RandomQuat(Lcg& random, float* q)
{
    double v[4], length = 0;
    for (double& c : v) {
        c = RandomFloat(random, -1, 1);
        length += c * c;
    }
    length = std::sqrt(length) + 1e-6;
    for (int i = 0; i < 4; ++i)
        q[i] = static_cast<float>(v[i] / length);
}

// A .ske image: `count` bones, random parents (earlier bones; sometimes none or unknown), mirrors.
std::vector<uint32_t> MakeSke(Lcg& random, int count, std::vector<uint32_t>& names)
{
    names.clear();
    for (int i = 0; i < count; ++i)
        names.push_back(0x1000u * (i + 1) + random.Below(0x400) * 4 + (random.Below(3) ? 1u : 0u));
    if (count > 3 && random.Below(2))
        names[random.Below(count)] = 0x9F173EEB; // (the bone GetBoneMatrix turns by)
    if (count > 3 && random.Below(2))
        names[random.Below(count)] = 0x580C0963;
    std::vector<uint32_t> ske = {0x5BE, random.Below(2), static_cast<uint32_t>(count)};
    ske.insert(ske.end(), names.begin(), names.end());
    for (int i = 0; i < count; ++i)
        ske.push_back(i == 0 ? 0u : random.Below(8) == 0 ? 0xBAD : names[random.Below(i)]);
    for (int i = 0; i < count; ++i)
        ske.push_back(random.Below(3) == 0 ? names[random.Below(count)] : random.Below(6) == 0 ? 0x777u : 0u);
    for (int i = 0; i < count; ++i) {
        float q[4];
        RandomQuat(random, q);
        for (float f : q)
            ske.push_back(std::bit_cast<uint32_t>(f));
        for (int k = 0; k < 3; ++k)
            ske.push_back(std::bit_cast<uint32_t>(RandomFloat(random, -40, 40)));
        ske.push_back(std::bit_cast<uint32_t>(random.Below(4) ? 1.0f : RandomFloat(random, 0, 2)));
    }
    return ske;
}

// The original's 0x004A2F20 or our stub: EAX out, EDX translation, BL swapZ; the quaternion and mirror on the stack.
void CallBoneMatrix(void* code, Mth::Matrix* target, const Mth::Vector* translation, const float* quat, int mirror,
                    int swapZ)
{
    __asm {
        push ebx
        mov eax, target
        mov edx, translation
        mov ebx, swapZ
        push mirror
        push quat
        call code
        add esp, 8
        pop ebx
    }
}

Mth::Vector ScaleValue(Lcg& random)
{
    switch (random.Below(5)) {
    case 0:
        return {1.0f, 1.0f, 1.0f, RandomFloat(random, 0, 2)};
    case 1:
        return {0.0f, 0.0f, 0.0f, 1.0f};
    case 2:
        return random.Below(2) ? Mth::Vector{1.0f, 1.0f, 0.0f, 1.0f} : Mth::Vector{0.0f, 0.0f, 1.5f, 1.0f};
    default:
        return {RandomFloat(random, 0.2f, 2), RandomFloat(random, 0.2f, 2), RandomFloat(random, 0.2f, 2),
                RandomFloat(random, 0.2f, 2)};
    }
}

void TestSkeletons()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}});
    StandIns& s = *new StandIns();
    StandInSkeletonCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("skeletons", reported, [&](bool original) {
            g_trace.clear();
            g_answers.clear();
            ResetArena(0xEE);
            Lcg random(0x5CE0 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            GameVariable<uint8_t>(0x007CC979) = 1;
            g_noArray = random.Below(4) == 0;
            g_fakeArray[1] = 3;
            int count = 1 + static_cast<int>(random.Below(round % 3 == 0 ? 3 : 12));
            std::vector<uint32_t> names;
            std::vector<uint32_t> ske = MakeSke(random, count, names);
            g_exceptions = {names[random.Below(count)], 0x55, random.Below(2) ? names[random.Below(count)] : 0x66};
            // The asset.
            auto* data = reinterpret_cast<Gfx::CSkeletonData*>(Bump(sizeof(Gfx::CSkeletonData)));
            Record({Relocate(At<uint32_t(__thiscall*)(void*)>(original, 0x004A2C20)(data))});
            for (uint32_t i = 0; i < 0x598; i += 4)
                Record({*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(data) + i)});
            Record({At<uint32_t(__thiscall*)(void*, const uint32_t*, uint32_t, int)>(original, 0x004A39C0)(
                        data, ske.data(), 0, 1) & 0xFF});
            for (int i = 0; i < 64; ++i)
                data->m_lodMasks[i] = random.Below(3) ? 0 : random.Next();
            float distance = 0;
            for (float& d : data->m_lodDistances)
                d = distance += RandomFloat(random, 1, 50);
            for (int i = 0; i < count + 1; ++i) {
                uint32_t name = random.Below(4) ? names[random.Below(count)] : 0x1234;
                Record({At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A2C50)(data, i),
                        At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A2C60)(data, i),
                        At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004A2C70)(data, name),
                        At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A30D0)(data, i),
                        At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x004A3100)(
                            data, random.Below(count + 1), random.Below(count)) & 0xFF});
            }
            Record({Relocate(At<uint32_t(__thiscall*)(void*)>(original, 0x004A2CA0)(data))});
            // Skeletons.
            auto* skeleton = reinterpret_cast<Gfx::CSkeleton*>(Bump(sizeof(Gfx::CSkeleton)));
            std::memset(skeleton, 0xAB, sizeof(*skeleton));
            Record({Relocate(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x004A3D30)(skeleton, data))});
            auto* other = reinterpret_cast<Gfx::CSkeleton*>(Bump(sizeof(Gfx::CSkeleton)));
            At<uint32_t(__thiscall*)(void*, void*)>(original, 0x004A3D30)(other, data);
            if (random.Below(2))
                skeleton->m_flags |= Gfx::CSkeleton::kCollapseZeroScaled;
            skeleton->m_lod = random.Below(6);
            skeleton->m_maxLod = random.Below(6);
            auto* pose = reinterpret_cast<Gfx::CPose*>(Bump(sizeof(Gfx::CPose)));
            for (int step = 0; step < 30; ++step) {
                uint32_t name = random.Below(5) ? names[random.Below(count)] : 0x4321;
                switch (random.Below(15)) {
                case 0:
                    Record({static_cast<uint32_t>(At<int(__thiscall*)(void*, uint32_t)>(original, 0x004A2B60)(
                                skeleton, name)),
                            Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A2B90)(
                                skeleton, static_cast<int>(random.Below(count)))),
                            At<uint32_t(__thiscall*)(void*)>(original, 0x004A2BA0)(skeleton)});
                    break;
                case 1:
                    Record({At<uint32_t(__thiscall*)(void*, uint32_t, int)>(original, 0x004A2BB0)(
                                skeleton, name, static_cast<int>(random.Below(3))) & 0xFF});
                    break;
                case 2:
                    At<void(__thiscall*)(void*)>(original, 0x004A2CE0)(skeleton);
                    break;
                case 3: {
                    if (count > 1 && random.Below(3) == 0) { // (a bone scaled to nothing, then an update)
                        Mth::Vector zero = {0.0f, 0.0f, 0.0f, 1.0f};
                        At<uint32_t(__thiscall*)(void*, uint32_t, const Mth::Vector*, int)>(original, 0x004A2D80)(
                            skeleton, names[1 + random.Below(count - 1)], &zero, 0);
                        At<void(__thiscall*)(void*, const void*)>(original, 0x004A44E0)(skeleton, pose);
                        break;
                    }
                    Mth::Vector scale = ScaleValue(random);
                    Record({At<uint32_t(__thiscall*)(void*, uint32_t, const Mth::Vector*, int)>(original, 0x004A2D80)(
                                random.Below(3) ? skeleton : other, name, &scale,
                                static_cast<int>(random.Below(3))) & 0xFF});
                    Mth::Vector out = {7, 7, 7, 7};
                    Record({At<uint32_t(__thiscall*)(void*, uint32_t, Mth::Vector*)>(original, 0x004A2D20)(
                                skeleton, name, &out) & 0xFF,
                            std::bit_cast<uint32_t>(out.x), std::bit_cast<uint32_t>(out.w)});
                    break;
                }
                case 4:
                    At<void(__thiscall*)(void*, void*, int)>(original, 0x004A2E20)(skeleton, other,
                                                                                  static_cast<int>(random.Below(2)));
                    break;
                case 5: {
                    g_answers = {random.Below(2), random.Below(3)};
                    void* params = random.Below(6) ? reinterpret_cast<void*>(Bump(0x10)) : nullptr;
                    Record({At<uint32_t(__thiscall*)(void*, void*)>(original, 0x004A3570)(skeleton, params) & 0xFF});
                    break;
                }
                case 6:
                    At<void(__thiscall*)(void*, float)>(original, 0x004A3090)(
                        skeleton, random.Below(3) ? RandomFloat(random, 0, 300)
                                  : random.Below(2) ? data->m_lodDistances[random.Below(6)] // (exactly at one)
                                                    : AwkwardFloat(random, -1, 1));
                    if (random.Below(4) == 0)
                        At<void(__thiscall*)(void*, void*)>(original, 0x004A3150)(skeleton, data);
                    break;
                case 7: {
                    Mth::Matrix out;
                    std::memset(&out, 0x5A, sizeof(out));
                    Record({At<uint32_t(__thiscall*)(void*, uint32_t, Mth::Matrix*)>(original, 0x004A36E0)(
                                skeleton, name, &out) & 0xFF});
                    for (int k = 0; k < 4; ++k)
                        Record({std::bit_cast<uint32_t>(out.row[k].x), std::bit_cast<uint32_t>(out.row[k].w)});
                    Mth::Vector at = {9, 9, 9, 9};
                    Record({At<uint32_t(__thiscall*)(void*, uint32_t, Mth::Vector*)>(original, 0x004A3870)(
                                skeleton, name, &at) & 0xFF,
                            std::bit_cast<uint32_t>(at.x), std::bit_cast<uint32_t>(at.y), std::bit_cast<uint32_t>(at.z),
                            std::bit_cast<uint32_t>(at.w)});
                    break;
                }
                case 8: {
                    Mth::Matrix world;
                    for (int k = 0; k < 4; ++k)
                        world.row[k] = {RandomFloat(random, -2, 2), RandomFloat(random, -2, 2),
                                        RandomFloat(random, -2, 2), RandomFloat(random, -2, 2)};
                    At<void(__thiscall*)(void*, const Mth::Matrix*, int, int, int)>(original, 0x004A31E0)(
                        skeleton, &world, 1, 2, 3);
                    break;
                }
                case 9:
                case 10:
                case 11:
                    for (int i = 0; i < 64; ++i) {
                        RandomQuat(random, pose->rotations[i]);
                        for (int k = 0; k < 4; ++k)
                            pose->translations[i][k] = RandomFloat(random, -30, 30);
                    }
                    if (random.Below(2))
                        At<void(__thiscall*)(void*, const void*)>(original, 0x004A44E0)(skeleton, pose);
                    else
                        At<void(__thiscall*)(void*, const void*, const void*)>(original, 0x004A4240)(
                            skeleton, pose->rotations, pose->translations);
                    break;
                case 12: {
                    Mth::Matrix out;
                    float q[4];
                    RandomQuat(random, q);
                    if (random.Below(4) == 0)
                        q[random.Below(4)] = AwkwardFloat(random, -1, 1);
                    Mth::Vector t = {RandomFloat(random, -5, 5), RandomFloat(random, -5, 5), RandomFloat(random, -5, 5),
                                     9.0f};
                    void* code = original ? OriginalFunction(0x004A2F20)
                                          : reinterpret_cast<void*>(&Gfx::QuatToBoneMatrixStub);
                    CallBoneMatrix(code, &out, &t, q, static_cast<int>(random.Below(2)) | 0x100,
                                   static_cast<int>(random.Below(2)) | 0x7700);
                    for (int k = 0; k < 4; ++k)
                        Record({std::bit_cast<uint32_t>(out.row[k].x), std::bit_cast<uint32_t>(out.row[k].y),
                                std::bit_cast<uint32_t>(out.row[k].z), std::bit_cast<uint32_t>(out.row[k].w)});
                    break;
                }
                case 13:
                    At<void(__thiscall*)(void*)>(original, 0x004A4000)(skeleton);
                    break;
                default: {
                    auto* copy = reinterpret_cast<Gfx::CSkeleton*>(Bump(sizeof(Gfx::CSkeleton)));
                    std::memset(copy, 0xCD, sizeof(*copy));
                    Record({Relocate(At<uint32_t(__thiscall*)(void*, const void*)>(original, 0x004A3E80)(copy,
                                                                                                   skeleton))});
                    if (random.Below(2))
                        At<void(__thiscall*)(void*)>(original, 0x004A2CB0)(copy);
                    else
                        skeleton = copy;
                    break;
                }
                }
            }
            Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A3950)(
                data, static_cast<int>(random.Below(4))))});
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "skeletons: assets loaded (hierarchy, inverse binds, mirrors), skeletons made / copied, scales, "
              "activity, LOD, bone matrices and positions, pose updates, collapses, debug drawing");
}

} // namespace

SELFTEST(Early, TestSkeletons, RemoveStandIns);
