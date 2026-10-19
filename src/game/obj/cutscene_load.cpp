// Cutscenes: the data's constructor and the objects a cutscene adds (0x004645A0-0x00465502). See cutscene.h.

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/cutscene.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kUnnamed = 0; // (a flag: a checksum without a name)
constexpr uint32_t kModelComponent = 0x286A8D26;
constexpr uint32_t kSkeletonComponent = 0x222756D5;
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
constexpr uint32_t kSkaterFlag = 0x723CDA8F;
constexpr uint32_t kSkaterFlag2 = 0x6914D9D2;
constexpr uint32_t kKeepSkeleton = 0x0CBEC8AC;
constexpr uint32_t kNoModelFile = 0x5B5D592E;
constexpr uint32_t kModelFile = 0x86BD5B8F;
constexpr uint32_t kMale = 0x3F813177;
constexpr uint32_t kMaleParam = 0x92B43E79;
constexpr uint32_t kInCutscene = 0x153A84DE;
constexpr uint32_t kSkeletonParam = 0xD3982061;
constexpr uint32_t kSkaterScript = 0xF37D9F53;
// (the model asset's name: the model checksum plus this)
constexpr uint32_t kModelFileOffset = 0xFD8697E1;

// The skater appearance parts asked about (names unknown).
constexpr uint32_t kPartA = 0x650FAB6D, kPartB = 0x0FC85BAE, kPartC = 0x6DF453B6, kHidden = 0xC4C5B2CC;

Mem::PoolPair& StructPools() { return GameVariable<Mem::PoolPair>(0x006F3614); }
Mem::PoolPair& SourcePools() { return GameVariable<Mem::PoolPair>(0x006FA7F4); }

Script::CStruct* MakeStruct()
{
    void* memory = StructPools().Allocate(8);
    return memory ? static_cast<Script::CStruct*>(memory)->Init() : nullptr;
}
void FreeStruct(Script::CStruct* s)
{
    if (!s)
        return;
    s->Clear();
    Script::DeleteStruct(s);
}

void* ModelOf(CBaseComponent* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
bool StandsIn(CBaseComponent* model)
{
    return *(reinterpret_cast<uint8_t*>(model) + 0x234) != 0;
}
uint32_t StandInId(CBaseComponent* model)
{
    return GameFunction<uint32_t(__thiscall*)(CBaseComponent*)>(0x00420100)(model);
}
void* SkeletonOf(CBaseComponent* component)
{
    return GameFunction<void*(__thiscall*)(CBaseComponent*)>(0x005480F0)(component);
}
// The level object a cutscene object stands in for.
CCompositeObject* LevelObjectOf(CCompositeObject* object)
{
    return static_cast<CCompositeObject*>(FindObject(StandInId(object->GetComponent(kModelComponent))));
}
void ModelSetOffset(void* model, const float* offset)
{
    GameFunction<void(__thiscall*)(void*, const float*)>(0x00492E80)(model, offset);
}
// The skater's appearance (+0x34A8) and its parts.
void* AppearancePart(void* appearance, uint32_t part)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x00489050)(appearance, part);
}

} // namespace

// 0x004645A0
void* __cdecl AnimSourceAllocate(unsigned int size)
{
    return SourcePools().Allocate(size);
}

// 0x004645E0
void __cdecl AnimSourceFree(void* item)
{
    SourcePools().Free(item);
}

// 0x00464640
CCutsceneData* CCutsceneData::Construct()
{
    m_vtable = reinterpret_cast<void**>(0x00647908);
    mp_bundle = nullptr;
    m_objectPart = 0;
    m_cameraPart = 0;
    m_cameraParts = 0;
    m_cameraSource = nullptr;
    m_objectParts = 0;
    m_objectSource = nullptr;
    m_objectCount = 0;
    m_assetCount = 0;
    m_unknown3460 = 0;
    m_unknown3470 = 1.0f;
    m_unknown3474 = 0;
    m_time = 0.0f;
    m_unknown3480 = 0.0f;
    m_cameraStart = 0.0f;
    m_unknown348C = 0.0f;
    m_vblanks = 0;
    m_lastVblanks = 0;
    m_endTime = 0.0f;
    m_colourSaved = false;
    m_unknown34A8 = nullptr;
    m_unknown34AC = nullptr;
    m_subtitles = nullptr;
    m_nextSubtitle = 0;
    m_subtitleTime = 0.0f;
    m_nextSubtitleTime = -1.0f;
    mp_params = MakeStruct();
    return this;
}

