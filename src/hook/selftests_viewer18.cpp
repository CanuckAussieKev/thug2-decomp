// Self-tests for the skater profile's small methods (src/game/skate/skater_profile.cpp): the original and ours run on a
// profile in an arena; its info structure, the arrays and the appearance are recording stand-ins (answering by
// structure and name), and the calls, results and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/obj/object.h"
#include "game/skate/profile_manager.h"
#include "game/skater/trick_component.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkaterProfile;

constexpr uint32_t kProfile = 0x0000;
constexpr uint32_t kOther = 0x0100;
constexpr uint32_t kSlots = 0x0400; // the specials' slot array (+4 size, +8 elements)

std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answers;
std::map<std::pair<uint32_t, uint32_t>, bool> g_flags;
bool g_skater;

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
    s.At({0x00476860, 0x00476F10, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [answer](Regs& r) { return answer(r, 0x6860); });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return g_flags.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Rec(0x76B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00478650, AddressOf(&CStruct::Assign)}, 4, [](Regs& r) {
        Rec(0x8650, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004893A0}, 4, [](Regs& r) {
        Rec(0x93A0, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00488F40}, 8, [](Regs& r) {
        Rec(0x8F40, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    // (0x005480A0 is the empty function - the original's asserts, compiled out: it runs as it is, ours calls
    // Stubs::DoNothing)
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        uint32_t element = r.ecx + 0x40 + 0x20 * r.args[0];
        return r.ecx == At(0x0800) ? *reinterpret_cast<uint32_t*>(element) : element; // (the checksum array: values)
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return At(0x0800);
    });
    s.At({0x00566520}, 0, [](Regs& r) {
        Rec(0x6520, {r.ecx});
        return g_skater ? At(0x0A00) : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return At(0x0B00);
    });
    s.At({0x00522410, AddressOf(&Obj::CTrickComponent::SetTrickMappings)}, 4, [](Regs& r) {
        Rec(0x2410, {r.ecx, r.args[0]});
        return 0u;
    });
}

// Every getter with its value there and not; the stats in and out of range; the counters increased; copying from
// another profile and itself; reading a save part (none, with and without an appearance); the special trick slots
// (each, out of range, empty or not, flagged or not, applied with a skater or without); the checksum set.
void TestProfileMethods()
{
    SaveGlobals({{0x007CE478, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_answers.clear();
        g_flags.clear();
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CE478) = At(0x0900);
        auto* p = Ptr<CSkaterProfile>(kProfile);
        uint32_t info = At(kProfile + 0x5C);
        auto call = [&](auto address, auto ours, auto... args) {
            using R = decltype((p->*ours)(args...));
            R r = original ? Original<R(__thiscall*)(void*, decltype(args)...)>(address)(p, args...) : (p->*ours)(args...);
            return r;
        };
        auto push = [&](uint32_t v) { t.push_back(Rel(v)); };
        // (a value an asserted lookup didn't find is left unset - garbage in both: kept only once it's there)
        int pass = 0;
        auto keep = [&](uint32_t v) { push(pass ? v : 0u); };
        // Getters: missing, then there.
        for (pass = 0; pass < 2; ++pass) {
            if (pass) {
                g_answers[{info, 0xA1DC81F9}] = 0x4A3E;
                g_answers[{info, 0x2AB66CB8}] = At(0x0E00);
                g_answers[{info, 0xD82F8AC8}] = 1;
                g_answers[{info, 0xDDBEE809}] = At(0x0300);
                g_answers[{info, 0xAC9B9EDA}] = 3;
                g_answers[{info, 0x471FC4DD}] = 12;
                g_answers[{info, 0x7D02BCC3}] = 0x287FCD4E;
                g_answers[{info, 0xC15DBF86}] = 0xC1C1;
                g_answers[{info, 0x5E5E}] = At(0x0310);
                g_answers[{info, 0xF36C1878}] = At(0x0E40);
                g_answers[{info, 0x439F4704}] = 7;
                g_flags[{info, 0xF3149AC5}] = true;
            }
            keep(call(0x00586150, &CSkaterProfile::GetName));
            keep(call(0x00586170, &CSkaterProfile::IsPro));
            keep(reinterpret_cast<uint32_t>(call(0x00586210, &CSkaterProfile::GetDisplayName)));
            keep(reinterpret_cast<uint32_t>(call(0x00586130, &CSkaterProfile::GetSpecials)));
            keep(static_cast<uint32_t>(call(0x005860A0, &CSkaterProfile::GetMaxSpecials)));
            keep(static_cast<uint32_t>(call(0x00586040, &CSkaterProfile::GetInfo471FC4DD)));
            for (uint32_t name : {0x7D02BCC3u, 0xC15DBF86u, 0x1234u})
                push(call(0x005860C0, &CSkaterProfile::GetChecksumValue, name));
            keep(reinterpret_cast<uint32_t>(call(0x00586110, &CSkaterProfile::GetInfoStructure, 0x5E5Eu)));
            push(call(0x00586190, &CSkaterProfile::HasFlagF3149AC5));
            push(Text(reinterpret_cast<uint32_t>(call(0x005861C0, &CSkaterProfile::GetStringF36C1878))));
            keep(static_cast<uint32_t>(call(0x00585FB0, &CSkaterProfile::GetStat, 0x439F4704u)));
        }
        for (int value : {-1, 0, 5, 10, 11})
            push(call(0x00585FD0, &CSkaterProfile::SetStat, 0x439F4704u, value));
        push(call(0x00586000, &CSkaterProfile::AddToInfo471FC4DD, 4));
        push(call(0x00586060, &CSkaterProfile::AddMaxSpecials, -2));
        if (original)
            Original<void(__thiscall*)(void*, uint8_t)>(0x005861A0)(p, 0xC7);
        else
            p->SetValue09F350E5(0xC7);
        if (original)
            Original<void(__thiscall*)(void*, const char*)>(0x005861F0)(p, Ptr<char>(0x0E80));
        else
            p->SetStringF36C1878(Ptr<char>(0x0E80));
        push(reinterpret_cast<uint32_t>(call(0x00585ED0, &CSkaterProfile::CopyFrom, Ptr<CSkaterProfile>(kOther))));
        push(reinterpret_cast<uint32_t>(call(0x00585ED0, &CSkaterProfile::CopyFrom, p)));
        // A save part.
        uint32_t save = At(0x0600), part = At(0x0640);
        if (original)
            Original<void(__thiscall*)(void*, uint32_t, void*)>(0x00585F00)(p, 0x5A5A, reinterpret_cast<void*>(save));
        else
            p->ReadFromSave(0x5A5A, reinterpret_cast<CStruct*>(save));
        g_answers[{save, 0x5A5A}] = part;
        g_answers[{part, 0x3476CEA8}] = At(0x0680);
        for (int appearance : {0, 1}) {
            if (appearance)
                g_answers[{part, 0x554C7D6F}] = At(0x06C0);
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, void*)>(0x00585F00)(p, 0x5A5A, reinterpret_cast<void*>(save));
            else
                p->ReadFromSave(0x5A5A, reinterpret_cast<CStruct*>(save));
        }
        // Special trick slots: specials (0x0300) has the slot array (0x0400: 3 slots).
        g_answers[{At(0x0300), 0}] = At(kSlots);
        *Ptr<uint32_t>(kSlots + 4) = 3;
        for (int i = 0; i < 3; ++i) {
            uint32_t slot = At(kSlots) + 0x40 + 0x20 * i;
            g_answers[{slot, 0x5B077CE1}] = 0x7A1C0 + i;
            g_answers[{slot, 0xA92A2280}] = 0x0EE0 + i;
            if (i == 1)
                g_answers[{slot, 0xB56A8816}] = 1;
        }
        for (int i : {0, 1, 2, 3, -1}) {
            CSkaterProfile::SpecialTrick out;
            std::memset(&out, 0xEE, sizeof(out));
            CSkaterProfile::SpecialTrick* r = original ? Original<CSkaterProfile::SpecialTrick*(__thiscall*)(void*, void*, int)>(0x00586630)(p, &out, i)
                                                       : p->GetSpecialTrick(&out, i);
            t.push_back(r == &out);
            // (out of range: the slot past the array has no trick - the local's unset words are garbage in both)
            bool inRange = i >= 0 && i < 3;
            t.insert(t.end(), {inRange ? out.trick : 0u, inRange ? out.keys : 0u, static_cast<uint32_t>(out.flag)});
        }
        CSkaterProfile::SpecialTrick tricks[3] = {{0x111, 0x222, true, {}}, {0xF60C9090, 0x333, false, {}}, {0x444, 0x555, false, {}}};
        for (int i : {0, 1, 2, 3, -1}) {
            for (int apply : {0, 1}) {
                g_skater = (i & 1) != 0;
                CSkaterProfile::SpecialTrick* trick = &tricks[(i + 3) % 3];
                t.push_back(original ? Original<bool(__thiscall*)(void*, int, void*, bool)>(0x00586710)(p, i, trick, apply != 0)
                                     : p->SetSpecialTrick(i, trick, apply != 0));
            }
        }
        for (int i = 0; i < 3; ++i) {
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00587110)(&tricks[i]) : tricks[i].IsEmpty());
            std::memset(&tricks[i], 0xAB, sizeof(tricks[i]));
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00587100)(&tricks[i])
                                                                : tricks[i].Construct())) == Rel(reinterpret_cast<uint32_t>(&tricks[i])));
            t.push_back(tricks[i].flag);
        }
        // The checksum set: the global array (0x0800: 6 items with repeats).
        *Ptr<uint32_t>(0x0800 + 4) = 6;
        const uint32_t items[] = {5, 7, 5, 9, 7, 11};
        for (int i = 0; i < 6; ++i)
            *Ptr<uint32_t>(0x0800 + 0x40 + 0x20 * i) = items[i];
        auto* set = Ptr<Obj::ChecksumSet>(0x1000);
        std::memset(set, 0xCD, sizeof(Obj::ChecksumSet));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00587120)(set) : set->Construct())));
        set->m_count = 2;
        if (original)
            Original<void(__thiscall*)(void*)>(0x00587130)(set);
        else
            set->Fill();
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "profile methods");
}

} // namespace

SELFTEST(Early, TestProfileMethods, ViewerTestKit::Cleanup);
