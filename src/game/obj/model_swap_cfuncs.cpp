// Script commands (CFunctions) 0x005B34A0-0x005B3A90: swapping objects' models (see model_swap_cfuncs.h).

#include "game/obj/model_swap_cfuncs.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/model.h"
#include "game/gfx/skeleton.h"
#include "game/obj/compositemgr.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/shadow_component.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_cfuncs.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Obj::CCompositeObject;
using Script::CStruct;

constexpr uint32_t kObj1 = 0x9A2DD8A1;              // "obj_1"
constexpr uint32_t kObj2 = 0x0324891B;              // "obj_2"
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29; // "SkaterCorePhysics"
constexpr uint32_t kModel = 0x286A8D26;             // "model"
constexpr uint32_t kShadow = 0x8A897DD2;            // "shadow"
constexpr uint32_t kSkeleton = 0x222756D5;          // "skeleton"
constexpr uint32_t kSuspend = 0xCE0CA665;           // "suspend"
constexpr uint32_t kSkater = 0x5B8AB877;            // "Skater"
const char* const kPedShadow = reinterpret_cast<const char*>(0x006464CC); // "Ped_Shadow"

// The swaps made (0x007CEDE0, five entries): each object's id and the id of the object whose model it wears now.
struct SwapEntry {
    bool used;
    uint8_t pad1[3];
    uint32_t id;
    uint32_t current;
};
static_assert(sizeof(SwapEntry) == 12, "layout must match the original");
GAME_GLOBAL(SwapEntry[5], g_modelSwaps, 0x007CEDE0);
SwapEntry* Swaps() { return g_modelSwaps; }
constexpr int kSwapCount = 5;

// 0x005AA930 (CFuncs::SwapMapping, profile_cfuncs.cpp): the swap of the objects `a` and `b` recorded in the table.
void RecordSwap(uint32_t a, uint32_t b) { CFuncs::SwapMapping(a, b); }

// A skeleton (the skeleton component's +0x18): 0x004A3D30 constructs one in `memory` from the asset at another's +8,
// 0x004A2E20 copies one into another, 0x004A2CB0 is its destructor body.
uint8_t* SkeletonOf(Obj::CBaseComponent* component) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x18); }
void CopySkeleton(uint8_t* to, uint8_t* from)
{
    reinterpret_cast<Gfx::CSkeleton*>(to)->CopyBoneScales(reinterpret_cast<Gfx::CSkeleton*>(from), true);
}

// A model's +0xC (0x00492E50 / 0x00493460: the setter tells the model's parts) and its lights (+0x2C), whose
// position follows a pointer (0x0048E4D0).
uint32_t ModelGet(Nx::CModel* model) { return GameFunction<uint32_t(__thiscall*)(Nx::CModel*)>(0x00492E50)(model); }
void ModelSet(Nx::CModel* model, uint32_t value)
{
    model->SetBoneMatrices(reinterpret_cast<uint8_t*>(value));
}
void FollowPosition(Nx::CModel* model, Mth::Vector* position)
{
    void* lights = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C);
    static_cast<Nx::CModelLights*>(lights)->SetPositionPointer(position);
}

// The model component's SetModel through its vtable (slot 14).
void SetModelVirtual(Obj::CModelComponent* component, Nx::CModel* model)
{
    auto** vtable = *reinterpret_cast<void***>(component);
    reinterpret_cast<void(__thiscall*)(Obj::CModelComponent*, Nx::CModel*)>(vtable[14])(component, model);
}

