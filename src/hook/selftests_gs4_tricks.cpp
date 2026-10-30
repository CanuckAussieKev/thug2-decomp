// Self-tests for the script commands 0x005B0A40-0x005B1B60 (configurable tricks, records, soundtracks, controllers):
// each original against ours on identical stand-in state; the profile manager, the profiles, the skate module, the
// skaters and the other game objects they reach are stood in for by recording code (the game's functions and ours
// alike), so the commands are compared call by call. Structure getters run for real on hand-built structures.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/front/front_end.h"
#include "game/gfx/texture_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/pcm/music.h"
#include "game/script/trick_cfuncs.h"
#include "game/sio/device.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
#include "game/core/crc.h"
#include "game/goals/goal_manager.h"
#include "game/script/struct.h"
#include "game/skate/score.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;
using Obj::CSkaterProfile;

// The game's objects (stand-ins: plain memory, the same addresses in both runs).
alignas(16) uint8_t g_skate[0x100];
alignas(16) uint8_t g_profileManager[0x40];
alignas(16) uint8_t g_profiles[4][0x64];
alignas(16) uint8_t g_skaters[8][0x200];
alignas(16) uint8_t g_created[10][8];
alignas(16) uint8_t g_frontEnd[0x100];
alignas(16) uint8_t g_inputManager[0x140];
alignas(16) uint8_t g_netManager[0x40];
alignas(16) uint8_t g_objectManager[0x40];
alignas(16) uint8_t g_records[0x40];
alignas(16) uint8_t g_career[0x40];
alignas(16) uint8_t g_sioManager[0x40];
alignas(16) uint8_t g_device[0x40];
TestParams g_mapping;     // a profile's trick_mapping
TestParams g_createdInfo[10];
TestParams g_profileInfo[4];

const uint32_t kProfileNames[4] = {0x0A0F0001, 0x0A0F0002, 0x0A0F0003, 0x0A0F0004};
CSkaterProfile::SpecialTrick g_specials[6];
int g_maxSpecials;
uint32_t g_currentProfile; // an index into g_profiles, or 9: none
bool g_inNetGame;
uint32_t g_localSkater;    // an index into g_skaters, or 9: none
bool g_skaterExists[8];
bool g_haveDevice;
uint32_t g_catKey;

uint32_t ProfileIndex(uint32_t address)
{
    for (uint32_t i = 0; i < 4; ++i)
        if (address == reinterpret_cast<uint32_t>(g_profiles[i]))
            return i;
    return address;
}
uint32_t SkaterIndex(uint32_t address)
{
    for (uint32_t i = 0; i < 8; ++i)
        if (address == reinterpret_cast<uint32_t>(g_skaters[i]))
            return 0x5C00 + i;
    return address;
}
uint32_t Profile(uint32_t index) { return index < 4 ? reinterpret_cast<uint32_t>(g_profiles[index]) : 0u; }

