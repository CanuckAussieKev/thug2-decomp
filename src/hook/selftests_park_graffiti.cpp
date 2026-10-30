// Self-tests for the trick object (graffiti) code at the start of the park editor & graffiti range
// (src/game/obj/trick_objects.cpp, 0x005889F0-0x00588BCF): the original and ours on the same stand-in manager,
// clusters, lists and nodes (viewer_testkit.h's arena), the goal manager and the clusters' destructors stood in for
// by recording code; the calls, results and whole arena compared.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/trick_objects.h"
#include "game/goals/goal_manager.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CTrickObject;
using Obj::CTrickObjectCluster;
using Obj::CTrickObjectList;
using Obj::CTrickObjectManager;

// The arena: the manager, its two tables' buckets (and chained items), three clusters (their vtable's slot 0 a
// recording destructor), the list, an output buffer and some list nodes.
constexpr uint32_t kVtable = 0x0080;
constexpr uint32_t kManager = 0x0100;
constexpr uint32_t kClusterBuckets = 0x0200;
constexpr uint32_t kObjectBuckets = 0x0300;
constexpr uint32_t kObjectChain = 0x0340;
constexpr uint32_t kClusters = 0x0400; // three, 0x60 apart
constexpr uint32_t kList = 0x1000;
constexpr uint32_t kOut = 0x2000;
constexpr uint32_t kNodes = 0x3000; // 0x20 apart
constexpr uint32_t kGoals = 0x7800;

using Item = Lst::HashTable::Item;

CTrickObjectManager* Manager() { return Ptr<CTrickObjectManager>(kManager); }
CTrickObjectCluster* Cluster(int i) { return Ptr<CTrickObjectCluster>(kClusters + 0x60 * i); }
CTrickObjectList* List() { return Ptr<CTrickObjectList>(kList); }
Mem::ListNode* Node(int i) { return Ptr<Mem::ListNode>(kNodes + 0x20 * i); }

void Table(Lst::HashTable& table, uint32_t buckets)
{
    table.m_bits = 2;
    table.m_buckets = Ptr<Item>(buckets);
}

// Clusters A (0xA0), B (0xB2) and C (no name). The object table: 0x11, 0x15 (chained) and 0xA0 -> A; 0x22, 0xB2
// (chained) -> B; 0x33 -> C.
void SetUpManager(bool chains)
{
    for (int i = 0; i < 3; ++i)
        *reinterpret_cast<uint32_t*>(Cluster(i)) = At(kVtable);
    Cluster(0)->m_name = 0xA0;
    Cluster(1)->m_name = 0xB2;
    Cluster(2)->m_name = 0;
    CTrickObjectManager* m = Manager();
    m->m_clusterCount = 3;
    Table(m->m_clusters, kClusterBuckets);
    Table(m->m_objectClusters, kObjectBuckets);
    Item* c = m->m_clusters.m_buckets;
    c[0] = {0xA0, Cluster(0), nullptr};
    c[2] = {0xB2, Cluster(1), nullptr};
    c[3] = {0x77, Cluster(2), nullptr};
    Item* o = m->m_objectClusters.m_buckets;
    Item* chain = Ptr<Item>(kObjectChain);
    o[0] = {0xA0, Cluster(0), nullptr};
    o[1] = {0x11, Cluster(0), chains ? &chain[0] : nullptr};
    o[2] = {0x22, Cluster(1), chains ? &chain[1] : nullptr};
    o[3] = {0x33, Cluster(2), nullptr};
    chain[0] = {0x15, Cluster(0), nullptr};
    chain[1] = {0xB2, Cluster(1), nullptr};
    GameVariable<CTrickObjectManager*>(0x007CE500) = m;
}

