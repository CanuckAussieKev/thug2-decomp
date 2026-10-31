// Cutscenes: the data's constructor and the objects a cutscene adds (0x004645A0-0x00465502). See cutscene.h.

#include <cstdint>
#include <cstring>

#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/rig.h"
#include "game/maths/vec4.h"
#include "game/skater/rider.h"
#include "game/skate/skate_module.h"
#include "game/assets/resource_registry.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/net/buddy_status.h"
#include "game/obj/cutscene.h"
#include "game/obj/model_component.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/gfx/scene_load.h"
#include "game/obj/cutscene.h"

namespace GameObjects {

namespace {

constexpr uint32_t kUnnamed = 0; // (a flag: a checksum without a name)
constexpr uint32_t kModelModuleName = 0x286A8D26;
constexpr uint32_t kSkeletonModuleName = 0x222756D5;
constexpr uint32_t kFlagParam = 0x82C84419;
constexpr uint32_t kRefreshScript = 0x2E20EA32;     // (CreateObject's second script)
constexpr uint32_t kRefreshGlobal = 0x4AF50A5B;     // a global integer: refresh the objects once

// The objects' parameters (names unknown).
constexpr uint32_t kVersion = 0x40E32C3C;
constexpr uint32_t kObjects = 0x4DE5330C;
constexpr uint32_t kName = 0x2BFC2486;
constexpr uint32_t kModel = 0xB974F2FC;
constexpr uint32_t kKind = 0x7321A8D6;
constexpr uint32_t kSkeleton = 0x09794932;
constexpr uint32_t kAnim = 0x6C2BFB7F;
constexpr uint32_t kIsSkaterMarker = 0x723CDA8F;
constexpr uint32_t kSkaterFlag2 = 0x6914D9D2;
constexpr uint32_t kKeepSkeleton = 0x0CBEC8AC;
constexpr uint32_t kNoModelFile = 0x5B5D592E;
constexpr uint32_t kModelFile = 0x86BD5B8F;
constexpr uint32_t kMale = 0x3F813177;
constexpr uint32_t kMaleParam = 0x92B43E79;
constexpr uint32_t kPlayingCutsceneKey = 0x153A84DE;
constexpr uint32_t kSkeletonParam = 0xD3982061;
constexpr uint32_t kSkaterScript = 0xF37D9F53;
// (the model asset's name: the model checksum plus this)
constexpr uint32_t kModelFileOffset = 0xFD8697E1;

// The skater appearance parts asked about (names unknown).
constexpr uint32_t kPartA = 0x650FAB6D, kPartB = 0x0FC85BAE, kPartC = 0x6DF453B6, kHidden = 0xC4C5B2CC;

MemSys::PoolPair& StructPools() { return Scripts::g_structPools; }
MemSys::PoolPair& SourcePools() { return Engine3D::g_frameSourcePools; }

Scripts::CStruct* MakeStruct()
{
    void* memory = StructPools().Carve(8);
    return memory ? static_cast<Scripts::CStruct*>(memory)->PrepareAll() : nullptr;
}
void FreeStruct(Scripts::CStruct* s)
{
    if (!s)
        return;
    s->Clear();
    Scripts::DeleteStruct(s);
}

void* ModelOf(CObjectModule* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
bool StandsIn(CObjectModule* model)
{
    return *(reinterpret_cast<uint8_t*>(model) + 0x234) != 0;
}
uint32_t StandInId(CObjectModule* model)
{
    return static_cast<CMeshInstanceModule*>(model)->GetLinkedEntityName();
}
} // namespace

// (no address: the getters folded at 0x00576E60 / 0x00532EB0 - cutscene.h)
void* __fastcall ModelFader(void* model) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(model) + 0x2C); }
void* __fastcall AppearanceFace(void* appearance) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(appearance) + 0x54); }

