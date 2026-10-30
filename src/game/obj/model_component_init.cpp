// The model component's construction, destruction, initialisation and update (Obj::CModelComponent; the rest in
// model_component.cpp). From the disassembly of 0x0041FFA0-0x00420058, 0x00420270-0x0042028E, 0x00420350-0x004204ED,
// 0x00420BF0-0x00421225, 0x00421230-0x004213D6, 0x00421490-0x004215DE and 0x004219F0-0x00421AD7.

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/bone_volumes.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/model_builder.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/skeleton.h"
#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "game/obj/model_component.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "game/core/crc.h"
#include "game/core/str.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646128);
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kSuspend = 0xCE0CA665;

// InitFromStructure's / InitModel's parameters.
constexpr uint32_t kUseModelLights = 0x10079F2D;
constexpr uint32_t kMaxSceneLights = 0xD223B8BD;
constexpr uint32_t kShadowVolume = 0x2A1F92C0;
constexpr uint32_t kClass = 0x12B4E660;
constexpr uint32_t kLevelObject = 0xB7B3BD86;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kUseAssetManager = 0xC63C8D38;
constexpr uint32_t kTexDictOffset = 0xF891AC27;
constexpr uint32_t kRefObjectName = 0x153A84DE;
constexpr uint32_t kAssetName = 0x86BD5B8F;
constexpr uint32_t kMulticolor = 0x92B43E79;
constexpr uint32_t kCloneFrom = 0x7DD037B3;
constexpr uint32_t kGeom = 0xE7308C1F;
constexpr uint32_t kGeoms = 0x44E31DFF;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kModelName = 0xB974F2FC;
constexpr uint32_t kAsset = 0x354BA8E9;        // (an asset by checksum; its name isn't in THUG2's scripts)
constexpr uint32_t kSkeletonName = 0x09794932;
constexpr uint32_t kAllowReplaceTex = 0x6C37FDC7;
constexpr uint32_t kNoModel = 0x806FFF30;      // (a model name meaning none)
constexpr uint32_t kDefaultBuildScript = 0x91C1EC93;
constexpr uint32_t kBoneVolumesOn = 0x74FF5CB4; // (a script global integer: bone volumes made for models)
constexpr uint32_t kBoneVolumeHeap = 0xA432DC41;

// A skeleton component's skeleton (0x005480F0: its +0x18), a skeleton's name (0x004A2BA0: its +0x14) and data (+0x08).
void* SkeletonOf(CBaseComponent* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18);
}
uint32_t SkeletonName(void* skeleton) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skeleton) + 0x14); }
void* SkeletonData(void* skeleton) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 0x08); }

// Nx (not rewritten yet): a new model (0x0048BFA0) and its deletion (0x0048C3C0); the model's geoms cleared
// (0x00494BA0), counted (0x00492E40), by index (0x00494640) and name (0x00493CD0), their names (0x00494680), one added
// (0x004938E0); a geom cloned (0x0048DDC0 - for a model - or 0x0048DD80 - a sector's) and activated (0x0048DE60);
// the model loaded from an asset (0x004934A0) or a file (0x004935E0); its scale (0x00492EE0), active flags (0x00492E10),
// skeleton (0x00492DD0), shadow volume (0x00492E90), bounding sphere (0x00492F30), lights (+0x2C) made (0x00493BD0) or
// taken away (0x00493C10), bone volumes (0x00492FC0 / 0x00492F90), placed (0x00493090) and its skinning updated
// (0x00493410).
Nx::CModel* NewModel(int arg) { return GameFunction<Nx::CModel*(__cdecl*)(int)>(0x0048BFA0)(arg); }
void DeleteModel(Nx::CModel* model) { Nx::Engine::DeleteGeomObject(model); }
void* ModelLights(Nx::CModel* model) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C); }
void* CloneModelGeom(void* geom, Nx::CModel* to) { return static_cast<Nx::CGeom*>(geom)->CloneForModel(1, to); }
void* CloneSectorGeom(void* geom) { return static_cast<Nx::CGeom*>(geom)->Clone(1, nullptr); }
void ActivateGeom(void* geom) { GameFunction<void(__thiscall*)(void*, int)>(0x0048DE60)(geom, 1); }
void AddGeom(Nx::CModel* model, void* geom, uint32_t name) { model->AddGeom(geom, name, true); }
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any).
bool GetScale(Mth::Vector* scale, Script::CStruct* params)
{
    return Gfx::GetScaleFromParams(scale, params);
}
// A level sector by name in any scene (0x0048C510) and its geometry (0x004A2BA0: its +0x14).
void* GetSector(uint32_t name) { return Nx::Engine::GetSector(name); }
void* SectorGeom(void* sector) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(sector) + 0x14); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void ModelFileName(const char* name, const char* ext, char* out)
{
    Gfx::MakeModelFileName(name, ext, out);
}
// The model's lights' brightness (0x0048E660).
void UpdateLightsBrightness(void* lights) { static_cast<Nx::CModelLights*>(lights)->UpdateBrightness(); }

