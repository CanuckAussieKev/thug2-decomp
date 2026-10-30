// Self-tests for the fam component (src/game/obj/fam.cpp, 0x0041D170-0x0041D6D4) and the flexible particle component
// (flexible_particle.cpp, 0x0041D6E0-0x0041E25E): the original and ours, every call they make stood in for and
// recorded (the allocations, the base component, the script structure's getters and adders, the scripts, the streams,
// the asset manager, the animation blender, the particle manager and its systems, the suspend component, the clock).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/anim_blender.h"
#include "game/obj/fam.h"
#include "game/obj/flexible_particle.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/suspend_component.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/particle_system.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CFamComponent;
using Obj::CFlexibleParticleComponent;

struct World {
    alignas(16) uint8_t component[0x60];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t blender[0x60];
    alignas(16) uint8_t assman[0x40];
    alignas(16) uint8_t asset[0x20];
    alignas(16) uint8_t structMemory[2][0x10];
    alignas(16) uint8_t script[0xD0];
    alignas(16) uint8_t system[0x140];
    alignas(16) uint8_t otherSystem[0x140];
    alignas(16) uint8_t suspend[0x40];
    alignas(16) uint8_t otherObject[0x120];
    alignas(16) uint8_t model[0x240];
    alignas(16) uint8_t particleManager[0x20];
    alignas(16) uint8_t netManager[0x20];
    alignas(16) uint8_t rotation[0x40];
    alignas(16) uint8_t pool[0x20];
    void* blenderVtable[16];
    void* objectVtable[12];
    void* systemVtable[4];
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

struct Answers {
    uint32_t seed;
    bool callocFails;
    uint32_t have;        // which parameters the structure has (bits by the name's checksum)
    bool streamExists;
    bool loadAnim;
    bool haveAsset;
    bool haveAssetData;
    bool findSystem;
    bool createSystem;
    bool shouldUpdate;
    bool inNetGame;
    int structAt;
    double frameSeconds;
    uint32_t forceOn[2];  // names the structure has whatever `have` says
    uint32_t forceOff;    // and one it hasn't
};
Answers g_a;
bool Has(uint32_t name)
{
    if (name == g_a.forceOff)
        return false;
    if (name == g_a.forceOn[0] || name == g_a.forceOn[1])
        return true;
    return ((name * 2654435761u) >> 27 ^ g_a.have) & 1;
}

double __cdecl StandInFrameSeconds()
{
    Record({0xEAA0});
    return g_a.frameSeconds;
}

void FillFloats(Lcg& random, uint8_t* p, size_t size, int spread)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = static_cast<float>(static_cast<int>(random.Below(2 * spread + 1)) - spread) / 7.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void StandIn(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        std::memset(g_world.component, 0xCD, sizeof(g_world.component)); // (dirty: what's left unset shows)
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
    s.At({0x0046F060, AddressOf(Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The script structures: from the pool or the manager.
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.structMemory[g_a.structAt++ % 2]);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return reinterpret_cast<uint32_t>(g_world.structMemory[g_a.structAt++ % 2]);
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Rel(r.ecx)});
        return r.ecx;
    });
    using AddChecksumFn = void (Script::CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&Script::CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        return 0u;
    });
    using AddIntegerFn = void (Script::CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&Script::CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Rel(r.args[0])});
        return 0u;
    });
    // The structure's getters.
    using GetChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&Script::CStruct::GetChecksum))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        // (the unnamed checksum is always there: Obj_PlayFam's slot for it is uninitialised)
        Record({0x6950, r.ecx, r.args[0], Rel(r.args[1]), r.args[0] ? Rel(*out) : 0u, r.args[2]});
        if (r.args[0] && !Has(r.args[0]))
            return 0u;
        *out = r.args[0] == 0x0830ECAF ? 0x0B1E0002u : (r.args[0] * 3) ^ g_a.seed;
        return 1u;
    });
    using GetIntegerFn = bool (Script::CStruct::*)(uint32_t, int*, int);
    s.At({0x00476630, AddressOf(static_cast<GetIntegerFn>(&Script::CStruct::GetInteger))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<int*>(r.args[1]);
        Record({0x6630, r.ecx, r.args[0], Rel(r.args[1]), static_cast<uint32_t>(*out), r.args[2]});
        if (!Has(r.args[0]))
            return 0u;
        *out = static_cast<int>((r.args[0] ^ g_a.seed) % 7) - 2;
        return 1u;
    });
    using GetFloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    s.At({0x004766D0, AddressOf(static_cast<GetFloatFn>(&Script::CStruct::GetFloat))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x66D0, r.ecx, r.args[0], Rel(r.args[1]), Bits(*out), r.args[2]});
        if (!Has(r.args[0]))
            return 0u;
        *out = static_cast<float>((r.args[0] ^ g_a.seed) % 101) / 7.0f - 3.0f;
        return 1u;
    });
    using GetStringFn = bool (Script::CStruct::*)(uint32_t, const char**, int);
    s.At({0x00476C60, AddressOf(static_cast<GetStringFn>(&Script::CStruct::GetString))}, 12, [](Regs& r) {
        Record({0x6C60, r.ecx, r.args[0], Rel(r.args[1]), r.args[2] & 0xFF});
        if (!Has(r.args[0]))
            return 0u;
        *reinterpret_cast<const char**>(r.args[1]) = "test_model";
        return 1u;
    });
    using GetStructureFn = bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int);
    s.At({0x004767C0, AddressOf(static_cast<GetStructureFn>(&Script::CStruct::GetStructure))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x67C0, r.ecx, r.args[0], Rel(r.args[1]), Rel(*out), r.args[2]});
        if (!Has(r.args[0]))
            return 0u;
        *out = 0x5C710000 + g_a.seed;
        return 1u;
    });
    using GetVectorFn = bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int);
    s.At({0x00476FB0, AddressOf(static_cast<GetVectorFn>(&Script::CStruct::GetVector))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x6FB0, r.ecx, r.args[0], Rel(r.args[1]), r.args[2]});
        if (!Has(r.args[0]))
            return 0u;
        out[0] = 1.5f;
        out[1] = -2.5f;
        out[2] = static_cast<float>(g_a.seed);
        out[3] = 1.0f;
        return 1u;
    });
    using FlagFn = bool (Script::CStruct::*)(uint32_t);
    s.At({0x00476B40, AddressOf(static_cast<FlagFn>(&Script::CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, r.ecx, r.args[0]});
        return Has(r.args[0]) ? 1u : 0u;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 40u + g_a.seed;
    });
    // The fam's world.
    s.At({0x00474350, AddressOf(Script::StopScriptsUsingObjectThatReferTo)}, 0, [](Regs& r) {
        Record({0x4350, Rel(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        switch (r.args[0]) {
        case 0x7149B753:
            return reinterpret_cast<uint32_t>(g_world.blender);
        case 0xCE0CA665:
            return g_a.seed % 5 == 2 ? 0u : reinterpret_cast<uint32_t>(g_world.suspend);
        case 0x286A8D26:
            return reinterpret_cast<uint32_t>(g_world.model);
        default:
            return 0u;
        }
    });
    g_world.blenderVtable[12] = s.Thunk(12, [](Regs& r) {
        Record({0xB012, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
        return 1u;
    });
    s.At({0x00419AB0, AddressOf(&Obj::CAnimBlenderComponent::GetAnimSet)}, 0, [](Regs& r) {
        Record({0x9AB0, Rel(r.ecx)});
        return 0xA5E70000u + g_a.seed;
    });
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Record({0x7B90, Rel(r.ecx), r.args[0], r.args[1]});
        return g_a.haveAsset ? reinterpret_cast<uint32_t>(g_world.asset) : 0u;
    });
    s.At({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, [](Regs& r) {
        Record({0x7B20, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, Rel(r.ecx), r.args[0], r.args[1]});
        return g_a.haveAssetData ? reinterpret_cast<uint32_t>(g_world.asset) : 0u;
    });
    s.At({0x00408870, AddressOf(&Ass::CAssMan::LoadAnim)}, 20, [](Regs& r) {
        Record({0x8870, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2],
                r.args[3] & 0xFF, r.args[4] & 0xFF});
        return g_a.loadAnim ? 1u : 0u;
    });
    s.At({0x00407A70, AddressOf(&Ass::CAssMan::SetUnknown18)}, 4, [](Regs& r) {
        Record({0x7A70, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(Dbg::Printf)}, 0, [](Regs& r) {
        uint32_t format = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x1C30, format, format == Hash("loading %s\n") ? Hash(reinterpret_cast<const char*>(r.args[1])) : 0u});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // (the game's sprintf: only the original calls it)
    s.At({0x00401B90, AddressOf(Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        uint32_t hash = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x1B90, hash});
        return hash ^ 0x5A5A0000;
    });
    s.At({0x00450500, AddressOf(Pcm::StreamExists)}, 0, [](Regs& r) {
        Record({0x0500, r.args[0]});
        return g_a.streamExists ? 1u : 0u;
    });
    s.At({0x004506D0, AddressOf(Pcm::StopStream)}, 0, [](Regs& r) {
        Record({0x06D0, r.args[0]});
        return 0u;
    });
    s.At({0x00451A50, AddressOf(Pcm::PreLoadStream)}, 0, [](Regs& r) {
        Record({0x1A50, r.args[0], r.args[1]});
        return g_a.seed % 4 == 1 ? 0u : 0x57E40000u + g_a.seed;
    });
    using SpawnFn = Script::CScript* (*)(uint32_t, Script::CStruct*, uint32_t, Script::CStruct*, int, uint32_t, bool,
                                         bool, bool, bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnFn>(&Script::SpawnScript))}, 0, [](Regs& r) {
        Record({0x5420, r.args[0], Rel(r.args[1]), r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        return reinterpret_cast<uint32_t>(g_world.script);
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // The particle manager and its systems.
    s.At({0x0049AC20, AddressOf(&Nx::CFlexibleParticleManager::FindSystem)}, 4, [](Regs& r) {
        Record({0xAC20, Rel(r.ecx), Rel(r.args[0])});
        return g_a.findSystem ? reinterpret_cast<uint32_t>(g_world.otherSystem) : 0u;
    });
    s.At({0x0049AC60, AddressOf(&Nx::CFlexibleParticleManager::CreateSystem)}, 0x34, [](Regs& r) {
        Record({0xAC60, Rel(r.ecx), Rel(r.args[0])});
        for (int i = 1; i < 11; ++i)
            Record({r.args[i]});
        Record({r.args[11] & 0xFF, Hash(reinterpret_cast<const char*>(r.args[12]))});
        return g_a.createSystem ? reinterpret_cast<uint32_t>(g_world.system) : 0u;
    });
    s.At({0x0049AD90, AddressOf(&Nx::CFlexibleParticleManager::DestroySystemNow)}, 4, [](Regs& r) {
        Record({0xAD90, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0049ADB0, AddressOf(&Nx::CFlexibleParticleManager::DestroySystemWhenEmpty)}, 4, [](Regs& r) {
        Record({0xADB0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00497B00, AddressOf(&Nx::CParticleSystem::CallMemberFunction)}, 12, [](Regs& r) {
        Record({0x7B00, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
        return 0x1234FF00u | (r.args[0] & 1); // (only AL counts)
    });
    s.At({0x00497790, AddressOf(&Nx::CParticleSystem::SetEmitScript)}, 12, [](Regs& r) {
        Record({0x7790, Rel(r.ecx), r.args[0], Rel(r.args[1]), Rel(r.args[2])});
        return 0u;
    });
    s.At({0x00497900, AddressOf(&Nx::CParticleSystem::SetUpdateScript)}, 12, [](Regs& r) {
        Record({0x7900, Rel(r.ecx), r.args[0], Rel(r.args[1]), Rel(r.args[2])});
        return 0u;
    });
    s.At({0x004987F0, AddressOf(&Nx::CParticleSystem::Update)}, 8, [](Regs& r) {
        Record({0x87F0, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00496F60, AddressOf(&Nx::CParticleSystem::RunEmitScript)}, 0, [](Regs& r) {
        Record({0x6F60, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00497AB0, AddressOf(&Nx::CParticleSystem::GetRotationMatrix)}, 4, [](Regs& r) {
        Record({0x7AB0, Rel(r.ecx), Rel(r.args[0])});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            out[i] = static_cast<float>(i) * 0.5f - static_cast<float>(g_a.seed);
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2]), HashBytes(reinterpret_cast<const uint8_t*>(a), 64),
                HashBytes(reinterpret_cast<const uint8_t*>(b), 64)});
        float result[16];
        for (int i = 0; i < 16; ++i)
            result[i] = a[i] * 0.5f - b[15 - i] * 0.25f;
        std::memcpy(out, result, sizeof(result));
        return r.args[0];
    });
    g_world.systemVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0x5900, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_world.objectVtable[6] = s.Thunk(0, [](Regs& r) {
        Record({0x0B06, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.otherObject);
    });
    s.At({0x00433EA0, AddressOf(&Obj::CSuspendComponent::ShouldAnimate)}, 4, [](Regs& r) {
        Record({0x3EA0, Rel(r.ecx), r.args[0]});
        return g_a.shouldUpdate ? 1u : 0u;
    });
    s.At({0x00433E70, AddressOf(&Obj::CSuspendComponent::DisableLod)}, 0, [](Regs& r) {
        Record({0x3E70, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, Rel(r.ecx)});
        return g_a.inNetGame ? 1u : 0u;
    });
    s.Bytes(0x005BEAA0, Branch(0xE9, 0x005BEAA0, reinterpret_cast<void*>(&StandInFrameSeconds)));
    uint32_t oursFrame = AddressOf(&Tmr::GetSlomoFrameSeconds);
    s.Bytes(oursFrame, Branch(0xE9, oursFrame, reinterpret_cast<void*>(&StandInFrameSeconds)));
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0xFA700000 + seed);
    FillFloats(random, g_world.component, sizeof(g_world.component), 2000);
    FillFloats(random, g_world.object, sizeof(g_world.object), 2000);
    FillFloats(random, g_world.system, sizeof(g_world.system), 2000);
    FillFloats(random, g_world.otherSystem, sizeof(g_world.otherSystem), 2000);
    FillFloats(random, g_world.model, sizeof(g_world.model), 2000);
    for (uint8_t* system : {g_world.system, g_world.otherSystem}) {
        *reinterpret_cast<void***>(system) = g_world.systemVtable;
        system[0x12A] = static_cast<uint8_t>(seed & 1);
        system[0x12B] = static_cast<uint8_t>((seed >> 1) & 1);
    }
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    object->m_vtable = g_world.objectVtable;
    object->m_id = 0x0B1E0000 + seed;
    object->m_compositeFlags = seed % 3 == 0 ? Obj::COMPOSITE_FLAG_HIDDEN : 0;
    auto* c = reinterpret_cast<Obj::CBaseComponent*>(g_world.component);
    c->m_object = object;
    std::memset(g_world.blender, 0, sizeof(g_world.blender));
    *reinterpret_cast<void***>(g_world.blender) = g_world.blenderVtable;
    std::memset(g_world.assman, 0, sizeof(g_world.assman));
    *reinterpret_cast<int*>(g_world.assman + 0x18) = 0x18180000 + static_cast<int>(seed);
    std::memset(g_world.script, 0, sizeof(g_world.script));
    std::memset(g_world.suspend, 0, sizeof(g_world.suspend));
    g_world.suspend[0x30] = seed % 4 == 3 ? 1 : 0;
}

void DumpComponent(std::vector<uint32_t>& trace, size_t size)
{
    for (size_t i = 0; i < size / 4; ++i)
        trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
}

constexpr uint32_t kFamAddresses[] = {0x0041D200, 0x0041D170, 0x0041D190, 0x0041D1A0, 0x0041D2C0,
                                      0x0041D3E0, 0x0041D480, 0x0041D660, 0x0041D280};

// Made, registered, destroyed; UnloadFam (none loaded, one with and without its asset, no object); LoadFam (loaded or
// not); PlayFam (no such stream, a stream playing or not, a volume or none, stream only or not, the animation there or
// not); the two members.
void TestFamComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F3614, 16}, {0x007CC979, 1}, {0x006918DC, 4}});
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            memory.Zero();
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<void*>(0x006918DC) = g_world.assman;
            if (seed & 2)
                GameVariable<void*>(0x006F361C) = g_world.pool;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kFamAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CFamComponent*>(g_world.component);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            g_a.callocFails = seed == 7;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041D200))()));
            g_a.callocFails = false;
            DumpComponent(trace, sizeof(CFamComponent));
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x0041D170))();
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041D190))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041D1A0))(
                c, seed % 3)));
            ResetWorld(seed);
            Lcg random(0xFA300000 + seed);
            c->m_fam = seed % 5 == 0 ? 0u : 0xFA000000 + seed;
            c->m_stream = seed % 3 == 0 ? 0u : 0x5E000000 + seed;
            for (int k = 0; k < 6; ++k) {
                uint32_t pick = random.Next();
                g_a.have = pick >> 3;
                g_a.streamExists = pick % 5 != 0;
                g_a.loadAnim = (pick >> 4) & 1;
                g_a.haveAsset = (pick >> 5) & 1;
                g_a.haveAssetData = (pick >> 6) & 1;
                if (k == 4 && (seed & 4))
                    c->m_object = nullptr; // (UnloadFam's object check)
                switch (pick % 6) {
                case 0:
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041D2C0))(c);
                    break;
                case 1:
                    reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(Code(original, 0x0041D3E0))(
                        c, 0xC0FFEE00 + seed, 0xA5E70000u + seed);
                    break;
                case 2:
                case 3:
                    if (!c->m_object)
                        c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, bool, int, int)>(
                                        Code(original, 0x0041D480))(c, 0xF00D0000 + seed, (pick & 0x80) != 0,
                                                                     static_cast<int>(pick % 3), 7) &
                                    0xFF);
                    break;
                case 4:
                    if (!c->m_object)
                        c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(
                        Code(original, 0x0041D660))(c, params, script));
                    break;
                default:
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(
                        Code(original, 0x0041D280))(c, params, script));
                    break;
                }
                DumpComponent(trace, sizeof(CFamComponent));
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < sizeof(g_world.script) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.script)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "fam component: made, unloaded / loaded, played, the members");
    }
    delete &s;
}

