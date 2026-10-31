// The model viewer's object (0x005897B0-0x0058AFB7). See viewer_object.h.

#include "game/sys/viewer_object.h"
#include "game/gfx/d3dx.h"
#include "game/net/task_vtable.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/string.h"
#include "game/core/task.h"
#include "game/gfx/engine.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/movie_cfuncs.h"
#include "game/script/cscript.h"
#include "game/script/load.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/sys/viewer.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/object.h"
#include "game/script/cfuncs_scene.h"

namespace GameObjects {

namespace {

extern void** const kViewerObjectVtable;
constexpr uint32_t kKillViewerObjectPanel = 0x9E65A71E; // kill_viewer_object_panel
constexpr uint32_t kCasArtist = 0x2A648514;              // cas_artist
constexpr uint32_t kModelName = 0xB974F2FC;              // modelname
constexpr uint32_t kModel = 0x286A8D26;                  // model
constexpr uint32_t kProfile = 0x7EA855F0;                // profile
constexpr uint32_t kSkeletonFileName = 0xF679DB13;       // skeletonFileName
constexpr uint32_t kSkeletonName = 0x09794932;           // skeletonName
constexpr uint32_t kViewerObjPos = 0x15670AB7;           // ViewerObjPos
constexpr uint32_t kAddComponents = 0x24C71BC7;          // viewerobj_add_components
constexpr uint32_t kInitModel = 0xBBAB79DE;              // viewerobj_init_model
constexpr uint32_t kIdle = 0x23DB4AEA;                   // idle
constexpr int kSkeletonAsset = 6;                        // (the asset type)

// `path` set to a model's file path: "models\<name>\<name><extension>", or "models\<name>" when the name holds a path
// separator (0x004879D0, in the CAS code - not rewritten yet - makes it as a temporary String).
void SetModelPath(TextUtil::String& path, const char* name, const char* extension)
{
    alignas(TextUtil::String) uint8_t temporary[sizeof(TextUtil::String)];
    Gfx2D::MakeModelPath(reinterpret_cast<TextUtil::String*>(temporary), name, extension);
    path = *reinterpret_cast<TextUtil::String*>(temporary);
    reinterpret_cast<TextUtil::String*>(temporary)->~String();
}

// `path` without its .skin / .mdl part, with `extension` (".col" / ".tex") on the end (0x0048BFE0 cuts the path).
void SiblingFile(const char* path, const char* extension, char (&out)[256])
{
    Engine3D::Engine::SplitSkinName(path, out);
    strcat_s(out, extension);
}

// A CAS artist's cached copy of `file` dropped, so it's read again from the edited file.
void DropCachedFile(const char* file)
{
    if (!Scripts::FetchIntValue(kCasArtist))
        return;
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    if (AssetStore::CResource* asset = assets->ObtainLoadedItem(Checksums::HashFromText(file), 0))
        assets->DiscardLoadedItem(asset);
}

// Model `name`'s .col, .skin, .mdl and .tex files dropped (in that order).
void DropCachedModel(const char* name)
{
    TextUtil::String path;
    SetModelPath(path, name, ".skin");
    char sibling[256];
    SiblingFile(path.c_str(), ".col", sibling);
    DropCachedFile(sibling);
    DropCachedFile(path.c_str());
    SetModelPath(path, name, ".mdl");
    DropCachedFile(path.c_str());
    SiblingFile(path.c_str(), ".tex", sibling);
    DropCachedFile(sibling);
}

CAnimBlenderComponent* Animation(CGameEntity* object)
{
    return static_cast<CAnimBlenderComponent*>(object->GetComponent(CAnimBlenderComponent::kType));
}

// The same, for its virtual member functions (vtable +0x30).
CObjectModule* AnimationBase(CGameEntity* object) { return object->GetComponent(CAnimBlenderComponent::kType); }

// More names.
constexpr uint32_t kSpeed = 0xF0D90109;                  // speed
constexpr uint32_t kSetSpeed = 0xBD4EDD44;               // (the animation component's Speed member function)
constexpr uint32_t kStep = 0x6AAEB76F;                   // (its step member function: frames)
constexpr uint32_t kFrames = 0x06C9F278;
constexpr uint32_t kReverse = 0xF5E2B871;                // (its reverse member function)
constexpr uint32_t kSkeleton = 0x222756D5;               // skeleton (the component)
constexpr uint32_t kMaxWheelSpeed = 0xCACA33A4;          // max_wheel_speed
constexpr uint32_t kLine1 = 0x282C53D5;                  // line1
constexpr uint32_t kLine2 = 0xB125026F;                  // line2
constexpr uint32_t kLine3 = 0xC62232F9;                  // line3
constexpr uint32_t kLine4 = 0x5846A75A;                  // textRow4
constexpr uint32_t kDrawViewerObjectPanel = 0xED9C1839;  // draw_viewer_object_panel
constexpr uint32_t kViewerAutoRepeatTime = 0xE60442D5;   // ViewerAutoRepeatTime
constexpr uint32_t kViewerAutoRepeatSpeed = 0x2DA212E5;  // ViewerAutoRepeatSpeed
constexpr uint32_t kObjectTypeComposite = 0xCDEB9229;    // OBJECT_TYPE_COMPOSITE
const float kFrameTime = std::bit_cast<float>(0x3C888889u);              // 1/60 s
constexpr float kSpeedStep = 0.25f;                                      // 0x0064F130
constexpr float kDegreesToRadians = 0.017453292f;                        // 0x00645464

// A pooled script structure, and one cleared and freed (either may be null).
Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// D3DXMatrixMultiply (library code: stdcall), out = a * b.
void MultiplyMatrix(Maths::Matrix* out, const Maths::Matrix* a, const Maths::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The model component's model (+0x208), its bone count (0x00492FF0) and hierarchy (0x00493040: objects of 0x50 bytes,
// each with its matrix at +0x10) - Engine3D::CRenderModel, not rewritten yet; the skeleton component's skeleton (the folded
// getter 0x005480F0: +0x18) and the skeleton's bone matrices (0x005480C0: +0x04).
uint8_t* ModelOf(CObjectModule* component)
{
    return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
int BoneCount(uint8_t* model) { return static_cast<int>(reinterpret_cast<Engine3D::CRenderModel*>(model)->GetAssetBoneCount()); }
uint8_t* HierarchyOf(uint8_t* model) { return reinterpret_cast<uint8_t*>(reinterpret_cast<Engine3D::CRenderModel*>(model)->GetAssetHierarchy()); }
uint8_t* SkeletonOf(CObjectModule* component)
{
    return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x18);
}
Maths::Matrix* BoneMatrices(uint8_t* skeleton) { return *reinterpret_cast<Maths::Matrix**>(skeleton + 0x04); }

uint32_t g_noSkeletonCounter{10};
int g_repeatCount;
uint32_t g_repeatCountGuard;
float g_wheelAngle;

// The wheels' angle (degrees) and the counter that moves on while there's no skeleton.
float& WheelAngle() { return g_wheelAngle; }
uint32_t& NoSkeletonCounter() { return g_noSkeletonCounter; }
// The input handler's auto-repeat count (a function static: its guard is bit 1 of 0x007CE58C).
int& RepeatCount() { return g_repeatCount; }
uint32_t& RepeatCountGuard() { return g_repeatCountGuard; }
uint8_t* InputManager() { return reinterpret_cast<uint8_t*>(PadInput::g_inputManager); } // PadInput::Manager (0x84 bytes a controller)

CPreviewEntity* ObjectOf(NetLink::InboundMsgCtx* context) { return static_cast<CPreviewEntity*>(context->m_data); }

} // namespace

// 0x005897B0
void CPreviewEntity::ShutDown() { Scripts::LaunchScript(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr); }

// 0x005897D0
void CPreviewEntity::PlayAnimation(uint32_t anim)
{
    // (The original looks the component up again for each call.)
    if (!Animation(this) || !Animation(this)->HasValue(anim))
        return;
    const float length = Animation(this)->GetValue(anim);
    Animation(this)->PlayAnimationEx(anim, 0, 0, std::bit_cast<uint32_t>(length), 1, std::bit_cast<uint32_t>(0.3f),
                                     std::bit_cast<uint32_t>(1.0f));
}

// 0x00589860
void CPreviewEntity::DestroyBody()
{
    m_vtable = kViewerObjectVtable;
    m_task->Remove(); // (its input handler; not null-checked: as the original)
    if (m_asset) {
        AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
        if (AssetStore::CResource* asset = assets->ObtainLoadedItem(m_asset, 0)) {
            assets->DiscardLoadedItem(asset);
            assets->ObtainLoadedItem(m_asset, 0);
        }
        m_asset = 0;
    }
    if (m_task)
        reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(m_task))[2])(m_task, 1);
    CGameEntity::DestroyBody();
}

