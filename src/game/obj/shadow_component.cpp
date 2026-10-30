// The shadow component (game/obj/shadow_component.h).

#include "game/obj/shadow_component.h"

#include <cstdint>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_shadows.h"
#include "game/gfx/model.h"
#include "game/mth/matrix.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646498);

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kShadowType = 0x9AC24B18;  // ShadowType
constexpr uint32_t kDetailed = 0x76A54CD1;    // detailed
constexpr uint32_t kSimple = 0x3E84C2FD;      // simple
constexpr uint32_t kVolume = 0x46653221;      // Volume
constexpr uint32_t kNone = 0x806FFF30;        // none
constexpr uint32_t kStayPut = 0x7E773A11;     // (a flag: the shadow doesn't follow the object; name unknown)
constexpr uint32_t kShadowModel = 0x545F8172; // ShadowModel
constexpr uint32_t kShadowScale = 0x6F8CD62F; // ShadowScale
constexpr uint32_t kOffset = 0xA6F5352F;      // offset
constexpr uint32_t kShadowOff = 0xAAF6E513;   // Obj_ShadowOff
constexpr uint32_t kShadowOn = 0xF272C43A;    // Obj_ShadowOn

enum ShadowKind { SHADOW_DETAILED = 1, SHADOW_SIMPLE = 2, SHADOW_VOLUME = 3 };

// The detailed and Volume shadows are cast along this over this distance.
const Mth::Vector kDown = {0.0f, -1.0f, 0.0f, 1.0f};
constexpr float kCastDistance = 72.0f;
// A detailed shadow is placed this far up the object's up.
constexpr float kDetailedHeight = 36.0f;

// The renderer's shadow object (see the class comment).
void** Vtable(void* shadow) { return *static_cast<void***>(shadow); }
int& ShadowKindOf(void* shadow) { return *reinterpret_cast<int*>(static_cast<uint8_t*>(shadow) + 0x04); }
float& ShadowScaleOf(void* shadow) { return *reinterpret_cast<float*>(static_cast<uint8_t*>(shadow) + 0x0C); }
float& ShadowOffsetOf(void* shadow) { return *reinterpret_cast<float*>(static_cast<uint8_t*>(shadow) + 0x10); }
void DeleteShadow(void* shadow) { reinterpret_cast<void*(__thiscall*)(void*, int)>(Vtable(shadow)[0])(shadow, 1); }
void ShadowUpdate(void* shadow, const Mth::Vector* pos, const Mth::Matrix* matrix, Mth::Vector normal)
{
    reinterpret_cast<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Matrix*, Mth::Vector)>(Vtable(shadow)[1])(
        shadow, pos, matrix, normal);
}
void ShadowSetPosition(void* shadow, Mth::Vector pos)
{
    reinterpret_cast<void(__thiscall*)(void*, Mth::Vector)>(Vtable(shadow)[2])(shadow, pos);
}
void ShadowSetMatrix(void* shadow, const Mth::Matrix* matrix)
{
    reinterpret_cast<void(__thiscall*)(void*, const Mth::Matrix*)>(Vtable(shadow)[4])(shadow, matrix);
}
void ShadowHide(void* shadow) { reinterpret_cast<void(__thiscall*)(void*)>(Vtable(shadow)[5])(shadow); }
void ShadowShow(void* shadow) { reinterpret_cast<void(__thiscall*)(void*)>(Vtable(shadow)[6])(shadow); }

// The model component's model (+0x208), and that model's flags (+0x44: 0x2 visible, 0x4 hidden), its +0x1C (a
// simple shadow's first scale), its checksum (+0x30) and its +0x2C object (vtable slot 5 gives a vector).
uint8_t* ModelOf(CBaseComponent* component) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208); }
// 0x004949B0 (not rewritten yet): whether the model casts a shadow (its flag 0x10).
void SetModelShadow(uint8_t* model, bool on) { reinterpret_cast<Nx::CModel*>(model)->EnableShadow(on); }
// 0x00493040 (not rewritten yet): the model's hierarchy (0x50-byte objects from +0x50; the root's matrix at +0x10).
const uint8_t* HierarchyOf(uint8_t* model)
{
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<Nx::CModel*>(model)->GetAssetHierarchy());
}

} // namespace

// 0x00431800
CBaseComponent* __cdecl CShadowComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CShadowComponent));
    return memory ? static_cast<CShadowComponent*>(memory)->Construct() : nullptr;
}

// 0x00431380
CShadowComponent* CShadowComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    mp_shadow = nullptr;
    m_normal = {0.0f, 1.0f, 0.0f, 1.0f};
    m_shadowType = kNone;
    m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_followObject = true;
    return this;
}

