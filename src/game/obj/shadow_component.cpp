// The shadow component (game/obj/shadow_component.h).

#include "game/obj/shadow_component.h"

#include <cstdint>

#include "game/assets/resource_registry.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_shadows.h"
#include "game/gfx/model.h"
#include "game/maths/mat4.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/gfx/weather.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kShadowType = 0x9AC24B18;  // ShadowType
constexpr uint32_t kDetailed = 0x76A54CD1;    // detailed
constexpr uint32_t kSimple = 0x3E84C2FD;      // simple
constexpr uint32_t kVolume = 0x46653221;      // Volume
constexpr uint32_t kNone = 0x806FFF30;        // none
constexpr uint32_t kStayPut = 0x7E773A11;     // (a flag: the shadow doesn't follow the object; name unknown)
constexpr uint32_t kShadowModel = 0x545F8172; // ShadowModel
constexpr uint32_t kBlobSizeFactor = 0x6F8CD62F; // ShadowScale
constexpr uint32_t kOffset = 0xA6F5352F;      // offset
constexpr uint32_t kCastNoShadow = 0xAAF6E513;   // Obj_ShadowOff
constexpr uint32_t kEnableShadowCast = 0xF272C43A;    // Obj_ShadowOn

enum ShadowKind { SHADOW_DETAILED = 1, SHADOW_SIMPLE = 2, SHADOW_VOLUME = 3 };

// The detailed and Volume shadows are cast along this over this distance.
const Maths::Vec4 kDown = {0.0f, -1.0f, 0.0f, 1.0f};
constexpr float kCastDistance = 72.0f;
// A detailed shadow is placed this far up the object's up.
constexpr float kDetailedHeight = 36.0f;

// The renderer's shadow object (see the class comment).
void** Vtable(void* shadow) { return *static_cast<void***>(shadow); }
int& ShadowKindOf(void* shadow) { return *reinterpret_cast<int*>(static_cast<uint8_t*>(shadow) + 0x04); }
float& ShadowScaleOf(void* shadow) { return *reinterpret_cast<float*>(static_cast<uint8_t*>(shadow) + 0x0C); }
float& ShadowOffsetOf(void* shadow) { return *reinterpret_cast<float*>(static_cast<uint8_t*>(shadow) + 0x10); }
void ReleaseBlobShadow(void* shadow) { reinterpret_cast<void*(__thiscall*)(void*, int)>(Vtable(shadow)[0])(shadow, 1); }
void ShadowUpdate(void* shadow, const Maths::Vec4* pos, const Maths::Mat4* matrix, Maths::Vec4 normal)
{
    reinterpret_cast<void(__thiscall*)(void*, const Maths::Vec4*, const Maths::Mat4*, Maths::Vec4)>(Vtable(shadow)[1])(
        shadow, pos, matrix, normal);
}
void ShadowSetPosition(void* shadow, Maths::Vec4 pos)
{
    reinterpret_cast<void(__thiscall*)(void*, Maths::Vec4)>(Vtable(shadow)[2])(shadow, pos);
}
void ShadowSetMatrix(void* shadow, const Maths::Mat4* matrix)
{
    reinterpret_cast<void(__thiscall*)(void*, const Maths::Mat4*)>(Vtable(shadow)[4])(shadow, matrix);
}
void ShadowHide(void* shadow) { reinterpret_cast<void(__thiscall*)(void*)>(Vtable(shadow)[5])(shadow); }
void ShadowShow(void* shadow) { reinterpret_cast<void(__thiscall*)(void*)>(Vtable(shadow)[6])(shadow); }

// The model component's model (+0x208), and that model's flags (+0x44: 0x2 visible, 0x4 hidden), its +0x1C (a
// simple shadow's first scale), its checksum (+0x30) and its +0x2C object (vtable slot 5 gives a vector).
uint8_t* ModelOf(CObjectModule* component) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208); }
// 0x004949B0 (not rewritten yet): whether the model casts a shadow (its flag 0x10).
void SetModelShadow(uint8_t* model, bool on) { reinterpret_cast<Engine3D::CRenderModel*>(model)->ActivateShadow(on); }
// 0x00493040 (not rewritten yet): the model's hierarchy (0x50-byte objects from +0x50; the root's matrix at +0x10).
const uint8_t* HierarchyOf(uint8_t* model)
{
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<Engine3D::CRenderModel*>(model)->GetAssetHierarchy());
}

} // namespace

// 0x00431800
CObjectModule* __cdecl CGroundShadowModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CGroundShadowModule));
    return memory ? static_cast<CGroundShadowModule*>(memory)->MakeNew() : nullptr;
}

