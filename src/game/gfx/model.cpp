// The engine's model: its members on the geom list - counts, flags, bones, visibility, colours, lights and UVs. See
// model.h (loading, drawing and the rest: model_geoms.cpp).

#include "game/gfx/model.h"

#include "game/ass/assman.h"
#include "game/gfx/model_internal.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

using namespace ModelGeoms;

namespace {

// A flag of m_flags set from a bit (the setters take a flag's low bit, as the original's shifts and masks).
uint8_t SetFlag(uint8_t flags, uint8_t flag, int bit) { return static_cast<uint8_t>(bit ? flags | flag : flags & ~flag); }

// The first geom's entry (as the original: through the head's next even when there's no head).
sModelGeom* FirstEntry(CModel* model) { return Entry(First(model)); }

// An asset's scene data's +0xB8 / +0xB4 (the folded getters 0x00490AC0 / 0x00490AB0).
uint32_t SceneB8(void* scene) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x00490AC0)(scene); }
uint32_t SceneB4(void* scene) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x00490AB0)(scene); }

// Lights made and deleted (the engine's 0x0048EA80 / 0x0048C080).
uint8_t* NewLights() { return GameFunction<uint8_t*(__cdecl*)()>(0x0048EA80)(); }

} // namespace

// 0x00492DC0
bool CModel::VirtualTrue4(int, int, int, int) { return true; }

// 0x00492EB0
Mth::Vector* CModel::PlatGetBoundingSphere(Mth::Vector* out)
{
    out->z = 0.0f;
    out->y = 36.0f;
    out->x = 0.0f;
    out->w = m_numBones > 0 ? 48.0f : 1200.0f;
    return out;
}

// 0x00492DD0
bool CModel::SetSkeleton(uint8_t* skeleton)
{
    if (!m_boneMatrices) {
        m_numBones = SkeletonBoneCount(skeleton);
        ModelVirtual<bool>(this, 1, SkeletonBoneCount(skeleton));
        m_boneMatrices = SkeletonBones(skeleton);
    }
    return true;
}

// 0x00492E10
bool CModel::SetDisplayMode(int mode)
{
    if (mode == m_displayMode)
        return false;
    bool allowed = ModelVirtual<bool>(this, 3, mode);
    if (allowed)
        m_displayMode = mode;
    return allowed;
}

// 0x00492E40
int CModel::GetNumGeoms() { return m_numGeoms; }

// 0x00492E50
uint8_t* CModel::GetBoneMatrices() { return m_boneMatrices; }

// 0x00493460
void CModel::SetBoneMatrices(uint8_t* matrices)
{
    m_boneMatrices = matrices;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomBones(Geom(node), m_boneMatrices, m_numBones);
}

// 0x00492E60
void CModel::EnableScaling(uint8_t on) { m_flags = SetFlag(m_flags, kModelScaled, on & 1); }

// 0x00492E80
void CModel::SetBoundingSphere(const Mth::Vector* sphere)
{
    m_flags &= ~kModelSphereValid;
    ModelVirtual<void>(this, 13, sphere);
}

// 0x00492E90
void CModel::EnableShadowVolume(uint8_t on) { m_flags = SetFlag(m_flags, kModelShadowVolume, on & 1); }

// 0x00492EE0
bool CModel::SetScale(const Mth::Vector* scale)
{
    m_scale = *scale;
    // (compared as bit patterns)
    const auto* bits = reinterpret_cast<const uint32_t*>(scale);
    bool scaled = !(bits[0] == 0x3F800000 && bits[1] == 0x3F800000 && bits[2] == 0x3F800000);
    m_flags = SetFlag(m_flags, kModelScaled, scaled);
    return true;
}

// 0x00492F30
Mth::Vector* CModel::GetBoundingSphere(Mth::Vector* out)
{
    if (!(m_flags & kModelSphereValid)) {
        Mth::Vector sphere;
        m_boundingSphere = *ModelVirtual<Mth::Vector*>(this, 12, &sphere);
        m_flags |= kModelSphereValid;
    }
    *out = m_boundingSphere;
    return out;
}

// 0x00492F90
void CModel::SetBoneVolumes(uint8_t* volumes)
{
    if (!m_geomListSize)
        return;
    sModelGeom* first = FirstEntry(this);
    if (first && !first->boneVolumes)
        first->boneVolumes = volumes;
}

