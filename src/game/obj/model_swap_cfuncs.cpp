// Script commands (CFunctions) 0x005B34A0-0x005B3A90: swapping objects' models (see model_swap_cfuncs.h).

#include "game/obj/model_swap_cfuncs.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/model.h"
#include "game/gfx/rig.h"
#include "game/obj/compositemgr.h"
#include "game/obj/model_component.h"
#include "game/obj/entity.h"
#include "game/obj/shadow_component.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_cfuncs.h"
#include "game/skate/skate_module.h"
#include "game/skater/rider.h"
#include "game/core/code_address.h"

namespace ScriptCommands {

namespace {

using GameObjects::CGameEntity;
using Scripts::CStruct;

constexpr uint32_t kObj1 = 0x9A2DD8A1;              // "obj_1"
constexpr uint32_t kObj2 = 0x0324891B;              // "obj_2"
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29; // "SkaterCorePhysics"
constexpr uint32_t kModel = 0x286A8D26;             // "model"
constexpr uint32_t kShadow = 0x8A897DD2;            // "shadow"
constexpr uint32_t kSkeleton = 0x222756D5;          // "skeleton"
constexpr uint32_t kSuspend = 0xCE0CA665;           // "suspend"
constexpr uint32_t kSkater = 0x5B8AB877;            // "Skater"
const char* const kPedShadow = "Ped_Shadow"; // "Ped_Shadow"

// The swaps made (0x007CEDE0, five entries): each object's id and the id of the object whose model it wears now.
struct SwapEntry {
    bool used;
    uint8_t pad1[3];
    uint32_t id;
    uint32_t current;
};
static_assert(sizeof(SwapEntry) == 12, "layout must match the original");
static_assert(sizeof(ScriptCommands::SwapSlot) == sizeof(SwapEntry), "one table, two views");
SwapEntry* Swaps() { return reinterpret_cast<SwapEntry*>(ScriptCommands::g_swapTable); } // (profile_cfuncs.cpp owns it)
constexpr int kSwapCount = 5;

// 0x005AA930 (ScriptCommands::SwapMapping, profile_cfuncs.cpp): the swap of the objects `a` and `b` recorded in the table.
void RecordSwap(uint32_t a, uint32_t b) { ScriptCommands::SwapMapping(a, b); }

// A skeleton (the skeleton component's +0x18): 0x004A3D30 constructs one in `memory` from the asset at another's +8,
// 0x004A2E20 copies one into another, 0x004A2CB0 is its destructor body.
uint8_t* SkeletonOf(GameObjects::CObjectModule* component) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x18); }
void CopySkeleton(uint8_t* to, uint8_t* from)
{
    reinterpret_cast<Gfx2D::CRig*>(to)->CopyBoneScales(reinterpret_cast<Gfx2D::CRig*>(from), true);
}

// A model's +0xC (0x00492E50 / 0x00493460: the setter tells the model's parts) and its lights (+0x2C), whose
// position follows a pointer (0x0048E4D0).
uint32_t ModelGet(Engine3D::CRenderModel* model) { return reinterpret_cast<uint32_t(__thiscall*)(Engine3D::CRenderModel*)>(CodeAddress(&Engine3D::CRenderModel::GetBoneMatrices))(model); }
void ModelSet(Engine3D::CRenderModel* model, uint32_t value)
{
    model->SetBoneMatrices(reinterpret_cast<uint8_t*>(value));
}
void FollowPosition(Engine3D::CRenderModel* model, Maths::Vec4* position)
{
    void* lights = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C);
    static_cast<Engine3D::CModelLights*>(lights)->BindPositionSource(position);
}

// The model component's ChangeMesh through its vtable (slot 14).
void SetModelVirtual(GameObjects::CMeshInstanceModule* component, Engine3D::CRenderModel* model)
{
    auto** vtable = *reinterpret_cast<void***>(component);
    reinterpret_cast<void(__thiscall*)(GameObjects::CMeshInstanceModule*, Engine3D::CRenderModel*)>(vtable[14])(component, model);
}

// An object's shadow made again: a skater's in its skater heap (type 1), anything else's type 2.
void RemakeShadow(CGameEntity* object, GameObjects::CGroundShadowModule* shadow, GameObjects::CRider* skater)
{
    if (object->FetchModule(kSkaterCorePhysics)) {
        MemSys::Hub* manager = MemSys::Hub::Pull();
        manager->EnterHeapScope(manager->m_skaterHeaps[skater->GetSkaterNumber()]);
        shadow->CreateShadow(1, kPedShadow);
        MemSys::Hub::Pull()->LeaveHeapScope();
    } else {
        shadow->CreateShadow(2, kPedShadow);
    }
}

} // namespace

