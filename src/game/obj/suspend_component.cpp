// The suspend component and its task (game/obj/suspend_component.h).

#include "game/obj/suspend_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/gfx/engine.h"
#include "game/gfx/graphics_options.h"
#include "game/gfx/model.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/ass/assman.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(uint32_t[12], g_suspendComponentMemberFunctions, 0x00684C48,
            0xA0B7E09B, 0xCE0CA665, 0, 0, 0, 0,
            0, 0xCE0CA665, 0, 0, 0, 0);
GAME_GLOBAL(int, g_suspendFrameCount, 0x006951B0);
GAME_GLOBAL(int, g_suspendInterleave2Count, 0x006951B4);
GAME_GLOBAL(int, g_suspendInterleave4Count, 0x006951B8);
GAME_GLOBAL(SuspendTaskHolder*, g_suspendTaskHolder, 0x006951BC);

namespace {

void** const kVtable = reinterpret_cast<void**>(0x006465C8);
void** const kTaskVtable = reinterpret_cast<void**>(0x006476F0); // Tsk::Task<...>
constexpr int kTaskPriority = -1100;

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kDefaultSuspendDistance = 0xAD501995; // DefaultMovingObjectSuspendDistance
constexpr uint32_t kSuspendDistance = 0xE4279BA2;        // SuspendDistance
constexpr uint32_t kNeverSuspend = 0xCB839DC1;           // NeverSuspend
constexpr uint32_t kNoLod = 0x95EDC6F6;                  // NoLOD
constexpr uint32_t kLodDist[4] = {0x4EEDFAE7, 0xD7E4AB5D, 0xA0E39BCB, 0x3E870E68}; // lod_dist1-4

constexpr float kInchesPerFoot = 12.0f;     // 0x0064546C
constexpr float kLod2Beyond1 = 75.0f;       // 0x006465FC
constexpr int kCheckFrames = 10;
// The distances that mean no LOD (1000000 - 1000003): lod_dist3 at its no-LOD value means the model is always drawn.
const uint32_t kNoLodBits[4] = {0x49742400, 0x49742410, 0x49742420, 0x49742430};

int& FrameCount() { return g_suspendFrameCount; }
int& Interleave2Count() { return g_suspendInterleave2Count; }
int& Interleave4Count() { return g_suspendInterleave4Count; }

void SetNoLod(float* distances)
{
    for (int i = 0; i < 4; ++i)
        reinterpret_cast<uint32_t*>(distances)[i] = kNoLodBits[i];
}

// The model component's model (+0x208) and what it's asked (not rewritten yet): drawn or not (0x00493C60), its
// bounding sphere (0x00492F30: centre, radius in w), and whether it wants to be drawn regardless (+0x234).
uint8_t* ModelOf(CBaseComponent* component) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208); }
void SetModelActive(uint8_t* model, bool active) { reinterpret_cast<Nx::CModel*>(model)->SetActive(active); }

} // namespace

// 0x00434290
CBaseComponent* __cdecl CSuspendComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSuspendComponent));
    return memory ? static_cast<CSuspendComponent*>(memory)->Construct() : nullptr;
}

// 0x00434610
void __cdecl CSuspendComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_suspendComponentMemberFunctions, 0x18);
    CreateSuspendTask();
}

// 0x00433F50
CSuspendComponent* CSuspendComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    double distance = static_cast<double>(Script::GetFloat(kDefaultSuspendDistance)) * kInchesPerFoot;
    m_suspendDistance = static_cast<float>(distance);
    m_neverSuspend = true;
    m_framesBeforeCheck = kCheckFrames;
    m_framesBeforeLod = 3;
    m_suspendDistanceSquared = static_cast<float>(distance * distance);
    return this;
}

// 0x00433E10
void CSuspendComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00433FD0
CSuspendComponent* CSuspendComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00433E50
void CSuspendComponent::Update() { CBaseComponent::Suspend(true); }