// 0x00492FC0
uint8_t* CModel::GetBoneVolumes()
{
    if (!m_geomListSize)
        return nullptr;
    sModelGeom* first = FirstEntry(this);
    return first ? first->boneVolumes : nullptr;
}

// 0x00492FF0
uint32_t CModel::GetAssetBoneCount()
{
    if (m_numAssetGeoms != 1)
        return 0;
    return SceneB8(Ass::AssetManager()->GetAssetData(FirstEntry(this)->assetName, 1));
}

// 0x00493040
uint32_t CModel::GetAssetHierarchy()
{
    if (m_numAssetGeoms != 1)
        return 0;
    return SceneB4(Ass::AssetManager()->GetAssetData(FirstEntry(this)->assetName, 1));
}

// 0x00493410
bool CModel::CopyBones(uint8_t* skeleton)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        CopyGeomBones(Geom(node), SkeletonBones(skeleton), SkeletonBoneCount(skeleton));
    return true;
}

// 0x00493B30
uint32_t CModel::GetStripBreakFlags()
{
    uint32_t flags = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        sModelGeom* entry = Entry(node);
        if (entry->scene) {
            flags |= *reinterpret_cast<uint32_t*>(entry->scene + 0xB0);
        } else if (entry->assetName) {
            auto* scene = static_cast<uint8_t*>(Ass::AssetManager()->GetAssetData(entry->assetName, 1));
            if (scene)
                flags |= *reinterpret_cast<uint32_t*>(scene + 0xB0);
        }
    }
    return flags;
}

// 0x00493B90
void CModel::BreakStrips(uint32_t mask)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        BreakGeomStrips(Geom(node), mask);
}

// 0x00494700
void CModel::CleanInstanceStrips()
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        CleanGeomInstanceStrips(Geom(node));
}

// 0x00494B30
bool CModel::BreakOwnStrips()
{
    uint32_t mask = GetStripBreakFlags();
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        BreakGeomStrips(Geom(node), mask);
    return true;
}

// 0x00493BD0
bool CModel::CreateLights()
{
    mp_lights = NewLights();
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomLights(Geom(node), mp_lights);
    return true;
}

// 0x00493C10
bool CModel::DestroyLights()
{
    DeleteScene(mp_lights);
    mp_lights = nullptr;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomLights(Geom(node), nullptr);
    return true;
}

// 0x00493C60
bool CModel::SetActive(uint8_t active)
{
    if ((m_flags & kModelHidden) && active)
        return false;
    if (((m_flags >> 1) & 1) != active) {
        for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
            SetGeomActive(Geom(node), active);
        m_flags = SetFlag(m_flags, kModelActive, active & 1);
    }
    return true;
}

// 0x00494B70
bool CModel::Hide(uint8_t hide)
{
    m_flags = SetFlag(m_flags, kModelHidden, hide & 1);
    return SetActive(hide == 0);
}

// 0x00493CD0
void* CModel::GetGeomByName(uint32_t name)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if (name == Entry(node)->name)
            return Geom(node);
    }
    return nullptr;
}

// 0x00494640
void* CModel::GetGeom(int index)
{
    int i = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++i) {
        if (index == i)
            return Geom(node);
    }
    return nullptr;
}

// 0x00494680
uint32_t CModel::GetGeomName(int index)
{
    int i = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++i) {
        if (index == i)
            return Entry(node)->name;
    }
    return 0;
}

// 0x004946C0
uint8_t* CModel::GetGeomTextures(int index)
{
    int i = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++i) {
        if (index == i)
            return Entry(node)->textures;
    }
    return nullptr;
}

// 0x00493FB0
bool CModel::ClearGeomColor(uint32_t name)
{
    bool all = name == kAllGeoms;
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if (name == Entry(node)->name || all) {
            ModelGeoms::ClearGeomColor(Geom(node));
            any = true;
        }
    }
    return any;
}

// 0x00494110
bool CModel::SetUVOffset(uint32_t material, int pass, float u, float v)
{
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        any |= GeomCall<bool>(0x0048DFE0, Geom(node), material, pass, u, v);
    return any;
}

// 0x00494160
bool CModel::SetUVMatrix(uint32_t material, int pass, const Mth::Matrix* matrix)
{
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        any |= GeomCall<bool>(0x0048DFF0, Geom(node), material, pass, matrix);
    return any;
}

// 0x004941B0
bool CModel::AllocateUVMatrixParams(uint32_t material, int pass)
{
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        any |= GeomCall<bool>(0x0048E000, Geom(node), material, pass);
    return any;
}

