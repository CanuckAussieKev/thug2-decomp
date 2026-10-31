// The model component's construction, destruction, initialisation and update (GameObjects::CMeshInstanceModule; the rest in
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
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kSuspend = 0xCE0CA665;

// ConfigureFromData's / SetUpMesh's parameters.
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
void* SkeletonOf(CObjectModule* component)
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
Engine3D::CRenderModel* NewModel(int arg) { return (void)arg, static_cast<Engine3D::CRenderModel*>(Engine3D::SpawnModel()); }
void DeleteModel(Engine3D::CRenderModel* model) { Engine3D::Engine::DeleteGeomObject(model); }
void* ModelLights(Engine3D::CRenderModel* model) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C); }
void* CloneModelGeom(void* geom, Engine3D::CRenderModel* to) { return static_cast<Engine3D::CGeom*>(geom)->CloneForModel(1, to); }
void* CloneSectorGeom(void* geom) { return static_cast<Engine3D::CGeom*>(geom)->Clone(1, nullptr); }
void ActivateGeom(void* geom) { reinterpret_cast<void(__thiscall*)(void*, int)>(&Engine3D::VirtualSlot13Jump)(geom, 1); }
void AddGeom(Engine3D::CRenderModel* model, void* geom, uint32_t name) { model->AddGeom(geom, name, true); }
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any).
bool GetScale(Maths::Vector* scale, Scripts::CStruct* params)
{
    return Gfx2D::ScaleFromArgs(scale, params);
}
// A level sector by name in any scene (0x0048C510) and its geometry (0x004A2BA0: its +0x14).
void* LookUpSector(uint32_t name) { return Engine3D::Engine::LookUpSector(name); }
void* SectorGeom(void* sector) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(sector) + 0x14); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void ModelFileName(const char* name, const char* ext, char* out)
{
    Gfx2D::MakeModelFileName(name, ext, out);
}
// The model's lights' brightness (0x0048E660).
void UpdateLightsBrightness(void* lights) { static_cast<Engine3D::CModelLights*>(lights)->UpdateBrightness(); }

// Shared bone volumes (0x0C bytes: the volumes, their count, users), by skeleton name (0x0048B3C0); made (0x0048B350),
// sized and registered (0x0048BEA0), filled from the skeleton's data (0x0048B430).
void* FindBoneVolumes(uint32_t name) { return Gfx2D::FindBoneVolumes(name); }

// The model builder Gfx (an GameObjects::CObject, vtable 0x00648920, 0x54 bytes, constructor 0x004897D0: use the asset
// manager, the texture dictionary offset) that builds a model from a profile with a build script (0x0048A2F0); its
// destructor is the CObject's.
struct ModelBuilder {
    alignas(16) uint8_t bytes[0x60];
};

} // namespace

// 0x00421490
CObjectModule* __cdecl CMeshInstanceModule::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CMeshInstanceModule));
    return memory ? static_cast<CMeshInstanceModule*>(memory)->Construct() : nullptr;
}

// 0x00420BF0
CMeshInstanceModule* CMeshInstanceModule::Construct()
{
    CObjectModule::Construct();
    m_vtable = kVtable;
    m_type = kType;
    m_meshInstance = NewModel(1);
    m_pivot = {0.0f, 0.0f, 0.0f, 1.0f};
    for (CModelSpinAxis& axis : m_twistRate) {
        for (CModelSpin& spin : axis.m_spins)
            spin.m_flags &= ~(MODEL_SPIN_ON | MODEL_SPIN_HOLD);
        axis.m_active = false;
    }
    m_colorValueCount = 0;
    m_sectorModel = false;
    m_shift = {0.0f, 0.0f, 0.0f, 1.0f};
    m_brightness = 1.0f;
    return this;
}

// 0x00420000
void CMeshInstanceModule::DestroyBody()
{
    m_vtable = kVtable;
    DeleteModel(m_meshInstance);
    CObjectModule::DestroyBody();
}

