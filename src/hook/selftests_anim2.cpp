// Self-tests for AN-2: the asset base class and the types' constructors (src/game/ass/asset.cpp) and the asset
// manager (src/game/ass/assman.cpp) - the original and ours on identical stand-in state, compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/ass/asset.h"
#include "game/ass/assman.h"
#include <algorithm>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/task.h"
#include "game/file/async.h"
#include "game/script/node_manager.h"
#include "game/script/struct.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Ass::CAsset;
using Ass::CAssMan;

struct AssetArena {
    alignas(16) uint8_t heap[0x8000];
    uint8_t assets[6][0x60];
    Mem::ListNode nodes[4];
    Tsk::Stack logicTasks; // the main loop's (Mlp::Manager +0x04)
    uint8_t mlp[0x10];
    uint8_t fakeManager[0x200];
    uint8_t items[64][12]; // the pool's hash items
};
AssetArena g_assets;
uint32_t g_assetHeapUsed;
int g_itemsUsed;
bool g_assetOriginal;

template <typename F>
F AssetFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_assetOriginal, address));
}

uint32_t Bump(uint32_t size)
{
    uint32_t at = reinterpret_cast<uint32_t>(g_assets.heap) + g_assetHeapUsed;
    g_assetHeapUsed += (size + 15) & ~15u;
    if (g_assetHeapUsed > sizeof(g_assets.heap))
        return 0;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}

// The memory manager, the pool, the async file calls and the script integer, stood in for (recorded).
uint32_t g_keepFailed;
CAsset* g_waitingAsset;
int g_waitsLeft;
void StandInCommon(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        if (r.args[0])
            std::memset(reinterpret_cast<void*>(r.args[0]), 0xDD, 4);
        return 0u;
    });
    s.At({0x005BC450, AddressOf(&Mem::PoolManager::CreateItem)}, 0, [](Regs& r) {
        Record({0x9001C000u, r.args[0]});
        return reinterpret_cast<uint32_t>(g_assets.items[g_itemsUsed++ % 64]);
    });
    s.At({0x005BC4A0, AddressOf(&Mem::PoolManager::FreeItem)}, 0, [](Regs& r) {
        Record({0x9001F000u, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x6E700000u, r.args[0]});
        return g_keepFailed;
    });
    s.At({0x005B6A50, AddressOf(static_cast<void (*)(bool)>(&File::AsyncWaitForIO))}, 0, [](Regs& r) {
        Record({0xA5A10000u, r.args[0] & 0xFF});
        if (g_waitingAsset && --g_waitsLeft <= 0)
            g_waitingAsset->m_state = Ass::kLoaded;
        return 0u;
    });
    s.At({0x005B63D0, AddressOf(&File::AsyncFileHandle::Cancel)}, 0, [](Regs& r) {
        Record({0xCA4C0000u, r.ecx});
        return 1u;
    });
    s.At({0x005B68A0, AddressOf(&File::AsyncClose)}, 0, [](Regs& r) {
        Record({0xC105E000u, r.args[0]});
        return 1u;
    });
}

void SetUpArena()
{
    std::memset(&g_assets, 0xEE, sizeof(g_assets));
    g_assetHeapUsed = 0;
    g_itemsUsed = 0;
    g_trace.clear();
}
std::vector<uint32_t> ArenaTrace()
{
    std::vector<uint32_t> trace = g_trace;
    const auto* words = reinterpret_cast<const uint32_t*>(&g_assets);
    uint32_t ourTask = reinterpret_cast<uint32_t>(&CAssMan::ProcessTask);
    for (size_t i = 0; i < sizeof(g_assets) / 4; ++i)
        trace.push_back(words[i] == ourTask ? 0x00407EB0u : words[i]); // (the task callback: ours is elsewhere)
    return trace;
}

// --- the base class ------------------------------------------------------------------------------------------

