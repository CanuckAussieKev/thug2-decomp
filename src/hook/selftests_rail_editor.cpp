// Self-tests for the rail editor's lists, pools and packing (rail_editor.cpp): original vs ours on a random world of
// rails (rail_editor_testkit.h), every callee into game code stood in for - the sectors' deletion and colours, the
// memory manager, the object manager, sprintf; the pools' own code runs for real.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/ed/rail_editor.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rail_editor_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RailEditorKit;

template <typename F>
F Orig(uint32_t address)
{
    return Original<F>(address);
}

// The stand-ins every rail editor test uses.
std::vector<StandIn> RailStandIns()
{
    return {
        {0x00595200, 0, 0, Ret::Int, false, 0, {Ours(&Ed::CParkManager::sInstance)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kManager)); }},
        {0x00591DD0, 8, 2, Ret::Bool, true, 0, {Ours(&Ed::CParkGenerator::DeleteSector)}},
        {0x0059E170, 0, 0, Ret::None, false, 0, {Ours(Ed::SetCountsDirty)}},
        {0x00592090, 0, 0, Ret::None, true, 0, {Ours(&Ed::CParkGenerator::RebuildCollision)}},
        {0x0049E670, 0, 0, Ret::None, true},
        {0x0049E680, 4, 1, Ret::None, true},
        {0x0049E9B0, 4, 1, Ret::None, true, 1},
        {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)},
         [](Frame& f, Result& r) { r.eax = reinterpret_cast<uint32_t>(HeapAllocate(f.args[0])); }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(Mem::EnsureManager)}},
        {0x004619F0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CBaseManager::GetObjectByID)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kObjectArea)); }},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kEditor)); }},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x00401CC0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kEditor)) : 0; }},
        {0x006234FE, 0, 0, Ret::None, false, 0, {}, [](Frame& f, Result& r) {
             // sprintf(buffer, "CreatedRailCluster%d", n) - ours calls the DLL's C runtime: not recorded (RunWorld)
             r.eax = static_cast<uint32_t>(std::snprintf(reinterpret_cast<char*>(f.args[0]), 52,
                                                         reinterpret_cast<const char*>(f.args[1]), f.args[2]));
         }},
        {0x00479070, 0, 1, Ret::Int, false, 0, {Ours(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kColours)); }},
        {0x0046CC30, 4, 1, Ret::Int, true, 0, {Ours(&Script::CArray::GetElement)}, [](Frame& f, Result& r) {
             if (f.ecx == reinterpret_cast<uint32_t>(ArenaPtr(kColours)))
                 r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kColours + 0x40));
             else
                 r.eax = 0x1F3 + f.args[0] * 0x101 + (Next() % 7) * 0x10;
         }},
    };
}