// 0x00431380
CGroundShadowModule* CGroundShadowModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kType;
    m_shadowHandle = nullptr;
    m_surfaceDir = {0.0f, 1.0f, 0.0f, 1.0f};
    m_shadowKind = kNone;
    m_atPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_followObject = true;
    return this;
}

// 0x00431300
void CGroundShadowModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_shadowHandle) {
        ReleaseBlobShadow(m_shadowHandle);
        m_shadowHandle = nullptr;
    }
    CObjectModule::DestroyBody();
}

// 0x004313D0
CGroundShadowModule* CGroundShadowModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00431400
void CGroundShadowModule::Seal()
{
    m_meshInstance = m_entity->FetchModule(kModel);
    uint32_t checksum = *reinterpret_cast<uint32_t*>(ModelOf(m_meshInstance) + 0x30);
    void* asset = AssetStore::g_assetManager->GetAssetData(checksum, 0);
    if (m_shadowHandle)
        reinterpret_cast<void(__thiscall*)(void*, uint8_t*, void*)>(Vtable(m_shadowHandle)[7])(m_shadowHandle, ModelOf(m_meshInstance),
                                                                                         asset);
}

// 0x00431450
void CGroundShadowModule::RefreshShadow()
{
    if (!m_shadowHandle || !m_meshInstance)
        return;
    uint8_t* model = ModelOf(m_meshInstance);
    if (!model)
        return;
    uint8_t flags = model[0x44];
    if (!(flags & 0x2) || (flags & 0x4)) {
        ShadowHide(m_shadowHandle);
        return;
    }
    switch (ShadowKindOf(m_shadowHandle)) {
    case SHADOW_DETAILED: {
        const CGameEntity* object = m_entity;
        Maths::Vec4 above = object->m_atPoint + object->m_basisFrame.row[1] * kDetailedHeight;
        // (the model's +0x2C object's slot 5 vector, turned round - and not used)
        if (void* fader = *reinterpret_cast<void**>(model + 0x2C)) {
            const Maths::Vec4* v = reinterpret_cast<const Maths::Vec4*(__thiscall*)(void*, int)>(Vtable(fader)[5])(fader, 0);
            Maths::Vec4 unused = *v * -1.0f;
            (void)unused;
        }
        ShadowSetPosition(m_shadowHandle, above);
        break;
    }
    case SHADOW_SIMPLE:
        ShadowScaleOf(m_shadowHandle) = *reinterpret_cast<float*>(model + 0x1C);
        [[fallthrough]];
    case SHADOW_VOLUME:
        ShadowUpdate(m_shadowHandle, &m_atPoint, &m_entity->m_basisFrame, m_surfaceDir);
        break;
    default:
        break;
    }
    ShadowShow(m_shadowHandle);
}

// 0x00431570
void CGroundShadowModule::Conceal(bool hide)
{
    if (!m_shadowHandle)
        return;
    if (hide)
        ShadowHide(m_shadowHandle);
    else
        ShadowShow(m_shadowHandle);
}

// 0x004315A0
void CGroundShadowModule::BuildBlobShadow(int type, const char* modelName)
{
    if (m_shadowHandle)
        return;
    CObjectModule* modelComponent = m_entity->FetchModule(kModel);
    uint8_t* model = modelComponent ? ModelOf(modelComponent) : nullptr;
    switch (type) {
    case SHADOW_DETAILED: {
        void* memory = MemSys::AllocZeroed(0x24);
        m_shadowHandle = memory ? static_cast<Engine3D::CFullShadow*>(memory)->MakeNew(reinterpret_cast<Engine3D::CRenderModel*>(model), kDown,
                                                                                      kCastDistance)
                           : nullptr;
        return;
    }
    case SHADOW_SIMPLE: {
        void* memory = MemSys::AllocZeroed(0x14);
        void* shadow = memory ? static_cast<Engine3D::CBlobShadow*>(memory)->MakeNew() : nullptr;
        // (no shadow - out of memory - is written through regardless, as the original)
        ShadowScaleOf(shadow) = *reinterpret_cast<float*>(model + 0x1C);
        static_cast<Engine3D::CBlobShadow*>(shadow)->ChangeMesh(modelName);
        m_shadowHandle = shadow;
        break;
    }
    case SHADOW_VOLUME: {
        void* memory = MemSys::Alloc(0x68);
        m_shadowHandle = memory ? static_cast<Engine3D::CShadowVolume*>(memory)->MakeNew(reinterpret_cast<Engine3D::CRenderModel*>(model), kDown,
                                                                                   kCastDistance)
                           : nullptr;
        break;
    }
    default:
        return;
    }
    ShadowUpdate(m_shadowHandle, &m_atPoint, &m_entity->m_basisFrame, m_surfaceDir);
}