void* g_destructor;

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) {
        Rec(0x91B0, {});
        return At(kGoals);
    });
    s.At({0x00559150}, 4, [](Regs& r) {
        Rec(0x9150, {r.ecx, r.args[0]});
        return 0u;
    });
    g_destructor = s.Thunk(4, [](Regs& r) {
        Rec(0x0D00, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

// A head (data -1) and `count` nodes after it, in order.
void MakeList(int head, int count)
{
    Mem::ListNode* h = Node(head);
    h->data = reinterpret_cast<void*>(-1);
    h->next = h->prev = h;
    for (int i = 1; i <= count; ++i) {
        Mem::ListNode* n = Node(head + i);
        n->vtable = reinterpret_cast<void**>(0x0064F0D8);
        n->data = n;
        n->prev = h->prev;
        n->next = h;
        h->prev->next = n;
        h->prev = n;
    }
}

void TestParkGraffiti1()
{
    SaveGlobals({{0x007CE500, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        *Ptr<void*>(kVtable) = g_destructor;
        SetUpManager(true);
        CTrickObjectList* list = List();
        auto add = [&](uint32_t name) {
            bool r = original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x00588A20)(list, name) : list->Add(name);
            t.push_back(r);
            t.push_back(static_cast<uint32_t>(list->m_count));
        };
        auto copy = [&](uint32_t maxBytes) {
            std::memset(Ptr<void>(kOut), 0xEE, 0x40);
            uint32_t r = original ? Original<uint32_t(__thiscall*)(void*, uint32_t*, uint32_t)>(0x00588AA0)(
                                        list, Ptr<uint32_t>(kOut), maxBytes)
                                  : list->CopyClusterNames(Ptr<uint32_t>(kOut), maxBytes);
            t.push_back(r);
            for (int i = 0; i < 16; ++i)
                t.push_back(Ptr<uint32_t>(kOut)[i]);
        };

        // Adding: A, A again (through a chained name), B, C (no name), a name with no cluster.
        list->m_count = 0;
        for (uint32_t name : {0x11u, 0x15u, 0x22u, 0x33u, 0x44u, 0xB2u})
            add(name);
        // A listed just past the count: listed again; then a full ring (only the first 0x200 looked at) wrapping.
        list->m_count = 3;
        list->m_items[0] = list->m_items[1] = list->m_items[2] = 0x99;
        list->m_items[3] = 0xA0;
        add(0x11);
        for (uint32_t i = 0; i < CTrickObjectList::kCapacity; ++i)
            list->m_items[i] = 0x99;
        list->m_count = 0x205;
        *Ptr<uint32_t>(kList + 0x808) = 0xA0; // (past the ring: never looked at)
        add(0x11);
        add(0x11);
        add(0x22);

        // Copying: the existing clusters' names, stopping at the byte limit.
        list->m_count = 6;
        const uint32_t items[] = {0xA0, 0x99, 0x33, 0xB2, 0x11, 0x22};
        std::memcpy(list->m_items, items, sizeof(items));
        for (uint32_t maxBytes : {100u, 8u, 9u, 4u, 0u})
            copy(maxBytes);
        list->m_count = 0;
        copy(100);
        for (uint32_t i = 0; i < CTrickObjectList::kCapacity; ++i)
            list->m_items[i] = 0x99;
        list->m_items[0x1FF] = 0x11;
        list->m_count = 0x300;
        *Ptr<uint32_t>(kList + 0x808) = 0x22; // (past the ring: never looked at)
        copy(100);

        // Trick objects destroyed (body; deleting without and with freeing) out of a list of three.
        MakeList(0, 3);
        auto* a = reinterpret_cast<CTrickObject*>(Node(1));
        auto* b = reinterpret_cast<CTrickObject*>(Node(2));
        auto* c = reinterpret_cast<CTrickObject*>(Node(3));
        original ? Original<void(__thiscall*)(void*)>(0x00588BB0)(b) : b->DestroyBody();
        for (CTrickObject* o : {a, c}) {
            unsigned int flags = o == a ? 0 : 1;
            void* r = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x00588B40)(o, flags)
                               : o->Destroy(flags);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        }

        // Unlinking every node: three, one, none.
        for (int count : {3, 1, 0}) {
            MakeList(8, count);
            auto* head = static_cast<Obj::TrickObjectHead*>(Node(8));
            original ? Original<void(__thiscall*)(void*)>(0x00588B70)(head) : head->UnlinkAll();
            PushArena(t);
            std::memset(Ptr<void>(kNodes + 0x20 * 8), 0, 0x20 * 5);
        }

        // Every cluster removed (no chained items: those go back to a pool).
        SetUpManager(false);
        bool removed = original ? Original<bool(__thiscall*)(void*)>(0x005889F0)(Manager()) : Manager()->RemoveAllClusters();
        t.push_back(removed);

        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 1: trick object lists, destructors, clusters removed");
}

// Batch 2: a cluster made, its objects coloured (in a created park and not), destroyed (objects deleted through a
// recording destructor that takes them out of the list; one with no data is never deleted).
void* g_objectDestructor;

void TestParkGraffiti2()
{
    SaveGlobals({{0x007CE500, 4, {}}, {0x007CE478, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    s->At({0x00588200, AddressOf(&CTrickObject::SetGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x8200, {r.ecx, r.args[0]});
        return 1u;
    });
    s->At({0x004F4970}, 0, [](Regs&) {
        Rec(0x4970, {});
        return At(0x7900);
    });
    s->At({0x004F4950}, 8, [](Regs& r) {
        Rec(0x4950, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    g_objectDestructor = s->Thunk(4, [](Regs& r) {
        Rec(0x0E00, {r.ecx, r.args[0]});
        reinterpret_cast<Mem::ListNode*>(r.ecx)->UnlinkAs(reinterpret_cast<void**>(0x0064F0D0));
        return r.ecx;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        std::memset(Ptr<void>(0x4000), 0xCD, 0x400);
        auto* cluster = Ptr<CTrickObjectCluster>(0x4000);
        void* r = original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x00588BD0)(cluster, 0xA0) : cluster->Construct(0xA0);
        t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        PushArena(t);

        // Three objects (their vtable's slot 0 the recording destructor) and, second, one with no data.
        *Ptr<void*>(kVtable) = g_objectDestructor;
        auto addObjects = [&](int count, bool nullSecond) {
            for (int i = 0; i < count; ++i) {
                auto* n = Ptr<Mem::ListNode>(0x4100 + 0x20 * i);
                n->vtable = Ptr<void*>(kVtable);
                n->data = nullSecond && i == 1 ? nullptr : n;
                n->priority = 0;
                n->next = &cluster->m_objects;
                n->prev = cluster->m_objects.prev;
                cluster->m_objects.prev->next = n;
                cluster->m_objects.prev = n;
            }
        };
        addObjects(3, false);
        *Ptr<uint32_t>(0x7A00 + 0x378) = 0x12345678;
        GameVariable<uint32_t>(0x007CE478) = At(0x7A00);
        for (uint32_t level : {0x12345678u, 0xB664035Du}) {
            *Ptr<uint32_t>(0x7A00 + 0x378) = level;
            for (int index : {0, 3}) {
                bool ok = original ? Original<bool(__thiscall*)(void*, int)>(0x00588D20)(cluster, index)
                                   : cluster->SetGraffitiColor(index);
                t.push_back(ok);
            }
        }
        // Destroyed: three objects; then (made again) a first object with no data and two others; then none.
        for (int variant = 0; variant < 3; ++variant) {
            if (variant > 0) {
                cluster->Construct(0xB2);
                addObjects(variant == 1 ? 3 : 0, variant == 1);
                if (variant == 1) { // (the one with no data first)
                    auto* first = Ptr<Mem::ListNode>(0x4120);
                    first->prev->next = first->next;
                    first->next->prev = first->prev;
                    first->next = cluster->m_objects.next;
                    first->prev = &cluster->m_objects;
                    cluster->m_objects.next->prev = first;
                    cluster->m_objects.next = first;
                }
            }
            original ? Original<void(__thiscall*)(void*)>(0x00588C30)(cluster) : cluster->DestroyBody();
            PushArena(t);
        }
        // An empty cluster coloured: nothing but the created-park rail.
        cluster->Construct(0xC3);
        bool ok = original ? Original<bool(__thiscall*)(void*, int)>(0x00588D20)(cluster, 1) : cluster->SetGraffitiColor(1);
        t.push_back(ok);

        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 2: a trick object cluster made, coloured, destroyed");
}

// Batch 3: the manager made (both tables and its structure from the stand-in allocator, on the script heap) and
// destroyed (three clusters deleted, an empty item skipped, the buckets freed) - twice.
void TestParkGraffiti3()
{
    SaveGlobals({{0x007CE500, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        *Ptr<void*>(kVtable) = g_destructor;
        *Ptr<uint32_t>(0x7000 + 0x124) = At(0x7300); // the script heap
        for (int variant = 0; variant < 2; ++variant) {
            auto* manager = Ptr<CTrickObjectManager>(0x4000 + 0x100 * variant);
            std::memset(manager, 0xCD, 0x40);
            void* r = original ? Original<void*(__thiscall*)(void*)>(0x00588DC0)(manager) : manager->Construct();
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            for (int i = 0; i < 0x10; ++i)
                t.push_back(Rel(reinterpret_cast<uint32_t*>(manager)[i]));
            manager->m_vtable = nullptr; // (the destructor puts its own back)
            // Three clusters (in their buckets: chained items go back to a pool) deleted with it.
            for (int i = 0; i < 3; ++i) {
                *reinterpret_cast<uint32_t*>(Cluster(i)) = At(kVtable);
                manager->m_clusters.m_buckets[i * 5] = {0x100u + i, Cluster(i), nullptr};
                manager->m_objectClusters.m_buckets[i * 7] = {0x200u + i, Cluster(i), nullptr};
            }
            manager->m_clusterCount = 3;
            if (variant == 1)
                manager->m_objectClusters.m_buckets[9] = {0x209, nullptr, nullptr};
            original ? Original<void(__thiscall*)(void*)>(0x00588E90)(manager) : manager->DestroyBody();
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 3: the trick object manager made and destroyed");
}

// Batch 4: a cluster's colour cleared (an object failing part way; in a created park and not); the manager colouring an
// object's cluster by player, clearing every cluster (one failing), filing objects under clusters.
uint32_t g_failObject; // the object (or cluster) whose clearing fails
void TestParkGraffiti4()
{
    SaveGlobals({{0x007CE500, 4, {}}, {0x007CE478, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    s->At({0x00588270, AddressOf(&CTrickObject::ClearGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x8270, {r.ecx, r.args[0]});
        return r.ecx == g_failObject ? 0u : 1u;
    });
    s->At({0x004F4970}, 0, [](Regs&) {
        Rec(0x4970, {});
        return At(0x7900);
    });
    s->At({0x004F3D10}, 4, [](Regs& r) {
        Rec(0x3D10, {r.ecx, r.args[0]});
        return 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        auto* cluster = Ptr<CTrickObjectCluster>(0x4000);
        cluster->Construct(0xA0);
        for (int i = 0; i < 3; ++i) {
            auto* n = Ptr<Mem::ListNode>(0x4100 + 0x20 * i);
            n->data = n;
            n->next = &cluster->m_objects;
            n->prev = cluster->m_objects.prev;
            cluster->m_objects.prev->next = n;
            cluster->m_objects.prev = n;
        }
        GameVariable<uint32_t>(0x007CE478) = At(0x7A00);
        for (uint32_t level : {0x12345678u, 0xB664035Du}) {
            *Ptr<uint32_t>(0x7A00 + 0x378) = level;
            for (uint32_t fail : {0u, At(0x4120), At(0x4100)}) {
                g_failObject = fail;
                for (int index : {1, 4}) {
                    bool ok = original ? Original<bool(__thiscall*)(void*, int)>(0x00588F40)(cluster, index)
                                       : cluster->ClearGraffitiColor(index);
                    t.push_back(ok);
                }
            }
        }
        cluster->Construct(0xB1); // (none)
        t.push_back(original ? Original<bool(__thiscall*)(void*, int)>(0x00588F40)(cluster, 2) : cluster->ClearGraffitiColor(2));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 4: a cluster's graffiti colour cleared");
}

void TestParkGraffiti4b()
{
    SaveGlobals({{0x007CE500, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    s->At({0x00588D20, AddressOf(&CTrickObjectCluster::SetGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x8D20, {r.ecx, r.args[0]});
        return 1u;
    });
    s->At({0x00588F40, AddressOf(&CTrickObjectCluster::ClearGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x8F40, {r.ecx, r.args[0]});
        return r.ecx == g_failObject ? 0u : 1u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        SetUpManager(true);
        CTrickObjectManager* m = Manager();
        for (uint32_t name : {0x11u, 0x15u, 0x22u, 0x44u}) {
            for (int player : {-1, 0, 6}) {
                bool ok = original ? Original<bool(__thiscall*)(void*, uint32_t, int)>(0x00588FF0)(m, name, player)
                                   : m->SetObjectGraffitiColor(name, player);
                t.push_back(ok);
            }
        }
        for (uint32_t fail : {0u, At(kClusters + 0x60), At(kClusters)}) {
            g_failObject = fail;
            for (int index : {3, 7}) {
                bool ok = original ? Original<bool(__thiscall*)(void*, int)>(0x00589030)(m, index) : m->ClearGraffitiColor(index);
                t.push_back(ok);
                t.push_back(static_cast<uint32_t>(m->m_clusters.m_iterBucket));
            }
        }
        // A (chained) cluster item with no value ends the walk early: C, after it, isn't cleared.
        g_failObject = 0;
        *Ptr<Item>(0x0380) = {0x98, nullptr, nullptr};
        m->m_clusters.m_buckets[2].next = Ptr<Item>(0x0380);
        t.push_back(original ? Original<bool(__thiscall*)(void*, int)>(0x00589030)(m, 5) : m->ClearGraffitiColor(5));
        // Objects filed under clusters (empty buckets: an occupied one would take a pooled item).
        std::memset(m->m_objectClusters.m_buckets, 0, 4 * sizeof(Item));
        const uint32_t pairs[][2] = {{0x60, 0xA0}, {0x71, 0x99}, {0x82, 0x77}};
        for (const auto& p : pairs) {
            bool ok = original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x00589080)(m, p[0], p[1])
                               : m->AddObjectToCluster(p[0], p[1]);
            t.push_back(ok);
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 4b: the manager colouring, clearing, filing objects");
}

// Batch 5: the two cluster-walk callbacks; the players' graffiti applied from the parameters (Skater0 / 2 / 5 arrays);
// the manager acquired (there, not there, made, out of memory); both scalar deleting destructors; a player's clusters
// released (on the server with held clusters, with none, as a client).
std::map<uint32_t, uint32_t> g_arrays; // Skater<n> text hash -> array
uint32_t g_callocResult;
void TestParkGraffiti5()
{
    SaveGlobals({{0x007CE500, 4, {}}, {0x007CE504, 4, {}}, {0x007CCE14, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    s->At({0x006234FE}, 0, [](Regs& r) {
        int n = std::snprintf(reinterpret_cast<char*>(r.args[0]), 32, reinterpret_cast<const char*>(r.args[1]), r.args[2]);
        return static_cast<uint32_t>(n);
    });
    s->At({0x004768B0, AddressOf(static_cast<bool (CStruct::*)(const char*, Script::CArray**, int)>(&CStruct::GetArray))}, 12,
          [](Regs& r) {
              Rec(0x68B0, {r.ecx, Text(r.args[0]), r.args[2]});
              auto it = g_arrays.find(Text(r.args[0]));
              *reinterpret_cast<uint32_t*>(r.args[1]) = it == g_arrays.end() ? 0 : it->second;
              return it == g_arrays.end() ? 0u : 1u;
          });
    s->At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0]];
    });
    s->At({0x00588D20, AddressOf(&CTrickObjectCluster::SetGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x8D20, {r.ecx, r.args[0]});
        return 1u;
    });
    s->At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
        if (g_callocResult)
            std::memset(reinterpret_cast<void*>(g_callocResult), 0, r.args[0]);
        return g_callocResult;
    });
    s->At({0x00588C30, AddressOf(&CTrickObjectCluster::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x8C30, {r.ecx});
        return 0u;
    });
    s->At({0x00588E90, AddressOf(&CTrickObjectManager::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x8E90, {r.ecx});
        return 0u;
    });
    s->At({0x0056BB70}, 4, [](Regs& r) {
        Rec(0xBB70, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x00589030, AddressOf(&CTrickObjectManager::ClearGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x9030, {r.ecx, r.args[0]});
        return 1u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        SetUpManager(true);
        CTrickObjectManager* m = Manager();
        m->m_params = Ptr<CStruct>(0x0600);

        // The callbacks: A held by player 2, B by player 5, C by nobody (but with player 2's id).
        Cluster(0)->m_owned = true;
        Cluster(0)->m_owner = 2;
        Cluster(1)->m_owned = true;
        Cluster(1)->m_owner = 5;
        Cluster(2)->m_owner = 2;
        for (int i = 0; i < 3; ++i) {
            Cluster(i)->m_bestScore = 0x50 + i;
            for (int j = 0; j < 8; ++j)
                Cluster(i)->m_scores[j] = 0x10 * i + j + 1;
        }
        auto* release = Ptr<Obj::HeldClusterRelease>(0x5000);
        release->player = 2;
        release->count = 1;
        for (int i = 0; i < 3; ++i)
            original ? Original<void(__cdecl*)(void*, void*)>(0x00588300)(Cluster(i), release)
                     : Obj::ReleaseHeldCluster(Cluster(i), release);
        auto* list = Ptr<Obj::FreeClusterList>(0x5800);
        for (uint32_t count : {0u, 31u, 32u}) {
            list->count = count;
            for (int i = 0; i < 3; ++i)
                original ? Original<void(__cdecl*)(void*, void*)>(0x00588350)(Cluster(i), list)
                         : Obj::ListFreeCluster(Cluster(i), list);
            t.push_back(list->count);
        }

        // Graffiti from the parameters: Skater0 (A's and an unknown object), Skater2 (B's), Skater5 (empty).
        g_arrays.clear();
        const uint32_t arrays[][4] = {{2, 0x11, 0x44, 0}, {1, 0x22, 0, 0}, {0, 0, 0, 0}};
        const char* names[] = {"Skater0", "Skater2", "Skater5"};
        for (int i = 0; i < 3; ++i) {
            uint32_t at = At(0x0700 + 0x40 * i);
            *reinterpret_cast<uint32_t*>(at + 4) = arrays[i][0];
            for (int j = 0; j < 3; ++j)
                reinterpret_cast<uint32_t*>(at + 0x10)[j] = arrays[i][1 + j];
            g_arrays[Hash(names[i])] = at;
        }
        original ? Original<void(__thiscall*)(void*)>(0x005890B0)(m) : m->ApplyPlayerGraffiti();

        // Acquired: there (made or not); not there and not made; made; out of memory.
        auto acquire = [&](bool create) {
            void* r = original ? Original<void*(__cdecl*)(bool)>(0x00589180)(create) : Obj::AcquireTrickObjectManager(create);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE500)));
            t.push_back(GameVariable<uint32_t>(0x007CE504));
        };
        GameVariable<uint32_t>(0x007CE504) = 4;
        acquire(true);
        acquire(false);
        GameVariable<CTrickObjectManager*>(0x007CE500) = nullptr;
        acquire(false);
        g_callocResult = At(0x6000);
        acquire(true);
        GameVariable<CTrickObjectManager*>(0x007CE500) = nullptr;
        g_callocResult = 0;
        acquire(true);
        GameVariable<CTrickObjectManager*>(0x007CE500) = m;

        // The scalar deleting destructors.
        for (unsigned int flags : {0u, 1u}) {
            void* r = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005891D0)(Cluster(1), flags)
                               : Cluster(1)->Destroy(flags);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            r = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x00589200)(m, flags) : m->Destroy(flags);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        }

        // A player's clusters released: on the server holding A (2), holding nothing (6); as a client.
        GameVariable<uint32_t>(0x007CCE14) = At(0x7600);
        Cluster(0)->m_owned = true;
        Cluster(0)->m_owner = 2;
        for (uint8_t flags : {uint8_t(1), uint8_t(0)}) {
            *Ptr<uint8_t>(0x7600) = flags;
            for (int player : {2, 6}) {
                bool ok = original ? Original<bool(__thiscall*)(void*, int)>(0x00589230)(m, player)
                                   : m->ReleasePlayerClusters(player);
                t.push_back(ok);
                Cluster(0)->m_owned = true;
            }
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 5: callbacks, the players' graffiti, the manager acquired / destroyed / released");
}

// Batch 6: a trick scored on trick objects - not sent (only coloured); sent with nothing changed; sent with two clusters
// changed and two victims (one with no player), the scorer's connection local (done) and remote (coloured too). The
// network manager, its players, the server app's sends and the manager's log stood in for; the messages' set bytes
// recorded.
uint32_t g_logged;
uint8_t g_stolen[8];
int g_playerIndex;
constexpr uint32_t kNet = 0x7600, kApp = 0x7680, kPlayers = 0x6800, kSkater = 0x6E00, kConn = 0x6C00;
uint32_t PlayerAt(int i) { return At(kPlayers + 0x40 * i); }
void RecordMessage(uint32_t id, uint32_t size, uint32_t data)
{
    if (!data || !size)
        return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    const auto* words = reinterpret_cast<const uint32_t*>(data);
    switch (id) {
    case 0x4B: Record({bytes[0], words[1], words[2]}); break;
    case 0x3B:
        Record({bytes[0], bytes[0xA]});
        for (uint32_t i = 3; i < size / 4; ++i)
            Record({words[i]});
        break;
    case 0x5B:
        Record({bytes[0]});
        for (uint32_t i = 1; i < size / 4; ++i)
            Record({words[i]});
        break;
    default: Record({0xBAD, id});
    }
}
void TestParkGraffiti6()
{
    SaveGlobals({{0x007CE500, 4, {}}, {0x007CE478, 4, {}}, {0x007CCE14, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    s->At({0x005886C0, AddressOf(&CTrickObjectManager::LogTrickObjects)}, 20, [](Regs& r) {
        Rec(0x86C0, {r.ecx, r.args[0], r.args[1], r.args[3], r.args[4]});
        std::memcpy(reinterpret_cast<void*>(r.args[2]), g_stolen, 8);
        return g_logged;
    });
    s->At({0x00566600}, 4, [](Regs& r) {
        Rec(0x6600, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(At(kSkater) + 0x14) = r.args[0];
        return At(kSkater);
    });
    s->At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Rec(0xAEA0, {r.ecx, r.args[0] & 0xFFFF});
        uint32_t id = r.args[0] & 0xFFFF;
        return id == 0 || id == 1 || id == 5 ? PlayerAt(id == 5 ? 2 : static_cast<int>(id)) : 0u;
    });
    s->At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        Rec(0x34A0, {r.ecx});
        return *reinterpret_cast<uint32_t*>(r.ecx + 0x3C);
    });
    s->At({0x00543320, AddressOf(&GameNet::PlayerInfo::IsObserver)}, 0, [](Regs& r) {
        Rec(0x3320, {r.ecx});
        return *reinterpret_cast<uint32_t*>(r.ecx + 0x38);
    });
    s->At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1] & 0xFF});
        g_playerIndex = 0;
        return PlayerAt(0);
    });
    s->At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Rec(0x7010, {r.ecx, r.args[1] & 0xFF});
        return ++g_playerIndex < 3 ? PlayerAt(g_playerIndex) : 0u;
    });
    s->At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        const auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[1]);
        Rec(0x7EB0, {r.ecx, r.args[0], desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                     static_cast<uint32_t>(desc->m_priority)});
        RecordMessage(desc->m_id, desc->m_length, reinterpret_cast<uint32_t>(desc->m_data));
        return 0u;
    });
    s->At({0x00458AB0, AddressOf(&Net::App::StreamMessage)}, 0x20, [](Regs& r) {
        Rec(0x8AB0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF, Text(r.args[4]), r.args[5] & 0xFF,
                     r.args[6] & 0xFF, r.args[7] & 0xFF});
        RecordMessage(r.args[1] & 0xFF, r.args[2] & 0xFFFF, r.args[3]);
        return 0u;
    });
    s->At({0x00588D20, AddressOf(&CTrickObjectCluster::SetGraffitiColor)}, 4, [](Regs& r) {
        Rec(0x8D20, {r.ecx, r.args[0]});
        return 1u;
    });
    s->At({0x0056BB70}, 4, [](Regs& r) {
        Rec(0xBB70, {r.ecx, r.args[0]});
        return 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        SetUpManager(true);
        GameVariable<uint32_t>(0x007CCE14) = At(kNet);
        GameVariable<uint32_t>(0x007CE478) = At(0x7A00);
        *Ptr<uint32_t>(kNet + 4) = At(kApp);
        *Ptr<uint8_t>(kNet + 0x38) = 0x2C; // the game id
        for (int i = 0; i < 3; ++i) {
            *reinterpret_cast<uint32_t*>(PlayerAt(i) + 0x18) = At(kConn);
            *reinterpret_cast<uint32_t*>(PlayerAt(i) + 0x38) = i == 2; // observing
            *reinterpret_cast<uint32_t*>(PlayerAt(i) + 0x3C) = 0x40 + i; // its connection's handle
        }
        uint32_t* objects = Ptr<uint32_t>(0x5000);
        auto score = [&](int player, uint32_t points, uint32_t count, bool send) {
            const uint32_t names[] = {0x11, 0x44, 0x22, 0x15};
            std::memcpy(objects, names, sizeof(names));
            original ? Original<void(__cdecl*)(int, uint32_t, uint32_t, uint32_t*, bool)>(0x005892C0)(player, points, count,
                                                                                                    objects, send)
                     : Obj::ScoreTrickObjects(player, points, count, objects, send);
        };
        score(1, 500, 4, false);
        score(-1, 10, 3, false);
        g_logged = 0;
        std::memset(g_stolen, 0, 8);
        score(0, 700, 4, true);
        g_logged = 3;
        g_stolen[2] = g_stolen[5] = g_stolen[7] = 1;
        for (uint32_t flags : {1u, 0u}) {
            *Ptr<uint32_t>(kConn + 0x64) = flags;
            score(1, 1200, 4, true);
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 6: a trick scored on trick objects, sent to the players");
}

// Batch 7: free clusters given to a player (some free, none free); clusters added (existing, new, in a created park); objects added to clusters (added, refused, the cluster's own name). Tables of 8 empty buckets, so
// nothing is chained (chained items come from a pool).
bool g_objectAdded;
void TestParkGraffiti7()
{
    SaveGlobals({{0x007CE500, 4, {}}, {0x007CE478, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    s->At({0x005892C0, AddressOf(&Obj::ScoreTrickObjects)}, 0, [](Regs& r) {
        Rec(0x92C0, {r.args[0], r.args[1], r.args[2], r.args[4] & 0xFF});
        for (uint32_t i = 0; i < r.args[2]; ++i)
            Record({reinterpret_cast<const uint32_t*>(r.args[3])[i]});
        return 0u;
    });
    s->At({0x004F4970}, 0, [](Regs&) {
        Rec(0x4970, {});
        return At(0x7900);
    });
    s->At({0x004F3D10}, 4, [](Regs& r) {
        Rec(0x3D10, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x00588630, AddressOf(&CTrickObjectCluster::AddObject)}, 4, [](Regs& r) {
        Rec(0x8630, {r.ecx, r.args[0]});
        return g_objectAdded ? 1u : 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        SetUpManager(false);
        CTrickObjectManager* m = Manager();
        GameVariable<uint32_t>(0x007CE478) = At(0x7A00);
        *Ptr<uint32_t>(0x7A00 + 0x378) = 0x12345678;

        // Free clusters: all three, then (A and B held, C taken too) none.
        for (int held = 0; held < 2; ++held) {
            for (int i = 0; i < 3; ++i)
                Cluster(i)->m_owned = held != 0;
            original ? Original<void(__thiscall*)(void*, int)>(0x00589670)(m, 3) : m->GiveFreeClusters(3);
        }

        // New tables of 8 buckets; clusters added.
        m->m_clusters.m_bits = m->m_objectClusters.m_bits = 3;
        m->m_clusters.m_buckets = Ptr<Item>(0x5200);
        m->m_objectClusters.m_buckets = Ptr<Item>(0x5280);
        m->m_clusters.m_buckets[0] = {0xA0, Cluster(0), nullptr};
        auto add = [&](uint32_t name) {
            bool ok = original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x005896D0)(m, name) : m->AddCluster(name);
            t.push_back(ok);
            t.push_back(static_cast<uint32_t>(m->m_clusterCount));
        };
        add(0xA0);
        add(0x51);
        *Ptr<uint32_t>(0x7A00 + 0x378) = 0xB664035D;
        add(0x52);
        *Ptr<uint32_t>(0x7A00 + 0x378) = 0x12345678;

        // Objects added: to A (added, then refused), A's own name, to the new 0x51.
        auto addObject = [&](uint32_t object, uint32_t cluster, bool result) {
            g_objectAdded = result;
            bool ok = original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x00589760)(m, object, cluster)
                               : m->AddObject(object, cluster);
            t.push_back(ok);
        };
        addObject(0x63, 0xA0, true);
        addObject(0x64, 0xA0, false);
        addObject(0xA0, 0xA0, true);
        addObject(0x65, 0x51, true);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park graffiti 7: free clusters given, clusters and objects added");
}

} // namespace

SELFTEST(Early, TestParkGraffiti1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti2, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti3, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti4, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti4b, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti5, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti6, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti7, ViewerTestKit::Cleanup);