void StandInGameCalls(StandIns& s)
{
    using Obj::CPlayerProfileManager;
    s.At({0x00461AB0, AddressOf(&Obj::CGeneralManager::FlushDeadObjects)}, 0, [](Regs& r) {
        Record({0x1AB0, r.ecx == reinterpret_cast<uint32_t>(g_objectManager)});
        return 0u;
    });
    s.At({0x0057B7E0, AddressOf(&CPlayerProfileManager::GetTemplate)}, 4, [](Regs& r) {
        Record({0xB7E0, r.ecx == reinterpret_cast<uint32_t>(g_profileManager), r.args[0]});
        return Profile(r.args[0] == kProfileNames[2] ? 2 : 3);
    });
    s.At({0x0057B630, AddressOf(&CPlayerProfileManager::GetCurrentProfile)}, 0, [](Regs&) {
        Record({0xB630});
        return Profile(g_currentProfile);
    });
    s.At({0x0057B640, AddressOf(&CPlayerProfileManager::GetProfile)}, 4, [](Regs& r) {
        Record({0xB640, r.args[0]});
        return r.args[0] < 2 ? Profile(r.args[0]) : 0u;
    });
    s.At({0x0057B680, AddressOf(&CPlayerProfileManager::SetLocked)}, 4, [](Regs& r) {
        Record({0xB680, r.ecx == reinterpret_cast<uint32_t>(g_profileManager), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx == reinterpret_cast<uint32_t>(g_skate), r.args[0]});
        return Profile(1);
    });
    s.At({0x00557FB0, AddressOf(&Game::CGoalManager::GetGoalByIndex)}, 4, [](Regs& r) {
        Record({0x7FB0, r.ecx == reinterpret_cast<uint32_t>(g_profileManager), r.args[0]});
        return Profile(r.args[0]);
    });
    s.At({0x00586150, AddressOf(&CSkaterProfile::GetName)}, 0, [](Regs& r) {
        uint32_t index = ProfileIndex(r.ecx);
        Record({0x6150, index});
        return index < 4 ? kProfileNames[index] : 0u;
    });
    s.At({0x00586110, AddressOf(&CSkaterProfile::GetInfoStructure)}, 4, [](Regs& r) {
        Record({0x6110, ProfileIndex(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_mapping.Struct());
    });
    s.At({0x005860A0, AddressOf(&CSkaterProfile::GetMaxSpecials)}, 0, [](Regs& r) {
        Record({0x60A0, ProfileIndex(r.ecx)});
        return static_cast<uint32_t>(g_maxSpecials);
    });
    s.At({0x00586630, AddressOf(&CSkaterProfile::GetSpecialTrick)}, 8, [](Regs& r) {
        Record({0x6630, ProfileIndex(r.ecx), r.args[1]});
        auto* out = reinterpret_cast<CSkaterProfile::SpecialTrick*>(r.args[0]);
        *out = g_specials[r.args[1] % 6];
        return r.args[0];
    });
    s.At({0x00586710, AddressOf(&CSkaterProfile::SetSpecialTrick)}, 12, [](Regs& r) {
        auto* trick = reinterpret_cast<CSkaterProfile::SpecialTrick*>(r.args[1]);
        Record({0x6710, ProfileIndex(r.ecx), r.args[0], trick->trick, trick->keys, trick->flag, r.args[2] & 0xFF});
        return 1u;
    });
    s.At({0x005A9D90, AddressOf(&CFuncs::FindNameOfValue)}, 0, [](Regs& r) {
        Record({0x9D90, r.args[0] == reinterpret_cast<uint32_t>(g_mapping.Struct()), r.args[1], r.args[2]});
        return r.args[1] == 0x7E1C0001 ? 0x4E700001u : r.args[1] == 0x7E1C0002 ? 0x4E700002u : 0u;
    });
    s.At({0x005A9E30}, 0, [](Regs& r) { // (the structure in EBX)
        Record({0x9E30, r.ebx == reinterpret_cast<uint32_t>(g_mapping.Struct()), r.args[0]});
        return g_catKey;
    });
    s.At({AddressOf(&CFuncs::FindNameOfInteger)}, 0, [](Regs& r) {
        Record({0x9E30, r.args[0] == reinterpret_cast<uint32_t>(g_mapping.Struct()), r.args[1]});
        return g_catKey;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx == reinterpret_cast<uint32_t>(g_netManager)});
        return g_inNetGame ? 1u : 0u;
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx == reinterpret_cast<uint32_t>(g_skate)});
        return g_localSkater < 8 ? reinterpret_cast<uint32_t>(g_skaters[g_localSkater]) : 0u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx == reinterpret_cast<uint32_t>(g_skate), r.args[0]});
        return r.args[0] < 8 && g_skaterExists[r.args[0]] ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, SkaterIndex(r.ecx), r.args[0]});
        return r.ecx + 0x100;
    });
    s.At({0x00522410, AddressOf(&Obj::CTrickComponent::SetTrickMappings)}, 4, [](Regs& r) {
        Record({0x2410, SkaterIndex(r.ecx - 0x100), ProfileIndex(r.args[0])});
        return 0u;
    });
    s.At({0x00584130, AddressOf(&Obj::CSkaterCareer::GetLevel)}, 0, [](Regs& r) {
        Record({0x4130, r.ecx == reinterpret_cast<uint32_t>(g_career)});
        return 17u;
    });
    s.At({0x0057EEC0, AddressOf(&Records::CGameRecords::GetLevelRecords)}, 4, [](Regs& r) {
        Record({0xEEC0, r.ecx == reinterpret_cast<uint32_t>(g_records), r.args[0]});
        return 0x12340000u + r.args[0];
    });
    s.At({0x0057F7B0, AddressOf(&Records::CLevelRecords::WriteToStructure)}, 4, [](Regs& r) {
        Record({0xF7B0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, SkaterIndex(r.ecx)});
        return r.ecx + 0x40;
    });
    s.At({0x00561400, AddressOf(&Mdl::Score::ResetBests)}, 0, [](Regs& r) {
        Record({0x1400, SkaterIndex(r.ecx - 0x40)});
        return 0u;
    });
    s.At({0x005624C0, AddressOf(&Mdl::Score::CountTrickAfterTaps)}, 16, [](Regs& r) {
        Record({0x24C0, SkaterIndex(r.ecx - 0x40), r.args[0], r.args[1], r.args[2], r.args[3]});
        return 7u;
    });
    s.At({0x0048C150, AddressOf(&Pcm::GetNumSoundtracks)}, 0, [](Regs&) {
        Record({0xC150});
        return 5u;
    });
    s.At({0x0048C160, AddressOf(&Pcm::GetSoundtrackName)}, 0, [](Regs& r) {
        Record({0xC160, r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] == 2 ? "Second Soundtrack" : "Other");
    });
    s.At({0x00567090, AddressOf(&Mdl::Skate::BindControllers)}, 0, [](Regs& r) {
        Record({0x7090, r.ecx == reinterpret_cast<uint32_t>(g_skate)});
        return 0u;
    });
    s.At({0x0054FD00, AddressOf(&Mdl::FrontEnd::UpdateInputMappings)}, 0, [](Regs& r) {
        auto* map = reinterpret_cast<int*>(g_frontEnd + 0x28);
        Record({0xFD00, r.ecx == reinterpret_cast<uint32_t>(g_frontEnd), static_cast<uint32_t>(map[0]),
                static_cast<uint32_t>(map[1]), static_cast<uint32_t>(map[2])});
        return 0u;
    });
    s.At({0x00564DA0, AddressOf(&Mdl::Skate::CheckControllers)}, 0, [](Regs& r) {
        Record({0x4DA0, r.ecx == reinterpret_cast<uint32_t>(g_skate)});
        return 0u;
    });
    s.At({0x005BE490, AddressOf(&Sio::Manager::GetDevice)}, 8, [](Regs& r) {
        Record({0xE490, r.ecx == reinterpret_cast<uint32_t>(g_sioManager), r.args[0], r.args[1]});
        return g_haveDevice ? reinterpret_cast<uint32_t>(g_device) : 0u;
    });
    s.At({0x005BDA20, AddressOf(&Sio::Device::ActivateActuator)}, 8, [](Regs& r) {
        Record({0xDA20, r.ecx == reinterpret_cast<uint32_t>(g_device), r.args[0], r.args[1]});
        return 0u;
    });
}