// An object's shadow made again: a skater's in its skater heap (type 1), anything else's type 2.
void RemakeShadow(CCompositeObject* object, Obj::CShadowComponent* shadow, Obj::CSkater* skater)
{
    if (object->GetComponent(kSkaterCorePhysics)) {
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_skaterHeaps[skater->GetSkaterNumber()]);
        shadow->CreateShadow(1, kPedShadow);
        Mem::Manager::Get()->PopContext();
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
bool __cdecl CFunc_SwapObjectModels(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkater* skater = nullptr;
    uint32_t id1;
    params->GetChecksum(kObj1, &id1, 1);
    uint32_t id2;
    params->GetChecksum(kObj2, &id2, 1);
    auto* a = static_cast<CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(id1));
    auto* b = static_cast<CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(id2));
    if (a->GetComponent(kSkaterCorePhysics))
        skater = static_cast<Obj::CSkater*>(a);
    if (b->GetComponent(kSkaterCorePhysics))
        skater = static_cast<Obj::CSkater*>(b);
    if (!a || !b || a == b)
        return false;
    RecordSwap(a->m_id, b->m_id);

    auto* modelA = static_cast<Obj::CModelComponent*>(a->GetComponent(kModel));
    auto* modelB = static_cast<Obj::CModelComponent*>(b->GetComponent(kModel));
    auto* shadowA = static_cast<Obj::CShadowComponent*>(a->GetComponent(kShadow));
    auto* shadowB = static_cast<Obj::CShadowComponent*>(b->GetComponent(kShadow));
    if (!modelA || !modelB)
        return false;
    shadowA->DestroyShadow();
    shadowB->DestroyShadow();

    // the skeletons swapped through a copy
    uint8_t* skeletonA = SkeletonOf(a->GetComponent(kSkeleton));
    uint8_t* skeletonB = SkeletonOf(b->GetComponent(kSkeleton));
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    void* memory = Mem::New(0x1C);
    uint8_t* temp = memory ? reinterpret_cast<uint8_t*>(static_cast<Gfx::CSkeleton*>(memory)->Construct(
                                 *reinterpret_cast<Gfx::CSkeletonData**>(skeletonA + 8)))
                           : nullptr;
    Mem::Manager::Get()->PopContext();
    CopySkeleton(temp, skeletonA);
    CopySkeleton(skeletonA, skeletonB);
    CopySkeleton(skeletonB, temp);
    if (temp) {
        reinterpret_cast<Gfx::CSkeleton*>(temp)->DestroyBody();
        Mem::Manager::Get()->Free(temp);
    }

    // the renderable models swapped
    Nx::CModel* nxA = modelA->mp_model;
    Nx::CModel* nxB = modelB->mp_model;
    uint32_t valueA = ModelGet(nxA);
    ModelSet(nxA, ModelGet(nxB));
    ModelSet(nxB, valueA);
    SetModelVirtual(modelA, nxB);
    SetModelVirtual(modelB, nxA);

    auto* suspendA = static_cast<Obj::CSuspendComponent*>(a->GetComponent(kSuspend));
    auto* suspendB = static_cast<Obj::CSuspendComponent*>(b->GetComponent(kSuspend));
    suspendA->ResetCounts();
    suspendB->ResetCounts();
    a->Suspend(false);
    b->Suspend(false);

    if (skater) {
        // (each component's model as it is now)
        FollowPosition(modelA->mp_model, &a->m_pos);
        FollowPosition(modelB->mp_model, &b->m_pos);
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
bool __cdecl CFunc_RestoreAllSwappedObjectModels(Script::CStruct*, Script::CScript* script)
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
            CStruct* memory = Script::NewStruct();
            CStruct* swap = memory ? memory->Init() : nullptr;
            swap->AddChecksum(kObj1, entry.id);
            swap->AddChecksum(kObj2, entry.current);
            CFunc_SwapObjectModels(swap, script);
            if (swap) {
                // (delete inlined: the structure pool pair's Free)
                swap->Clear();
                GameVariable<Mem::PoolPair>(0x006F3614).Free(swap);
            }
        }
        if (!any || --remaining == 0)
            break;
    }
    return true;
}

// 0x005B3A30: GetSkaterModelIsSwapped [Skater=n]: whether skater n (or the local skater) wears another model.
bool __cdecl CFunc_GetSkaterModelIsSwapped(Script::CStruct* params, Script::CScript*)
{
    int index;
    Obj::CSkater* skater;
    if (params->GetInteger(kSkater, &index, 0))
        skater = static_cast<Obj::CSkater*>(Mdl::Skate::Instance()->GetSkater(index));
    else
        skater = Mdl::Skate::Instance()->GetLocalSkater();
    return skater->m_unknown31C != 0;
}

} // namespace CFuncs

REPLACE(0x005B34A0, CFuncs::CFunc_SwapObjectModels);
REPLACE(0x005B3890, CFuncs::CFunc_RestoreAllSwappedObjectModels);
REPLACE(0x005B3A30, CFuncs::CFunc_GetSkaterModelIsSwapped);