// 0x0058A2B0
CPreviewEntity* CPreviewEntity::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

Maths::Vector g_originPosition{0.0f, 0.0f, 0.0f, 1.0f};

// 0x00589910
void CPreviewEntity::Load(Scripts::CStruct* params)
{
    CompositeObjectManager()->VirtualRegisterObject(this);
    AddMotionComponents(this);
    AddNodeComponents(this, params);
    MemSys::Manager* memory = MemSys::Manager::Get();
    memory->EnterHeapScope(ScriptCommands::g_gotExtraMemory ? static_cast<MemSys::Allocator*>(memory->m_debugHeap)
                                                        : static_cast<MemSys::Allocator*>(memory->m_bottomUpHeap));
    const char* name;
    for (int i = 0; i < 4; ++i) {
        char key[260];
        std::snprintf(key, sizeof(key), "model%d", i);
        if (params->GetString(key, &name, 0))
            DropCachedModel(name);
    }
    if (params->GetString(kModelName, &name, 0) || params->GetString(kModel, &name, 0)) {
        DropCachedModel(name);
    } else {
        Scripts::CStruct* profile;
        params->ReadSubStruct(kProfile, &profile, 0); // (not used)
    }
    const char* skeletonFile;
    if (params->GetString(kSkeletonFileName, &skeletonFile, 0)) {
        uint32_t skeleton; // (left unset if skeletonName is missing: as the original)
        params->ReadHashValue(kSkeletonName, &skeleton, 1);
        AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
        if (!assets->ObtainLoadedItem(skeleton, 0)) {
            assets->LoadAsset(skeletonFile, kSkeletonAsset, skeleton, 0, false, false, false, nullptr, 0);
            m_asset = skeleton;
        }
    }
    Scripts::VectorValue position;
    if (params->GetVector(kViewerObjPos, &position, 0)) {
        m_pos.x = position.x;
        m_pos.y = position.y;
        m_pos.z = position.z;
        m_pos.w = position.w;
    } else {
        m_pos = g_originPosition; // (0, 0, 0, 1)
    }
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&m_basisFrame, kIdentity, sizeof(kIdentity));
    Scripts::LaunchScript(kAddComponents, params, this, false, nullptr);
    reinterpret_cast<void(__thiscall*)(CPreviewEntity*)>(m_vtable[0x20 / 4])(this); // Finalize
    Scripts::LaunchScript(kInitModel, params, this, false, nullptr);
    PlayAnimation(kIdle);
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x00589FD0
void CPreviewEntity::ReloadAnimation(const char* file, uint32_t anim)
{
    if (!Animation(this))
        return;
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    uint32_t name = Checksums::HashFromText(file);
    if (anim)
        name = Animation(this)->GetAnimSet() + anim;
    if (AssetStore::CResource* asset = assets->ObtainLoadedItem(name, 0))
        assets->DiscardLoadedItem(asset);
    assets->LoadAnim(file, anim, Animation(this)->GetAnimSet(), false, false);
    PlayAnimation(anim);
}