void ResetObjects()
{
    std::memset(g_skate, 0, sizeof(g_skate));
    reinterpret_cast<void**>(g_skate)[0x20 / 4] = g_career;
    std::memset(g_profileManager, 0, sizeof(g_profileManager));
    reinterpret_cast<uint32_t*>(g_profileManager)[0] = 3; // the templates' count
    std::memset(g_profiles, 0, sizeof(g_profiles));
    for (int i = 0; i < 4; ++i)
        std::memcpy(g_profiles[i] + 0x5C, g_profileInfo[i].storage, 8);
    std::memset(g_skaters, 0, sizeof(g_skaters));
    for (int i = 0; i < 10; ++i) {
        reinterpret_cast<void**>(g_skaters[2] + 0x180)[i] = g_created[i];
        reinterpret_cast<CStruct**>(g_created[i])[1] = g_createdInfo[i].Struct();
    }
    std::memset(g_frontEnd, 0, sizeof(g_frontEnd));
    std::memset(g_inputManager, 0, sizeof(g_inputManager));
    GameVariable<void*>(0x007CE478) = g_skate;
    GameVariable<void*>(0x007CE4B4) = g_profileManager;
    GameVariable<void*>(0x00696D40) = g_inputManager;
    GameVariable<void*>(0x007CCE14) = g_netManager;
    GameVariable<void*>(0x006F0FB8) = g_objectManager;
    GameVariable<void*>(0x007CE4BC) = g_records;
    GameVariable<void*>(0x007CE464) = g_frontEnd;
    GameVariable<void*>(0x007D1E20) = g_sioManager;
    g_currentProfile = 0;
    g_inNetGame = false;
    g_localSkater = 2;
    for (int i = 0; i < 8; ++i)
        g_skaterExists[i] = i < 5;
    g_maxSpecials = 4;
    g_haveDevice = true;
    g_catKey = 0;
}

