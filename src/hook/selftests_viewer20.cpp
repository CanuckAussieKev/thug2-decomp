// Self-tests for the object / camera animation movies (src/game/obj/anim_movies.cpp) and the trick object pieces
// (src/game/obj/trick_objects.cpp): the original and ours run on movies in an arena; the base movie's code, the
// timeline (a stand-in vtable), the asset, the camera and the parameter lookups are recording stand-ins, and the
// calls, results and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/obj/anim_movies.h"
#include "game/obj/trick_objects.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CCameraAnimMovie;
using Obj::CObjectAnimMovie;

constexpr uint32_t kObjects = 0x0000; // the object movie (0x64)
constexpr uint32_t kCamera = 0x0100;  // the camera movie (0x94)
constexpr uint32_t kParams = 0x0200;
constexpr uint32_t kAsset = 0x0300;   // an animation asset (its length first)

std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> g_answers; // (structure, name) -> the value's words
std::map<std::pair<uint32_t, uint32_t>, bool> g_flags;
bool g_asset;
bool g_complete;
void* g_timelineVtable[8];
void* g_managerVtable[1];

void Install(StandIns& s)
{
    InstallCommon(s);
    auto answer = [](Regs& r, uint32_t tag) {
        Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
        auto it = g_answers.find({r.ecx, r.args[0]});
        if (it == g_answers.end())
            return 0u;
        std::memcpy(reinterpret_cast<void*>(r.args[1]), it->second.data(), 4 * it->second.size());
        return 1u;
    };
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [answer](Regs& r) { return answer(r, 0x6950); });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [answer](Regs& r) { return answer(r, 0x6630); });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [answer](Regs& r) { return answer(r, 0x66D0); });
    s.At({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [answer](Regs& r) { return answer(r, 0x6FB0); });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return g_flags.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Rec(0x8480, {r.ecx, r.args[0]});
        return r.ecx;
    });
    // The base movie.
    s.At({0x00468FB0}, 0, [](Regs& r) {
        Rec(0x8FB0, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x11, 0x2C);
        return r.ecx;
    });
    s.At({0x00469230}, 0, [](Regs& r) {
        Rec(0x9230, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0u;
    });
    s.At({0x00469050}, 4, [](Regs& r) {
        Rec(0x9050, {r.ecx, r.args[0]});
        return 1u;
    });
    s.At({0x00468F50}, 4, [](Regs& r) {
        Rec(0x8F50, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00543150}, 0, [](Regs& r) {
        Rec(0x3150, {r.ecx});
        return 0x4A11E0u;
    });
    s.At({0x00407C00}, 8, [](Regs& r) {
        Rec(0x7C00, {r.ecx, r.args[0], r.args[1]});
        return g_asset ? At(kAsset) : 0u;
    });
    s.At({0x004A0CF0}, 12, [](Regs& r) {
        Rec(0x0CF0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 7u;
    });
    // The timeline.
    s.At({0x00481650}, 0, [](Regs& r) {
        Rec(0x1650, {r.ecx});
        *reinterpret_cast<void***>(r.ecx) = g_timelineVtable;
        reinterpret_cast<float*>(r.ecx)[6] = 2.0f;
        return r.ecx;
    });
    s.At({0x00481690}, 0, [](Regs& r) {
        Rec(0x1690, {r.ecx});
        return 0u;
    });
    s.At({0x004813F0}, 0, [](Regs& r) {
        Rec(0x13F0, {r.ecx});
        return g_complete ? 1u : 0u;
    });
    g_timelineVtable[2] = s.Thunk(0, [](Regs& r) {
        Rec(0x0702, {r.ecx});
        reinterpret_cast<float*>(r.ecx)[6] += 0.5f;
        return 0u;
    });
    g_timelineVtable[3] = s.Thunk(24, [](Regs& r) {
        Rec(0x0703, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
    // The asset and the camera.
    s.At({0x00482F10}, 0, [](Regs& r) {
        Rec(0x2F10, {r.ecx});
        return 0u;
    });
    s.At({0x00482F40}, 16, [](Regs& r) {
        Rec(0x2F40, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
    s.At({0x00485B20}, 4, [](Regs& r) {
        Rec(0x5B20, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00485A50}, 0, [](Regs& r) {
        Rec(0x5A50, {r.ecx});
        return 0u;
    });
    s.At({0x00587A50}, 0, [](Regs& r) {
        Rec(0x7A50, {r.ecx});
        return 0u;
    });
    // The trick object manager's deleting destructor.
    g_managerVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x0800, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

template <typename R, typename Ours, typename... A>
R Call(bool original, uint32_t address, void* object, Ours ours, A... args)
{
    if (original)
        return Original<R(__thiscall*)(void*, A...)>(address)(object, args...);
    return ours(args...);
}

void TestAnimMovies()
{
    SaveGlobals({{0x007CE500, 4, {}}, {0x007CE504, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_answers.clear();
        g_flags.clear();
        std::vector<uint32_t>& t = traces[run];
        auto mark = [&](uint32_t m) { g_trace.push_back(0xEE000000 | m); };
        auto* o = Ptr<CObjectAnimMovie>(kObjects);
        auto* c = Ptr<CCameraAnimMovie>(kCamera);
        uint32_t params = At(kParams);
        auto* p = Ptr<CStruct>(kParams);
        *Ptr<float>(kAsset) = 3.25f;
        std::memset(o, 0xCD, sizeof(*o));
        std::memset(c, 0xCD, sizeof(*c));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x005871B0)(o) : o->Construct())));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x005873F0)(c) : c->Construct())));
        mark(1);
        o->m_base.m_camera = Ptr<Gfx::Camera>(0x0600);
        c->m_base.m_camera = Ptr<Gfx::Camera>(0x0640);
        // Load: no asset found; found (forwards; looping and backwards); a timer.
        auto loadBoth = [&]() {
            t.push_back(Call<bool>(original, 0x005875D0, o, [&](CStruct* q) { return o->Load(q); }, p));
            t.push_back(Call<bool>(original, 0x00587F70, c, [&](CStruct* q) { return c->Load(q); }, p));
        };
        g_asset = false;
        loadBoth();
        mark(2);
        g_asset = true;
        g_answers[{params, 0x17E7321E}] = {0x42700000};
        g_answers[{params, 0x531E4D28}] = {0x7A26E7};
        g_answers[{params, 0x906642BF}] = {0, 0, 0, 0x3F800000};
        loadBoth();
        mark(3);
        g_flags[{params, 0x5EA0E211}] = true;
        g_flags[{params, 0xF8CFD515}] = true;
        g_answers[{params, 0x906642BF}] = {0, 0, 0x40000000, 0x3F800000};
        g_answers[{params, 0xA00D3D6E}] = {0x3F000000, 0, 0, 0x3F800000};
        loadBoth();
        mark(4);
        g_flags.clear();
        g_flags[{params, 0xEE721991}] = true;
        g_answers[{params, 0x019176C5}] = {45};
        g_answers.erase({params, 0x17E7321E});
        g_answers[{params, 0x906642BF}] = {0x7FC00000, 0, 0, 0x3F800000}; // a NaN: set
        loadBoth();
        mark(5);
        // Start, IsDone, the held check, with each flag.
        for (int i = 0; i < 8; ++i) {
            g_complete = (i & 1) != 0;
            o->m_base.m_unknown18 = c->m_base.m_unknown18 = (i & 2) ? 1 : 0;
            o->m_base.m_unknown1A = c->m_base.m_unknown1A = (i & 4) ? 1 : 0;
            o->m_anim = (i & 2) ? Ptr<void>(kAsset) : nullptr;
            c->m_anim = (i & 4) ? Ptr<void>(kAsset) : nullptr;
            t.push_back(Call<bool>(original, 0x00587270, o, [&]() { return o->Start(); }));
            t.push_back(Call<bool>(original, 0x00587320, c, [&]() { return c->Start(); }));
            t.push_back(Call<bool>(original, 0x00587280, o, [&]() { return o->IsDone(); }));
            t.push_back(Call<bool>(original, 0x00587330, c, [&]() { return c->IsDone(); }));
            t.push_back(Call<int>(original, 0x005872A0, o, [&]() { return o->IsHeldAndComplete(); }));
            t.push_back(Call<int>(original, 0x00587350, c, [&]() { return c->IsHeldAndComplete(); }));
        }
        mark(6);
        // Play directly (both ways); the camera's member functions; Update with and without an asset.
        for (int backwards : {0, 1}) {
            Call<void>(original, 0x005873A0, o, [&](void* a, int l, bool b) { o->Play(a, l, b); }, Ptr<void>(kAsset), 2, backwards != 0);
            Call<void>(original, 0x00587540, c, [&](void* a, int l, bool b) { c->Play(a, l, b); }, Ptr<void>(kAsset), 3, backwards != 0);
        }
        mark(7);
        for (uint32_t name : {0xA8D5A188u, 0xD7241A6Cu, 0x12345678u})
            t.push_back(Call<bool>(original, 0x00587590, c,
                                   [&](uint32_t n, CStruct* q, Script::CScript* sc) { return c->CallMemberFunction(n, q, sc); }, name,
                                   p, Ptr<Script::CScript>(0x0680)));
        mark(8);
        for (int anim : {0, 1}) {
            c->m_anim = anim ? Ptr<void>(kAsset) : nullptr;
            Call<void>(original, 0x00588090, c, [&]() { c->Update(); });
        }
        mark(9);
        // The destructors (body; deleting without and with freeing).
        for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(Call<void*>(original, 0x00587370, o, [&](uint8_t f) -> void* { return o->Destroy(f); }, flags))));
            t.push_back(Rel(reinterpret_cast<uint32_t>(Call<void*>(original, 0x00587450, c, [&](uint8_t f) -> void* { return c->Destroy(f); }, flags))));
        }
        mark(10);
        // The trick object pieces: the manager released at counts 3, 1 (deleted), 0 with none; the cluster count
        // callback; a cluster's params set and emptied; the list's constructor.
        GameVariable<void**>(0x007CE500) = reinterpret_cast<void**>(At(0x0700));
        *Ptr<void**>(0x0700) = g_managerVtable;
        for (uint32_t count : {3u, 1u, 0u}) {
            GameVariable<uint32_t>(0x007CE504) = count;
            if (original)
                Original<void(__cdecl*)()>(0x005880E0)();
            else
                Obj::ReleaseTrickObjectManager();
            t.push_back(GameVariable<uint32_t>(0x007CE504));
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE500)));
        }
        Obj::ClusterCount counter = {5, 0x77};
        for (int i = 0; i < 4; ++i) {
            uint8_t* object = Ptr<uint8_t>(0x0800 + 0x50 * i);
            object[0x40] = (i & 1) ? 1 : 0;
            *reinterpret_cast<uint32_t*>(object + 0x3C) = (i & 2) ? 0x77 : 0x78;
            Obj::ClusterCount* r = original ? Original<Obj::ClusterCount*(__cdecl*)(uint8_t*, Obj::ClusterCount*)>(0x00588120)(object, &counter)
                                            : Obj::CountObjectInCluster(object, &counter);
            t.push_back(r == &counter);
            t.push_back(static_cast<uint32_t>(counter.count));
        }
        auto* cluster = Ptr<Obj::CTrickCluster>(0x0A00);
        cluster->m_params = Ptr<CStruct>(0x0A80);
        for (CStruct* from : {p, static_cast<CStruct*>(nullptr)}) {
            if (original)
                Original<void(__thiscall*)(void*, CStruct*)>(0x00588150)(cluster, from);
            else
                cluster->SetParams(from);
        }
        auto* list = Ptr<Obj::CTrickObjectList>(0x1000);
        list->m_count = 9;
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00588170)(list) : list->Construct())));
        t.push_back(list->m_count);
        list->m_count = 4;
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00588180)(list) : list->Clear());
        mark(11);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "anim movies");
}

} // namespace

SELFTEST(Early, TestAnimMovies, ViewerTestKit::Cleanup);
