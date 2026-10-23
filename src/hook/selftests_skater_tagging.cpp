// Self-tests for the tagging and tagging control components (game/skater/tagging.cpp): the originals against ours,
// every call recorded by stand-ins (hook/standins.h) - the feeler's collision query scripted.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/obj/skeleton_component.h"
#include "game/obj/trigger_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/tagging.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CTaggingComponent;
using Obj::CTaggingControlComponent;

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

alignas(16) uint8_t g_tagging[0x40];
alignas(16) uint8_t g_control[0x60];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_physics[0x40];      // physics control (+0x2C state)
alignas(16) uint8_t g_state[0x200];
alignas(16) uint8_t g_core[0x1600];
alignas(16) uint8_t g_coreFlags[0x40];
alignas(16) uint8_t g_skeleton[0x20];
alignas(16) uint8_t g_walk[0x80];         // +0x18 state, +0x48 heading
alignas(16) uint8_t g_input[0x400];       // +0x1A0 the tag button
alignas(16) uint8_t g_trigger[0x20];
alignas(16) uint8_t g_score[0x20];        // the score component (+0x18: g_scoreData)
alignas(16) uint8_t g_scoreData[0x80];
alignas(16) uint8_t g_textures[8][0x20];
alignas(16) uint8_t g_structs[8][0x10];
alignas(16) uint8_t g_block[2][0x60];
alignas(16) uint8_t g_netManager[0x40];
alignas(16) uint8_t g_client[0x40];

int g_nextTexture, g_nextStruct, g_blockNext, g_rnd, g_pressedTime, g_hitsLeft;
bool g_found, g_inNetGame;
uint32_t g_hitPattern;
float g_settings[4];
Mth::Vector g_bone;

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
    for (auto r : {in(g_textures, sizeof(g_textures), sizeof(g_textures[0]), 0x81000000),
                   in(g_structs, sizeof(g_structs), sizeof(g_structs[0]), 0x82000000),
                   in(g_object, sizeof(g_object), sizeof(g_object), 0x83000000),
                   in(g_tagging, sizeof(g_tagging), sizeof(g_tagging), 0x84000000),
                   in(g_block, sizeof(g_block), sizeof(g_block[0]), 0x85000000),
                   in(g_trigger, sizeof(g_trigger), sizeof(g_trigger), 0x86000000),
                   in(g_client, sizeof(g_client), sizeof(g_client), 0x87000000),
                   in(g_walk, sizeof(g_walk), sizeof(g_walk), 0x88000000),
                   in(g_skeleton, sizeof(g_skeleton), sizeof(g_skeleton), 0x89000000)})
        if (r)
            return r;
    return 0xBAD00000;
}

float __cdecl StandInFloat(uint32_t name)
{
    Record({0xF10A, name});
    return g_settings[(name >> 3) & 3];
}

float __cdecl StandInTagSetting(uint32_t name)
{
    Record({0x7A65, name});
    return g_settings[(name >> 5) & 3];
}

void RecordVector(const void* v)
{
    const uint32_t* w = static_cast<const uint32_t*>(v);
    Record({w[0], w[1], w[2], w[3]});
}