// 0x00431300
void CShadowComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_shadow) {
        DeleteShadow(mp_shadow);
        mp_shadow = nullptr;
    }
    CBaseComponent::DestroyBody();
}

// 0x004313D0
CShadowComponent* CShadowComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00431400
void CShadowComponent::Finalize()
{
    mp_model = m_object->GetComponent(kModel);
    uint32_t checksum = *reinterpret_cast<uint32_t*>(ModelOf(mp_model) + 0x30);
    void* asset = GameVariable<Ass::CAssMan*>(0x006918DC)->GetAssetData(checksum, 0);
    if (mp_shadow)
        reinterpret_cast<void(__thiscall*)(void*, uint8_t*, void*)>(Vtable(mp_shadow)[7])(mp_shadow, ModelOf(mp_model),
                                                                                         asset);
}

// 0x00431450
void CShadowComponent::RefreshShadow()
{
    if (!mp_shadow || !mp_model)
        return;
    uint8_t* model = ModelOf(mp_model);
    if (!model)
        return;
    uint8_t flags = model[0x44];
    if (!(flags & 0x2) || (flags & 0x4)) {
        ShadowHide(mp_shadow);
        return;
    }
    switch (ShadowKindOf(mp_shadow)) {
    case SHADOW_DETAILED: {
        const CCompositeObject* object = m_object;
        Mth::Vector above = object->m_pos + object->m_matrix.row[1] * kDetailedHeight;
        // (the model's +0x2C object's slot 5 vector, turned round - and not used)
        if (void* fader = *reinterpret_cast<void**>(model + 0x2C)) {
            const Mth::Vector* v = reinterpret_cast<const Mth::Vector*(__thiscall*)(void*, int)>(Vtable(fader)[5])(fader, 0);
            Mth::Vector unused = *v * -1.0f;
            (void)unused;
        }
        ShadowSetPosition(mp_shadow, above);
        break;
    }
    case SHADOW_SIMPLE:
        ShadowScaleOf(mp_shadow) = *reinterpret_cast<float*>(model + 0x1C);
        [[fallthrough]];
    case SHADOW_VOLUME:
        ShadowUpdate(mp_shadow, &m_pos, &m_object->m_matrix, m_normal);
        break;
    default:
        break;
    }
    ShadowShow(mp_shadow);
}

// 0x00431570
void CShadowComponent::Hide(bool hide)
{
    if (!mp_shadow)
        return;
    if (hide)
        ShadowHide(mp_shadow);
    else
        ShadowShow(mp_shadow);
}

// 0x004315A0
void CShadowComponent::CreateShadow(int type, const char* modelName)
{
    if (mp_shadow)
        return;
    CBaseComponent* modelComponent = m_object->GetComponent(kModel);
    uint8_t* model = modelComponent ? ModelOf(modelComponent) : nullptr;
    switch (type) {
    case SHADOW_DETAILED: {
        void* memory = Mem::Calloc(0x24);
        mp_shadow = memory ? static_cast<Nx::CDetailedShadow*>(memory)->Construct(reinterpret_cast<Nx::CModel*>(model), kDown,
                                                                                      kCastDistance)
                           : nullptr;
        return;
    }
    case SHADOW_SIMPLE: {
        void* memory = Mem::Calloc(0x14);
        void* shadow = memory ? static_cast<Nx::CSimpleShadow*>(memory)->Construct() : nullptr;
        // (no shadow - out of memory - is written through regardless, as the original)
        ShadowScaleOf(shadow) = *reinterpret_cast<float*>(model + 0x1C);
        static_cast<Nx::CSimpleShadow*>(shadow)->SetModel(modelName);
        mp_shadow = shadow;
        break;
    }
    case SHADOW_VOLUME: {
        void* memory = Mem::New(0x68);
        mp_shadow = memory ? static_cast<Nx::CShadowVolume*>(memory)->Construct(reinterpret_cast<Nx::CModel*>(model), kDown,
                                                                                   kCastDistance)
                           : nullptr;
        break;
    }
    default:
        return;
    }
    ShadowUpdate(mp_shadow, &m_pos, &m_object->m_matrix, m_normal);
}

// 0x00431780
void CShadowComponent::DestroyShadow()
{
    if (!mp_shadow)
        return;
    if (mp_model)
        SetModelShadow(ModelOf(mp_model), false);
    if (mp_shadow)
        DeleteShadow(mp_shadow);
    mp_shadow = nullptr;
}

// 0x004317C0
void CShadowComponent::SetPosition(const Mth::Vector* position) { m_pos = *position; }

// 0x004317E0
void CShadowComponent::SetNormal(const Mth::Vector* normal) { m_normal = *normal; }

