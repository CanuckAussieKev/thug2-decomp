// Cutscenes: reading the objects table, the clock, the objects' transforms and the details' constructor and
// destructor (0x00465510-0x0046661A). See cutscene.h.

#include <cmath>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/mem.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/model.h"
#include "game/gfx/skeleton.h"
#include "game/mth/matrix.h"
#include "game/net/buddy_status.h"
#include "game/obj/cutscene.h"
#include "game/obj/matrix_component.h"
#include "game/obj/model_component.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/sys/game_config.h"
#include "game/core/stubs.h"
#include "game/gfx/viewport.h"
#include "hook/game_code.h"

namespace Obj {

namespace {

extern void** const kCutsceneDetailsVtable;

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kModelComponent = 0x286A8D26;
constexpr uint32_t kSkeletonComponent = 0x222756D5;
constexpr uint32_t kMatrixComponent = 0x208353AC; // (told the object's new matrix: a CMatrixComponent)
constexpr uint32_t kVersion = 0x40E32C3C;
constexpr uint32_t kObjects = 0x4DE5330C;
constexpr uint32_t kName = 0x2BFC2486;
constexpr uint32_t kModel = 0xB974F2FC;
constexpr uint32_t kSkeleton = 0x09794932;
constexpr uint32_t kAnim = 0x6C2BFB7F;
constexpr uint32_t kSkaterFlag = 0x723CDA8F;
constexpr uint32_t kSkaterFlag2 = 0x6914D9D2;
constexpr uint32_t kFixedTime = 0x0C00425A;      // a global integer: the time from the frame counter 0x006F115C
constexpr uint32_t kEndScript = 0x4C0FD1D7;      // run when a looping cutscene reaches its end
constexpr uint32_t kEndScript2 = 0xDEFF69FF;
constexpr uint32_t kDebugBoxes = 0x5ECC0073;     // a global integer: draw the objects' boxes (compiled out)
constexpr uint32_t kAttachBone = 0x5A0E0860;     // the bone a child hangs from
constexpr float kFrame = 0.03333333507180214f;   // (0x00646430: a 30th of a second)
constexpr float kStep = 0.01666666753590107f;     // (0x006463B8: a 60th)
constexpr float kPartEnd = 0.03343333676457405f;  // (0x0064791C)

// The vblank count since the cutscene started, minus the time it was paused, in seconds (x87: the count as an
// unsigned 64-bit number - the low 63 bits plus 2^63 - over the rate, as the original).
float VblankSeconds(uint64_t vblanks, int rate)
{
    uint64_t lowPart[1] = {vblanks & 0x7FFFFFFFFFFFFFFFull};
    uint64_t highPart[1] = {vblanks & 0x8000000000000000ull};
    float result;
    __asm {
        fild qword ptr [lowPart]
        fild qword ptr [highPart]
        fchs
        faddp st(1), st
        fidiv dword ptr [rate]
        fstp dword ptr [result]
    }
    return result;
}

void* NewAnimSource()
{
    void* memory = AnimSourceAllocate(0x214);
    return memory ? static_cast<Nx::CFrameSource*>(memory)->Construct() : nullptr;
}
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
void* ModelOf(CBaseComponent* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
// The model's fader (cutscene.h) fading to `target` by 0.02 a frame.
void* FaderOf(void* model) { return ModelFader(model); }
void FadeModel(void* model, float target)
{
    void* fader = FaderOf(model);
    static_cast<Nx::CModelLights*>(fader)->MoveBrightness(target, 0.02f);
}
uint8_t* Bytes(void* object)
{
    return static_cast<uint8_t*>(object);
}
// An object's two matrices (+0x6C and +0xC0) set to `matrix`.
void SetMatrices(CCompositeObject* object, const float* matrix)
{
    std::memcpy(Bytes(object) + 0x6C, matrix, 64);
    std::memcpy(Bytes(object) + 0xC0, matrix, 64);
}
float* Rotations() { return g_cutsceneRotations; }
float* Translations() { return g_cutsceneTranslations; }

// (debug drawing, compiled out: the object's box)
inline void DebugDrawBox(const void*, const void*, const float*, int, int, int, uint32_t) {}

} // namespace

// 0x00465510
bool __stdcall ObjectsFromTable(Script::CStruct* params, const uint32_t* table)
{
    params->AddInteger(kVersion, static_cast<int>(table[0]));
    int count = static_cast<int>(table[1]);
    const uint32_t* entry = table + 2;
    Script::CArray* memory = Script::NewArray();
    Script::CArray* objects = memory ? memory->Init() : nullptr;
    objects->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_STRUCTURE);
    for (int i = 0; i < count; ++i, entry += 5) {
        Script::CStruct* block = Script::NewStruct();
        Script::CStruct* object = block ? block->Init() : nullptr;
        object->AddChecksum(kName, entry[0]);
        object->AddChecksum(kModel, entry[1]);
        object->AddChecksum(kSkeleton, entry[2]);
        object->AddChecksum(kAnim, entry[3]);
        uint32_t flags = entry[4];
        if (flags >> 30 & 1)
            object->AddChecksum(kUnnamed, kSkaterFlag);
        if (flags >> 31 & 1)
            object->AddChecksum(kUnnamed, kSkaterFlag2);
        objects->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(object));
    }
    params->AddArrayPointer(kObjects, objects);
    return true;
}

