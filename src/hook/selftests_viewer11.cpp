// Self-tests for the skater's profile, reset, board, show / hide, node and pause pieces (src/game/skater/skater.cpp,
// 0x00580BE0-0x00581845): the original and ours run on a stand-in skater in an arena; everything they call outside
// is a recording stand-in (by address: its this, its arguments, a scripted answer), and the calls, results and the
// whole arena are compared.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/sio/device.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/appmain_testkit.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkater;

// Arena layout.
constexpr uint32_t kSkater = 0x0000;   // 0x324
constexpr uint32_t kProfile = 0x0400;  // a profile (0x64; its info at +0x5C)
constexpr uint32_t kCorePhys = 0x0500; // m_corePhysics (+0xC an object, +0x1514 / +0x1528 blocks, +0x18 / +0x58... )
constexpr uint32_t kBlocks = 0x2000;   // small blocks, 0x100 apart
constexpr uint32_t kText = 0x4000;     // strings, 0x20 apart

uint32_t Block(int i) { return At(kBlocks + 0x100 * i); }
uint32_t TextAt(int i) { return At(kText + 0x20 * i); }

void* g_skaterVtable[24];
bool g_classic, g_net, g_noStructure, g_noNode;
uint32_t g_stance;
int g_elements;
int g_clock;

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0xD9151D4D: return Block(1);  // SkaterPhysicsControl
    case 0x488AF07A: return Block(2);  // SkaterFlipAndRotate
    case 0x589E5FB3: return Block(3);
    case 0x43686585: return Block(4);
    case 0x21FC3301: return Block(5);
    case 0x5B512EDC: return Block(6);
    case 0x286A8D26: return Block(7);  // model
    case 0x850EEF66: return Block(8);
    case 0xB5F1E9D2: return Block(9);
    default: return 0u;
    }
}