// 0x00420270
CMeshInstanceModule* CMeshInstanceModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004204B0
void CMeshInstanceModule::Finalize()
{
    m_boneRig = m_object->GetComponent(kSkeleton);
    mp_animBlender = m_object->GetComponent(kAnimBlender);
    mp_suspend = m_object->GetComponent(kSuspend);
}

// 0x004214F0
void CMeshInstanceModule::ConfigureFromData(Scripts::CStruct* params)
{
    if (params->HasBareFlag(kUseModelLights)) {
        if (!ModelLights(m_meshInstance)) {
            m_meshInstance->CreateLights();
            void* lights = ModelLights(m_meshInstance);
            static_cast<Engine3D::CModelLights*>(lights)->BindPositionSource(&m_object->m_pos);
            int count = 1;
            params->FetchIntValue(kMaxSceneLights, &count, 0);
            *reinterpret_cast<int*>(static_cast<uint8_t*>(lights) + 4) = count; // (0x00407700)
        }
    } else if (ModelLights(m_meshInstance)) {
        m_meshInstance->DestroyLights();
    }
    if (CObjectModule* skeleton = m_object->GetComponent(kSkeleton))
        m_meshInstance->ChooseRig(reinterpret_cast<uint8_t*>(SkeletonOf(skeleton)));
    int shadow = 0;
    params->FetchIntValue(kShadowVolume, &shadow, 0);
    m_meshInstance->ActivateShadowVolume(shadow != 0);
    SetUpMesh(params);
}

// 0x004219F0
void CMeshInstanceModule::Update()
{
    Maths::Matrix matrix;
    if (m_levelObject) {
        // A level object: its sector's model follows.
        CGameEntity* object = m_object;
        auto* source = static_cast<CGameEntity*>(FindObject(m_linkedEntityLabel));
        Engine3D::CRenderModel* model = static_cast<CMeshInstanceModule*>(source->GetComponent(kModel))->m_meshInstance;
        CObjectModule* skeleton = object->GetComponent(kSkeleton);
        GetDrawMatrix(&matrix);
        model->Render(&matrix, false, reinterpret_cast<uint8_t*>(SkeletonOf(skeleton)));
        model->CopyBones(reinterpret_cast<uint8_t*>(SkeletonOf(skeleton)));
        return;
    }
    uint8_t modelFlags = m_meshInstance ? reinterpret_cast<uint8_t*>(m_meshInstance)[0x44] : 0;
    if (!m_meshInstance || !(modelFlags & 2) || (modelFlags & 4))
        return; // (not active, or hidden)
    GetDrawMatrix(&matrix);
    void* skeleton = m_boneRig ? SkeletonOf(m_boneRig) : nullptr;
    bool ready = m_boneRig != nullptr;
    if (mp_animBlender)
        ready = static_cast<CAnimBlenderComponent*>(mp_animBlender)->ModelReady();
    m_meshInstance->Render(&matrix, !ready, reinterpret_cast<uint8_t*>(skeleton));
}

// 0x00420480
void CMeshInstanceModule::JumpTo()
{
    CObjectModule::Update(); // (through the vtable)
    if (m_meshInstance && ModelLights(m_meshInstance))
        UpdateLightsBrightness(ModelLights(m_meshInstance));
}

// 0x00421360
void CMeshInstanceModule::SetModel(Engine3D::CRenderModel* model)
{
    m_meshInstance = model;
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_FINALIZED) {
        CObjectModule::Update();
        if (m_meshInstance && ModelLights(m_meshInstance))
            UpdateLightsBrightness(ModelLights(m_meshInstance));
    }
    Maths::Vector sphere;
    m_enclosingBall = *model->FetchBoundSphere(&sphere);
}