// 0x0058A2E0
void CPreviewEntity::SetRotation(float z, float x)
{
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&m_basisFrame, kIdentity, sizeof(kIdentity));
    Maths::Matrix rotation;
    MultiplyMatrix(&m_basisFrame, &m_basisFrame, &Maths::MakeRollMatrix(rotation, z));
    MultiplyMatrix(&m_basisFrame, &m_basisFrame, &Maths::MakePitchMatrix(rotation, x));
}

// 0x0058A360
void CPreviewEntity::ScaleAnimRate(float speed)
{
    m_speed = speed;
    if (speed > 1.0f)
        m_speed = 1.0f;
    if (m_speed < 0.0f)
        m_speed = 0.0f;
    if (Animation(this)) {
        Animation(this)->FirstChannel47E850(m_speed, 0.0f, 0.0f, 0.0f);
        Scripts::CStruct* params = NewStructure();
        params->PushFloat(kSpeed, m_speed);
        AnimationBase(this)->InvokeMethodByName(kSetSpeed, params, nullptr);
        DeleteStructure(params);
    }
    m_isHalted = false;
}

// 0x0058A4A0
void CPreviewEntity::StepAnimation(bool forward)
{
    if (!m_isHalted) {
        m_savedSpeed = m_speed;
        ScaleAnimRate(0.0f);
    }
    if (Animation(this)) {
        // (The original looks the component up again for each call.)
        if (forward) {
            Animation(this)->PassToEachChannel(std::bit_cast<uint32_t>(kFrameTime));
            Animation(this)->VisitChannels();
            Scripts::CStruct* params = NewStructure();
            params->PushFloat(kFrames, kFrameTime);
            AnimationBase(this)->InvokeMethodByName(kStep, params, nullptr);
            DeleteStructure(params);
        } else {
            // Reversed for a step forward, then turned round again.
            Animation(this)->FirstChannel47E810(0);
            Animation(this)->PassToEachChannel(std::bit_cast<uint32_t>(-kFrameTime));
            Animation(this)->VisitChannels();
            Animation(this)->FirstChannel47E810(0);
            AnimationBase(this)->InvokeMethodByName(kReverse, nullptr, nullptr);
            Scripts::CStruct* params = NewStructure();
            params->PushFloat(kFrames, -kFrameTime);
            AnimationBase(this)->InvokeMethodByName(kStep, params, nullptr);
            DeleteStructure(params);
            AnimationBase(this)->InvokeMethodByName(kReverse, nullptr, nullptr);
        }
        Animation(this)->FirstChannel47EFF0();
    }
    m_isHalted = true;
}