// 0x00431360
void CShadowComponent::SetLightDirection(const Mth::Vector* direction)
{
    if (mp_shadow)
        reinterpret_cast<void(__thiscall*)(void*, const Mth::Vector*)>(Vtable(mp_shadow)[3])(mp_shadow, direction);
}

// 0x00431860
void CShadowComponent::InitFromStructure(Script::CStruct* params)
{
    m_shadowType = kSimple;
    params->GetChecksum(kShadowType, &m_shadowType, 0);
    if (params->ContainsFlag(kStayPut))
        m_followObject = false;
    DestroyShadow();
    switch (m_shadowType) {
    case kDetailed:
        if (!mp_shadow)
            CreateShadow(SHADOW_DETAILED, nullptr);
        break;
    case kSimple: {
        const char* model = "Ped_Shadow";
        params->GetString(kShadowModel, &model, 0);
        if (!mp_shadow)
            CreateShadow(SHADOW_SIMPLE, model);
        float scale = 1.0f;
        params->GetFloat(kShadowScale, &scale, 0);
        ShadowScaleOf(mp_shadow) = scale;
        ShadowUpdate(mp_shadow, &m_pos, &m_object->m_matrix, m_normal);
        break;
    }
    case kVolume:
        if (!mp_shadow)
            CreateShadow(SHADOW_VOLUME, nullptr);
        break;
    case kNone:
        if (mp_shadow)
            DestroyShadow();
        break;
    default:
        break;
    }
}

// 0x004319D0
void CShadowComponent::Update()
{
    if (m_followObject) {
        m_pos = m_object->m_pos;
        if (mp_shadow) {
            Mth::Matrix matrix = m_object->m_displayMatrix;
            Mth::Vector pos = m_object->m_pos;
            uint8_t* model = mp_model ? ModelOf(mp_model) : nullptr;
            if (model && HierarchyOf(model)) {
                // The model's root: its matrix times the display matrix, at the object's position plus its translation.
                const float* root = reinterpret_cast<const float*>(HierarchyOf(model) + 0x40);
                pos = {root[0], root[1], root[2], 0.0f};
                Mth::Matrix rootMatrix = *reinterpret_cast<const Mth::Matrix*>(HierarchyOf(model) + 0x10);
                (void)rootMatrix; // (copied and not used: the product reads the hierarchy's again)
                pos += m_object->m_pos;
                matrix = Mth::MatrixProduct{reinterpret_cast<const Mth::Matrix*>(HierarchyOf(model) + 0x10),
                                            &m_object->m_displayMatrix};
            }
            matrix.row[3] = pos;
            ShadowSetMatrix(mp_shadow, &matrix);
        }
    }
    RefreshShadow();
}

// 0x00431B10
int CShadowComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* /*script*/)
{
    if (name == kShadowOff) {
        DestroyShadow();
        return 1;
    }
    if (name != kShadowOn)
        return 2;
    uint32_t type; // (read into the parameters' own argument slot)
    if (!params->GetChecksum(kShadowType, &type, 0))
        params->AddChecksum(kShadowType, kSimple);
    reinterpret_cast<void(__thiscall*)(CShadowComponent*, Script::CStruct*)>(m_vtable[2])(this, params);
    if (mp_shadow && ShadowKindOf(mp_shadow) == SHADOW_SIMPLE) {
        float offset = 0.2f; // (read into the name's argument slot)
        params->GetFloat(kOffset, &offset, 0);
        ShadowOffsetOf(mp_shadow) = offset;
    }
    return 1;
}

} // namespace Obj

REPLACE(0x00431800, Obj::CShadowComponent::Create);
REPLACE(0x00431380, &Obj::CShadowComponent::Construct);
REPLACE(0x00431300, &Obj::CShadowComponent::DestroyBody);
REPLACE(0x004313D0, &Obj::CShadowComponent::Destroy);
REPLACE(0x00431400, &Obj::CShadowComponent::Finalize);
REPLACE(0x00431450, &Obj::CShadowComponent::RefreshShadow);
REPLACE(0x00431570, &Obj::CShadowComponent::Hide);
REPLACE(0x004315A0, &Obj::CShadowComponent::CreateShadow);
REPLACE(0x00431780, &Obj::CShadowComponent::DestroyShadow);
REPLACE(0x004317C0, &Obj::CShadowComponent::SetPosition);
REPLACE(0x004317E0, &Obj::CShadowComponent::SetNormal);
REPLACE(0x00431360, &Obj::CShadowComponent::SetLightDirection);
REPLACE(0x00431860, &Obj::CShadowComponent::InitFromStructure);
REPLACE(0x004319D0, &Obj::CShadowComponent::Update);
REPLACE(0x00431B10, &Obj::CShadowComponent::CallMemberFunction);
