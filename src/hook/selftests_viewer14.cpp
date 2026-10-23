// Self-tests for the skater's restart and script member functions (src/game/skater/skater.cpp, 0x00583400 /
// 0x00583470 / 0x005835D0): the original and ours run on a stand-in skater in an arena; what they call outside is a
// recording stand-in (floats from C++ functions jumped to), and the calls, results and the whole arena are compared.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/crc.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/skater/skater.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkater;

// Arena layout.
constexpr uint32_t kSkater = 0x0000;   // 0x324
constexpr uint32_t kCorePhys = 0x0400;
constexpr uint32_t kBlocks = 0x2000;   // small blocks, 0x100 apart
constexpr uint32_t kParams = 0x5000;   // structures, 0x10 apart

uint32_t Block(int i) { return At(kBlocks + 0x100 * i); }
uint32_t Params(int i) { return At(kParams + 0x10 * i); }

void* g_skaterVtable[24];
bool g_camera, g_restartLink, g_flag4FC;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_strings;

float __fastcall GetStatStandIn(void* self, void*, int index)
{
    Rec(0x0490, {reinterpret_cast<uint32_t>(self), static_cast<uint32_t>(index)});
    return 2.5f * index;
}
float __fastcall ScaledStandIn(void* self, void*, CStruct* params)
{
    Rec(0x05B0, {reinterpret_cast<uint32_t>(self), Rel(reinterpret_cast<uint32_t>(params))});
    return 7.75f;
}

void Simple(StandIns& s, std::initializer_list<uint32_t> addresses, int pop, uint32_t answer = 0)
{
    uint32_t tag = *addresses.begin() & 0xFFFFF;
    s.At(addresses, static_cast<uint16_t>(pop), [tag, pop, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return answer;
    });
}

