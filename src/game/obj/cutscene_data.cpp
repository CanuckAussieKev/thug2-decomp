// Cutscenes: the cutscene's objects, assets, lights, camera and poses (0x00463AA0-0x0046449B). See cutscene.h.

#include <bit>
#include <cstdint>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/camera.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/model.h"
#include "game/gfx/scene_lights.h"
#include "game/gfx/skeleton.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/net/buddy_status.h"
#include "game/obj/cutscene.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"
#include "game/gfx/engine.h"
#include "game/sys/startup_init.h"

namespace Obj {

uint8_t g_cutsceneCameraSlot[5];
float g_cutsceneRotations[512];
float g_cutsceneTranslations[512];

namespace {

constexpr uint32_t kModelComponent = 0x286A8D26;    // the component whose +0x208 is the object's model
constexpr uint32_t kSkeletonComponent = 0x222756D5;
constexpr uint32_t kCutsceneObjectType = 0xCDEB9229;
constexpr uint32_t kHideObjects = 0x67F08671;       // a global array: the objects a cutscene hides
constexpr uint32_t kLights = 0x2A79C44E;            // a global array: the nodes of the cutscene's lights
constexpr uint32_t kCreateScript = 0x1F29C5A5;      // the scripts that make a cutscene object
constexpr uint32_t kInitScript = 0x2E20EA32;
constexpr uint32_t kFlagParam = 0x82C84419;         // CreateObject's parameter: +0x34B0
constexpr uint32_t kProfile = 0x1804A7C4;           // SkaterModelName's parameters and table
constexpr uint32_t kProfiles = 0xCA1DB456;
constexpr uint32_t kProfileName = 0x40C698AF;
constexpr uint32_t kProfileModel = 0x16CBB85A;
constexpr uint32_t kMale = 0x3F813177;
constexpr uint32_t kNoColour = 0x5A96985D;          // ReadColour's structure: {r, g, b}, or this set: white
constexpr uint32_t kRed = 0x7323E97C;
constexpr uint32_t kGreen = 0x0424D9EA;
constexpr uint32_t kBlue = 0x9D2D8850;

// The model of an object's model component (+0x208), and the model calls (not rewritten yet).
void* ModelOf(CBaseComponent* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
uint32_t ModelGet(void* model)
{
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(CodeAddress(&Nx::CModel::GetBoneMatrices))(model);
}
void ModelSet(void* model, uint32_t value)
{
    static_cast<Nx::CModel*>(model)->SetBoneMatrices(reinterpret_cast<uint8_t*>(value));
}

// The asset manager (0x006918DC).
Ass::CAssMan* AssetManager()
{
    return Ass::g_assetManager;
}
// (from memory, a2 the data and a3 its size: not permanent, copied)
void LoadAssetFile(uint32_t name, uint32_t a2, uint32_t a3)
{
    AssetManager()->LoadAssetFromStream(name, 5, 0, reinterpret_cast<void*>(a2), a3, false, true, nullptr, 0);
}

} // namespace

// 0x00463AA0 (x87: the squares and w * z unrounded)
void __cdecl QuatToMatrix(float* quat, const float* translation, float* matrix)
{
    quat[0] = -quat[0];
    quat[1] = -quat[1];
    quat[2] = -quat[2];
    double xx = static_cast<double>(quat[0]) * quat[0];
    double yy = static_cast<double>(quat[1]) * quat[1];
    double zz = static_cast<double>(quat[2]) * quat[2];
    double zy = static_cast<double>(quat[2]) * quat[1];
    double zx = static_cast<double>(quat[2]) * quat[0];
    double yx = static_cast<double>(quat[1]) * quat[0];
    float wx = static_cast<float>(static_cast<double>(quat[3]) * quat[0]);
    float wy = static_cast<float>(static_cast<double>(quat[3]) * quat[1]);
    double wz = static_cast<double>(quat[3]) * quat[2];
    matrix[0] = static_cast<float>(1.0f - (zz + yy) * 2.0);
    matrix[1] = static_cast<float>((wz + yx) * 2.0);
    matrix[2] = static_cast<float>((zx - wy) * 2.0);
    matrix[3] = 0.0f;
    matrix[4] = static_cast<float>((yx - wz) * 2.0);
    matrix[5] = static_cast<float>(1.0f - (zz + xx) * 2.0);
    matrix[6] = static_cast<float>((wx + zy) * 2.0);
    matrix[7] = 0.0f;
    matrix[8] = static_cast<float>((wy + zx) * 2.0);
    matrix[9] = static_cast<float>((zy - wx) * 2.0);
    matrix[10] = static_cast<float>(1.0f - (yy + xx) * 2.0);
    matrix[11] = 0.0f;
    std::memcpy(matrix + 12, translation, 16);
    matrix[15] = 1.0f;
}

// 0x00464340
void IdentityQuat::Set()
{
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = 0.0f;
    q[3] = 1.0f;
}

// 0x00463BA0
const char* __cdecl SkaterModelName(Script::CStruct* params)
{
    uint32_t profile;
    if (params->GetChecksum(kProfile, &profile, 0)) {
        Script::CArray* profiles = Script::GetArray(kProfiles);
        for (int i = 0; i < static_cast<int>(profiles->m_size); ++i) {
            auto* entry = reinterpret_cast<Script::CStruct*>(profiles->GetElement(static_cast<uint32_t>(i)));
            uint32_t name = 0; // (asserted: the original's is uninitialised)
            entry->GetChecksum(kProfileName, &name, 1);
            if (name == profile) {
                const char* model = nullptr;
                entry->GetString(kProfileModel, &model, 1);
                return model;
            }
        }
        return nullptr;
    }
    int male = 0; // (asserted: the original's is uninitialised)
    params->GetInteger(kMale, &male, 1);
    return male == 1 ? "Male1" : "Female1"; // (the game's strings 0x00647900 / 0x006478F8)
}

// 0x00463DA0
float* __cdecl ReadColour(float* out, Script::CStruct* params, uint32_t name)
{
    Script::CStruct* colour = nullptr;
    if (params->GetStructure(name, &colour, 0)) {
        int none = 1;
        colour->GetInteger(kNoColour, &none, 0);
        if (none == 0) {
            int red = 0, green = 0, blue = 0; // (asserted: the original's are uninitialised)
            colour->GetInteger(kRed, &red, 1);
            colour->GetInteger(kGreen, &green, 1);
            colour->GetInteger(kBlue, &blue, 1);
            out[3] = 1.0f;
            out[0] = static_cast<float>(red * static_cast<double>(0.0099999998f));
            out[1] = static_cast<float>(green * static_cast<double>(0.0099999998f));
            out[2] = static_cast<float>(blue * static_cast<double>(0.0099999998f));
            return out;
        }
    }
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    return out;
}

// 0x00463E80
void AddCutsceneLights()
{
    Script::CArray* lights = Script::GetArray(kLights);
    if (!lights)
        return;
    int count = static_cast<int>(lights->m_size);
    for (int i = 0; i < count; ++i) {
        Script::CStruct* node = Script::GetStructure(lights->GetElement(static_cast<uint32_t>(i)));
        Nx::CreateLightFromNode(node);
    }
}

// 0x00463ED0
void RemoveCutsceneLights()
{
    Script::CArray* lights = Script::GetArray(kLights);
    if (!lights)
        return;
    int count = static_cast<int>(lights->m_size);
    for (int i = 0; i < count; ++i) {
        Script::CStruct* node = Script::GetStructure(lights->GetElement(static_cast<uint32_t>(i)));
        Nx::DeleteSceneLight(SkateScript::GetNodeName(node));
    }
}

// 0x004640D0
void SetUpCutsceneHeaps()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    reinterpret_cast<Nx::CFlexibleParticleManager*&>(Nx::g_flexibleParticleManager)->CreateTempHeap(0x25800);
    uint32_t available = Mem::Manager::Get()->Available();
    Mem::Manager::Get()->InitCutsceneHeaps(available - 0x32000);
    Mem::Manager::Get()->PopContext();
}

// 0x00463C70
void CCutsceneData::DestroyObjects()
{
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        if (entry.active) {
            // the level's object it stood in for gets its model's settings back
            CBaseComponent* standIn = static_cast<CCompositeObject*>(entry.object)->GetComponent(kModelComponent);
            uint32_t id = static_cast<CModelComponent*>(standIn)->GetRefObjectName();
            auto* original = static_cast<CCompositeObject*>(FindObject(id));
            void* model = ModelOf(original->GetComponent(kModelComponent));
            static_cast<Nx::CModel*>(model)->SetBoundingSphere(reinterpret_cast<const Mth::Vector*>(m_lent2C));
            static_cast<Nx::CModel*>(model)->Hide(m_lent3C);
            static_cast<Nx::CModel*>(model)->EnableScaling(m_lent3D);
            uint32_t mine = ModelGet(model);
            ModelSet(model, ModelGet(ModelOf(standIn)));
            ModelSet(ModelOf(standIn), mine);
            std::memcpy(reinterpret_cast<uint8_t*>(original) + 0x4C, m_colour, 16);
        }
        if (entry.object) {
            void** vtable = *reinterpret_cast<void***>(entry.object);
            reinterpret_cast<void(__thiscall*)(void*, int)>(vtable[0])(entry.object, 1);
        }
        entry.object = nullptr;
    }
    m_objectCount = 0;
    g_cutscenePlaying = false;
}