namespace {

void* SkeletonOf(CObjectModule* component)
{
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    return reinterpret_cast<void*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(component)->Word18());
}
// The level object a cutscene object stands in for.
CGameEntity* LevelObjectOf(CGameEntity* object)
{
    return static_cast<CGameEntity*>(FindObject(StandInId(object->FetchModule(kModelModuleName))));
}
void ModelSetOffset(void* model, const float* offset)
{
    static_cast<Engine3D::CRenderModel*>(model)->ChangeBoundSphere(reinterpret_cast<const Maths::Vec4*>(offset));
}
// The skater's appearance (+0x34A8) and its parts.
void* AppearancePart(void* appearance, uint32_t part)
{
    return static_cast<Gfx2D::CModelLook*>(appearance)->GetPartOption(part);
}

} // namespace

// 0x004645A0
void* __cdecl AnimSourceAllocate(unsigned int size)
{
    return SourcePools().Carve(size);
}

// 0x004645E0
void __cdecl AnimSourceFree(void* item)
{
    SourcePools().Dealloc(item);
}

// 0x00464640
CCinematicTable* CCinematicTable::MakeNew()
{
    m_vtable = g_cutsceneDataVtable;
    m_bundlePtr = nullptr;
    m_objectPart = 0;
    m_cameraPart = 0;
    m_cameraParts = 0;
    m_cameraSource = nullptr;
    m_objectParts = 0;
    m_objectSource = nullptr;
    m_objectCount = 0;
    m_assetCount = 0;
    m_ready = false;
    m_unknown3470 = 1.0f;
    m_unknown3474 = 0;
    m_clock = 0.0f;
    m_unknown3480 = 0.0f;
    m_cameraStart = 0.0f;
    m_unknown348C = 0.0f;
    m_vblanks = 0;
    m_prevRetraces = 0;
    m_endTime = 0.0f;
    m_colourSaved = false;
    m_unknown34A8 = nullptr;
    m_unknown34AC = nullptr;
    m_subtitles = nullptr;
    m_nextSubtitle = 0;
    m_subtitleTime = 0.0f;
    m_nextSubtitleTime = -1.0f;
    m_callArgs = MakeStruct();
    return this;
}

// 0x00464780
void CCinematicTable::RefreshObjects()
{
    if (!m_flag34B0 || Scripts::FetchIntValue(kRefreshGlobal) == 0)
        return;
    Scripts::CStruct* params = MakeStruct();
    params->PutNameKey(kUnnamed, kFlagParam);
    for (int i = 0; i < m_objectCount; ++i) {
        CGameEntity* object = m_entities[i].object;
        if (object->FetchModule(kModelModuleName))
            Scripts::LaunchScript(kRefreshScript, params, object, false, nullptr);
    }
    auto* skater =
        static_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
    if (skater)
        Scripts::LaunchScript(kRefreshScript, params, skater, false, nullptr);
    FreeStruct(params);
    Scripts::FindSymbol(kRefreshGlobal)->integer = 0;
}