// 0x00431780
void CGroundShadowModule::DestroyShadow()
{
    if (!m_shadowHandle)
        return;
    if (m_meshInstance)
        SetModelShadow(ModelOf(m_meshInstance), false);
    if (m_shadowHandle)
        ReleaseBlobShadow(m_shadowHandle);
    m_shadowHandle = nullptr;
}

// 0x004317C0
void CGroundShadowModule::ChangePlace(const Maths::Vec4* position) { m_atPoint = *position; }

// 0x004317E0
void CGroundShadowModule::SetNormal(const Maths::Vec4* normal) { m_surfaceDir = *normal; }

// 0x00431360
void CGroundShadowModule::SetLightDirection(const Maths::Vec4* direction)
{
    if (m_shadowHandle)
        reinterpret_cast<void(__thiscall*)(void*, const Maths::Vec4*)>(Vtable(m_shadowHandle)[3])(m_shadowHandle, direction);
}

// 0x00431860
void CGroundShadowModule::ConfigureFromData(Scripts::CStruct* params)
{
    m_shadowKind = kSimple;
    params->ReadHashValue(kShadowType, &m_shadowKind, 0);
    if (params->HasBareFlag(kStayPut))
        m_followObject = false;
    DestroyShadow();
    switch (m_shadowKind) {
    case kDetailed:
        if (!m_shadowHandle)
            BuildBlobShadow(SHADOW_DETAILED, nullptr);
        break;
    case kSimple: {
        const char* model = "Ped_Shadow";
        params->FetchText(kShadowModel, &model, 0);
        if (!m_shadowHandle)
            BuildBlobShadow(SHADOW_SIMPLE, model);
        float scale = 1.0f;
        params->FetchFloat(kBlobSizeFactor, &scale, 0);
        ShadowScaleOf(m_shadowHandle) = scale;
        ShadowUpdate(m_shadowHandle, &m_atPoint, &m_entity->m_basisFrame, m_surfaceDir);
        break;
    }
    case kVolume:
        if (!m_shadowHandle)
            BuildBlobShadow(SHADOW_VOLUME, nullptr);
        break;
    case kNone:
        if (m_shadowHandle)
            DestroyShadow();
        break;
    default:
        break;
    }
}

// 0x004319D0
void CGroundShadowModule::TickFrame()
{
    if (m_followObject) {
        m_atPoint = m_entity->m_atPoint;
        if (m_shadowHandle) {
            Maths::Mat4 matrix = m_entity->m_displayMatrix;
            Maths::Vec4 pos = m_entity->m_atPoint;
            uint8_t* model = m_meshInstance ? ModelOf(m_meshInstance) : nullptr;
            if (model && HierarchyOf(model)) {
                // The model's root: its matrix times the display matrix, at the object's position plus its translation.
                const float* root = reinterpret_cast<const float*>(HierarchyOf(model) + 0x40);
                pos = {root[0], root[1], root[2], 0.0f};
                Maths::Mat4 baseMat = *reinterpret_cast<const Maths::Mat4*>(HierarchyOf(model) + 0x10);
                (void)baseMat; // (copied and not used: the product reads the hierarchy's again)
                pos += m_entity->m_atPoint;
                matrix = Maths::MatrixProduct{reinterpret_cast<const Maths::Mat4*>(HierarchyOf(model) + 0x10),
                                            &m_entity->m_displayMatrix};
            }
            matrix.row[3] = pos;
            ShadowSetMatrix(m_shadowHandle, &matrix);
        }
    }
    RefreshShadow();
}

// 0x00431B10
int CGroundShadowModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (name == kCastNoShadow) {
        DestroyShadow();
        return 1;
    }
    if (name != kEnableShadowCast)
        return 2;
    uint32_t type; // (read into the parameters' own argument slot)
    if (!params->ReadHashValue(kShadowType, &type, 0))
        params->PutNameKey(kShadowType, kSimple);
    reinterpret_cast<void(__thiscall*)(CGroundShadowModule*, Scripts::CStruct*)>(m_vtable[2])(this, params);
    if (m_shadowHandle && ShadowKindOf(m_shadowHandle) == SHADOW_SIMPLE) {
        float offset = 0.2f; // (read into the name's argument slot)
        params->FetchFloat(kOffset, &offset, 0);
        ShadowOffsetOf(m_shadowHandle) = offset;
    }
    return 1;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CGroundShadowModule::Teardown,
         &GameObjects::CGroundShadowModule::TickFrame,
         &GameObjects::CGroundShadowModule::ConfigureFromData,
         &GameObjects::CGroundShadowModule::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         &GameObjects::CGroundShadowModule::Conceal,
         Engine3D::VirtualSlot1Jump,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CGroundShadowModule::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
