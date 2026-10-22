// Self-tests for the trick object (graffiti) code at the start of the park editor & graffiti range
// (src/game/obj/trick_objects.cpp, 0x005889F0-0x00588BCF): the original and ours on the same stand-in manager,
// clusters, lists and nodes (viewer_testkit.h's arena), the goal manager and the clusters' destructors stood in for
// by recording code; the calls, results and whole arena compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/obj/trick_objects.h"
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
    s.At({0x005591B0}, 0, [](Regs&) {
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

} // namespace

SELFTEST(Early, TestParkGraffiti1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGraffiti2, ViewerTestKit::Cleanup);