// 0x0058A720
void CPreviewEntity::SpinWheels()
{
    CObjectModule* model = GetComponent(kModel);
    if (!model)
        return;
    uint8_t* nxModel = ModelOf(model);
    const uint8_t* hierarchy = HierarchyOf(nxModel);
    if (!GetComponent(kSkeleton)) {
        NoSkeletonCounter() = (NoSkeletonCounter() + 1) & 0x1F;
        return;
    }
    uint8_t* skeleton = SkeletonOf(GetComponent(kSkeleton));
    // The angle kept in double precision through the wrap (the original's x87 register), stored as a float each time.
    double angle = static_cast<double>(Scripts::GetFloat(kMaxWheelSpeed)) * m_speed + WheelAngle();
    WheelAngle() = static_cast<float>(angle);
    if (angle > 360.0) {
        do
            angle -= 360.0;
        while (angle > 360.0);
        WheelAngle() = static_cast<float>(angle);
    }
    if (angle < 0.0) {
        do
            angle += 360.0;
        while (angle < 0.0);
        WheelAngle() = static_cast<float>(angle);
    }
    if (BoneCount(nxModel) <= 5)
        return;
    // The bones from the model's hierarchy, the wheels (2-5) turned about x, and all but the first put under it.
    Maths::Matrix* bones = BoneMatrices(skeleton);
    for (int i = 0; i < BoneCount(nxModel); ++i)
        bones[i] = *reinterpret_cast<const Maths::Matrix*>(hierarchy + 0x10 + i * 0x50);
    for (int wheel = 2; wheel < 6; ++wheel) {
        Maths::Matrix spin;
        const float radians = static_cast<float>(static_cast<double>(WheelAngle()) * kDegreesToRadians);
        MultiplyMatrix(&bones[wheel], &Maths::MakePitchMatrix(spin, radians), &bones[wheel]);
    }
    for (int bone = 1; bone < 6; ++bone)
        MultiplyMatrix(&bones[bone], &bones[bone], &bones[0]);
}

