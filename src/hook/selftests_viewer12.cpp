// Self-tests for the skater's set-up (src/game/skater/skater.cpp, CSkater::Init 0x00581850): the original and ours set
// up a stand-in skater in an arena; what it calls outside (the camera, components made from structures, the asset
// manager, the profile and stats pieces, scripts...) is a recording stand-in, and the calls and the whole arena are
// compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkater;

// Arena layout.
constexpr uint32_t kSkater = 0x0000;   // 0x324
constexpr uint32_t kProfile = 0x0400;
constexpr uint32_t kCorePhys = 0x0500;
constexpr uint32_t kBlocks = 0x2000;   // small blocks, 0x100 apart
constexpr uint32_t kSkate = 0x5000;    // the skate module (+0x394.. pad settings)

uint32_t Block(int i) { return At(kBlocks + 0x100 * i); }

void* g_skaterVtable[24];
void* g_modelVtable[16];
bool g_cameraFound, g_assetInfo, g_noCollisionData, g_customTag, g_sticker;

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0x7149B753: return Block(1);  // animblender
    case 0x286A8D26: return Block(2);  // model
    case 0x8A897DD2: return Block(3);  // shadow
    case 0x404EB936: return Block(4);  // vibration
    case 0x187574FA: return Block(5);  // skatersound
    case 0x1859F772: return Block(6);
    case 0xA432DC41: return Block(7);  // collision
    case 0x5B512EDC: return Block(8);
    case 0x726E85AA: return Block(9);  // Walk
    case 0x5E43A604: return Block(10); // (the camera's) skatercamera
    case 0xC7D37EC2: return Block(11); // walkcamera
    default: return 0u;
    }
}

void Simple(StandIns& s, std::initializer_list<uint32_t> addresses, int pop, uint32_t answer = 0, bool block = false)
{
    uint32_t tag = *addresses.begin() & 0xFFFFF;
    s.At(addresses, static_cast<uint16_t>(pop), [tag, pop, answer, block](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return block ? Block(static_cast<int>(answer)) : answer;
    });
}

float __fastcall ScaledStandIn(void* self, void*, uint32_t name)
{
    Rec(0x0790, {reinterpret_cast<uint32_t>(self), name});
    return 12.5f;
}