// 0x00434000
void CSuspendComponent::InitFromStructure(Script::CStruct* params)
{
    if (params->GetFloat(kSuspendDistance, &m_suspendDistance, 0)) {
        if (m_suspendDistance == 0.0f && !params->GetFloat(kLodDist[0], &m_suspendDistance, 0))
            m_suspendDistance = Script::GetFloat(kDefaultSuspendDistance);
        m_neverSuspend = false;
        double distance = static_cast<double>(m_suspendDistance) * kInchesPerFoot;
        m_suspendDistance = static_cast<float>(distance);
        m_suspendDistanceSquared = static_cast<float>(distance * distance);
    }
    if (params->ContainsFlag(kNeverSuspend))
        m_neverSuspend = true;
    if (Script::GetInteger(kNoLod)) {
        SetNoLod(m_lodDistances);
        return;
    }
    float* lod = m_lodDistances;
    lod[0] = 50.0f;
    reinterpret_cast<uint32_t*>(lod)[2] = kNoLodBits[1];
    reinterpret_cast<uint32_t*>(lod)[3] = kNoLodBits[2];
    params->GetFloat(kLodDist[0], &lod[0], 0);
    lod[1] = lod[0] + kLod2Beyond1;
    params->GetFloat(kLodDist[1], &lod[1], 0);
    params->GetFloat(kLodDist[2], &lod[2], 0);
    params->GetFloat(kLodDist[3], &lod[3], 0);
    double first = static_cast<double>(lod[0]) * kInchesPerFoot, second = static_cast<double>(lod[1]) * kInchesPerFoot;
    lod[0] = static_cast<float>(first);
    lod[1] = static_cast<float>(second);
    lod[2] = lod[2] * kInchesPerFoot;
    lod[3] = lod[3] * kInchesPerFoot;
    // Distances out of order are reported (compiled out of this build).
    if (!(first < second))
        Script::PrintContents(params, 0);
    if (!(lod[1] < lod[2]))
        Script::PrintContents(params, 0);
    if (!(lod[2] < lod[3]))
        Script::PrintContents(params, 0);
    lod[0] = Script::GetFloat(0xD3D072F9) * lod[0]; // LOD0DistanceMultiplier
    lod[1] = Script::GetFloat(0x0432F2A1) * lod[1]; // LOD1DistanceMultiplier
    // NOT ORIGINAL - [Graphics] lod_distance_scale (game/gfx/graphics_options.h): animated and drawn further out.
    float scale = Nx::GraphicsOptions::Get().lodDistanceScale;
    lod[0] = Nx::GraphicsOptions::Scaled(lod[0], scale);
    lod[1] = Nx::GraphicsOptions::Scaled(lod[1], scale);
}

// 0x004341C0
void CSuspendComponent::Finalize() { mp_model = m_object->GetComponent(kModel); }

// 0x00433E60
void CSuspendComponent::ResetCounts()
{
    m_framesBeforeLod = kCheckFrames;
    m_framesBeforeCheck = kCheckFrames;
}

// 0x00433E70
void CSuspendComponent::DisableLod() { SetNoLod(m_lodDistances); }

// 0x00433E90
bool CSuspendComponent::IsFar() { return m_far; }

// 0x00433EA0
bool CSuspendComponent::ShouldAnimate(float* distanceSquared)
{
    const float d = m_distanceSquared;
    if (distanceSquared)
        *distanceSquared = d;
    bool animate = true;
    double lod = m_lodDistances[0];
    if (d > lod * lod) { // (not a number: as within)
        animate = false;
    } else {
        float interleave2 = Script::GetFloat(0xDB120FB5); // AnimLODInterleave2
        double interleave4 = Script::GetFloat(0x3271AA80); // AnimLODInterleave4
        float within2 = static_cast<float>(static_cast<double>(interleave2) * interleave2);
        if (d > interleave4 * interleave4)
            animate = (++Interleave4Count() & 3) == 0;
        else if (d > within2)
            animate = (++Interleave2Count() & 1) == 0;
    }
    return m_framesBeforeLod != 0 ? true : animate;
}

// 0x004342F0
void CSuspendComponent::UpdateVisibility()
{
    if (!mp_model || !ModelOf(mp_model))
        return;
    bool visible = false;
    if (!(m_object->m_objectFlags & OBJECT_FLAG_INVISIBLE)) {
        uint8_t* model = ModelOf(mp_model);
        if (reinterpret_cast<const uint32_t*>(m_lodDistances)[2] == kNoLodBits[2]) {
            visible = true;
        } else if (Mdl::g_viewportCount > 1) {
            visible = true;
        } else if (reinterpret_cast<uint8_t*>(mp_model)[0x234]) { // (the model component's)
            visible = true;
        } else {
            double lod = m_lodDistances[1];
            if (m_distanceSquared < lod * lod) {
                // In view: its bounding sphere where the object is.
                float sphere[4];
                reinterpret_cast<Nx::CModel*>(model)->GetBoundingSphere(reinterpret_cast<Mth::Vector*>(sphere));
                Mth::Vector centre = {sphere[0], sphere[1], sphere[2], 1.0f};
                centre.MultiplyBy(m_object->m_matrix);
                centre += m_object->m_pos;
                visible = Nx::Engine::SphereVisibleInOneViewport(&centre, sphere[3]) == 0;
            }
        }
    }
    uint8_t flags = ModelOf(mp_model)[0x44];
    bool shown = (flags & 0x2) && !(flags & 0x4);
    SetModelActive(ModelOf(mp_model), visible);
    if (visible && !shown)
        mp_model->Update();
}

