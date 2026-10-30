// Self-test for the collision component (src/game/obj/collision_component.cpp, 0x0041C710-0x0041CA55): the original
// and ours, every call they make stood in for and recorded (the allocation, the base component, the script
// structure, the shape type, the sector, the asset manager, the movable collision made, the collision object's slots,
// the bone matrix and D3DX's multiply).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/buddy_status.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_world.h"
#include "game/obj/collision_component.h"
#include "game/obj/matrix_component.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

// The stand-in world: a component, its object, a model component, a model, a bone, asset data, a sector, collision
// objects (with recording vtables) and geometries.
struct World {
    alignas(16) uint8_t component[0x60];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t modelComponent[0x240];
    alignas(16) uint8_t model[0x60];
    alignas(16) uint8_t bone[0x60];
    alignas(16) uint8_t asset[0x90];
    alignas(16) uint8_t sector[0x20];
    alignas(16) uint8_t sectorCollision[0x20];
    alignas(16) uint8_t collision[0x20];
    alignas(16) uint8_t made[0x20];
    alignas(16) uint8_t geometries[0x100];
    void* componentVtable[16];
    void* collisionVtable[8];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// What the stand-ins answer this run.
struct Answers {
    uint32_t seed;
    bool callocFails, haveMode, haveClass, levelObject, sectorHasCollision, haveModel, haveBone, haveAsset;
    uint32_t shapeType;
};
Answers g_a;

void StandIn(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        std::memset(g_world.component, 0, sizeof(g_world.component));
        return reinterpret_cast<uint32_t>(g_world.component);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00417800, AddressOf(Nx::UnregisterMovable)}, 0, [](Regs& r) {
        Record({0x7800, Rel(r.args[0])});
        return 0u;
    });
    using GetChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&Script::CStruct::GetChecksum))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        // (what's in the slot before: Name's is uninitialised in the original)
        Record({0x6950, r.ecx, name, name == 0xA1DC81F9 ? 0u : *out, r.args[2]});
        bool found = name == 0x2D7E583B ? g_a.haveMode : name == 0x12B4E660 ? g_a.haveClass : true;
        if (!found)
            return 0u;
        *out = name == 0x12B4E660 ? (g_a.levelObject ? 0xB7B3BD86u : 0xEF59C100u) : name ^ g_a.seed;
        return 1u;
    });
    s.At({0x0040ADD0, AddressOf(Nx::ShapeTypeFromChecksum)}, 0, [](Regs& r) {
        Record({0xADD0, r.args[0]});
        return g_a.shapeType;
    });
    s.At({0x0048C510, AddressOf(&Nx::Engine::GetSector)}, 0, [](Regs& r) {
        Record({0xC510, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.sector);
    });
    s.At({0x005480F0, AddressOf(&GameNet::FoldedGetters::Word18)}, 0, [](Regs& r) {
        Record({0x80F0, Rel(r.ecx)});
        return g_a.sectorHasCollision ? reinterpret_cast<uint32_t>(g_world.sectorCollision) : 0u;
    });
    s.At({0x00414990, AddressOf(Nx::CreateMovableCollision)}, 0, [](Regs& r) {
        Record({0x4990, r.args[0], Rel(r.args[1]), r.args[2], Rel(r.args[3])});
        return g_a.seed % 4 == 3 ? 0u : reinterpret_cast<uint32_t>(g_world.made);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return g_a.haveModel ? reinterpret_cast<uint32_t>(g_world.modelComponent) : 0u;
    });
    s.At({0x00493040}, 0, [](Regs& r) {
        Record({0x3040, Rel(r.ecx)});
        return g_a.haveBone ? reinterpret_cast<uint32_t>(g_world.bone) : 0u;
    });
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.ecx, r.args[0], r.args[1]});
        return g_a.haveAsset ? reinterpret_cast<uint32_t>(g_world.asset) : 0u;
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Rel(r.args[1]), Rel(r.args[2]), HashBytes(reinterpret_cast<const uint8_t*>(a), 64),
                HashBytes(reinterpret_cast<const uint8_t*>(b), 64)});
        for (int i = 0; i < 16; ++i)
            out[i] = a[i] * 2.0f - b[15 - i];
        return r.args[0];
    });
    // The component's slot 13 (InitCollision, called virtually by InitFromStructure) and the collision objects' slots
    // 0 (the scalar deleting destructor), 5 (the position) and 6 (the orientation).
    g_world.componentVtable[13] = s.Thunk(8, [](Regs& r) {
        Record({0x13, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        return 0u;
    });
    g_world.collisionVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xC0, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_world.collisionVtable[5] = s.Thunk(4, [](Regs& r) {
        const auto* v = reinterpret_cast<const float*>(r.args[0]);
        Record({0xC5, Rel(r.ecx), Rel(r.args[0]), Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3])});
        return 0u;
    });
    g_world.collisionVtable[6] = s.Thunk(4, [](Regs& r) {
        Record({0xC6, Rel(r.ecx), Rel(r.args[0]), HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 64)});
        return 0u;
    });
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0xC011C000 + seed);
    auto fill = [&random](uint8_t* p, size_t size) {
        for (size_t i = 0; i < size; i += 4) {
            float f = static_cast<float>(static_cast<int>(random.Below(4001)) - 2000) / 16.0f;
            std::memcpy(p + i, &f, 4);
        }
    };
    fill(g_world.object, sizeof(g_world.object));
    fill(g_world.bone, sizeof(g_world.bone));
    std::memset(g_world.modelComponent, 0, sizeof(g_world.modelComponent));
    *reinterpret_cast<uint8_t**>(g_world.modelComponent + 0x208) = g_world.model;
    std::memset(g_world.model, 0, sizeof(g_world.model));
    *reinterpret_cast<uint32_t*>(g_world.model + 0x30) = 0x0DE10000 + seed;
    std::memset(g_world.asset, 0, sizeof(g_world.asset));
    *reinterpret_cast<uint32_t*>(g_world.asset + 0x88) = seed % 3 + 1;
    *reinterpret_cast<uint8_t**>(g_world.asset + 0x8C) = g_world.geometries;
    std::memset(g_world.sectorCollision, 0, sizeof(g_world.sectorCollision));
    *reinterpret_cast<uint8_t**>(g_world.sectorCollision + 0xC) = g_world.geometries + 0x40;
    for (uint8_t* c : {g_world.collision, g_world.made}) {
        std::memset(c, 0, 0x20);
        *reinterpret_cast<void***>(c) = g_world.collisionVtable;
    }
}