// The base methods called directly on assets of every type: construction, the state setters through the vtable
// (StartLoading, Unload, ProcessBuffer -> FinishLoading, Abort with and without a file), the setters and getters,
// the read callback (with and without its second value), dependents added and removed (first, middle, last, one
// that isn't there, an empty list), the destructors (base and the types', freeing or not) and the dependents
// list's node / head destructors.
void TestAnimAssetBase()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006F0D84, 4}});
    StandIns& s = *new StandIns();
    StandInCommon(s);
    s.At({0x0045D140, AddressOf(&SkateScript::CNodeManager::SetOwner)}, 4, [](Regs& r) {
        Record({0x5E70000u, r.ecx, r.args[0]});
        return 0u;
    });
    auto runOnce = [&](bool original) {
        g_assetOriginal = original;
        SetUpArena();
        GameVariable<void*>(0x007D1800) = g_assets.fakeManager;
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<void*>(0x006F0D84) = g_assets.mlp;
        if (!original)
            UseOurVtables();
        const uint32_t constructors[] = {0x00407910, 0x004071A0, 0x00409220, 0x004095F0, 0x00409970,
                                         0x00409C40, 0x0040A210, 0x0040A580, 0x00407A30, 0x00408BB0};
        auto at = [](int i) { return reinterpret_cast<CAsset*>(g_assets.assets[i]); };
        // Each constructor on dirty memory, its vtable's GetName / GetType / IsPermanent called.
        for (uint32_t constructor : constructors) {
            std::memset(g_assets.assets[0], 0xA5, sizeof(g_assets.assets[0]));
            CAsset* asset = at(0);
            Record({reinterpret_cast<uint32_t>(AssetFn<CAsset*(__thiscall*)(CAsset*)>(constructor)(asset))});
            const auto* words = reinterpret_cast<const uint32_t*>(asset);
            for (int k = 0; k < 0x60 / 4; ++k)
                Record({words[k]});
        }
        AssetFn<void(__thiscall*)(CAsset*)>(0x00408BF0)(at(0)); // (the last one is a node array)
        Record({g_assets.assets[0][0x60]});

        // The base on assets 1-4.
        for (int i = 1; i <= 4; ++i)
            AssetFn<CAsset*(__thiscall*)(CAsset*)>(0x00407910)(at(i));
        CAsset* a = at(1);
        auto setLoading = AssetFn<int(__thiscall*)(CAsset*)>(0x00407670);
        Record({static_cast<uint32_t>(setLoading(a)), static_cast<uint32_t>(a->m_state)});
        for (uint32_t setter : {0x00407680u, 0x00407690u, 0x004076A0u, 0x004076B0u, 0x004076C0u}) {
            bool result = AssetFn<bool(__thiscall*)(CAsset*)>(setter)(a);
            Record({result, static_cast<uint32_t>(a->m_state)});
            Record({AssetFn<bool(__thiscall*)(CAsset*)>(0x00407610)(a)});
        }
        Record({static_cast<uint32_t>(AssetFn<int(__thiscall*)(CAsset*, uint32_t, uint32_t, uint32_t, uint32_t,
                                                               uint32_t)>(0x004075F0)(a, 1, 2, 3, 4, 5)),
                static_cast<uint32_t>(a->m_state)});
        Record({static_cast<uint32_t>(AssetFn<int(__thiscall*)(CAsset*)>(0x00407600)(a)),
                static_cast<uint32_t>(a->m_state)});
        a->m_buffer = reinterpret_cast<void*>(0xB0FF0000);
        a->m_size = 0x1234;
        Record({AssetFn<bool(__thiscall*)(CAsset*)>(0x00407620)(a), static_cast<uint32_t>(a->m_state)});
        Record({AssetFn<bool(__thiscall*)(CAsset*, void*, uint32_t)>(0x00407630)(a, nullptr, 7),
                static_cast<uint32_t>(a->m_state)});
        Record({AssetFn<bool(__thiscall*)(CAsset*)>(0x00407640)(a), static_cast<uint32_t>(a->m_state)});
        a->m_file = reinterpret_cast<File::AsyncFileHandle*>(g_assets.mlp);
        Record({AssetFn<bool(__thiscall*)(CAsset*)>(0x00407640)(a), static_cast<uint32_t>(a->m_state),
                reinterpret_cast<uint32_t>(a->m_file)});
        Record({TestStandIns::Hash(AssetFn<const char*(__thiscall*)(CAsset*)>(0x004076D0)(a))});
        AssetFn<void(__thiscall*)(CAsset*, void*)>(0x004076E0)(a, reinterpret_cast<void*>(0xDA7A0001));
        AssetFn<void(__thiscall*)(CAsset*, bool)>(0x004072F0)(a, true);
        AssetFn<void(__thiscall*)(CAsset*, CAsset*)>(0x004076F0)(a, at(2));
        AssetFn<void(__thiscall*)(CAsset*, uint32_t)>(0x00407700)(a, 0xC4EC0001);
        AssetFn<void(__thiscall*)(CAsset*, Ass::LoadCallback, void*, void*)>(0x004075D0)(
            a, reinterpret_cast<Ass::LoadCallback>(0xCA11BAC0), reinterpret_cast<void*>(0xD0), reinterpret_cast<void*>(0xD1));
        // The read callback (cdecl).
        using ReadFn = void(__cdecl*)(void*, int, int, void*, void*);
        AssetFn<ReadFn>(0x00407710)(nullptr, 8, 0x777, at(3), nullptr);
        AssetFn<ReadFn>(0x00407710)(nullptr, 8, 0x888, at(4), reinterpret_cast<void*>(1)); // (ignored)

        // Dependents: three added, the middle, a missing one, the first and the last removed; an empty list.
        auto add = AssetFn<void(__thiscall*)(CAsset*, CAsset*)>(0x00407840);
        auto remove = AssetFn<void(__thiscall*)(CAsset*, CAsset*)>(0x00407890);
        CAsset* group = at(2);
        add(group, at(1));
        add(group, at(3));
        add(group, at(4));
        add(group, at(3)); // (twice: the first is removed)
        remove(group, at(3));
        remove(group, at(5));
        remove(group, at(1));
        remove(group, at(3));
        remove(group, at(4));
        remove(group, at(4)); // (an empty list)
        // The dependents nodes' destructors on loose nodes, and the destructors.
        Mem::ListNode* n = g_assets.nodes;
        for (int k = 0; k < 4; ++k)
            n[k] = {reinterpret_cast<void**>(0x77770000u + k), nullptr, k, &n[(k + 1) % 4], &n[(k + 3) % 4]};
        AssetFn<void(__thiscall*)(void*)>(0x004077B0)(&n[0]);
        Record({reinterpret_cast<uint32_t>(AssetFn<void*(__thiscall*)(void*, uint8_t)>(0x004077D0)(&n[1], 1))});
        Record({reinterpret_cast<uint32_t>(AssetFn<void*(__thiscall*)(void*, uint8_t)>(0x004078E0)(&n[2], 0))});
        add(at(3), at(4));
        Record({reinterpret_cast<uint32_t>(AssetFn<CAsset*(__thiscall*)(CAsset*, uint8_t)>(0x00407950)(at(3), 1))});
        AssetFn<CAsset*(__thiscall*)(CAsset*)>(0x00409970)(at(4));
        AssetFn<void(__thiscall*)(CAsset*)>(0x00407810)(at(4));
        Record({reinterpret_cast<uint32_t>(AssetFn<CAsset*(__thiscall*)(CAsset*, uint8_t)>(0x00407AF0)(at(1), 0))});
        Record({reinterpret_cast<uint32_t>(AssetFn<CAsset*(__thiscall*)(CAsset*, uint8_t)>(0x00407AF0)(at(2), 1))});
        EndRunPatches();
        return ArenaTrace();
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Ass::CAsset base methods, type constructors, dependents, destructors");
}
SELFTEST(Early, TestAnimAssetBase, TestStandIns::RemoveStandIns);

