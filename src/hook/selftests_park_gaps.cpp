// Self-tests for the gap manager's slots (src/game/ed/gap_slots.cpp): the original and ours on the same stand-in
// arena (viewer_testkit.h's); every result and the whole arena compared.

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/ed/park.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Ed::CGapManager;
using Ed::CMetaPiece;
using Ed::CPiece;
using Ed::GapInfo;

constexpr uint32_t kManager = 0x2000;
constexpr uint32_t kRecords = 0x3000; // gap records, 0x100 apart

void TestParkGaps()
{
    SaveGlobals({{0x007CE5D4, 4, {}}});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    s->At({0x006234FE}, 0, [](Regs& r) {
        char* out = reinterpret_cast<char*>(r.args[0]);
        return static_cast<uint32_t>(std::vsnprintf(out, 32, reinterpret_cast<const char*>(r.args[1]),
                                                    reinterpret_cast<va_list>(&r.args[2])));
    });
    s->At({0x004790C0, AddressOf(static_cast<int (*)(const char*)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x90C0, {Text(r.args[0])});
        auto it = g_integers.find(Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0])));
        return it == g_integers.end() ? 0u : static_cast<uint32_t>(it->second);
    });
    s->At({0x005902B0}, 4, [](Regs& r) {
        Rec(0x02B0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        g_printfArgs = 2;
        g_integers.clear();
        auto rec = [&](uint32_t value) { t.push_back(Rel(value)); };
        auto* manager = Ptr<CGapManager>(kManager);
        std::memset(manager, 0xCD, 0xA8);
        GameVariable<uint32_t>(0x007CE5D4) = 0;
        rec(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, void*)>(0x0058CCC0)(manager, Ptr<void>(0x7700))
                                                : manager->Construct(Ptr<Ed::CParkManager>(0x7700))));
        rec(GameVariable<uint32_t>(0x007CE5D4));
        rec(reinterpret_cast<uint32_t>(original ? Original<void*(__cdecl*)()>(0x0058CBB0)() : CGapManager::sInstance()));
        auto* words = Ptr<uint32_t>(kManager);
        auto* firstHalf = Ptr<uint8_t>(kManager + 0x84);
        auto freeSlot = [&] {
            rec(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x0058C990)(manager)
                                               : manager->GetFreeGapIndex()));
        };
        freeSlot();
        // Gaps in slots 0, 1 (first halves) and 2 (gap 0's second half); every record's pieces and metas distinct.
        for (int i = 0; i < 3; ++i) {
            uint32_t* record = Ptr<uint32_t>(kRecords + 0x100 * i);
            for (int k = 0; k < 6; ++k)
                record[k] = At(0x6000 + 0x100 * i + 0x10 * k);
            std::snprintf(reinterpret_cast<char*>(record) + 0x18, 0x80, "gap name %d", i);
            reinterpret_cast<GapInfo*>(reinterpret_cast<uint8_t*>(record) + 0x98)->m_score = 100 * i + 5;
        }
        words[1] = At(kRecords);
        words[2] = At(kRecords + 0x100);
        words[3] = At(kRecords);
        firstHalf[0] = 1;
        firstHalf[1] = 1;
        firstHalf[2] = 0;
        freeSlot();
        // The second half of gap 1 (slot 1), then with every slot taken.
        original ? Original<void(__thiscall*)(void*, void*, void*, int)>(0x0058C9B0)(manager, Ptr<void>(0x7000), Ptr<void>(0x7010), 1)
                 : manager->EndGap(Ptr<void>(0x7000), Ptr<CMetaPiece>(0x7010), 1);
        PushArena(t);
        for (int i = 5; i < 33; ++i)
            words[i] = At(kRecords + 0x200);
        freeSlot();
        original ? Original<void(__thiscall*)(void*, void*, void*, int)>(0x0058C9B0)(manager, Ptr<void>(0x7100), Ptr<void>(0x7110), 0)
                 : manager->EndGap(Ptr<void>(0x7100), Ptr<CMetaPiece>(0x7110), 0);
        for (int i = 5; i < 33; ++i)
            words[i] = 0;
        // Lookups by metapiece: each half's, one not in a gap; with and without the index.
        const uint32_t metas[] = {At(0x6040), At(0x6050), At(0x6140), At(0x7010), At(0x6150), At(0x9999)};
        for (uint32_t meta : metas) {
            for (int withIndex = 0; withIndex < 2; ++withIndex) {
                int index = -7;
                rec(original ? Original<bool(__thiscall*)(void*, void*, int*)>(0x0058CA10)(manager, reinterpret_cast<void*>(meta), withIndex ? &index : nullptr)
                             : manager->GetGapIndex(reinterpret_cast<CMetaPiece*>(meta), withIndex ? &index : nullptr));
                rec(static_cast<uint32_t>(index));
            }
        }
        // Trigger names by piece (slot 1 is odd: with PrintGaps on, printed).
        const uint32_t pieces[] = {At(0x6020), At(0x6030), At(0x6120), At(0x7000), At(0x6130)};
        for (int print = 0; print < 2; ++print) {
            g_integers[Crc::GenerateCRCFromString("PrintGaps")] = print;
            for (uint32_t piece : pieces)
                rec(original ? Original<uint32_t(__thiscall*)(void*, void*)>(0x0058CA60)(manager, reinterpret_cast<void*>(piece))
                             : manager->GetGapTriggerName(reinterpret_cast<CPiece*>(piece)));
        }
        // Infos: slots 0-4 (3 and 4 empty... 3 holds gap 1's second half now).
        for (int slot = 0; slot < 5; ++slot) {
            int half = -7;
            rec(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int, int*)>(0x0058CB20)(manager, slot, &half)
                                                    : manager->GetGapInfo(slot, &half)));
            rec(static_cast<uint32_t>(half));
        }
        // Highlights: a gap with both pieces, one with only its first, one with only its second, an empty slot.
        Ptr<uint32_t>(kRecords + 0x100)[3] = 0;
        Ptr<uint32_t>(kRecords + 0x200)[2] = 0;
        Ptr<uint32_t>(kRecords + 0x200)[3] = At(0x6230);
        words[5] = At(kRecords + 0x200);
        for (int slot : {0, 1, 4, 6})
            original ? Original<void(__thiscall*)(void*, int)>(0x0058CB70)(manager, slot) : manager->SetCurrentGap(slot);
        PushArena(t);
        // A gap's info: made, and its length for each turn (and a bad one), each half.
        auto* info = Ptr<GapInfo>(0x8000);
        std::memset(info, 0xCD, sizeof(GapInfo));
        rec(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0058CBC0)(info) : info->Construct()));
        info->m_pos[0].w = 3;
        info->m_pos[0].l = 5;
        info->m_pos[1].w = 2;
        info->m_pos[1].l = 7;
        info->m_leftExtent[0] = 1;
        info->m_rightExtent[0] = 4;
        info->m_leftExtent[1] = 6;
        info->m_rightExtent[1] = 0;
        for (int rot : {0, 1, 2, 3, 4, -1}) {
            for (int half = 0; half < 2; ++half) {
                info->m_rot[half] = rot;
                float length;
                if (original) {
                    auto f = Original<float(__thiscall*)(void*, int)>(0x0058CBF0);
                    length = f(info, half);
                } else {
                    length = info->GetLength(half);
                }
                rec(std::bit_cast<uint32_t>(length));
            }
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park gaps: the gap manager's slots, a gap's info");
}

} // namespace

SELFTEST(Early, TestParkGaps, ViewerTestKit::Cleanup);