// 0x00463F20
void CCutsceneData::LoadAsset(uint32_t name, uint32_t a2, uint32_t a3)
{
    LoadAssetFile(name, a2, a3);
    m_assets[m_assetCount] = name;
    ++m_assetCount;
}

// 0x00463F70
void CCutsceneData::LoadAnimation(uint32_t name, uint32_t a2, uint32_t a3)
{
    AnimationEntry& entry = m_animations[m_animationCount];
    LoadAssetFile(name, a2, a3);
    m_assets[m_assetCount] = name;
    ++m_assetCount;
    void* data = Nx::GetAnimData(name, 1);
    entry.source = nullptr;
    entry.name = name;
    if (!entry.data)
        entry.data = data;
    ++m_animationCount;
}

// 0x00464000
CCompositeObject* CCutsceneData::CreateObject(Script::CStruct* params)
{
    void* memory = Mem::Calloc(0x108);
    auto* object = memory ? static_cast<CCompositeObject*>(memory)->Construct() : nullptr;
    object->SetType(kCutsceneObjectType);
    void* objects = static_cast<void*>(g_compositeObjectManager);
    reinterpret_cast<void(__thiscall*)(void*, CCompositeObject*)>((*reinterpret_cast<void***>(objects))[1])(objects,
                                                                                                         object);
    params->AddInteger(kFlagParam, m_flag34B0);
    Script::RunScript(kCreateScript, params, object, false, nullptr);
    reinterpret_cast<void(__thiscall*)(CCompositeObject*)>((*reinterpret_cast<void***>(object))[0x20 / 4])(object);
    Script::RunScript(kInitScript, params, object, false, nullptr);
    auto* colour = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(object) + 0x4C);
    colour[0] = 0.0f;
    colour[1] = 0.0f;
    colour[2] = 0.0f;
    colour[3] = 1.0f;
    return object;
}

