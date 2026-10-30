// Self-tests for the skeleton component (game/obj/skeleton_component.cpp): the originals against ours on stand-in
// objects, every call into other code recorded by stand-ins (hook/standins.h) - the renderer's skeleton, the texture
// splat, D3DX's multiply, rand(), the script structures - and the component compared afterwards.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/skeleton_component.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/skeleton.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CSkeletonComponent;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

struct World {
    alignas(16) uint8_t component[0x40];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t other[0x120];
    alignas(16) uint8_t otherSkeleton[0x20]; // the other object's skeleton component
    alignas(16) uint8_t skeleton[0x40];
    alignas(16) uint8_t data[0x40];          // the skeleton data: +4 the count, +8 the names
    alignas(16) uint8_t block[0x40];
    alignas(16) uint8_t params[0x10];
    alignas(16) uint8_t objectParams[0x10];
    alignas(16) uint8_t scriptParams[0x10];
    alignas(16) uint8_t script[0x40];
    alignas(16) uint8_t array[0x10];
    alignas(16) uint8_t manager[0x10];
    alignas(16) uint8_t texture[0x10];
    alignas(16) uint8_t asset[0x10];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= reinterpret_cast<uint32_t>(tib->StackLimit) && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x57AC0000; // (somewhere on the stack: the two differ)
    return word;
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

struct Answers {
    bool allocFails, bonePositionFound;
    uint32_t found;  // a bit for each name found (by its hash)
    uint32_t seed;
    int rand;
    float floats[4];
};
Answers g_a;

uint32_t NameBit(uint32_t name) { return 1u << ((name * 2654435761u) >> 27); }
bool Found(uint32_t name) { return (g_a.found & NameBit(name)) != 0; }
float FloatFor(uint32_t name) { return g_a.floats[(name * 7u) >> 30]; }

void StandIn(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.allocFails)
            return 0u;
        std::memset(g_world.block, 0, sizeof(g_world.block));
        return reinterpret_cast<uint32_t>(g_world.block);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        if (g_a.allocFails)
            return 0u;
        std::memset(g_world.block, 0xB1, sizeof(g_world.block));
        return reinterpret_cast<uint32_t>(g_world.block);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(r.ecx == reinterpret_cast<uint32_t>(g_world.other) ? g_world.otherSkeleton
                                                                                               : g_world.component);
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.other);
    });
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.ecx, r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>(g_world.asset);
    });
    // The renderer's skeleton.
    s.At({0x004A3870, AddressOf(&Gfx::CSkeleton::GetBonePosition)}, 8, [](Regs& r) {
        Record({0x3870, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        if (!g_a.bonePositionFound)
            return 0u;
        auto* out = reinterpret_cast<float*>(r.args[1]);
        for (int i = 0; i < 4; ++i)
            out[i] = static_cast<float>((r.args[0] >> (4 * i)) & 0xFF) * 0.75f - 40.0f;
        return 1u;
    });
    s.At({0x004A36E0, AddressOf(&Gfx::CSkeleton::GetBoneMatrix)}, 8, [](Regs& r) {
        Record({0x36E0, Rel(r.ecx), r.args[0]});
        auto* out = reinterpret_cast<float*>(r.args[1]);
        for (int i = 0; i < 16; ++i)
            out[i] = static_cast<float>(static_cast<int>((r.args[0] * (i + 3)) % 97) - 48) / 13.0f;
        return 1u;
    });
    s.At({0x004A2D80, AddressOf(&Gfx::CSkeleton::SetBoneScale)}, 12, [](Regs& r) {
        const uint32_t* v = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0x2D80, Rel(r.ecx), r.args[0], v[0], v[1], v[2], v[3], r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x004A2BB0, AddressOf(&Gfx::CSkeleton::SetBoneActive)}, 8, [](Regs& r) {
        Record({0x2BB0, Rel(r.ecx), r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x004A2C70, AddressOf(&Gfx::CSkeletonData::GetIndex)}, 4, [](Regs& r) {
        Record({0x2C70, Rel(r.ecx), r.args[0]});
        return r.args[0] % 6;
    });
    s.At({0x004A3100, AddressOf(&Gfx::CSkeletonData::IsDescendant)}, 8, [](Regs& r) {
        Record({0x3100, Rel(r.ecx), r.args[0], r.args[1]});
        return static_cast<uint32_t>((r.args[0] + r.args[1]) % 3 == 0);
    });
    s.At({0x004A4000, AddressOf(&Gfx::CSkeleton::CollapseZeroScaledBones)}, 0, [](Regs& r) {
        Record({0x4000, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x004A2CB0, AddressOf(&Gfx::CSkeleton::DestroyBody)}, 0, [](Regs& r) {
        Record({0x2CB0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x004A3D30, AddressOf(&Gfx::CSkeleton::Construct)}, 4, [](Regs& r) {
        Record({0x3D30, Rel(r.ecx), Rel(r.args[0])});
        return reinterpret_cast<uint32_t>(g_world.skeleton);
    });
    s.At({0x004A3E80, AddressOf(&Gfx::CSkeleton::ConstructCopy)}, 4, [](Regs& r) {
        Record({0x3E80, Rel(r.ecx), Rel(r.args[0])});
        return reinterpret_cast<uint32_t>(g_world.skeleton);
    });
    s.At({0x0049F560, AddressOf(&Nx::CTexDict::GetTextureByName)}, 4, [](Regs& r) {
        Record({0xF560, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return reinterpret_cast<uint32_t>(g_world.texture);
    });
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.texture) + 4;
    });
    s.At({0x00623BA3}, 0, [](Regs&) {
        g_a.rand = (g_a.rand * 7919 + 13) % 32768;
        Record({0xBA3, static_cast<uint32_t>(g_a.rand)});
        return static_cast<uint32_t>(g_a.rand);
    });
    s.At({0x004928A0, AddressOf(Nx::Engine::CreateTextureSplatAt)}, 0, [](Regs& r) {
        const uint32_t* p = reinterpret_cast<const uint32_t*>(r.args[0]);
        const uint32_t* e = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0x28A0, p[0], p[1], p[2], p[3], e[0], e[1], e[2], e[3]});
        for (int i = 2; i < 12; ++i)
            Record({Rel(r.args[i])});
        return 1u;
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Rel(r.args[0]) == Rel(r.args[1]), HashBytes(reinterpret_cast<const uint8_t*>(a), 64),
                HashBytes(reinterpret_cast<const uint8_t*>(b), 64)});
        float result[16];
        for (int i = 0; i < 16; ++i)
            result[i] = a[i] * 0.5f + b[15 - i];
        if (g_a.seed & 0x10) { // (half a turn about x: the quaternion's w is 0)
            static const float kHalfTurn[16] = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 5, 6, 7, 1};
            std::memcpy(result, kHalfTurn, 64);
        }
        std::memcpy(out, result, 64);
        return r.args[0];
    });
    s.At({0x0045F3A0, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObjectFromNode)}, 12, [](Regs& r) {
        Record({0xF3A0, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1]), r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The script structures: a name found or not by its hash; values from it.
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            // (what's in the slot before, unless the original leaves it uninitialised: the asserted ones, size=,
            // max_bone_skip_lod=)
            bool uninitialised = r.args[2] != 0 || r.args[0] == 0x083FDB95 || r.args[0] == 0xD3982061;
            Record({tag, Rel(r.ecx), r.args[0], uninitialised ? 0xDEADu : *reinterpret_cast<uint32_t*>(r.args[1]),
                    r.args[2]});
            if (!Found(r.args[0]))
                return 0u;
            uint32_t* out = reinterpret_cast<uint32_t*>(r.args[1]);
            switch (tag) {
            case 0x6950: // a checksum
            case 0x6A30:
                *out = r.args[0] == 0xCAB94088 ? 0x00B00000 + (g_a.seed & 0xFF) * 0x111 : r.args[0] ^ g_a.seed;
                break;
            case 0x6630: // an integer
                *out = g_a.seed & 3;
                break;
            case 0x66D0: { // a float
                float f = FloatFor(r.args[0]);
                std::memcpy(out, &f, 4);
                break;
            }
            case 0x6590: // a string
                *out = reinterpret_cast<uint32_t>(r.args[0] == 0xA1DC81F9 ? "splat_name" : "splat_texture");
                break;
            case 0x6FB0: { // a vector
                float* v = reinterpret_cast<float*>(out);
                for (int i = 0; i < 4; ++i)
                    v[i] = (g_a.seed & 8) ? 0.0f : FloatFor(r.args[0] + i) * (i == 3 ? 0.0f : 1.0f);
                break;
            }
            case 0x6860: // an array
                *out = reinterpret_cast<uint32_t>(g_world.array);
                break;
            case 0x67C0: // a structure
                *out = reinterpret_cast<uint32_t>(g_world.objectParams);
                break;
            }
            return 1u;
        };
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         getter(0x6950));
    s.At({0x00476A30, AddressOf(&CStruct::GetChecksumOrStringChecksum)}, 12, getter(0x6A30));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
    s.At({0x00476590, 0x00476C60,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, getter(0x6590));
    s.At({0x00476FB0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, getter(0x6FB0));
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, getter(0x6860));
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         getter(0x67C0));
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        return static_cast<uint32_t>(Found(r.args[0] ^ 0x5A5A5A5A));
    });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477F70, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::VectorValue)>(&CStruct::AddVector))},
         0x14, [](Regs& r) {
             Record({0x7F70, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
             return 0u;
         });
}

