// Self-tests for create-a-trick (0x00551CA0-0x005527BF): the created trick's constructor and destructors and the
// commands, the original against ours on the same stand-in skater (its created tricks and editor state), with
// every structure and array method stood in for by recording code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/goals/create_a_trick.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Game::CCreateATrick;

alignas(16) uint8_t g_skater[0x200];
alignas(16) uint8_t g_tricks[3][0x10];
alignas(16) uint8_t g_trickParts[3][3][0x10];
bool g_noSkater;
uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
uint32_t Address(const void* p) { return reinterpret_cast<uint32_t>(p); }

void StandInCatCalls(StandIns& s)
{
    StandInGoalMemory(s);
    StandInStructAdds(s);
    StandInPrintf(s);
    StandInArrays(s);
    StandInGetters(s);
    StandInStructChanges(s);
    s.At({0x00566520}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return g_noSkater ? 0u : Address(g_skater);
    });
    using GetIntName = bool (CStruct::*)(const char*, int*, int);
    using GetStName = bool (CStruct::*)(const char*, CStruct**, int);
    using GetArName = bool (CStruct::*)(const char*, CArray**, int);
    struct NameGetter {
        uint32_t address;
        uint32_t ours;
        uint32_t tag;
    };
    for (NameGetter g : {NameGetter{0x00476680, AddressOf(static_cast<GetIntName>(&CStruct::GetInteger)), 0x6630},
                         NameGetter{0x00476810, AddressOf(static_cast<GetStName>(&CStruct::GetStructure)), 0x67C0},
                         NameGetter{0x004768B0, AddressOf(static_cast<GetArName>(&CStruct::GetArray)), 0x6860}}) {
        uint32_t tag = g.tag;
        s.At({g.address, g.ours}, 12, [tag](Regs& r) {
            uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]));
            Record({tag, r.ecx, name, r.args[2] & 0xFF});
            Regs copy = r;
            copy.args[0] = name;
            return Answer(copy, tag) ? 1u : 0u;
        });
    }
    using AddStName = void (CStruct::*)(const char*, CStruct*);
    using AddFltName = void (CStruct::*)(const char*, float);
    s.At({0x00478770, AddressOf(static_cast<AddStName>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8770, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00477D10, AddressOf(static_cast<AddFltName>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7D10, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        Record({0x8A60, r.args[0], r.args[1]});
        return 0u;
    });
    using GetStGlobal = CStruct* (*)(const char*);
    using GetArGlobal = CArray* (*)(const char*);
    s.At({0x00479040, AddressOf(static_cast<GetStGlobal>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9040, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x5D3F1000u;
    });
    s.At({0x00479090, AddressOf(static_cast<GetArGlobal>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9090, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x5D3F2000u + (Hash(reinterpret_cast<const char*>(r.args[0])) & 0xF0);
    });
}

void TestGoalsCreateATrick()
{
    Setup();
    auto* standIns = new StandIns;
    StandInCatCalls(*standIns);
    using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);
    struct Command {
        uint32_t address;
        CFunc ours;
    };
    const Command kCommands[] = {
        {0x00551CA0, Game::CFunc_GetCreateATrickParams}, {0x00551D20, Game::CFunc_GetCreateATrickOtherParams},
        {0x00551D80, Game::CFunc_GetCreateATrickRotations}, {0x00551DE0, Game::CFunc_GetCreateATrickAnimations},
        {0x00551E40, Game::CFunc_SetCreateATrickParams}, {0x00551F70, Game::CFunc_SetCreateATrickOtherParams},
        {0x00551FF0, Game::CFunc_SetCreateATrickRotations}, {0x00552070, Game::CFunc_SetCreateATrickAnimations},
        {0x005520F0, Game::CFunc_CAT_SetNumAnims}, {0x00552130, Game::CFunc_CAT_GetNumAnims},
        {0x00552160, Game::CFunc_CAT_SetAnimsDone}, {0x005521A0, Game::CFunc_CAT_GetAnimsDone},
        {0x005521F0, Game::CFunc_CAT_SetRotsDone}, {0x00552230, Game::CFunc_CAT_GetRotsDone},
        {0x00552280, Game::CFunc_CAT_SetBailDone}, {0x005522C0, Game::CFunc_CAT_GetBailDone},
        {0x00552310, Game::CFunc_CAT_SetFlipSkater}, {0x00552350, Game::CFunc_CAT_GetFlipSkater},
        {0x005523A0, Game::CFunc_CAT_SetHoldTime}, {0x005523E0, Game::CFunc_CAT_GetHoldTime},
        {0x00552410, Game::CFunc_CAT_SetTotalY}, {0x00552450, Game::CFunc_CAT_GetTotalY},
        {0x00552480, Game::CFunc_CAT_SetTotalX}, {0x005524C0, Game::CFunc_CAT_GetTotalX},
        {0x005524F0, Game::CFunc_CAT_SetTotalZ}, {0x00552530, Game::CFunc_CAT_GetTotalZ},
    };
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        // The created tricks: made and destroyed.
        for (int k = 0; k < 2; ++k) {
            auto* trick = reinterpret_cast<CCreateATrick*>(g_tricks[2]);
            std::memset(g_tricks[2], 0x6B, sizeof(g_tricks[2]));
            Record({Address(o ? Original<CCreateATrick*(__thiscall*)(void*)>(0x00552560)(trick) : trick->Construct())});
            for (uint32_t w = 0; w < 0x10; w += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(g_tricks[2] + w));
            if (k)
                trick->m_rotations = nullptr; // (an array missing)
            Record({Address(o ? Original<CCreateATrick*(__thiscall*)(void*, uint8_t)>(0x005527A0)(trick, static_cast<uint8_t>(k))
                              : trick->Destroy(static_cast<uint8_t>(k)))});
        }
        for (uint32_t seed = 0; seed < 120; ++seed) {
            g_seed = seed * 2654435761u + 9;
            g_getAnswers.clear();
            g_noSkater = Next() % 6 == 0;
            for (uint32_t k = 0; k < sizeof(g_skater); ++k)
                g_skater[k] = static_cast<uint8_t>(Next());
            auto* skater = reinterpret_cast<Obj::CSkater*>(g_skater);
            for (int i = 0; i < 11; ++i)
                skater->m_trickInfo[i] = i < 3 && (i != 1 || Next() % 4) ? g_tricks[i] : nullptr;
            for (int i = 0; i < 3; ++i)
                for (int p = 0; p < 3; ++p)
                    reinterpret_cast<uint8_t**>(g_tricks[i])[1 + p] = g_trickParts[i][p];
            Params params;
            params.Integer(0, static_cast<int>(Next() % 4) - 1);
            uint32_t p = Address(params.Struct());
            uint32_t index = Next() % 3;
            if (!skater->m_trickInfo[1] && index == 1)
                index = 2; // (a missing trick is read through null)
            g_getAnswers[{p, Crc::GenerateCRCFromString("trick_index")}] = index;
            g_getAnswers[{p, 0}] = Next() % 5;
            for (const char* name : {"other_params", "rotation_info", "animation_info"})
                if (Next() % 3)
                    g_getAnswers[{p, Crc::GenerateCRCFromString(name)}] = 0x5E700000u + (Next() & 0xFF);
            Record({0xEEEE0000 | seed});
            for (const Command& command : kCommands)
                CallCommand(o, command.address, command.ours, params);
            for (uint32_t k = 0x1A8; k < 0x1C0; k += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(g_skater + k));
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 5000, "create-a-trick: the original reached its calls");
    CompareTraces(traces[0], traces[1], "create-a-trick (0x00551CA0-0x005527BF): created tricks and their commands");
}

void RestoreCat() { Restore(); }

} // namespace

SELFTEST(Early, TestGoalsCreateATrick, RestoreCat);