// 0x004641A0
void CCutsceneData::ShowHiddenObjects()
{
    if (!m_flag34B0)
        return;
    Script::CArray* hidden = Script::GetArray(kHideObjects);
    if (!hidden)
        return;
    for (uint32_t i = 0; i < hidden->m_size; ++i) {
        auto* object = static_cast<CCompositeObject*>(FindObject(hidden->GetElement(i)));
        if (!object)
            continue;
        CBaseComponent* component = object->GetComponent(kModelComponent);
        if (!component || !ModelOf(component))
            continue;
        static_cast<Nx::CModel*>(ModelOf(component))->SetActive(1);
        reinterpret_cast<void(__thiscall*)(CBaseComponent*)>((*reinterpret_cast<void***>(component))[1])(component);
    }
}

// 0x00464220
void CCutsceneData::UpdateCamera(void* camera)
{
    if (!m_flag34B0)
        return;
    static_cast<Gfx::Camera*>(camera)->StoreOldPos();
    auto* position = reinterpret_cast<float*>(static_cast<Gfx::Camera*>(camera)->GetPos());
    auto* matrix = reinterpret_cast<float*>(static_cast<Gfx::Camera*>(camera)->GetMatrix());
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float translation[4];
    bool flag = false;
    static_cast<Gfx::CBonedAnimFrameData*>(mp_cameraAnim)
        ->GetFloatKeyPose(rotation, translation, &flag, static_cast<float>(m_time - static_cast<double>(m_cameraStart)),
                          static_cast<int>(reinterpret_cast<uint32_t>(m_cameraSource)));
    std::memcpy(position, translation, 16);
    float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    QuatToMatrix(rotation, origin, matrix);
    if (m_skipped)
        return;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_cutsceneBottomUpHeap);
    static_cast<Gfx::CBonedAnimFrameData*>(mp_objectAnims)
        ->FireEvents(std::bit_cast<float>(m_unknown3478), m_time, camera, 0);
    Mem::Manager::Get()->PopContext();
}