// A stand-in recording its this and `pop` / 4 arguments, answering `answer` (with `block`: block `answer`'s address
// in the current arena).
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
// The same for a cdecl function of `count` arguments.
void SimpleCdecl(StandIns& s, std::initializer_list<uint32_t> addresses, int count, uint32_t answer = 0)
{
    uint32_t tag = *addresses.begin() & 0xFFFFF;
    void* thunk = s.At(addresses, 0, [tag, count, answer](Regs& r) {
        g_trace.push_back(tag);
        for (int i = 0; i < count; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return answer;
    });
    AlsoOurs(s, addresses, thunk);
}

void Install(StandIns& s)
{
    InstallCommon(s);
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
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [getter](Regs& r) { return getter(r, 0x6630); });
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [getter](Regs& r) { return getter(r, 0x6590); });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [getter](Regs& r) { return getter(r, 0x66D0); });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        return g_noStructure ? 0u : Block(20) + (r.args[0] & 0x30);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return ComponentFor(r.args[0]);
    });
    s.At({0x00553220}, 4, [](Regs& r) {
        Rec(0x3220, {r.ecx, r.args[0]});
        return g_classic ? 1u : 0u;
    });
    s.At({0x00584540, OursOf(0x00584540)}, 4, [](Regs& r) {
        Rec(0x4540, {r.ecx, r.args[0]});
        return 5u + (r.args[0] & 3);
    });
    s.At({0x00585FB0, AddressOf(&Obj::CSkaterProfile::GetStat)}, 4, [](Regs& r) {
        Rec(0x5FB0, {r.ecx, r.args[0]});
        return r.args[0] & 7;
    });
    s.At({0x005860C0, AddressOf(&Obj::CSkaterProfile::GetChecksumValue)}, 4, [](Regs& r) {
        Rec(0x60C0, {r.ecx, r.args[0]});
        return r.args[0] == 0x7D02BCC3 ? g_stance : 0xA5A50000u;
    });
    s.At({0x00586170, AddressOf(&Obj::CSkaterProfile::IsPro)}, 0, [](Regs& r) {
        Rec(0x6170, {r.ecx});
        return 1u;
    });
    s.At({0x00586210, AddressOf(&Obj::CSkaterProfile::GetDisplayName)}, 0, [](Regs& r) {
        Rec(0x6210, {r.ecx});
        return TextAt(1);
    });
    s.At({0x00586150, AddressOf(&Obj::CSkaterProfile::GetName)}, 0, [](Regs& r) {
        Rec(0x6150, {r.ecx});
        return 0x4A3Eu;
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Rec(0xECE0, {});
        return static_cast<uint32_t>(g_clock += 3);
    });
    s.At({0x0044E4F0, OursOf(0x0044E4F0)}, 0, [](Regs& r) {
        Rec(0xE4F0, {r.ecx});
        return g_net ? 1u : 0u;
    });
    Simple(s, {0x0053AEA0}, 4, 30, true);
    Simple(s, {0x00532EA0}, 4, 31, true);
    s.At({0x0049F9B0}, 16, [](Regs& r) {
        Rec(0xF9B0, {r.ecx, Text(r.args[0]), r.args[1], r.args[2], r.args[3]});
        return Block(40 + g_elements++);
    });
    Simple(s, {0x00553E90}, 0, 32, true);
    for (uint32_t address : {0x00511420u, 0x00518630u, 0x004F8D80u, 0x0046BC60u, 0x00511310u, 0x0051EAB0u, 0x0051EB30u,
                             0x0051EF00u, 0x0051EB00u, 0x0051ED10u, 0x005232E0u, 0x00519650u, 0x0043D520u, 0x00419A20u,
                             0x0048E4C0u, 0x005144D0u, 0x00513AB0u, 0x00431780u, 0x0047B2A0u})
        Simple(s, {address}, 0, address == 0x00419A20u ? 0x419A2u : 0u);
    s.At({0x0041A730, OursOf(0x0041A730)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Rec(0xA730, {r.ecx, v[0], v[1], v[2]});
        return r.ecx;
    });
    for (uint32_t address : {0x005178B0u, 0x0048DED0u, 0x0049F500u, 0x00565BF0u})
        Simple(s, {address}, 4);
    for (uint32_t address : {0x00563370u, 0x00563390u, 0x0048D060u, 0x0048DE80u, 0x0048DEB0u, 0x0049F420u, 0x0048DE30u})
        Simple(s, {address}, 8);
    for (uint32_t address : {0x004202A0u, 0x0049F360u, 0x00420110u})
        Simple(s, {address}, 12);
    Simple(s, {0x00511390}, 16);
    for (uint32_t address : {0x004317C0u, 0x004317E0u})
        s.At({address, OursOf(address)}, 4, [address](Regs& r) {
            const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
            Rec(address & 0xFFFF, {r.ecx, r.args[0], v[0], v[1], v[2], v[3]});
            return 0u;
        });
    Simple(s, {0x0045D990, AddressOf(&Obj::CCompositeObject::Hide)}, 4);
    Simple(s, {0x0045E4C0, AddressOf(&Obj::CCompositeObject::SetTeleported)}, 4);
    Simple(s, {0x005BDB60, AddressOf(&Sio::Device::PauseActuators)}, 0);
    Simple(s, {0x005BDBD0, AddressOf(&Sio::Device::UnpauseActuators)}, 0);
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Rec(0xC1B0, {r.args[0], r.args[1] & 0xFF});
        return g_noNode ? 0u : Block(33);
    });
    s.At({0x0045C560, AddressOf(static_cast<bool (*)(uint32_t, Mth::Vector*)>(&SkateScript::GetPosition))}, 0, [](Regs& r) {
        Rec(0xC560, {r.args[0], r.args[1]});
        auto* v = reinterpret_cast<float*>(r.args[1]);
        v[0] = 11.5f, v[1] = -2.0f, v[2] = 7.25f, v[3] = 1.0f;
        return 1u;
    });
    s.At({0x0045C180, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetAngles))}, 0,
         [](Regs& r) {
             Rec(0xC180, {r.args[0]});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 0.1f, v[1] = 0.2f, v[2] = 0.3f, v[3] = 0.0f;
             return 1u;
         });
    g_skaterVtable[0x48 / 4] = s.Thunk(0, [](Regs& r) {
        Rec(0x5148, {r.ecx});
        return 0u;
    });
}

uint8_t& Byte(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint8_t*>(address + offset); }
uint32_t& Word(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint32_t*>(address + offset); }