// Shared bone volumes (0x0C bytes: the volumes, their count, users), by skeleton name (0x0048B3C0); made (0x0048B350),
// sized and registered (0x0048BEA0), filled from the skeleton's data (0x0048B430).
void* FindBoneVolumes(uint32_t name) { return Gfx::FindBoneVolumes(name); }

// The model builder Gfx (an Obj::CObject, vtable 0x00648920, 0x54 bytes, constructor 0x004897D0: use the asset
// manager, the texture dictionary offset) that builds a model from a profile with a build script (0x0048A2F0); its
// destructor is the CObject's.
struct ModelBuilder {
    alignas(16) uint8_t bytes[0x60];
};

} // namespace

// 0x00421490
CBaseComponent* __cdecl CModelComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CModelComponent));
    return memory ? static_cast<CModelComponent*>(memory)->Construct() : nullptr;
}

// 0x00420BF0
CModelComponent* CModelComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    mp_model = NewModel(1);
    m_pivot = {0.0f, 0.0f, 0.0f, 1.0f};
    for (CModelSpinAxis& axis : m_spin) {
        for (CModelSpin& spin : axis.m_spins)
            spin.m_flags &= ~(MODEL_SPIN_ON | MODEL_SPIN_HOLD);
        axis.m_active = false;
    }
    m_colorValueCount = 0;
    m_sectorModel = false;
    m_offset = {0.0f, 0.0f, 0.0f, 1.0f};
    m_brightness = 1.0f;
    return this;
}

// 0x00420000
void CModelComponent::DestroyBody()
{
    m_vtable = kVtable;
    DeleteModel(mp_model);
    CBaseComponent::DestroyBody();
}

// 0x00420270
CModelComponent* CModelComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004204B0
void CModelComponent::Finalize()
{
    mp_skeleton = m_object->GetComponent(kSkeleton);
    mp_animBlender = m_object->GetComponent(kAnimBlender);
    mp_suspend = m_object->GetComponent(kSuspend);
}

// 0x004214F0
void CModelComponent::InitFromStructure(Script::CStruct* params)
{
    if (params->ContainsFlag(kUseModelLights)) {
        if (!ModelLights(mp_model)) {
            mp_model->CreateLights();
            void* lights = ModelLights(mp_model);
            static_cast<Nx::CModelLights*>(lights)->SetPositionPointer(&m_object->m_pos);
            int count = 1;
            params->GetInteger(kMaxSceneLights, &count, 0);
            *reinterpret_cast<int*>(static_cast<uint8_t*>(lights) + 4) = count; // (0x00407700)
        }
    } else if (ModelLights(mp_model)) {
        mp_model->DestroyLights();
    }
    if (CBaseComponent* skeleton = m_object->GetComponent(kSkeleton))
        mp_model->SetSkeleton(reinterpret_cast<uint8_t*>(SkeletonOf(skeleton)));
    int shadow = 0;
    params->GetInteger(kShadowVolume, &shadow, 0);
    mp_model->EnableShadowVolume(shadow != 0);
    InitModel(params);
}

// 0x004219F0
void CModelComponent::Update()
{
    Mth::Matrix matrix;
    if (m_levelObject) {
        // A level object: its sector's model follows.
        CCompositeObject* object = m_object;
        auto* source = static_cast<CCompositeObject*>(FindObject(m_refObjectName));
        Nx::CModel* model = static_cast<CModelComponent*>(source->GetComponent(kModel))->mp_model;
        CBaseComponent* skeleton = object->GetComponent(kSkeleton);
        GetDisplayMatrix(&matrix);
        model->Render(&matrix, false, reinterpret_cast<uint8_t*>(SkeletonOf(skeleton)));
        model->CopyBones(reinterpret_cast<uint8_t*>(SkeletonOf(skeleton)));
        return;
    }
    uint8_t modelFlags = mp_model ? reinterpret_cast<uint8_t*>(mp_model)[0x44] : 0;
    if (!mp_model || !(modelFlags & 2) || (modelFlags & 4))
        return; // (not active, or hidden)
    GetDisplayMatrix(&matrix);
    void* skeleton = mp_skeleton ? SkeletonOf(mp_skeleton) : nullptr;
    bool ready = mp_skeleton != nullptr;
    if (mp_animBlender)
        ready = static_cast<CAnimBlenderComponent*>(mp_animBlender)->ModelReady();
    mp_model->Render(&matrix, !ready, reinterpret_cast<uint8_t*>(skeleton));
}