void Fill(Random& r, uint8_t* p, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        // (values with every bit of their mantissa used: roundings show)
        float f = static_cast<float>(static_cast<int>(r.Below(4001)) - 2000) / 3.0f + static_cast<float>(r.Below(1000)) / 7.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void Build(Random& r, uint32_t n)
{
    Fill(r, g_world.object, sizeof(g_world.object));
    Fill(r, g_world.skeleton, sizeof(g_world.skeleton));
    std::memset(g_world.component, 0, sizeof(g_world.component));
    std::memset(g_world.block, 0xB2, sizeof(g_world.block));
    std::memset(g_world.otherSkeleton, 0, sizeof(g_world.otherSkeleton));
    At<uint8_t*>(g_world.otherSkeleton, 0x18) = g_world.skeleton + 0x20;
    auto* c = reinterpret_cast<CSkeletonComponent*>(g_world.component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    c->mp_skeleton = r.OneIn(6) ? nullptr : g_world.skeleton;
    At<uint32_t>(g_world.skeleton, 0) = r.Next();
    At<uint8_t*>(g_world.skeleton, 8) = g_world.data;
    std::memset(g_world.data, 0, sizeof(g_world.data));
    At<uint32_t>(g_world.data, 4) = r.Below(9);
    for (int i = 0; i < 8; ++i)
        At<uint32_t>(g_world.data, 8 + 4 * i) = 0xB0000000 + n * 16 + i;
    std::memset(g_world.script, 0, sizeof(g_world.script));
    At<uint8_t*>(g_world.script, 0x14) = g_world.scriptParams;
    g_a = {};
    g_a.allocFails = r.OneIn(6);
    g_a.bonePositionFound = !r.OneIn(4);
    g_a.found = r.Next() ^ (r.Next() << 12);
    g_a.seed = r.Next();
    g_a.rand = static_cast<int>(r.Below(32768));
    for (float& f : g_a.floats)
        f = r.OneIn(5) ? 0.0f : static_cast<float>(static_cast<int>(r.Below(2001)) - 500) / 8.0f;
    // (some objects' at straight along z, some with x negative or zero)
    if (r.OneIn(4))
        At<float>(g_world.object, 0x8C) = r.Bit() ? 0.0f : -0.0f;
    float atZ = static_cast<float>(static_cast<int>(r.Below(201)) - 100) / 100.0f;
    At<float>(g_world.object, 0x94) = atZ;
}

void Trace(std::vector<uint32_t>& trace)
{
    for (const uint8_t* area : {g_world.component, g_world.block, g_world.skeleton})
        for (size_t i = 0; i < 0x20; i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(area + i)));
}

void TestSkeletonComponent()
{
    std::vector<Saved> saved = {{0x007CC979, 1, {}}, {0x007D1800, 4, {}}, {0x006918DC, 4, {}}, {0x006F0FB8, 4, {}},
                                {0x00700F20, 4, {}}};
    SaveAll(saved);
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<void*>(0x007D1800) = g_world.manager;
    GameVariable<uint32_t>(0x006918DC) = 0xA55E0000;
    GameVariable<uint32_t>(0x006F0FB8) = 0xC0B70000;
    GameVariable<uint32_t>(0x00700F20) = 0x7E770000;
    auto* s = new StandIns;
    StandIn(*s);
    std::vector<uint32_t> traces[2];
    int splats = 0, spawned = 0;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x5E1E};
        for (uint32_t n = 0; n < 4000; ++n) {
            Build(r, n);
            auto* c = reinterpret_cast<CSkeletonComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(g_world.params);
            auto* script = reinterpret_cast<Script::CScript*>(g_world.script);
            uint32_t which = r.Below(12);
            Record({0xCA5E, which});
            size_t before = g_trace.size();
            using Member = bool(__thiscall*)(void*, void*, void*);
            switch (which) {
            case 0: { // Create, RegisterMemberFunctions, Destroy
                void* made = original ? Original<void*(__cdecl*)()>(0x00431F60)() : CSkeletonComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                if (original)
                    Original<void(__cdecl*)()>(0x00431BE0)();
                else
                    CSkeletonComponent::RegisterMemberFunctions();
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00432790)(c, flags) : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                break;
            }
            case 1: // InitFromStructure (InitSkeleton)
                if (c->mp_skeleton == nullptr)
                    c->mp_skeleton = g_world.skeleton; // (the copy reads the other's)
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00431CF0)(c, params);
                else
                    c->InitFromStructure(params);
                break;
            case 2: { // the bone positions
                Mth::Vector out = {1.0f, 2.0f, 3.0f, 4.0f};
                uint32_t bone = 0x00B00000 + r.Below(0x1000);
                bool local = original ? Original<bool(__thiscall*)(void*, uint32_t, void*)>(0x00431C60)(c, bone, &out)
                                      : c->GetLocalBonePosition(bone, &out);
                bool world = original ? Original<bool(__thiscall*)(void*, uint32_t, void*)>(0x00432050)(c, bone, &out)
                                      : c->GetBonePosition(bone, &out);
                Record({local, world, Bits(out.x), Bits(out.y), Bits(out.z), Bits(out.w)});
                break;
            }
            case 3: // Obj_SetBoneActive
            case 4: { // Obj_SetBoneScale
                if (!c->mp_skeleton)
                    c->mp_skeleton = g_world.skeleton;
                if (which == 4)
                    g_a.found |= NameBit(0xCAB94088) | NameBit(0x13B9DA7B); // (asserted: garbage otherwise)
                bool result = original ? Original<Member>(which == 3 ? 0x00431C00 : 0x00431E30)(c, params, script)
                              : which == 3 ? c->Obj_SetBoneActive(params, script)
                                           : c->Obj_SetBoneScale(params, script);
                Record({result});
                break;
            }
            case 5: { // obj_GetBonePosition
                bool result = original ? Original<Member>(0x004327C0)(c, params, script) : c->Obj_GetBonePosition(params, script);
                Record({result});
                break;
            }
            case 6:
            case 7: { // TextureSplat (always a size, and a bone or forward=: the original's start is garbage otherwise)
                g_a.found |= NameBit(0x083FDB95);
                if (!Found(0xCAB94088))
                    g_a.found |= NameBit(0x186ED079);
                bool result = original ? Original<Member>(0x00432860)(c, params, script) : c->TextureSplat(params, script);
                Record({result});
                break;
            }
            case 8:
            case 9: { // SpawnTextureSplat directly: every flag
                Mth::Vector start = {static_cast<float>(r.Below(100)), -3.5f, 7.25f, 1.0f};
                Mth::Vector end = {0.0f, 0.0f, 0.0f, 0.0f};
                if (r.Bit())
                    end = {static_cast<float>(r.Below(9)) - 4.0f, 1.5f, -2.0f, 1.0f};
                uint32_t bone = r.Bit() ? 0u : 0x00B00123u;
                float spread = r.OneIn(3) ? 0.0f : static_cast<float>(r.Below(50)) / 3.0f;
                bool follow = r.Bit(), dropdown = r.Bit();
                float length = static_cast<float>(r.Below(40)) / 7.0f;
                if (original)
                    Original<void(__thiscall*)(void*, const void*, const void*, uint32_t, float, float, float, float, float,
                                               float, float, void*, uint32_t, bool, bool)>(0x004320F0)(
                        c, &start, &end, bone, 1.5f, 2.5f, 0.3f, 0.7f, spread, 4000.0f, length, g_world.texture, 0x54628ED7,
                        follow, dropdown);
                else
                    c->SpawnTextureSplat(&start, &end, bone, 1.5f, 2.5f, 0.3f, 0.7f, spread, 4000.0f, length,
                                         g_world.texture, 0x54628ED7, follow, dropdown);
                break;
            }
            case 10: { // SpawnCompositeObjectAtBone directly: the offset's w cleared
                Mth::Vector vel = {1.0f, 2.0f, 3.0f, 0.0f}, rotvel = {0.5f, 0.25f, 0.125f, 0.0f};
                Mth::Vector offset = {3.0f, -2.0f, 1.0f, 9.0f};
                if (original)
                    Original<void(__thiscall*)(void*, uint32_t, void*, void*, float, const void*, const void*, void*)>(
                        0x00432490)(c, 0x00B00042, g_world.array, g_world.objectParams, 0.75f, &vel, &rotvel, &offset);
                else
                    c->SpawnCompositeObjectAtBone(0x00B00042, reinterpret_cast<Script::CArray*>(g_world.array),
                                                  reinterpret_cast<Script::CStruct*>(g_world.objectParams), 0.75f, &vel,
                                                  &rotvel, &offset);
                Record({Bits(offset.x), Bits(offset.w)});
                break;
            }
            default: { // Skeleton_SpawnCompositeObject
                g_a.found |= NameBit(0xCAB94088) | NameBit(0x11B70A02) | NameBit(0x7031F10C) | NameBit(0x0C4C809E) |
                             NameBit(0xFB1A83B2); // (asserted: the original's are garbage otherwise)
                bool result = original ? Original<Member>(0x00432B90)(c, params, script)
                                       : c->Skeleton_SpawnCompositeObject(params, script);
                Record({result});
                break;
            }
            }
            if (original)
                for (size_t k = before; k < g_trace.size(); ++k) {
                    splats += g_trace[k] == 0x28A0;
                    spawned += g_trace[k] == 0xF3A0;
                }
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    RestoreAll(saved);
    Log("  skeleton component: %d texture splats, %d objects spawned in the original's run", splats, spawned);
    Check(splats > 800 && spawned > 200, "skeleton component: the cases reach the splats and the spawns");
    for (size_t i = 0; i < traces[0].size() && i < traces[1].size(); ++i) {
        if (traces[0][i] != traces[1][i]) {
            size_t k = i;
            while (k > 0 && traces[0][k] != 0xCA5E)
                --k;
            Log("  first difference at %u: the case at %u (kind %u)", static_cast<unsigned>(i), static_cast<unsigned>(k),
                traces[0][k + 1]);
            break;
        }
    }
    Compare(traces[0], traces[1], "skeleton component (create, init, bones, splats, spawns, member functions)");
}

} // namespace

SELFTEST(Early, TestSkeletonComponent, RemoveStandIns);
