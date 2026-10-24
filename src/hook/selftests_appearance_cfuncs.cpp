// Self-tests for the model refresh, appearance part, stat and velocity script commands of 0x005AAEF0-0x005B0A40
// (appearance_cfuncs.cpp, skater_profile_cfuncs.cpp, skater_query_cfuncs.cpp - batch GS3-6): each original against ours on
// stand-in skaters (with model and trick components whose virtual calls are recorded), profiles and their parts,
// the profile manager, the career and the object manager; every call without source and the structure changes
// recorded (over the game's function and ours alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/crc.h"
#include "game/obj/model_component.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "game/skate/appearance_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/skater_profile_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/skater/skater_query_cfuncs.h"
#include "game/skater/trick_component.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Obj::CSkaterProfile;

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kTrick = 0x270F56E1;
constexpr uint32_t kBailBoard = 0x884EF81B;
constexpr int kProfiles = 3;

alignas(16) uint8_t g_profiles[kProfiles][0x64];
alignas(16) uint8_t g_skaters[2][0x330];
alignas(16) uint8_t g_models[2][0x40];
alignas(16) uint8_t g_tricks[2][0x40];
alignas(16) uint8_t g_board[0x50];
alignas(16) uint8_t g_profileManager[0x40];
Script::CSymbolTableEntry g_symbols[2]; // the current ped pro global (0x09EEBCB8) and a structure it may name

// The global 0x09EEBCB8 in the stand-in symbol table as `type` (a name: of the second global 0x0000ABCD, a structure,
// when `linked`; else of nothing).
void SetPedProSymbol(Script::ESymbolType type, bool linked)
{
    std::memset(g_symbols, 0, sizeof(g_symbols));
    g_symbols[0].unknown0 = 1;
    g_symbols[0].type = type;
    g_symbols[0].name = 0x09EEBCB8;
    g_symbols[0].checksum = linked ? 0x0000ABCD : 0x00012345;
    g_symbols[1].unknown0 = 1;
    g_symbols[1].type = Script::ESYMBOLTYPE_STRUCTURE;
    g_symbols[1].name = 0x0000ABCD;
    g_table[0x09EEBCB8 & 0xFFF] = &g_symbols[0];
    g_table[0x0000ABCD & 0xFFF] = &g_symbols[1];
}
void* g_modelVtable[16];
void* g_boardVtable[8];
std::map<uint32_t, uint32_t> g_answers;
bool g_boardThere;
Params g_parts[3];   // the parts the appearance calls hand out
Params g_infos[kProfiles];

uint32_t Answer(uint32_t address)
{
    auto it = g_answers.find(address);
    return it == g_answers.end() ? 0u : it->second;
}
template <size_t N, size_t S>
int IndexIn(uint8_t (&table)[N][S], uint32_t address)
{
    for (size_t i = 0; i < N; ++i) {
        if (address == reinterpret_cast<uint32_t>(table[i]))
            return static_cast<int>(i);
    }
    return -1;
}
uint32_t Rel(uint32_t address) // (a stand-in object as its table and index)
{
    int i;
    if ((i = IndexIn(g_profiles, address)) >= 0)
        return 0x9F000000u + i;
    if ((i = IndexIn(g_skaters, address)) >= 0)
        return 0x5C000000u + i;
    if ((i = IndexIn(g_models, address)) >= 0)
        return 0x30000000u + i;
    if ((i = IndexIn(g_tricks, address)) >= 0)
        return 0x31000000u + i;
    for (int k = 0; k < 3; ++k) {
        if (address == reinterpret_cast<uint32_t>(g_parts[k].Struct()))
            return 0xA7000000u + k;
    }
    for (int k = 0; k < kProfiles; ++k) {
        if (address >= reinterpret_cast<uint32_t>(g_profiles[k]) && address < reinterpret_cast<uint32_t>(g_profiles[k]) + 0x64)
            return 0x9E000000u + k * 0x100 + (address - reinterpret_cast<uint32_t>(g_profiles[k]));
    }
    return address;
}

void StandInAppearanceCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    StandInGameModeIsTrue(s);
    s.At({0x005665C0}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] < 2 ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x00564FC0}, 4, [](Regs& r) {
        Record({0x4FC0, r.ecx, r.args[0]});
        return r.args[0] < kProfiles ? reinterpret_cast<uint32_t>(g_profiles[r.args[0]]) : 0u;
    });
    s.At({0x00564F80}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[1]);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        int i = IndexIn(g_skaters, r.ecx);
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        if (i < 0)
            return 0u;
        return r.args[0] == kModel ? reinterpret_cast<uint32_t>(g_models[i])
                                   : r.args[0] == kTrick ? reinterpret_cast<uint32_t>(g_tricks[i]) : 0u;
    });
    s.At({0x004200E0, AddressOf(&Obj::CModelComponent::RebuildFromProfile)}, 8, [](Regs& r) {
        Record({0x00E0, Rel(r.ecx), Rel(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x004202A0, AddressOf(&Obj::CModelComponent::HideGeom)}, 12, [](Regs& r) {
        Record({0x02A0, Rel(r.ecx), r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFF});
        return 1u;
    });
    g_modelVtable[13] = s.Thunk(16, [](Regs& r) {
        Record({0x1313, Rel(r.ecx), Rel(r.args[0]), r.args[1] & 0xFF, r.args[2], r.args[3]});
        return 0u;
    });
    g_modelVtable[1] = s.Thunk(0, [](Regs& r) {
        Record({0x1111, Rel(r.ecx)});
        return 0u;
    });
    g_boardVtable[6] = s.Thunk(0, [](Regs& r) {
        Record({0x6666, r.ecx == reinterpret_cast<uint32_t>(g_board) ? 1u : 0u});
        return 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return g_boardThere ? reinterpret_cast<uint32_t>(g_board) : 0u;
    });
    s.At({0x00461AB0, AddressOf(&Obj::CGeneralManager::FlushDeadObjects)}, 0, [](Regs& r) {
        Record({0x1AB0, r.ecx});
        return 0u;
    });
    s.At({0x00580BE0, AddressOf(&Obj::CSkater::SetStatsFromProfile)}, 4, [](Regs& r) {
        Record({0x0BE0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00522410, AddressOf(&Obj::CTrickComponent::SetTrickMappings)}, 4, [](Regs& r) {
        Record({0x2410, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00580EB0, AddressOf(&Obj::CSkater::UpdateFromProfile)}, 4, [](Regs& r) {
        Record({0x0EB0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // The appearance's parts
    s.At({0x00485CF0}, 0, [](Regs& r) {
        Record({0x5CF0, r.args[0], r.args[1], r.args[2] & 0xFF});
        return r.args[1] == 0xD0 ? 0u : reinterpret_cast<uint32_t>(g_parts[0].Struct());
    });
    s.At({0x00489050}, 4, [](Regs& r) {
        Record({0x9050, Rel(r.ecx), r.args[0]});
        return r.args[0] == 0xBAD ? 0u : reinterpret_cast<uint32_t>(g_parts[1].Struct());
    });
    s.At({0x004890B0}, 4, [](Regs& r) {
        Record({0x90B0, Rel(r.ecx), r.args[0]});
        return r.args[0] == 0xBAD ? 0u : reinterpret_cast<uint32_t>(g_parts[2].Struct());
    });
    using AddSt = void (CStruct::*)(uint32_t, CStruct*);
    using AddStNamed = void (CStruct::*)(const char*, CStruct*);
    s.At({0x004781B0, AddressOf(static_cast<AddSt>(&CStruct::AddStructurePointer))}, 8, [](Regs& r) {
        Record({0x81B0, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478260, AddressOf(static_cast<AddStNamed>(&CStruct::AddStructurePointer))}, 8, [](Regs& r) {
        Record({0x8260, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // The career, the profiles, the profile manager, the symbol table
    s.At({0x00584580, AddressOf(&Obj::CSkaterCareer::AddToInfo471FC4DD)}, 4, [](Regs& r) {
        Record({0x4580, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00584540, AddressOf(&Obj::CSkaterCareer::GetInfoInteger)}, 4, [](Regs& r) {
        Record({0x4540, r.ecx, r.args[0]});
        return 0xC4A1u;
    });
    s.At({0x00586000, AddressOf(&CSkaterProfile::AddToInfo471FC4DD)}, 4, [](Regs& r) {
        Record({0x6000, Rel(r.ecx), r.args[0]});
        return 1u;
    });
    s.At({0x00586040, AddressOf(&CSkaterProfile::GetInfo471FC4DD)}, 0, [](Regs& r) {
        Record({0x6040, Rel(r.ecx)});
        return 0x4471u;
    });
    s.At({0x00586060, AddressOf(&CSkaterProfile::AddMaxSpecials)}, 4, [](Regs& r) {
        Record({0x6060, Rel(r.ecx), r.args[0]});
        return 1u;
    });
    s.At({0x00585FB0, AddressOf(&CSkaterProfile::GetStat)}, 4, [](Regs& r) {
        Record({0x5FB0, Rel(r.ecx), r.args[0]});
        return 7u;
    });
    s.At({0x00586210, AddressOf(&CSkaterProfile::GetDisplayName)}, 0, [](Regs& r) {
        Record({0x6210, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>("Custom Skater");
    });
    s.At({0x0057B7E0, AddressOf(&Obj::CPlayerProfileManager::GetTemplate)}, 4, [](Regs& r) {
        Record({0xB7E0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % kProfiles]);
    });
    s.At({0x0057B630, AddressOf(&Obj::CPlayerProfileManager::GetCurrentProfile)}, 0, [](Regs& r) {
        Record({0xB630, r.ecx});
        return reinterpret_cast<uint32_t>(g_profiles[0]);
    });
    s.At({0x0057B640, AddressOf(&Obj::CPlayerProfileManager::GetProfile)}, 4, [](Regs& r) {
        Record({0xB640, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % kProfiles]);
    });
    s.At({0x00557FB0}, 4, [](Regs& r) {
        Record({0x7FB0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % kProfiles]);
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5790, r.args[0], Rel(r.args[1]), Rel(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
}

uint32_t FloatBits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

void TestAppearanceCFuncs()
{
    Setup();
    for (uint32_t address : {0x007CE4B4u, 0x006F0FB8u}) {
        g_saved.push_back({address, 4, {}});
        g_saved.back().bytes.resize(4);
        std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(address), 4);
    }
    GameVariable<uint8_t*>(0x007CE4B4) = g_profileManager;
    GameVariable<uint32_t>(0x006F0FB8) = 0x0B1EC700;
    auto* standIns = new StandIns;
    StandInAppearanceCalls(*standIns);
    for (int k = 0; k < 3; ++k) {
        if (!g_parts[k].count)
            g_parts[k].Integer(0x7A00 + k, k);
    }

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        std::memset(g_profiles, 0, sizeof(g_profiles));
        std::memset(g_skaters, 0, sizeof(g_skaters));
        std::memset(g_board, 0, sizeof(g_board));
        std::memset(g_profileManager, 0, sizeof(g_profileManager));
        *reinterpret_cast<uint32_t*>(g_profileManager) = 3;         // (three templates)
        *reinterpret_cast<int*>(g_profileManager + 0x1C) = 1;       // (the current index)
        *reinterpret_cast<void***>(g_board) = g_boardVtable;
        for (int i = 0; i < 2; ++i) {
            std::memset(g_models[i], 0, sizeof(g_models[i]));
            std::memset(g_tricks[i], 0, sizeof(g_tricks[i]));
            *reinterpret_cast<void***>(g_models[i]) = g_modelVtable;
            auto* skater = reinterpret_cast<Obj::CSkater*>(g_skaters[i]);
            skater->m_skaterNumber = i == 0 ? 1 : 0;
            skater->m_viewport = i;
            skater->m_vel = {3.0f, -4.0f, 12.0f, 0.5f};
            skater->m_pos = {-7.75f, 1234.9f, 0.6f, 1.0f};
        }
        for (int k = 0; k < kProfiles; ++k) {
            g_infos[k] = Params();
            if (k != 2)
                g_infos[k].Integer(0x27EB9B9D, k); // is_hidden: 0, 1 (profile 2: missing)
            std::memcpy(g_profiles[k] + 0x5C, g_infos[k].Struct(), 8);
        }
        g_boardThere = true;
        g_answers.clear();
        auto call = [&](uint32_t address, CFunc ours, Params& params, void* object = nullptr) {
            CallCommand(original, address, ours, params, object);
        };
        using namespace CFuncs;
        Params none;

        // The model refreshes
        {
            Params a; a.Integer(0x7EA855F0, 2).Integer(0x5B8AB877, 1);
            call(0x005AF750, CFunc_RefreshSkaterColors, a);
            call(0x005AF7D0, CFunc_RefreshSkaterScale, a);
            call(0x005AF850, CFunc_RefreshSkaterVisibility, a);
            call(0x005AF8D0, CFunc_RefreshSkaterUV, a);
            Params b; b.Integer(0x5B8AB877, 0);
            call(0x005AF750, CFunc_RefreshSkaterColors, b);
            // RefreshSkaterModel: the unnamed index for both, separate ones, the first viewport (board there or not),
            // no_board
            Params c; c.Integer(0, 1);
            call(0x005AFD80, CFunc_RefreshSkaterModel, c);
            Params d; d.Integer(0, 1).Integer(0x7EA855F0, 2).Integer(0x5B8AB877, 0);
            call(0x005AFD80, CFunc_RefreshSkaterModel, d);
            g_boardThere = false;
            Params e; e.Checksum(0x41EEC86B, 0x1);
            call(0x005AFD80, CFunc_RefreshSkaterModel, e);
            Params f; f.Flag(0x41EEC86B);
            call(0x005AFD80, CFunc_RefreshSkaterModel, f);
            g_boardThere = true;
            // EditPlayerAppearance
            static Params targetParams;
            if (!targetParams.count)
                targetParams.Integer(0x1111, 1);
            Params g; g.Integer(0, 2).Checksum(0xB990D003, 0x7A26E7).Structure(0x46A2869C, targetParams.Struct());
            call(0x005AFF30, CFunc_EditPlayerAppearance, g);
            Params h; h.Integer(0x7EA855F0, 1).Checksum(0xB990D003, 0x7A26E8);
            call(0x005AFF30, CFunc_EditPlayerAppearance, h);
        }
        // The appearance's parts
        {
            Params a; a.Checksum(0xB6F08F39, 0xF00).Checksum(0x4BB2084E, 0xD1);
            call(0x005B0420, CFunc_GetActualCASOptionStruct, a);
            Params b; b.Checksum(0xB6F08F39, 0xF01).Checksum(0x4BB2084E, 0xD0);
            call(0x005B0420, CFunc_GetActualCASOptionStruct, b);
            Params p1; p1.Integer(0x67E6859A, 1).Checksum(0xB6F08F39, 0xF02);
            Params bad; bad.Integer(0x67E6859A, 2).Checksum(0xB6F08F39, 0xBAD);
            for (Params* p : {&p1, &bad, &none}) {
                call(0x005B0480, CFunc_GetActualPlayerAppearancePart, *p);
                call(0x005B0500, CFunc_GetPlayerAppearancePart, *p);
            }
            Params g1; g1.Integer(0x67E6859A, 2);
            call(0x005B0580, CFunc_GetPlayerAppearance, g1);
            call(0x005B0580, CFunc_GetPlayerAppearance, none);
            Params color; color.Integer(0x67E6859A, 1).Checksum(0xB6F08F39, 0xF03).Float(0x6E94F918, 12.9f)
                .Float(0xE4F130F4, -3.7f).Integer(0x949BC47B, 80).Integer(0x97DBDDE6, 3);
            call(0x005B0670, CFunc_SetPlayerAppearanceColor, color);
            Params colorBad; colorBad.Checksum(0xB6F08F39, 0xBAD).Float(0x6E94F918, 1.0f);
            call(0x005B0670, CFunc_SetPlayerAppearanceColor, colorBad);
            Params scale; scale.Checksum(0xB6F08F39, 0xF04).Float(0x7323E97C, 1.75f).Float(0x9D2D8850, -0.5f)
                .Integer(0x5A96985D, 0);
            call(0x005B0780, CFunc_SetPlayerAppearanceScale, scale);
            Params scaleNew; scaleNew.Integer(0x67E6859A, 2).Checksum(0xB6F08F39, 0xBAD).Float(0x0424D9EA, 99.99f);
            call(0x005B0780, CFunc_SetPlayerAppearanceScale, scaleNew);
            Params uv; uv.Checksum(0xB6F08F39, 0xF05).Float(0xCF6AA087, 0.25f).Float(0x5663F13D, -1.5f)
                .Float(0x266932C8, 2.0f).Integer(0x1256B6C6, 90).Integer(0x8602F6EE, 1);
            call(0x005B0920, CFunc_SetPlayerAppearanceUV, uv);
            Params uvBad; uvBad.Checksum(0xB6F08F39, 0xBAD);
            call(0x005B0920, CFunc_SetPlayerAppearanceUV, uvBad);
        }
        // Stat points, special trick slots, stats, profile queries
        {
            Params three; three.Integer(0, 3);
            for (uint32_t classic : {0u, 1u}) {
                g_modeProperties[0x5A2C589A] = classic;
                call(0x005AF950, CFunc_AwardStatPoint, three);
                call(0x005AF950, CFunc_AwardStatPoint, none);
                Params stat; stat.Checksum(0, 0x57A7).Checksum(0x5B8AB877, 2);
                call(0x005B0280, CFunc_GetStatValue, stat);
                Params stat2; stat2.Checksum(0, 0x57A8);
                call(0x005B0280, CFunc_GetStatValue, stat2);
            }
            Params stat3; stat3.Checksum(0, 0x57A9);
            for (float forced : {7.9f, -2.5f}) { // (a forced stat value)
                *reinterpret_cast<float*>(g_skate + 0x3D0) = forced;
                call(0x005B0280, CFunc_GetStatValue, stat3);
            }
            *reinterpret_cast<float*>(g_skate + 0x3D0) = -0.0f;
            call(0x005B0280, CFunc_GetStatValue, stat3);
            *reinterpret_cast<float*>(g_skate + 0x3D0) = 0.0f;
            Params all; all.Flag(0xC4E78E22);
            call(0x005AF9F0, CFunc_AwardSpecialTrickSlot, all);
            Params named; named.Checksum(0x7EA855F0, 0x22);
            call(0x005AF9F0, CFunc_AwardSpecialTrickSlot, named);
            call(0x005AF9F0, CFunc_AwardSpecialTrickSlot, none);
            Params player1; player1.Integer(0x67E6859A, 1);
            call(0x005AFAA0, CFunc_UpdateSkaterStats, player1);
            call(0x005AFFE0, CFunc_GetCurrentSkaterProfileIndex, none);
            call(0x005B0000, CFunc_GetCustomSkaterName, none);
            // GetCurrentPedProName: a structure, a name of a structure, a name of nothing, an integer
            const std::pair<Script::ESymbolType, bool> symbols[] = {{Script::ESYMBOLTYPE_STRUCTURE, false},
                                                                    {Script::ESYMBOLTYPE_NAME, true},
                                                                    {Script::ESYMBOLTYPE_NAME, false},
                                                                    {Script::ESYMBOLTYPE_INTEGER, false}};
            for (const auto& symbol : symbols) {
                SetPedProSymbol(symbol.first, symbol.second);
                call(0x005B0030, CFunc_GetCurrentPedProName, none);
            }
            g_table[0x09EEBCB8 & 0xFFF] = nullptr;
            g_table[0x0000ABCD & 0xFFF] = nullptr;
            Params player2; player2.Integer(0x67E6859A, 2);
            call(0x005B0360, CFunc_GetNumStatPointsAvailable, player2);
            for (uint32_t pro : {0u, 1u, 2u}) {
                Params unlock; unlock.Checksum(0xA1DC81F9, pro);
                call(0x005B03B0, CFunc_UnlockSkater, unlock);
            }
        }
        // The script's object's position and velocity
        {
            call(0x005B0070, CFunc_GetSkaterPosition, none, g_skaters[0]);
            call(0x005B0070, CFunc_GetSkaterPosition, none);
            auto velocities = [] {
                for (int i = 0; i < 2; ++i) {
                    auto* skater = reinterpret_cast<Obj::CSkater*>(g_skaters[i]);
                    Record({FloatBits(skater->m_vel.x), FloatBits(skater->m_vel.y), FloatBits(skater->m_vel.z),
                            FloatBits(skater->m_vel.w)});
                }
            };
            Params speed; speed.Float(0, 26.0f);
            call(0x005B00F0, CFunc_SetSkaterVelocity, speed, g_skaters[0]);
            velocities();
            Params intSpeed; intSpeed.Integer(0, 5);
            call(0x005B00F0, CFunc_SetSkaterVelocity, intSpeed, g_skaters[1]);
            velocities();
            Params vector; vector.Float(0x6187906D, 1.5f).Float(0x1680A0FB, -2.25f).Integer(0x8F89F141, 7);
            call(0x005B00F0, CFunc_SetSkaterVelocity, vector, g_skaters[1]);
            velocities();
            Params partial; partial.Float(0x1680A0FB, 9.5f);
            call(0x005B00F0, CFunc_SetSkaterVelocity, partial, g_skaters[0]);
            velocities();
            call(0x005B00F0, CFunc_SetSkaterVelocity, speed);
        }
        RecordState();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 900, "appearance and stat commands: the original reached its calls");
    Compare(traces[0], traces[1], "model refresh, appearance part, stat and velocity commands (0x005AF750-0x005B0920)");
}

} // namespace

SELFTEST(Early, TestAppearanceCFuncs, Restore);
