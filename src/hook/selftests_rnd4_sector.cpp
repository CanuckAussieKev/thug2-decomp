// Self-tests for the engine's sector (src/game/gfx/sector.cpp, RND4-1): the original and ours over stand-in sectors
// - their geometry's calls (the engine's CGeom dispatchers) and collision objects (a recording vtable) stood in,
// each member called with random arguments in a random order, every result, call and the arena compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/sector.h"
#include "game/nx/coll_obj.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CSector;

void* g_sectorVtable[4];
void* g_geomVtable[1];
void* g_collVtable[32];
uint32_t g_calls;
bool g_cloneFails;     // the platform's clone gives null
bool g_collCloneFails; // the collision's clone gives null
uint32_t g_collResult; // what the collision's getters give back

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

// A collision object (0x10 bytes and a little more): random flags and geometry.
Nx::CCollObj* MakeColl(Lcg& random)
{
    auto* coll = reinterpret_cast<Nx::CCollObj*>(Block(0x20));
    coll->m_vtable = g_collVtable;
    coll->m_checksum = random.Next();
    coll->m_flags = static_cast<uint16_t>(random.Next());
    coll->m_visitStamp = 0;
    coll->pad0B = 0;
    coll->m_geometry = random.Below(4) ? reinterpret_cast<Nx::CCollObjTriData*>(Block(0x10)) : nullptr;
    return coll;
}
void* MakeGeom()
{
    auto* geom = reinterpret_cast<void**>(Block(0x10));
    geom[0] = g_geomVtable;
    return geom;
}
CSector* MakeSector(Lcg& random)
{
    auto* sector = reinterpret_cast<CSector*>(Block(sizeof(CSector)));
    sector->vtable = g_sectorVtable;
    sector->m_checksum = random.Next();
    sector->m_flags = random.Next() ^ (random.Next() << 12);
    sector->m_terrain = random.Next();
    sector->m_rotY = static_cast<int>(random.Below(9)) - 4;
    sector->mp_geom = random.Below(3) ? MakeGeom() : nullptr;
    sector->mp_collObj = random.Below(3) ? MakeColl(random) : nullptr;
    return sector;
}