// 0x005B34A0: SwapObjectModels obj_1=<id> obj_2=<id>: the two objects (a skater among them, if either has skater
// core physics - the second if both) swap their renderable models, skeletons (through a copy made in the top-down
// heap) and shadows; the swap is recorded; a skater's model-swapped value (+0x31C) is the id of an entry whose object
// wears no other model now (0 if none). The objects are asked for core physics before they're checked for null, as in
// the original. False if either is missing, they're the same object or either has no model.
bool __cdecl CFunc_SwapObjectModels(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = nullptr;
    uint32_t id1;
    params->ReadHashValue(kObj1, &id1, 1);
    uint32_t id2;
    params->ReadHashValue(kObj2, &id2, 1);
    auto* a = static_cast<CGameEntity*>(GameObjects::CompositeObjectManager()->LookUpEntity(id1));
    auto* b = static_cast<CGameEntity*>(GameObjects::CompositeObjectManager()->LookUpEntity(id2));
    if (a->FetchModule(kSkaterCorePhysics))
        skater = static_cast<GameObjects::CRider*>(a);
    if (b->FetchModule(kSkaterCorePhysics))
        skater = static_cast<GameObjects::CRider*>(b);
    if (!a || !b || a == b)
        return false;
    RecordSwap(a->m_ident, b->m_ident);

    auto* modelA = static_cast<GameObjects::CMeshInstanceModule*>(a->FetchModule(kModel));
    auto* modelB = static_cast<GameObjects::CMeshInstanceModule*>(b->FetchModule(kModel));
    auto* shadowA = static_cast<GameObjects::CGroundShadowModule*>(a->FetchModule(kShadow));
    auto* shadowB = static_cast<GameObjects::CGroundShadowModule*>(b->FetchModule(kShadow));
    if (!modelA || !modelB)
        return false;
    shadowA->DestroyShadow();
    shadowB->DestroyShadow();

    // the skeletons swapped through a copy
    uint8_t* skeletonA = SkeletonOf(a->FetchModule(kSkeleton));
    uint8_t* skeletonB = SkeletonOf(b->FetchModule(kSkeleton));
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    void* memory = MemSys::Alloc(0x1C);
    uint8_t* temp = memory ? reinterpret_cast<uint8_t*>(static_cast<Gfx2D::CRig*>(memory)->MakeNew(
                                 *reinterpret_cast<Gfx2D::CRigData**>(skeletonA + 8)))
                           : nullptr;
    MemSys::Hub::Pull()->LeaveHeapScope();
    CopySkeleton(temp, skeletonA);
    CopySkeleton(skeletonA, skeletonB);
    CopySkeleton(skeletonB, temp);
    if (temp) {
        reinterpret_cast<Gfx2D::CRig*>(temp)->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(temp);
    }

    // the renderable models swapped
    Engine3D::CRenderModel* nxA = modelA->m_meshInstance;
    Engine3D::CRenderModel* nxB = modelB->m_meshInstance;
    uint32_t valueA = ModelGet(nxA);
    ModelSet(nxA, ModelGet(nxB));
    ModelSet(nxB, valueA);
    SetModelVirtual(modelA, nxB);
    SetModelVirtual(modelB, nxA);

    auto* suspendA = static_cast<GameObjects::CDormancyModule*>(a->FetchModule(kSuspend));
    auto* suspendB = static_cast<GameObjects::CDormancyModule*>(b->FetchModule(kSuspend));
    suspendA->ResetCounts();
    suspendB->ResetCounts();
    a->HoldOff(false);
    b->HoldOff(false);

    if (skater) {
        // (each component's model as it is now)
        FollowPosition(modelA->m_meshInstance, &a->m_atPoint);
        FollowPosition(modelB->m_meshInstance, &b->m_atPoint);
        bool found = false;
        for (int i = 0; i < kSwapCount; ++i) {
            if (Swaps()[i].used && Swaps()[i].current == 0) {
                skater->m_unknown31C = Swaps()[i].id;
                found = true;
            }
        }
        if (!found)
            skater->m_unknown31C = 0;
    }
    RemakeShadow(a, shadowA, skater);
    RemakeShadow(b, shadowB, skater);
    return true;
}

// 0x005B3890: RestoreAllSwappedObjectModels: each recorded swap undone (SwapObjectModels on the entry's object and the
// one whose model it wears), the table gone over again while it still holds swaps - at most as many times as there
// were. Always true.
bool __cdecl CFunc_RestoreAllSwappedObjectModels(Scripts::CStruct*, Scripts::CScript* script)
{
    int remaining = 0;
    for (int i = 0; i < kSwapCount; ++i)
        if (Swaps()[i].used)
            ++remaining;
    if (!remaining)
        return true;
    for (;;) {
        bool any = false;
        for (int i = 0; i < kSwapCount; ++i) {
            SwapEntry& entry = Swaps()[i];
            if (!entry.used)
                continue;
            any = true;
            CStruct* memory = Scripts::NewStruct();
            CStruct* swap = memory ? memory->PrepareAll() : nullptr;
            swap->PutNameKey(kObj1, entry.id);
            swap->PutNameKey(kObj2, entry.current);
            CFunc_SwapObjectModels(swap, script);
            if (swap) {
                // (delete inlined: the structure pool pair's Free)
                swap->Clear();
                Scripts::g_structPools.Dealloc(swap);
            }
        }
        if (!any || --remaining == 0)
            break;
    }
    return true;
}

// 0x005B3A30: GetSkaterModelIsSwapped [Skater=n]: whether skater n (or the local skater) wears another model.
bool __cdecl CFunc_GetSkaterModelIsSwapped(Scripts::CStruct* params, Scripts::CScript*)
{
    int index;
    GameObjects::CRider* skater;
    if (params->FetchIntValue(kSkater, &index, 0))
        skater = static_cast<GameObjects::CRider*>(GameModules::Skate::Sole()->GetRiderAt(index));
    else
        skater = GameModules::Skate::Sole()->GetHomeRider();
    return skater->m_unknown31C != 0;
}

} // namespace ScriptCommands
