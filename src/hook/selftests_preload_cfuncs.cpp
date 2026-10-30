// Self-tests for the preloading script commands and the temporary appearance ones (0x005AAEF0-0x005ABBEF:
// preload_cfuncs.cpp, skater_profile_cfuncs.cpp - batch GS3-8): each original against ours on a stand-in node array
// (vehicles, game objects, flags, pedestrians), CAS option lists, car model components and profiles; the node walk,
// Nx's models, the asset manager, the CAS look-ups, the appearance and model builder, the C runtime calls the
// original makes and the structure changes stood in for by recording code (over the game's function and ours
// alike). Pointers into either run's stack (the appearance, the builder, the scratch strings) are recorded by the
// order they were first seen.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/string.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/preload_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skater_profile_cfuncs.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Script::CArray;

constexpr uint32_t kClass = 0x12B4E660, kType = 0x7321A8D6, kModel = 0x286A8D26, kSecondModel = 0x545F8172;
constexpr uint32_t kProfile = 0x7EA855F0, kPart = 0xB6F08F39, kDescId = 0x4BB2084E, kNoRandom = 0xF6F8E158;
constexpr uint32_t kLevelSpecific = 0x3598BF7D, kExcludeFromLevels = 0x94D12F97, kNum = 0x23BC5091;
constexpr uint32_t kVehicle = 0xE47F1B79, kGameObject = 0xEF59C100, kPedestrian = 0xA0DFAC98;
constexpr uint32_t kSkipNode = 0x51C1;     // (the stand-in SkipNode's answer: a node with this integer)
constexpr uint32_t kNoPedestrians = 0x2CAA92ED;

// The node array, the CAS option list and the profiles' structures the commands walk.
Params g_nodes[10];
int g_nodeCount, g_nodeIndex;
Params g_options[6];
CArray g_optionList;
uint32_t g_optionElements[6];
Params g_pedProfiles[2], g_pedSlots[2];
Params g_levelLists[2];
uint32_t g_globalNoPeds, g_multiplayer, g_inNetGame;
int g_matchCount;
uint32_t g_rnd;
// Car model components (their objects, models)
alignas(16) uint8_t g_components[3][0x210];
alignas(16) uint8_t g_objects[3][0x40];
void* g_componentVtable[16];
uint32_t g_textures[3][3] = {{0x7E1, 0x7E2, 0x7E1}, {0x7E2, 0x7E3, 0}, {0x7E2, 0x7E1, 0}}; // (car 2: nothing new)
int g_geoms[3] = {3, 2, 2};
alignas(16) uint8_t g_profiles[3][0x64];
std::vector<uint32_t> g_pointers;

uint32_t Seen(uint32_t pointer) // (a pointer by the order it was first seen)
{
    for (size_t i = 0; i < g_pointers.size(); ++i) {
        if (g_pointers[i] == pointer)
            return 0x5EE00000u + static_cast<uint32_t>(i);
    }
    g_pointers.push_back(pointer);
    return 0x5EE00000u + static_cast<uint32_t>(g_pointers.size() - 1);
}
int ComponentIndex(uint32_t address)
{
    for (int i = 0; i < 3; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_components[i]) ||
            address == reinterpret_cast<uint32_t>(g_components[i] + 0x100)) // (a component's model: +0x100)
            return i;
    }
    return -1;
}
int ProfileIndex(uint32_t address)
{
    for (int i = 0; i < 3; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_profiles[i]))
            return i;
        if (address == reinterpret_cast<uint32_t>(g_profiles[i] + 0x4C))
            return 0x10 + i;
    }
    return -1;
}
int OptionIndex(uint32_t address)
{
    for (int i = 0; i < 6; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_options[i].Struct()))
            return i;
    }
    return -1;
}

void StandInPreloadCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    s.At({0x0045C1D0, AddressOf(&SkateScript::StartNodeWalk)}, 0, [](Regs& r) {
        Record({0xC1D0, r.args[0]});
        g_nodeIndex = 0;
        return 0u;
    });
    s.At({0x0045C1F0, AddressOf(&SkateScript::NextNode)}, 0, [](Regs&) {
        Record({0xC1F0, static_cast<uint32_t>(g_nodeIndex)});
        return g_nodeIndex < g_nodeCount ? reinterpret_cast<uint32_t>(g_nodes[g_nodeIndex++].Struct()) : 0u;
    });
    s.At({0x0045C200, AddressOf(&SkateScript::EndNodeWalk)}, 0, [](Regs&) {
        Record({0xC200});
        return 0u;
    });
    s.At({0x00565BB0, AddressOf(&Mdl::Skate::ShouldBeAbsent)}, 4, [](Regs& r) {
        int skip = 0;
        reinterpret_cast<CStruct*>(r.args[0])->GetInteger(kSkipNode, &skip, 0);
        Record({0x5BB0, static_cast<uint32_t>(skip)});
        return skip ? 1u : 0u;
    });
    s.At({0x005651F0, AddressOf(&Mdl::Skate::IsMultiplayer)}, 0, [](Regs&) {
        Record({0x51F0});
        return g_multiplayer;
    });
    s.At({0x00626EFB}, 0, [](Regs& r) { // (the original's _stricmp: the C runtime isn't started offline)
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x00623B10}, 0, [](Regs& r) { // (strstr)
        return reinterpret_cast<uint32_t>(std::strstr(reinterpret_cast<char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x004879D0, AddressOf(&Gfx::MakeModelPath)}, 0, [](Regs& r) {
        char path[0x100];
        const char* name = reinterpret_cast<const char*>(r.args[1]);
        std::snprintf(path, sizeof(path), "models\\%s%s", name, // (a name with an extension of its own keeps it)
                      std::strchr(name, '.') ? "" : reinterpret_cast<const char*>(r.args[2]));
        Record({0x79D0, Hash(path)});
        new (reinterpret_cast<void*>(r.args[0])) Str::String(path);
        return r.args[0];
    });
    s.At({0x0048BFA0, AddressOf(&Nx::CreateModel)}, 0, [](Regs&) {
        Record({0xBFA0}); // (its argument isn't read or passed)
        return 0x0DE10000u;
    });
    s.At({0x0048C3C0, AddressOf(&Nx::Engine::DeleteGeomObject)}, 0, [](Regs& r) {
        Record({0xC3C0, r.args[0]});
        return 0u;
    });
    s.At({0x004935E0, AddressOf(&Nx::CModel::AddFileGeom)}, 32, [](Regs& r) {
        Record({0x35E0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3],
                r.args[4] & 0xFF, r.args[5], r.args[6], r.args[7]});
        return 1u;
    });
    s.At({0x0048BFE0, AddressOf(&Nx::Engine::SplitSkinName)}, 0, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        char* base = reinterpret_cast<char*>(r.args[1]);
        size_t length = std::strlen(name);
        const char* dot = std::strrchr(name, '.');
        size_t keep = dot ? static_cast<size_t>(dot - name) : length;
        std::memcpy(base, name, keep);
        base[keep] = 0;
        Record({0xBFE0, Hash(name)});
        return 1u;
    });
    s.At({0x00407A60, AddressOf(&Ass::CAssMan::GetPermanentByDefault)}, 0, [](Regs& r) {
        Record({0x7A60, r.ecx});
        return 1u;
    });
    s.At({0x004087B0, AddressOf(&Ass::CAssMan::LoadOrGetAsset)}, 36, [](Regs& r) {
        Record({0x87B0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3],
                r.args[4] & 0xFF, r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7], r.args[8]});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx});
        return g_inNetGame;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return r.args[0] == kNoPedestrians ? g_globalNoPeds : 0u;
    });
    s.At({0x005AAAA0}, 0, [](Regs& r) {
        Record({0xAAA0, r.eax});
        return 0u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return r.args[0] == 0xF001 ? reinterpret_cast<uint32_t>(g_pedProfiles[0].Struct())
                                   : r.args[0] == 0xF002 ? reinterpret_cast<uint32_t>(g_pedProfiles[1].Struct()) : 0u;
    });
    s.At({0x00488EA0}, 0, [](Regs& r) {
        Record({0x8EA0, Seen(r.ecx)});
        return r.ecx;
    });
    s.At({0x00488F40}, 8, [](Regs& r) {
        Record({0x8F40, Seen(r.ecx), r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x004897D0}, 8, [](Regs& r) {
        Record({0x97D0, Seen(r.ecx), r.args[0], r.args[1]});
        return r.ecx;
    });
    s.At({0x0048A2F0}, 16, [](Regs& r) {
        Record({0xA2F0, Seen(r.ecx), Seen(r.args[0]), r.args[1], r.args[2], r.args[3]});
        return 1u;
    });
    s.At({0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, [](Regs& r) {
        Record({0x15D0F, Seen(r.ecx)});
        return 0u;
    });
    s.At({0x004890F0}, 0, [](Regs& r) {
        Record({0x90F0, Seen(r.ecx)});
        return 0u;
    });
    // The CAS options
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070A, r.args[0]});
        return reinterpret_cast<uint32_t>(&g_optionList);
    });
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, static_cast<uint32_t>(OptionIndex(r.ecx)), r.args[0]});
        return 0u;
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        ++g_rnd;
        int n = static_cast<int>(r.args[0]);
        return n > 0 ? static_cast<uint32_t>((g_rnd * 5 + 1) % static_cast<uint32_t>(n)) : 0u;
    });
    s.At({0x00485CF0}, 0, [](Regs& r) {
        Record({0x5CF0, r.args[0], r.args[1], r.args[2] & 0xFF});
        return r.args[1] < 6 ? reinterpret_cast<uint32_t>(g_options[r.args[1]].Struct()) : 0u;
    });
    s.At({0x00485D70}, 0, [](Regs& r) {
        Record({0x5D70, r.args[0], r.args[1], Seen(r.args[2]), *reinterpret_cast<uint32_t*>(r.args[3])});
        auto** out = reinterpret_cast<CStruct**>(r.args[2]);
        int count = r.args[1] == 0 ? g_matchCount : 2;
        for (int i = 0; i < count; ++i)
            out[i] = g_options[(i + r.args[1]) % 6].Struct();
        *reinterpret_cast<int*>(r.args[3]) = count;
        return 0u;
    });
    // The car components
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Record({0xF2D0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_components[0]);
    });
    s.At({0x00492E40}, 0, [](Regs& r) {
        int i = ComponentIndex(r.ecx);
        Record({0x2E40, static_cast<uint32_t>(i)});
        return i >= 0 ? static_cast<uint32_t>(g_geoms[i]) : 0u;
    });
    s.At({0x004946C0}, 4, [](Regs& r) {
        int i = ComponentIndex(r.ecx);
        Record({0x46C0, static_cast<uint32_t>(i), r.args[0]});
        return i >= 0 && r.args[0] < 3 ? g_textures[i][r.args[0]] : 0u;
    });
    g_componentVtable[12] = s.Thunk(12, [](Regs& r) {
        Record({0x1212, static_cast<uint32_t>(ComponentIndex(r.ecx)), r.args[0], r.args[1], r.args[2]});
        return 1u;
    });
    // The profiles
    s.At({0x0057B640, AddressOf(&Obj::CPlayerProfileManager::GetProfile)}, 4, [](Regs& r) {
        Record({0xB640, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % 2]);
    });
    s.At({0x0057B650, AddressOf(&Obj::CPlayerProfileManager::GetExtraProfile)}, 4, [](Regs& r) {
        Record({0xB650, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[2]);
    });
    s.At({0x0057B790, AddressOf(&Obj::CPlayerProfileManager::ResetSecondProfile)}, 0, [](Regs& r) {
        Record({0xB790, r.ecx});
        return 0u;
    });
    s.At({0x004893A0}, 4, [](Regs& r) {
        Record({0x93A0, static_cast<uint32_t>(ProfileIndex(r.ecx)), static_cast<uint32_t>(ProfileIndex(r.args[0]))});
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, static_cast<uint32_t>(ProfileIndex(r.ecx)), static_cast<uint32_t>(ProfileIndex(r.args[0]))});
        return 0u;
    });
}

