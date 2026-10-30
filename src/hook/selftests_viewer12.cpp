// Self-tests for the skater's set-up (src/game/skater/skater.cpp, CSkater::Init 0x00581850): the original and ours set
// up a stand-in skater in an arena; what it calls outside (the camera, components made from structures, the asset
// manager, the profile and stats pieces, scripts...) is a recording stand-in, and the calls and the whole arena are
// compared.

#include <initializer_list>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/net/net_dispatcher.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/goals/create_a_trick.h"
#include "game/script/game_cfuncs.h"
#include "game/skater/skater_camera_component.h"
#include "game/skater/trick_component.h"
#include "hook/appmain_testkit.h"
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
bool g_cameraFound, g_assetInfo, g_noCollisionData, g_customTag, g_sticker, g_noSuspend, g_noFlip, g_netRule;

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
    case 0xCE0CA665: return g_noSuspend ? 0u : Block(26); // suspend
    case 0x488AF07A: return g_noFlip ? 0u : Block(27);    // SkaterFlipAndRotate
    default: return 0u;
    }
}

// Our replacement of a game function (stood in for too: our code calls it directly); the address itself if none.
uint32_t OursOf(uint32_t address)
{
    void* ours = AppMainTests::OursAt(address);
    return ours ? Resolve(ours) : address;
}
// A stand-in's thunk written over our replacements of `addresses` as well.
void AlsoOurs(StandIns& s, std::initializer_list<uint32_t> addresses, void* thunk)
{
    // (only the functions skater.cpp calls directly now: others' replacements may run for real)
    static const uint32_t kDirect[] = {0x00511420, 0x00518630, 0x0046BC60, 0x00511310, 0x00519650, 0x0043D520, 0x00419A20, 0x005144D0, 0x00513AB0, 0x00431780, 0x0047B2A0, 0x005178B0, 0x004202A0, 0x00420110, 0x00511390, 0x0053AEA0, 0x00532EA0, 0x00419A90, 0x00420480, 0x0051D830, 0x0051D9C0, 0x0051DA20, 0x00407A60, 0x00511570, 0x0042BBF0, 0x00524C50, 0x00525E60, 0x00419B30, 0x0043D560, 0x00522410, 0x00512030, 0x0051BF00, 0x00506BE0, 0x00510F80, 0x0043D550, 0x00543340};
    for (uint32_t address : addresses) {
        if (std::find(std::begin(kDirect), std::end(kDirect), address) == std::end(kDirect))
            continue;
        uint32_t ours = OursOf(address);
        if (ours != address)
            s.Bytes(ours, Branch(0xE9, ours, thunk));
    }
}

