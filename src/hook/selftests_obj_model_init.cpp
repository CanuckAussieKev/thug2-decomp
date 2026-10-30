// Self-tests for the model component's construction, destruction, initialisation and update
// (src/game/obj/model_component_init.cpp): the original and ours on the same arena, every call into the rest of the
// game stood in for and recorded (the model, its geoms and lights, the sectors, the bone volumes, the memory manager,
// the script structures).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/manager.h"
#include "game/obj/anim_blender.h"
#include "game/obj/model_component.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/gfx/model_builder.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;
using Obj::CModelComponent;

constexpr uint32_t kComponent = 0x0000, kObject = 0x0300, kModel = 0x0500, kLights = 0x0600,
                   kSkeletonComponent = 0x0700, kSkeleton = 0x0780, kSkeletonData = 0x07C0, kBlender = 0x0800,
                   kSuspend = 0x0880, kSource = 0x0900, kSourceModel = 0x0C00, kSourceObject = 0x0D00, kGeom = 0x0E00,
                   kClone = 0x0E80, kSector = 0x0F00, kVolumes = 0x0F80, kMade = 0x1000, kHeap = 0x1040,
                   kLevelObject = 0x1100, kLevelModelComponent = 0x1200, kLevelModel = 0x1500, kArray = 0x1600,
                   kArrayElements = 0x1640, kParams = 0x3000, kProfile = 0x3100;

constexpr uint32_t kSkeletonType = 0x222756D5, kAnimBlenderType = 0x7149B753, kSuspendType = 0xCE0CA665,
                   kModelType = 0x286A8D26;

struct Answers {
    uint32_t calloc;
    bool haveSkeleton, haveBlender, haveSuspend, blenderReady, scale, sector, sectorGeom, geomFound, volumes,
        volumesFound, made, boneVolumesOn;
    int sourceGeoms;
    uint32_t missingGeom; // a geom name not in the source model
};
Answers g_a;
void* g_componentVtable[16];

const char kFileName[] = "Models\\SomeObject";
const char kNone[] = "none";