void MakeNodes()
{
    for (Params& node : g_nodes)
        node = Params();
    // vehicles and game objects (one skipped, one "none", one without collision, flags), pedestrians
    g_nodes[0].Checksum(kClass, kVehicle).String(kModel, "veh\\Car01.MDL").Flag(0x6C37FDC7);
    g_nodes[1].Checksum(kClass, kGameObject).String(kModel, "gameobjects\\bench").Flag(0x0BF29BC0);
    g_nodes[2].Checksum(kClass, kGameObject).String(kModel, "NONE");
    g_nodes[3].Checksum(kClass, kGameObject).Checksum(kType, 0xBEBB41F0).String(kModel, "flag_red");
    g_nodes[4].Checksum(kClass, kVehicle).String(kModel, "veh\\skipped").Integer(kSkipNode, 1);
    g_nodes[5].Checksum(kClass, 0x1234).String(kModel, "other");
    g_nodes[6].Checksum(kClass, kPedestrian).String(kModel, "peds\\ped01").String(kSecondModel, "peds\\hat")
        .Checksum(kProfile, 0xF001);
    g_nodes[7].Checksum(kClass, kPedestrian).String(kModel, "none").Checksum(kProfile, 0xF002);
    g_nodes[8].Checksum(kClass, kPedestrian).Checksum(kProfile, 0xF003);
    g_nodes[9].Checksum(kClass, kGameObject).Checksum(kType, 0x55555).String(kModel, "gameobjects\\sign.mdx");
    g_nodeCount = 10;
}