// 0x00420480
void CModelComponent::Teleport()
{
    CBaseComponent::Update(); // (through the vtable)
    if (mp_model && ModelLights(mp_model))
        UpdateLightsBrightness(ModelLights(mp_model));
}

// 0x00421360
void CModelComponent::SetModel(Nx::CModel* model)
{
    mp_model = model;
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_FINALIZED) {
        CBaseComponent::Update();
        if (mp_model && ModelLights(mp_model))
            UpdateLightsBrightness(ModelLights(mp_model));
    }
    Mth::Vector sphere;
    m_boundingSphere = *model->GetBoundingSphere(&sphere);
}

// 0x00421230
void CModelComponent::InitModelFromProfile(void* profile, bool useAssetManager, int texDictOffset,
                                           uint32_t buildScript)
{
    Nx::CModel* model = mp_model;
    void* skeleton = nullptr;
    if (CBaseComponent* component = m_object->GetComponent(kSkeleton))
        skeleton = SkeletonOf(component);
    ModelBuilder builder;
    reinterpret_cast<Gfx::CModelBuilder*>(&builder)->Construct(useAssetManager, texDictOffset);
    reinterpret_cast<Gfx::CModelBuilder*>(&builder)->Build(static_cast<Gfx::CModelAppearance*>(profile), model, static_cast<Gfx::CSkeleton*>(skeleton), buildScript ? buildScript : kDefaultBuildScript);
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_FINALIZED) {
        CBaseComponent::Update();
        if (mp_model && ModelLights(mp_model))
            UpdateLightsBrightness(ModelLights(mp_model));
    }
    Mth::Vector sphere;
    m_boundingSphere = *model->GetBoundingSphere(&sphere);
    if (Script::GetInteger(kBoneVolumesOn))
        InitBoneVolumes();
    reinterpret_cast<CObject*>(&builder)->DestroyBody();
}

// 0x0041FFA0
void CModelComponent::InitFromSector(uint32_t name)
{
    void* sector = GetSector(name);
    if (!sector)
        return;
    void* geom = SectorGeom(sector);
    if (!geom)
        return;
    void* clone = CloneSectorGeom(geom);
    ActivateGeom(clone);
    AddGeom(mp_model, clone, 0);
    m_sectorModel = true;
}

// 0x00420350
void CModelComponent::InitBoneVolumes()
{
    if (!mp_skeleton) {
        mp_skeleton = m_object->GetComponent(kSkeleton);
        if (!mp_skeleton)
            return;
    }
    if (mp_model->GetBoneVolumes())
        return;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->GetNamedHeap(kBoneVolumeHeap, 1));
    void* skeleton = SkeletonOf(mp_skeleton);
    void* data = SkeletonData(skeleton);
    void* volumes = FindBoneVolumes(SkeletonName(skeleton));
    if (volumes) {
        ++*reinterpret_cast<int*>(static_cast<uint8_t*>(volumes) + 8); // (another user)
    } else {
        // Made and registered for the skeleton - but not given to this model (the original's): the next one finds it.
        void* made = Mem::New(0x0C);
        if (made)
            made = static_cast<Gfx::CBoneVolumes*>(made)->Construct(0);
        static_cast<Gfx::CBoneVolumes*>(made)->Register(SkeletonName(skeleton));
        static_cast<Gfx::CBoneVolumes*>(made)->Fill(static_cast<Gfx::CSkeletonData*>(data));
    }
    mp_model->SetBoneVolumes(reinterpret_cast<uint8_t*>(volumes));
    Mem::Manager::Get()->PopContext();
}