constexpr uint32_t kParticleAddresses[] = {0x0041E1E0, 0x0041D6E0, 0x0041DA00, 0x0041DA80, 0x0041E240, 0x0041DB00,
                                           0x0041D740, 0x0041DF80, 0x0041D760, 0x0041D780, 0x0041D700, 0x0041D7F0,
                                           0x0041D820, 0x0041D840, 0x0041D8F0, 0x0041D920, 0x0041D940, 0x0041D960,
                                           0x0041E190, 0x0041D9C0};

// Made, registered, constructed, destroyed (with a system and without); InitFromStructure from random parameter sets
// (perm finding a system or not, the system made or not); Finalize; Update (no system, suspended - in a network game or
// not, before / at / after the update time, NaN -, updated or idle, EnableRotate, a brightness model, hidden); Hide;
// the members, through the table entry runner too; the system's flag setter.
void TestFlexibleParticleComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006FA7C0, 4}, {0x006FA7D0, 4}, {0x0067D6D0, 4}});
    float nan = std::numeric_limits<float>::quiet_NaN();
    for (uint32_t seed = 1; seed <= 48; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            GameVariable<void*>(0x006FA7C0) = g_world.particleManager;
            GameVariable<void*>(0x006FA7D0) = g_world.netManager;
            GameVariable<float>(0x0067D6D0) = 0.05f;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kParticleAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CFlexibleParticleComponent*>(g_world.component);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            g_a.callocFails = seed == 9;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041E1E0))()));
            g_a.callocFails = false;
            DumpComponent(trace, sizeof(CFlexibleParticleComponent));
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x0041D6E0))();
            c->mp_system.mp_object = seed & 1 ? reinterpret_cast<Nx::CParticleSystem*>(g_world.system) : nullptr;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041DA80))(c);
            DumpComponent(trace, sizeof(CFlexibleParticleComponent));
            c->mp_system.mp_object = nullptr;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041E240))(
                c, seed % 3)));
            ResetWorld(seed);
            std::memset(g_world.component, 0x5A, sizeof(g_world.component));
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x0041DA00))(c)));
            DumpComponent(trace, sizeof(CFlexibleParticleComponent));
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
            // InitFromStructure.
            Lcg random(0x9A700000 + seed);
            g_a.have = random.Next();
            g_a.findSystem = (seed & 2) != 0;
            g_a.createSystem = seed % 7 != 3;
            if (seed % 4 == 1) { // (update_Brightness without ObjID: the 0x184197D5 checksum is the object's id)
                g_a.forceOn[0] = 0xBBFA4B06;
                g_a.forceOn[1] = 0x184197D5;
                g_a.forceOff = 0x0830ECAF;
            }
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041DB00))(c, params);
            g_a.forceOn[0] = g_a.forceOn[1] = g_a.forceOff = 0;
            DumpComponent(trace, sizeof(CFlexibleParticleComponent));
            for (size_t i = 0; i < sizeof(g_world.system) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.system)[i]));
            trace.push_back(Bits(GameVariable<float>(0x0067D6D0)));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041D740))(c);
            GameVariable<float>(0x0067D6D0) = 0.05f; // (an update time a frame can reach exactly)
            // Updates.
            for (int k = 0; k < 8; ++k) {
                uint32_t pick = random.Next();
                c->mp_system.mp_object = pick % 9 == 0 ? nullptr : reinterpret_cast<Nx::CParticleSystem*>(g_world.system);
                g_world.suspend[0x30] = (pick >> 4) % 3 == 0 ? 1 : 0;
                g_a.inNetGame = (pick >> 6) % 4 == 0;
                g_a.shouldUpdate = (pick >> 8) % 3 != 0;
                static const double frames[] = {0.01, 0.0166666, 0.05, 0.0, 0.1};
                g_a.frameSeconds = frames[(pick >> 10) % 5];
                c->m_time = (pick >> 13) % 7 == 0 ? nan : static_cast<float>((pick >> 13) % 4) * 0.0125f;
                if (k == 1) { // (reaching the update time exactly)
                    c->m_time = 0.0f;
                    g_a.frameSeconds = 0.05;
                    g_world.suspend[0x30] = 1;
                    g_a.inNetGame = false;
                    c->mp_system.mp_object = reinterpret_cast<Nx::CParticleSystem*>(g_world.system);
                }
                c->m_rotate = (pick >> 16) & 1;
                c->mp_brightnessModel = (pick >> 17) & 1 ? reinterpret_cast<Obj::CBaseComponent*>(g_world.model) : nullptr;
                g_world.system[0x12A] = static_cast<uint8_t>((pick >> 18) & 1);
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041DF80))(c);
                DumpComponent(trace, sizeof(CFlexibleParticleComponent));
                for (size_t i = 0; i < sizeof(g_world.system) / 4; ++i)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.system)[i]));
                for (size_t i = 0x30; i < 0x40; ++i) // (the display matrix)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.object)[i]));
            }
            // Hide, the members (with a system and without; through the entry runner too), the flag setter.
            for (int k = 0; k < 2; ++k) {
                c->mp_system.mp_object = k == 0 ? reinterpret_cast<Nx::CParticleSystem*>(g_world.system) : nullptr;
                g_a.have = random.Next();
                *reinterpret_cast<float*>(g_world.system + 0x70) = k ? 0.0f : static_cast<float>(seed % 3) - 1.0f;
                reinterpret_cast<void(__thiscall*)(void*, bool)>(Code(original, 0x0041D760))(c, (seed & 1) != 0);
                for (uint32_t address : {0x0041D7F0u, 0x0041D820u, 0x0041D840u, 0x0041D8F0u, 0x0041D920u, 0x0041D940u,
                                         0x0041D960u}) {
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                        Code(original, address))(c, 0x63274395 + k, params, script));
                }
                for (uint32_t name : {0x2299A9F2u, 0x96896315u, 0x12345678u}) {
                    if (name == 0x2299A9F2u && k == 1)
                        continue; // (IsEmitting reads the system unchecked)
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                        Code(original, 0x0041D780))(c, name, params, script));
                }
                Script::CMemberFunctionEntry entry = {};
                entry.mName = 0x711B1A6A;
                entry.mCode = original ? reinterpret_cast<void*>(0x0041D7F0) : OursAt(0x0041D7F0);
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*, void*)>(
                    Code(original, 0x0041D700))(c, &entry, params, script));
                // (an adjustment of 0xC: the member sees the name's slot as its system)
                entry.mThisAdjustment = 0xC;
                uint32_t name = c->m_name;
                c->m_name = reinterpret_cast<uint32_t>(g_world.otherSystem);
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*, void*)>(
                    Code(original, 0x0041D700))(c, &entry, params, script));
                c->m_name = name;
                entry.mThisAdjustment = 0;
                entry.mCode = nullptr;
                entry.mName = 0x96896315;
                c->m_vtable = reinterpret_cast<void**>(0x00645FE4); // (slot 12: the original's CallMemberFunction)
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*, void*)>(
                    Code(original, 0x0041D700))(c, &entry, params, script));
                for (size_t i = 0; i < sizeof(g_world.system) / 4; ++i)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.system)[i]));
            }
            c->mp_system.mp_object = reinterpret_cast<Nx::CParticleSystem*>(g_world.system);
            trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                Code(original, 0x0041E190))(c, 0x4893F05D, params, script));
            DumpComponent(trace, sizeof(CFlexibleParticleComponent));
            for (int bit : {0, 3, 14, 31}) {
                reinterpret_cast<void(__thiscall*)(void*, int, bool)>(Code(original, 0x0041D9C0))(g_world.system, bit,
                                                                                                  bit != 3);
                trace.push_back(*reinterpret_cast<uint32_t*>(g_world.system + 0x120));
            }
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "flexible particle component: made, initialised, updated, the members");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestFamComponent, RemoveStandIns);
SELFTEST(Early, TestFlexibleParticleComponent, RemoveStandIns);
