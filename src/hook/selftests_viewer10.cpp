// Self-tests for the skater pieces (src/game/skater/skater.cpp, 0x0057FE30-0x00580BD8): the original and ours run on a
// stand-in skater in an arena (its components, physics, score, state blocks and lists made by hand); structures,
// scripts, the network, the skate module, cheats, the front end and the shadow are recording stand-ins (floats
// returned by C++ functions jumped to), and the calls, results and the whole arena are compared.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "game/front/front_end.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/objman.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/shadow_component.h"
#include "game/obj/walk_hang.h"
#include "game/skate/career.h"
#include "game/skater/physics_control.h"
#include "game/skater/stats_manager.h"
#include "game/skater/skater.h"
#include "game/gfx/viewport.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/script/game_cfuncs.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkater;

// Arena layout.
constexpr uint32_t kSkater = 0x0000;   // 0x2C8
constexpr uint32_t kCore = 0x0400;     // the SkaterCorePhysics component (+0x2ED riding, +0x2F0 what, +0x302)
constexpr uint32_t kControl = 0x0800;  // SkaterPhysicsControl (+0x18)
constexpr uint32_t kModel = 0x0900;    // the model component (+0x208)
constexpr uint32_t kCorePhys = 0x0A00; // m_corePhysics: +0xC an object (+0x110), +0x1528 a block
constexpr uint32_t kBlocks = 0x2000;   // small blocks, 0x40 apart
constexpr uint32_t kSkate = 0x2800;    // the skate module (+0x20 cheats, +0x3D0)
constexpr uint32_t kParams = 0x3000;   // structures, 0x10 apart
constexpr uint32_t kInfos = 0x3800;    // trick infos, 0x10 apart
constexpr uint32_t kArrays = 0x3C00;   // arrays (+4 size, +8 elements), 0x20 apart, elements +0x10

uint32_t Block(int i) { return At(kBlocks + 0x40 * i); }
uint32_t Params(int i) { return At(kParams + 0x10 * i); }

void* g_skaterVtable[20];
void* g_listVtable[8];
void* g_lightVtable[8];
std::map<std::pair<uint32_t, uint32_t>, std::pair<float, float>> g_pairs;
std::map<std::pair<uint32_t, uint32_t>, float> g_paramFloats;
std::map<uint32_t, float> g_globalFloats;
std::map<uint32_t, uint32_t> g_globalStructures;
bool g_rule, g_net, g_cheat, g_classic, g_noControl;
uint32_t g_player;
int g_playerKind; // 1: 0x00543490 says yes, 2: 0x005433B0 says yes
int g_difficulty;
std::vector<uint32_t> g_boards;
float g_light[4];
bool g_noModel, g_noLights;

float __cdecl GetFloatStandIn(uint32_t name)
{
    Rec(0x8F00, {name});
    auto it = g_globalFloats.find(name);
    return it == g_globalFloats.end() ? 0.0f : it->second;
}

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0x5BD63B29: return At(kCore);
    case 0xD9151D4D: return g_noControl ? 0u : At(kControl);
    case 0x286A8D26: return g_noModel ? 0u : At(kModel);
    default: return 0u;
    }
}