void StandInAll(StandIns& s)
{
    using Script::CStruct;
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInFloat)));
    s.Bytes(0x0043DA70, Branch(0xE9, 0x0043DA70, reinterpret_cast<void*>(&StandInTagSetting)));
    // Textures.
    s.At({0x0049F9B0}, 0x10, [](Regs& r) {
        Record({0x49F9, Id(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3]});
        return reinterpret_cast<uint32_t>(g_textures[g_nextTexture++ & 7]);
    });
    s.At({0x0049FBE0}, 8, [](Regs& r) {
        Record({0x49FB, r.args[0], Id(r.args[1])});
        return reinterpret_cast<uint32_t>(g_textures[g_nextTexture++ & 7]);
    });
    s.At({0x0049F540}, 4, [](Regs& r) { Record({0x49F5, r.args[0]}); return reinterpret_cast<uint32_t>(g_textures[7]); });
    s.At({0x0049F500}, 4, [](Regs& r) { Record({0x49F0, Id(r.args[0])}); return 0u; });
    s.At({0x0049F3B0}, 0, [](Regs& r) { Record({0x49F3, Id(r.ecx)}); return 0u; });
    s.At({0x0049F3F0}, 4, [](Regs& r) { Record({0x49F4, Id(r.ecx), r.args[0] & 0xFF}); return 0u; });
    s.At({0x004916F0}, 0, [](Regs& r) { Record({0x4916, r.args[0]}); return 0u; });
    s.At({0x00487D50}, 4, [](Regs& r) { Record({0x487D, Id(r.ecx), Id(r.args[0])}); return 0u; });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) { Record({0x9054, r.args[0]}); return 0u; });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) { Record({0x90B}); return 0u; });
    s.At({0x006234FE}, 0, StandInSprintf);
    // Script.
    auto newStruct = [](Regs&) {
        Record({0x4E5});
        return reinterpret_cast<uint32_t>(g_structs[g_nextStruct++ & 7]);
    };
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, newStruct);
    s.At({0x005BA3D0}, 0, newStruct);
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) { Record({0x1417, Id(r.ecx)}); return r.ecx; });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0xC1EA, Id(r.ecx)}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0xDE1, Id(r.args[0])}); return 0u; });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Record({0x6F17, Id(r.ecx), r.args[0], r.args[2]});
             if (g_found)
                 *reinterpret_cast<int*>(r.args[1]) = 3;
             return static_cast<uint32_t>(g_found);
         });
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             Record({0x6F57, Id(r.ecx), r.args[0], r.args[2]});
             if (g_found)
                 *reinterpret_cast<const char**>(r.args[1]) = "tags\\custom";
             return static_cast<uint32_t>(g_found);
         });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x6F10, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<float*>(r.args[1]) = g_settings[(r.args[0] >> 5) & 3] * 10.0f;
             return 1u;
         });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x6E57, r.args[0]});
        return reinterpret_cast<uint32_t>(g_structs[6]);
    });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0xADDF, Id(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5C41, r.args[0], Id(r.args[1]), Id(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) { Record({0xDB6, r.args[0]}); return 0u; });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        return static_cast<uint32_t>(g_rnd);
    });
    // The decal.
    s.At({0x004928A0}, 0, [](Regs& r) {
        Record({0x4928});
        RecordVector(reinterpret_cast<const void*>(r.args[0]));
        RecordVector(reinterpret_cast<const void*>(r.args[1]));
        for (int i = 2; i < 11; ++i)
            Record({r.args[i]});
        Record({Id(r.args[11])});
        return 1u;
    });
    // The network.
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs&) {
        Record({0x44E4});
        return static_cast<uint32_t>(g_inNetGame);
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* msg = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
        const uint8_t* data = static_cast<const uint8_t*>(msg->m_data);
        Record({0x459E, Id(r.ecx), msg->m_id, msg->m_length, static_cast<uint32_t>(msg->m_queue), msg->m_groupId, data[0],
                data[1]}); // (bytes 2 and 3 are left over on the original's stack)
        const uint32_t* w = reinterpret_cast<const uint32_t*>(data + 4);
        for (int i = 0; i < 9; ++i)
            Record({w[i]});
        return 0u;
    });
    // The skater.
    s.At({0x00432050, AddressOf(&Obj::CSkeletonComponent::GetBonePosition)}, 8, [](Regs& r) {
        Record({0x4320, Id(r.ecx), r.args[0]});
        *reinterpret_cast<Mth::Vector*>(r.args[1]) = g_bone;
        return 1u;
    });
    s.At({0x0046B920, AddressOf(&Obj::CControlButton::GetPressedTime)}, 0, [](Regs& r) {
        Record({0x46B9, r.ecx - reinterpret_cast<uint32_t>(g_input)});
        return static_cast<uint32_t>(g_pressedTime);
    });
    s.At({0x00434BF0, AddressOf(&Obj::CTriggerComponent::CheckFeeler)}, 8, [](Regs& r) {
        Record({0x434B, Id(r.ecx), r.args[0]});
        return 0u;
    });
    // The feeler's query: the line recorded, a hit made up.
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        Record({0x4172, r.args[0] & 0xFF, r.args[1] & 0xFF, feeler->m_hit.ignoreFlags, feeler->m_hit.requireFlags});
        RecordVector(&feeler->m_line.start);
        RecordVector(&feeler->m_line.end);
        bool hit = (g_hitPattern & 1) != 0;
        g_hitPattern = (g_hitPattern >> 1) | (g_hitPattern << 31);
        if (hit) {
            const Mth::Vector& a = feeler->m_line.start;
            const Mth::Vector& b = feeler->m_line.end;
            float t = 0.25f + 0.0625f * static_cast<float>(g_hitsLeft++ % 3); // (equally near walls now and then)
            feeler->m_point = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f};
            float nx = 0.31f * static_cast<float>(g_hitsLeft % 7) - 0.9f, nz = 0.17f * static_cast<float>(g_hitsLeft % 11) - 0.8f;
            feeler->m_normal = {nx, 0.123f * static_cast<float>(g_hitsLeft % 5), nz, 0.07f * static_cast<float>(g_hitsLeft % 4)};
            feeler->m_distance = (g_hitsLeft % 4 == 0 ? -1.0f : 1.0f) * t * 37.0f;
        }
        return static_cast<uint32_t>(hit);
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA11, r.args[0]});
        return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) { Record({0xF5EE, Id(r.args[0])}); return 0u; });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD7, Id(r.ecx), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
        return 0u;
    });
}

