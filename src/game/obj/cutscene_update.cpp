// Cutscenes: reading the objects table, the clock, the objects' transforms and the details' constructor and
// destructor (0x00465510-0x0046661A). See cutscene.h.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/ass/resource_registry.h"
#include "game/gfx/d3dx.h"
#include "game/core/mem.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/model.h"
#include "game/gfx/rig.h"
#include "game/mth/mat4.h"
#include "game/net/buddy_status.h"
#include "game/obj/cutscene.h"
#include "game/obj/matrix_component.h"
#include "game/obj/model_component.h"
#include "game/obj/node_objects.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/sys/game_config.h"
#include "game/core/stubs.h"
#include "game/gfx/viewport.h"

namespace GameObjects {

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
    return memory ? static_cast<Engine3D::CFrameSource*>(memory)->MakeNew() : nullptr;
}
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
void* ModelOf(CObjectModule* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
// The model's fader (cutscene.h) fading to `target` by 0.02 a frame.
void* FaderOf(void* model) { return ModelFader(model); }
void FadeModel(void* model, float target)
{
    void* fader = FaderOf(model);
    static_cast<Engine3D::CModelLights*>(fader)->MoveBrightness(target, 0.02f);
}
uint8_t* Bytes(void* object)
{
    return static_cast<uint8_t*>(object);
}
// An object's two matrices (+0x6C and +0xC0) set to `matrix`.
void SetMatrices(CGameEntity* object, const float* matrix)
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
bool __stdcall ObjectsFromTable(Scripts::CStruct* params, const uint32_t* table)
{
    params->PutInt(kVersion, static_cast<int>(table[0]));
    int count = static_cast<int>(table[1]);
    const uint32_t* entry = table + 2;
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* objects = memory ? memory->PrepareAll() : nullptr;
    objects->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_STRUCT);
    for (int i = 0; i < count; ++i, entry += 5) {
        Scripts::CStruct* block = Scripts::NewStruct();
        Scripts::CStruct* object = block ? block->PrepareAll() : nullptr;
        object->PutNameKey(kName, entry[0]);
        object->PutNameKey(kModel, entry[1]);
        object->PutNameKey(kSkeleton, entry[2]);
        object->PutNameKey(kAnim, entry[3]);
        uint32_t flags = entry[4];
        if (flags >> 30 & 1)
            object->PutNameKey(kUnnamed, kSkaterFlag);
        if (flags >> 31 & 1)
            object->PutNameKey(kUnnamed, kSkaterFlag2);
        objects->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(object));
    }
    params->AppendArrayRef(kObjects, objects);
    return true;
}

// 0x004656E0
void CCinematicTable::UpdateTime(int /*unused*/)
{
    m_unknown3478 = *reinterpret_cast<uint32_t*>(&m_clock);
    if (Scripts::FetchIntValue(kFixedTime) != 0) {
        m_clock = static_cast<float>(g_cutsceneFixedFrame * static_cast<double>(kFrame));
    } else if (m_unknown3474) {
        // a fixed step a frame (looping back at the end)
        double time = static_cast<double>(m_unknown3470) * kStep + m_clock;
        m_clock = static_cast<float>(time);
        if (m_unknown348C > 0.0f && !(time < m_unknown348C) && !std::isnan(time)) {
            m_clock = *reinterpret_cast<float*>(&m_unknown3488);
            m_unknown3478 = 0;
            Scripts::LaunchScript(kEndScript, nullptr, nullptr, false, nullptr);
            Scripts::LaunchScript(kEndScript2, nullptr, nullptr, false, nullptr);
        }
    } else {
        int rate = GameConfig::g_frameRate;
        uint64_t vblanks = FrameClock::FetchRetraceCount() - m_vblanks - m_pausedVblanks;
        m_clock = VblankSeconds(vblanks, rate);
    }
    if (!m_flag34B0)
        return;
    // the camera and object animations come in parts: the next part when this one's over
    float length = *static_cast<float*>(mp_cameraAnim);
    if (!(m_clock >= static_cast<double>(kPartEnd) + length)) { // (unordered too)
        m_cameraStart = static_cast<float>(m_clock / (static_cast<double>(length) + kFrame) * kFrame);
        return;
    }
    m_unknown3480 = m_clock;
    m_cameraStart = 0.0f;
    if (m_cameraPart < m_cameraParts) {
        ++m_cameraPart;
        uint32_t name = HashOfName("_CAM", m_cameraPart); // (the game's string 0x00647914)
        if (m_cameraSource)
            DeleteVirtual(m_cameraSource);
        m_cameraSource = NewAnimSource();
        static_cast<Engine3D::CFrameSource*>(m_cameraSource)->SetDone(true);
        mp_cameraAnim = static_cast<Engine3D::CFrameSource*>(m_cameraSource)->PlayClip(name);
        static_cast<Gfx2D::CJointClipFrames*>(mp_cameraAnim)->ArmEvents();
    }
    if (m_objectPart < m_objectParts) {
        ++m_objectPart;
        uint32_t name = HashOfName("_OBA", m_objectPart); // (the game's string 0x0064790C)
        if (m_objectSource)
            DeleteVirtual(m_objectSource);
        m_objectSource = NewAnimSource();
        mp_animList = static_cast<Engine3D::CFrameSource*>(m_objectSource)->PlayClip(name);
        static_cast<Gfx2D::CJointClipFrames*>(mp_animList)->ArmEvents();
    }
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_entities[i];
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
            static_cast<Engine3D::CFrameSource*>(entry.animation->source)->PlayClip(anim);
            static_cast<Engine3D::CFrameSource*>(entry.animation->source)->SetDone(true);
        }
    }
}