// 0x0058A8B0
void CPreviewEntity::UpdatePanel()
{
    if (Animation(this) && m_panelShown) {
        const uint32_t anim = Animation(this)->GetTimeline30();
        if (Animation(this)->HasValue(anim)) {
            Scripts::CStruct* params = NewStructure();
            char line1[128], line2[128], line3[128], textRow4[128];
            std::snprintf(line1, sizeof(line1), "name %s", "Unknown");
            params->AddString(kLine1, line1);
            std::snprintf(line2, sizeof(line2), "speed %f", m_isHalted ? 0.0 : static_cast<double>(m_speed));
            params->AddString(kLine2, line2);
            // (Each value looked up again for each line, the length before the time.)
            uint32_t current = Animation(this)->GetTimeline30();
            float length = Animation(this)->GetValue(current);
            float time = Animation(this)->GetTimeline18();
            std::snprintf(line3, sizeof(line3), "time %f of %f", static_cast<double>(time), static_cast<double>(length));
            params->AddString(kLine3, line3);
            current = Animation(this)->GetTimeline30();
            length = Animation(this)->GetValue(current);
            const int lastFrame = static_cast<int>(static_cast<double>(length) * 60.0);
            time = Animation(this)->GetTimeline18();
            const int frame = static_cast<int>(static_cast<double>(time) * 60.0);
            std::snprintf(textRow4, sizeof(textRow4), "frame %d of %d", frame, lastFrame);
            params->AddString(kLine4, textRow4);
            Scripts::LaunchScript(kDrawViewerObjectPanel, params, nullptr, false, nullptr);
            DeleteStructure(params);
        }
    }
    CObjectModule* model = GetComponent(kModel);
    if (model && BoneCount(ModelOf(model)))
        SpinWheels();
}

// 0x0058AB30
void CPreviewEntity::OwnUpdate()
{
    if (m_actions & kCommandTogglePanel) {
        m_panelShown = !m_panelShown;
        Scripts::LaunchScript(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr);
    }
    if (m_actions & kCommandFaster)
        ScaleAnimRate((m_isHalted ? m_savedSpeed : m_speed) + kSpeedStep);
    if (m_actions & kCommandSlower)
        ScaleAnimRate((m_isHalted ? m_savedSpeed : m_speed) - kSpeedStep);
    if (m_actions & kCommandStepForward)
        StepAnimation(true);
    if (m_actions & kCommandStepBack)
        StepAnimation(false);
    // The rest are the viewer's (if there is one).
    static const struct {
        uint32_t command;
        uint32_t viewerCommand;
    } kViewerCommands[] = {
        {kCommandToggleMode, 0x4000},
        {kCommandCameraUp, GameModules::Viewer::kCommandShoulderA},
        {kCommandCameraDown, GameModules::Viewer::kCommandShoulderB},
        {kCommandRollLeft, 0x10000},
        {kCommandRollRight, 0x20000},
    };
    for (const auto& c : kViewerCommands) {
        if (m_actions & c.command) {
            if (GameModules::Viewer* viewer = GameModules::GetLiveViewer())
                viewer->AddCommands(c.viewerCommand);
        }
    }
    UpdatePanel();
    m_actions = 0;
}