// 0x00464780
void CCutsceneData::RefreshObjects()
{
    if (!m_flag34B0 || Script::GetInteger(kRefreshGlobal) == 0)
        return;
    Script::CStruct* params = MakeStruct();
    params->AddChecksum(kUnnamed, kFlagParam);
    for (int i = 0; i < m_objectCount; ++i) {
        CCompositeObject* object = m_objects[i].object;
        if (object->GetComponent(kModelComponent))
            Script::RunScript(kRefreshScript, params, object, false, nullptr);
    }
    auto* skater =
        GameFunction<CCompositeObject*(__thiscall*)(void*)>(0x00566520)(GameVariable<void*>(0x007CE478));
    if (skater)
        Script::RunScript(kRefreshScript, params, skater, false, nullptr);
    FreeStruct(params);
    Script::FindSymbol(kRefreshGlobal)->integer = 0;
}

// 0x004648D0
void CCutsceneData::AddObjects(Script::CStruct* params)
{
    int version = 0;
    params->GetInteger(kVersion, &version, 1);
    Script::CArray* objects = nullptr;
    params->GetArray(kObjects, &objects, 1);
    bool wantsSkater = false; // (an object of kind 0x6218C3C9: script 0xF37D9F53 runs...)
    bool hasSkater = false;   // (...unless a skater object was added)
    for (uint32_t i = 0; i < objects->m_size; ++i) {
        auto* object = reinterpret_cast<Script::CStruct*>(objects->GetElement(i));
        Script::CStruct* params2 = MakeStruct();
        uint32_t name = 0, kind = 0, model = 0, skeleton = 0, anim = 0;
        bool isSkater, isSkater2, special;
        uint32_t modelFile;
        if (version >= 2) {
            object->GetChecksum(kName, &name, 1);
            object->GetChecksum(kKind, &kind, 1);
            object->GetChecksum(kSkeleton, &skeleton, 0);
            model = name;
            modelFile = name + kModelFileOffset;
            anim = name;
            isSkater = object->ContainsFlag(kSkaterFlag);
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
            object->GetChecksum(kName, &name, 1);
            object->GetChecksum(kModel, &model, 1);
            modelFile = model + kModelFileOffset;
            object->GetChecksum(kSkeleton, &skeleton, 1);
            object->GetChecksum(kAnim, &anim, 1);
            isSkater = object->ContainsFlag(kSkaterFlag);
            isSkater2 = object->ContainsFlag(kSkaterFlag2);
            special = model == 0x5B8AB877 || model == 0x67E6859A;
        }
        if (isSkater2 && isSkater)
            hasSkater = true;
        bool skater = isSkater && isSkater2;
        bool add = true;
        if (!special) {
            void* asset =
                GameFunction<void*(__thiscall*)(void*, uint32_t, int)>(0x00407C00)(GameVariable<void*>(0x006918DC),
                                                                                  modelFile, 0);
            if (!asset && !skater)
                params2->AddChecksum(kUnnamed, kNoModelFile);
            if (object->ContainsFlag(kKeepSkeleton)) {
                params2->AddChecksum(kUnnamed, kKeepSkeleton);
            } else {
                skeleton = 0;
                anim = 0;
            }
            if (skater) {
                void* face = GameFunction<void*(__thiscall*)(void*)>(0x00576E60)(m_unknown34A8);
                if (face && GameFunction<bool(__thiscall*)(void*)>(0x00487510)(face))
                    ++modelFile;
                int male = 0;
                static_cast<Script::CStruct*>(m_unknown34AC)->GetInteger(kMale, &male, 1);
                if (male == 0)
                    modelFile += 2;
                params2->AddInteger(kMaleParam, male);
            }
            params2->AddChecksum(kModelFile, modelFile);
        } else if (!m_flag34B0) {
            add = false;
        } else {
            params2->AddChecksum(kInCutscene, 0);
        }
        if (add && skater) {
            // the skater's object isn't made when a part of its appearance is hidden
            void* part = AppearancePart(m_unknown34A8, kPartA);
            if (!part)
                part = AppearancePart(m_unknown34A8, kPartB);
            bool hidden = part && static_cast<Script::CStruct*>(part)->ContainsFlag(kHidden);
            if (!hidden) {
                part = AppearancePart(m_unknown34A8, kPartC);
                hidden = part && static_cast<Script::CStruct*>(part)->ContainsFlag(kHidden);
            }
            if (hidden) {
                hasSkater = false;
                add = false;
            }
        }
        if (add) {
            if (skeleton) {
                params2->AddChecksum(kSkeleton, skeleton);
                params2->AddInteger(kSkeletonParam, 0);
            }
            ObjectEntry& entry = m_objects[m_objectCount];
            entry.object = CreateObject(params2);
            CBaseComponent* standIn = entry.object->GetComponent(kModelComponent);
            if (standIn) {
                float offset[4] = {0.0f, 0.0f, 0.0f, 150.0f};
                ModelSetOffset(ModelOf(standIn), offset);
                if (StandsIn(standIn)) {
                    // the level object's model lends its settings (saved once) and is hidden
                    auto* level = static_cast<CCompositeObject*>(FindObject(StandInId(standIn)));
                    CBaseComponent* levelModel = level->GetComponent(kModelComponent);
                    void* model2 = ModelOf(levelModel);
                    if (!m_colourSaved) {
                        float saved[4];
                        const float* settings =
                            GameFunction<const float*(__thiscall*)(void*, float*)>(0x00492F30)(model2, saved);
                        std::memcpy(m_lent2C, settings, 16);
                        uint8_t flags = *(static_cast<uint8_t*>(model2) + 0x44);
                        m_lent3C = flags >> 2 & 1;
                        m_lent3D = flags >> 3 & 1;
                        std::memcpy(m_colour, reinterpret_cast<uint8_t*>(level) + 0x4C, 16);
                        m_colourSaved = true;
                    }
                    float offset2[4] = {0.0f, 0.0f, 0.0f, 150.0f};
                    ModelSetOffset(model2, offset2);
                    GameFunction<void(__thiscall*)(void*, int)>(0x00494B70)(model2, 0);
                    GameFunction<void(__thiscall*)(void*, int)>(0x00492E60)(model2, 0);
                    uint32_t mine = GameFunction<uint32_t(__thiscall*)(void*)>(0x00492E50)(model2);
                    uint32_t theirs = GameFunction<uint32_t(__thiscall*)(void*)>(0x00492E50)(ModelOf(standIn));
                    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00493460)(model2, theirs);
                    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00493460)(ModelOf(standIn), mine);
                    GameFunction<void(__thiscall*)(CBaseComponent*)>(0x004200C0)(levelModel);
                }
                reinterpret_cast<void(__thiscall*)(CBaseComponent*, int)>((*reinterpret_cast<void***>(standIn))[7])(
                    standIn, 1);
            }
            entry.name = name;
            entry.object->SetID(name);
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
                        memory ? GameFunction<void*(__thiscall*)(void*)>(0x0048CF70)(memory) : nullptr;
                    GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x0048CFD0)(animation->source, anim);
                    GameFunction<void*(__thiscall*)(void*, bool)>(0x0048CFA0)(animation->source, true);
                    break;
                }
            }
            ++m_objectCount;
        }
        FreeStruct(params2);
    }
    if (objects->m_size != 0 && wantsSkater && !hasSkater)
        Script::RunScript(kSkaterScript, nullptr, nullptr, false, nullptr);

    // the objects that stand in for level objects (and their children) take those's skeletons
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        if (entry.active)
            CopySkeleton(entry, entry);
        if (ObjectEntry* parent = FindEntry(entry.name - 1)) {
            if (parent->active)
                CopySkeleton(*parent, entry);
        }
    }
    // their models' flags 0x00493B30 merged with their parents' level objects'
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        CBaseComponent* component = entry.object->GetComponent(kModelComponent);
        if (!component)
            continue;
        void* model = ModelOf(component);
        uint32_t flags = GameFunction<uint32_t(__thiscall*)(void*)>(0x00493B30)(model);
        ObjectEntry* parent = FindEntry(entry.name - 1);
        if (parent && parent->active) {
            CCompositeObject* level = LevelObjectOf(parent->object);
            flags |= GameFunction<uint32_t(__thiscall*)(void*)>(0x00493B30)(
                ModelOf(level->GetComponent(kModelComponent)));
        }
        GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00493B90)(model, flags);
    }
    // children: their parent, the skater's look (colours from its appearance parts), their animation index
    for (int i = 0; i < m_objectCount; ++i) {
        ObjectEntry& entry = m_objects[i];
        ObjectEntry* parent = FindEntry(entry.name - 1);
        if (parent) {
            entry.parent = parent->object;
            if (parent->active)
                ApplySkaterLook(entry);
        }
        void* anims = mp_animList;
        for (int j = 0; j < *reinterpret_cast<int*>(static_cast<uint8_t*>(anims) + 8); ++j) {
            if (entry.name == GameFunction<uint32_t(__thiscall*)(void*, int)>(0x00482F00)(mp_animList, j)) {
                entry.animIndex = j;
                break;
            }
        }
    }
}