// 0x004341E0
bool CSuspendComponent::Obj_ResetNoSuspendCount(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    m_framesBeforeLod = kCheckFrames;
    m_framesBeforeCheck = kCheckFrames;
    m_object->Suspend(false);
    return true;
}

// 0x00433E20
void __cdecl NextSuspendFrame()
{
    int previous = FrameCount();
    int frame = previous + 1;
    FrameCount() = frame;
    Interleave2Count() = frame;
    Interleave4Count() = (((previous >> 1) & 1) + 1) ^ frame;
}

// 0x00434520
SuspendTaskHolder* SuspendTaskHolder::Construct()
{
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (task) {
        task->Construct(kTaskPriority);
        task->m_vtable = kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&SuspendTask); // +0x18 the callback
        reinterpret_cast<void**>(task)[7] = this;                                  // +0x1C the data
    }
    mp_task = task;
    reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 0x04)->AddTask(task); // the logic tasks
    return this;
}

// 0x004345B0
void __cdecl CreateSuspendTask()
{
    void* memory = Mem::Calloc(sizeof(SuspendTaskHolder));
    g_suspendTaskHolder = memory ? static_cast<SuspendTaskHolder*>(memory)->Construct() : nullptr;
}

// 0x00434420
void __cdecl SuspendTask(void* /*task*/)
{
    // The camera the distances are measured from: the skate module's +0x3DC (a flag that turns the checks off, then
    // the position).
    const uint8_t* camera = reinterpret_cast<uint8_t*>(Mdl::g_skateModule) + 0x3DC;
    auto* component =
        static_cast<CSuspendComponent*>(CompositeObjectManager()->GetFirstComponentByType(CSuspendComponent::kType));
    while (component) {
        component->m_distanceSquared = 0.0f;
        component->m_far = false;
        if (component->m_framesBeforeCheck) {
            // (the original goes round again with the same component: its frames all counted down at once)
            --component->m_framesBeforeCheck;
            continue;
        }
        if (!camera[0] && !component->m_neverSuspend) {
            if (component->m_framesBeforeLod) {
                --component->m_framesBeforeLod;
            } else {
                const Mth::Vector& pos = component->m_object->m_pos;
                const float* at = reinterpret_cast<const float*>(camera + 4);
                double dx = static_cast<double>(pos.x) - at[0], dy = static_cast<double>(pos.y) - at[1],
                       dz = static_cast<double>(pos.z) - at[2];
                double distanceSquared = (dz * dz + dy * dy) + dx * dx;
                component->m_distanceSquared = static_cast<float>(distanceSquared);
                component->m_far = distanceSquared > component->m_suspendDistanceSquared;
            }
        }
        CCompositeObject* object = component->m_object;
        bool suspended = (object->m_compositeFlags & COMPOSITE_FLAG_SUSPENDED) != 0;
        if (component->m_far && !suspended)
            object->Suspend(true);
        else if (!component->m_far && suspended)
            object->Suspend(false);
        if (CBaseComponent* model = component->mp_model) {
            if (component->m_framesBeforeLod) {
                if (ModelOf(model))
                    SetModelActive(ModelOf(model), true);
            } else {
                component->UpdateVisibility();
            }
        }
        component = static_cast<CSuspendComponent*>(component->mp_nextOfType);
    }
}

} // namespace Obj

REPLACE(0x00434290, Obj::CSuspendComponent::Create);
REPLACE(0x00434610, Obj::CSuspendComponent::RegisterMemberFunctions);
REPLACE(0x00433F50, &Obj::CSuspendComponent::Construct);
REPLACE(0x00433E10, &Obj::CSuspendComponent::DestroyBody);
REPLACE(0x00433FD0, &Obj::CSuspendComponent::Destroy);
REPLACE(0x00433E50, &Obj::CSuspendComponent::Update);
REPLACE(0x00434000, &Obj::CSuspendComponent::InitFromStructure);
REPLACE(0x004341C0, &Obj::CSuspendComponent::Finalize);
REPLACE(0x00433E60, &Obj::CSuspendComponent::ResetCounts);
REPLACE(0x00433E70, &Obj::CSuspendComponent::DisableLod);
REPLACE_SOURCE_ONLY(0x00433E90, &Obj::CSuspendComponent::IsFar); // 4 bytes
REPLACE(0x00433EA0, &Obj::CSuspendComponent::ShouldAnimate);
REPLACE(0x004342F0, &Obj::CSuspendComponent::UpdateVisibility);
REPLACE(0x004341E0, &Obj::CSuspendComponent::Obj_ResetNoSuspendCount);
REPLACE(0x00433E20, Obj::NextSuspendFrame);
REPLACE(0x00434520, &Obj::SuspendTaskHolder::Construct);
REPLACE(0x004345B0, Obj::CreateSuspendTask);
REPLACE(0x00434420, Obj::SuspendTask);