// The ops: a number, the original's call and ours on the world (`rails` as built).
uint32_t RunOp(int op, bool o, const std::vector<CEditedRail*>& rails, uint32_t pick)
{
    CRailEditorComponent* ed = Editor();
    CEditedRail* rail = rails.empty() ? nullptr : rails[pick % rails.size()];
    auto pointOf = [&](CEditedRail* r, uint32_t n) {
        CEditedRailPoint* p = r ? r->mp_points : nullptr;
        for (uint32_t i = 0; p && i < n; ++i)
            p = p->mp_next;
        return p;
    };
    float x0 = Coordinate(), z0 = Coordinate(), x1 = Coordinate(), z1 = Coordinate();
    auto* outVec = At<Mth::Vector>(kOut);
    auto* outDist = At<float>(kOut + 0x10);
    auto* outIndex = At<int>(kOut + 0x14);
    auto* outId = At<uint32_t>(kOut + 0x18);
    Mth::Vector* pos = At<Mth::Vector>(kOut + 0x20);
    *pos = {Coordinate(), RandomFloat(10.0f), Coordinate(), 1.0f};
    switch (op) {
    case 0: {
        uint32_t sum = 0;
        for (CEditedRail* r : rails)
            sum = sum * 31 + (o ? Orig<int(__thiscall*)(void*)>(0x004F3B90)(r) : r->CountPoints());
        return sum;
    }
    case 1: {
        if (!rail)
            return 0;
        int index = static_cast<int>(Next() % 9) - 1;
        return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*, int)>(0x004F3BB0)(rail, index)
                                            : rail->GetPoint(index));
    }
    case 2:
        if (!rail)
            return 0;
        return o ? Orig<int(__thiscall*)(void*, float, float, float, float)>(0x004F4310)(rail, x0, z0, x1, z1)
                 : rail->CountPointsInside(x0, z0, x1, z1);
    case 3:
        if (!rail)
            return 0;
        return o ? Orig<bool(__thiscall*)(void*, float, float, float, float)>(0x004F44B0)(rail, x0, z0, x1, z1)
                 : rail->AnyPointOutside(x0, z0, x1, z1);
    case 4: {
        if (!rail)
            return 0;
        int skip = static_cast<int>(Next() % 5) - 1;
        return o ? Orig<bool(__thiscall*)(void*, const void*, void*, float*, int*, int)>(0x004F4280)(
                       rail, pos, outVec, outDist, outIndex, skip)
                 : rail->FindNearestPoint(pos, outVec, outDist, outIndex, skip);
    }
    case 5: {
        uint32_t id = rail && Next() % 3 ? rail->m_id : 0x777;
        uint32_t n = o ? Orig<int(__thiscall*)(void*)>(0x004F3C70)(ed) : ed->CountPoints();
        uint32_t found = reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*, uint32_t)>(0x004F3C50)(ed, id)
                                                      : ed->FindRail(id));
        return n * 0x10001 + found;
    }
    case 6: {
        uint32_t a = o ? Orig<bool(__thiscall*)(void*, float, float, float, float)>(0x004F4600)(ed, x0, z0, x1, z1)
                       : ed->AnyPointOutside(x0, z0, x1, z1);
        uint32_t b = o ? Orig<int(__thiscall*)(void*, float, float, float, float)>(0x004F4690)(ed, x0, z0, x1, z1)
                       : ed->CountPointsInside(x0, z0, x1, z1);
        return a + b * 2;
    }
    case 7: {
        uint32_t skipRail = rail && Next() % 2 ? rail->m_id : 0x999;
        int skipIndex = static_cast<int>(Next() % 4) - 1;
        if (o) {
            auto find = Orig<bool(__thiscall*)(void*, const void*, void*, float*, uint32_t*, int*, uint32_t, int)>(
                0x004F4710);
            ScrubStack(); // (right before the call: nothing else may write below the stack pointer in between)
            return find(ed, pos, outVec, outDist, outId, outIndex, skipRail, skipIndex);
        }
        return ed->FindNearestPoint(pos, outVec, outDist, outId, outIndex, skipRail, skipIndex);
    }
    case 8: {
        if (!rail)
            return 0;
        if (Next() % 6 == 0)
            PointPool()->m_used = 400; // (full as far as NewPoint asks)
        return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*)>(0x004F6240)(rail) : rail->NewPoint());
    }
    case 9: {
        if (!rail || !rail->mp_points)
            return 0;
        CEditedRailPoint* source = pointOf(rails[Next() % rails.size()], Next() % 3);
        if (!source)
            source = rail->mp_points;
        if (o)
            Orig<void(__thiscall*)(void*, const void*)>(0x004F62D0)(rail, source);
        else
            rail->AddPointCopy(source);
        return 1;
    }
    case 10: {
        if (!rail)
            return 0;
        CEditedRailPoint* p = Next() % 8 ? pointOf(rail, Next() % 5) : nullptr;
        if (!p && Next() % 2)
            p = rail->mp_points;
        if (Next() % 4 == 0)
            RailPool()->m_used = 200;
        return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*, void*)>(0x004F6300)(rail, p)
                                            : rail->CutAt(p));
    }
    case 11: {
        if (rails.size() < 2)
            return 0;
        CEditedRail* from = rails[(pick + 1) % rails.size()];
        if (o)
            Orig<void(__thiscall*)(void*, const void*)>(0x004F6F40)(rail, from);
        else
            rail->CopyFrom(from);
        return 1;
    }
    case 12: {
        if (!rail)
            return 0;
        // a box around some of the points
        x0 = -60.0f + RandomFloat(40.0f), x1 = 60.0f + RandomFloat(40.0f);
        z0 = -60.0f + RandomFloat(40.0f), z1 = 60.0f + RandomFloat(40.0f);
        CEditedRail* list = Next() % 3 == 0 && rails.size() > 1 ? rails[(pick + 1) % rails.size()] : nullptr;
        return reinterpret_cast<uint32_t>(
            o ? Orig<void*(__thiscall*)(const void*, float, float, float, float, void*)>(0x004F7100)(rail, x0, z0, x1,
                                                                                                      z1, list)
              : rail->CopyInside(x0, z0, x1, z1, list));
    }
    case 13: {
        if (!rail)
            return 0;
        if (Next() % 2) {
            if (o)
                Orig<void(__thiscall*)(void*)>(0x004F6E20)(rail);
            else
                rail->Clear();
            return 1;
        }
        auto* fresh = static_cast<CEditedRail*>(Take(RailPool()));
        if (!fresh)
            return 0;
        return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*)>(0x004F70C0)(fresh)
                                            : fresh->Construct());
    }
    case 14:
        return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*)>(0x004F72C0)(ed) : ed->NewRail());
    case 15:
        if (!rail)
            return 0;
        if (o)
            Orig<void(__thiscall*)(void*, void*)>(0x004F7270)(ed, rail);
        else
            ed->DeleteRail(rail);
        return 1;
    case 16:
        if (o)
            Orig<void(__thiscall*)(void*)>(0x004F7370)(ed);
        else
            ed->DeleteShortRails();
        return 1;
    case 17: {
        if (!rail || !rail->mp_points)
            return 0;
        CEditedRailPoint* p = pointOf(rail, Next() % 4);
        if (!p)
            p = rail->mp_points;
        if (Next() % 4 == 0)
            RailPool()->m_used = 200;
        return o ? Orig<bool(__thiscall*)(void*, void*, void*)>(0x004F73E0)(ed, rail, p) : ed->CutRail(rail, p);
    }
    case 18: {
        if (!rail)
            return 0;
        int inside = static_cast<int>(Next() % 3);
        x0 = -50.0f + RandomFloat(40.0f), x1 = 50.0f + RandomFloat(40.0f);
        z0 = -50.0f + RandomFloat(40.0f), z1 = 50.0f + RandomFloat(40.0f);
        return o ? Orig<bool(__thiscall*)(void*, void*, float, float, float, float, int)>(0x004F7410)(ed, rail, x0, z0,
                                                                                                       x1, z1, inside)
                 : ed->CutPointInOrOut(rail, x0, z0, x1, z1, inside);
    }
    case 19: {
        int inside = static_cast<int>(Next() % 2);
        x0 = -50.0f + RandomFloat(60.0f), x1 = 50.0f + RandomFloat(60.0f);
        z0 = -50.0f + RandomFloat(60.0f), z1 = 50.0f + RandomFloat(60.0f);
        if (Next() % 2) {
            float t = x0;
            x0 = x1;
            x1 = t;
        }
        if (o)
            Orig<void(__thiscall*)(void*, float, float, float, float, int)>(0x004F74B0)(ed, x0, z0, x1, z1, inside);
        else
            ed->DeletePoints(x0, z0, x1, z1, inside);
        return 1;
    }
    case 20:
        if (o)
            Orig<void(__thiscall*)(void*)>(0x004F7550)(ed);
        else
            ed->Clear();
        return 1;
    case 21:
        if (Next() % 2)
            return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(void*)>(0x004F45B0)(ed) : ed->GetPacked());
        if (o)
            Orig<void(__thiscall*)(void*)>(0x004F4500)(ed);
        else
            ed->Pack();
        return 1;
    case 22: {
        // a small buffer, set and unpacked
        auto* buffer = At<Ed::RailBuffer>(kScratch);
        std::memset(buffer, 0, 0x200);
        buffer->m_railCount = static_cast<uint16_t>(Next() % 4);
        int records = 0;
        for (int i = 0; i < buffer->m_railCount; ++i) {
            buffer->m_pointCounts[i] = static_cast<uint16_t>(Next() % 5);
            records += buffer->m_pointCounts[i];
        }
        auto* data = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(buffer) + 0x192);
        for (int i = 0; i < records * 5; ++i)
            data[i] = static_cast<uint16_t>(Next());
        if (Next() % 3 == 0) // (the pool runs out partway as far as NewPoint asks: its records skipped)
            PointPool()->m_used = 397 + static_cast<int>(Next() % 3) + ed->CountPoints(); // (Clear frees those)
        if (o) {
            Orig<void(__thiscall*)(void*, const void*)>(0x004F3C00)(ed, buffer);
            Orig<void(__thiscall*)(void*)>(0x004F7DF0)(ed);
        } else {
            ed->SetPacked(buffer);
            ed->Unpack();
        }
        return 1;
    }
    case 23:
        return reinterpret_cast<uint32_t>(
            o ? Orig<void*(__thiscall*)(const void*, float, float, float, float)>(0x004F7230)(ed, -60.0f, -70.0f, 80.0f,
                                                                                             50.0f)
              : ed->CopyRailsInside(-60.0f, -70.0f, 80.0f, 50.0f));
    case 24: {
        uint32_t which = Next() % 5;
        bool active = Next() % 2;
        if (o) {
            static const uint32_t calls[5] = {0x004F3C20, 0x004F3D30, 0x004F45C0, 0x004F45E0, 0x004F3BD0};
            if (which == 1)
                Orig<void(__thiscall*)(void*, bool)>(calls[1])(ed, active);
            else if (which == 4)
                rail ? Orig<void(__thiscall*)(void*)>(calls[4])(rail) : void();
            else
                Orig<void(__thiscall*)(void*)>(calls[which])(ed);
        } else {
            switch (which) {
            case 0:
                ed->UnHighlightAll();
                break;
            case 1:
                ed->SetActive(active);
                break;
            case 2:
                ed->DeleteAllSegments();
                break;
            case 3:
                ed->DeleteAllPosts();
                break;
            default:
                if (rail)
                    rail->ClearColours();
                break;
            }
        }
        return which;
    }
    case 25: {
        char name[32];
        std::snprintf(name, sizeof(name), "CreatedRailCluster%d", static_cast<int>(Next() % 5));
        uint32_t cluster = Next() % 5 ? Crc::GenerateCRCFromString(name) : 0x1234;
        uint32_t which = Next() % 3;
        int colour = static_cast<int>(Next() % 4);
        if (which == 0)
            return reinterpret_cast<uint32_t>(o ? Orig<void*(__thiscall*)(const void*, uint32_t)>(0x004F3CA0)(ed,
                                                                                                            cluster)
                                                : ed->FindRailByCluster(cluster));
        if (which == 1) {
            if (o)
                Orig<void(__thiscall*)(void*, uint32_t)>(0x004F3D10)(ed, cluster);
            else
                ed->ClearClusterColours(cluster);
        } else {
            if (o)
                Orig<void(__thiscall*)(void*, uint32_t, int)>(0x004F4950)(ed, cluster, colour);
            else
                ed->SetClusterColour(cluster, colour);
        }
        return which;
    }
    case 26:
        if (o)
            Orig<void(__thiscall*)(void*)>(0x004F48F0)(ed);
        else
            ed->DeleteDottedLine();
        return 1;
    case 27:
        return reinterpret_cast<uint32_t>(o ? Orig<void*(__cdecl*)()>(0x004F4970)() : CRailEditorComponent::Instance());
    case 28:
        return o ? Orig<int(__cdecl*)()>(0x004F6BF0)() : CRailEditorComponent::FreePoints();
    case 29: {
        // made, finalised, destroyed (the pools made by the constructor live in the heap)
        uint32_t made = reinterpret_cast<uint32_t>(o ? Orig<void*(__cdecl*)()>(0x004F7FB0)()
                                                     : CRailEditorComponent::Create());
        Record(made);
        if (!made)
            return 0;
        ed->m_object = reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kObjectArea));
        if (o)
            Orig<void(__thiscall*)(void*)>(0x004F47E0)(ed);
        else
            ed->Finalize();
        // a rail of two points from the new pools, then destroyed
        CEditedRail* r = o ? Orig<CEditedRail*(__thiscall*)(void*)>(0x004F72C0)(ed) : ed->NewRail();
        for (int i = 0; i < 2; ++i)
            o ? (void)Orig<void*(__thiscall*)(void*)>(0x004F6240)(r) : (void)r->NewPoint();
        uint32_t flags = Next() % 2;
        if (o)
            Orig<void*(__thiscall*)(void*, unsigned int)>(0x004F8010)(ed, flags);
        else
            ed->Destroy(flags);
        return 1;
    }
    default: {
        // the pools' new / delete
        uint32_t which = Next() % 3;
        if (which == 0) {
            void* m = o ? Orig<void*(__cdecl*)(unsigned int)>(0x004F4AF0)(0x10) : CRailEditorComponent::NewRailMemory(0x10);
            return reinterpret_cast<uint32_t>(m);
        }
        if (which == 1 && rail) {
            // (a rail item, or a heap block)
            void* item = Next() % 3 ? static_cast<void*>(rail) : HeapAllocate(0x10);
            o ? Orig<void(__cdecl*)(void*)>(0x004F4B30)(item) : CRailEditorComponent::FreeRail(item);
            return 1;
        }
        if (rail && rail->mp_points) {
            void* item = Next() % 3 ? static_cast<void*>(rail->mp_points) : HeapAllocate(0x28);
            o ? Orig<void(__cdecl*)(void*)>(0x004F4A10)(item) : CRailEditorComponent::FreePoint(item);
        }
        return 2;
    }
    }
}

constexpr int kOps = 31;

void TestRailEditorLists()
{
    StandIns standIns(RailStandIns());
    Globals saved = Globals::Read();
    bool same = true;
    int counts[kOps] = {};
    for (int variant = 0; variant < 12000 && same; ++variant) {
        g_rng = 0x51ED2701u * (variant + 1);
        std::vector<CEditedRail*> rails = BuildWorld(5, 6);
        int op = static_cast<int>(Next() % kOps);
        uint32_t pick = Next();
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunWorld(seed, [&] { return RunOp(op, true, rails, pick); });
        auto actual = RunWorld(seed, [&] { return RunOp(op, false, rails, pick); });
        char what[48];
        std::snprintf(what, sizeof(what), "rail editor op %d", op);
        same = Compare(expected, actual, what, variant);
        ++counts[op];
    }
    saved.Write();
    Check(same, "ed: the rail editor's rails, points, pools, packing and component (31 kinds of call)");
}

} // namespace

SELFTEST(Early, TestRailEditorLists);