// 0x00421230
void CMeshInstanceModule::SetUpMeshFromProfile(void* profile, bool viaResourceStore, int texDictOffset,
                                           uint32_t buildScript)
{
    Engine3D::CRenderModel* model = m_meshInstance;
    void* skeleton = nullptr;
    if (CObjectModule* component = m_object->GetComponent(kSkeleton))
        skeleton = SkeletonOf(component);
    ModelBuilder builder;
    reinterpret_cast<Gfx2D::CModelAssembler*>(&builder)->Construct(viaResourceStore, texDictOffset);
    reinterpret_cast<Gfx2D::CModelAssembler*>(&builder)->Build(static_cast<Gfx2D::CModelLook*>(profile), model, static_cast<Gfx2D::CRig*>(skeleton), buildScript ? buildScript : kDefaultBuildScript);
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_FINALIZED) {
        CObjectModule::Update();
        if (m_meshInstance && ModelLights(m_meshInstance))
            UpdateLightsBrightness(ModelLights(m_meshInstance));
    }
    Maths::Vector sphere;
    m_enclosingBall = *model->FetchBoundSphere(&sphere);
    if (Scripts::FetchIntValue(kBoneVolumesOn))
        InitBoneVolumes();
    reinterpret_cast<CObject*>(&builder)->DestroyBody();
}

// 0x0041FFA0
void CMeshInstanceModule::InitFromSector(uint32_t name)
{
    void* sector = LookUpSector(name);
    if (!sector)
        return;
    void* geom = SectorGeom(sector);
    if (!geom)
        return;
    void* clone = CloneSectorGeom(geom);
    ActivateGeom(clone);
    AddGeom(m_meshInstance, clone, 0);
    m_sectorModel = true;
}

