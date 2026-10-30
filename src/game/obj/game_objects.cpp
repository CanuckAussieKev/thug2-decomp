// Cars made in code and the King of the Hill crown, 0x005780E0-0x00578670. See game_objects.h.

#include "game/obj/game_objects.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/string.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/skeleton.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/buddy_status.h"
#include "game/obj/bbox_component.h"
#include "game/obj/car_physics.h"
#include "game/obj/compositemgr.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/object_proximity.h"
#include "game/obj/objman.h"
#include "game/obj/sound_component.h"
#include "game/obj/stream_component.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

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

void** const kCrownVtable = reinterpret_cast<void**>(0x0064EC44);

constexpr uint32_t kTriggerScript = 0x2CA8A299;    // TriggerScript
constexpr uint32_t kParams = 0x7031F10C;           // params
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;    // OBJECT_TYPE_PED
constexpr uint32_t kPedAddComponents = 0xF3EEC45E; // ped_add_components (a script)
constexpr uint32_t kPedInitModel = 0x955A2767;     // ped_init_model (a script)
constexpr uint32_t kPedDisableBones = 0x48CA2D26;  // ped_disable_bones (a script)

// A new component of `size` bytes from T's constructor (null if there's no memory).
template <typename T>
CBaseComponent* NewComponent(unsigned int size)
{
    void* memory = Mem::Calloc(size);
    return memory ? static_cast<T*>(memory)->Construct() : nullptr;
}
// A component's InitFromStructure (vtable +0x08).
void InitComponentFromStructure(CBaseComponent* component, Script::CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CBaseComponent*, Script::CStruct*)>((*reinterpret_cast<void***>(component))[2])(
        component, params);
}
// The model component's update after the object is finished (0x00420480).
void UpdateModel(CBaseComponent* model) { static_cast<CModelComponent*>(model)->Teleport(); }
// The composite object's Finalize (vtable +0x20).
void VirtualFinalize(CCompositeObject* object)
{
    reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[8])(object);
}

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

} // namespace

// 0x0057B250
void AddBaseComponents(CCompositeObject* object)
{
    object->AddComponent(NewComponent<CSuspendComponent>(0x4C)); // suspend
    object->AddComponent(NewComponent<CBBoxComponent>(0x4C));    // (type 0xB87C8E43)
}

// 0x0057B2F0
void AddMotionComponents(CCompositeObject* object)
{
    object->AddComponent(NewComponent<CMotionComponent>(0x24C)); // motion
    object->AddComponent(NewComponent<CObjectProximityComponent>(0x30)); // objectproximity
    object->AddComponent(NewComponent<CSoundComponent>(0xC0));   // sound
    object->AddComponent(NewComponent<CStreamComponent>(0x2C));  // stream
    object->AddComponent(NewComponent<CLockObjComponent>(0x90)); // lockobj
}

// 0x0057B420
void AddNodeComponents(CCompositeObject* object, Script::CStruct* node)
{
    CBaseComponent* motion = object->GetComponent(kMotion);
    if (!motion)
        AddMotionComponents(object); // (sic: not looked up again - the motion stays null below)
    static_cast<CMotionComponent*>(motion)->OrientToNode(node);
    if (uint32_t name = SkateScript::GetNodeName(node))
        object->SetID(name);
    uint32_t script = 0;
    if (node->GetChecksum(kTriggerScript, &script, 0)) {
        Script::CStruct* params = nullptr;
        node->GetStructure(kParams, &params, 0);
        object->SwitchScript(script, params);
    }
    InitComponentFromStructure(motion, node);
    InitComponentFromStructure(object->GetComponent(kSuspend), node);
}

// 0x0057B4E0
void CreatePed(Script::CStruct* params)
{
    void* memory = Mem::Calloc(sizeof(CCompositeObject));
    auto* ped = memory ? static_cast<CCompositeObject*>(memory)->Construct() : nullptr;
    AddBaseComponents(ped);
    AddMotionComponents(ped);
    ped->SetType(kObjectTypePed);
    CompositeObjectManager()->VirtualRegisterObject(ped);
    SkateScript::GetPosition(params, &ped->m_pos);
    AddNodeComponents(ped, params);
    Script::RunScript(kPedAddComponents, params, ped, false, nullptr);
    VirtualFinalize(ped);
    Script::RunScript(kPedInitModel, params, ped, false, nullptr);
    auto* motion = reinterpret_cast<uint8_t*>(ped->GetComponent(kMotion));
    *reinterpret_cast<float*>(motion + 0x2C) = 44.0f;
    *reinterpret_cast<float*>(motion + 0x24) = 48.0f;
    *reinterpret_cast<float*>(motion + 0x28) = 12.0f;
    *reinterpret_cast<float*>(motion + 0x34) = 24.0f;
    reinterpret_cast<CMotionComponent*>(motion)->InitPathFollower(ped);
    auto* path = static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
    *reinterpret_cast<float*>(path + 0x94) = 24.0f;
    *reinterpret_cast<uint32_t*>(motion + 0x1C) |= 0x400;
    UpdateModel(ped->GetComponent(kModel));
    Script::RunScript(kPedDisableBones, nullptr, ped, false, nullptr);
}

// 0x005780E0
void InitCar(CCompositeObject* car, Script::CStruct* params)
{
    AddMotionComponents(car);
    AddNodeComponents(car, params);
    auto* motion = reinterpret_cast<uint8_t*>(car->GetComponent(kMotion));
    *reinterpret_cast<float*>(motion + 0x2C) = 528.0f;
    *reinterpret_cast<float*>(motion + 0x24) = 216.0f;
    *reinterpret_cast<float*>(motion + 0x28) = 384.0f;
    motion[0x70] = 0;
    reinterpret_cast<CMotionComponent*>(motion)->InitPathFollower(car);
    auto* extra = static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
    *reinterpret_cast<float*>(extra + 0x94) = 240.0f;
    Script::RunScript(kCarAddComponents, params, car, false, nullptr);
    VirtualFinalize(car);
    UpdateModel(car->GetComponent(kModel));
}