// 0x0058A060
void __cdecl HandleViewerObjectInput(ViewerObjectInputHandler* handler)
{
    CPreviewEntity* object = handler->m_data;
    const int delay = Scripts::FetchIntValue(kViewerAutoRepeatTime);
    const int rate = Scripts::FetchIntValue(kViewerAutoRepeatSpeed);
    int count; // (the static starts as the first call's delay)
    if (!(RepeatCountGuard() & 1)) {
        RepeatCountGuard() |= 1;
        count = delay;
    } else {
        count = RepeatCount();
    }
    if (handler->m_controlData->m_makes & 0x10)
        object->m_actions |= CPreviewEntity::kCommandTogglePanel;
    // A frame back / on when Left / Right is pressed, and while it's held: after `delay` frames, then every `rate`.
    const PadInput::Data* data = handler->m_controlData;
    if (data->m_makes & PadInput::Data::kButtonLeft) {
        object->m_actions |= CPreviewEntity::kCommandStepBack;
        RepeatCount() = delay;
    } else if (data->m_makes & PadInput::Data::kButtonRight) {
        object->m_actions |= CPreviewEntity::kCommandStepForward;
        RepeatCount() = delay;
    } else if (data->m_buttons & PadInput::Data::kButtonLeft) {
        RepeatCount() = --count;
        if (count <= 0) {
            object->m_actions |= CPreviewEntity::kCommandStepBack;
            RepeatCount() = rate;
        }
    } else if (data->m_buttons & PadInput::Data::kButtonRight) {
        RepeatCount() = --count;
        if (count <= 0) {
            object->m_actions |= CPreviewEntity::kCommandStepForward;
            RepeatCount() = rate;
        }
    } else {
        RepeatCount() = delay;
    }
    if (handler->m_controlData->m_makes & 0x200)
        object->m_actions |= CPreviewEntity::kCommandToggleMode;
    const uint32_t buttons = handler->m_controlData->m_buttons;
    if (buttons & 0x8) {
        if (!(buttons & 0x2))
            object->m_actions |= CPreviewEntity::kCommandCameraUp;
    } else if (buttons & 0x2) {
        object->m_actions |= CPreviewEntity::kCommandCameraDown;
    }
    const uint32_t makes = handler->m_controlData->m_makes;
    if (makes & 0x4)
        object->m_actions |= CPreviewEntity::kCommandFaster;
    else if (makes & 0x1)
        object->m_actions |= CPreviewEntity::kCommandSlower;
    if (!object->GetComponent(CAnimBlenderComponent::kType)) {
        const uint32_t held = handler->m_controlData->m_buttons;
        if (held & 0x20)
            object->m_actions |= CPreviewEntity::kCommandRollLeft;
        else if (held & 0x80)
            object->m_actions |= CPreviewEntity::kCommandRollRight;
    }
}

// 0x00589840
int __cdecl HandlePlayAnimation(NetLink::InboundMsgCtx* context)
{
    ObjectOf(context)->PlayAnimation(*reinterpret_cast<const uint32_t*>(context->m_body));
    return 1;
}

// 0x0058ACA0
int __cdecl HandleSetAnimSpeed(NetLink::InboundMsgCtx* context)
{
    ObjectOf(context)->ScaleAnimRate(*reinterpret_cast<const float*>(context->m_body));
    return 1;
}

// 0x0058ACC0
int __cdecl HandleStepAnimation(NetLink::InboundMsgCtx* context)
{
    ObjectOf(context)->StepAnimation(*reinterpret_cast<const bool*>(context->m_body)); // (the byte as it is)
    return 1;
}

// 0x0058A1F0
int __cdecl HandleReloadAnimation(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    ObjectOf(context)->ReloadAnimation(message, *reinterpret_cast<const uint32_t*>(message + 0x80));
    return 1;
}