// 0x004648D0
void CCinematicTable::AddObjects(Scripts::CStruct* params)
{
    int version = 0;
    params->FetchIntValue(kVersion, &version, 1);
    Scripts::CArray* objects = nullptr;
    params->FetchList(kObjects, &objects, 1);
    bool wantsSkater = false; // (an object of kind 0x6218C3C9: script 0xF37D9F53 runs...)
    bool hasSkater = false;   // (...unless a skater object was added)
    for (uint32_t i = 0; i < objects->m_extent; ++i) {
        auto* object = reinterpret_cast<Scripts::CStruct*>(objects->FetchPart(i));
        Scripts::CStruct* params2 = MakeStruct();
        uint32_t name = 0, kind = 0, model = 0, skeleton = 0, anim = 0;
        bool isSkater, isSkater2, special;
        uint32_t modelFile;
        if (version >= 2) {
            object->ReadHashValue(kName, &name, 1);
            object->ReadHashValue(kKind, &kind, 1);
            object->ReadHashValue(kSkeleton, &skeleton, 0);
            model = name;
            modelFile = name + kModelFileOffset;
            anim = name;
            isSkater = object->HasBareFlag(kIsSkaterMarker);
            if (isSkater) {
                isSkater2 = kind == 0x01815FB3;
                special = false;
                if (isSkater2)
                    skeleton = 0x9954F90F;
            } else {
                isSkater2 = kind == 0x6218C3C9;
                special = isSkater2;
                if (isSkater2) {
                    wantsSkater = true;
                    skeleton = 0x9FE476E2;
                }
            }
        } else {
            object->ReadHashValue(kName, &name, 1);
            object->ReadHashValue(kModel, &model, 1);
            modelFile = model + kModelFileOffset;
            object->ReadHashValue(kSkeleton, &skeleton, 1);
            object->ReadHashValue(kAnim, &anim, 1);
            isSkater = object->HasBareFlag(kIsSkaterMarker);
            isSkater2 = object->HasBareFlag(kSkaterFlag2);
            special = model == 0x5B8AB877 || model == 0x67E6859A;
        }
        if (isSkater2 && isSkater)
            hasSkater = true;
        bool skater = isSkater && isSkater2;
        bool add = true;
        if (!special) {
            void* asset = AssetStore::g_assetManager->GetAssetData(modelFile, 0);
            if (!asset && !skater)
                params2->PutNameKey(kUnnamed, kNoModelFile);
            if (object->HasBareFlag(kKeepSkeleton)) {
                params2->PutNameKey(kUnnamed, kKeepSkeleton);
            } else {
                skeleton = 0;
                anim = 0;
            }
            if (skater) {
                void* face = AppearanceFace(m_unknown34A8);
                if (face && reinterpret_cast<bool(__thiscall*)(void*)>(CodeAddress(&Gfx2D::CFaceSkin::IsSound))(face))
                    ++modelFile;
                int male = 0;
                static_cast<Scripts::CStruct*>(m_unknown34AC)->FetchIntValue(kMale, &male, 1);
                if (male == 0)
                    modelFile += 2;
                params2->PutInt(kMaleParam, male);
            }
            params2->PutNameKey(kModelFile, modelFile);
        } else if (!m_flag34B0) {
            add = false;
        } else {
            params2->PutNameKey(kPlayingCutsceneKey, 0);
        }
        if (add && skater) {
            // the skater's object isn't made when a part of its appearance is hidden
            void* part = AppearancePart(m_unknown34A8, kPartA);
            if (!part)
                part = AppearancePart(m_unknown34A8, kPartB);
            bool hidden = part && static_cast<Scripts::CStruct*>(part)->HasBareFlag(kHidden);
            if (!hidden) {
                part = AppearancePart(m_unknown34A8, kPartC);
                hidden = part && static_cast<Scripts::CStruct*>(part)->HasBareFlag(kHidden);
            }
            if (hidden) {
                hasSkater = false;
                add = false;
            }
        }
        if (add) {
            if (skeleton) {
                params2->PutNameKey(kSkeleton, skeleton);
                params2->PutInt(kSkeletonParam, 0);
            }
            ObjectEntry& entry = m_entities[m_objectCount];
            entry.object = CreateObject(params2);
            CObjectModule* standIn = entry.object->FetchModule(kModelModuleName);
            if (standIn) {
                float offset[4] = {0.0f, 0.0f, 0.0f, 150.0f};
                ModelSetOffset(ModelOf(standIn), offset);
                if (StandsIn(standIn)) {
                    // the level object's model lends its settings (saved once) and is hidden
                    auto* level = static_cast<CGameEntity*>(FindObject(StandInId(standIn)));
                    CObjectModule* levelModel = level->FetchModule(kModelModuleName);
                    void* model2 = ModelOf(levelModel);
                    if (!m_colourSaved) {
                        float saved[4];
                        const float* settings =
                            reinterpret_cast<const float*>(static_cast<Engine3D::CRenderModel*>(model2)->FetchBoundSphere(reinterpret_cast<Maths::Vec4*>(saved)));
                        std::memcpy(m_lent2C, settings, 16);
                        uint8_t flags = *(static_cast<uint8_t*>(model2) + 0x44);
                        m_lent3C = flags >> 2 & 1;
                        m_lent3D = flags >> 3 & 1;
                        std::memcpy(m_shade, reinterpret_cast<uint8_t*>(level) + 0x4C, 16);
                        m_colourSaved = true;
                    }
                    float offset2[4] = {0.0f, 0.0f, 0.0f, 150.0f};
                    ModelSetOffset(model2, offset2);
                    static_cast<Engine3D::CRenderModel*>(model2)->Conceal(0);
                    static_cast<Engine3D::CRenderModel*>(model2)->AllowResizing(0);
                    uint32_t mine = reinterpret_cast<uint32_t(__thiscall*)(void*)>(CodeAddress(&Engine3D::CRenderModel::FetchSkinningPalette))(model2);
                    uint32_t theirs = reinterpret_cast<uint32_t(__thiscall*)(void*)>(CodeAddress(&Engine3D::CRenderModel::FetchSkinningPalette))(ModelOf(standIn));
                    static_cast<Engine3D::CRenderModel*>(model2)->UploadSkinningPalette(reinterpret_cast<uint8_t*>(theirs));
                    static_cast<Engine3D::CRenderModel*>(ModelOf(standIn))->UploadSkinningPalette(reinterpret_cast<uint8_t*>(mine));
                    static_cast<CMeshInstanceModule*>(levelModel)->UpdateBrightness();
                }
                reinterpret_cast<void(__thiscall*)(CObjectModule*, int)>((*reinterpret_cast<void***>(standIn))[7])(
                    standIn, 1);
            }
            entry.name = name;
            entry.object->ChangeSerial(name);
            entry.skeleton = skeleton;
            entry.active = special;
            entry.animation = nullptr;
            entry.parent = nullptr;
            entry.animIndex = -1;
            if (anim) {
                for (int j = 0; j < m_animationCount; ++j) {
                    if (m_animations[j].name != anim)
                        continue;
                    AnimationEntry* animation = &m_animations[j];
                    entry.animation = animation;
                    void* memory = AnimSourceAllocate(0x214);
                    animation->source =
                        memory ? static_cast<Engine3D::CFrameSource*>(memory)->MakeNew() : nullptr;
                    static_cast<Engine3D::CFrameSource*>(animation->source)->PlayClip(anim);
                    static_cast<Engine3D::CFrameSource*>(animation->source)->SetDone(true);
                    break;
                }
            }
            ++m_objectCount;
        }
        FreeStruct(params2);
    }
    if (objects->m_extent != 0 && wantsSkater && !hasSkater)
        Scripts::LaunchScript(kSkaterScript, nullptr, nullptr, false, nullptr);

    // the objects that stand in for level objects (and their children) take those's skeletons
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_entities[i];
        if (entry.active)
            CopySkeleton(entry, entry);
        if (ObjectEntry* parent = FindEntry(entry.name - 1)) {
            if (parent->active)
                CopySkeleton(*parent, entry);
        }
    }
    // their models' flags 0x00493B30 merged with their parents' level objects'
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_entities[i];
        CObjectModule* component = entry.object->FetchModule(kModelModuleName);
        if (!component)
            continue;
        void* model = ModelOf(component);
        uint32_t flags = static_cast<Engine3D::CRenderModel*>(model)->GetStripBreakFlags();
        ObjectEntry* parent = FindEntry(entry.name - 1);
        if (parent && parent->active) {
            CGameEntity* level = LevelObjectOf(parent->object);
            flags |= static_cast<Engine3D::CRenderModel*>(ModelOf(level->FetchModule(kModelModuleName)))->GetStripBreakFlags();
        }
        static_cast<Engine3D::CRenderModel*>(model)->BreakStrips(flags);
    }
    // children: their parent, the skater's look (colours from its appearance parts), their animation index
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_entities[i];
        ObjectEntry* parent = FindEntry(entry.name - 1);
        if (parent) {
            entry.parent = parent->object;
            if (parent->active)
                ApplySkaterLook(entry);
        }
        void* anims = m_animList;
        for (int j = 0; j < *reinterpret_cast<int*>(static_cast<uint8_t*>(anims) + 8); ++j) {
            if (entry.name == static_cast<Gfx2D::CJointClipFrames*>(m_animList)->FetchJointName(j)) {
                entry.animIndex = j;
                break;
            }
        }
    }
}