// 0x00420350
void CMeshInstanceModule::InitBoneVolumes()
{
    if (!m_boneRig) {
        m_boneRig = m_object->GetComponent(kSkeleton);
        if (!m_boneRig)
            return;
    }
    if (m_meshInstance->GetBoneVolumes())
        return;
    MemSys::Manager* manager = MemSys::Manager::Get();
    manager->EnterHeapScope(manager->GetNamedHeap(kBoneVolumeHeap, 1));
    void* skeleton = SkeletonOf(m_boneRig);
    void* data = SkeletonData(skeleton);
    void* volumes = FindBoneVolumes(SkeletonName(skeleton));
    if (volumes) {
        ++*reinterpret_cast<int*>(static_cast<uint8_t*>(volumes) + 8); // (another user)
    } else {
        // Made and registered for the skeleton - but not given to this model (the original's): the next one finds it.
        void* made = MemSys::New(0x0C);
        if (made)
            made = static_cast<Gfx2D::CBoneVolumes*>(made)->Construct(0);
        static_cast<Gfx2D::CBoneVolumes*>(made)->Register(SkeletonName(skeleton));
        static_cast<Gfx2D::CBoneVolumes*>(made)->Fill(static_cast<Gfx2D::CRigData*>(data));
    }
    m_meshInstance->SetBoneVolumes(reinterpret_cast<uint8_t*>(volumes));
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x00420D10
void CMeshInstanceModule::SetUpMesh(Scripts::CStruct* params)
{
    m_meshInstance->DropGeoms();
    Maths::Vector scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (GetScale(&scale, params))
        m_meshInstance->ChangeScale(&scale);
    uint32_t type = 0;
    params->ReadHashValue(kClass, &type, 0);
    if (type == kLevelObject) {
        uint32_t name;
        params->ReadHashValue(kName, &name, 1);
        InitFromSector(name);
    } else {
        int viaResourceStore = 1;
        params->FetchIntValue(kUseAssetManager, &viaResourceStore, 0);
        int texDictOffset = 0;
        params->FetchIntValue(kTexDictOffset, &texDictOffset, 0);
        uint32_t name;
        if (params->ReadHashValue(kRefObjectName, &name, 0)) {
            // A level object's model: its sector's (Update follows it).
            m_linkedEntityLabel = name;
            m_levelObject = true;
            m_meshInstance->SetDisplayMode(6);
        } else if (params->ReadHashValue(kAssetName, &name, 0)) {
            int multicolor = 0;
            params->FetchIntValue(kMulticolor, &multicolor, 0);
            m_meshInstance->AddAssetGeom(name, 0, multicolor != 0, 1);
        } else if (params->ReadHashValue(kCloneFrom, &name, 0)) {
            // Another object's model's geoms cloned (and its colour values).
            auto* object = static_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(name));
            auto* source = static_cast<CMeshInstanceModule*>(object->GetComponent(kModel));
            m_colorValueCount = source->m_colorValueCount;
            for (int i = 0; i < m_colorValueCount; ++i)
                m_colorValues[i] = source->m_colorValues[i];
            uint32_t meshLabel = 0;
            Scripts::CArray* geoms;
            if (params->ReadHashValue(kGeom, &meshLabel, 0)) {
                void* geom = source->m_meshInstance->GetGeomByName(meshLabel);
                if (!geom)
                    return; // (nothing after it either)
                void* clone = CloneModelGeom(geom, m_meshInstance);
                ActivateGeom(clone);
                AddGeom(m_meshInstance, clone, meshLabel);
            } else if (params->GetArray(kGeoms, &geoms, 0)) {
                for (uint32_t i = 0; i < geoms->m_size; ++i) {
                    meshLabel = geoms->GetElement(i);
                    if (!meshLabel)
                        continue;
                    void* geom = source->m_meshInstance->GetGeomByName(meshLabel);
                    if (!geom)
                        return;
                    void* clone = CloneModelGeom(geom, m_meshInstance);
                    ActivateGeom(clone);
                    AddGeom(m_meshInstance, clone, meshLabel);
                }
            } else {
                for (int i = 0; i < source->m_meshInstance->FetchGeomTotal(); ++i) {
                    void* clone = CloneModelGeom(source->m_meshInstance->FetchGeom(i), m_meshInstance);
                    ActivateGeom(clone);
                    AddGeom(m_meshInstance, clone, source->m_meshInstance->GetGeomName(i));
                }
            }
        } else {
            const char* file;
            if (params->GetString(kModel, &file, false) || params->GetString(kModelName, &file, false)) {
                // A model file (unless it's the "none" name).
                if (Checksums::HashFromText(file) != kNoModel) {
                    char path[0x200];
                    uint32_t skeletonName;
                    if (params->ReadHashValue(kSkeletonName, &skeletonName, 0))
                        ModelFileName(file, ".skin", path);
                    else
                        ModelFileName(file, ".mdl", path);
                    TextUtil::ToLowerText(path);
                    bool allowReplace = params->HasBareFlag(kAllowReplaceTex);
                    m_meshInstance->AddFileGeom(static_cast<const char*>(path), 0, viaResourceStore != 0, texDictOffset, allowReplace, 0, 1, 0);
                }
            } else if (params->ReadHashValue(kAsset, &name, 0)) {
                m_meshInstance->AddAssetGeom(name, 0, 0, 1);
            } else if (params->ReadHashValue(kModel, &name, 0)) {
                // A level sector's geometry as the model.
                void* clone = CloneSectorGeom(SectorGeom(LookUpSector(name)));
                ActivateGeom(clone);
                AddGeom(m_meshInstance, clone, 0);
            }
        }
    }
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_FINALIZED) {
        CObjectModule::Update();
        if (m_meshInstance && ModelLights(m_meshInstance))
            UpdateLightsBrightness(ModelLights(m_meshInstance));
    }
    Maths::Vector sphere;
    m_enclosingBall = *m_meshInstance->FetchBoundSphere(&sphere);
    if (Scripts::FetchIntValue(kBoneVolumesOn))
        InitBoneVolumes();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CMeshInstanceModule::Destroy,
         &GameObjects::CMeshInstanceModule::Update,
         &GameObjects::CMeshInstanceModule::ConfigureFromData,
         &GameObjects::CMeshInstanceModule::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         &GameObjects::CMeshInstanceModule::Hide,
         &GameObjects::CMeshInstanceModule::JumpTo,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CMeshInstanceModule::InvokeMethodByName,
         &GameObjects::CMeshInstanceModule::SetUpMeshFromProfile,
         &GameObjects::CMeshInstanceModule::SetModel};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