// 0x00465AB0
void CCinematicTable::UpdateStandIn(CGameEntity* object)
{
    if (CObjectModule* component = object->FetchModule(kModelComponent)) {
        void* model = ModelOf(component);
        if (*(Bytes(model) + 0x44) & 4) // (hidden)
            return;
        if (Bytes(component)[0x234]) {
            // the level object it stands in for: faded out, moved along
            auto* level = static_cast<CGameEntity*>(
                FindObject(static_cast<CMeshInstanceModule*>(component)->GetLinkedEntityName()));
            CObjectModule* levelComponent = level->FetchModule(kModelComponent);
            void* levelModel = ModelOf(levelComponent);
            FadeModel(levelModel, 0.0f);
            static_cast<CMeshInstanceModule*>(levelComponent)->UpdateBrightness();
            CObjectModule* skeleton = object->FetchModule(kSkeletonComponent);
            float matrix[16];
            *reinterpret_cast<Maths::Mat4*>(matrix) = *reinterpret_cast<const Maths::Mat4*>(Bytes(object) + 0xC0);
            std::memcpy(matrix + 12, Bytes(object) + 0x4C, 16);
            static_cast<Engine3D::CRenderModel*>(levelModel)->Render(
                reinterpret_cast<Maths::Mat4*>(matrix), false,
                reinterpret_cast<uint8_t*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(skeleton)->Word18()));
            std::memcpy(Bytes(level) + 0x4C, Bytes(object) + 0x4C, 16);
        } else {
            if (FaderOf(model)) {
                FadeModel(ModelOf(component), 1.0f);
                static_cast<CMeshInstanceModule*>(component)->UpdateBrightness();
            }
            reinterpret_cast<void(__thiscall*)(CObjectModule*)>((*reinterpret_cast<void***>(component))[1])(
                component);
        }
    }
    if (CObjectModule* matrix = object->FetchModule(kMatrixComponent))
        static_cast<CMatrixComponent*>(matrix)->ChangeMat(reinterpret_cast<const Maths::Mat4*>(Bytes(object) + 0xC0));
}