void Install(StandIns& s)
{
    InstallCommon(s);
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&GetFloatStandIn)));
    auto getter = [](Regs& r, uint32_t tag) {
        Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
        auto it = g_params.find({r.ecx, r.args[0]});
        if (it == g_params.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [getter](Regs& r) { return getter(r, 0x6950); });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [getter](Regs& r) { return getter(r, 0x6630); });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [getter](Regs& r) { return getter(r, 0x67C0); });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))}, 12,
         [getter](Regs& r) { return getter(r, 0x6860); });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Rec(0x66D0, {r.ecx, r.args[0]});
             auto it = g_paramFloats.find({r.ecx, r.args[0]});
             if (it == g_paramFloats.end())
                 return 0u;
             *reinterpret_cast<float*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476DF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CPair*, bool)>(&CStruct::GetPair))}, 12,
         [](Regs& r) {
             Rec(0x6DF0, {r.ecx, r.args[0]});
             auto it = g_pairs.find({r.ecx, r.args[0]});
             if (it == g_pairs.end())
                 return 0u;
             auto* pair = reinterpret_cast<Script::CPair*>(r.args[1]);
             pair->x = it->second.first;
             pair->y = it->second.second;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return g_params.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArray))}, 8,
         [](Regs& r) {
             Rec(0x8B00, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        auto it = g_globalStructures.find(r.args[0]);
        return it == g_globalStructures.end() ? Params(60) : it->second;
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return At(kArrays + 0x300) + (r.args[0] & 0x30);
    });
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        Rec(0x8A60, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0] % 4] + r.args[0] * 0x100;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return ComponentFor(r.args[0]);
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return r.args[0] == 0x51u ? At(kSkater) : (r.args[0] == 0x52u ? 0u : At(kBlocks + 0x7C0));
    });
    // The rules, the network, cheats, modes.
    s.At({0x005A1180, AddressOf(CFuncs::CFunc_InSplitScreenGame)}, 0, [](Regs& r) {
        Rec(0x1180, {r.args[0], r.args[1]});
        return g_rule ? 1u : 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Rec(0xE4F0, {r.ecx});
        return g_net ? 1u : 0u;
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Rec(0xAEA0, {r.ecx, r.args[0] & 0xFFFF}); // (the id's low 16 bits: all it reads)
        return g_player;
    });
    s.At({0x00543490, AddressOf(&GameNet::PlayerInfo::IsKing)}, 0, [](Regs& r) {
        Rec(0x3490, {r.ecx});
        return g_playerKind == 1 ? 1u : 0u;
    });
    s.At({0x005433B0, AddressOf(&GameNet::PlayerInfo::IsCarryingFlag)}, 0, [](Regs& r) {
        Rec(0x33B0, {r.ecx});
        return g_playerKind == 2 ? 1u : 0u;
    });
    s.At({0x00584CC0, AddressOf(&Obj::CSkaterCareer::GetCheat)}, 4, [](Regs& r) {
        Rec(0x4CC0, {r.ecx, r.args[0]});
        return g_cheat ? 1u : 0u;
    });
    s.At({0x00564D90, AddressOf(&Mdl::Skate::GetGoalManager)}, 0, [](Regs& r) {
        Rec(0x4D90, {r.ecx});
        return Block(30);
    });
    s.At({0x00559050, AddressOf(&Game::CGoalManager::GetDifficultyLevel)}, 4, [](Regs& r) {
        Rec(0x9050, {r.ecx, r.args[0] & 0xFF});
        return static_cast<uint32_t>(g_difficulty);
    });
    s.At({0x00553220, AddressOf(&Game::CGameMode::IsTrue)}, 4, [](Regs& r) {
        Rec(0x3220, {r.ecx, r.args[0]});
        return g_classic ? 1u : 0u;
    });
    s.At({0x00566100, AddressOf(&Mdl::Skate::SkipToRestartPoint)}, 4, [](Regs& r) {
        Rec(0x6100, {r.ecx, r.args[0]});
        if (g_boards.empty())
            return 0x0B0A0000u + r.args[0];
        uint32_t board = g_boards.front();
        g_boards.erase(g_boards.begin());
        return board;
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Rec(0x3DE0, {r.args[0]});
        return 3u;
    });
    s.At({0x004A09C0, AddressOf(Nx::GetActiveCamera)}, 0, [](Regs& r) {
        Rec(0x09C0, {r.args[0]});
        return Block(31) + r.args[0];
    });
    s.At({0x0042BD60, AddressOf(&Obj::SetRigidBodyPusherReach)}, 0, [](Regs& r) {
        Rec(0xBD60, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0051C480, AddressOf(&Obj::CStatsManagerComponent::SetGoalStatus)}, 8, [](Regs& r) {
        Rec(0xC480, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005178B0, AddressOf(&Obj::CSkaterPhysicsControlComponent::SetPaused)}, 4, [](Regs& r) {
        Rec(0x78B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0054F960, AddressOf(&Mdl::FrontEnd::PauseGame)}, 4, [](Regs& r) {
        Rec(0xF960, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    // The shadow: the model's light object (its +0x2C: Block(32) or none, read by the original through the folded
    // getter 0x00532EB0, by ours directly) and its vtable slot 5 (the original through the jump 0x00456B30).
    g_lightVtable[5] = s.Thunk(4, [](Regs& r) {
        Rec(0x6B30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_light);
    });
    s.At({0x0043D990, AddressOf(&Walk::Lerp)}, 0, [](Regs& r) {
        const auto* a = reinterpret_cast<const uint32_t*>(r.args[1]);
        const auto* b = reinterpret_cast<const uint32_t*>(r.args[2]);
        Rec(0xD990, {a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], r.args[3]});
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        out[0] = a[0], out[1] = b[1], out[2] = r.args[3], out[3] = 0x3F800000;
        return r.args[0];
    });
    s.At({0x00431360, AddressOf(&Obj::CShadowComponent::SetLightDirection)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Rec(0x1360, {r.ecx, v[0], v[1], v[2], v[3]});
        return 0u;
    });
    // The skater's vtable (+0x3C: set the board) and its list's (+0x14).
    g_skaterVtable[15] = s.Thunk(8, [](Regs& r) {
        Rec(0xB0A2, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    g_listVtable[5] = s.Thunk(4, [](Regs& r) {
        Rec(0x1157, {r.ecx, r.args[0]});
        return 0u;
    });
}

uint8_t& Byte(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint8_t*>(address + offset); }
uint32_t& Word(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint32_t*>(address + offset); }
float& Float(uint32_t address, uint32_t offset) { return *reinterpret_cast<float*>(address + offset); }

// Every function: stats (overrides, defaults, the skate module's, online rules, the cheat, the bonus), scaled values
// (stat or none, switch pairs clamped both ways, every difficulty and classic, limits both ways), the physics
// structures for every ride, the profile match for each condition, sparks, the CAT / list / chapter info, boards
// (repeats, none), the pause states (with and without the game freezing, no control) and the shadow light (no model,
// no light, above and below the horizon, fading both ways from each state).
void TestSkater()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x007CCE14, 4, {}}, {0x007CE470, 4, {}}, {0x007CE464, 4, {}}, {0x006F0FB8, 4, {}},
                 {0x007CE4C4, 0x24, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_pairs.clear();
        g_paramFloats.clear();
        g_globalFloats = {{0x6357D57C, 4.0f}, {0x9816E1E3, 2.0f}, {0x0000F10A, 1.25f}};
        g_globalStructures.clear();
        g_integers.clear();
        g_rule = g_net = g_cheat = g_classic = g_noControl = g_noModel = g_noLights = false;
        g_player = 0;
        g_playerKind = 0;
        g_difficulty = 1;
        g_boards.clear();
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CE478) = At(kSkate);
        GameVariable<uint32_t>(0x007CCE14) = Block(33);
        GameVariable<uint32_t>(0x007CE470) = Block(34);
        GameVariable<uint32_t>(0x007CE464) = Block(35);
        GameVariable<uint32_t>(0x006F0FB8) = Block(36);
        std::memset(reinterpret_cast<void*>(0x007CE4C4), 0, 0x24);
        Word(At(kSkate), 0x20) = Block(37);
        // The skater.
        auto* skater = Ptr<CSkater>(kSkater);
        uint32_t self = At(kSkater);
        skater->m_vtable = g_skaterVtable;
        skater->m_id = 0x51;
        Byte(self, 0xBC) = 1;
        skater->m_corePhysics = Ptr<uint8_t>(kCorePhys);
        Word(At(kCorePhys), 0xC) = Block(1);
        Word(At(kCorePhys), 0x1528) = Block(2);
        skater->m_scoreHolder = Ptr<uint8_t>(kBlocks + 0x40 * 3);
        Word(Block(3), 0x18) = Block(4);
        skater->m_state284 = nullptr;
        skater->m_state288 = nullptr;
        Word(Block(5), 0x2C) = Block(6);
        skater->m_shadow = Ptr<void>(kBlocks + 0x40 * 7);
        skater->m_list28C = Ptr<void>(kBlocks + 0x40 * 8);
        *Ptr<void**>(kBlocks + 0x40 * 8) = g_listVtable;
        Word(At(kModel), 0x208) = Block(9);
        Word(Block(9), 0x2C) = Block(32);
        *Ptr<void**>(kBlocks + 0x40 * 32) = g_lightVtable;
        for (int i = 0; i < 10; ++i)
            skater->m_statOverrides[i] = i % 3 == 0 ? -1.0f : 1.5f * i;
        skater->m_skaterNumber = 3;
        skater->m_viewport = 1;
        skater->m_flag2C4 = true;
        skater->m_style = 0x57E1;
        skater->m_name = 0x7A3E;
        skater->m_male = 1;
        for (int i = 0; i < 11; ++i) {
            skater->m_trickInfo[i] = Ptr<uint8_t>(kInfos + 0x10 * i);
            Word(At(kInfos + 0x10 * i), 4) = Params(40 + i);
            Word(At(kInfos + 0x10 * i), 8) = At(kArrays + 0x200 + 4 * i);
            Word(At(kInfos + 0x10 * i), 0xC) = At(kArrays + 0x280 + 4 * i);
        }
        auto stat = [&](int index) {
            float v = original ? Original<float(__thiscall*)(void*, int)>(0x00580490)(skater, index) : skater->GetStat(index);
            t.push_back(Bits(v));
        };
        // Overrides.
        const float values[] = {12.0f, -3.0f, 5.0f, std::numeric_limits<float>::quiet_NaN(), 10.0f, 0.0f, 9.5f, -0.5f};
        for (int rule = 0; rule < 2; ++rule) {
            g_rule = rule != 0;
            for (int i = 0; i < 8; ++i) {
                if (original)
                    Original<void(__thiscall*)(void*, int, float)>(0x0057FE30)(skater, 6 + (i & 1), values[i]);
                else
                    skater->SetStatOverride(6 + (i & 1), values[i]);
                t.push_back(Bits(skater->m_statOverrides[6 + (i & 1)]));
            }
        }
        g_rule = false;
        for (int i = 0; i < 10; ++i)
            skater->m_statOverrides[i] = i % 3 == 0 ? -1.0f : 1.5f * i;
        // Stats.
        for (int i : {0, 1, 2, 3})
            stat(i);
        Float(At(kSkate), 0x3D0) = 7.5f;
        stat(1);
        Float(At(kSkate), 0x3D0) = 0.0f;
        g_net = true;
        stat(1);
        g_player = Block(10);
        for (int kind : {0, 1, 2}) {
            g_playerKind = kind;
            stat(2);
        }
        g_net = false;
        g_rule = true;
        for (int kind : {0, 1}) {
            g_playerKind = kind;
            g_player = kind ? Block(10) : 0u;
            stat(4);
        }
        g_player = Block(10);
        stat(4);
        g_rule = false;
        g_cheat = true;
        stat(5);
        g_cheat = false;
        Byte(Block(4), 0x74) = 1;
        stat(5);
        Byte(Block(4), 0x74) = 0;
        // Scaled values.
        auto scaled = [&](uint32_t params) {
            float v = original ? Original<float(__thiscall*)(void*, CStruct*)>(0x005805B0)(skater, reinterpret_cast<CStruct*>(params))
                               : skater->GetScaledValue(reinterpret_cast<CStruct*>(params));
            t.push_back(Bits(v));
        };
        uint32_t p = Params(0);
        g_pairs[{p, 0}] = {2.0f, 10.0f};
        scaled(p); // no stat
        g_params[{p, 0}] = 4;
        scaled(p);
        g_pairs[{p, 0x9016B4E7}] = {-0.4f, 1.3f};
        for (int goofy = 0; goofy < 2; ++goofy) {
            Byte(Block(1), 0x110) = static_cast<uint8_t>(goofy);
            for (int flipped = 0; flipped < 2; ++flipped) {
                Byte(Block(2), 0x28) = static_cast<uint8_t>(flipped);
                skater->m_statOverrides[6] = flipped ? 9.0f : 0.5f; // (the switch stat: past each end)
                scaled(p);
            }
        }
        skater->m_statOverrides[6] = 6.0f;
        scaled(p);
        g_pairs[{p, 0xBA8FB854}] = {0.5f, 2.5f};
        for (int difficulty : {0, 1, 2, 3}) {
            g_difficulty = difficulty;
            for (int classic : {0, 1}) {
                g_classic = classic != 0;
                scaled(p);
            }
        }
        g_difficulty = 1;
        g_classic = false;
        g_paramFloats[{p, 0x8069179F}] = 5.0f;
        scaled(p); // rising: at most 5
        g_paramFloats[{p, 0x8069179F}] = 50.0f;
        scaled(p);
        g_pairs[{p, 0}] = {10.0f, 2.0f};
        scaled(p); // falling: at least 50
        g_paramFloats[{p, 0x8069179F}] = 1.0f;
        scaled(p);
        // By name: the physics structure's or the global one; a structure's own.
        uint32_t physics = Params(61);
        g_globalStructures[0x8CBC89DD] = physics;
        g_params[{physics, 0x0000AAAA}] = p;
        g_globalStructures[0x0000BBBB] = p;
        for (uint32_t name : {0x0000AAAAu, 0x0000BBBBu}) {
            t.push_back(Bits(original ? Original<float(__thiscall*)(void*, uint32_t)>(0x00580790)(skater, name)
                                      : skater->GetScaledPhysics(name)));
        }
        skater->m_id = 0x52; // (not found: the default physics)
        t.push_back(Bits(original ? Original<float(__thiscall*)(void*, uint32_t)>(0x00580790)(skater, 0x0000AAAAu)
                                  : skater->GetScaledPhysics(0x0000AAAAu)));
        skater->m_id = 0x51;
        g_params[{Params(1), 0x0000CCCC}] = p;
        for (uint32_t name : {0x0000CCCCu, 0x0000DDDDu})
            t.push_back(Bits(original ? Original<float(__thiscall*)(void*, uint32_t, CStruct*)>(0x005807F0)(skater, name, Ptr<CStruct>(kParams + 0x10))
                                      : skater->GetScaledParam(name, Ptr<CStruct>(kParams + 0x10))));
        // Physics structures.
        auto structure = [&](uint32_t object) {
            t.push_back(Rel(original ? Original<uint32_t(__cdecl*)(uint32_t)>(0x005802E0)(object)
                                     : reinterpret_cast<uint32_t>(Obj::GetPhysicsStructure(reinterpret_cast<Obj::CCompositeObject*>(object)))));
        };
        for (uint32_t name : {0x8B3B782Bu, 0x05716042u, 0x8711BC92u, 0x871FFB73u, 0x43DA544Eu, 0x08C9AD5Eu, 0xD3D12913u})
            g_globalStructures[name] = Params(62) + (name & 0xF0);
        structure(0);
        structure(self);
        Byte(At(kCore), 0x302) = 1;
        structure(self);
        Byte(At(kCore), 0x2ED) = 1;
        for (uint32_t ride : {0x3029FB1Fu, 0x61C62B2Du, 0x86929898u, 0xB2C99BF0u, 0x906F06E2u, 0xD8D2D646u, 0x14A3A4CFu,
                              0x8C7873D0u, 0x6CD38BBBu, 0x12345678u, 0xFFFFFFFFu}) {
            Word(At(kCore), 0x2F0) = ride;
            structure(self);
        }
        Byte(At(kCore), 0x2ED) = 0;
        Byte(At(kCore), 0x302) = 0;
        Byte(self, 0xBC) = 0;
        structure(self);
        Byte(self, 0xBC) = 1;
        // Physics values.
        g_paramFloats[{physics, 0x0000F10A}] = 3.5f;
        g_params[{physics, 0x0000F10B}] = 7;
        g_integers[0x0000F10C] = 9;
        for (uint32_t name : {0x0000F10Au, 0x0000F10Cu}) {
            t.push_back(Bits(original ? Original<float(__cdecl*)(uint32_t, int, void*)>(0x00580400)(name, 1, skater)
                                      : Obj::GetPhysicsFloat(name, 1, skater)));
            t.push_back(static_cast<uint32_t>(original ? Original<int(__cdecl*)(uint32_t, int, void*)>(0x00580440)(name + 1, 0, nullptr)
                                                       : Obj::GetPhysicsInt(name + 1, 0, nullptr)));
        }
        // The profile match.
        uint32_t m = Params(2);
        auto match = [&]() {
            t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*)>(0x0057FED0)(skater, reinterpret_cast<CStruct*>(m))
                                 : skater->MatchesParams(reinterpret_cast<CStruct*>(m)));
        };
        match();
        g_params[{m, 0xD82F8AC8}] = 1;
        match();
        skater->m_pro = true;
        match();
        g_params[{m, 0x5A3264BB}] = 1;
        match();
        g_params.erase({m, 0x5A3264BB});
        g_params[{m, 0xE5C2F84C}] = 1;
        match();
        g_params.erase({m, 0xE5C2F84C});
        g_params[{m, 0x3F813177}] = 1;
        match();
        skater->m_male = 0;
        match();
        skater->m_male = 1;
        g_params[{m, 0xC2E3D008}] = 0x7A3F;
        match();
        g_params[{m, 0xC2E3D008}] = 0x7A3E;
        match();
        for (int goofy : {0, 1}) {
            skater->m_goofy = goofy != 0;
            for (uint32_t stance : {0xB58EFC2Bu, 0x287FCD4Eu}) {
                g_params[{m, 0x7D02BCC3}] = stance;
                match();
            }
        }
        skater->m_goofy = true;
        g_params[{m, 0xCC424795}] = 0x57E2;
        match();
        g_params[{m, 0xCC424795}] = 0x57E1;
        match();
        // Sparks, getters.
        auto voidCall = [&](uint32_t address, void (CSkater::*ours)()) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(skater);
            else
                (skater->*ours)();
            t.push_back(skater->m_sparks);
        };
        voidCall(0x00580000, &CSkater::SparksOff);
        voidCall(0x0057FFE0, &CSkater::SparksOn);
        voidCall(0x00580000, &CSkater::SparksOff);
        t.push_back(Rel(original ? Original<uint32_t(__thiscall*)(void*)>(0x00580040)(skater) : reinterpret_cast<uint32_t>(skater->GetBlock238())));
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00580050)(skater) : skater->GetFlag2C4());
        t.push_back(Rel(original ? Original<uint32_t(__thiscall*)(void*)>(0x00580480)(skater) : reinterpret_cast<uint32_t>(skater->GetScore())));
        t.push_back(Rel(original ? Original<uint32_t(__thiscall*)(void*)>(0x0057FEB0)(skater) : reinterpret_cast<uint32_t>(skater->GetCamera())));
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x0057FEC0)(skater) : skater->GetSkaterNumber()));
        for (int viewport : {0, 1, 2}) {
            skater->m_viewport = viewport;
            t.push_back(Rel(original ? Original<uint32_t(__thiscall*)(void*)>(0x00580880)(skater)
                                     : reinterpret_cast<uint32_t>(skater->GetSkaterCamera())));
        }
        // CAT info, the list, chapters.
        uint32_t tricks = At(kArrays);
        for (int i = 0; i < 4; ++i)
            Word(tricks, 0x10 + 4 * i) = Params(20 + i);
        g_params[{Params(3), 0x1E26FD3E}] = tricks;
        for (int i = 0; i < 4; ++i) {
            g_params[{Params(20 + i), 0x26A7CADF}] = Params(30 + i);
            if (i != 2)
                g_params[{Params(20 + i), 0x6B19FC8F}] = At(kArrays + 0x100 + 4 * i);
            g_params[{Params(20 + i), 0x2E628EE6}] = At(kArrays + 0x180 + 4 * i);
        }
        auto withParams = [&](uint32_t address, void (CSkater::*ours)(CStruct*), uint32_t params) {
            if (original)
                Original<void(__thiscall*)(void*, CStruct*)>(address)(skater, reinterpret_cast<CStruct*>(params));
            else
                (skater->*ours)(reinterpret_cast<CStruct*>(params));
        };
        withParams(0x00580060, &CSkater::LoadCATInfo, Params(3));
        g_params[{Params(4), 0x44B66A9F}] = tricks;
        withParams(0x00580150, &CSkater::LoadList44B66A9F, Params(4));
        withParams(0x005801B0, &CSkater::WriteChapterInfo, Params(5));
        withParams(0x00580210, &CSkater::ReadChapterInfo, Params(5));
        g_params[{Params(6), 0x8E8EE0C0}] = At(kArrays + 0x20);
        g_params[{Params(6), 0x72FC1AE3}] = At(kArrays + 0x40);
        withParams(0x00580210, &CSkater::ReadChapterInfo, Params(6));
        if (original)
            Original<void(__thiscall*)(void*, float)>(0x005802C0)(skater, 2.75f);
        else
            skater->Set42BD60(2.75f);
        // Boards.
        auto board = [&]() {
            if (original)
                Original<void(__thiscall*)(void*)>(0x00580830)(skater);
            else
                skater->MoveToRandomRestart();
            t.push_back(skater->m_lastRestart);
        };
        g_boards = {0x0B0A0003, 0x0B0A0003, 0x0B0A0005};
        skater->m_lastRestart = 0x0B0A0003;
        board();
        g_boards.assign(25, 0x0B0A0005);
        board(); // (the same every time: 20 tries)
        g_boards = {0};
        board();
        g_boards.clear();
        // Pausing.
        auto pause = [&](int state) {
            if (original)
                Original<void(__thiscall*)(void*, int)>(0x005808A0)(skater, state);
            else
                skater->SetPauseState(state);
            t.push_back(static_cast<uint32_t>(skater->m_pauseState));
        };
        skater->m_pauseState = 0;
        for (float freeze : {2.0f, 0.0f}) {
            g_globalFloats.erase(0x9816E1E3);
            g_integers[0x9816E1E3] = freeze != 0.0f ? 1 : 0;
            for (int control : {0, 1}) {
                Byte(At(kControl), 0x18) = static_cast<uint8_t>(control);
                for (int state : {1, 1, 2, 0, 2, 5, 0})
                    pause(state);
            }
        }
        g_noControl = true;
        pause(1);
        g_noControl = false;
        // The shadow.
        auto shadow = [&]() {
            if (original)
                Original<void(__thiscall*)(void*, int, int)>(0x005809C0)(skater, 1, 2);
            else
                skater->UpdateShadowLight(1, 2);
            t.push_back(static_cast<uint32_t>(skater->m_shadowFade));
            const auto* statics = reinterpret_cast<const uint32_t*>(0x007CE4C4);
            t.insert(t.end(), statics, statics + 9);
        };
        g_noModel = true;
        shadow();
        g_noModel = false;
        Word(At(kModel), 0x208) = 0;
        shadow();
        Word(At(kModel), 0x208) = Block(9);
        g_noLights = true;
        Word(Block(9), 0x2C) = 0;
        shadow();
        g_noLights = false;
        Word(Block(9), 0x2C) = Block(32);
        const float lights[][4] = {{0.3f, 0.9f, -0.2f, 1.0f}, {0.1f, -0.2f, 0.7f, 0.5f}, {0.0f, -0.5f, 0.0f, 1.0f},
                                   {-0.6f, 0.4f, 0.6f, 0.0f}};
        for (const auto& l : lights) {
            std::memcpy(g_light, l, sizeof(g_light));
            shadow();
        }
        // Fading: each state's way.
        skater->m_shadowFade = 15;
        skater->m_state284 = Ptr<uint8_t>(kBlocks + 0x40 * 5);
        for (uint32_t ground : {0u, 1u}) {
            Word(Block(2), 0x18) = ground;
            shadow();
            shadow();
        }
        Byte(Block(6), 0x118) = 1;
        skater->m_state288 = Ptr<uint8_t>(kBlocks + 0x40 * 11);
        for (uint32_t state : {1u, 0u, 1u, 2u, 1u}) {
            Word(Block(11), 0x18) = state;
            shadow();
        }
        Byte(Block(6), 0x118) = 0;
        Word(Block(6), 0x1C) = 1;
        shadow();
        skater->m_shadowFade = 1;
        skater->m_state284 = nullptr;
        skater->m_state288 = nullptr;
        shadow();
        shadow();
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater");
}

} // namespace

SELFTEST(Early, TestSkater, ViewerTestKit::Cleanup);
