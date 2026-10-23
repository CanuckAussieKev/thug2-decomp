// Self-tests for the rest of the skater profile (src/game/skate/skater_profile.cpp): SetFromParams, Reset, WriteToSave,
// the constructor and the network buffer reads and writes. The original and ours run on a profile in an arena; the
// structures, the appearance's code and the buffer coders are recording stand-ins (lookups answered by structure
// and name), and the calls, results and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/obj/object.h"
#include "game/script/struct_buffer.h"
#include "game/skate/appearance_guard.h"
#include "game/skate/profile_manager.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkaterProfile;

constexpr uint32_t kProfile = 0x0000;
constexpr uint32_t kParams = 0x0200;
constexpr uint32_t kSave = 0x0240;
constexpr uint32_t kStats = 0x0800; // the global stat list (an array: +4 size; elements from +0x40, 0x20 apart)
constexpr uint32_t kStrings = 0x0C00;

std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answers;
std::map<uint32_t, uint32_t> g_nextComponent; // component (0: the first) -> the next (0: none)
bool g_face;
bool g_faceValid;
int g_written;

std::vector<uint32_t> g_sanitised;
uint32_t g_sanitisedExpected;

void Install(StandIns& s)
{
    InstallCommon(s);
    auto answer = [](Regs& r, uint32_t tag) {
        Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
        auto it = g_answers.find({r.ecx, r.args[0]});
        if (it == g_answers.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [answer](Regs& r) { return answer(r, 0x6950); });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [answer](Regs& r) { return answer(r, 0x6630); });
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [answer](Regs& r) { return answer(r, 0x6590); });
    s.At({0x004767C0, 0x00476E70, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, [answer](Regs& r) { return answer(r, 0x67C0); });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Rec(0x8670, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Rec(0x6490, {r.ecx, r.args[0]});
        auto it = g_nextComponent.find(r.args[0]);
        return it == g_nextComponent.end() ? 0u : it->second;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        return At(0x0700);
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9071, {r.args[0]});
        return At(kStats);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return r.ecx + 0x40 + 0x20 * r.args[0];
    });
    // The buffer coders: a structure written (some bytes) / read back (the end).
    s.At({0x00479E20}, 0, [](Regs& r) {
        Rec(0x9E20, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return static_cast<uint32_t>(g_written += 0x11);
    });
    s.At({0x0047A0C0, AddressOf(&Script::ReadFromBuffer)}, 0, [](Regs& r) {
        Rec(0xA0C0, {r.args[0], r.args[1]});
        return r.args[1] + 0x23;
    });
    // The appearance.
    for (uint32_t address : {0x004891D0u, 0x00489160u, 0x00488EA0u}) {
        s.At({address}, 0, [address](Regs& r) {
            Rec(address & 0xFFFF, {r.ecx});
            return r.ecx;
        });
    }
    s.At({0x00488F00}, 0, [](Regs& r) {
        Rec(0x8F00, {r.ecx});
        return 1u;
    });
    s.At({0x00488FA0}, 8, [](Regs& r) {
        Rec(0x8FA0, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 1u;
    });
    s.At({0x00489400}, 4, [](Regs& r) {
        Rec(0x9400, {r.ecx, r.args[0]});
        return r.args[0] + 0x30;
    });
    // (ours only: the appearance guard after the read - TP-R9, tested in selftests_thugpro_guards.cpp - left out here)
    // (counted: ours calls it once per read, on the appearance just read - the profile's +0x4C)
    s.At({AddressOf(&Obj::AppearanceGuard::Sanitise)}, 0, [](Regs& r) {
        g_sanitised.push_back(r.args[0]);
        return 0u;
    });
    s.At({0x004896A0}, 12, [](Regs& r) {
        Rec(0x96A0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0x29u;
    });
    s.At({0x00576E60}, 0, [](Regs& r) {
        Rec(0x6E60, {r.ecx});
        return g_face ? At(0x0D00) : 0u;
    });
    s.At({0x00487510}, 0, [](Regs& r) {
        Rec(0x7510, {r.ecx});
        return g_faceValid ? 1u : 0u;
    });
    s.At({0x00487680}, 4, [](Regs& r) {
        Rec(0x7680, {r.ecx, r.args[0]});
        return 0u;
    });
}

// A string in the arena (its address).
uint32_t String(int index, const char* text)
{
    uint32_t at = kStrings + 0x20 * index;
    std::memcpy(Ptr<char>(at), text, std::strlen(text) + 1);
    return At(at);
}

// Everything SetFromParams / Reset read from `params`, there (`pro`: is_pro; `name`: the skater's name).
void FillParams(uint32_t params, int pro, uint32_t name)
{
    int n = 0;
    for (uint32_t key : {0x2AB66CB8u, 0x562E3ECDu, 0x2820E997u, 0x6C375B95u})
        g_answers[{params, key}] = String(n++, "text");
    g_answers[{params, 0xD82F8AC8}] = static_cast<uint32_t>(pro);
    uint32_t value = 0x101;
    for (uint32_t key : {0x3F813177u, 0x09F350E5u, 0x4564BAB1u, 0x5ECFEF4Du, 0xB78D4AAEu, 0xAC9B9EDAu, 0x471FC4DDu})
        g_answers[{params, key}] = value++;
    g_answers[{params, 0xA1DC81F9}] = name;
    g_answers[{params, 0x7D02BCC3}] = 0x5001;
    g_answers[{params, 0xC15DBF86}] = 0x5002;
    g_answers[{params, 0xDFD4043E}] = 0x5003;
    g_answers[{params, 0x1D6F290C}] = 0x5004;
    g_answers[{params, 0xE365363E}] = 0x5005;
    g_answers[{params, 0xDDBEE809}] = At(0x0740);
    for (int i = 0; i < 3; ++i)
        g_answers[{params, 0x7000u + i}] = 20u + i;
}

void TestProfileRest()
{
    SaveGlobals({});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        *Ptr<uint32_t>(0x7000 + 0x124) = 0x00124000; // the script heap
        *Ptr<uint32_t>(0x7000 + 0x5D4) = 0x005D4000; // the skater info heap
        g_answers.clear();
        g_nextComponent.clear();
        g_written = 0;
        std::vector<uint32_t>& t = traces[run];
        auto* p = Ptr<CSkaterProfile>(kProfile);
        if (!original)
            g_sanitisedExpected = At(kProfile + 0x4C);
        uint32_t info = At(kProfile + 0x5C);
        auto* params = Ptr<CStruct>(kParams);
        auto mark = [&](uint32_t m) { g_trace.push_back(0xEE000000 | m); };
        // The info's structures and the stat list.
        g_answers[{info, 0xD544AA2D}] = At(0x0780);
        g_answers[{info, 0xDDBEE809}] = At(0x07C0);
        g_answers[{info, 0xA1DC81F9}] = 0x0A7BE964;
        *Ptr<uint32_t>(kStats + 4) = 3;
        for (int i = 0; i < 3; ++i)
            g_answers[{At(kStats + 0x40 + 0x20 * i), 0xA1DC81F9}] = 0x7000u + i;
        auto setFrom = [&](CStruct* from, bool a, bool b) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*, bool, bool)>(0x00586230)(p, from, a, b)
                                 : p->SetFromParams(from, a, b));
        };
        // SetFromParams: none; everything (a custom skater: not a pro); a pro (0x27EB9B9D not there: the integer
        // before it kept); the specials alone.
        setFrom(nullptr, true, true);
        mark(1);
        FillParams(At(kParams), 0, 0x0A7BE964);
        g_answers[{At(kParams), 0x27EB9B9D}] = 0x77;
        g_answers[{info, 0xD82F8AC8}] = 0;
        setFrom(params, true, true);
        mark(2);
        g_answers.erase({At(kParams), 0x27EB9B9D});
        FillParams(At(kParams), 1, 0x1234);
        g_answers[{info, 0xD82F8AC8}] = 1;
        setFrom(params, true, false);
        mark(3);
        setFrom(params, false, true);
        mark(4);
        // Reset: none; a custom skater (no pro: a face texture); a pro with another name.
        auto reset = [&](CStruct* from) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*)>(0x00586810)(p, from) : p->Reset(from));
        };
        reset(nullptr);
        mark(5);
        g_answers[{info, 0xD82F8AC8}] = 0;
        g_answers[{info, 0x37F745A5}] = String(5, "pro");
        reset(params);
        mark(6);
        g_answers[{info, 0xD82F8AC8}] = 1;
        g_answers[{info, 0xA1DC81F9}] = 0x4321;
        reset(params);
        mark(7);
        // WriteToSave: a pro without a face; a custom skater with a face that isn't ready; one with a face.
        auto write = [&]() {
            if (original)
                Original<void(__thiscall*)(void*, CStruct*)>(0x00586B00)(p, Ptr<CStruct>(kSave));
            else
                p->WriteToSave(Ptr<CStruct>(kSave));
        };
        g_face = false;
        write();
        mark(8);
        g_answers[{info, 0xD82F8AC8}] = 0;
        g_face = true;
        g_faceValid = false;
        write();
        mark(9);
        g_faceValid = true;
        write();
        mark(10);
        // The constructor (Reset with no params).
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00586D60)(Ptr<void>(0x0100))
                                                            : Ptr<CSkaterProfile>(0x0100)->Construct())));
        mark(11);
        // The network buffer.
        uint8_t* buffer = Ptr<uint8_t>(0x0E00);
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<uint8_t*(__thiscall*)(void*, uint8_t*)>(0x00586DC0)(p, buffer)
                                                            : p->ReadFromBuffer(buffer))));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<uint8_t*(__thiscall*)(void*, uint8_t*)>(0x00586A40)(p, buffer)
                                                            : p->ReadInfoFromBuffer(buffer))));
        mark(12);
        // The info's components: one of each kind sent, and some that aren't.
        const uint32_t names[] = {0x4FA8DF27, 0x2AB66CB8, 0x130618A0, 0x2820E997, 0x562E3ECD, 0x7D02BCC3, 0xC15DBF86,
                                  0xA1DC81F9, 0x9B65D7B8, 0x3F813177, 0x439F4704, 0x6DCB497C, 0x9016B4E7, 0xAF895B3F,
                                  0xAE798769, 0xB1FC0722, 0xB78D4AAE, 0xF0D90109, 0xD82F8AC8, 0xEDF5DB70, 0xF73A13E3,
                                  0x0A7BE964, 0xDDBEE809, 0x12345678, 0xFFFFFFFF, 0x00000001};
        uint32_t previous = 0;
        for (uint32_t i = 0; i < sizeof(names) / 4; ++i) {
            uint32_t c = At(0x1400 + 0x10 * i);
            *Ptr<uint32_t>(0x1400 + 0x10 * i + 4) = names[i];
            *Ptr<uint32_t>(0x1400 + 0x10 * i + 8) = i < 5 ? String(10 + i, "sent") : 0x900 + i;
            g_nextComponent[previous] = c;
            previous = c;
        }
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, uint8_t*, int)>(0x00586E70)(p, buffer, 0x400)
                                                   : p->WriteInfoToBuffer(buffer, 0x400)));
        mark(13);
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, uint8_t*, int, int)>(0x00587050)(p, buffer, 0x400, 7)
                                                   : p->WriteToBuffer(buffer, 0x400, 7)));
        mark(14);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "profile: params, reset, saves, buffers");
    Check(g_sanitised.size() == 1 && g_sanitised[0] == g_sanitisedExpected, "profile: the read appearance guarded");
    g_sanitised.clear();
}

} // namespace

SELFTEST(Early, TestProfileRest, ViewerTestKit::Cleanup);
