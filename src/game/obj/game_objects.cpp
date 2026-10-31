// Cars made in code and the King of the Hill crown, 0x005780E0-0x00578670. See game_objects.h.

#include "game/obj/game_objects.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/text_store.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/rig.h"
#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/net/buddy_status.h"
#include "game/obj/bbox_component.h"
#include "game/obj/car_physics.h"
#include "game/obj/compositemgr.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/object_proximity.h"
#include "game/obj/entity_hub.h"
#include "game/obj/sound_component.h"
#include "game/obj/stream_component.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/obj/entity.h"
#include "game/core/stubs.h"

namespace GameObjects {

namespace {

// Names.
constexpr uint32_t kMotion = 0x0A015E17;            // motion (a component)
constexpr uint32_t kModel = 0x286A8D26;             // model
constexpr uint32_t kSkeleton = 0x222756D5;          // skeleton
constexpr uint32_t kSuspend = 0xCE0CA665;           // suspend
constexpr uint32_t kComponent = 0xB6015EA8;         // component
constexpr uint32_t kBoneHead = 0xDDEC28AF;          // bone_head
constexpr uint32_t kCrownObject = 0xC015BC23;       // crown_object (its id)
constexpr uint32_t kCarAddComponents = 0xF1753591;  // car_add_components (a script)
constexpr uint32_t kObjectTypeCar = 0x5C0312EB;     // OBJECT_TYPE_CAR
constexpr uint32_t kObjectTypeCrown = 0xD2156C50;

extern void** const kCrownVtable;

constexpr uint32_t kTriggerScript = 0x2CA8A299;    // TriggerScript
constexpr uint32_t kParams = 0x7031F10C;           // params
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;    // OBJECT_TYPE_PED
constexpr uint32_t kPedAddComponents = 0xF3EEC45E; // ped_add_components (a script)
constexpr uint32_t kPedInitModel = 0x955A2767;     // ped_init_model (a script)
constexpr uint32_t kPedDisableBones = 0x48CA2D26;  // ped_disable_bones (a script)

// A new component of `size` bytes from T's constructor (null if there's no memory).
template <typename T>
CObjectModule* NewComponent(unsigned int size)
{
    void* memory = MemSys::AllocZeroed(size);
    return memory ? static_cast<T*>(memory)->MakeNew() : nullptr;
}
// A component's ConfigureFromData (vtable +0x08).
void InitComponentFromStructure(CObjectModule* component, Scripts::CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CObjectModule*, Scripts::CStruct*)>((*reinterpret_cast<void***>(component))[2])(
        component, params);
}
// The model component's update after the object is finished (0x00420480).
void UpdateModel(CObjectModule* model) { static_cast<CMeshInstanceModule*>(model)->JumpTo(); }
// The composite object's Finalize (vtable +0x20).
void VirtualFinalize(CGameEntity* object)
{
    reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[8])(object);
}

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

} // namespace

// 0x0057B250
void AddBaseComponents(CGameEntity* object)
{
    object->AttachModule(NewComponent<CDormancyModule>(0x4C)); // suspend
    object->AttachModule(NewComponent<CBBoxComponent>(0x4C));    // (type 0xB87C8E43)
}

// 0x0057B2F0
void AddMotionComponents(CGameEntity* object)
{
    object->AttachModule(NewComponent<CKinematicsModule>(0x24C)); // motion
    object->AttachModule(NewComponent<CObjectProximityComponent>(0x30)); // objectproximity
    object->AttachModule(NewComponent<CAudioEmitterModule>(0xC0));   // sound
    object->AttachModule(NewComponent<CAudioStreamModule>(0x2C));  // stream
    object->AttachModule(NewComponent<CPinnedObjectModule>(0x90)); // lockobj
}

// 0x0057B420
void AddNodeComponents(CGameEntity* object, Scripts::CStruct* node)
{
    CObjectModule* motion = object->FetchModule(kMotion);
    if (!motion)
        AddMotionComponents(object); // (sic: not looked up again - the motion stays null below)
    static_cast<CKinematicsModule*>(motion)->FaceNode(node);
    if (uint32_t name = LevelNodes::GetNodeName(node))
        object->ChangeSerial(name);
    uint32_t script = 0;
    if (node->ReadHashValue(kTriggerScript, &script, 0)) {
        Scripts::CStruct* params = nullptr;
        node->ReadSubStruct(kParams, &params, 0);
        object->ReplaceScript(script, params);
    }
    InitComponentFromStructure(motion, node);
    InitComponentFromStructure(object->FetchModule(kSuspend), node);
}