// --- the manager -----------------------------------------------------------------------------------------------

int g_loadResult;            // what the next Load / LoadFromBuffer returns (nonzero: failed)
int g_loadState;             // the state it leaves
uint32_t g_dataCounter;
bool g_finishResult;         // what FinishLoading returns
uint32_t g_reloadResult;

uint32_t TypeOf(uint32_t asset) { return *reinterpret_cast<uint32_t*>(asset); }

// The types' Load, LoadFromBuffer, Unload, Reload and FinishLoading stood in for (recorded; a load gives the
// asset its data and the scripted state).
void StandInAssetSlots(StandIns& s)
{
    std::vector<uint32_t> seen;
    auto each = [&](int slot, uint16_t pop, Handler handler) {
        for (uint32_t vtable : kAssetVtables) {
            uint32_t target = reinterpret_cast<uint32_t*>(vtable)[slot];
            if (std::find(seen.begin(), seen.end(), target) != seen.end())
                continue;
            seen.push_back(target);
            std::vector<uint32_t> at = {target};
            if (void* ours = OursAt(target))
                at.push_back(Resolve(ours));
            void* thunk = s.Thunk(pop, handler);
            for (uint32_t address : at)
                s.Bytes(address, Branch(0xE9, address, thunk));
        }
    };
    auto load = [](Regs& r) {
        Record({0x10AD0000u, TypeOf(r.ecx), r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3], r.args[4]});
        auto* asset = reinterpret_cast<CAsset*>(r.ecx);
        asset->m_state = g_loadState;
        asset->m_data = reinterpret_cast<void*>(0xDA7A0000u + ++g_dataCounter);
        return static_cast<uint32_t>(g_loadResult);
    };
    each(4, 0x14, load);
    each(5, 0x14, load);
    each(6, 0, [](Regs& r) {
        Record({0x0710AD00u, TypeOf(r.ecx), r.ecx});
        return 0u;
    });
    each(7, 4, [](Regs& r) {
        Record({0x4E10AD00u, TypeOf(r.ecx), r.args[0]});
        return g_reloadResult;
    });
    each(13, 8, [](Regs& r) {
        Record({0xF1A15000u, TypeOf(r.ecx), r.args[0], r.args[1]});
        if (g_finishResult)
            reinterpret_cast<CAsset*>(r.ecx)->m_state = Ass::kLoaded;
        return static_cast<uint32_t>(g_finishResult);
    });
}

