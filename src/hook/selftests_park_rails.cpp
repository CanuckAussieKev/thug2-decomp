// Self-tests for the park's rail sets, strings and points (src/game/ed/park_rails.cpp): the original and ours on the
// same stand-in arena (viewer_testkit.h's), the memory manager stood in, the item pools real; a script of calls made
// on both, every result and the whole arena compared.

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/pool.h"
#include "game/ed/park_rails.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Ed::CParkRailPoint;
using Ed::CParkRailSet;
using Ed::CParkRailString;

constexpr uint32_t kSet = 0x4000;
constexpr uint32_t kSet2 = 0x4100;
constexpr uint32_t kLoose = 0x4200; // a string from the heap (in no pool)

void TestParkRails()
{
    SaveGlobals({{0x007CE594, 16, {}}, {0x007CE5A4, 16, {}}});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    s->At({0x006234FE}, 0, [](Regs& r) {
        char* out = reinterpret_cast<char*>(r.args[0]);
        int n = std::vsnprintf(out, 64, reinterpret_cast<const char*>(r.args[1]),
                             reinterpret_cast<va_list>(&r.args[2]));
        return static_cast<uint32_t>(n);
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        // The pairs: current 1 for the strings (0 for the points), a pool "of their own" there already (lent over).
        std::memset(reinterpret_cast<void*>(0x007CE594), 0, 16);
        std::memset(reinterpret_cast<void*>(0x007CE5A4), 0, 16);
        GameVariable<int>(0x007CE594) = 1;
        GameVariable<uint8_t>(0x007CE598 + 1) = 1;
        auto* set = Ptr<CParkRailSet>(kSet);
        auto* set2 = Ptr<CParkRailSet>(kSet2);
        std::memset(set, 0xCD, 0x100);
        std::memset(set2, 0xCD, 0x100);
        auto rec = [&](uint32_t value) { t.push_back(Rel(value)); };
        auto ptr = [&](const void* p) { rec(reinterpret_cast<uint32_t>(p)); };

        // Made, and their pools.
        ptr(original ? Original<void*(__thiscall*)(void*)>(0x0058AFF0)(set) : set->Construct());
        ptr(original ? Original<void*(__thiscall*)(void*)>(0x0058AFF0)(set2) : set2->Construct());
        for (int i = 0; i < 6; ++i) // (the last string isn't cleared)
            rec(reinterpret_cast<uint32_t*>(set)[i]);
        original ? Original<void(__thiscall*)(void*, int, int, bool)>(0x0058B0C0)(set, 12, 5, true)
                 : set->CreatePools(12, 5, true);
        original ? Original<void(__thiscall*)(void*, int, int, bool)>(0x0058B0C0)(set2, 40, 12, false)
                 : set2->CreatePools(40, 12, false);
        auto newString = [&](CParkRailSet* in) {
            return original ? Original<CParkRailString*(__thiscall*)(void*)>(0x0058B370)(in) : in->NewString();
        };
        auto newPoint = [&](CParkRailSet* in) {
            return original ? Original<CParkRailPoint*(__thiscall*)(void*)>(0x0058B2D0)(in) : in->NewPoint();
        };
        auto addPoint = [&](CParkRailString* string, CParkRailPoint* point) {
            original ? Original<void(__thiscall*)(void*, void*)>(0x0058AFC0)(string, point) : string->AddPoint(point);
        };
        auto addString = [&](CParkRailSet* in, CParkRailString* string) {
            original ? Original<void(__thiscall*)(void*, void*)>(0x0058B010)(in, string) : in->AddString(string);
        };
        auto segments = [&](CParkRailString* string) {
            rec(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x0058B040)(string)
                                               : string->GetSegmentCount()));
        };
        // Three source strings: a climbing one (3 points), a loop of 4 (id 7), one of a single point (id 7).
        const int counts[3] = {3, 4, 1};
        for (int i = 0; i < 3; ++i) {
            CParkRailString* string = newString(set);
            ptr(string);
            rec(string->m_flags);
            string->m_type = i == 0 ? CParkRailString::kClimbingNode : 0x1234 + i;
            string->m_flags = static_cast<uint8_t>(i == 1 ? 0xF5 : 0xF8 | (i == 2 ? 2 : 0));
            string->m_id = i == 0 ? 3 : 7;
            for (int j = 0; j < counts[i]; ++j) {
                CParkRailPoint* point = newPoint(set);
                ptr(point);
                for (int k = 0; k < 4; ++k)
                    rec(std::bit_cast<uint32_t>((&point->m_pos.x)[k]));
                float* f = reinterpret_cast<float*>(point);
                for (int k = 0; k < 11; ++k)
                    f[k] = static_cast<float>((i * 11 + j * 5 + k * 3) % 17) * 1.375f - 7.25f;
                addPoint(string, point);
            }
            addString(set, string);
            segments(string);
        }
        segments(Ptr<CParkRailString>(0x4300)); // (an empty one: no points)
        std::memset(Ptr<void>(0x4300), 0, 0x20);
        segments(Ptr<CParkRailString>(0x4300));
        rec(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x0058B070)(set) : set->CountPoints()));
        for (uint32_t id : {3u, 7u, 9u})
            for (int index = 0; index < 3; ++index)
                ptr(original ? Original<void*(__thiscall*)(void*, uint32_t, int)>(0x0058B090)(set, id, index)
                             : set->FindString(id, index));
        // Copied into the second set: turned 0-4 (and -1), at an offset, climbing and not.
        const Mth::Vector offset = {100.5f, -20.25f, 340.75f, 1.0f};
        for (int rot : {0, 1, 2, 3, 4, -1}) {
            for (int which = 0; which < 3; which += 2) {
                CParkRailString* from = set->mp_first;
                for (int k = 0; k < which; ++k)
                    from = from->mp_next;
                CParkRailString* copy = newString(set2);
                copy->m_flags = 0x5C;
                original ? Original<void(__thiscall*)(void*, const void*, const void*, int, uint32_t, float)>(0x0058B420)(
                               copy, from, &offset, rot, 0xAB00 + rot, 0.5f + static_cast<float>(rot))
                         : copy->CopyFrom(from, &offset, rot, 0xAB00 + rot, 0.5f + static_cast<float>(rot));
                addString(set2, copy);
            }
        }
        PushArena(t);
        // Taken apart: a string's points, a string (and null, and one from the heap), every string, the pools.
        original ? Original<void(__thiscall*)(void*)>(0x0058B620)(set->mp_first) : set->mp_first->DeletePoints();
        CParkRailString* second = set->mp_first->mp_next;
        original ? Original<void(__thiscall*)(void*, void*)>(0x0058B5B0)(set, nullptr) : set->DeleteString(nullptr);
        set->mp_first->mp_next = second->mp_next; // (unlinked first, as the park generator does)
        original ? Original<void(__thiscall*)(void*, void*)>(0x0058B5B0)(set, second) : set->DeleteString(second);
        original ? Original<void(__thiscall*)(void*, void*)>(0x0058B5B0)(set, Ptr<void>(kLoose))
                 : set->DeleteString(Ptr<CParkRailString>(kLoose));
        original ? Original<void(__cdecl*)(void*)>(0x0058B220)(Ptr<void>(kLoose + 0x40))
                 : CParkRailString::Free(Ptr<void>(kLoose + 0x40));
        PushArena(t);
        original ? Original<void(__thiscall*)(void*)>(0x0058B700)(set2) : set2->DeleteStrings();
        original ? Original<void(__thiscall*)(void*)>(0x0058B700)(set) : set->DeleteStrings();
        original ? Original<void(__thiscall*)(void*)>(0x0058B280)(set) : set->DestroyPools();
        set2->mp_pointPool = nullptr; // (strings' pool only)
        original ? Original<void(__thiscall*)(void*)>(0x0058B280)(set2) : set2->DestroyPools();
        original ? Original<void(__thiscall*)(void*)>(0x0058B280)(set2) : set2->DestroyPools(); // (none)
        for (uint32_t a : {0x007CE594u, 0x007CE5A4u})
            for (int i = 0; i < 4; ++i)
                rec(reinterpret_cast<uint32_t*>(a)[i]);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park rails: sets, strings and points");
}

} // namespace

SELFTEST(Early, TestParkRails, ViewerTestKit::Cleanup);