void Install(StandIns& s)
{
    InstallCommon(s);
    for (uint32_t address : {0x00580790u, AddressOf(&CSkater::GetScaledPhysics)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&ScaledStandIn)));
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        uint32_t c = ComponentFor(r.args[0]);
        return r.args[0] == 0xA432DC41 && g_noCollisionData ? Block(12) : c;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return g_cameraFound ? Block(13) : 0u;
    });
    s.At({0x0045F3A0, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObjectFromNode)}, 12, [](Regs& r) {
        Rec(0xF3A0, {r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF});
        return Block(14);
    });
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9090, {Text(r.args[0])});
        return Block(15);
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        return g_assetInfo ? Block(16) : 0u;
    });
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) {
             Rec(0x6950, {r.ecx, r.args[0], r.args[2] & 0xFF});
             *reinterpret_cast<uint32_t*>(r.args[1]) = 0x5E1E7011;
             return 1u;
         });
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [](Regs& r) {
             Rec(0x6590, {r.ecx, r.args[0], r.args[2] & 0xFF});
             if (r.args[0] == 0x130618A0 && !g_sticker)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = Block(17) + (r.args[0] & 0x30);
             return 1u;
         });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Rec(0x6630, {r.ecx, r.args[0], r.args[2] & 0xFF});
             *reinterpret_cast<int*>(r.args[1]) = g_customTag ? 1 : 0;
             return 1u;
         });
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Rec(0x38E0, {r.ecx});
        return r.ecx;
    });
    s.At({0x004747C0, AddressOf(static_cast<void (Script::CScript::*)(const char*, CStruct*, Obj::CObject*)>(&Script::CScript::SetScript))},
         12, [](Regs& r) {
             Rec(0x47C0, {r.ecx, Text(r.args[0]), r.args[1], r.args[2]});
             return 0u;
         });
    s.At({0x00407A50}, 4, [](Regs& r) {
        Rec(0x7A50, {r.ecx, r.args[0] & 0xFF}); // (a bool: the original passes a reused stack word)
        return 0u;
    });
    Simple(s, {0x0045E450, AddressOf(static_cast<bool (Obj::CCompositeObject::*)(uint32_t, CStruct*, Script::CScript*)>(&Obj::CCompositeObject::CallMemberFunction))}, 12, 1);
    s.At({0x0057B2F0, AddressOf(&Obj::AddMotionComponents)}, 0, [](Regs& r) {
        Rec(0xB2F0, {r.args[0]});
        return 0u;
    });
    Simple(s, {0x00580EB0, AddressOf(&CSkater::UpdateFromProfile)}, 4);
    Simple(s, {0x00580BE0, AddressOf(&CSkater::SetStatsFromProfile)}, 4);
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    for (uint32_t address : {0x004FA3C0u, 0x00524C50u, 0x00525E60u, 0x00419B30u, 0x0043D560u, 0x00522410u,
                             0x0051D9C0u, 0x0051DA20u})
        Simple(s, {address}, 4);
    for (uint32_t address : {0x004F9AE0u, 0x00419A90u})
        Simple(s, {address}, 8);
    for (uint32_t address : {0x0051EB00u, 0x0051ED10u, 0x0051EB30u, 0x0051EF00u, 0x00420480u, 0x0051D830u})
        Simple(s, {address}, 0);
    Simple(s, {0x00407A60}, 0, 1);
    s.At({0x005480F0}, 0, [](Regs& r) {
        Rec(0x80F0, {r.ecx});
        return r.ecx == Block(12) ? 0u : Block(18);
    });
    s.At({0x00552560}, 0, [](Regs& r) {
        Rec(0x2560, {r.ecx});
        return r.ecx;
    });
    s.At({0x0059F890}, 0, [](Regs& r) {
        Rec(0xF890, {r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0042E380}, 0, [](Regs& r) {
        Rec(0xE380, {r.args[0]});
        return 0u;
    });
    // The skater's vtable: Finalize (+0x20), CreateComponentFromStructure (+0x24); the model's +0x34.
    g_skaterVtable[8] = s.Thunk(0, [](Regs& r) {
        Rec(0x5120, {r.ecx});
        return 0u;
    });
    g_skaterVtable[9] = s.Thunk(8, [](Regs& r) {
        Rec(0x5124, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    g_modelVtable[0x34 / 4] = s.Thunk(16, [](Regs& r) {
        Rec(0x3434, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
}

// Init for a local skater (its camera found and not, with and without the asset info, the collision's data, the
// custom skater's own tag or not, a sticker) and one that isn't local.
void TestSkaterInit()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x006918DC, 4, {}}, {0x006F0FB8, 4, {}}, {0x006F3548, 16, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CE478) = At(kSkate);
        GameVariable<uint32_t>(0x006918DC) = Block(20);
        GameVariable<uint32_t>(0x006F0FB8) = Block(21);
        std::memset(reinterpret_cast<void*>(0x006F3548), 0, 16);
        for (int i = 0; i < 0x20; ++i)
            *Ptr<uint8_t>(kSkate + 0x394 + i) = static_cast<uint8_t>(0x30 + i);
        for (int i = 0; i < 8; ++i) {
            *Ptr<uint32_t>(0x7000 + 0x1C0 + 4 * i) = 0x7C000000u + i; // the skater geometry heaps
            *Ptr<uint32_t>(0x7000 + 0x5D4) = 0x7B000000u;              // the skater info heap
        }
        *Ptr<void**>(kBlocks + 0x100 * 2) = g_modelVtable;
        struct Case {
            bool local, camera, assetInfo, noCollisionData, custom, customTag, sticker;
            int viewport;
            bool with258, with270;
        };
        const Case cases[] = {{true, true, true, false, false, false, true, 0, true, true},
                              {true, false, false, true, true, true, false, 1, false, false},
                              {true, false, true, false, true, false, true, 1, true, false},
                              {false, false, false, false, false, false, false, 0, false, true}};
        for (const Case& c : cases) {
            auto* skater = Ptr<CSkater>(kSkater);
            std::memset(skater, 0, sizeof(CSkater));
            skater->m_vtable = g_skaterVtable;
            skater->m_id = 0x51;
            skater->m_unknownBC = c.local ? 1 : 0;
            skater->m_viewport = c.viewport;
            skater->m_skaterNumber = 3;
            skater->m_name = c.custom ? 0x0A7BE964u : 0x4A3Eu;
            skater->m_corePhysics = Ptr<uint8_t>(kCorePhys);
            skater->m_scoreHolder = Ptr<uint8_t>(kBlocks + 0x100 * 22);
            *Ptr<uint32_t>(kBlocks + 0x100 * 22 + 0x18) = Block(23);
            skater->m_unknown258 = c.with258 ? Ptr<void>(kBlocks + 0x100 * 24) : nullptr;
            skater->m_unknown270 = c.with270 ? Ptr<uint8_t>(kBlocks + 0x100 * 25) : nullptr;
            g_cameraFound = c.camera;
            g_assetInfo = c.assetInfo;
            g_noCollisionData = c.noCollisionData;
            g_customTag = c.customTag;
            g_sticker = c.sticker;
            if (original)
                Original<void(__thiscall*)(void*, void*)>(0x00581850)(skater, Ptr<void>(kProfile));
            else
                skater->Init(Ptr<Obj::CSkaterProfile>(kProfile));
            const auto* words = reinterpret_cast<const uint32_t*>(skater);
            for (size_t i = 0; i < sizeof(CSkater) / 4; ++i)
                t.push_back(Rel(words[i]));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater init");
}

} // namespace

SELFTEST(Early, TestSkaterInit, ViewerTestKit::Cleanup);