// Names with known checksums (in different buckets of the 10-bit table, some sharing one).
const char* const kNames[] = {"skater.ska", "board.TEX", "level.qb", "park.scn", "geo.GEOM", "col.col",
                              "bones.ske", "model.mdl", "big.skin", "film.cut", "anim.FAM", "raw.bin"};

// The manager: made, started and stopped; assets loaded from files and memory (every type, by extension and by
// type, with a group, async, failing with and without the debug integer, the one-shot callback), found (waiting for
// unfinished ones), reloaded, LoadOrGet / LoadAnim (there already, new, by checksum base + offset), the pending
// list processed (read / processing / loaded / still loading / waiting on its group), unloaded one by one, by type
// and all (permanent ones kept or not), the extension lookup, and destroyed through the reference count.
void TestAnimAssetManager()
{    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006F0D84, 4}, {0x006918DC, 8}, {0x00696D68, 4}});
    StandIns& s = *new StandIns();
    StandInCommon(s);
    StandInAssetSlots(s);
    s.At({0x0045D140, AddressOf(&SkateScript::CNodeManager::SetOwner)}, 4, [](Regs& r) {
        Record({0x5E70000u, r.ecx, r.args[0]});
        return 0u;
    });
    // The cutscene asset's IsLoaded asks its cutscene data: the asset's state instead.
    s.At({0x00408AA0, AddressOf(&Ass::CCutsceneAsset::IsLoaded)}, 0, [](Regs& r) {
        Record({0xC0710000u});
        return static_cast<uint32_t>(reinterpret_cast<CAsset*>(r.ecx)->m_state == Ass::kLoaded);
    });
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]),
                                              reinterpret_cast<const char*>(r.args[1])));
    });
    void* callback = s.Thunk(0, [](Regs& r) {
        Record({0xCB000000u, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });

    auto runOnce = [&](bool original) {
        g_assetOriginal = original;
        SetUpArena();
        GameVariable<void*>(0x007D1800) = g_assets.fakeManager;
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<void*>(0x006F0D84) = g_assets.fakeManager + 0x100;
        GameVariable<void*>(0x006918DC) = nullptr;
        GameVariable<int>(0x006918E0) = 0;
        // The main loop manager: its logic tasks (+0x04) are the arena's stack.
        GameVariable<uint8_t*>(0x00696D68) = reinterpret_cast<uint8_t*>(&g_assets.logicTasks) - 4;
        g_keepFailed = 0;
        g_waitingAsset = nullptr;
        g_loadResult = 0;
        g_loadState = Ass::kLoaded;
        g_dataCounter = 0;
        g_finishResult = true;
        g_reloadResult = 1;
        if (!original)
            UseOurVtables();
        g_assets.logicTasks.Construct();

        auto create = AssetFn<CAssMan*(__cdecl*)(bool)>(0x004088E0);
        Record({reinterpret_cast<uint32_t>(create(false))}); // none yet, not made
        CAssMan* manager = create(true);
        Record({reinterpret_cast<uint32_t>(manager), reinterpret_cast<uint32_t>(create(true)),
                GameVariable<uint32_t>(0x006918E0)});
        AssetFn<void(__thiscall*)(CAssMan*)>(0x00407E90)(manager);
        AssetFn<void(__thiscall*)(CAssMan*, bool)>(0x00407A50)(manager, true);
        Record({AssetFn<bool(__thiscall*)(CAssMan*)>(0x00407A60)(manager)});
        AssetFn<void(__thiscall*)(CAssMan*, int)>(0x00407A70)(manager, 0x1818);

        auto load = AssetFn<void*(__thiscall*)(CAssMan*, const char*, int, uint32_t, uint32_t, bool, bool, bool, void*,
                                              int)>(0x00408440);
        auto stream = AssetFn<void*(__thiscall*)(CAssMan*, uint32_t, int, uint32_t, void*, uint32_t, bool, bool, void*,
                                                int)>(0x00408060);
        auto loadOrGet = AssetFn<void*(__thiscall*)(CAssMan*, const char*, int, uint32_t, uint32_t, bool, bool, bool,
                                                   void*, int)>(0x004087B0);
        auto loadAnim = AssetFn<bool(__thiscall*)(CAssMan*, const char*, uint32_t, uint32_t, bool, bool)>(0x00408870);
        auto get = AssetFn<CAsset*(__thiscall*)(CAssMan*, uint32_t, int)>(0x00407B90);
        auto getData = AssetFn<void*(__thiscall*)(CAssMan*, uint32_t, int)>(0x00407C00);
        auto getByName = AssetFn<void*(__thiscall*)(CAssMan*, const char*, int)>(0x00407C70);
        auto exists = AssetFn<bool(__thiscall*)(CAssMan*, uint32_t)>(0x00407CA0);
        auto reload = AssetFn<bool(__thiscall*)(CAssMan*, uint32_t, const char*, int)>(0x00407EC0);
        auto unload = AssetFn<void(__thiscall*)(CAssMan*, CAsset*)>(0x00407B20);
        auto typeOf = AssetFn<int(__thiscall*)(CAssMan*, const char*)>(0x004079C0);
        auto r = [](const void* p) { return reinterpret_cast<uint32_t>(p); };

        for (const char* name : {"x.ska", "X.Fam", "a.b.SCN", "noext", "xska", "Mqb", "dir.d/file", ".qb", "q.", "foo.zzz", "m.skin",
                                 "t.tex", "c.col", "k.ske", "g.geom", "z.cut", "b.bin", "m.mdl"})
            Record({static_cast<uint32_t>(typeOf(manager, name))});

        // From files: each name's type by its extension (the first with the one-shot callback).
        manager->m_callback = reinterpret_cast<Ass::LoadCallback>(callback);
        manager->m_callbackData0 = reinterpret_cast<void*>(0xD0);
        manager->m_callbackData1 = reinterpret_cast<void*>(0xD1);
        manager->m_callbackOnce = true;
        for (int i = 0; i < 10; ++i)
            Record({r(load(manager, kNames[i], 0, 0, 0, false, i & 1, i % 3 == 0, reinterpret_cast<void*>(0x9A0 + i),
                           i))});
        Record({r(load(manager, "owned.qb", Ass::kOwnedNodeArray, 0, 0, false, false, true, nullptr, 0))});
        // With a checksum of its own and a group (the textures).
        uint32_t textures = Crc::GenerateCRCFromString(kNames[1]);
        Record({r(load(manager, "withgroup.mdl", 0, 0x12345678, textures, false, false, false, nullptr, 0))});
        // The callback kept (not one-shot) for the stream loads.
        manager->m_callback = reinterpret_cast<Ass::LoadCallback>(callback);
        manager->m_callbackOnce = false;
        for (int type : {2, 3, 4, 5, 6, 7, 8, 10, 11})
            Record({r(stream(manager, 0x51000000u + type * 0x1001, type, type == 7 ? textures : 0,
                             reinterpret_cast<void*>(0xDA000000u + type), 0x100u * type, type & 1, type == 5,
                             reinterpret_cast<void*>(0x9B0 + type), -type))});
        // Bucket 0: 0x400 at its head, 0 behind it; the head flushed (key 0, no value): 0 still found behind it.
        Record({r(stream(manager, 0x400, 5, 0, nullptr, 4, false, false, nullptr, 0))});
        Record({r(stream(manager, 0, 6, 0, nullptr, 4, false, false, nullptr, 0))});
        unload(manager, get(manager, 0x400, 0));
        Record({exists(manager, 0), r(get(manager, 0, 0)), r(getData(manager, 0, 0))});
        // Failing: deleted, or kept with the debug integer set.
        g_loadResult = 1;
        Record({r(load(manager, "fail.ska", 0, 0, 0, false, false, false, nullptr, 0))});
        Record({r(stream(manager, 0x0FA11001, 5, 0, nullptr, 4, false, false, nullptr, 0))});
        g_keepFailed = 1;
        Record({r(load(manager, "kept.ska", 0, 0, 0, false, false, false, nullptr, 0))});
        g_keepFailed = 0;
        g_loadResult = 0;
        // Async: onto the pending list, still loading.
        g_loadState = Ass::kLoading;
        Record({r(load(manager, "async.ska", 0, 0, 0, true, false, false, nullptr, 0))});
        g_loadState = Ass::kLoaded;

        // Finding: loaded ones, the async one (waited for: three waits), missing ones.
        uint32_t async = Crc::GenerateCRCFromString("async.ska");
        CAsset* pending = Ass::AssetManager()->m_pending;
        g_waitingAsset = pending;
        g_waitsLeft = 3;
        Record({r(get(manager, async, 1)), static_cast<uint32_t>(pending->m_state)});
        pending->m_state = Ass::kLoading;
        g_waitsLeft = 2;
        Record({r(getData(manager, async, 0))});
        pending->m_state = Ass::kLoading;
        g_waitsLeft = 1;
        Record({r(getByName(manager, "async.ska", 0))});
        // A cutscene (its IsLoaded recorded): asked twice before the first wait.
        g_loadState = Ass::kLoading;
        load(manager, "wait.cut", 0, 0, 0, true, false, false, nullptr, 0);
        g_loadState = Ass::kLoaded;
        g_waitingAsset = Ass::AssetManager()->m_pending->m_prev;
        g_waitsLeft = 2;
        Record({r(get(manager, Crc::GenerateCRCFromString("wait.cut"), 0))});
        g_waitingAsset->m_state = Ass::kLoading; // (back to pending-like: unloaded through the pending list)
        g_waitingAsset = nullptr;
        Record({r(get(manager, 0xBADBAD, 0)), r(getData(manager, 0xBADBAD, 0)), r(getByName(manager, "nope", 0))});
        for (const char* name : kNames)
            Record({exists(manager, Crc::GenerateCRCFromString(name)), r(getByName(manager, name, 0))});
        Record({exists(manager, 0x12345678), exists(manager, 0)});
        // Reloading.
        Record({reload(manager, 0x12345678, "again.mdl", 0), reload(manager, 0xBADBAD, "x", 0)});
        g_reloadResult = 0;
        Record({reload(manager, textures, "again.tex", 1)});
        // LoadOrGet: there already (waiting), new; LoadAnim: there, new, by base + offset.
        pending->m_state = Ass::kRead;
        g_waitingAsset = pending;
        g_waitsLeft = 2;
        Record({r(loadOrGet(manager, "async.ska", 5, 0, 0, false, false, false, nullptr, 0))});
        g_waitingAsset = nullptr;
        Record({r(loadOrGet(manager, "fresh.ska", 0, 0, 0, false, true, true, nullptr, 3))});
        Record({loadAnim(manager, "skater.ska", 0, 0, false, false), loadAnim(manager, "new.ska", 0, 0, true, false),
                loadAnim(manager, "other.ska", 0x40000000, 0x15, false, true),
                loadAnim(manager, "other2.ska", 0x40000000, 0x15, false, true)});
        g_loadResult = 1;
        Record({loadAnim(manager, "bad.ska", 0, 0, false, false)});
        g_loadResult = 0;
        // (the async loads made loading-state assets on the pending list: finished now, as the slots leave them)
        // (the pending ones not loaded again: UnloadAsset looks for a loaded one in the loaded list - in the wrong
        // list the search wouldn't end)
        for (CAsset* a = manager->m_pending; a;) {
            a->m_state = Ass::kLoading;
            a = a->m_next == manager->m_pending ? nullptr : a->m_next;
        }

        // Unloading: one with dependents' group, a pending one, by type, all but the permanent ones.
        unload(manager, get(manager, 0x12345678, 0));
        Record({r(manager->m_loaded), r(manager->m_pending)});
        AssetFn<void(__thiscall*)(CAssMan*, bool, int)>(0x00407F50)(manager, false, Ass::kAnimation);
        AssetFn<void(__thiscall*)(CAssMan*, bool, int)>(0x00407F50)(manager, true, Ass::kTextureDictionary);
        AssetFn<void(__thiscall*)(CAssMan*, bool)>(0x00407F20)(manager, false);
        Record({r(manager->m_loaded), r(manager->m_pending)});

        // The pending list processed: a chain ending in null (see ProcessPendingAssets) of a loaded one, a read one,
        // one processed (that fails to finish), one still loading and one whose group isn't loaded.
        CAsset* chain[5];
        for (int i = 0; i < 5; ++i) {
            chain[i] = reinterpret_cast<CAsset*>(g_assets.assets[i]);
            AssetFn<CAsset*(__thiscall*)(CAsset*)>(0x00407A30)(chain[i]);
        }
        const int states[5] = {Ass::kLoaded, Ass::kRead, Ass::kProcessing, Ass::kLoading, Ass::kRead};
        for (int i = 0; i < 5; ++i) {
            chain[i]->m_state = states[i];
            chain[i]->m_next = i < 4 ? chain[i + 1] : nullptr;
            chain[i]->m_prev = i ? chain[i - 1] : nullptr;
            chain[i]->m_callback = reinterpret_cast<Ass::LoadCallback>(callback);
            chain[i]->m_size = 0x5000u + i;
        }
        chain[4]->m_group = chain[3];
        chain[1]->m_group = chain[0];
        manager->m_pending = chain[0];
        g_finishResult = true;
        AssetFn<void(__thiscall*)(CAssMan*)>(0x00407DD0)(manager);
        g_finishResult = false;
        chain[2]->m_state = Ass::kProcessing;
        manager->m_pending = chain[2];
        chain[2]->m_next = nullptr;
        AssetFn<void(__thiscall*)(CAssMan*)>(0x00407DD0)(manager);
        manager->m_pending = nullptr;
        AssetFn<void(__thiscall*)(CAssMan*)>(0x00407DD0)(manager); // (none)
        Tsk::BaseTask* task = manager->m_task;
        AssetFn<void(__cdecl*)(Tsk::BaseTask*)>(0x00407EB0)(task);
        manager->m_loaded = nullptr; // (the chain's assets aren't the manager's)

        // RemoveFromList on its own: the only one, the head, a middle one, one not in it.
        CAsset* head = nullptr;
        auto removeFrom = AssetFn<bool(__cdecl*)(CAsset**, CAsset*)>(0x00407A80);
        for (int i = 0; i < 3; ++i) {
            chain[i]->m_next = chain[(i + 1) % 3];
            chain[i]->m_prev = chain[(i + 2) % 3];
        }
        head = chain[0];
        chain[3]->m_next = chain[4];
        Record({removeFrom(&head, chain[1]), r(head), removeFrom(&head, chain[0]), r(head), removeFrom(&head, chain[2]),
                r(head)});
        chain[3]->m_next = chain[3];
        Record({removeFrom(&head, chain[3]), r(head)});

        // Assets left in both lists, then destroyed by the last reference.
        g_loadState = Ass::kLoading;
        load(manager, "left.ska", 0, 0, 0, true, false, false, nullptr, 0);
        g_loadState = Ass::kLoaded;
        load(manager, "left.tex", 0, 0, 0, false, false, true, nullptr, 0);
        AssetFn<void(__thiscall*)(CAssMan*)>(0x00407AE0)(manager);
        AssetFn<void(__cdecl*)()>(0x00407980)();
        Record({GameVariable<uint32_t>(0x006918DC), GameVariable<uint32_t>(0x006918E0)});
        AssetFn<void(__cdecl*)()>(0x00407980)();
        Record({GameVariable<uint32_t>(0x006918DC), GameVariable<uint32_t>(0x006918E0)});
        AssetFn<void(__cdecl*)()>(0x00407980)();
        Record({GameVariable<uint32_t>(0x006918DC), GameVariable<uint32_t>(0x006918E0)});

        // A manager constructed and destroyed in place on dirty memory.
        auto* lone = reinterpret_cast<CAssMan*>(g_assets.assets[5]);
        std::memset(lone, 0xC5, sizeof(CAssMan));
        Record({r(AssetFn<CAssMan*(__thiscall*)(CAssMan*)>(0x00407F80)(lone))});
        const auto* words = reinterpret_cast<const uint32_t*>(lone);
        for (int k = 0; k < 15; ++k)
            Record({words[k] == reinterpret_cast<uint32_t>(&CAssMan::ProcessTask) ? 0x00407EB0u : words[k]});
        Record({r(AssetFn<CAssMan*(__thiscall*)(CAssMan*, uint8_t)>(0x00408A10)(lone, 0))});
        EndRunPatches();
        return ArenaTrace();
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Ass::CAssMan (load / find / reload / process / unload / destroy)");
}
SELFTEST(Early, TestAnimAssetManager, TestStandIns::RemoveStandIns);

} // namespace