// 0x00494800
bool CModel::HideGeom(uint32_t name, bool hide)
{
    bool all = name == kAllGeoms;
    bool any = false;
    int index = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++index) {
        if (name == Entry(node)->name || all) {
            if (hide)
                m_visibleGeoms &= ~GeomBit(index);
            else
                m_visibleGeoms |= GeomBit(index);
            any = true;
        }
    }
    return any;
}

// 0x00494880
bool CModel::IsGeomHidden(uint32_t name)
{
    bool hidden = true;
    int index = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++index) {
        if (name == Entry(node)->name && (m_visibleGeoms & GeomBit(index)))
            hidden = false;
    }
    return hidden;
}

// 0x004948D0
bool CModel::SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t rgba = r | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
                    (static_cast<uint32_t>(a) << 24);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomColor(Geom(node), rgba);
    return true;
}

// 0x00494930
uint32_t CModel::GetVisibility()
{
    uint32_t mask = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        mask |= GetGeomVisibility(Geom(node));
    return mask;
}

// 0x00494970
bool CModel::SetVisibility(uint32_t mask)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomVisibility(Geom(node), mask);
    return true;
}

// 0x004949B0
void CModel::EnableShadow(uint8_t on)
{
    m_flags = SetFlag(m_flags, kModelShadow, on & 1);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomShadow(Geom(node), on);
}

} // namespace Nx

REPLACE(0x00492DC0, &Nx::CModel::VirtualTrue4);
REPLACE(0x00492EB0, &Nx::CModel::PlatGetBoundingSphere);
REPLACE(0x00492DD0, &Nx::CModel::SetSkeleton);
REPLACE(0x00492E10, &Nx::CModel::SetDisplayMode);
REPLACE(0x00492E40, &Nx::CModel::GetNumGeoms);
REPLACE_SOURCE_ONLY(0x00492E50, &Nx::CModel::GetBoneMatrices); // 4 bytes
REPLACE(0x00493460, &Nx::CModel::SetBoneMatrices);
REPLACE(0x00492E60, &Nx::CModel::EnableScaling);
REPLACE(0x00492E80, &Nx::CModel::SetBoundingSphere);
REPLACE(0x00492E90, &Nx::CModel::EnableShadowVolume);
REPLACE(0x00492EE0, &Nx::CModel::SetScale);
REPLACE(0x00492F30, &Nx::CModel::GetBoundingSphere);
REPLACE(0x00492F90, &Nx::CModel::SetBoneVolumes);
REPLACE(0x00492FC0, &Nx::CModel::GetBoneVolumes);
REPLACE(0x00492FF0, &Nx::CModel::GetAssetBoneCount);
REPLACE(0x00493040, &Nx::CModel::GetAssetHierarchy);
REPLACE(0x00493410, &Nx::CModel::CopyBones);
REPLACE(0x00493B30, &Nx::CModel::GetStripBreakFlags);
REPLACE(0x00493B90, &Nx::CModel::BreakStrips);
REPLACE(0x00494B30, &Nx::CModel::BreakOwnStrips);
REPLACE(0x00494700, &Nx::CModel::CleanInstanceStrips);
REPLACE(0x00493BD0, &Nx::CModel::CreateLights);
REPLACE(0x00493C10, &Nx::CModel::DestroyLights);
REPLACE(0x00493C60, &Nx::CModel::SetActive);
REPLACE(0x00494B70, &Nx::CModel::Hide);
REPLACE(0x00493CD0, &Nx::CModel::GetGeomByName);
REPLACE(0x00494640, &Nx::CModel::GetGeom);
REPLACE(0x00494680, &Nx::CModel::GetGeomName);
REPLACE(0x004946C0, &Nx::CModel::GetGeomTextures);
REPLACE(0x00493FB0, &Nx::CModel::ClearGeomColor);
REPLACE(0x00494110, &Nx::CModel::SetUVOffset);
REPLACE(0x00494160, &Nx::CModel::SetUVMatrix);
REPLACE(0x004941B0, &Nx::CModel::AllocateUVMatrixParams);
REPLACE(0x00494800, &Nx::CModel::HideGeom);
REPLACE(0x00494880, &Nx::CModel::IsGeomHidden);
REPLACE(0x004948D0, &Nx::CModel::SetColor);
REPLACE(0x00494930, &Nx::CModel::GetVisibility);
REPLACE(0x00494970, &Nx::CModel::SetVisibility);
REPLACE(0x004949B0, &Nx::CModel::EnableShadow);