void StandInSector(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, r.args[0]});
        return 0u;
    });
    // The platform's clone (slot 3): a new sector, filled as a constructor would leave it, or null.
    g_sectorVtable[3] = s.Thunk(8, [](Regs& r) {
        Record({0xA003, Relocate(r.ecx), Relocate(r.args[0]), Relocate(r.args[1])});
        if (g_cloneFails)
            return 0u;
        auto* clone = reinterpret_cast<CSector*>(Block(sizeof(CSector)));
        clone->vtable = g_sectorVtable;
        clone->m_flags = 0x5A5A5A5A;
        clone->m_terrain = 0x7E77A111;
        clone->m_rotY = 77;
        clone->mp_geom = (g_calls++ & 1) ? MakeGeom() : nullptr;
        clone->mp_collObj = nullptr;
        return reinterpret_cast<uint32_t>(clone);
    });
    for (int slot = 0; slot < 3; ++slot) {
        g_sectorVtable[slot] = s.Thunk(4, [slot](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx), r.args[0]});
            return 0u;
        });
    }
    g_geomVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xB000, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    // The collision's slots: [0] destroy, [5] / [6] position / orientation, [7] / [8] / [21] getters, [12] clone,
    // [22] / [23] turn / scale about a point.
    for (int slot = 0; slot < 32; ++slot) {
        uint16_t pop = (slot == 0 || slot == 5 || slot == 6 || slot == 12) ? 4 : (slot == 22 || slot == 23) ? 8 : 0;
        g_collVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xC000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            if (slot == 12) {
                if (g_collCloneFails)
                    return 0u;
                Lcg random(r.args[0] + g_calls++);
                return reinterpret_cast<uint32_t>(MakeColl(random));
            }
            return g_collResult + slot + ++g_calls;
        });
    }
    // The engine's geom dispatchers: tag, the geom, the arguments (bytes where the geom reads a byte).
    struct Call {
        uint32_t address;
        uint16_t pop;
        uint32_t byteArgs;
    };
    for (Call c : {Call{0x0048D060, 0, 0}, Call{0x0048DE10, 4, 0}, Call{0x0048DE40, 4, 0}, Call{0x0048DE50, 0, 0},
                   Call{0x0048DE60, 4, 1}, Call{0x0048DE70, 0, 0}, Call{0x0048DE80, 0, 0}, Call{0x0048DEC0, 4, 0},
                   Call{0x0048DED0, 0, 0}, Call{0x0048DEE0, 4, 0}, Call{0x0048DEF0, 4, 0}, Call{0x0048DF00, 4, 0},
                   Call{0x0048DF10, 4, 0}, Call{0x0048DFB0, 32, 0}, Call{0x0048DFC0, 4, 1}, Call{0x0048DFD0, 8, 0},
                   Call{0x0048E010, 0, 0}, Call{0x0048E020, 4, 1}, Call{0x0048E030, 0, 0}, Call{0x0049F420, 12, 0}}) {
        s.At({c.address, c.address == 0x0049F420 ? AddressOf(&Nx::VirtualSlot41Jump) : c.address}, c.pop, [c](Regs& r) {
            Record({c.address & 0xFFFFF, Relocate(r.ecx)});
            for (int i = 0; i < c.pop / 4; ++i)
                Record({(c.byteArgs >> i) & 1 ? r.args[i] & 0xFF : Relocate(r.args[i])});
            if (c.address == 0x0048DE10) { // the colour into *out
                auto* out = reinterpret_cast<uint8_t*>(r.args[0]);
                out[0] = 0x12;
                out[1] = static_cast<uint8_t>(g_calls);
                out[2] = 0x34;
                out[3] = 0x56;
            }
            return (Relocate(r.ecx) >> 4) + c.address + ++g_calls;
        });
    }
    // A clone of the geom (for the instance and scene given).
    s.At({0x0048DD80}, 8, [](Regs& r) {
        Record({0xDD80, Relocate(r.ecx), r.args[0], Relocate(r.args[1])});
        return reinterpret_cast<uint32_t>(MakeGeom());
    });
    s.At({0x0040AEB0}, 0, [](Regs& r) {
        Record({0xAEB0, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x00415C10}, 4, [](Regs& r) {
        Record({0x5C10, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x0040FEC0, AddressOf(&Nx::CCollObj::SetChecksum)}, 4, [](Regs& r) {
        Record({0xFEC0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x0048CA80}, 0, [](Regs&) {
        Record({0xCA80});
        return 0x5EC70000u + ++g_calls;
    });
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

void TestSectorMembers()
{
    GameMemory memory({{0x007CC979, 1}});
    StandIns& s = *new StandIns();
    StandInSector(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 50; ++round) {
        ok &= CompareTwoRuns("sector members", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            Lcg random(0x5EC7 + round);
            g_collResult = random.Next() & ~0xFFFu;
            std::vector<CSector*> sectors;
            for (int i = 0; i < 4; ++i)
                sectors.push_back(MakeSector(random));
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            for (int step = 0; step < 40; ++step) {
                CSector* sector = sectors[random.Below(static_cast<uint32_t>(sectors.size()))];
                uint32_t byteArg = random.Pick({0u, 1u, 2u, 0x100u, 0x101u, 0xFFFFFF00u});
                uint32_t word = random.Next() ^ (random.Next() << 13);
                float vector[4] = {1.5f, -2.0f, static_cast<float>(word & 0xFF), 1.0f};
                g_cloneFails = random.Below(4) == 0;
                g_collCloneFails = random.Below(3) == 0;
                switch (random.Below(38)) {
                case 0:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E900)(sector));
                    break;
                case 1:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049E930)(sector, byteArg));
                    sectors.push_back(MakeSector(random));
                    break;
                case 2:
                    At<void(__thiscall*)(void*)>(original, 0x0049E630)(sector);
                    break;
                case 3:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E8C0)(sector, byteArg);
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E8D0)(sector) & 0xFF);
                    result(At<uint32_t(__thiscall*)(void*, int, void*)>(original, 0x0049E8E0)(sector, 3, sector));
                    break;
                case 34:
                case 35:
                case 36:
                case 4: {
                    uint32_t clone = At<uint32_t(__thiscall*)(void*, int, uint32_t, void*)>(original, 0x0049EAD0)(
                        sector, static_cast<int>(random.Below(3)), byteArg, sectors[0]);
                    result(clone);
                    if (clone)
                        sectors.push_back(reinterpret_cast<CSector*>(clone));
                    break;
                }
                case 5:
                    At<void(__thiscall*)(void*, void*)>(original, 0x0049E660)(sector, random.Below(3) ? MakeGeom()
                                                                                                    : nullptr);
                    break;
                case 6:
                    if (random.Below(2))
                        sector->mp_collObj = nullptr;
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x0049EAA0)(sector, MakeColl(random)) &
                           0xFF);
                    break;
                case 7:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E810)(sector));
                    break;
                case 8:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E890)(sector, word);
                    break;
                case 9:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E8A0)(sector, word);
                    break;
                case 10:
                    At<void(__thiscall*)(void*)>(original, 0x0049E670)(sector);
                    break;
                case 11: {
                    uint8_t* out = Block(4);
                    std::memset(out, 0x99, 4);
                    result(At<uint32_t(__thiscall*)(void*, void*)>(original, 0x0049E980)(sector, out));
                    break;
                }
                case 12:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E680)(sector, word);
                    break;
                case 13:
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E690)(sector));
                    break;
                case 14:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E9B0)(sector, byteArg);
                    break;
                case 15:
                    if (sector->mp_geom || sector->mp_collObj)
                        result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E9E0)(sector) & 0xFF);
                    break;
                case 16:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049EA30)(sector, byteArg);
                    break;
                case 17:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049EA60)(sector, byteArg);
                    break;
                case 18:
                    if (sector->mp_geom || sector->mp_collObj)
                        result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E6A0)(sector));
                    if (sector->mp_collObj)
                        result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049E740)(sector));
                    break;
                case 19:
                    At<void(__thiscall*)(void*, const float*)>(original, 0x0049E6C0)(sector, vector);
                    break;
                case 20:
                    At<void(__thiscall*)(void*, const float*)>(original, 0x0049E700)(sector, vector);
                    break;
                case 21:
                    At<void(__thiscall*)(void*, int)>(original, 0x0049E760)(sector,
                                                                          static_cast<int>(random.Below(11)) - 5);
                    break;
                case 22:
                    At<void(__thiscall*)(void*, const float*)>(original, 0x0049E7C0)(sector, vector);
                    break;
                case 23:
                    At<void(__thiscall*)(void*, const float*)>(original, 0x0049EA00)(sector, vector);
                    break;
                case 24:
                    At<void(__thiscall*)(void*, int, int, int)>(original, 0x0049E820)(sector, 1, 2,
                                                                                     static_cast<int>(word));
                    break;
                case 25:
                    At<void(__thiscall*)(void*, float, float, float, float, float, float, float, float)>(
                        original, 0x0049E830)(sector, 1.0f, 2.0f, 3.0f, 4.0f, -1.0f, -2.0f, 0.5f,
                                              static_cast<float>(word & 0xFFF));
                    break;
                case 26:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E840)(sector, byteArg);
                    break;
                case 27:
                    At<void(__thiscall*)(void*, float, float)>(original, 0x0049E850)(sector, 0.25f,
                                                                                    static_cast<float>(word & 0xFF));
                    break;
                case 28:
                    At<void(__thiscall*)(void*)>(original, 0x0049E860)(sector);
                    break;
                case 29:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049E870)(sector, byteArg);
                    break;
                case 30:
                    At<void(__thiscall*)(void*)>(original, 0x0049E880)(sector);
                    break;
                case 31:
                    sector->m_flags = word;
                    break;
                case 32:
                    if (sector->mp_collObj)
                        sector->mp_collObj->m_flags = static_cast<uint16_t>(word);
                    break;
                default:
                    sector->mp_geom = random.Below(3) ? MakeGeom() : nullptr;
                    sector->mp_collObj = random.Below(3) ? MakeColl(random) : nullptr;
                    break;
                }
                Record({0xEF00, sector->m_flags, static_cast<uint32_t>(sector->m_rotY), Relocate(reinterpret_cast<uint32_t>(sector->vtable))});
                sector->vtable = g_sectorVtable; // (the platform's clone stood in again after a constructor / destructor)
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "sector: members (geometry and collision calls, flags, clones, turns, stubs)");
}

} // namespace

SELFTEST(Early, TestSectorMembers, RemoveStandIns);
