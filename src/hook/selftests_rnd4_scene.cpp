// Self-tests for the engine's scene members of 0x0049D1A0-0x0049E2EF (src/game/gfx/engine_scene_sectors.cpp,
// RND4-5): the original and ours over a stand-in scene (a recording vtable; its sectors stand-in sectors with their
// own recording vtables, real hash tables and lists in the arena) - the collision grid, the scene file's collision
// reader, the rest of the scene, the files, script structures and memory contexts stood in - random operations,
// every result, call and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/file/pip.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/sector.h"
#include "game/gfx/texture_dict.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CScene;
using Nx::CSector;

void* g_sceneVtable[12];
void* g_sectorVtable[4];
void* g_collVtable[24];
void* g_geomVtable[40];
Lcg* g_random;
uint32_t g_calls;
int g_loadCount;          // what the collision reader gives: this many objects
std::vector<uint32_t> g_loadNames; // ...named these
bool g_pipAlways;         // files always load

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

Nx::CCollObj* MakeColl(uint32_t name)
{
    auto* coll = reinterpret_cast<Nx::CCollObj*>(Block(0x18));
    std::memset(coll, 0, 0x18);
    coll->m_vtable = g_collVtable;
    coll->m_checksum = name;
    coll->m_flags = static_cast<uint16_t>(g_random->Next());
    return coll;
}
void* MakeGeom()
{
    auto* geom = reinterpret_cast<void**>(Block(0x10));
    geom[0] = g_geomVtable;
    return geom;
}
CSector* MakeSector(uint32_t name, bool withGeom)
{
    auto* sector = reinterpret_cast<CSector*>(Block(0x34));
    std::memset(sector, 0, 0x34);
    sector->vtable = g_sectorVtable;
    sector->m_checksum = name;
    sector->m_flags = g_random->Next() & 0x7F;
    sector->mp_geom = withGeom ? MakeGeom() : nullptr;
    sector->mp_collObj = g_random->Below(4) ? MakeColl(name) : nullptr;
    return sector;
}
Mem::ListNode* MakeHead()
{
    auto* head = reinterpret_cast<Mem::ListNode*>(Block(sizeof(Mem::ListNode)));
    head->vtable = reinterpret_cast<void**>(0x006499DC);
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    return head;
}
void Append(Mem::ListNode* head, void* data)
{
    auto* node = reinterpret_cast<Mem::ListNode*>(Block(sizeof(Mem::ListNode)));
    node->vtable = reinterpret_cast<void**>(0x006499D8);
    node->data = data;
    node->priority = 0;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
void RecordList(Mem::ListNode* head)
{
    for (Mem::ListNode* node = head->next; node != head && node; node = node->next)
        Record({Relocate(reinterpret_cast<uint32_t>(node->data))});
    Record({0x11570000});
}

void StandInScene(StandIns& s)
{
    StandInMemory(s);
    s.At({0x006234FE}, 0, [](Regs& r) { return StandInSprintf(r); });
    s.At({0x005BC450, AddressOf(Mem::PoolManager::CreateItem)}, 0, [](Regs& r) {
        Record({0xC450, r.args[0]});
        return Bump(12);
    });
    s.At({0x005BC4A0, AddressOf(Mem::PoolManager::FreeItem)}, 0, [](Regs& r) {
        Record({0xC4A0, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    // the grid: made over a box, filled, brought up to date (each list's objects recorded)
    s.At({0x00418440, AddressOf(&Nx::CCollGrid::Init)}, 4, [](Regs& r) {
        Record({0x8440, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x00418BD0, AddressOf(&Nx::CCollGrid::Fill)}, 8, [](Regs& r) {
        Record({0x8BD0, Relocate(r.ecx), Relocate(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x00418F60, AddressOf(&Nx::CCollGrid::Update)}, 12, [](Regs& r) {
        Record({0x8F60, Relocate(r.ecx)});
        for (int i = 0; i < 3; ++i)
            RecordList(reinterpret_cast<Mem::ListNode*>(r.args[i]));
        return 0u;
    });
    // the rest of the scene: a sector by name (the table's), deleted, the collision read, let go of
    s.At({0x0049CB00, AddressOf(&CScene::GetSector)}, 4, [](Regs& r) {
        Record({0xCB00, r.args[0]});
        auto* scene = reinterpret_cast<CScene*>(r.ecx);
        return reinterpret_cast<uint32_t>(scene->mp_sectors->GetItem(r.args[0]));
    });
    s.At({0x0049CB40, AddressOf(&CScene::DeleteSector)}, 4, [](Regs& r) {
        Record({0xCB40, Relocate(r.ecx), Relocate(r.args[0])});
        return 0xFFFFFF01u;
    });
    s.At({0x0049CFB0, AddressOf(&CScene::UnloadCollision)}, 0, [](Regs& r) {
        Record({0xCFB0, Relocate(r.ecx)});
        return 1u;
    });
    s.At({0x0049CBB0, AddressOf(&CScene::LoadCollisionData)}, 24, [](Regs& r) {
        Record({0xCBB0, Relocate(r.args[0]), Relocate(r.args[1]), Relocate(r.args[2]), Relocate(r.args[3]),
                Relocate(r.args[4]), r.args[5]});
        // `count` objects in an array new (the count before them), named as asked
        auto* array = reinterpret_cast<uint32_t*>(Block(4 + 0x18 * (g_loadCount + 1)));
        array[0] = static_cast<uint32_t>(g_loadCount);
        for (int i = 0; i < g_loadCount; ++i) {
            auto* coll = reinterpret_cast<Nx::CCollObj*>(reinterpret_cast<uint8_t*>(array + 1) + 0x18 * i);
            std::memset(coll, 0, 0x18);
            coll->m_vtable = g_collVtable;
            coll->m_checksum = g_loadNames[static_cast<size_t>(i)];
        }
        *reinterpret_cast<int*>(r.args[1]) = g_loadCount;
        *reinterpret_cast<uint32_t*>(r.args[2]) = reinterpret_cast<uint32_t>(array + 1);
        *reinterpret_cast<uint32_t*>(r.args[3]) = reinterpret_cast<uint32_t>(Block(0x10));
        return 1u;
    });
    s.At({0x005B7C00, AddressOf(Pip::Load)}, 0, [](Regs& r) {
        Record({0x7C00, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2], r.args[3],
                r.args[4]});
        return g_random->Below(4) || g_pipAlways ? reinterpret_cast<uint32_t>(Block(0x20)) : 0u;
    });
    s.At({0x005B7270, AddressOf(static_cast<void (*)(const char*)>(&Pip::Unload))}, 0, [](Regs& r) {
        Record({0x7270, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x0049EFD0, AddressOf(Nx::UnloadTexDict)}, 0, [](Regs& r) {
        Record({0xEFD0, Relocate(r.args[0])});
        return 1u;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F61, r.args[0]});
        return g_random->Below(2);
    });
    s.At({0x004A1090}, 0, [](Regs& r) {
        Record({0x1090, r.ecx});
        return 0u;
    });
    using Script::CStruct;
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Record({0x79D0, Relocate(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Record({0x7B80, Relocate(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    // the geoms' counts
    s.At({0x0048DF70}, 0, [](Regs& r) { return (Relocate(r.ecx) & 0xFFF) + 3; });
    s.At({0x0049C6F0}, 0, [](Regs& r) { return (Relocate(r.ecx) & 0xFF) + 7; });
    for (int slot = 0; slot < 40; ++slot) {
        g_geomVtable[slot] = s.Thunk(0, [slot](Regs& r) { return (Relocate(r.ecx) & 0x7F) + 100 * slot; });
    }
    // the platform's: the scene's slots 5 / 6 (true or false; slot 5 takes the name), 7 (a new sector)
    for (int slot = 0; slot < 12; ++slot) {
        uint16_t pop = (slot == 0 || slot == 5) ? 4 : 0;
        g_sceneVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx)});
            if (pop)
                Record({slot == 5 ? Hash(reinterpret_cast<const char*>(r.args[0])) : r.args[0]});
            if (slot == 7)
                return reinterpret_cast<uint32_t>(MakeSector(0xBEEF, false));
            return g_random->Below(3) ? 0xFFFFFF01u : 0xFFFFFF00u;
        });
    }
    // the sectors' destroy and clone
    g_sectorVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xB000, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    for (int slot = 1; slot < 4; ++slot) {
        g_sectorVtable[slot] = s.Thunk(slot == 3 ? 8 : 4, [slot](Regs& r) {
            Record({0xB000u + slot, Relocate(r.ecx), r.args[0]});
            if (slot == 3 && g_random->Below(4))
                return reinterpret_cast<uint32_t>(MakeSector(0, false));
            return 0u;
        });
    }
    s.At({0x0048CA80}, 0, [](Regs&) { return 0x5EC70000u + ++g_calls; });
    // the geoms' clone, faces and active flag
    s.At({0x0048DD80}, 8, [](Regs& r) {
        Record({0xDD80, Relocate(r.ecx), r.args[0], Relocate(r.args[1])});
        return reinterpret_cast<uint32_t>(MakeGeom());
    });
    s.At({0x0048DF10}, 4, [](Regs& r) {
        Record({0xDF10, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x0048DE60}, 4, [](Regs& r) {
        Record({0xDE60, Relocate(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0040FEC0, AddressOf(&Nx::CCollObj::SetChecksum)}, 4, [](Regs& r) {
        Record({0xFEC0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    for (int slot = 0; slot < 24; ++slot) {
        uint16_t pop = (slot == 0 || slot == 12) ? 4 : 0;
        g_collVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xC000u + slot, Relocate(r.ecx)});
            if (pop)
                Record({r.args[0]});
            if (slot == 12)
                return reinterpret_cast<uint32_t>(MakeColl(0xC10E));
            return r.ecx;
        });
    }
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

const uint32_t kNames[] = {0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800, 0x900, 0xA00};

void TestEngineScene()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FA7C8, 4}});
    StandIns& s = *new StandIns();
    StandInScene(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("engine scene", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x5CE0 + round);
            Lcg standIns(0x5CE1 + 7 * round);
            g_random = &standIns;
            GameVariable<bool>(0x007CC979) = true;
            uint8_t* manager = Block(sizeof(Mem::Manager));
            *reinterpret_cast<uint32_t*>(manager + 0x110) = 0x70D0;
            GameVariable<uint8_t*>(0x007D1800) = manager;
            GameVariable<uint32_t>(0x006FA7C8) = 0x3EA7;
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            // a scene as constructed (allocations failing on request), then stood in
            auto* scene = reinterpret_cast<CScene*>(Block(0x284));
            for (int i = 0; i < 0x284; ++i)
                reinterpret_cast<uint8_t*>(scene)[i] = static_cast<uint8_t>(random.Next());
            if (round % 9 == 4)
                g_failAllocations = {random.Below(2) == 0, false, random.Below(2) == 0, random.Below(2) == 0};
            result(At<uint32_t(__thiscall*)(void*, int)>(original, 0x0049D390)(scene, 2 + (round & 1)));
            g_failAllocations.clear();
            for (int i = 0; i < 0x284; i += 4)
                Record({Relocate(*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(scene) + i))});
            if (!scene->mp_sectors) {
                std::vector<uint32_t> trace = g_trace;
                DumpArena(trace);
                return trace;
            }
            scene->m_vtable = g_sceneVtable;
            if (!scene->mp_hiddenSectors)
                scene->mp_hiddenSectors = MakeHead();
            if (!scene->mp_addSceneSectors)
                scene->mp_addSceneSectors = MakeHead();
            std::memcpy(scene->m_name, "test_scene", 11);
            scene->mp_texDict = reinterpret_cast<Nx::CTexDict*>(Block(0x14));
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(scene->mp_texDict) + 0x10) = 0x7E5;
            scene->m_hasCollision = random.Below(2);
            if (random.Below(2))
                scene->mp_collGrid = reinterpret_cast<Nx::CCollGrid*>(Block(0x40));
            // its sectors (some with geometry), some hidden by an added scene
            std::vector<CSector*> sectors;
            for (int i = 0; i < 6; ++i) {
                CSector* sector = MakeSector(kNames[random.Below(10)], random.Below(2));
                At<void(__thiscall*)(void*, void*)>(original, 0x0049D4E0)(scene, sector);
                sectors.push_back(sector);
                if (random.Below(3) == 0)
                    Append(scene->mp_hiddenSectors, sector);
            }
            // a scene added over it (new sectors and old), shown, hidden, shown again, let go of
            if (round % 3 == 1) {
                scene->mp_collStatics = reinterpret_cast<Nx::CCollStatic*>(Block(0x18));
                if (random.Below(2))
                    scene->mp_collGrid = reinterpret_cast<Nx::CCollGrid*>(Block(0x40));
                g_pipAlways = true;
                g_loadCount = 3;
                g_loadNames = {0x0F01, sectors[random.Below(6)]->m_checksum, 0x0F02};
                result(At<uint32_t(__thiscall*)(void*, const char*)>(original, 0x0049DFD0)(scene, "added") & 0xFF);
                g_pipAlways = false;
                for (int i = 0; i < 3; ++i) {
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049DDE0)(scene) & 0xFF);
                    for (CSector* each : sectors)
                        Record({each->m_flags, each->mp_collObj ? each->mp_collObj->m_flags : 0xFFFFu});
                }
                result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E200)(scene) & 0xFF);
            }
            for (int step = 0; step < 14; ++step) {
                CSector* sector = sectors[random.Below(static_cast<uint32_t>(sectors.size()))];
                uint32_t byteArg = random.Pick({0u, 1u, 0x100u, 0xFFFFFF01u});
                // the next collision read: objects named after existing sectors and new ones
                g_loadCount = static_cast<int>(random.Below(5));
                g_loadNames.clear();
                for (int i = 0; i < g_loadCount; ++i)
                    g_loadNames.push_back(random.Below(2) ? kNames[random.Below(10)] : 0x0F00 + random.Below(4));
                for (CSector* each : sectors) // (a sector's flags as the next update will find them)
                    if (random.Below(4) == 0)
                        each->m_flags = random.Next() & 0x7F;
                switch (random.Below(16)) {
                case 0:
                    At<void(__thiscall*)(void*, void*)>(original, 0x0049D1A0)(scene, Block(4));
                    break;
                case 1: {
                    CScene* other = random.Below(2) ? nullptr : scene;
                    uint32_t name = random.Below(2) ? 0 : 0xC1C1;
                    result(At<uint32_t(__thiscall*)(void*, void*, void*, int, uint32_t, uint32_t)>(original,
                                                                                                 0x0049D510)(
                        scene, sector, other, 1, byteArg, name));
                    break;
                }
                case 2:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, void*, int, uint32_t, uint32_t)>(original,
                                                                                                    0x0049DF30)(
                        scene, kNames[random.Below(10)], nullptr, 2, byteArg, 0xC2C2));
                    break;
                case 3:
                    result(At<uint32_t(__thiscall*)(void*, const void*, int)>(original, 0x0049D590)(scene, Block(4),
                                                                                                 5) &
                           0xFF);
                    break;
                case 4:
                    result(At<uint32_t(__thiscall*)(void*, const char*, int)>(original, 0x0049DF70)(scene, "lvl", 3) &
                           0xFF);
                    break;
                case 5:
                case 6: {
                    Mem::ListNode* added = nullptr;
                    if (random.Below(2)) {
                        added = MakeHead();
                        for (int i = 0; i < 3; ++i) {
                            CSector* listed = sectors[random.Below(static_cast<uint32_t>(sectors.size()))];
                            if (listed->mp_collObj)
                                Append(added, listed);
                        }
                    }
                    At<void(__thiscall*)(void*, void*)>(original, 0x0049D780)(scene, added);
                    break;
                }
                case 7:
                    scene->m_sectorKept = random.Below(2);
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x0049E1B0)(scene, nullptr) & 0xFF);
                    break;
                case 8:
                    if (random.Below(4) == 0)
                        scene->mp_collStatics = nullptr;
                    result(At<uint32_t(__thiscall*)(void*, const char*)>(original, 0x0049DFD0)(scene, "added") &
                           0xFF);
                    break;
                case 9:
                case 10:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049DDE0)(scene) & 0xFF);
                    break;
                case 11:
                    if (random.Below(2))
                        scene->m_addCollStaticCount = 0;
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E200)(scene) & 0xFF);
                    break;
                case 12: {
                    // a node and a head destroyed
                    Mem::ListNode* head = MakeHead();
                    Append(head, sector);
                    Append(head, sectors[0]);
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049D320)(head->next, byteArg));
                    At<void(__thiscall*)(void*)>(original, 0x0049D300)(head->next);
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049D360)(head, byteArg));
                    break;
                }
                case 13:
                    sectors.push_back(MakeSector(kNames[random.Below(10)], false));
                    At<void(__thiscall*)(void*, void*)>(original, 0x0049D4E0)(scene, sectors.back());
                    break;
                default:
                    scene->m_added = random.Below(2);
                    scene->m_addData = random.Below(2) ? reinterpret_cast<uint32_t>(Block(4)) : 0;
                    if (random.Below(2)) {
                        At<void(__thiscall*)(void*)>(original, 0x0049DD10)(scene);
                    } else {
                        result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049E1D0)(scene, byteArg));
                    }
                    result(reinterpret_cast<uint32_t>(scene->m_vtable));
                    step = 100; // (gone)
                    break;
                }
                Record({0xEF00, scene->m_sectorKept, scene->m_added});
                for (CSector* each : sectors)
                    Record({each->m_flags});
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine scene: construct, sectors added / cloned, collision, super sectors, the added scene, destroy");
}

} // namespace

SELFTEST(Early, TestEngineScene, RemoveStandIns);
