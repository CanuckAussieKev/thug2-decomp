// Self-tests for the player profile manager (src/game/skate/profile_manager.cpp): the original and ours run the same
// operations on an identical manager in an arena. The lookup table's own code (0x00557F10 / 0x00561430 /
// 0x005628B0 / 0x00562970) runs for real (its nodes from the stand-in memory manager); the profiles are stand-ins
// (their name, is_pro, display name, specials, max_specials and stats in their first words, read and written by
// recording stand-ins for the profile's methods); structures, scripts, memory and debug output are recorded
// (viewer_testkit.h), and the calls and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/profile_manager.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/model_appearance.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CPlayerProfileManager;
using Obj::CSkaterProfile;

// Arena layout.
constexpr uint32_t kManager = 0x0000;  // 0x40
constexpr uint32_t kParams = 0x0100;   // template parameters, 0x10 apart: [0] the name, [1] is_pro
constexpr uint32_t kSave = 0x0200;
constexpr uint32_t kCustom = 0x0240;   // the save's custom structure
constexpr uint32_t kAppearance = 0x0260;
constexpr uint32_t kInfo = 0x0280;
constexpr uint32_t kFace = 0x02A0;
constexpr uint32_t kTexture = 0x02C0;
constexpr uint32_t kSpecials = 0x0400; // each profile's specials structure, 0x10 apart
constexpr uint32_t kText = 0x0600;

// The stand-in profile's words.
constexpr uint32_t kProfileName = 0x00;
constexpr uint32_t kProfilePro = 0x04;
constexpr uint32_t kProfileDisplay = 0x08;
constexpr uint32_t kProfileSpecials = 0x0C;
constexpr uint32_t kProfileMaxSpecials = 0x10;
constexpr uint32_t kProfileStats = 0x14; // 10 words

constexpr uint32_t kStats[] = {0x439F4704, 0xAF895B3F, 0x9B65D7B8, 0xF0D90109, 0xEDF5DB70,
                               0x6DCB497C, 0x9016B4E7, 0xF73A13E3, 0xAE798769, 0xB1FC0722};
constexpr uint32_t kMaxSpecials = 0xAC9B9EDA;
constexpr uint32_t kCustomName = 0x0A7BE964;

int g_profilesMade;

uint32_t& Word(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint32_t*>(address + offset); }

int StatIndex(uint32_t name)
{
    for (int i = 0; i < 10; ++i) {
        if (kStats[i] == name)
            return i;
    }
    return -1;
}