// Rec, with the bool arguments in `bools` (a bit per argument) recorded by their low byte: the original pushes the
// whole register a SETcc left the flag in.
void* RecBools(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t tag, uint32_t bools,
               std::function<uint32_t(Regs&)> answer = nullptr)
{
    return s.At(addresses, static_cast<uint16_t>(4 * args), [args, tag, bools, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i)
            g_trace.push_back(bools >> i & 1 ? r.args[i] & 0xFF : Rel(r.args[i]));
        return answer ? answer(r) : 0u;
    });
}

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    StandInStructGetters(s);
    // (strings: only the test's own - a checksum under the same name isn't one)
    s.At({0x00476590, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(
                          &Script::CStruct::GetString))},
         12, [](Regs& r) {
             Record({0x6590, Rel(r.ecx), r.args[0], (r.args[2] & 0xFF) != 0 ? 1u : 0u});
             uint32_t value;
             if (!FindComponent(r.ecx, r.args[0], &value) ||
                 (value != reinterpret_cast<uint32_t>(kFileName) && value != reinterpret_cast<uint32_t>(kNone)))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = value;
             return 1u;
         });
    g_componentVtable[1] = RecThunk(s, 0, 0x0001); // Update (through the vtable)
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        if (r.ecx == Addr(kSourceObject) || r.ecx == Addr(kLevelObject))
            return r.args[0] == kModelType ? (r.ecx == Addr(kSourceObject) ? Addr(kSource) : Addr(kLevelModelComponent))
                                           : 0u;
        switch (r.args[0]) {
        case kSkeletonType: return g_a.haveSkeleton ? Addr(kSkeletonComponent) : 0u;
        case kAnimBlenderType: return g_a.haveBlender ? Addr(kBlender) : 0u;
        case kSuspendType: return g_a.haveSuspend ? Addr(kSuspend) : 0u;
        default: return 0u;
        }
    });
    Rec(s, {0x00419CA0, AddressOf(&Obj::CAnimBlenderComponent::ModelReady)}, 0, 0x9CA0,
        [](Regs&) { return g_a.blenderReady ? 1u : 0u; });
    Rec(s, {0x00460AD0, AddressOf(&Obj::FindObject)}, 1, 0x0AD0, [](Regs&) { return Addr(kLevelObject); }, false,
        true);
    Rec(s, {0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 1, 0x19F0,
        [](Regs&) { return Addr(kSourceObject); });
    Rec(s, {0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 1, 0x8F60,
        [](Regs&) { return g_a.boneVolumesOn ? 1u : 0u; }, false, true);
    // The model and its geoms.
    Rec(s, {0x0048BFA0, AddressOf(Nx::CreateModel)}, 1, 0xBFA0, [](Regs&) { return Addr(kModel); }, false, true);
    Rec(s, {0x0048C3C0, AddressOf(Nx::Engine::DeleteGeomObject)}, 1, 0xC3C0, nullptr, false, true);
    Rec(s, {0x00494BA0}, 0, 0x4BA0);
    Rec(s, {0x00492EE0, AddressOf(&Nx::CModel::SetScale)}, 1, 0x2EE0, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), {v[0], v[1], v[2], v[3]});
        return 0u;
    });
    Rec(s, {0x00487B00, AddressOf(Gfx::GetScaleFromParams)}, 2, 0x7B00, [](Regs& r) {
        if (!g_a.scale)
            return 0u;
        auto* v = reinterpret_cast<float*>(r.args[0]);
        v[0] = 2.0f;
        v[2] = 0.5f;
        return 1u;
    }, false, true);
    Rec(s, {0x00492E10, AddressOf(&Nx::CModel::SetDisplayMode)}, 1, 0x2E10);
    RecBools(s, {0x004934A0, AddressOf(&Nx::CModel::AddAssetGeom)}, 4, 0x34A0, 4, [](Regs&) { return 1u; });
    Rec(s, {0x00493CD0, AddressOf(&Nx::CModel::GetGeomByName)}, 1, 0x3CD0,
        [](Regs& r) { return g_a.geomFound && r.args[0] != g_a.missingGeom ? Addr(kGeom) : 0u; });
    Rec(s, {0x0048DDC0, AddressOf(&Nx::CGeom::CloneForModel)}, 2, 0xDDC0, [](Regs&) { return Addr(kClone); });
    Rec(s, {0x0048DD80, AddressOf(&Nx::CGeom::Clone)}, 2, 0xDD80, [](Regs&) { return Addr(kClone); });
    Rec(s, {0x0048DE60}, 1, 0xDE60);
    Rec(s, {0x004938E0}, 3, 0x38E0);
    Rec(s, {0x00492E40, AddressOf(&Nx::CModel::GetNumGeoms)}, 0, 0x2E40, [](Regs&) { return static_cast<uint32_t>(g_a.sourceGeoms); });
    Rec(s, {0x00494640}, 1, 0x4640, [](Regs& r) { return Addr(kGeom) + 4 * r.args[0]; });
    Rec(s, {0x00494680}, 1, 0x4680, [](Regs& r) { return 0x6E0E0000u + r.args[0]; });
    Rec(s, {0x0048C510}, 1, 0xC510, [](Regs&) { return g_a.sector ? Addr(kSector) : 0u; }, false, true);
    s.At({0x00487A70}, 0, [](Regs& r) { // (the extension's text recorded, not where it is)
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        const char* ext = reinterpret_cast<const char*>(r.args[1]);
        g_trace.insert(g_trace.end(), {0x7A70, Rel(r.args[0]), HashBytes(reinterpret_cast<const uint8_t*>(ext),
                                                                          std::strlen(ext)), Rel(r.args[2])});
        char* out = reinterpret_cast<char*>(r.args[2]);
        size_t nameLength = std::strlen(name);
        std::memcpy(out, name, nameLength);
        std::memcpy(out + nameLength, ext, std::strlen(ext) + 1);
        return 0u;
    });
    RecBools(s, {0x004935E0, AddressOf(&Nx::CModel::AddFileGeom)}, 8, 0x35E0, 4 | 0x10, [](Regs& r) {
        const char* path = reinterpret_cast<const char*>(r.args[0]);
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(path), std::strlen(path)));
        return 1u;
    });
    Rec(s, {0x00492F30, AddressOf(&Nx::CModel::GetBoundingSphere)}, 1, 0x2F30, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        v[0] = 1.5f;
        v[1] = -2.0f;
        v[2] = 3.25f;
        v[3] = 40.0f;
        return r.args[0];
    });
    Rec(s, {0x00493BD0, AddressOf(&Nx::CModel::CreateLights)}, 0, 0x3BD0, [](Regs& r) {
        F<uint32_t>(reinterpret_cast<void*>(r.ecx), 0x2C) = Addr(kLights);
        return 0u;
    });
    Rec(s, {0x00493C10, AddressOf(&Nx::CModel::DestroyLights)}, 0, 0x3C10);
    Rec(s, {0x0048E4D0, AddressOf(&Nx::CModelLights::SetPositionPointer)}, 1, 0xE4D0);
    Rec(s, {0x00492DD0, AddressOf(&Nx::CModel::SetSkeleton)}, 1, 0x2DD0);
    RecBools(s, {0x00492E90, AddressOf(&Nx::CModel::EnableShadowVolume)}, 1, 0x2E90, 1);
    Rec(s, {0x0048E660, AddressOf(&Nx::CModelLights::UpdateBrightness)}, 0, 0xE660);
    RecBools(s, {0x00493090, AddressOf(&Nx::CModel::Render)}, 3, 0x3090, 2, [](Regs& r) {
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(m), 64));
        return 0u;
    });
    Rec(s, {0x00493410, AddressOf(&Nx::CModel::CopyBones)}, 1, 0x3410);
    // The bone volumes.
    Rec(s, {0x00492FC0, AddressOf(&Nx::CModel::GetBoneVolumes)}, 0, 0x2FC0, [](Regs&) { return g_a.volumes ? Addr(kVolumes) : 0u; });
    Rec(s, {0x00492F90, AddressOf(&Nx::CModel::SetBoneVolumes)}, 1, 0x2F90);
    Rec(s, {0x0048B3C0, AddressOf(Gfx::FindBoneVolumes)}, 1, 0xB3C0, [](Regs&) { return g_a.volumesFound ? Addr(kVolumes) : 0u; }, false, true);
    Rec(s, {0x0048B350, AddressOf(&Gfx::CBoneVolumes::Construct)}, 1, 0xB350, [](Regs& r) { return r.ecx; });
    Rec(s, {0x0048BEA0, AddressOf(&Gfx::CBoneVolumes::Register)}, 1, 0xBEA0);
    Rec(s, {0x0048B430, AddressOf(&Gfx::CBoneVolumes::Fill)}, 1, 0xB430);
    Rec(s, {0x005BAF50, AddressOf(&Mem::Manager::GetNamedHeap)}, 2, 0xAF50, [](Regs&) { return Addr(kHeap); });
    Rec(s, {0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 1, 0xB0E0);
    Rec(s, {0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xB110);
    Rec(s, {0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 3, 0xAFC0,
        [](Regs&) { return g_a.made ? Addr(kMade) : 0u; });
    // The model builder.
    RecBools(s, {0x004897D0, AddressOf(&Gfx::CModelBuilder::Construct)}, 2, 0x97D0, 1, [](Regs& r) { return r.ecx; });
    Rec(s, {0x0048A2F0, AddressOf(&Gfx::CModelBuilder::Build)}, 4, 0xA2F0);
    Rec(s, {0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, 0x15D0);
}

constexpr uint32_t kInitAddresses[] = {0x00421490, 0x00420BF0, 0x00420000, 0x00420270, 0x004204B0,
                                       0x004214F0, 0x004219F0, 0x00420480, 0x00421360, 0x00421230,
                                       0x0041FFA0, 0x00420350, 0x00420D10};

// The arena: the component (its object, model, skeleton), a source model component to clone from, a level object.
void SetUpWorld(uint32_t seed, Lcg& random)
{
    FillArena(seed);
    g_structs.clear();
    auto* c = reinterpret_cast<CModelComponent*>(At(kComponent));
    c->m_vtable = g_componentVtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(At(kObject));
    c->mp_model = reinterpret_cast<Nx::CModel*>(At(kModel));
    c->mp_skeleton = random.Below(3) ? reinterpret_cast<Obj::CBaseComponent*>(At(kSkeletonComponent)) : nullptr;
    c->mp_animBlender = random.Below(3) ? reinterpret_cast<Obj::CBaseComponent*>(At(kBlender)) : nullptr;
    c->m_levelObject = random.Below(4) == 0;
    c->m_refObjectName = 0x7EF00000 + seed;
    c->m_colorValueCount = 0;
    F<uint32_t>(kObject + 0x104) = random.Next(); // (COMPOSITE_FLAG_FINALIZED or not)
    F<uint32_t>(kModel + 0x2C) = random.Below(3) ? Addr(kLights) : 0u;
    F<uint8_t>(kModel + 0x44) = static_cast<uint8_t>(random.Below(8));
    F<uint32_t>(kSkeletonComponent + 0x18) = Addr(kSkeleton);
    F<uint32_t>(kSkeleton + 0x08) = Addr(kSkeletonData);
    F<uint32_t>(kSkeleton + 0x14) = 0x5CE10000 + seed;
    F<uint32_t>(kSector + 0x14) = g_a.sectorGeom ? Addr(kGeom) : 0u;
    F<int>(kVolumes + 8) = 3;
    // The source model component: its model and colour values.
    auto* source = reinterpret_cast<CModelComponent*>(At(kSource));
    source->mp_model = reinterpret_cast<Nx::CModel*>(At(kSourceModel));
    source->m_colorValueCount = static_cast<int>(random.Below(5));
    reinterpret_cast<CModelComponent*>(At(kLevelModelComponent))->mp_model =
        reinterpret_cast<Nx::CModel*>(At(kLevelModel));
    // An array of geom names (some 0).
    F<uint32_t>(kArray + 4) = 1 + random.Below(4);
    F<uint32_t>(kArray + 8) = Addr(kArrayElements);
    for (uint32_t i = 0; i < 4; ++i)
        F<uint32_t>(kArrayElements + 4 * i) = random.Below(4) == 0 ? 0u : 0x6E000000u + i;
}

// The structure InitFromStructure / InitModel read, by `kind`.
void SetParams(int kind, Lcg& random)
{
    void* params = At(kParams);
    if (random.Below(2))
        SetComponent(params, 0x10079F2D, 0); // UseModelLights
    if (random.Below(2))
        SetComponent(params, 0xD223B8BD, 2 + random.Below(3)); // Max_Scene_Lights
    if (random.Below(2))
        SetComponent(params, 0x2A1F92C0, random.Below(2)); // ShadowVolume
    if (random.Below(2))
        SetComponent(params, 0xC63C8D38, random.Below(2)); // use_asset_manager
    if (random.Below(2))
        SetComponent(params, 0xF891AC27, 1 + random.Below(9)); // texDictOffset
    switch (kind) {
    case 0:
        SetComponent(params, 0x12B4E660, 0xB7B3BD86); // Class=LevelObject
        SetComponent(params, 0xA1DC81F9, 0x5EC70000);   // Name
        break;
    case 1: SetComponent(params, 0x153A84DE, 0x7EF00042); break; // refObjectName
    case 2:
        SetComponent(params, 0x86BD5B8F, 0xA55E0001); // assetName
        if (random.Below(2))
            SetComponent(params, 0x92B43E79, random.Below(2)); // multicolor
        break;
    case 3:
        SetComponent(params, 0x7DD037B3, 0xC10E0001); // CloneFrom
        SetComponent(params, 0xE7308C1F, 0x6E000003); // GEOM
        break;
    case 4:
        SetComponent(params, 0x7DD037B3, 0xC10E0001);
        SetComponent(params, 0x44E31DFF, Addr(kArray)); // geoms
        break;
    case 5: SetComponent(params, 0x7DD037B3, 0xC10E0001); break;
    case 6:
    case 7:
        SetComponent(params, kind == 6 ? kModelType : 0xB974F2FC, reinterpret_cast<uint32_t>(
                                                                      random.Below(5) ? kFileName : kNone));
        if (random.Below(2))
            SetComponent(params, 0x09794932, 0x5CE1); // SkeletonName
        if (random.Below(2))
            SetComponent(params, 0x6C37FDC7, 0); // AllowReplaceTex
        break;
    case 8: SetComponent(params, 0x354BA8E9, 0xA55E0002); break;
    case 9: SetComponent(params, kModelType, 0x5EC70003); break; // (a sector by checksum)
    default: break;
    }
}

void TestModelComponentInit()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F0FB8, 4}, {0x007CC979, 1}});
    for (uint32_t seed = 1; seed <= 96; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x1A170000 + seed);
            g_a = {};
            g_a.haveSkeleton = random.Below(4) != 0;
            g_a.haveBlender = random.Below(2) != 0;
            g_a.haveSuspend = random.Below(2) != 0;
            g_a.blenderReady = random.Below(2) != 0;
            g_a.scale = random.Below(2) != 0;
            g_a.sector = random.Below(5) != 0;
            g_a.sectorGeom = random.Below(5) != 0;
            g_a.geomFound = random.Below(5) != 0;
            g_a.missingGeom = random.Below(3) == 0 ? 0x6E000002u + random.Below(2) : 0u;
            g_a.volumes = random.Below(3) == 0;
            g_a.volumesFound = random.Below(2) != 0;
            g_a.made = random.Below(6) != 0;
            g_a.boneVolumesOn = random.Below(2) != 0;
            g_a.sourceGeoms = static_cast<int>(random.Below(4));
            if (seed % 22 == 3)
                g_a.geomFound = false; // (CloneFrom's GEOM not there: nothing more)
            if (seed % 11 == 9)
                g_a.sector = true; // (a sector by checksum isn't checked: a crash in both without it)
            GameVariable<uint32_t>(0x006F0FB8) = 0xC0770000;
            GameVariable<uint8_t>(0x007CC979) = 1;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kInitAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            // Made and destroyed.
            SetUpWorld(seed, random);
            TestLifecycle(original, 0x00421490, 0x00420000, 0x00420270, kComponent, sizeof(CModelComponent),
                          g_a.calloc, trace);
            // (constructed on 0xFF bytes - every flag set -, destroyed with flags 2: not freed)
            std::memset(At(kComponent), 0xFF, sizeof(CModelComponent));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00420BF0))(At(kComponent));
            for (uint32_t i = 0; i < sizeof(CModelComponent); i += 4)
                trace.push_back(Rel(F<uint32_t>(kComponent + i)));
            reinterpret_cast<void*(__thiscall*)(void*, unsigned)>(Code(original, 0x00420270))(At(kComponent), 2);
            // InitFromStructure (InitModel) from a parameter set.
            SetUpWorld(seed, random);
            SetParams(static_cast<int>(seed % 11), random);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x004214F0))(At(kComponent), At(kParams));
            auto* c = reinterpret_cast<CModelComponent*>(At(kComponent));
            for (uint32_t i = 0; i < sizeof(CModelComponent); i += 4) // (what InitModel set)
                trace.push_back(Rel(F<uint32_t>(kComponent + i)));
            // Finalize, updates, teleport, another model, a profile, bone volumes, a sector.
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x004204B0))(c);
            c->m_vtable = g_componentVtable;
            for (int k = 0; k < 2; ++k) {
                c->m_levelObject = k == 0 && g_a.haveSkeleton && random.Below(3) == 0; // (without a skeleton: a crash in both)
                F<uint8_t>(kModel + 0x44) = static_cast<uint8_t>(random.Below(8));
                c->mp_skeleton = random.Below(3) ? reinterpret_cast<Obj::CBaseComponent*>(At(kSkeletonComponent))
                                                 : nullptr;
                c->mp_animBlender = random.Below(3) ? reinterpret_cast<Obj::CBaseComponent*>(At(kBlender)) : nullptr;
                c->mp_model = random.Below(6) ? reinterpret_cast<Nx::CModel*>(At(kModel)) : nullptr;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x004219F0))(c);
            }
            c->mp_model = random.Below(5) ? reinterpret_cast<Nx::CModel*>(At(kModel)) : nullptr;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00420480))(c);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00421360))(c, At(kModel));
            reinterpret_cast<void(__thiscall*)(void*, void*, bool, int, uint32_t)>(Code(original, 0x00421230))(
                c, At(kProfile), (seed & 1) != 0, static_cast<int>(seed % 5), seed % 3 == 0 ? 0u : 0xB5C10000 + seed);
            c->mp_skeleton = random.Below(3) ? reinterpret_cast<Obj::CBaseComponent*>(At(kSkeletonComponent)) : nullptr;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00420350))(c);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(Code(original, 0x0041FFA0))(c, 0x5EC70000 + seed);
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (uint32_t i = 0; i < 0x1800; i += 4)
                trace.push_back(Rel(F<uint32_t>(i)));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual,
                "model component: made, destroyed, InitFromStructure / InitModel, Finalize, Update, Teleport, SetModel, "
                "InitModelFromProfile, bone volumes, a sector's model");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestModelComponentInit, RemoveStandIns);