// 0x00464360
void CCutsceneData::UpdateObjectPoses()
{
    float* rotations = g_cutsceneRotations;
    float* translations = g_cutsceneTranslations;
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        if (!entry.animation)
            continue;
        CBaseComponent* component = entry.object->GetComponent(kSkeletonComponent);
        if (!component)
            continue;
        // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
        auto* skeleton = reinterpret_cast<uint8_t*>(reinterpret_cast<GameNet::FoldedGetters*>(component)->Word18());
        auto* source = static_cast<uint8_t*>(entry.animation->source);
        void* animation = entry.animation->data;
        reinterpret_cast<Gfx::CSkeleton*>(skeleton)->SetLOD(0.0f);
        uint8_t* data = *reinterpret_cast<uint8_t**>(skeleton + 8);
        *reinterpret_cast<uint8_t**>(source + 0x20C) = data ? data + 0x408 : nullptr;
        *reinterpret_cast<uint32_t*>(source + 0x210) = *reinterpret_cast<uint32_t*>(skeleton + 0xC);
        const uint8_t* basePose = *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(skeleton + 8) + 0x590);
        uint32_t count = reinterpret_cast<uint32_t(__thiscall*)(void*)>(CodeAddress(&Gfx::CSkeleton::GetNumBones))(skeleton);
        std::memcpy(translations, basePose, count * 16);
        // (within the first twelfth of a second, without the source)
        if (m_time - static_cast<double>(m_unknown3480) < 0.083333336f)
            source = nullptr;
        static_cast<Gfx::CBonedAnimFrameData*>(animation)->GetFrames(
            reinterpret_cast<Mth::Quat*>(rotations), reinterpret_cast<Mth::Vector*>(translations),
            g_cutsceneCameraSlot, static_cast<float>(m_time - static_cast<double>(m_cameraStart)), -1,
            source);
        reinterpret_cast<Gfx::CSkeleton*>(skeleton)->Update(reinterpret_cast<const float(*)[4]>(rotations),
                                                            reinterpret_cast<const float(*)[4]>(translations));
    }
}

} // namespace Obj

namespace Obj {

// 0x00640A30 (a static initialiser): the 128 cutscene bone rotations, each the quaternion (0, 0, 0, 1).
void ConstructCutsceneRotations()
{
    for (int i = 0; i < 128; ++i) {
        g_cutsceneRotations[4 * i + 0] = 0.0f;
        g_cutsceneRotations[4 * i + 1] = 0.0f;
        g_cutsceneRotations[4 * i + 2] = 0.0f;
        g_cutsceneRotations[4 * i + 3] = 1.0f;
    }
}

} // namespace Obj
