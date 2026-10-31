// The suspend component and its task (game/obj/suspend_component.h).

#include "game/obj/suspend_component.h"
#include "game/net/task_vtable.h"

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
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_suspendComponentMemberFunctions[12]{
            0xA0B7E09B, 0xCE0CA665, 0, 0, 0, 0,
            0, 0xCE0CA665, 0, 0, 0, 0};
int g_suspendFrameCount;
int g_suspendInterleave2Count;
int g_suspendInterleave4Count;
SuspendTaskHolder* g_suspendTaskHolder;

namespace {

extern void** const kVtable;
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
uint8_t* ModelOf(CObjectModule* component) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208); }
void SetModelActive(uint8_t* model, bool active) { reinterpret_cast<Engine3D::CRenderModel*>(model)->SetActive(active); }

} // namespace

// 0x00434290
CObjectModule* __cdecl CDormancyModule::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CDormancyModule));
    return memory ? static_cast<CDormancyModule*>(memory)->Construct() : nullptr;
}

// 0x00434610
void __cdecl CDormancyModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_suspendComponentMemberFunctions, 0x18);
    CreateSuspendTask();
}

// 0x00433F50
CDormancyModule* CDormancyModule::Construct()
{
    CObjectModule::Construct();
    m_vtable = kVtable;
    m_type = kType;
    double distance = static_cast<double>(Scripts::GetFloat(kDefaultSuspendDistance)) * kInchesPerFoot;
    m_suspendDistance = static_cast<float>(distance);
    m_neverSuspend = true;
    m_framesBeforeCheck = kCheckFrames;
    m_framesBeforeLod = 3;
    m_suspendDistanceSquared = static_cast<float>(distance * distance);
    return this;
}

// 0x00433E10
void CDormancyModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00433FD0
CDormancyModule* CDormancyModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00433E50
void CDormancyModule::Update() { CObjectModule::Suspend(true); }

// 0x00434000
void CDormancyModule::ConfigureFromData(Scripts::CStruct* params)
{
    if (params->GetFloat(kSuspendDistance, &m_suspendDistance, 0)) {
        if (m_suspendDistance == 0.0f && !params->GetFloat(kLodDist[0], &m_suspendDistance, 0))
            m_suspendDistance = Scripts::GetFloat(kDefaultSuspendDistance);
        m_neverSuspend = false;
        double distance = static_cast<double>(m_suspendDistance) * kInchesPerFoot;
        m_suspendDistance = static_cast<float>(distance);
        m_suspendDistanceSquared = static_cast<float>(distance * distance);
    }
    if (params->HasBareFlag(kNeverSuspend))
        m_neverSuspend = true;
    if (Scripts::FetchIntValue(kNoLod)) {
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
        Scripts::DumpToLog(params, 0);
    if (!(lod[1] < lod[2]))
        Scripts::DumpToLog(params, 0);
    if (!(lod[2] < lod[3]))
        Scripts::DumpToLog(params, 0);
    lod[0] = Scripts::GetFloat(0xD3D072F9) * lod[0]; // LOD0DistanceMultiplier
    lod[1] = Scripts::GetFloat(0x0432F2A1) * lod[1]; // LOD1DistanceMultiplier
    // NOT ORIGINAL - [Graphics] lod_distance_scale (game/gfx/graphics_options.h): animated and drawn further out.
    float scale = Engine3D::GraphicsOptions::Get().lodDistanceScale;
    lod[0] = Engine3D::GraphicsOptions::Scaled(lod[0], scale);
    lod[1] = Engine3D::GraphicsOptions::Scaled(lod[1], scale);
}

// 0x004341C0
void CDormancyModule::Finalize() { m_meshInstance = m_object->GetComponent(kModel); }

// 0x00433E60
void CDormancyModule::ResetCounts()
{
    m_framesBeforeLod = kCheckFrames;
    m_framesBeforeCheck = kCheckFrames;
}

// 0x00433E70
void CDormancyModule::DisableLod() { SetNoLod(m_lodDistances); }

// 0x00433E90
bool CDormancyModule::IsFar() { return m_far; }

// 0x00433EA0
bool CDormancyModule::WantsAnimating(float* distanceSquared)
{
    const float d = m_distanceSquared;
    if (distanceSquared)
        *distanceSquared = d;
    bool animate = true;
    double lod = m_lodDistances[0];
    if (d > lod * lod) { // (not a number: as within)
        animate = false;
    } else {
        float skipEvery2 = Scripts::GetFloat(0xDB120FB5); // AnimLODInterleave2
        double skipEvery4 = Scripts::GetFloat(0x3271AA80); // AnimLODInterleave4
        float within2 = static_cast<float>(static_cast<double>(skipEvery2) * skipEvery2);
        if (d > skipEvery4 * skipEvery4)
            animate = (++Interleave4Count() & 3) == 0;
        else if (d > within2)
            animate = (++Interleave2Count() & 1) == 0;
    }
    return m_framesBeforeLod != 0 ? true : animate;
}

// 0x004342F0
void CDormancyModule::UpdateVisibility()
{
    if (!m_meshInstance || !ModelOf(m_meshInstance))
        return;
    bool visible = false;
    if (!(m_object->m_objectFlags & ENTITY_HIDDEN_BIT)) {
        uint8_t* model = ModelOf(m_meshInstance);
        if (reinterpret_cast<const uint32_t*>(m_lodDistances)[2] == kNoLodBits[2]) {
            visible = true;
        } else if (GameModules::g_viewportCount > 1) {
            visible = true;
        } else if (reinterpret_cast<uint8_t*>(m_meshInstance)[0x234]) { // (the model component's)
            visible = true;
        } else {
            double lod = m_lodDistances[1];
            if (m_distanceSquared < lod * lod) {
                // In view: its bounding sphere where the object is.
                float sphere[4];
                reinterpret_cast<Engine3D::CRenderModel*>(model)->FetchBoundSphere(reinterpret_cast<Maths::Vector*>(sphere));
                Maths::Vector centre = {sphere[0], sphere[1], sphere[2], 1.0f};
                centre.MultiplyBy(m_object->m_basisFrame);
                centre += m_object->m_pos;
                visible = Engine3D::Engine::SphereVisibleInOneViewport(&centre, sphere[3]) == 0;
            }
        }
    }
    uint8_t flags = ModelOf(m_meshInstance)[0x44];
    bool shown = (flags & 0x2) && !(flags & 0x4);
    SetModelActive(ModelOf(m_meshInstance), visible);
    if (visible && !shown)
        m_meshInstance->Update();
}

// 0x004341E0
bool CDormancyModule::Obj_ResetNoSuspendCount(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
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
    auto* task = static_cast<TaskList::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (task) {
        task->Construct(kTaskPriority);
        task->m_vtable = GameCode::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&SuspendTask); // +0x18 the callback
        reinterpret_cast<void**>(task)[7] = this;                                  // +0x1C the data
    }
    m_jobHandle = task;
    reinterpret_cast<TaskList::Stack*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04)->AddTask(task); // the logic tasks
    return this;
}