// 0x00465C00
void CCinematicTable::UpdateObjectTransforms()
{
    if (!m_objectSource)
        return;
    static_cast<Gfx2D::CJointClipFrames*>(mp_animList)->GetFloatKeyPose(
        Rotations(), Translations(), g_cutsceneCameraSlot,
        static_cast<float>(m_clock - static_cast<double>(m_cameraStart)),
        static_cast<int>(reinterpret_cast<uint32_t>(m_objectSource)));
    // the animated objects of the level (not the cutscene's own)
    for (int j = 0; j < *reinterpret_cast<int*>(Bytes(mp_animList) + 8); ++j) {
        uint32_t name = static_cast<Gfx2D::CJointClipFrames*>(mp_animList)->FetchJointName(j);
        bool own = false;
        for (int k = m_objectCount - 1; k >= 0; --k) {
            if (m_entities[k].name == name)
                own = true;
        }
        if (own)
            continue;
        auto* object = static_cast<CGameEntity*>(FindObject(name));
        if (!object)
            continue;
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float matrix[16];
        QuatToMatrix(Rotations() + 4 * j, origin, matrix);
        SetMatrices(object, matrix);
        std::memcpy(Bytes(object) + 0x4C, Translations() + 4 * j, 16);
    }
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_entities[i];
        CGameEntity* object = entry.object;
        if (CGameEntity* parent = entry.parent) {
            // hanging from the parent's bone 0x5A0E0860
            auto* skeleton = reinterpret_cast<void*>(
                reinterpret_cast<OnlinePlay::FoldedGetters*>(parent->FetchModule(kSkeletonComponent))->Word18());
            float bone[4], turned[4];
            static_cast<Gfx2D::CRig*>(skeleton)->GetBonePosition(kAttachBone, reinterpret_cast<Maths::Vec4*>(bone));
            *reinterpret_cast<Maths::Vec4*>(turned) =
                *reinterpret_cast<const Maths::Vec4*>(bone) * *reinterpret_cast<const Maths::Mat4*>(Bytes(parent) + 0xC0);
            const float* offset = turned;
            const float* at = reinterpret_cast<const float*>(Bytes(parent) + 0x4C);
            auto* position = reinterpret_cast<float*>(Bytes(object) + 0x4C);
            position[2] = static_cast<float>(static_cast<double>(at[2]) + offset[2]);
            position[3] = static_cast<float>(static_cast<double>(at[3]) + offset[3]);
            position[0] = static_cast<float>(static_cast<double>(offset[0]) + at[0]);
            position[1] = static_cast<float>(static_cast<double>(at[1]) + offset[1]);
            position[3] = 1.0f;
            float jointMat[16];
            static_cast<Gfx2D::CRig*>(skeleton)->ReadJointMatrix(kAttachBone, reinterpret_cast<Maths::Mat4*>(jointMat));
            float ownerMat[16];
            std::memcpy(ownerMat, Bytes(parent) + 0xC0, 64);
            std::memcpy(ownerMat + 12, at, 16);
            ownerMat[3] = 0.0f;
            ownerMat[7] = 0.0f;
            ownerMat[11] = 0.0f;
            D3dx::MatrixMultiply(jointMat, jointMat,
                                                                                         ownerMat);
            float matrix[16];
            std::memcpy(matrix, jointMat, 48);
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
        if (Scripts::FetchIntValue(kDebugBoxes)) {
            const float box[8] = {1.0f, 10.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, 1.0f};
            DebugDrawBox(Bytes(object) + 0xC0, Bytes(object) + 0x4C, box, 0, 1, 0, 0xFF0000C8);
        }
    }
}

// 0x00466450
void CCinematicTable::RestartObjects()
{
    if (m_flag34B0)
        return;
    m_clock = 0.0f;
    m_unknown3478 = 0;
    DestroyObjects();
    AddObjects(mp_params);
}

// 0x00466490
CCinematicEntry* CCinematicEntry::MakeNew(const uint32_t* settings)
{
    reinterpret_cast<CVideoEntry*>(this)->MakeNew();
    m_vtable = kCutsceneDetailsVtable;
    m_content = nullptr;
    m_asset = 0;
    Scripts::CStruct* block = Scripts::NewStruct();
    m_args = block ? block->PrepareAll() : nullptr;
    std::memcpy(m_settings, settings, 12);
    m_ownsData = static_cast<uint8_t>(m_settings[2] & 0xFF);
    m_flag5D = false;
    m_unknown60 = -1;
    return this;
}

// 0x00466560
void CCinematicEntry::DestroyBody()
{
    m_vtable = kCutsceneDetailsVtable;
    if (Scripts::CStruct* params = m_args) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    if (m_ownsData) {
        if (m_content) {
            auto* assets = AssetStore::g_assetManager;
            AssetStore::CResource* asset = assets->ObtainLoadedItem(m_asset, 1);
            assets->DiscardLoadedItem(asset);
            m_content = nullptr;
        }
        Engine3D::SetAmbientLightModulationFactor(m_musicVolume, 0.0f);
        Engine3D::SetDiffuseLightModulationFactor(0, m_streamVolume0, 0.0f);
        Engine3D::SetDiffuseLightModulationFactor(1, m_streamVolume1, 0.0f);
    }
    reinterpret_cast<CVideoEntry*>(this)->DestroyBody();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kCutsceneDetailsVtable_slots[] = {&GameObjects::CCinematicEntry::Teardown,
         &GameObjects::CCinematicEntry::ConfigureFromData,
         &GameObjects::CCinematicEntry::TickFrame,
         Stubs::ReturnTrue,
         &Engine3D::CViewRegion::QueryPoint,
         &GameObjects::CCinematicEntry::IsFinished,
         Stubs::ReturnFalse,
         &GameObjects::CCinematicEntry::IsReady,
         &GameObjects::CCinematicEntry::End,
         &GameObjects::CCinematicEntry::CanBeSkipped,
         Stubs::ReturnFalse,
         &GameObjects::CCinematicEntry::CallSlot12WithNotFlag5D,
         Stubs::IgnoreArgument,
         &GameObjects::CCinematicEntry::UpdateSlot12};
void** const kCutsceneDetailsVtable = reinterpret_cast<void**>(kCutsceneDetailsVtable_slots);

} // namespace
} // namespace Obj