void MakeOptions()
{
    for (Params& option : g_options)
        option = Params();
    g_levelLists[0] = Params();
    g_levelLists[0].Integer(0x1E5E1, 1);     // level_specific: this level
    g_levelLists[1] = Params();
    g_levelLists[1].Integer(0x1E5E2, 1);     // exclude_from_levels: another level
    g_options[0].Checksum(kDescId, 0xD0).Integer(0x355F9467, 1); // (allowed_to_pick: the adds are recorded, not made)
    g_options[1].Checksum(kDescId, 0xD1).Flag(kNoRandom).Structure(kLevelSpecific, g_levelLists[0].Struct());
    g_options[2].Checksum(kDescId, 0xD2).Structure(kLevelSpecific, g_levelLists[0].Struct()).Integer(0x355F9467, 1);
    g_options[3].Checksum(kDescId, 0xD3).Structure(kLevelSpecific, g_levelLists[1].Struct());
    g_options[4].Checksum(kDescId, 0xD4).Structure(kExcludeFromLevels, g_levelLists[1].Struct()).Integer(0x355F9467, 1);
    g_options[5].Checksum(kDescId, 0xD5).Structure(kExcludeFromLevels, g_levelLists[0].Struct());
    std::memset(&g_optionList, 0, sizeof(g_optionList));
    g_optionList.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    g_optionList.m_size = 6;
    for (int i = 0; i < 6; ++i)
        g_optionElements[i] = reinterpret_cast<uint32_t>(g_options[i].Struct());
    g_optionList.m_elements = g_optionElements;
    for (int i = 0; i < 2; ++i) {
        g_pedSlots[i] = Params();
        g_pedSlots[i].Checksum(kDescId, i == 0 ? 2 : 1); // (option 2; option 1 is no_random)
        g_pedProfiles[i] = Params();
        g_pedProfiles[i].Structure(0xB0D7, g_pedSlots[i].Struct());
    }
}