void Install(StandIns& s)
{
    InstallCommon(s, true);
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
        uint32_t block = Allocate(r.args[0]);
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
    auto getter = [](Regs& r, uint32_t tag) {
        Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
        auto it = g_params.find({r.ecx, r.args[0]});
        if (it == g_params.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [getter](Regs& r) { return getter(r, 0x6950); });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         12, [getter](Regs& r) { return getter(r, 0x6630); });
    s.At({0x004767C0, 0x00476E70, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, [getter](Regs& r) { return getter(r, 0x67C0); });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    // The profile.
    s.At({0x00586D60, AddressOf(&CSkaterProfile::Construct)}, 0, [](Regs& r) {
        Rec(0x6D60, {r.ecx});
        Word(r.ecx, kProfileDisplay) = At(kText);
        Word(r.ecx, kProfileSpecials) = At(kSpecials + 0x10 * g_profilesMade);
        Word(r.ecx, kProfileMaxSpecials) = 3 + g_profilesMade;
        for (int i = 0; i < 10; ++i)
            Word(r.ecx, kProfileStats + 4 * i) = static_cast<uint32_t>(i + g_profilesMade);
        ++g_profilesMade;
        return r.ecx;
    });
    s.At({0x00586150, AddressOf(&CSkaterProfile::GetName)}, 0, [](Regs& r) {
        Rec(0x6150, {r.ecx});
        return Word(r.ecx, kProfileName);
    });
    s.At({0x00586170, AddressOf(&CSkaterProfile::IsPro)}, 0, [](Regs& r) {
        Rec(0x6170, {r.ecx});
        return Word(r.ecx, kProfilePro);
    });
    s.At({0x00586210, AddressOf(&CSkaterProfile::GetDisplayName)}, 0, [](Regs& r) {
        Rec(0x6210, {r.ecx});
        return Word(r.ecx, kProfileDisplay);
    });
    s.At({0x00586130, AddressOf(&CSkaterProfile::GetSpecials)}, 0, [](Regs& r) {
        Rec(0x6130, {r.ecx});
        return Word(r.ecx, kProfileSpecials);
    });
    s.At({0x005860A0, AddressOf(&CSkaterProfile::GetMaxSpecials)}, 0, [](Regs& r) {
        Rec(0x60A0, {r.ecx});
        return Word(r.ecx, kProfileMaxSpecials);
    });
    s.At({0x00585FB0, AddressOf(&CSkaterProfile::GetStat)}, 4, [](Regs& r) {
        Rec(0x5FB0, {r.ecx, r.args[0]});
        int i = StatIndex(r.args[0]);
        return i < 0 ? 0x77u : Word(r.ecx, kProfileStats + 4 * i);
    });
    s.At({0x00585FD0, AddressOf(&CSkaterProfile::SetStat)}, 8, [](Regs& r) {
        Rec(0x5FD0, {r.ecx, r.args[0], r.args[1]});
        int i = StatIndex(r.args[0]);
        if (i >= 0)
            Word(r.ecx, kProfileStats + 4 * i) = r.args[1];
        else if (r.args[0] == kMaxSpecials)
            Word(r.ecx, kProfileMaxSpecials) = r.args[1];
        return r.args[1] <= 10 ? 1u : 0u;
    });
    s.At({0x00585ED0, AddressOf(&CSkaterProfile::CopyFrom)}, 4, [](Regs& r) {
        Rec(0x5ED0, {r.ecx, r.args[0]});
        if (r.args[0] && r.args[0] != r.ecx)
            std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 0x3C);
        return r.ecx;
    });
    s.At({0x00585F00, AddressOf(&CSkaterProfile::ReadFromSave)}, 8, [](Regs& r) {
        Rec(0x5F00, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00586B00, AddressOf(&CSkaterProfile::WriteToSave)}, 4, [](Regs& r) {
        Rec(0x6B00, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00586810, AddressOf(&CSkaterProfile::Reset)}, 4, [](Regs& r) {
        Rec(0x6810, {r.ecx, r.args[0]});
        if (r.args[0]) {
            Word(r.ecx, kProfileName) = Word(r.args[0], 0);
            Word(r.ecx, kProfilePro) = Word(r.args[0], 4);
        }
        return 0u;
    });
    s.At({0x00488F40, AddressOf(&Gfx::CModelAppearance::Load)}, 8, [](Regs& r) {
        Rec(0x8F40, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x004890F0, AddressOf(&Gfx::CModelAppearance::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x90F0, {r.ecx});
        return 0u;
    });
    s.At({0x004875F0}, 4, [](Regs& r) {
        Rec(0x75F0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00487520, AddressOf(&Gfx::CFaceTexture::SetValid)}, 4, [](Regs& r) {
        Rec(0x7520, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
}

void PushManager(std::vector<uint32_t>& t, CPlayerProfileManager* m)
{
    t.push_back(0x9A9A);
    for (int i = 0; i < 0x10; ++i)
        t.push_back(Rel(reinterpret_cast<uint32_t*>(m)[i]));
}

// The singleton made, counted and released; templates added (pros and custom skaters), found by name and index;
// Init's profiles; the current index, the lock, the extra profiles; the current skater set for each player; the
// second profile reset; the templates reset; the saves written and read (with and without a current skater); the
// custom skater read (no custom structure, an old version, version 2 with and without a face, a face texture, the
// deck flags, the stats kept or put back); the destructor and the last release.
void TestProfileManager()
{
    SaveGlobals({{0x007CE4B4, 4, {}}, {0x007CE4B8, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_profilesMade = 0;
        g_printfArgs = 0;
        std::vector<uint32_t>& t = traces[run];
        std::memcpy(Ptr<char>(kText), "Tony Hawk", sizeof("Tony Hawk"));
        // The singleton.
        GameVariable<uint32_t>(0x007CE4B4) = 0;
        GameVariable<uint32_t>(0x007CE4B8) = 0;
        auto create = [&](bool make) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__cdecl*)(bool)>(0x0057C0D0)(make)
                                                               : CPlayerProfileManager::Create(make))));
            t.push_back(GameVariable<uint32_t>(0x007CE4B8));
        };
        auto release = [&]() {
            if (original)
                Original<void(__cdecl*)()>(0x0057C120)();
            else
                CPlayerProfileManager::Release();
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE4B4)));
            t.push_back(GameVariable<uint32_t>(0x007CE4B8));
        };
        create(false);
        release();
        create(true);
        create(true);
        release();
        auto* m = GameVariable<CPlayerProfileManager*>(0x007CE4B4);
        PushManager(t, m);
        // A second one, constructed over 0xCD.
        auto* other = Ptr<CPlayerProfileManager>(0x0080);
        std::memset(other, 0xCD, sizeof(CPlayerProfileManager));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057BC00)(other)
                                                           : other->Construct())));
        PushManager(t, other);
        // Templates: two pros, the custom skater, another custom one.
        const uint32_t names[4] = {0x0000A001, 0x0000A002, kCustomName, 0x0000C002};
        for (int i = 0; i < 4; ++i) {
            uint32_t params = At(kParams + 0x10 * i);
            Word(params, 0) = names[i];
            Word(params, 4) = i < 2 ? 1u : 0u;
            g_params[{params, 0xA1DC81F9}] = names[i];
            t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*)>(0x0057BB40)(m, reinterpret_cast<CStruct*>(params))
                                 : m->AddTemplate(reinterpret_cast<CStruct*>(params)));
        }
        for (uint32_t name : {0x0000A001u, 0x0000C002u, kCustomName, 0x0000D00Du})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x0057B7E0)(m, name)
                                                               : m->GetTemplate(name))));
        for (int i : {3, 0, 2, 1})
            t.push_back(original ? Original<uint32_t(__thiscall*)(void*, int)>(0x0057B770)(m, i) : m->GetTemplateName(i));
        // Init.
        if (original)
            Original<void(__thiscall*)(void*)>(0x0057B690)(m);
        else
            m->Init();
        PushManager(t, m);
        auto profiles = [&]() {
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057B630)(m)
                                                               : m->GetCurrentProfile())));
            for (int i : {0, 1})
                t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int)>(0x0057B640)(m, i)
                                                                   : m->GetProfile(i))));
        };
        auto setIndex = [&](int index) {
            if (original)
                Original<void(__thiscall*)(void*, int)>(0x0057B620)(m, index);
            else
                m->SetCurrentIndex(index);
            profiles();
        };
        profiles();
        setIndex(1);
        for (bool locked : {true, false}) {
            if (original)
                Original<void(__thiscall*)(void*, bool)>(0x0057B680)(m, locked);
            else
                m->SetLocked(locked);
            t.push_back(Word(reinterpret_cast<uint32_t>(m), 0x20));
        }
        // Extras.
        for (uint32_t name : {0xE1u, 0xE2u})
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057B970)(m, name) : m->AddExtraProfile(name));
        for (uint32_t name : {0xE2u, 0xE1u, 0xE3u})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x0057B650)(m, name)
                                                               : m->GetExtraProfile(name))));
        // The current skater, for each player.
        auto setSkater = [&](uint32_t name) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t)>(0x0057BA20)(m, name);
            else
                m->SetCurrentSkater(name);
            profiles();
        };
        setSkater(0x0000A002);
        setIndex(0);
        setSkater(0x0000C002);
        setIndex(1);
        auto voidCall = [&](uint32_t address, void (CPlayerProfileManager::*ours)()) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(m);
            else
                (m->*ours)();
        };
        voidCall(0x0057B790, &CPlayerProfileManager::ResetSecondProfile);
        voidCall(0x0057B800, &CPlayerProfileManager::ResetTemplates);
        // Saves.
        auto* save = Ptr<CStruct>(kSave);
        auto withSave = [&](uint32_t address, void (CPlayerProfileManager::*ours)(CStruct*)) {
            if (original)
                Original<void(__thiscall*)(void*, CStruct*)>(address)(m, save);
            else
                (m->*ours)(save);
        };
        withSave(0x0057B840, &CPlayerProfileManager::WritePros);
        withSave(0x0057B920, &CPlayerProfileManager::WriteCustomSkaters);
        withSave(0x0057BAA0, &CPlayerProfileManager::ReadPros);
        g_params[{kSave + g_base, 0x75AC23EF}] = 0x0000A001;
        setIndex(0);
        withSave(0x0057BAA0, &CPlayerProfileManager::ReadPros);
        profiles();
        // The custom skater.
        auto readCustom = [&](bool keep) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*, bool)>(0x0057BD70)(m, save, keep)
                                 : m->ReadCustomSkater(save, keep));
            profiles();
            CSkaterProfile* current = m->m_profiles[m->m_currentIndex];
            for (int i = 0; i < 0x10; ++i)
                t.push_back(Rel(reinterpret_cast<uint32_t*>(current)[i]));
        };
        setIndex(1);
        g_params[{kSave + g_base, kCustomName}] = At(kCustom);
        readCustom(false); // no version
        g_params[{At(kCustom), 0x1902C8F0}] = 1;
        readCustom(false); // an old version
        g_params[{At(kCustom), 0x1902C8F0}] = 2;
        g_params[{At(kCustom), 0x554C7D6F}] = At(kAppearance);
        g_params[{At(kCustom), 0x3476CEA8}] = At(kInfo);
        readCustom(false); // no face; the stats put back
        Word(reinterpret_cast<uint32_t>(m->m_profiles[1]), 0x54) = At(kFace);
        for (int i = 0; i < 10; ++i)
            Word(reinterpret_cast<uint32_t>(m->m_profiles[1]), kProfileStats + 4 * i) = 0x40 + i;
        readCustom(true); // a face, no texture; the save's stats kept
        g_params[{At(kCustom), 0xF0D3BC94}] = At(kTexture);
        g_params[{At(kCustom), 0x95388A60}] = 6;
        setIndex(0);
        Word(reinterpret_cast<uint32_t>(m->GetTemplate(kCustomName)), 0x54) = At(kFace);
        readCustom(false); // the first player: the template itself; a face texture; deck flags
        // The destructors.
        voidCall(0x0057BC20, &CPlayerProfileManager::DestroyBody);
        PushManager(t, m);
        other->m_templates.m_count = 5; // (no nodes, but a count: cleared)
        if (original)
            Original<void(__thiscall*)(void*)>(0x0057BBF0)(&other->m_templates);
        else
            other->m_templates.Destroy();
        PushManager(t, other);
        // A template again: the last release deletes it.
        t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*)>(0x0057BB40)(m, reinterpret_cast<CStruct*>(At(kParams)))
                             : m->AddTemplate(reinterpret_cast<CStruct*>(At(kParams))));
        create(false);
        release();
        release();
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "profile manager");
}

} // namespace

SELFTEST(Early, TestProfileManager, ViewerTestKit::Cleanup);