// Every function in both states of what it tests: classic stats or the profile's; the profile's identity for a
// local skater and not, both stances, the stance flag already right or not; the input reset; the state reset with
// and without the physics control engaged and the +0x25C object; the network set-up; the board elements (an unknown
// type, a missing structure, each type, with and without the turned / overlay options); hiding and showing for local
// and not; the display name online and not; moving to no node, a missing node, a node (online and not); pausing and
// unpausing twice, local and not, with and without the input device.
void TestSkaterProfile()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x007CCE14, 4, {}}, {0x007CE470, 4, {}}, {0x00700F1C, 4, {}}, {0x006F85A8, 4, {}},
                 {0x00696D60, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_classic = g_net = g_noStructure = g_noNode = false;
        g_stance = 0x287FCD4E;
        g_elements = 0;
        g_clock = 1000;
        std::vector<uint32_t>& t = traces[run];
        const char* texts[] = {"", "Tony Hawk", "Bob", "board.img", "overlay.img", "deck"};
        for (int i = 0; i < 6; ++i)
            std::memcpy(reinterpret_cast<char*>(TextAt(i)), texts[i], std::strlen(texts[i]) + 1);
        GameVariable<uint32_t>(0x007CE478) = Block(10);
        GameVariable<uint32_t>(0x007CCE14) = Block(11);
        GameVariable<uint32_t>(0x007CE470) = Block(12);
        GameVariable<uint32_t>(0x00700F1C) = Block(13);
        GameVariable<uint32_t>(0x006F85A8) = Block(14);
        GameVariable<int>(0x00696D60) = 500;
        Word(Block(10), 0x20) = Block(15);
        // The memory manager's skater heaps (+0x180) and top-down heap (+0x110).
        for (int i = 0; i < 4; ++i)
            Word(At(0x7000), 0x180 + 4 * i) = 0x7E000000u + i;
        Word(At(0x7000), 0x110) = 0x7D000000u;
        auto* skater = Ptr<CSkater>(kSkater);
        uint32_t self = At(kSkater);
        skater->m_vtable = g_skaterVtable;
        skater->m_id = 0x10051;
        skater->m_skaterNumber = 2;
        skater->m_corePhysics = Ptr<uint8_t>(kCorePhys);
        Word(At(kCorePhys), 0xC) = Block(16);
        Word(At(kCorePhys), 0x1514) = Block(17);
        Word(At(kCorePhys), 0x1528) = Block(18);
        for (int i = 0; i < 16; ++i)
            reinterpret_cast<float*>(Block(16) + 0x6C)[i] = 0.5f * i - 3.0f;
        skater->m_scoreHolder = Ptr<uint8_t>(kBlocks + 0x100 * 19);
        Word(Block(19), 0x18) = Block(21);
        skater->m_shadow = Ptr<void>(kBlocks + 0x100 * 22);
        skater->m_unknown24C = Ptr<void>(kBlocks + 0x100 * 23);
        skater->m_unknown250 = Ptr<uint8_t>(kBlocks + 0x100 * 24);
        skater->m_unknown258 = Ptr<void>(kBlocks + 0x100 * 25);
        skater->m_unknown25C = nullptr;
        skater->m_unknown264 = Ptr<uint8_t>(kBlocks + 0x100 * 26);
        Word(Block(26), 0x490) = Block(27);
        Word(Block(27), 0x1C) = Block(28);
        skater->m_unknown27C = Ptr<uint8_t>(kBlocks + 0x100 * 29);
        for (int i = 0; i < 16; ++i)
            (&skater->m_matrix.row[0].x)[i] = 0.25f * i;
        new (&skater->m_displayName) Str::String();
        // The profile.
        uint32_t profile = At(kProfile);
        uint32_t info = profile + 0x5C;
        g_params[{info, 0x3F813177}] = 1;
        g_params[{info, 0x76FD33E8}] = 0x5717E;
        g_params[{info, 0x562E3ECD}] = TextAt(2);
        auto withProfile = [&](uint32_t address, void (CSkater::*ours)(Obj::CSkaterProfile*)) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t)>(address)(skater, profile);
            else
                (skater->*ours)(reinterpret_cast<Obj::CSkaterProfile*>(profile));
        };
        auto call = [&](uint32_t address, void (CSkater::*ours)()) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(skater);
            else
                (skater->*ours)();
        };
        // Stats.
        for (int classic : {0, 1}) {
            g_classic = classic != 0;
            withProfile(0x00580BE0, &CSkater::SetStatsFromProfile);
        }
        // Identity.
        for (int local : {0, 1}) {
            skater->m_unknownBC = static_cast<uint8_t>(local);
            for (uint32_t stance : {0x287FCD4Eu, 0xB58EFC2Bu, 0xB58EFC2Bu}) {
                g_stance = stance;
                withProfile(0x00580EB0, &CSkater::UpdateFromProfile);
                t.push_back(Hash(skater->m_displayName.c_str()));
            }
        }
        // No skate style in the profile: the style cleared first.
        g_params.erase({info, 0x76FD33E8});
        skater->m_style = 0x99;
        withProfile(0x00580EB0, &CSkater::UpdateFromProfile);
        t.push_back(skater->m_style);
        g_params[{info, 0x76FD33E8}] = 0x5717E;
        // Resets.
        for (int i = 0; i < 24; ++i)
            Byte(Block(18), 0x20 + 8 * i) = static_cast<uint8_t>(i % 3 == 0 ? 1 : (i % 3 == 1 ? 0 : 2));
        call(0x00580FF0, &CSkater::ResetInput);
        for (int engaged : {0, 1}) {
            Byte(Block(1), 0x18) = static_cast<uint8_t>(engaged);
            skater->m_unknown25C = engaged ? Ptr<void>(kBlocks + 0x100 * 34) : nullptr;
            call(0x005810E0, &CSkater::ResetState);
        }
        for (int local : {0, 1}) {
            skater->m_unknownBC = static_cast<uint8_t>(local);
            Word(Block(31), 8) = 0xC1C1u + local;
            call(0x005811E0, &CSkater::SetUpNetwork);
            call(0x005814D0, &CSkater::Reset27C);
            call(0x00581510, &CSkater::HideSkater);
            t.push_back(skater->m_flag2C4);
            call(0x00581550, &CSkater::ShowSkater);
            t.push_back(skater->m_flag2C4);
        }
        // Board elements.
        auto board = [&](uint32_t type) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t)>(0x00581260)(skater, type);
            else
                skater->ShowBoardElement(type);
        };
        board(0x12345678);
        g_noStructure = true;
        board(0x23A1DF80);
        g_noStructure = false;
        const uint32_t types[] = {0x23A1DF80, 0x07CB4FA9, 0x5B288E0B, 0xD0C656A2};
        for (int k = 0; k < 4; ++k) {
            uint32_t structure = Block(20) + (((k == 0 ? 0x53409C6Bu : k == 1 ? 0xC4BFAEC3u : k == 2 ? 0xEF76A8DBu : 0xFFE9F52Au)) & 0x30);
            g_params[{structure, 0xA01371B1}] = 0xB0A0u + k;
            g_params[{structure, 0x6AA8D676}] = 0x42000000u + k; // 32.0+
            g_params[{structure, 0xCCDFDDC2}] = 0x41800000u;
            g_params[{structure, 0x676F1DF1}] = 0xB0B0u + k;
            g_params[{structure, 0xC553C612}] = TextAt(3);
            g_params[{structure, 0x49B318FD}] = TextAt(5);
            g_params.erase({structure, 0xC876ADF3});
            g_params.erase({structure, 0x25A0E7E9});
            if (k & 1)
                g_params[{structure, 0xC876ADF3}] = 1;
            if (k >= 2) {
                g_params[{structure, 0x25A0E7E9}] = 1;
                g_params[{structure, 0x499F4BE9}] = TextAt(4);
            }
            board(types[k]);
        }
        // The display name.
        for (int net : {0, 1}) {
            g_net = net != 0;
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<const char*(__thiscall*)(void*)>(0x00581580)(skater)
                                                               : skater->GetDisplayName())));
        }
        // Nodes.
        auto move = [&](uint32_t node) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t)>(0x005815C0)(skater, node);
            else
                skater->MoveToNode(node);
        };
        move(0);
        g_noNode = true;
        move(0x0DE1);
        g_noNode = false;
        for (int net : {0, 1}) {
            g_net = net != 0;
            move(0x0DE2 + net);
        }
        g_net = false;
        // Pausing.
        for (int local : {0, 1}) {
            skater->m_unknownBC = static_cast<uint8_t>(local);
            for (int device : {1, 0}) {
                skater->m_unknown264 = device ? Ptr<uint8_t>(kBlocks + 0x100 * 26) : nullptr;
                skater->m_compositeFlags &= ~1u;
                call(0x00581760, &CSkater::Pause);
                t.push_back(skater->m_compositeFlags);
                call(0x00581760, &CSkater::Pause);
                t.push_back(skater->m_paused);
                call(0x005817F0, &CSkater::Unpause);
                call(0x005817F0, &CSkater::Unpause);
                t.push_back(skater->m_paused);
            }
        }
        skater->m_displayName.~String();
        (void)self;
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater profile");
}

} // namespace

SELFTEST(Early, TestSkaterProfile, ViewerTestKit::Cleanup);