// 0x0057B4E0
void SpawnPedestrian(Scripts::CStruct* params)
{
    void* memory = MemSys::AllocZeroed(sizeof(CGameEntity));
    auto* ped = memory ? static_cast<CGameEntity*>(memory)->MakeNew() : nullptr;
    AddBaseComponents(ped);
    AddMotionComponents(ped);
    ped->ChangeSpecies(kObjectTypePed);
    CompositeObjectManager()->VirtualRegisterObject(ped);
    LevelNodes::FetchPlace(params, &ped->m_atPoint);
    AddNodeComponents(ped, params);
    Scripts::LaunchScript(kPedAddComponents, params, ped, false, nullptr);
    VirtualFinalize(ped);
    Scripts::LaunchScript(kPedInitModel, params, ped, false, nullptr);
    auto* motion = reinterpret_cast<uint8_t*>(ped->FetchModule(kMotion));
    *reinterpret_cast<float*>(motion + 0x2C) = 44.0f;
    *reinterpret_cast<float*>(motion + 0x24) = 48.0f;
    *reinterpret_cast<float*>(motion + 0x28) = 12.0f;
    *reinterpret_cast<float*>(motion + 0x34) = 24.0f;
    reinterpret_cast<CKinematicsModule*>(motion)->InitPathFollower(ped);
    auto* path = static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
    *reinterpret_cast<float*>(path + 0x94) = 24.0f;
    *reinterpret_cast<uint32_t*>(motion + 0x1C) |= 0x400;
    UpdateModel(ped->FetchModule(kModel));
    Scripts::LaunchScript(kPedDisableBones, nullptr, ped, false, nullptr);
}

// 0x005780E0
void SetUpAutomobile(CGameEntity* car, Scripts::CStruct* params)
{
    AddMotionComponents(car);
    AddNodeComponents(car, params);
    auto* motion = reinterpret_cast<uint8_t*>(car->FetchModule(kMotion));
    *reinterpret_cast<float*>(motion + 0x2C) = 528.0f;
    *reinterpret_cast<float*>(motion + 0x24) = 216.0f;
    *reinterpret_cast<float*>(motion + 0x28) = 384.0f;
    motion[0x70] = 0;
    reinterpret_cast<CKinematicsModule*>(motion)->InitPathFollower(car);
    auto* extra = static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
    *reinterpret_cast<float*>(extra + 0x94) = 240.0f;
    Scripts::LaunchScript(kCarAddComponents, params, car, false, nullptr);
    VirtualFinalize(car);
    UpdateModel(car->FetchModule(kModel));
}

// 0x00578170
void SpawnAutomobile(Scripts::CStruct* params)
{
    void* memory = MemSys::AllocZeroed(sizeof(CGameEntity));
    auto* car = memory ? static_cast<CGameEntity*>(memory)->MakeNew() : nullptr;
    AddBaseComponents(car);
    car->ChangeSpecies(kObjectTypeCar);
    void* component = MemSys::AllocZeroed(0x58);
    car->AttachModule(component ? static_cast<CAutomobileDynamicsModule*>(component)->MakeNew() : nullptr);
    LevelNodes::FetchPlace(params, &car->m_atPoint);
    CompositeObjectManager()->VirtualRegisterObject(car);
    SetUpAutomobile(car, params);
}

// 0x00578240
CCrownObject* CCrownObject::MakeNew()
{
    CGameEntity::MakeNew();
    m_vtable = kCrownVtable;
    AddBaseComponents(this);
    m_kindTag = kObjectTypeCrown;
    ChangeSerial(kCrownObject);
    m_worn = false;
    return this;
}

// 0x005782B0
void CCrownObject::PutOn(CGameEntity* skater)
{
    m_skater = skater;
    m_worn = true;
}

// 0x005782D0
void CCrownObject::TakeOff() { m_worn = false; }

// 0x005782E0
bool CCrownObject::IsWorn() { return m_worn; }

// 0x005782F0
void CCrownObject::DestroyBody()
{
    m_vtable = kCrownVtable;
    m_worn = false;
    CGameEntity::DestroyBody();
}