void TestPreloadCFuncs()
{
    Setup();
    for (uint32_t address : {0x006918DCu, 0x006F0FB8u, 0x007CE4B4u}) {
        g_saved.push_back({address, 4, {}});
        g_saved.back().bytes.resize(4);
        std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(address), 4);
    }
    GameVariable<uint32_t>(0x006918DC) = 0xA55E0000;
    GameVariable<uint32_t>(0x006F0FB8) = 0x0B1EC700;
    GameVariable<uint32_t>(0x007CE4B4) = 0x9F0F0000;
    auto* standIns = new StandIns;
    StandInPreloadCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_pointers.clear();
        ResetState();
        ResetMemory();
        MakeNodes();
        MakeOptions();
        *reinterpret_cast<uint32_t*>(g_skate + 0x374) = 0x1E5E1; // (this level)
        g_multiplayer = g_inNetGame = g_globalNoPeds = 0;
        g_rnd = 0;
        g_matchCount = 3;
        std::memset(g_components, 0, sizeof(g_components));
        std::memset(g_objects, 0, sizeof(g_objects));
        std::memset(g_profiles, 0, sizeof(g_profiles));
        for (int i = 0; i < 3; ++i) {
            auto* component = reinterpret_cast<Obj::CBaseComponent*>(g_components[i]);
            component->m_vtable = g_componentVtable;
            component->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_objects[i]);
            component->mp_nextOfType = i < 2 ? reinterpret_cast<Obj::CBaseComponent*>(g_components[i + 1]) : nullptr;
            reinterpret_cast<Obj::CObject*>(g_objects[i])->m_type = i == 1 ? 0x77777777u : 0x5C0312EBu;
            *reinterpret_cast<uint8_t**>(g_components[i] + 0x208) = g_components[i] + 0x100;
        }
        auto call = [&](uint32_t address, CFunc ours, Params& params) { CallCommand(original, address, ours, params); };
        using namespace CFuncs;
        Params none;

        // PreloadModels: a single player game (the flag skipped), then multiplayer
        call(0x005AAFA0, CFunc_PreloadModels, none);
        g_multiplayer = 1;
        call(0x005AAFA0, CFunc_PreloadModels, none);
        // PreloadPedestrians: in a network game, with the global set, without random parts, with them
        g_inNetGame = 1;
        call(0x005AB300, CFunc_PreloadPedestrians, none);
        g_inNetGame = 0;
        g_globalNoPeds = 1;
        call(0x005AB300, CFunc_PreloadPedestrians, none);
        g_globalNoPeds = 0;
        Params noRandom; noRandom.Flag(kNoRandom);
        call(0x005AB300, CFunc_PreloadPedestrians, noRandom);
        call(0x005AB300, CFunc_PreloadPedestrians, none);
        // PreselectRandomPedestrians: options matching, then none matching (light / tan / dark); a smaller num
        Params pick; pick.Checksum(kPart, 0xB0D7);
        call(0x005AB690, CFunc_PreselectRandomPedestrians, pick);
        MakeOptions();
        g_matchCount = 0;
        Params pickTwo; pickTwo.Checksum(kPart, 0xB0D7).Integer(kNum, 5);
        call(0x005AB690, CFunc_PreselectRandomPedestrians, pickTwo);
        MakeOptions();
        g_matchCount = 2;
        Params pickOne; pickOne.Checksum(kPart, 0xB0D7).Integer(kNum, 1);
        call(0x005AB690, CFunc_PreselectRandomPedestrians, pickOne);
        // ReplaceCarTextures
        Params textures; textures.Integer(0x7E7E, 1);
        call(0x005ABB20, CFunc_ReplaceCarTextures, textures);
        // The temporary appearance, player 2's profile
        Params restore; restore.Checksum(0xA1DC81F9, 0xE7A1).Integer(0x67E6859A, 1);
        call(0x005AAEF0, CFunc_RestoreTemporaryAppearance, restore);
        restore.Flag(0x3AF36E6D);
        call(0x005AAEF0, CFunc_RestoreTemporaryAppearance, restore);
        call(0x005AAF90, CFunc_SyncPlayer2Profile, none);
        RecordState();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 700, "preload commands: the original reached its calls");
    Compare(traces[0], traces[1], "preload, pedestrian part, car texture and temporary appearance commands (0x005AAEF0-0x005ABB20)");
}

} // namespace

SELFTEST(Early, TestPreloadCFuncs, Restore);