// 0x0058ACE0
int __cdecl HandleReloadCamAnimation(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    Scripts::CStruct* params = NewStructure();
    params->AddString(0u, message);
    params->PutNameKey(0u, *reinterpret_cast<const uint32_t*>(message + 0x80));
    ScriptCommands::CFunc_ReloadSkaterCamAnim(params, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x0058A210
int __cdecl HandleRunAnimViewScript(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    CPreviewEntity* object = ObjectOf(context);
    char path[260];
    std::snprintf(path, sizeof(path), "scripts\\animview\\%s.qb", message);
    Scripts::LoadQB(path, 0, 0);
    Scripts::CScript* script = Scripts::SpawnScript(message, nullptr, 0, nullptr, 0, 0, false, false, false, false);
    script->m_object.Set(object); // (not null-checked: as the original)
    return 1;
}

// 0x0058ADB0
CPreviewEntity* CPreviewEntity::Construct(NetLink::App* app)
{
    CGameEntity::Construct();
    m_vtable = kViewerObjectVtable;
    m_actions = 0;
    AddBaseComponents(this);
    SetType(kObjectTypeComposite);
    uint8_t* input = InputManager();
    auto* handler = static_cast<ViewerObjectInputHandler*>(MemSys::AllocZeroed(sizeof(ViewerObjectInputHandler)));
    if (handler) {
        handler->Construct(0);
        handler->m_index = 0;
        handler->m_vtable = GameCode::kInputHandlerVtable;
        handler->m_code = &HandleViewerObjectInput;
        handler->m_data = this;
    }
    m_task = handler;
    // Onto its controller's handlers (not null-checked: as the original).
    uint8_t* controller = input + handler->m_index * 0x84;
    reinterpret_cast<TaskList::Stack*>(controller + 0x08)->AddTask(handler);
    handler->m_controlData = reinterpret_cast<PadInput::Data*>(controller + 0x44);
    handler->m_device = *reinterpret_cast<uint8_t**>(controller + 0x04);
    m_savedSpeed = 0.0f;
    m_panelShown = true;
    m_speed = 1.0f;
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&m_basisFrame, kIdentity, sizeof(kIdentity));
    m_isHalted = false;
    m_asset = 0;
    m_ownerApp = app;
    if (app) {
        NetLink::Dispatcher& dispatcher = app->m_dispatcher;
        dispatcher.AddHandler(0x25, &HandlePlayAnimation, 3, this, 0x80);
        dispatcher.AddHandler(0x26, &HandleSetAnimSpeed, 3, this, 0x80);
        dispatcher.AddHandler(0x27, &HandleStepAnimation, 3, this, 0x80);
        dispatcher.AddHandler(0x28, &HandleReloadAnimation, 3, this, 0x80);
        dispatcher.AddHandler(0x29, &HandleReloadCamAnimation, 3, this, 0x80);
        dispatcher.AddHandler(0x2C, &HandleRunAnimViewScript, 3, this, 0x80);
    }
    return this;
}

namespace {
static VtableSlot kViewerObjectVtable_slots[] = {&GameObjects::CPreviewEntity::Destroy, &GameObjects::CObject::SetProperties,
                static_cast<bool (GameObjects::CObject::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObject::InvokeMethodByName),
                static_cast<bool (GameObjects::CGameEntity::*)(uint32_t, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CGameEntity::InvokeMethodByName),
                Stubs::IgnoreArgument, &GameObjects::CObject::PassEventToScript, &GameObjects::CGameEntity::FlagAsDead,
                &GameObjects::CGameEntity::ShouldUpdateScripts, &GameObjects::CGameEntity::Finalize,
                &GameObjects::CGameEntity::CreateComponentFromStructure, &GameObjects::CPreviewEntity::OwnUpdate};
void** const kViewerObjectVtable = reinterpret_cast<void**>(kViewerObjectVtable_slots);
} // namespace

} // namespace Obj