// 0x00578310
void CCrownObject::OwnUpdate()
{
    if (!m_worn)
        return;
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    auto* skeleton = reinterpret_cast<void*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(m_skater->FetchModule(kSkeleton))->Word18());
    static_cast<Gfx2D::CRig*>(skeleton)->ReadJointMatrix(kBoneHead, &m_basisFrame);
    Maths::Mat4 turn;
    Maths::Multiply(m_basisFrame, Maths::MakePitchMatrix(turn, 90.0f), m_basisFrame);
    Maths::Vec4 raise = {0.0f, 0.0f, -18.0f, 0.0f};
    m_basisFrame.OffsetLocal(raise);
    Maths::Vec4 offset = m_basisFrame.row[3];
    m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Multiply(m_basisFrame, m_basisFrame, m_skater->m_basisFrame);
    Maths::Vec4 moved;
    m_atPoint = m_skater->m_basisFrame.Xform(moved, offset); // (the matrix times the offset, w too)
    const Maths::Vec4& at = m_skater->m_atPoint;
    m_atPoint.x = static_cast<float>(static_cast<double>(at.x) + m_atPoint.x);
    m_atPoint.y = static_cast<float>(static_cast<double>(at.y) + m_atPoint.y);
    m_atPoint.z = static_cast<float>(static_cast<double>(at.z) + m_atPoint.z);
    m_atPoint.w = static_cast<float>(static_cast<double>(at.w) + m_atPoint.w);
}

// 0x00578460
CCrownObject* CCrownObject::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00578490
void CCrownObject::InitFromNode(CEntityHub* manager, Scripts::CStruct* node)
{
    manager->VirtualRegisterObject(this);
    AddMotionComponents(this);
    Scripts::CStruct* model = NewStructure();
    model->PutNameKey(kComponent, kModel);
    VirtualCreateComponentFromStructure(model, nullptr);
    DeleteStructure(model);
    AddNodeComponents(this, node);
    ChangeSerial(kCrownObject);
    auto* suspend = reinterpret_cast<uint8_t*>(FetchModule(kSuspend));
    *reinterpret_cast<float*>(suspend + 0x34) = 1000000.0f;
    *reinterpret_cast<float*>(suspend + 0x38) = 1000001.0f;
    *reinterpret_cast<float*>(suspend + 0x3C) = 1000002.0f;
    *reinterpret_cast<float*>(suspend + 0x40) = 1000003.0f;
    TextUtil::Chars file;
    {
        // The model's path (0x004879D0 returns a String: made in `storage`).
        alignas(TextUtil::Chars) uint8_t storage[sizeof(TextUtil::Chars)];
        auto* path = Gfx2D::MakeModelPath(reinterpret_cast<TextUtil::Chars*>(storage), "crown", ".mdl");
        file = *path;
        path->~Chars();
    }
    // The model component's model (+0x208): loaded from the file (0x004935E0), then 0x00493C60(1).
    void* modelData = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(FetchModule(kModel)) + 0x208);
    static_cast<Engine3D::CRenderModel*>(modelData)->AddFileGeom(file.c_str(), 0, true, 0, false, false, true, false);
    modelData = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(FetchModule(kModel)) + 0x208);
    static_cast<Engine3D::CRenderModel*>(modelData)->ChangeLive(1);
    LevelNodes::FetchPlace(node, &m_atPoint);
    VirtualFinalize(this);
    UpdateModel(FetchModule(kModel));
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kCrownVtable_slots[] = {&GameObjects::CCrownObject::Teardown,
         &GameObjects::CEntity::ChangeTraits,
         static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
         static_cast<bool (GameObjects::CGameEntity::*)(uint32_t, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CGameEntity::InvokeMethodByName),
         Stubs::IgnoreArgument,
         &GameObjects::CEntity::PassEventToScript,
         &GameObjects::CGameEntity::FlagAsDead,
         &GameObjects::CGameEntity::ShouldUpdateScripts,
         &GameObjects::CGameEntity::Seal,
         &GameObjects::CGameEntity::CreateComponentFromStructure,
         &GameObjects::CCrownObject::OwnUpdate};
void** const kCrownVtable = reinterpret_cast<void**>(kCrownVtable_slots);

} // namespace
} // namespace Obj