void Simple(StandIns& s, std::initializer_list<uint32_t> addresses, int pop, uint32_t answer = 0, bool block = false)
{
    uint32_t tag = *addresses.begin() & 0xFFFFF;
    void* thunk = s.At(addresses, static_cast<uint16_t>(pop), [tag, pop, answer, block](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return block ? Block(static_cast<int>(answer)) : answer;
    });
    AlsoOurs(s, addresses, thunk);
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
    s.At({0x00407A50, OursOf(0x00407A50)}, 4, [](Regs& r) {
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
    for (uint32_t address : {0x00524C50u, 0x00525E60u, 0x00419B30u, 0x00522410u, 0x0051D9C0u, 0x0051DA20u})
        Simple(s, {address}, 4);
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x004FA3C0u, AddressOf(&Obj::CSkaterCameraComponent::SetSkater)}})
        Simple(s, {address, ours_}, 4);
    // (the vibration's SetActive: a bool - the original passes the skate module's byte, ours that byte != 0; the
    // original only tests and compares it as a truth value)
    s.At({0x0043D560, OursOf(0x0043D560)}, 4, [](Regs& r) {
        Rec(0xD560, {Rel(r.ecx), (r.args[0] & 0xFF) != 0 ? 1u : 0u});
        return 0u;
    });
    for (uint32_t address : {0x00419A90u})
        Simple(s, {address}, 8);
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x004F9AE0u, AddressOf(&Obj::CSkaterCameraComponent::SetMode)}})
        Simple(s, {address, ours_}, 8);
    for (uint32_t address : {0x00420480u, 0x0051D830u})
        Simple(s, {address}, 0);
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x0051EB00u, AddressOf(&Obj::CTrickComponent::ClearEventBuffer)}, {0x0051ED10u, AddressOf(&Obj::CTrickComponent::ClearQueue)}, {0x0051EB30u, AddressOf(&Obj::CTrickComponent::ClearBlock1238)}, {0x0051EF00u, AddressOf(&Obj::CTrickComponent::ClearBlock1280)}})
        Simple(s, {address, ours_}, 0);
    Simple(s, {0x00407A60}, 0, 1);
    s.At({0x00552560, AddressOf(&Game::CCreateATrick::Construct)}, 0, [](Regs& r) {
        Rec(0x2560, {r.ecx});
        return r.ecx;
    });
    s.At({0x0059F890, AddressOf(CFuncs::MakeCameraActive)}, 0, [](Regs& r) {
        Rec(0xF890, {r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0042E380, OursOf(0x0042E380)}, 0, [](Regs& r) {
        Rec(0xE380, {r.args[0]});
        return 0u;
    });
    // The scripts, the network, arrays.
    s.At({0x005A11B0, AddressOf(CFuncs::CFunc_InMultiplayerGame)}, 0, [](Regs& r) {
        Rec(0x11B0, {r.args[0], r.args[1]});
        return g_netRule ? 1u : 0u;
    });
    Simple(s, {0x00532EA0}, 4, 28, true);
    Simple(s, {0x0045A5C0, AddressOf(&Net::Dispatcher::AddHandler)}, 20);
    Simple(s, {0x004747A0, AddressOf(static_cast<void (Script::CScript::*)(uint32_t, CStruct*, Obj::CObject*)>(&Script::CScript::SetScript))}, 12);
    Simple(s, {0x004750B0, AddressOf(&Script::CScript::Update)}, 0);
    Simple(s, {0x00511570}, 0);
    s.At({0x0046CAA0, AddressOf(&Script::CArray::Init)}, 0, [](Regs& r) {
        Rec(0xCAA0, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 12);
        return r.ecx;
    });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    Simple(s, {0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8);
    Simple(s, {0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8);
    Simple(s, {0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArray))}, 8);
    Simple(s, {0x004780D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArrayPointer))}, 8);
    s.At({0x0051C470, OursOf(0x0051C470)}, 4, [](Regs& r) {
        Rec(0xC470, {r.ecx, r.args[0]});
        return r.args[0] * 3 + 1;
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
            // The collision component's +0x18 (its data; read by the original through the folded getter 0x005480F0,
            // by ours directly): Block(18), none for the stand-in component without data.
            *Ptr<uint32_t>(kBlocks + 0x100 * 7 + 0x18) = Block(18);
            *Ptr<uint32_t>(kBlocks + 0x100 * 12 + 0x18) = 0;
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

// InitFromProfile (local with the network rule, local without, not local; with and without a suspend component), the
// state script (each flag combination, local and not, with and without a script), switching scripts (with and
// without a script and the flip component), writing the CAT info and the list.
void TestSkaterScripts()
{
    SaveGlobals({{0x007CCE14, 4, {}}, {0x006F3548, 16, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    Simple(*s, {0x00581850, AddressOf(&CSkater::Init)}, 4);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CCE14) = Block(29);
        std::memset(reinterpret_cast<void*>(0x006F3548), 0, 16);
        auto* skater = Ptr<CSkater>(kSkater);
        std::memset(skater, 0, sizeof(CSkater));
        skater->m_vtable = g_skaterVtable;
        skater->m_viewport = 1;
        for (int i = 0; i < 11; ++i) {
            skater->m_trickInfo[i] = Ptr<uint8_t>(0x4000 + 0x10 * i);
            *Ptr<uint32_t>(0x4000 + 0x10 * i + 4) = 0x4100u + i;
            *Ptr<uint32_t>(0x4000 + 0x10 * i + 8) = 0x4200u + i;
            *Ptr<uint32_t>(0x4000 + 0x10 * i + 0xC) = 0x4300u + i;
        }
        skater->m_list28C = Ptr<void>(0x4400);
        struct Case {
            bool local, rule, suspend;
        };
        for (const Case& c : {Case{true, true, true}, Case{true, false, false}, Case{false, true, true}}) {
            skater->m_unknownBC = c.local ? 1 : 0;
            g_netRule = c.rule;
            g_noSuspend = !c.suspend;
            if (original)
                Original<void(__thiscall*)(void*, void*)>(0x005822A0)(skater, Ptr<void>(kProfile));
            else
                skater->InitFromProfile(Ptr<Obj::CSkaterProfile>(kProfile));
        }
        g_noSuspend = false;
        for (int local : {1, 0}) {
            skater->m_unknownBC = static_cast<uint8_t>(local);
            for (int flags = 0; flags < 4; ++flags) {
                skater->m_script = flags == 1 ? nullptr : skater->m_script;
                if (original)
                    Original<void(__thiscall*)(void*, bool, bool)>(0x005823A0)(skater, (flags & 1) != 0, (flags & 2) != 0);
                else
                    skater->RunStateScript((flags & 1) != 0, (flags & 2) != 0);
                t.push_back(Rel(reinterpret_cast<uint32_t>(skater->m_script)));
            }
        }
        for (int flip : {1, 0}) {
            g_noFlip = flip == 0;
            skater->m_script = flip ? nullptr : skater->m_script;
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, void*)>(0x00582510)(skater, 0x5C2A, Ptr<void>(0x4800));
            else
                skater->SwitchToScript(0x5C2A, Ptr<CStruct>(0x4800));
            t.push_back(Rel(reinterpret_cast<uint32_t>(skater->m_script)));
        }
        g_noFlip = false;
        if (original)
            Original<void(__thiscall*)(void*, void*)>(0x005825E0)(skater, Ptr<void>(0x4900));
        else
            skater->WriteCATInfo(Ptr<CStruct>(0x4900));
        if (original)
            Original<void(__thiscall*)(void*, void*)>(0x00582740)(skater, Ptr<void>(0x4900));
        else
            skater->WriteList44B66A9F(Ptr<CStruct>(0x4900));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater scripts");
}

} // namespace

SELFTEST(Early, TestSkaterInit, ViewerTestKit::Cleanup);
SELFTEST(Early, TestSkaterScripts, ViewerTestKit::Cleanup);