// 0x004345B0
void __cdecl CreateSuspendTask()
{
    void* memory = MemSys::AllocZeroed(sizeof(SuspendTaskHolder));
    g_suspendTaskHolder = memory ? static_cast<SuspendTaskHolder*>(memory)->Construct() : nullptr;
}

// 0x00434420
void __cdecl SuspendTask(void* /*task*/)
{
    // The camera the distances are measured from: the skate module's +0x3DC (a flag that turns the checks off, then
    // the position).
    const uint8_t* camera = reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x3DC;
    auto* component =
        static_cast<CDormancyModule*>(CompositeObjectManager()->FindFirstModuleOfKind(CDormancyModule::kType));
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
                const Maths::Vector& pos = component->m_object->m_pos;
                const float* at = reinterpret_cast<const float*>(camera + 4);
                double dx = static_cast<double>(pos.x) - at[0], dy = static_cast<double>(pos.y) - at[1],
                       dz = static_cast<double>(pos.z) - at[2];
                double distanceSquared = (dz * dz + dy * dy) + dx * dx;
                component->m_distanceSquared = static_cast<float>(distanceSquared);
                component->m_far = distanceSquared > component->m_suspendDistanceSquared;
            }
        }
        CGameEntity* object = component->m_object;
        bool suspended = (object->m_compositeFlags & COMPOSITE_FLAG_SUSPENDED) != 0;
        if (component->m_far && !suspended)
            object->Suspend(true);
        else if (!component->m_far && suspended)
            object->Suspend(false);
        if (CObjectModule* model = component->m_meshInstance) {
            if (component->m_framesBeforeLod) {
                if (ModelOf(model))
                    SetModelActive(ModelOf(model), true);
            } else {
                component->UpdateVisibility();
            }
        }
        component = static_cast<CDormancyModule*>(component->mp_nextOfType);
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CDormancyModule::Destroy,
         &GameObjects::CDormancyModule::Update,
         &GameObjects::CDormancyModule::ConfigureFromData,
         &GameObjects::CDormancyModule::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