// 0x004656E0
void CCutsceneData::UpdateTime(int /*unused*/)
{
    m_unknown3478 = *reinterpret_cast<uint32_t*>(&m_time);
    if (Script::GetInteger(kFixedTime) != 0) {
        m_time = static_cast<float>(g_cutsceneFixedFrame * static_cast<double>(kFrame));
    } else if (m_unknown3474) {
        // a fixed step a frame (looping back at the end)
        double time = static_cast<double>(m_unknown3470) * kStep + m_time;
        m_time = static_cast<float>(time);
        if (m_unknown348C > 0.0f && !(time < m_unknown348C) && !std::isnan(time)) {
            m_time = *reinterpret_cast<float*>(&m_unknown3488);
            m_unknown3478 = 0;
            Script::RunScript(kEndScript, nullptr, nullptr, false, nullptr);
            Script::RunScript(kEndScript2, nullptr, nullptr, false, nullptr);
        }
    } else {
        int rate = GameConfig::g_frameRate;
        uint64_t vblanks = Tmr::GetVblanks() - m_vblanks - m_pausedVblanks;
        m_time = VblankSeconds(vblanks, rate);
    }
    if (!m_flag34B0)
        return;
    // the camera and object animations come in parts: the next part when this one's over
    float length = *static_cast<float*>(mp_cameraAnim);
    if (!(m_time >= static_cast<double>(kPartEnd) + length)) { // (unordered too)
        m_cameraStart = static_cast<float>(m_time / (static_cast<double>(length) + kFrame) * kFrame);
        return;
    }
    m_unknown3480 = m_time;
    m_cameraStart = 0.0f;
    if (m_cameraPart < m_cameraParts) {
        ++m_cameraPart;
        uint32_t name = NameChecksum("_CAM", m_cameraPart); // (the game's string 0x00647914)
        if (m_cameraSource)
            DeleteVirtual(m_cameraSource);
        m_cameraSource = NewAnimSource();
        static_cast<Nx::CFrameSource*>(m_cameraSource)->SetDone(true);
        mp_cameraAnim = static_cast<Nx::CFrameSource*>(m_cameraSource)->SetAnim(name);
        static_cast<Gfx::CBonedAnimFrameData*>(mp_cameraAnim)->ArmEvents();
    }
    if (m_objectPart < m_objectParts) {
        ++m_objectPart;
        uint32_t name = NameChecksum("_OBA", m_objectPart); // (the game's string 0x0064790C)
        if (m_objectSource)
            DeleteVirtual(m_objectSource);
        m_objectSource = NewAnimSource();
        mp_animList = static_cast<Nx::CFrameSource*>(m_objectSource)->SetAnim(name);
        static_cast<Gfx::CBonedAnimFrameData*>(mp_animList)->ArmEvents();
    }
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        ++entry.part;
        if (entry.animation && entry.animation->source) {
            DeleteVirtual(entry.animation->source);
            entry.animation->source = nullptr;
        }
        uint32_t anim = entry.name + entry.part * 10;
        entry.animation = nullptr;
        for (int j = 0; j < m_animationCount; ++j) {
            if (m_animations[j].name == anim) {
                entry.animation = &m_animations[j];
                break;
            }
        }
        if (entry.animation) {
            entry.animation->source = NewAnimSource();
            static_cast<Nx::CFrameSource*>(entry.animation->source)->SetAnim(anim);
            static_cast<Nx::CFrameSource*>(entry.animation->source)->SetDone(true);
        }
    }
}

