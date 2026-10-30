// Self-tests for the profile list script commands (0x005ABBF0-0x005AC5EF, skater_profile_cfuncs.cpp - batch GS3-8): each
// original against ours on a stand-in master_skater_list and stat_names, profile templates with real info
// structures, the profile manager and the skate module; the profiles' and the skate module's calls without source and
// the structure changes recorded (over the game's function and ours alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/skater_profile_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "game/gfx/model_appearance.h"
#include "game/goals/goal_manager.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Obj::CSkaterProfile;
using Script::CArray;

constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kMasterSkaterList = 0xF31E8FF8;
constexpr uint32_t kStatNames = 0x61EB6BA9;
constexpr uint32_t kPointsAvailable = 0x471FC4DD;
constexpr int kTemplates = 3;

alignas(16) uint8_t g_profiles[kTemplates][0x64];
alignas(16) uint8_t g_skaters[3][0x330];
alignas(16) uint8_t g_profileManager[0x40];
Params g_infos[kTemplates];
Params g_entries[3];  // master_skater_list's skaters
Params g_statEntries[2];
CArray g_master, g_stats;
uint32_t g_masterElements[3], g_statElements[2];

uint32_t Rel(uint32_t address)
{
    for (int i = 0; i < kTemplates; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_profiles[i]))
            return 0x9F000000u + i;
        if (address == reinterpret_cast<uint32_t>(g_profiles[i] + 0x5C))
            return 0x9E000000u + i;
    }
    for (int i = 0; i < 3; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_entries[i].Struct()))
            return 0xE0000000u + i;
        if (address == reinterpret_cast<uint32_t>(g_skaters[i]))
            return 0x5C000000u + i;
    }
    return address;
}

void StandInProfileListCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070A, r.args[0]});
        return r.args[0] == kMasterSkaterList ? reinterpret_cast<uint32_t>(&g_master)
                                              : r.args[0] == kStatNames ? reinterpret_cast<uint32_t>(&g_stats) : 0u;
    });
    s.At({0x00557FB0, AddressOf(&Game::CGoalManager::GetGoalByIndex)}, 4, [](Regs& r) {
        Record({0x7FB0, r.ecx, r.args[0]});
        return r.args[0] < kTemplates ? reinterpret_cast<uint32_t>(g_profiles[r.args[0]]) : 0u;
    });
    s.At({0x0057B7E0, AddressOf(&Obj::CPlayerProfileManager::GetTemplate)}, 4, [](Regs& r) {
        Record({0xB7E0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % kTemplates]);
    });
    s.At({0x0057B640, AddressOf(&Obj::CPlayerProfileManager::GetProfile)}, 4, [](Regs& r) {
        Record({0xB640, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % kTemplates]);
    });
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[2]);
    });
    s.At({0x00586230, AddressOf(&CSkaterProfile::SetFromParams)}, 12, [](Regs& r) {
        Record({0x6230, Rel(r.ecx), Rel(r.args[0]), r.args[1] & 0xFF, r.args[2] & 0xFF});
        return 1u;
    });
    s.At({0x00586810, AddressOf(&CSkaterProfile::Reset)}, 4, [](Regs& r) {
        Record({0x6810, Rel(r.ecx), Rel(r.args[0])});
        return 1u;
    });
    s.At({0x00488F40, AddressOf(&Gfx::CModelAppearance::Load)}, 8, [](Regs& r) {
        Record({0x8F40, Rel(r.ecx), r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x005861A0, AddressOf(&CSkaterProfile::SetValue09F350E5)}, 4, [](Regs& r) {
        Record({0x61A0, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00585FD0, AddressOf(&CSkaterProfile::SetStat)}, 8, [](Regs& r) {
        Record({0x5FD0, Rel(r.ecx), r.args[0], r.args[1]});
        return r.args[1] <= 10 ? 1u : 0u;
    });
    s.At({0x00584560, AddressOf(&Obj::CSkaterCareer::SetInfoInteger)}, 8, [](Regs& r) {
        Record({0x4560, r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00566980, AddressOf(&Mdl::Skate::ResetSkaters)}, 8, [](Regs& r) {
        Record({0x6980, r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] < 3 && r.args[0] != 1 ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
}

void MakeArray(CArray& array, uint32_t* elements, Params* entries, int count)
{
    std::memset(&array, 0, sizeof(array));
    array.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    array.m_size = static_cast<uint32_t>(count);
    for (int i = 0; i < count; ++i)
        elements[i] = reinterpret_cast<uint32_t>(entries[i].Struct());
    if (count == 1)
        array.m_single = elements[0];
    else
        array.m_elements = elements;
}

void TestProfileListCFuncs()
{
    Setup();
    for (uint32_t address : {0x007CE4B4u}) {
        g_saved.push_back({address, 4, {}});
        g_saved.back().bytes.resize(4);
        std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(address), 4);
    }
    GameVariable<uint8_t*>(0x007CE4B4) = g_profileManager;
    auto* standIns = new StandIns;
    StandInProfileListCalls(*standIns);
    // master_skater_list: skaters 0x501 (template 1), 0x502 (template 2), 0x509 (none); stat_names: two stats
    const uint32_t names[3] = {0x501, 0x502, 0x509};
    for (int i = 0; i < 3; ++i) {
        g_entries[i] = Params();
        g_entries[i].Checksum(kName, names[i]).Integer(0x57A1, 3 + i).Integer(0x57A2, 7 - i).Integer(kPointsAvailable, 10 * i);
    }
    g_statEntries[0] = Params();
    g_statEntries[0].Checksum(kName, 0x57A1);
    g_statEntries[1] = Params();
    g_statEntries[1].Checksum(kName, 0x57A2);
    MakeArray(g_master, g_masterElements, g_entries, 3);
    MakeArray(g_stats, g_statElements, g_statEntries, 2);
    const uint32_t templateNames[kTemplates] = {0x500, 0x501, 0x502};

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        std::memset(g_profiles, 0, sizeof(g_profiles));
        std::memset(g_skaters, 0, sizeof(g_skaters));
        std::memset(g_profileManager, 0, sizeof(g_profileManager));
        *reinterpret_cast<uint32_t*>(g_profileManager) = kTemplates;
        for (int k = 0; k < kTemplates; ++k) {
            g_infos[k] = Params();
            g_infos[k].Checksum(kName, templateNames[k]).Integer(0x1234, k);
            std::memcpy(g_profiles[k] + 0x5C, g_infos[k].Struct(), 8);
        }
        g_skaters[0][0x168] = 1;
        auto call = [&](uint32_t address, CFunc ours, Params& params) { CallCommand(original, address, ours, params); };
        using namespace CFuncs;
        Params none;

        call(0x005ABBF0, CFunc_ResetAllToDefaultStats, none);
        for (uint32_t name : {0x501u, 0x502u, 0x777u}) {
            Params whole; whole.Checksum(kName, name);
            call(0x005ABE30, CFunc_ResetToDefaultProfile, whole);
            Params stats; stats.Checksum(kName, name).Checksum(0x55F82F0B, 0xC164AF20);
            call(0x005ABE30, CFunc_ResetToDefaultProfile, stats);
            Params info; info.Checksum(kName, name).Checksum(0x55F82F0B, 0x1);
            call(0x005ABE30, CFunc_ResetToDefaultProfile, info);
            Params get; get.Checksum(kName, name - 1);
            call(0x005AC1F0, CFunc_GetSkaterProfileInfoByName, get);
            static Params values;
            if (!values.count)
                values.Integer(0x4444, 4);
            Params set; set.Checksum(kName, name - 1).Structure(0x7031F10C, values.Struct());
            call(0x005AC280, CFunc_SetSkaterProfileInfoByName, set);
        }
        call(0x005ABF00, CFunc_ResetAllToDefaultProfile, none);
        static Params scriptParams;
        if (!scriptParams.count)
            scriptParams.Integer(0x3333, 3);
        Params each; each.Checksum(0x62BA3F6A, 0xD0D0).Structure(0x7031F10C, scriptParams.Struct());
        call(0x005ABF70, CFunc_ForEachSkaterName, each);
        call(0x005AC0C0, CFunc_ForEachSkaterProfile, each);
        Params eachBare; eachBare.Checksum(0x62BA3F6A, 0xD0D1);
        call(0x005ABF70, CFunc_ForEachSkaterName, eachBare);
        call(0x005AC0C0, CFunc_ForEachSkaterProfile, eachBare);
        call(0x005AC320, CFunc_PrintCurrentAppearance, none);
        static Params appearance, info;
        if (!appearance.count) {
            appearance.Integer(0x5555, 5);
            info.Integer(0x6666, 6);
        }
        Params neversoft; neversoft.Structure(0x554C7D6F, appearance.Struct()).Structure(0x3476CEA8, info.Struct());
        call(0x005AC340, CFunc_SetNeversoftSkater, neversoft);
        Params reset; reset.Integer(0xDD7DF50E, 4).Checksum(0x66780C6F, 0x40DE).Flag(0xD6F06BF6);
        call(0x005AC3B0, CFunc_ResetSkaters, reset);
        call(0x005AC3B0, CFunc_ResetSkaters, none);
        Params setInfo; setInfo.Integer(0x67E6859A, 2).Structure(0x7031F10C, info.Struct());
        call(0x005AC410, CFunc_SetSkaterProfileInfo, setInfo);
        call(0x005AC470, CFunc_GetSkaterProfileInfo, setInfo);
        Params prop; prop.Integer(0x67E6859A, 1).Checksum(0, 0x57A1).Integer(0, 8);
        call(0x005AC4B0, CFunc_SetSkaterProfileProperty, prop);
        Params prop2; prop2.Integer(0x67E6859A, 0).Checksum(0, 0x57A2).Integer(0, 12);
        call(0x005AC4B0, CFunc_SetSkaterProfileProperty, prop2);
        call(0x005AC520, CFunc_SetClassicStat, prop);
        for (int k = 0; k < 2; ++k) {
            Params toggle;
            if (k == 1)
                toggle.Flag(0xF649D637);
            call(0x005AC570, CFunc_ToggleAlwaysSpecial, toggle);
            Record({g_skaters[0][0x168], g_skaters[2][0x168]});
        }
        Params off; off.Flag(0xD443A2BC);
        call(0x005AC570, CFunc_ToggleAlwaysSpecial, off);
        Params both; both.Flag(0xD443A2BC).Flag(0xF649D637);
        call(0x005AC570, CFunc_ToggleAlwaysSpecial, both);
        Record({g_skaters[0][0x168], g_skaters[2][0x168]});
        RecordState();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 600, "profile list commands: the original reached its calls");
    Compare(traces[0], traces[1], "profile reset, list, info and special commands (0x005ABBF0-0x005AC570)");
}

} // namespace

SELFTEST(Early, TestProfileListCFuncs, Restore);