// 0x00578170
void CreateCar(Script::CStruct* params)
{
    void* memory = Mem::Calloc(sizeof(CCompositeObject));
    auto* car = memory ? static_cast<CCompositeObject*>(memory)->Construct() : nullptr;
    AddBaseComponents(car);
    car->SetType(kObjectTypeCar);
    void* component = Mem::Calloc(0x58);
    car->AddComponent(component ? static_cast<CCarPhysicsComponent*>(component)->Construct() : nullptr);
    SkateScript::GetPosition(params, &car->m_pos);
    CompositeObjectManager()->VirtualRegisterObject(car);
    InitCar(car, params);
}

// 0x00578240
CCrownObject* CCrownObject::Construct()
{
    CCompositeObject::Construct();
    m_vtable = kCrownVtable;
    AddBaseComponents(this);
    m_type = kObjectTypeCrown;
    SetID(kCrownObject);
    m_worn = false;
    return this;
}

// 0x005782B0
void CCrownObject::PutOn(CCompositeObject* skater)
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
    CCompositeObject::DestroyBody();
}

// 0x00578310
void CCrownObject::OwnUpdate()
{
    if (!m_worn)
        return;
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    auto* skeleton = reinterpret_cast<void*>(reinterpret_cast<GameNet::FoldedGetters*>(m_skater->GetComponent(kSkeleton))->Word18());
    static_cast<Gfx::CSkeleton*>(skeleton)->GetBoneMatrix(kBoneHead, &m_matrix);
    Mth::Matrix turn;
    Mth::Multiply(m_matrix, Mth::CreateRotateXMatrix(turn, 90.0f), m_matrix);
    Mth::Vector raise = {0.0f, 0.0f, -18.0f, 0.0f};
    m_matrix.TranslateLocal(raise);
    Mth::Vector offset = m_matrix.row[3];
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Multiply(m_matrix, m_matrix, m_skater->m_matrix);
    Mth::Vector moved;
    m_pos = m_skater->m_matrix.Transform(moved, offset); // (the matrix times the offset, w too)
    const Mth::Vector& at = m_skater->m_pos;
    m_pos.x = static_cast<float>(static_cast<double>(at.x) + m_pos.x);
    m_pos.y = static_cast<float>(static_cast<double>(at.y) + m_pos.y);
    m_pos.z = static_cast<float>(static_cast<double>(at.z) + m_pos.z);
    m_pos.w = static_cast<float>(static_cast<double>(at.w) + m_pos.w);
}

// 0x00578460
CCrownObject* CCrownObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00578490
void CCrownObject::InitFromNode(CCompositeObjectManager* manager, Script::CStruct* node)
{
    manager->VirtualRegisterObject(this);
    AddMotionComponents(this);
    Script::CStruct* model = NewStructure();
    model->AddChecksum(kComponent, kModel);
    VirtualCreateComponentFromStructure(model, nullptr);
    DeleteStructure(model);
    AddNodeComponents(this, node);
    SetID(kCrownObject);
    auto* suspend = reinterpret_cast<uint8_t*>(GetComponent(kSuspend));
    *reinterpret_cast<float*>(suspend + 0x34) = 1000000.0f;
    *reinterpret_cast<float*>(suspend + 0x38) = 1000001.0f;
    *reinterpret_cast<float*>(suspend + 0x3C) = 1000002.0f;
    *reinterpret_cast<float*>(suspend + 0x40) = 1000003.0f;
    Str::String file;
    {
        // The model's path (0x004879D0 returns a String: made in `storage`).
        alignas(Str::String) uint8_t storage[sizeof(Str::String)];
        auto* path = Gfx::MakeModelPath(reinterpret_cast<Str::String*>(storage), "crown", ".mdl");
        file = *path;
        path->~String();
    }
    // The model component's model (+0x208): loaded from the file (0x004935E0), then 0x00493C60(1).
    void* modelData = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(GetComponent(kModel)) + 0x208);
    static_cast<Nx::CModel*>(modelData)->AddFileGeom(file.c_str(), 0, true, 0, false, false, true, false);
    modelData = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(GetComponent(kModel)) + 0x208);
    static_cast<Nx::CModel*>(modelData)->SetActive(1);
    SkateScript::GetPosition(node, &m_pos);
    VirtualFinalize(this);
    UpdateModel(GetComponent(kModel));
}

} // namespace Obj

REPLACE(0x0057B250, Obj::AddBaseComponents);
REPLACE(0x0057B2F0, Obj::AddMotionComponents);
REPLACE(0x0057B420, Obj::AddNodeComponents);
REPLACE(0x0057B4E0, Obj::CreatePed);
REPLACE(0x005780E0, Obj::InitCar);
REPLACE(0x00578170, Obj::CreateCar);
REPLACE(0x00578240, &Obj::CCrownObject::Construct);
REPLACE(0x005782B0, &Obj::CCrownObject::PutOn);
REPLACE(0x005782D0, &Obj::CCrownObject::TakeOff);
REPLACE(0x005782E0, &Obj::CCrownObject::IsWorn);
REPLACE(0x005782F0, &Obj::CCrownObject::DestroyBody);
REPLACE(0x00578310, &Obj::CCrownObject::OwnUpdate);
REPLACE(0x00578460, &Obj::CCrownObject::Destroy);
REPLACE(0x00578490, &Obj::CCrownObject::InitFromNode);