void SetSkaterLocal(int index, bool local) { g_skaters[index][0xBC] = local ? 1 : 0; }
void SetPlayerController(int player, int controller) { reinterpret_cast<int*>(g_inputManager + 0x110)[player] = controller; }

void TestTrickCFuncs()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x007CE4B4, 4, {}}, {0x00696D40, 4, {}}, {0x007CCE14, 4, {}},
                 {0x006F0FB8, 4, {}}, {0x007CE4BC, 4, {}}, {0x007CE464, 4, {}}, {0x007D1E20, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInGameCalls(*standIns);

    // the global trick structures: ConfigurableTricks names 0x7E1C0001..; each with a type and params
    constexpr uint32_t kTrickType = 0x89FA55CE, kScr = 0xA6D2D890, kParams = 0x7031F10C, kIsSpecial = 0x1AEEAA40;
    constexpr uint32_t kGrab = 0xB9AE26D2, kFlip = 0xBA15FC7F, kGrind = 0x6881FCB4, kLip = 0x0A11F00D;
    constexpr uint32_t kName = 0xA1DC81F9, kSkater = 0x5B8AB877, kExtraTricks = 0x6E855102;
    static TestParams trickParams[6], tricks[6], extraParams, extraTrick, items[4];
    static TestArray configurable, extraArray, extraList, itemArray;
    for (TestParams& p : trickParams)
        p.Reset();
    for (TestParams& p : tricks)
        p.Reset();
    trickParams[0].String(kName, "Kickflip").Checksum(kSkater, kProfileNames[1]);
    trickParams[1].String(kName, "Indy").Flag(kIsSpecial).Checksum(kSkater, kProfileNames[2]).Flag(0x7ABFD5B4);
    trickParams[2].String(kName, "Crooked").Checksum(kExtraTricks, 0x0E7A0001);
    trickParams[3].String(kName, "Madonna").Flag(kIsSpecial).Checksum(kSkater, 0x0BADBAD0);
    trickParams[4].String(kName, "Heelflip").Checksum(kSkater, kProfileNames[2]);
    trickParams[5].String(kName, "5-0");
    extraParams.Reset();
    extraParams.String(kName, "Extra Grind");
    extraTrick.Reset();
    extraTrick.Structure(kParams, extraParams.Struct());
    extraList.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(extraTrick.Struct())});
    extraArray.Set(Script::ESYMBOLTYPE_NAME, {0x0E7A0001, 0x0E7A0009});
    tricks[0].Checksum(kTrickType, kFlip).Structure(kParams, trickParams[0].Struct());
    tricks[1].Checksum(kScr, kGrab).Structure(kParams, trickParams[1].Struct());
    tricks[2].Structure(kParams, trickParams[2].Struct()); // a grind trick
    tricks[3].Checksum(kTrickType, kGrab).Structure(kParams, trickParams[3].Struct());
    tricks[4].Checksum(kTrickType, kFlip).Checksum(kScr, kLip).Structure(kParams, trickParams[4].Struct());
    tricks[5].Checksum(kScr, kGrind).Structure(kParams, trickParams[5].Struct());
    configurable.Set(Script::ESYMBOLTYPE_NAME, {0x7E1C0001, 0x7E1C0002, 0x7E1C0003, 0x7E1C0099, 0x7E1C0004,
                                                0x7E1C0005, 0x7E1C0006});
    // the profiles' info (is_hidden), the created tricks' (full)
    constexpr uint32_t kIsHidden = 0x27EB9B9D, kFull = 0x1F802B5F;
    for (int i = 0; i < 4; ++i)
        g_profileInfo[i].Reset();
    g_profileInfo[1].Integer(kIsHidden, 0);
    g_profileInfo[2].Integer(kIsHidden, 3);
    for (int i = 0; i < 10; ++i) {
        g_createdInfo[i].Reset();
        if (i % 3 == 0)
            g_createdInfo[i].Integer(kFull, i == 6 ? 0 : 1);
        else if (i % 3 == 1)
            g_createdInfo[i].Integer(0x0BAD0001, 1);
    }
    // an array of items for GetIndexOfItemContaining
    constexpr uint32_t kItemName = 0x17E10001;
    for (TestParams& p : items)
        p.Reset();
    items[0].Checksum(kItemName, 0x5A5A0001);
    items[1].Integer(0x17E10002, 4);
    items[2].Checksum(kItemName, 0x5A5A0002);
    items[3].Checksum(kItemName, 0x5A5A0002);
    itemArray.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(items[0].Struct()),
                                                  reinterpret_cast<uint32_t>(items[1].Struct()),
                                                  reinterpret_cast<uint32_t>(items[2].Struct()),
                                                  reinterpret_cast<uint32_t>(items[3].Struct())});

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetObjects();
        UseStandInArrayPool();
        g_globalStructs.clear();
        g_globalArrays.clear();
        for (uint32_t i = 0; i < 6; ++i)
            g_globalStructs[0x7E1C0001 + i] = tricks[i].Struct();
        g_globalArrays[0x5004A967] = configurable.Get();
        g_globalArrays[0x0E7A0001] = extraList.Get();
        g_globalArrays[0x17E1A000] = itemArray.Get();
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        using namespace CFuncs;
        TestParams none;
        call(0x005B0A40, CFunc_FlushDeadObjects, none);

        // BindTrickToKeyCombo
        constexpr uint32_t kKeyCombo = 0xACFDB27A, kTrick = 0x270F56E1, kProfile = 0x7EA855F0;
        constexpr uint32_t kSpecial = 0xB394C01C, kIndex = 0x7F8C98FE, kCat = 0x61A1BC57, kUpdate = 0xC2D1F9A6;
        {
            TestParams a; a.Checksum(kKeyCombo, 0x4E700005).Checksum(kTrick, 0x7E1C0004);
            call(0x005B0A50, CFunc_BindTrickToKeyCombo, a);
            TestParams b; b.Checksum(kKeyCombo, 0x4E700006).Integer(kTrick, 3).Checksum(kProfile, kProfileNames[2]);
            call(0x005B0A50, CFunc_BindTrickToKeyCombo, b);
            TestParams c; c.Checksum(kKeyCombo, 0x4E700007).Integer(kTrick, 5).Flag(kSpecial).Integer(kIndex, 2)
                .Flag(kCat).Integer(Crc::GenerateCRCFromString("update_mappings"), 0);
            call(0x005B0A50, CFunc_BindTrickToKeyCombo, c);
            TestParams d; d.Checksum(kKeyCombo, 0x4E700008).Checksum(kTrick, 0x7E1C0002).Flag(kSpecial);
            call(0x005B0A50, CFunc_BindTrickToKeyCombo, d);
            TestParams e; e.Checksum(kKeyCombo, 0x4E700009).Checksum(kTrick, 0x7E1C0002).Flag(kSpecial)
                .Integer(kIndex, 1).Integer(Crc::GenerateCRCFromString("update_mappings"), 7)
                .Checksum(kProfile, kProfileNames[0]);
            call(0x005B0A50, CFunc_BindTrickToKeyCombo, e);
            TestParams f; f.Checksum(kKeyCombo, 0x4E70000E).Checksum(kTrick, 0x7E1C0005).Flag(kSpecial).Integer(kIndex, 3);
            call(0x005B0A50, CFunc_BindTrickToKeyCombo, f); // update_mappings defaults to 1
            (void)kUpdate;
        }
        // GetKeyComboBoundToTrick
        constexpr uint32_t kCatNum = 0xA75B8581;
        {
            g_mapping.Reset();
            g_mapping.Checksum(0x4E700001, 0x7E1C0001).Checksum(0x4E700002, 0x7E1C0005);
            g_specials[0] = {0x7E1C0008, 0x4E70000A, false, {}};
            g_specials[1] = {0x7E1C0003, 0x4E70000B, true, {}};
            g_specials[2] = {0x7E1C0003, 0x4E70000C, false, {}};
            g_specials[3] = {0, 0, false, {}};
            TestParams a; a.Checksum(kTrick, 0x7E1C0001);
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, a);
            TestParams b; b.Checksum(kTrick, 0x7E1C0002).Checksum(kProfile, kProfileNames[2]);
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, b); // bound to another trick
            TestParams c; c.Checksum(kTrick, 0x7E1C0004);
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, c); // none
            TestParams d; d.Checksum(kTrick, 0x7E1C0003).Flag(kSpecial);
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, d); // slot 1
            TestParams e; e.Checksum(kTrick, 0x7E1C0009).Flag(kSpecial);
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, e); // no slot
            g_maxSpecials = 0;
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, d); // no slots
            g_maxSpecials = 4;
            TestParams f; f.Integer(kCatNum, 6);
            g_catKey = 0x4E70000D;
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, f);
            g_catKey = 0;
            call(0x005B0C00, CFunc_GetKeyComboBoundToTrick, f);
        }
        // UpdateTrickMappings
        {
            TestParams a; a.Integer(kSkater, 1);
            call(0x005B0DA0, CFunc_UpdateTrickMappings, a);
            TestParams b; b.Integer(kSkater, 3); // a skater without a profile
            call(0x005B0DA0, CFunc_UpdateTrickMappings, b);
            TestParams c; c.Integer(kSkater, 6); // no skater
            call(0x005B0DA0, CFunc_UpdateTrickMappings, c);
            g_inNetGame = true;
            call(0x005B0DA0, CFunc_UpdateTrickMappings, a);
            g_inNetGame = false;
        }
        // GetConfigurableTricksFromType
        constexpr uint32_t kType = 0x7321A8D6;
        {
            for (uint32_t type : {kGrab, kFlip, kGrind, kLip, 0u}) {
                for (bool special : {false, true}) {
                    TestParams a;
                    if (type)
                        a.Checksum(kType, type);
                    if (special)
                        a.Flag(kSpecial);
                    UseStandInArrayPool();
                    call(0x005B0E30, CFunc_GetConfigurableTricksFromType, a);
                }
            }
            g_localSkater = 9;
            TestParams a; a.Checksum(kType, kFlip);
            UseStandInArrayPool();
            call(0x005B0E30, CFunc_GetConfigurableTricksFromType, a);
            g_localSkater = 2;
        }
        // TrickIsLocked
        {
            for (uint32_t trick : {0x7E1C0001u, 0x7E1C0002u, 0x7E1C0004u, 0x7E1C0005u, 0x7E1C0006u, 0x7E1C0077u}) {
                TestParams a; a.Checksum(kTrick, trick);
                call(0x005B1140, CFunc_TrickIsLocked, a);
            }
            g_currentProfile = 2;
            TestParams b; b.Checksum(kTrick, 0x7E1C0002);
            call(0x005B1140, CFunc_TrickIsLocked, b);
            g_currentProfile = 0;
        }
        // GetTrickDisplayText
        {
            for (uint32_t trick : {0x7E1C0001u, 0x7E1C0003u, 0x7E1C0077u}) {
                TestParams a; a.Checksum(kTrick, trick);
                call(0x005B1250, CFunc_GetTrickDisplayText, a);
            }
            static TestParams arrayParams, arrayTrick;
            arrayParams.Reset();
            arrayParams.String(kName, "Smith").Array(kExtraTricks, extraArray.Get());
            arrayTrick.Reset();
            arrayTrick.Structure(kParams, arrayParams.Struct());
            g_globalStructs[0x7E1C00AA] = arrayTrick.Struct();
            TestParams b; b.Checksum(kTrick, 0x7E1C00AA);
            call(0x005B1250, CFunc_GetTrickDisplayText, b);
        }
        // GetSpecialTrickInfo
        for (int index : {1, 2, 3}) {
            TestParams a; a.Integer(kIndex, index);
            call(0x005B1380, CFunc_GetSpecialTrickInfo, a);
        }
        // GetTrickType
        for (uint32_t trick : {0x7E1C0001u, 0x7E1C0002u, 0x7E1C0003u, 0x7E1C0005u}) {
            TestParams a; a.Checksum(kTrick, trick);
            call(0x005B1420, CFunc_GetTrickType, a);
        }
        // GetIndexOfItemContaining
        {
            constexpr uint32_t kArray = 0x5EF31148, kValue = 0xE288A7CB;
            for (int start : {-1, 0, 1, 3, 4}) {
                for (uint32_t value : {0x5A5A0001u, 0x5A5A0002u, 0x5A5A0003u}) {
                    TestParams a; a.Checksum(kArray, 0x17E1A000).Checksum(kName, kItemName).Checksum(kValue, value);
                    if (start >= 0)
                        a.Integer(kIndex, start);
                    call(0x005B14A0, CFunc_GetIndexOfItemContaining, a);
                }
            }
        }
        // GetLevelRecords
        {
            TestParams a; a.Integer(0x651533EC, 9);
            call(0x005B1590, CFunc_GetLevelRecords, a);
            call(0x005B1590, CFunc_GetLevelRecords, none);
        }
        // ResetComboRecords, GetNumberOfTrickOccurrences
        {
            call(0x005B15F0, CFunc_ResetComboRecords, none);
            g_localSkater = 9;
            call(0x005B15F0, CFunc_ResetComboRecords, none);
            g_localSkater = 3;
            TestParams a; a.String(0x3EAFA520, "Kickflip to Indy");
            call(0x005B1610, CFunc_GetNumberOfTrickOccurrences, a);
            g_localSkater = 2;
        }
        // soundtracks
        {
            call(0x005B1670, CFunc_GetNumSoundtracks, none);
            TestParams a; a.Integer(0, 2);
            call(0x005B1690, CFunc_GetSoundtrackName, a);
            TestParams b; b.Integer(0, 4);
            call(0x005B1690, CFunc_GetSoundtrackName, b);
        }
        // controllers
        constexpr uint32_t kController = 0xB30D9965;
        {
            TestParams a; a.Integer(kController, 3).Integer(0xC52D1A41, 1);
            call(0x005B16D0, CFunc_BindControllerToSkater, a);
            Record({static_cast<uint32_t>(reinterpret_cast<int*>(g_inputManager + 0x110)[0]),
                    static_cast<uint32_t>(reinterpret_cast<int*>(g_inputManager + 0x110)[1])});
            int* map = reinterpret_cast<int*>(g_frontEnd + 0x28);
            int cases[][4] = {{0, 1, 0, 1}, {0, 1, 1, 0}, {4, 5, 0, 5}, {4, 5, 1, 4}, {2, 2, 0, 2}, {6, 7, 0, 3}, {6, 7, 1, 3}};
            for (auto& c : cases) {
                map[0] = c[0];
                map[1] = c[1];
                map[2] = 99;
                TestParams b; b.Integer(kController, c[3]).Integer(0xCDC3D2FD, c[2]);
                call(0x005B1730, CFunc_BindFrontEndToController, b);
            }
            for (int i = 0; i < 8; ++i) {
                SetSkaterLocal(i, i != 1 && i != 4);
                SetPlayerController(i, i == 3 ? 2 : 10 + i);
            }
            SetPlayerController(1, 5);
            SetPlayerController(4, 5);
            SetPlayerController(6, 7);
            for (int skater : {0, 3, 2}) {
                for (int controller : {2, 5, 7, 13}) {
                    TestParams b; b.Integer(kSkater, skater).Integer(kController, controller);
                    call(0x005B17C0, CFunc_ControllerBoundToDifferentSkater, b);
                    call(0x005B1840, CFunc_ControllerBoundToSkater, b);
                }
            }
        }
        // GetKeyComboArrayFromTrickArray
        {
            static TestArray trickList;
            trickList.Set(Script::ESYMBOLTYPE_NAME, {0x7E1C0003, 0x7E1C0001, 0x7E1C0009, 0x7E1C0002, 0x7E1C0008});
            TestParams a; a.Array(0x1E26FD3E, trickList.Get());
            UseStandInArrayPool();
            call(0x005B1890, CFunc_GetKeyComboArrayFromTrickArray, a);
            g_maxSpecials = 1;
            UseStandInArrayPool();
            call(0x005B1890, CFunc_GetKeyComboArrayFromTrickArray, a);
            g_maxSpecials = 4;
        }
        // FirstInputReceived, VibrateController, LockCurrentSkaterProfileIndex, SetSpecialTrickInfo
        {
            call(0x005B1A00, CFunc_FirstInputReceived, none);
            TestParams a; a.Integer(0xBC6EA233, 1).Integer(0x114FC131, 2).Integer(0x9E497FC6, 75);
            call(0x005B1A10, CFunc_VibrateController, a);
            g_haveDevice = false;
            call(0x005B1A10, CFunc_VibrateController, a);
            TestParams b; b.Integer(0, 0);
            call(0x005B1A90, CFunc_LockCurrentSkaterProfileIndex, b);
            TestParams c; c.Integer(0, 5);
            call(0x005B1A90, CFunc_LockCurrentSkaterProfileIndex, c);
            call(0x005B1A90, CFunc_LockCurrentSkaterProfileIndex, none);
            TestParams d; d.Integer(0x53F1DF98, 3).Checksum(0xEF6FB249, 0x7E1C0006).Checksum(kKeyCombo, 0x4E70000F);
            call(0x005B1AD0, CFunc_SetSpecialTrickInfo, d);
            g_currentProfile = 9;
            call(0x005B1AD0, CFunc_SetSpecialTrickInfo, d);
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "configurable tricks, records, soundtracks, controllers (0x005B0A40-0x005B1B60)");
}

} // namespace

SELFTEST(Early, TestTrickCFuncs, Gs4Tests::RestoreGlobals);
