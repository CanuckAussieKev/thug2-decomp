// Cutscenes: the cutscene's objects, assets, lights, camera and poses (0x00463AA0-0x0046449B). See cutscene.h.

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/obj/cutscene.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

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
    return GameFunction<uint32_t(__thiscall*)(void*)>(0x00492E50)(model);
}
void ModelSet(void* model, uint32_t value)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00493460)(model, value);
}

// The asset manager (0x006918DC).
void* AssetManager()
{
    return GameVariable<void*>(0x006918DC);
}
void LoadAssetFile(uint32_t name, uint32_t a2, uint32_t a3)
{
    GameFunction<void*(__thiscall*)(void*, uint32_t, int, int, uint32_t, uint32_t, int, int, int, int)>(0x00408060)(
        AssetManager(), name, 5, 0, a2, a3, 0, 1, 0, 0);
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
        GameFunction<void(__cdecl*)(Script::CStruct*)>(0x00490570)(node);
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
        GameFunction<void(__cdecl*)(uint32_t)>(0x0048FCA0)(SkateScript::GetNodeName(node));
    }
}

// 0x004640D0
void SetUpCutsceneHeaps()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049AA80)(GameVariable<void*>(0x006FA7C0), 0x25800);
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
            uint32_t id = GameFunction<uint32_t(__thiscall*)(CBaseComponent*)>(0x00420100)(standIn);
            auto* original = static_cast<CCompositeObject*>(FindObject(id));
            void* model = ModelOf(original->GetComponent(kModelComponent));
            GameFunction<void(__thiscall*)(void*, void*)>(0x00492E80)(model, m_lent2C);
            GameFunction<void(__thiscall*)(void*, int)>(0x00494B70)(model, m_lent3C);
            GameFunction<void(__thiscall*)(void*, int)>(0x00492E60)(model, m_lent3D);
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
    GameVariable<bool>(0x006F1158) = false;
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
    void* data = GameFunction<void*(__cdecl*)(uint32_t, int)>(0x0048CF50)(name, 1);
    entry.unknown4 = 0;
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
    void* objects = GameVariable<void*>(0x006F0FB8);
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
        GameFunction<void(__thiscall*)(void*, int)>(0x00493C60)(ModelOf(component), 1);
        reinterpret_cast<void(__thiscall*)(CBaseComponent*)>((*reinterpret_cast<void***>(component))[1])(component);
    }
}

// 0x00464220
void CCutsceneData::UpdateCamera(void* camera)
{
    if (!m_flag34B0)
        return;
    static_cast<Gfx::Camera*>(camera)->StoreOldPos();
    auto* position = GameFunction<float*(__thiscall*)(void*)>(0x00485A00)(camera);
    auto* matrix = GameFunction<float*(__thiscall*)(void*)>(0x00485A10)(camera);
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float translation[4];
    bool flag = false;
    GameFunction<void(__thiscall*)(void*, float*, float*, bool*, float, uint32_t)>(0x004844C0)(
        mp_cameraAnim, rotation, translation, &flag, static_cast<float>(m_time - static_cast<double>(m_cameraStart)),
        m_unknown14);
    std::memcpy(position, translation, 16);
    float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    QuatToMatrix(rotation, origin, matrix);
    if (m_skipped)
        return;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_cutsceneBottomUpHeap);
    GameFunction<void(__thiscall*)(void*, uint32_t, float, void*, int)>(0x00482F40)(mp_objectAnims, m_unknown3478,
                                                                                 m_time, camera, 0);
    Mem::Manager::Get()->PopContext();
}

// 0x00464360
void CCutsceneData::UpdateObjectPoses()
{
    float* rotations = &GameVariable<float>(0x006F1160);
    float* translations = &GameVariable<float>(0x006F1960);
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        if (!entry.animation)
            continue;
        CBaseComponent* component = entry.object->GetComponent(kSkeletonComponent);
        if (!component)
            continue;
        uint8_t* skeleton = GameFunction<uint8_t*(__thiscall*)(CBaseComponent*)>(0x005480F0)(component);
        uint8_t* bones = *reinterpret_cast<uint8_t**>(entry.animation + 4);
        void* animation = *reinterpret_cast<void**>(entry.animation + 8);
        GameFunction<void(__thiscall*)(void*, int)>(0x004A3090)(skeleton, 0);
        uint8_t* data = *reinterpret_cast<uint8_t**>(skeleton + 8);
        *reinterpret_cast<uint8_t**>(bones + 0x20C) = data ? data + 0x408 : nullptr;
        *reinterpret_cast<uint32_t*>(bones + 0x210) = *reinterpret_cast<uint32_t*>(skeleton + 0xC);
        const uint8_t* basePose = *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(skeleton + 8) + 0x590);
        uint32_t count = GameFunction<uint32_t(__thiscall*)(void*)>(0x004A2BA0)(skeleton);
        std::memcpy(translations, basePose, count * 16);
        // (within the first twelfth of a second, without the bones)
        if (m_time - static_cast<double>(m_unknown3480) < 0.083333336f)
            bones = nullptr;
        GameFunction<void(__thiscall*)(void*, float*, float*, void*, float, int, void*)>(0x00485730)(
            animation, rotations, translations, &GameVariable<uint8_t>(0x006F0FD8),
            static_cast<float>(m_time - static_cast<double>(m_cameraStart)), -1, bones);
        GameFunction<void(__thiscall*)(void*, float*, float*)>(0x004A4240)(skeleton, rotations, translations);
    }
}

} // namespace Obj

REPLACE(0x00463AA0, Obj::QuatToMatrix);
REPLACE_SOURCE_ONLY(0x00464340, &Obj::IdentityQuat::Set); // (unreachable code)
REPLACE(0x00463BA0, Obj::SkaterModelName);
REPLACE(0x00463C70, &Obj::CCutsceneData::DestroyObjects);
REPLACE(0x00463DA0, Obj::ReadColour);
REPLACE(0x00463E80, Obj::AddCutsceneLights);
REPLACE(0x00463ED0, Obj::RemoveCutsceneLights);
REPLACE(0x00463F20, &Obj::CCutsceneData::LoadAsset);
REPLACE(0x00463F70, &Obj::CCutsceneData::LoadAnimation);
REPLACE(0x00464000, &Obj::CCutsceneData::CreateObject);
REPLACE(0x004640D0, Obj::SetUpCutsceneHeaps);
REPLACE(0x004641A0, &Obj::CCutsceneData::ShowHiddenObjects);
REPLACE(0x00464220, &Obj::CCutsceneData::UpdateCamera);
REPLACE(0x00464360, &Obj::CCutsceneData::UpdateObjectPoses);