// The entry with this name (the first).
CCinematicTable::ObjectEntry* CCinematicTable::FindEntry(uint32_t name)
{
    for (int i = 0; i < m_objectCount; ++i) {
        if (m_entities[i].name == name)
            return &m_entities[i];
    }
    return nullptr;
}

// `to`'s object takes the skeleton of the level object `from`'s object stands in for (when it has a skeleton and
// stands in itself).
void CCinematicTable::CopySkeleton(ObjectEntry& from, ObjectEntry& to)
{
    void* skeleton = SkeletonOf(LevelObjectOf(from.object)->FetchModule(kSkeletonModuleName));
    CGameEntity* object = to.object;
    CObjectModule* own = object->FetchModule(kSkeletonModuleName);
    if (own && StandsIn(object->FetchModule(kModelModuleName)))
        static_cast<Gfx2D::CRig*>(SkeletonOf(own))->CopyBoneScales(static_cast<Gfx2D::CRig*>(skeleton), false);
}

// A child of an object that stands in: the skater's look from its appearance (a colour from part A or B, or
// from 0x949BC47B's alternatives when 0x97DBDDE6 is set).
void CCinematicTable::ApplySkaterLook(ObjectEntry& entry)
{
    CObjectModule* component = entry.object->FetchModule(kModelModuleName);
    reinterpret_cast<void(__thiscall*)(CObjectModule*, void*, int, int, uint32_t)>(
        (*reinterpret_cast<void***>(component))[0x34 / 4])(component, m_unknown34A8, 0, 0, 0x8FB01036);
    uint32_t partName = kPartA;
    auto* part = static_cast<Scripts::CStruct*>(
        static_cast<Gfx2D::CModelLook*>(m_unknown34A8)->GetPart(kPartA));
    if (!part) {
        partName = kPartB;
        part = static_cast<Scripts::CStruct*>(
            static_cast<Gfx2D::CModelLook*>(m_unknown34A8)->GetPart(kPartB));
        if (!part)
            return;
    }
    void* model = ModelOf(component);
    int useOther = 1;
    uint32_t hueName = 0x6E94F918, saturationName = 0xE4F130F4, valueName = 0x949BC47B;
    part->FetchIntValue(0x97DBDDE6, &useOther, 0);
    if (useOther) {
        part = static_cast<Scripts::CStruct*>(AppearancePart(m_unknown34A8, partName));
        hueName = 0xC6CC7F7F;
        saturationName = 0x4CA9B693;
        valueName = 0x3CC3421C;
    }
    int hue, saturation, value;
    if (!part->FetchIntValue(hueName, &hue, 0) || !part->FetchIntValue(saturationName, &saturation, 0) ||
        !part->FetchIntValue(valueName, &value, 0))
        return;
    Scripts::CStruct* colour = MakeStruct();
    colour->PutNameKey(0xB6F08F39, 0);
    colour->PutNameKey(0x83418A6A, 0xAA2206EF);
    colour->PutInt(0x318F2BDB, 0);
    static_cast<Engine3D::CRenderModel*>(model)->SetColorFromScript(
        colour, static_cast<float>(hue), static_cast<float>(saturation * static_cast<double>(0.0099999998f)),
        static_cast<float>(value * static_cast<double>(0.0099999998f)));
    FreeStruct(colour);
}

} // namespace Obj

namespace GameObjects {

static VtableSlot g_cutsceneDataVtable_slots[] = {&GameObjects::CCinematicTable::Teardown};
void** const g_cutsceneDataVtable = reinterpret_cast<void**>(g_cutsceneDataVtable_slots);

} // namespace Obj