void Link(uint8_t* object, std::initializer_list<std::pair<uint8_t*, uint32_t>> parts)
{
    uint8_t* next = nullptr;
    for (auto part : parts) {
        At<uint32_t>(part.first, 4) = part.second;
        At<uint8_t*>(part.first, 0x10) = next;
        next = part.first;
    }
    At<uint8_t*>(object, 0x100) = next;
}

void Build(Random& r)
{
    for (size_t i = 0; i < sizeof(g_tagging); ++i)
        g_tagging[i] = static_cast<uint8_t>(r.Next());
    for (size_t i = 0; i < sizeof(g_control); ++i)
        g_control[i] = static_cast<uint8_t>(r.Next());
    std::memset(g_object, 0, sizeof(g_object));
    auto* tagging = reinterpret_cast<CTaggingComponent*>(g_tagging);
    auto* control = reinterpret_cast<CTaggingControlComponent*>(g_control);
    tagging->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    control->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    tagging->m_customTag = r.Bit();
    tagging->m_graffiti = r.OneIn(5) ? nullptr : g_textures[0];
    tagging->m_sticker = r.OneIn(5) ? nullptr : g_textures[1];
    At<uint32_t>(g_textures[0], 4) = 0x7E0;
    At<uint32_t>(g_textures[1], 4) = 0x7E1;
    control->m_walk = reinterpret_cast<Obj::CBaseComponent*>(g_walk);
    control->m_input = reinterpret_cast<Obj::CBaseComponent*>(g_input);
    control->m_corePhysics = reinterpret_cast<Obj::CBaseComponent*>(g_core);
    control->m_trigger = reinterpret_cast<Obj::CBaseComponent*>(g_trigger);
    control->m_tagging = tagging;
    control->m_countdown = r.OneIn(3) ? static_cast<int>(r.Below(5)) - 2 : 0;
    control->m_pending = r.Bit();
    control->m_blocked = r.OneIn(6);
    static const float cooldowns[] = {0.0f, -1.0f, 0.5f, 3.0f};
    control->m_cooldown = cooldowns[r.Below(4)];
    At<uint32_t>(g_object, 0x14) = 0x50 + r.Below(8);
    float pos[4] = {r.Range(-300.0f, 300.0f), r.Range(-20.0f, 20.0f), r.Range(-300.0f, 300.0f), 1.0f};
    std::memcpy(g_object + 0x4C, pos, 16);
    float vel[4] = {r.Range(-500.0f, 500.0f), r.Range(-50.0f, 50.0f), r.Range(-500.0f, 500.0f), r.Range(-1.0f, 1.0f)};
    std::memcpy(g_object + 0xAC, vel, 16);
    float up[4] = {r.Range(-1.0f, 1.0f), r.Range(0.2f, 1.0f), r.Range(-1.0f, 1.0f), r.Range(-1.0f, 1.0f)};
    std::memcpy(g_object + 0x7C, up, 16);
    At<uint32_t>(g_object, 0x158) = r.OneIn(4) ? 0xFC3A8D7F : 0;
    At<uint8_t*>(g_core, 0x0C) = g_object;
    At<uint8_t*>(g_core, 0x1528) = g_coreFlags;
    g_coreFlags[0x28] = r.Bit();
    At<uint8_t*>(g_physics, 0x2C) = g_state;
    g_state[0x118] = r.OneIn(6);
    At<int>(g_state, 0x1C) = r.OneIn(5) ? 0 : 1;
    At<int>(g_walk, 0x18) = static_cast<int>(r.Below(4)) == 3 ? 1 : static_cast<int>(r.Below(3)) * (r.Bit() ? 1 : 0);
    float heading[4] = {r.Range(-1.0f, 1.0f), 0.0f, r.Range(-1.0f, 1.0f), r.Range(0.0f, 1.0f)};
    std::memcpy(g_walk + 0x48, heading, 16);
    g_input[0x1A0] = r.Bit();
    g_pressedTime = r.OneIn(4) ? -5 : static_cast<int>(r.Below(200));
    At<uint8_t*>(g_score, 0x18) = g_scoreData;
    static const int points[] = {0, 999, 1000, 1001, 5000, 5001, 120};
    At<int>(g_scoreData, 0x14) = points[r.Below(7)];
    At<float>(g_scoreData, 0x60) = r.OneIn(3) ? 1.0f : r.Range(0.5f, 3.0f);
    Link(g_object, {{g_physics, 0xD9151D4D}, {g_core, 0x5BD63B29}, {g_skeleton, 0x222756D5}, {g_score, 0xD262802F},
                    {g_walk, 0x726E85AA}, {g_input, 0x27D7CD28}, {g_trigger, 0xE594F0A2}, {g_tagging, 0x5B512EDC}});
    g_bone = {r.Range(-300.0f, 300.0f), r.Range(0.0f, 60.0f), r.Range(-300.0f, 300.0f), r.Range(0.0f, 2.0f)};
    for (float& f : g_settings)
        f = r.Range(10.0f, 120.0f);
    if (r.OneIn(4))
        g_settings[r.Below(4)] = 0.0f;
    g_found = !r.OneIn(4);
    g_inNetGame = r.Bit();
    g_rnd = static_cast<int>(r.Below(91));
    g_hitPattern = r.Next() | (r.OneIn(4) ? 0u : r.Next() << 24);
    if (r.OneIn(6))
        g_hitPattern = 0;
    g_hitsLeft = static_cast<int>(r.Below(9));
    g_nextTexture = 2;
    g_nextStruct = 0;
    At<uint8_t*>(g_netManager, 8) = g_client;
}