// Every case of the six functions, for seeds that vary the answers.
void TestCollisionComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006918DC, 4}});
    for (uint32_t seed = 1; seed <= 16; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            GameVariable<uint32_t>(0x006918DC) = 0xA55E0000;
            Originals guard(original, {0x0041C920, 0x0041C710, 0x0041C780, 0x0041C7B0, 0x0041C890, 0x0041C980});
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<Obj::CCollisionComponent*>(g_world.component);
            // Create (and out of memory).
            g_a.callocFails = seed == 5;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041C920))()));
            g_a.callocFails = false;
            for (int i = 0; i < 7; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
            // InitFromStructure: no mode / a mode that isn't a shape / a shape; a LevelObject (its sector with or
            // without collision) or another class or none.
            c->m_vtable = g_world.componentVtable;
            g_a.haveMode = seed % 5 != 1;
            g_a.shapeType = seed % 7 == 2 ? 0u : (seed & 1 ? 4u : 1u);
            g_a.haveClass = seed % 6 != 4;
            g_a.levelObject = (seed & 2) != 0;
            g_a.sectorHasCollision = (seed & 4) != 0;
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(Code(original, 0x0041C7B0))(c, 0x5C700000 + seed);
            // InitCollision: a geometry given / none (the model's asset data there or not).
            g_a.haveAsset = (seed & 8) != 0;
            g_a.haveModel = true; // (none: a crash in both)
            for (int k = 0; k < 2; ++k) {
                c->mp_collision = nullptr;
                auto* geometry = k ? nullptr : reinterpret_cast<Nx::CCollObjTriData*>(g_world.geometries + 0x80);
                reinterpret_cast<void(__thiscall*)(void*, int, void*)>(Code(original, 0x0041C890))(c, 1 + k * 3 + (seed & 1),
                                                                                                  geometry);
                trace.push_back(Rel(reinterpret_cast<uint32_t>(c->mp_collision)));
            }
            // Update: no collision / no model / no bone / a bone.
            for (int k = 0; k < 4; ++k) {
                c->mp_collision = k == 0 ? nullptr : reinterpret_cast<Nx::CCollObj*>(g_world.collision);
                g_a.haveModel = k >= 2 || (k == 1 && seed % 3 == 0);
                g_a.haveBone = k == 3;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C980))(c);
            }
            // DestroyBody / Destroy: with and without collision, freed or not.
            for (int k = 0; k < 4; ++k) {
                c->m_vtable = g_world.componentVtable;
                c->mp_collision = k & 1 ? reinterpret_cast<Nx::CCollObj*>(g_world.collision) : nullptr;
                if (k < 2) {
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041C710))(c);
                } else {
                    trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(
                        Code(original, 0x0041C780))(c, k == 3 ? 1u + (seed & 2) : (seed & 1) * 2))); // (0 / 2: kept)
                }
                trace.push_back(Rel(reinterpret_cast<uint32_t>(c->m_vtable)));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(g_world.object) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "collision component: made, initialised from a structure, its collision made, "
                                  "updated, destroyed");
    }
    delete &s;
}

// The matrix component (src/game/obj/matrix_component.cpp, 0x0041CA60-0x0041CB75): made (and out of memory),
// constructed, its matrix set and handed to the object, destroyed (freed or not).
void TestMatrixComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            Originals guard(original, {0x0041CB20, 0x0041CAD0, 0x0041CA60, 0x0041CA70, 0x0041CAA0, 0x0041CAC0});
            std::vector<uint32_t> trace;
            alignas(16) static uint8_t component[0x60];
            auto* c = reinterpret_cast<Obj::CMatrixComponent*>(g_world.component);
            g_a.callocFails = seed == 2;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041CB20))()));
            g_a.callocFails = false;
            std::memset(component, 0xCD, sizeof(component));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x0041CAD0))(component)) ==
                            reinterpret_cast<uint32_t>(component));
            for (size_t i = 0; i < sizeof(component) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(component)[i]));
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
            reinterpret_cast<void(__thiscall*)(void*, const void*)>(Code(original, 0x0041CAC0))(c, g_world.bone);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041CAA0))(c);
            c->m_vtable = g_world.componentVtable; // (put back by the destructor)
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041CA60))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t>(c->m_vtable)));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041CA70))(
                c, seed & 1 ? 1u : 2u)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < 0x58 / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            for (size_t i = 0; i < sizeof(g_world.object) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "matrix component: made, constructed, its matrix set and given to the object, destroyed");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestCollisionComponent, RemoveStandIns);
SELFTEST(Early, TestMatrixComponent, RemoveStandIns);