void Install(StandIns& s)
{
    InstallCommon(s);
    for (uint32_t address : {0x00580490u, AddressOf(&CSkater::GetStat)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&GetStatStandIn)));
    for (uint32_t address : {0x005805B0u, AddressOf(&CSkater::GetScaledValue)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&ScaledStandIn)));
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
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [](Regs& r) {
             Rec(0x6590, {r.ecx, r.args[0], r.args[2] & 0xFF});
             auto it = g_strings.find({r.ecx, r.args[0]});
             if (it == g_strings.end())
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [getter](Regs& r) { return getter(r, 0x6630); });
    s.At({0x004767C0, 0x00476E70, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, [getter](Regs& r) { return getter(r, 0x67C0); });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return g_params.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00476BA0, AddressOf(static_cast<bool (CStruct::*)(const char*)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6BA0, {r.ecx, Text(r.args[0])});
        return 0u;
    });
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4, [](Regs& r) {
        Rec(0x6AF0, {r.ecx, r.args[0]});
        return g_params.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    Simple(s, {0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8);
    s.At({0x00401B90, 0x00472E60, AddressOf(&Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        Rec(0x1B90, {Text(r.args[0])});
        return 0xC4C0u;
    });
    s.At({0x0045C2D0, AddressOf(&SkateScript::GetNearestNodeByPrefix)}, 0, [](Regs& r) {
        Rec(0xC2D0, {r.args[0], r.args[1]});
        return Block(20);
    });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Rec(0xBE40, {r.args[0]});
        return 0x9E21u;
    });
    s.At({0x0045C3A0, AddressOf(static_cast<uint32_t (*)(uint32_t)>(&SkateScript::GetLinkCount))}, 0, [](Regs& r) {
        Rec(0xC3A0, {r.args[0]});
        return 3u;
    });
    s.At({0x0045C3E0, AddressOf(static_cast<uint32_t (*)(uint32_t, int)>(&SkateScript::GetLink))}, 0, [](Regs& r) {
        Rec(0xC3E0, {r.args[0], r.args[1]});
        return 0x111u + r.args[1];
    });
    s.At({0x0046B680}, 0, [](Regs& r) {
        Rec(0xB680, {r.args[0]});
        return g_restartLink && r.args[0] == 0x112u ? 1u : 0u;
    });
    Simple(s, {0x00566370}, 12);
    Simple(s, {0x00564C60}, 8);
    s.At({0x004FC440}, 0, [](Regs& r) {
        Rec(0xC440, {r.ecx});
        return g_flag4FC ? 1u : 0u;
    });
    s.At({0x004FC420}, 0, [](Regs& r) {
        Rec(0xC420, {r.ecx});
        return g_flag4FC ? 1u : 0u;
    });
    s.At({0x0042BD60}, 0, [](Regs& r) {
        Rec(0xBD60, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004A09C0}, 0, [](Regs& r) {
        Rec(0x09C0, {r.args[0]});
        return g_camera ? Block(21) : 0u;
    });
    for (uint32_t address : {0x004317C0u, 0x004317E0u})
        s.At({address}, 4, [address](Regs& r) {
            const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
            Rec(address & 0xFFFF, {r.ecx, r.args[0], v[0], v[1], v[2], v[3]});
            return 0u;
        });
    Simple(s, {0x0042BBF0, AddressOf(&Mth::Vector::SetLength)}, 4);
    Simple(s, {0x0045E4C0, AddressOf(&Obj::CCompositeObject::SetTeleported)}, 4);
    Simple(s, {0x0045E450, AddressOf(static_cast<bool (Obj::CCompositeObject::*)(uint32_t, CStruct*, Script::CScript*)>(&Obj::CCompositeObject::CallMemberFunction))}, 12, 0x77);
    Simple(s, {0x00580830, AddressOf(&CSkater::MoveToRandomRestart)}, 0);
    Simple(s, {0x00582800, AddressOf(&CSkater::LaunchProjectile)}, 0);
    Simple(s, {0x00582510, AddressOf(&CSkater::SwitchToScript)}, 8);
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return Block(1) + (r.args[0] & 0x70);
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return Block(22);
    });
    for (uint32_t address : {0x00512030u, 0x004F91D0u, 0x0051BF00u, 0x00419A20u, 0x00506BE0u, 0x004FA7C0u, 0x00510F80u})
        Simple(s, {address}, 0);
    Simple(s, {0x00474420, AddressOf(&Script::CScript::Destroy)}, 0);
    s.At({0x004499B0, AddressOf(static_cast<void (*)(void*)>(&Script::CScript::operator delete))}, 0, [](Regs& r) {
        Rec(0x99B0, {r.args[0]});
        return 0u;
    });
    Simple(s, {0x00473880, AddressOf(&Script::CScript::RunOnExitScripts)}, 0);
    for (int slot : {15, 17, 18}) // +0x3C SkipToRestart(node, walking), +0x44, +0x48
        g_skaterVtable[slot] = s.Thunk(slot == 15 ? 8 : 0, [slot](Regs& r) {
            Rec(0x5100 + slot, {r.ecx, slot == 15 ? r.args[0] : 0u, slot == 15 ? (r.args[1] & 0xFF) : 0u});
            return 0u;
        });
    g_skaterVtable[19] = s.Thunk(8, [](Regs& r) {
        Rec(0x514C, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
}

uint8_t& Byte(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint8_t*>(address + offset); }
uint32_t& Word(uint32_t address, uint32_t offset) { return *reinterpret_cast<uint32_t*>(address + offset); }

// ResetComponents (local and not, with and without a script); Restart (walking and not); each member function and
// its branches (KillSkater: by prefix, by a prefix string, by node, by a linked restart, by the skate module, in the
// park editor; ShouldMongo for each push style; SetCustomRestart set / clear / asked; PlaceBeforeCamera with and
// without a camera; the default).
void TestSkaterMembers()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x007CE5D8, 4, {}}, {0x006F0FB8, 4, {}}, {0x006F3548, 16, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        g_camera = g_restartLink = g_flag4FC = false;
        g_strings.clear();
        g_printfArgs = 0;
        GameVariable<uint32_t>(0x007CE478) = Block(10);
        GameVariable<uint32_t>(0x007CE5D8) = Block(11);
        GameVariable<uint32_t>(0x006F0FB8) = Block(12);
        auto* skater = Ptr<CSkater>(kSkater);
        skater->m_vtable = g_skaterVtable;
        skater->m_pos = {5.0f, 6.0f, 7.0f, 1.0f};
        for (int k = 0; k < 16; ++k)
            (&skater->m_matrix.row[0].x)[k] = 0.25f * k - 1.0f;
        skater->m_corePhysics = Ptr<uint8_t>(kCorePhys);
        Word(At(kCorePhys), 0xC) = Block(13);
        for (int k = 0; k < 16; ++k)
            reinterpret_cast<float*>(Block(13) + 0x6C)[k] = 0.5f * k;
        skater->m_state284 = Ptr<uint8_t>(kBlocks + 0x100 * 14);
        Word(Block(14), 0x2C) = Block(15);
        skater->m_shadow = Ptr<void>(kBlocks + 0x100 * 16);
        skater->m_unknown270 = Ptr<uint8_t>(kBlocks + 0x100 * 17);
        skater->m_unknown24C = Ptr<void>(kBlocks + 0x100 * 18);
        skater->m_name = 0x4A3E;
        skater->m_viewport = 1;
        for (int k = 0; k < 4; ++k)
            reinterpret_cast<float*>(Block(21))[k] = 1.0f + k; // the camera's... (filled below)
        auto* script = Ptr<Script::CScript>(0x4000);
        Word(At(0x4000), 0xB8) = 0xB8B8;
        Word(At(0x4000), 0x14) = Params(40); // its results
        auto member = [&](uint32_t name, uint32_t params) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, void*, void*)>(0x005835D0)(skater, name, reinterpret_cast<void*>(params), script)
                                 : skater->CallMemberFunction(name, reinterpret_cast<CStruct*>(params), script));
            const auto* words = reinterpret_cast<const uint32_t*>(skater);
            for (size_t i = 0x4C / 4; i < 0xC0 / 4; ++i)
                t.push_back(words[i]);
            for (size_t i = 0x2C8 / 4; i < 0x31C / 4; ++i)
                t.push_back(words[i]);
        };
        // ResetComponents / Restart.
        for (int local : {1, 0}) {
            skater->m_unknownBC = static_cast<uint8_t>(local);
            skater->m_script = local ? script : nullptr;
            if (original)
                Original<void(__thiscall*)(void*)>(0x00583400)(skater);
            else
                skater->ResetComponents();
            t.push_back(Rel(reinterpret_cast<uint32_t>(skater->m_script)));
        }
        for (int walking : {1, 0, 2}) { // (2: walking with +0x118 set - not switched)
            Byte(Block(15), 0x118) = walking == 2 ? 1 : 0;
            walking = walking != 0 ? 1 : 0;
            Word(Block(15), 0x1C) = static_cast<uint32_t>(walking);
            skater->m_script = walking ? script : nullptr;
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00583470)(skater, 0xAA, 0xBB);
            else
                skater->Restart(0xAA, 0xBB);
        }
        skater->m_script = nullptr;
        // KillSkater.
        uint32_t p = Params(0);
        g_params[{p, 0xD6F06BF6}] = 1;
        g_params[{p, 0x6C4E7971}] = 0x5E7F;
        member(0x1B572ABF, p);
        g_params.erase({p, 0x6C4E7971});
        g_strings[{Params(1), 0x6C4E7971}] = At(0x4800);
        std::memcpy(Ptr<char>(0x4800), "TRG_Restart", 12);
        member(0x1B572ABF, Params(1)); // (a prefix as a string)
        g_params[{p, 0x7A8017BA}] = 0x40DE;
        member(0x1B572ABF, p);
        g_params.erase({p, 0x7A8017BA});
        g_restartLink = true;
        member(0x1B572ABF, p);
        g_restartLink = false;
        member(0x1B572ABF, p);
        Word(Block(11), 0x40) = 0xD443A2BC;
        member(0x1B572ABF, p);
        Word(Block(11), 0x40) = 0;
        // The rest.
        member(0x0C09C1B7, p);
        member(0xBCE85494, p);
        g_params[{Params(2), 0}] = 0x4A3E;
        member(0x1C5612EE, Params(2));
        g_params[{Params(2), 0}] = 0x4A3F;
        member(0x1C5612EE, Params(2));
        for (int local : {0, 1}) {
            skater->m_unknownBC = static_cast<uint8_t>(local);
            for (uint32_t style : {0x0CDBA4F9u, 0x16C6DF0Bu, 0x12345u}) {
                skater->m_pushStyle = style;
                for (int flag : {0, 1}) {
                    g_flag4FC = flag != 0;
                    member(0x3E8EB713, p);
                }
            }
        }
        member(0x3F06191C, p);
        member(0x5436A335, p);
        member(0xE62F94A2, p);
        t.push_back(skater->m_sparks);
        member(0x42832375, Params(3)); // asked (not set)
        g_params[{Params(3), 0x19EBDA23}] = 1;
        g_flag4FC = true;
        member(0x42832375, Params(3)); // set
        g_params.erase({Params(3), 0x19EBDA23});
        member(0x42832375, Params(3));  // asked
        skater->m_pos = {50.0f, 60.0f, 70.0f, 1.0f};
        member(0x5A3C19E9, Params(3));
        g_params[{Params(3), 0x1A4E0EF9}] = 1;
        member(0x42832375, Params(3)); // cleared
        member(0x7015BC33, p);
        member(0x72AECFC0, p);
        for (int viewport : {0, 1}) {
            skater->m_viewport = viewport;
            member(0x97C8285A, p);
            member(0xF4C52F72, p);
        }
        g_params[{Params(4), 0xDF4700DE}] = 3;
        member(0x7915AA31, Params(4));
        g_params[{Params(4), 0}] = Params(5);
        member(0x9ABE8A21, Params(4));
        member(0xBF450BAB, p);
        member(0xD38E8B6C, p); // no camera
        g_camera = true;
        for (int k = 0; k < 4; ++k)
            reinterpret_cast<float*>(Block(21) + 0x4C)[k] = 10.0f * k + 1.0f; // (the camera's m_pos? see Gfx::Camera)
        member(0xD38E8B6C, p);
        member(0xC6870028, p);
        member(0x12345678, p);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater members");
}

} // namespace

SELFTEST(Early, TestSkaterMembers, ViewerTestKit::Cleanup);