void Trace()
{
    for (const uint8_t* block : {static_cast<const uint8_t*>(g_tagging), static_cast<const uint8_t*>(g_control)}) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(block);
        for (size_t i = 0; i < 0x38 / 4; ++i)
            g_trace.push_back(Id(w[i]) == 0xBAD00000 ? w[i] : Id(w[i]));
    }
}

void TestTagging()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x007CCE14, 4, {}}, {0x00700F20, 4, {}}, {0x006F3614, 4, {}},
                                {0x006F361C, 16, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int decals = 0;
    {
        auto* s = new StandIns;
        StandInAll(*s);
        alignas(16) static uint8_t memory[0x200] = {};
        At<uint32_t>(memory, 0x110) = 0x11011011;
        GameVariable<void*>(0x007D1800) = memory;
        GameVariable<void*>(0x007CCE14) = g_netManager;
        GameVariable<uint32_t>(0x00700F20) = 0x7EC7EC00;
        GameVariable<int>(0x006F3614) = 1;
        GameVariable<uint32_t>(0x006F361C + 4) = 0x12345678;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x5B512};
            for (int n = 0; n < 6000; ++n) {
                Build(r);
                auto* tagging = reinterpret_cast<CTaggingComponent*>(g_tagging);
                auto* control = reinterpret_cast<CTaggingControlComponent*>(g_control);
                int which = static_cast<int>(r.Below(9));
                Record({0xCA5E, static_cast<uint32_t>(which)});
                size_t before = g_trace.size();
                switch (which) {
                case 0:
                case 1: {
                    bool result = original ? Original<bool(__thiscall*)(void*)>(0x0051E390)(control) : control->Update();
                    Record({result});
                    break;
                }
                case 2:
                case 3: {
                    bool a = r.Bit(), b = r.Bit(), c = r.Bit();
                    bool result = original ? Original<bool(__thiscall*)(void*, bool, bool, bool)>(0x0051DDF0)(control, a, b, c)
                                           : control->SlapSticker(a, b, c);
                    Record({result});
                    break;
                }
                case 4: {
                    Mth::Vector a = {r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f), 1.0f};
                    Mth::Vector b = {r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f), r.Range(-9.0f, 9.0f), 0.5f};
                    bool sticker = r.Bit(), send = r.Bit();
                    int size = static_cast<int>(r.Below(120));
                    if (original)
                        Original<void(__thiscall*)(void*, bool, Mth::Vector, Mth::Vector, int, bool)>(0x0051DBC0)(tagging, sticker, a, b, size, send);
                    else
                        tagging->PlaceTag(sticker, a, b, size, send);
                    break;
                }
                case 5: {
                    static const uint32_t names[] = {0x89E8C244, 0x3A760118, 0xB260F511, 0xFE5E3293, 0xCD8B5E35, 0x5};
                    uint32_t name = names[r.Below(6)];
                    int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x0051E300)(control, name, nullptr, nullptr)
                                          : control->CallMemberFunction(name, nullptr, nullptr);
                    Record({static_cast<uint32_t>(result)});
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051DD90)(control);
                    else
                        control->Finalize();
                    break;
                }
                case 6: {
                    static const uint32_t names[] = {0x19504222, 0x90042D4A, 0x0164645C, 0x723DEE0A, 0x7};
                    uint32_t name = names[r.Below(5)];
                    auto* params = reinterpret_cast<Script::CStruct*>(g_structs[5]);
                    int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x0051DB00)(tagging, name, params, nullptr)
                                          : tagging->CallMemberFunction(name, params, nullptr);
                    Record({static_cast<uint32_t>(result)});
                    break;
                }
                case 7:
                    if (original) {
                        Original<void(__thiscall*)(void*, void*)>(0x0051D890)(tagging, g_structs[4]);
                        Original<void(__thiscall*)(void*)>(0x0051D830)(tagging);
                    } else {
                        tagging->InitFromStructure(reinterpret_cast<Script::CStruct*>(g_structs[4]));
                        tagging->ApplyCustomTagTexture();
                    }
                    Record({original ? Original<bool(__thiscall*)(void*)>(0x0051D850)(tagging) : tagging->HasCustomTagTexture()});
                    break;
                default: {
                    for (int kind = 0; kind < 2; ++kind) {
                        std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                        g_blockNext = r.OneIn(4) ? -1 : 0;
                        void* made;
                        if (original)
                            made = Original<void*(__cdecl*)()>(kind ? 0x0051E280 : 0x0051DA80)();
                        else
                            made = kind ? CTaggingControlComponent::Create() : CTaggingComponent::Create();
                        Record({Id(reinterpret_cast<uint32_t>(made))});
                        const uint32_t* w = reinterpret_cast<const uint32_t*>(g_block[0]);
                        g_trace.insert(g_trace.end(), w, w + 0x38 / 4);
                        if (!made)
                            continue;
                        if (!kind) { // (the textures it would give back)
                            At<void*>(g_block[0], 0x1C) = r.Bit() ? g_textures[3] : nullptr;
                            At<void*>(g_block[0], 0x20) = r.Bit() ? g_textures[4] : nullptr;
                        }
                        unsigned flags = r.Below(2);
                        void* result;
                        if (original)
                            result = Original<void*(__thiscall*)(void*, unsigned)>(kind ? 0x0051DD60 : 0x0051D860)(made, flags);
                        else if (kind)
                            result = static_cast<CTaggingControlComponent*>(made)->Destroy(flags);
                        else
                            result = static_cast<CTaggingComponent*>(made)->Destroy(flags);
                        Record({Id(reinterpret_cast<uint32_t>(result))});
                        g_trace.insert(g_trace.end(), w, w + 0x38 / 4);
                    }
                    break;
                }
                }
                if (original)
                    for (size_t k = before; k < g_trace.size(); ++k)
                        decals += g_trace[k] == 0x4928;
                Trace();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Log("  tagging: %d decals placed by the original", decals);
    Check(decals > 500, "tagging: the cases reach the decals");
    Compare(traces[0], traces[1], "tagging: tag button, sticker slap, decals, textures, member functions");
}

} // namespace

SELFTEST(Early, TestTagging, RemoveStandIns);