// 0x00420D10
void CModelComponent::InitModel(Script::CStruct* params)
{
    mp_model->ClearGeoms();
    Mth::Vector scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (GetScale(&scale, params))
        mp_model->SetScale(&scale);
    uint32_t type = 0;
    params->GetChecksum(kClass, &type, 0);
    if (type == kLevelObject) {
        uint32_t name;
        params->GetChecksum(kName, &name, 1);
        InitFromSector(name);
    } else {
        int useAssetManager = 1;
        params->GetInteger(kUseAssetManager, &useAssetManager, 0);
        int texDictOffset = 0;
        params->GetInteger(kTexDictOffset, &texDictOffset, 0);
        uint32_t name;
        if (params->GetChecksum(kRefObjectName, &name, 0)) {
            // A level object's model: its sector's (Update follows it).
            m_refObjectName = name;
            m_levelObject = true;
            mp_model->SetDisplayMode(6);
        } else if (params->GetChecksum(kAssetName, &name, 0)) {
            int multicolor = 0;
            params->GetInteger(kMulticolor, &multicolor, 0);
            mp_model->AddAssetGeom(name, 0, multicolor != 0, 1);
        } else if (params->GetChecksum(kCloneFrom, &name, 0)) {
            // Another object's model's geoms cloned (and its colour values).
            auto* object = static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(name));
            auto* source = static_cast<CModelComponent*>(object->GetComponent(kModel));
            m_colorValueCount = source->m_colorValueCount;
            for (int i = 0; i < m_colorValueCount; ++i)
                m_colorValues[i] = source->m_colorValues[i];
            uint32_t geomName = 0;
            Script::CArray* geoms;
            if (params->GetChecksum(kGeom, &geomName, 0)) {
                void* geom = source->mp_model->GetGeomByName(geomName);
                if (!geom)
                    return; // (nothing after it either)
                void* clone = CloneModelGeom(geom, mp_model);
                ActivateGeom(clone);
                AddGeom(mp_model, clone, geomName);
            } else if (params->GetArray(kGeoms, &geoms, 0)) {
                for (uint32_t i = 0; i < geoms->m_size; ++i) {
                    geomName = geoms->GetElement(i);
                    if (!geomName)
                        continue;
                    void* geom = source->mp_model->GetGeomByName(geomName);
                    if (!geom)
                        return;
                    void* clone = CloneModelGeom(geom, mp_model);
                    ActivateGeom(clone);
                    AddGeom(mp_model, clone, geomName);
                }
            } else {
                for (int i = 0; i < source->mp_model->GetNumGeoms(); ++i) {
                    void* clone = CloneModelGeom(source->mp_model->GetGeom(i), mp_model);
                    ActivateGeom(clone);
                    AddGeom(mp_model, clone, source->mp_model->GetGeomName(i));
                }
            }
        } else {
            const char* file;
            if (params->GetString(kModel, &file, false) || params->GetString(kModelName, &file, false)) {
                // A model file (unless it's the "none" name).
                if (Crc::GenerateCRCFromString(file) != kNoModel) {
                    char path[0x200];
                    uint32_t skeletonName;
                    if (params->GetChecksum(kSkeletonName, &skeletonName, 0))
                        ModelFileName(file, ".skin", path);
                    else
                        ModelFileName(file, ".mdl", path);
                    Str::LowerCase(path);
                    bool allowReplace = params->ContainsFlag(kAllowReplaceTex);
                    mp_model->AddFileGeom(static_cast<const char*>(path), 0, useAssetManager != 0, texDictOffset, allowReplace, 0, 1, 0);
                }
            } else if (params->GetChecksum(kAsset, &name, 0)) {
                mp_model->AddAssetGeom(name, 0, 0, 1);
            } else if (params->GetChecksum(kModel, &name, 0)) {
                // A level sector's geometry as the model.
                void* clone = CloneSectorGeom(SectorGeom(GetSector(name)));
                ActivateGeom(clone);
                AddGeom(mp_model, clone, 0);
            }
        }
    }
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_FINALIZED) {
        CBaseComponent::Update();
        if (mp_model && ModelLights(mp_model))
            UpdateLightsBrightness(ModelLights(mp_model));
    }
    Mth::Vector sphere;
    m_boundingSphere = *mp_model->GetBoundingSphere(&sphere);
    if (Script::GetInteger(kBoneVolumesOn))
        InitBoneVolumes();
}

} // namespace Obj

REPLACE(0x00421490, Obj::CModelComponent::Create);
REPLACE(0x00420BF0, &Obj::CModelComponent::Construct);
REPLACE(0x00420000, &Obj::CModelComponent::DestroyBody);
REPLACE(0x00420270, &Obj::CModelComponent::Destroy);
REPLACE(0x004204B0, &Obj::CModelComponent::Finalize);
REPLACE(0x004214F0, &Obj::CModelComponent::InitFromStructure);
REPLACE(0x004219F0, &Obj::CModelComponent::Update);
REPLACE(0x00420480, &Obj::CModelComponent::Teleport);
REPLACE(0x00421360, &Obj::CModelComponent::SetModel);
REPLACE(0x00421230, &Obj::CModelComponent::InitModelFromProfile);
REPLACE(0x0041FFA0, &Obj::CModelComponent::InitFromSector);
REPLACE(0x00420350, &Obj::CModelComponent::InitBoneVolumes);
REPLACE(0x00420D10, &Obj::CModelComponent::InitModel);