// The entry with this name (the first).
CCutsceneData::ObjectEntry* CCutsceneData::FindEntry(uint32_t name)
{
    for (int i = 0; i < m_objectCount; ++i) {
        if (m_objects[i].name == name)
            return &m_objects[i];
    }
    return nullptr;
}

// `to`'s object takes the skeleton of the level object `from`'s object stands in for (when it has a skeleton and
// stands in itself).
void CCutsceneData::CopySkeleton(ObjectEntry& from, ObjectEntry& to)
{
    void* skeleton = SkeletonOf(LevelObjectOf(from.object)->GetComponent(kSkeletonComponent));
    CCompositeObject* object = to.object;
    CBaseComponent* own = object->GetComponent(kSkeletonComponent);
    if (own && StandsIn(object->GetComponent(kModelComponent)))
        GameFunction<void(__thiscall*)(void*, void*, int)>(0x004A2E20)(SkeletonOf(own), skeleton, 0);
}

// A child of an object that stands in: the skater's look from its appearance (a colour from part A or B, or
// from 0x949BC47B's alternatives when 0x97DBDDE6 is set).
void CCutsceneData::ApplySkaterLook(ObjectEntry& entry)
{
    CBaseComponent* component = entry.object->GetComponent(kModelComponent);
    reinterpret_cast<void(__thiscall*)(CBaseComponent*, void*, int, int, uint32_t)>(
        (*reinterpret_cast<void***>(component))[0x34 / 4])(component, m_unknown34A8, 0, 0, 0x8FB01036);
    uint32_t partName = kPartA;
    auto* part = static_cast<Script::CStruct*>(
        GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x004890B0)(m_unknown34A8, kPartA));
    if (!part) {
        partName = kPartB;
        part = static_cast<Script::CStruct*>(
            GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x004890B0)(m_unknown34A8, kPartB));
        if (!part)
            return;
    }
    void* model = ModelOf(component);
    int useOther = 1;
    uint32_t hueName = 0x6E94F918, saturationName = 0xE4F130F4, valueName = 0x949BC47B;
    part->GetInteger(0x97DBDDE6, &useOther, 0);
    if (useOther) {
        part = static_cast<Script::CStruct*>(AppearancePart(m_unknown34A8, partName));
        hueName = 0xC6CC7F7F;
        saturationName = 0x4CA9B693;
        valueName = 0x3CC3421C;
    }
    int hue, saturation, value;
    if (!part->GetInteger(hueName, &hue, 0) || !part->GetInteger(saturationName, &saturation, 0) ||
        !part->GetInteger(valueName, &value, 0))
        return;
    Script::CStruct* colour = MakeStruct();
    colour->AddChecksum(0xB6F08F39, 0);
    colour->AddChecksum(0x83418A6A, 0xAA2206EF);
    colour->AddInteger(0x318F2BDB, 0);
    GameFunction<void(__thiscall*)(void*, Script::CStruct*, float, float, float)>(0x00494200)(
        model, colour, static_cast<float>(hue), static_cast<float>(saturation * static_cast<double>(0.0099999998f)),
        static_cast<float>(value * static_cast<double>(0.0099999998f)));
    FreeStruct(colour);
}

} // namespace Obj

REPLACE(0x004645A0, Obj::AnimSourceAllocate);
REPLACE(0x004645E0, Obj::AnimSourceFree);
REPLACE(0x00464640, &Obj::CCutsceneData::Construct);
REPLACE(0x00464780, &Obj::CCutsceneData::RefreshObjects);
REPLACE(0x004648D0, &Obj::CCutsceneData::AddObjects);