// 0x00465AB0
void CCutsceneData::UpdateStandIn(CCompositeObject* object)
{
    if (CBaseComponent* component = object->GetComponent(kModelComponent)) {
        void* model = ModelOf(component);
        if (*(Bytes(model) + 0x44) & 4) // (hidden)
            return;
        if (Bytes(component)[0x234]) {
            // the level object it stands in for: faded out, moved along
            auto* level = static_cast<CCompositeObject*>(
                FindObject(static_cast<CModelComponent*>(component)->GetRefObjectName()));
            CBaseComponent* levelComponent = level->GetComponent(kModelComponent);
            void* levelModel = ModelOf(levelComponent);
            FadeModel(levelModel, 0.0f);
            static_cast<CModelComponent*>(levelComponent)->UpdateBrightness();
            CBaseComponent* skeleton = object->GetComponent(kSkeletonComponent);
            float matrix[16];
            *reinterpret_cast<Mth::Matrix*>(matrix) = *reinterpret_cast<const Mth::Matrix*>(Bytes(object) + 0xC0);
            std::memcpy(matrix + 12, Bytes(object) + 0x4C, 16);
            static_cast<Nx::CModel*>(levelModel)->Render(
                reinterpret_cast<Mth::Matrix*>(matrix), false,
                reinterpret_cast<uint8_t*>(reinterpret_cast<GameNet::FoldedGetters*>(skeleton)->Word18()));
            std::memcpy(Bytes(level) + 0x4C, Bytes(object) + 0x4C, 16);
        } else {
            if (FaderOf(model)) {
                FadeModel(ModelOf(component), 1.0f);
                static_cast<CModelComponent*>(component)->UpdateBrightness();
            }
            reinterpret_cast<void(__thiscall*)(CBaseComponent*)>((*reinterpret_cast<void***>(component))[1])(
                component);
        }
    }
    if (CBaseComponent* matrix = object->GetComponent(kMatrixComponent))
        static_cast<CMatrixComponent*>(matrix)->SetMatrix(reinterpret_cast<const Mth::Matrix*>(Bytes(object) + 0xC0));
}

// 0x00465C00
void CCutsceneData::UpdateObjectTransforms()
{
    if (!m_objectSource)
        return;
    static_cast<Gfx::CBonedAnimFrameData*>(mp_animList)->GetFloatKeyPose(
        Rotations(), Translations(), g_cutsceneCameraSlot,
        static_cast<float>(m_time - static_cast<double>(m_cameraStart)),
        static_cast<int>(reinterpret_cast<uint32_t>(m_objectSource)));
    // the animated objects of the level (not the cutscene's own)
    for (int j = 0; j < *reinterpret_cast<int*>(Bytes(mp_animList) + 8); ++j) {
        uint32_t name = static_cast<Gfx::CBonedAnimFrameData*>(mp_animList)->GetBoneName(j);
        bool own = false;
        for (int k = m_objectCount - 1; k >= 0; --k) {
            if (m_objects[k].name == name)
                own = true;
        }
        if (own)
            continue;
        auto* object = static_cast<CCompositeObject*>(FindObject(name));
        if (!object)
            continue;
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float matrix[16];
        QuatToMatrix(Rotations() + 4 * j, origin, matrix);
        SetMatrices(object, matrix);
        std::memcpy(Bytes(object) + 0x4C, Translations() + 4 * j, 16);
    }
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        CCompositeObject* object = entry.object;
        if (CCompositeObject* parent = entry.parent) {
            // hanging from the parent's bone 0x5A0E0860
            auto* skeleton = reinterpret_cast<void*>(
                reinterpret_cast<GameNet::FoldedGetters*>(parent->GetComponent(kSkeletonComponent))->Word18());
            float bone[4], turned[4];
            static_cast<Gfx::CSkeleton*>(skeleton)->GetBonePosition(kAttachBone, reinterpret_cast<Mth::Vector*>(bone));
            *reinterpret_cast<Mth::Vector*>(turned) =
                *reinterpret_cast<const Mth::Vector*>(bone) * *reinterpret_cast<const Mth::Matrix*>(Bytes(parent) + 0xC0);
            const float* offset = turned;
            const float* at = reinterpret_cast<const float*>(Bytes(parent) + 0x4C);
            auto* position = reinterpret_cast<float*>(Bytes(object) + 0x4C);
            position[2] = static_cast<float>(static_cast<double>(at[2]) + offset[2]);
            position[3] = static_cast<float>(static_cast<double>(at[3]) + offset[3]);
            position[0] = static_cast<float>(static_cast<double>(offset[0]) + at[0]);
            position[1] = static_cast<float>(static_cast<double>(at[1]) + offset[1]);
            position[3] = 1.0f;
            float boneMatrix[16];
            static_cast<Gfx::CSkeleton*>(skeleton)->GetBoneMatrix(kAttachBone, reinterpret_cast<Mth::Matrix*>(boneMatrix));
            float parentMatrix[16];
            std::memcpy(parentMatrix, Bytes(parent) + 0xC0, 64);
            std::memcpy(parentMatrix + 12, at, 16);
            parentMatrix[3] = 0.0f;
            parentMatrix[7] = 0.0f;
            parentMatrix[11] = 0.0f;
            GameFunction<float*(__stdcall*)(float*, const float*, const float*)>(0x005BFA8C)(boneMatrix, boneMatrix,
                                                                                         parentMatrix);
            float matrix[16];
            std::memcpy(matrix, boneMatrix, 48);
            matrix[12] = 0.0f;
            matrix[13] = 0.0f;
            matrix[14] = 0.0f;
            matrix[15] = 1.0f;
            SetMatrices(object, matrix);
            UpdateStandIn(object);
        } else if (entry.animIndex != -1) {
            float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            float matrix[16];
            QuatToMatrix(Rotations() + 4 * entry.animIndex, origin, matrix);
            SetMatrices(object, matrix);
            std::memcpy(Bytes(object) + 0x4C, Translations() + 4 * entry.animIndex, 16);
            UpdateStandIn(object);
        }
        if (Script::GetInteger(kDebugBoxes)) {
            const float box[8] = {1.0f, 10.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, 1.0f};
            DebugDrawBox(Bytes(object) + 0xC0, Bytes(object) + 0x4C, box, 0, 1, 0, 0xFF0000C8);
        }
    }
}

// 0x00466450
void CCutsceneData::RestartObjects()
{
    if (m_flag34B0)
        return;
    m_time = 0.0f;
    m_unknown3478 = 0;
    DestroyObjects();
    AddObjects(mp_params);
}

// 0x00466490
CCutsceneDetails* CCutsceneDetails::Construct(const uint32_t* settings)
{
    reinterpret_cast<CMovieDetails*>(this)->Construct();
    m_vtable = kCutsceneDetailsVtable;
    m_data = nullptr;
    m_asset = 0;
    Script::CStruct* block = Script::NewStruct();
    m_params = block ? block->Init() : nullptr;
    std::memcpy(m_settings, settings, 12);
    m_ownsData = static_cast<uint8_t>(m_settings[2] & 0xFF);
    m_flag5D = false;
    m_unknown60 = -1;
    return this;
}

// 0x00466560
void CCutsceneDetails::DestroyBody()
{
    m_vtable = kCutsceneDetailsVtable;
    if (Script::CStruct* params = m_params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    if (m_ownsData) {
        if (m_data) {
            auto* assets = Ass::g_assetManager;
            Ass::CAsset* asset = assets->GetAsset(m_asset, 1);
            assets->UnloadAsset(asset);
            m_data = nullptr;
        }
        Nx::SetAmbientLightModulationFactor(m_musicVolume, 0.0f);
        Nx::SetDiffuseLightModulationFactor(0, m_streamVolume0, 0.0f);
        Nx::SetDiffuseLightModulationFactor(1, m_streamVolume1, 0.0f);
    }
    reinterpret_cast<CMovieDetails*>(this)->DestroyBody();
}

} // namespace Obj

REPLACE(0x00465510, Obj::ObjectsFromTable);
REPLACE(0x004656E0, &Obj::CCutsceneData::UpdateTime);
REPLACE(0x00465AB0, &Obj::CCutsceneData::UpdateStandIn);
REPLACE(0x00465C00, &Obj::CCutsceneData::UpdateObjectTransforms);
REPLACE(0x00466450, &Obj::CCutsceneData::RestartObjects);
REPLACE(0x00466490, &Obj::CCutsceneDetails::Construct);
REPLACE(0x00466560, &Obj::CCutsceneDetails::DestroyBody);

namespace Obj {
namespace {

GAME_VTABLE(kCutsceneDetailsVtable, 0x00647920,
    &Obj::CCutsceneDetails::Destroy,
    &Obj::CCutsceneDetails::InitFromStructure,
    &Obj::CCutsceneDetails::Update,
    Stubs::ReturnTrue,
    &Nx::CViewport::QueryPoint,
    &Obj::CCutsceneDetails::IsDone,
    Stubs::ReturnFalse,
    &Obj::CCutsceneDetails::IsReady,
    &Obj::CCutsceneDetails::End,
    &Obj::CCutsceneDetails::IsSkippable,
    Stubs::ReturnFalse,
    &Obj::CCutsceneDetails::CallSlot12WithNotFlag5D,
    Stubs::IgnoreArgument,
    &Obj::CCutsceneDetails::UpdateSlot12);

} // namespace
} // namespace Obj
