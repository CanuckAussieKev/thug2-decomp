// The engine's model: its members on the geom list - counts, flags, bones, visibility, colours, lights and UVs. See
// model.h (loading, drawing and the rest: model_geoms.cpp).

#include <cstdint>

#include "game/gfx/model.h"

#include "game/ass/assman.h"
#include "game/gfx/engine_mesh.h"
#include "game/gfx/model_internal.h"
#include "game/gfx/pc_lights.h"

namespace Engine3D {

using namespace ModelGeoms;

namespace {

// A flag of m_flags set from a bit (the setters take a flag's low bit, as the original's shifts and masks).
uint8_t SetFlag(uint8_t flags, uint8_t flag, int bit) { return static_cast<uint8_t>(bit ? flags | flag : flags & ~flag); }

// The first geom's entry (as the original: through the head's next even when there's no head).
sModelGeom* FirstEntry(CRenderModel* model) { return Entry(First(model)); }

// An asset's scene data's +0xB8 / +0xB4 (the folded getters 0x00490AC0 / 0x00490AB0).
uint32_t SceneB8(void* scene) { return static_cast<CSubMesh*>(scene)->GetHierarchyCount(); }
uint32_t SceneB4(void* scene) { return reinterpret_cast<uint32_t>(static_cast<CSubMesh*>(scene)->FetchBoneTree()); }

// Lights made and deleted (the engine's 0x0048EA80 / 0x0048C080).
uint8_t* NewLights() { return reinterpret_cast<uint8_t*>(SpawnModelLights()); }

} // namespace

// 0x00492DC0
bool CRenderModel::VirtualTrue4(int, int, int, int) { return true; }

// 0x00492EB0
Maths::Vector* CRenderModel::PlatGetBoundingSphere(Maths::Vector* out)
{
    out->z = 0.0f;
    out->y = 36.0f;
    out->x = 0.0f;
    out->w = m_boneTotal > 0 ? 48.0f : 1200.0f;
    return out;
}

// 0x00492DD0
bool CRenderModel::ChooseRig(uint8_t* skeleton)
{
    if (!m_boneMatrices) {
        m_boneTotal = SkeletonBoneCount(skeleton);
        ModelVirtual<bool>(this, 1, SkeletonBoneCount(skeleton));
        m_boneMatrices = SkeletonBones(skeleton);
    }
    return true;
}

// 0x00492E10
bool CRenderModel::SetDisplayMode(int mode)
{
    if (mode == m_displayMode)
        return false;
    bool allowed = ModelVirtual<bool>(this, 3, mode);
    if (allowed)
        m_displayMode = mode;
    return allowed;
}

// 0x00492E40
int CRenderModel::FetchGeomTotal() { return m_geomTotal; }

// 0x00492E50
uint8_t* CRenderModel::GetBoneMatrices() { return m_boneMatrices; }

// 0x00493460
void CRenderModel::SetBoneMatrices(uint8_t* matrices)
{
    m_boneMatrices = matrices;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomBones(Geom(node), m_boneMatrices, m_boneTotal);
}

// 0x00492E60
void CRenderModel::AllowResizing(uint8_t on) { m_flags = SetFlag(m_flags, kModelScaled, on & 1); }

// 0x00492E80
void CRenderModel::ChangeBoundSphere(const Maths::Vector* sphere)
{
    m_flags &= ~kModelSphereValid;
    ModelVirtual<void>(this, 13, sphere);
}

// 0x00492E90
void CRenderModel::ActivateShadowVolume(uint8_t on) { m_flags = SetFlag(m_flags, kModelShadowVolume, on & 1); }

// 0x00492EE0
bool CRenderModel::ChangeScale(const Maths::Vector* scale)
{
    m_sizeFactor = *scale;
    // (compared as bit patterns)
    const auto* bits = reinterpret_cast<const uint32_t*>(scale);
    bool scaled = !(bits[0] == 0x3F800000 && bits[1] == 0x3F800000 && bits[2] == 0x3F800000);
    m_flags = SetFlag(m_flags, kModelScaled, scaled);
    return true;
}

// 0x00492F30
Maths::Vector* CRenderModel::FetchBoundSphere(Maths::Vector* out)
{
    if (!(m_flags & kModelSphereValid)) {
        Maths::Vector sphere;
        m_enclosingBall = *ModelVirtual<Maths::Vector*>(this, 12, &sphere);
        m_flags |= kModelSphereValid;
    }
    *out = m_enclosingBall;
    return out;
}

// 0x00492F90
void CRenderModel::SetBoneVolumes(uint8_t* volumes)
{
    if (!m_geomListSize)
        return;
    sModelGeom* first = FirstEntry(this);
    if (first && !first->boneVolumes)
        first->boneVolumes = volumes;
}

// 0x00492FC0
uint8_t* CRenderModel::GetBoneVolumes()
{
    if (!m_geomListSize)
        return nullptr;
    sModelGeom* first = FirstEntry(this);
    return first ? first->boneVolumes : nullptr;
}

// 0x00492FF0
uint32_t CRenderModel::GetAssetBoneCount()
{
    if (m_numAssetGeoms != 1)
        return 0;
    return SceneB8(AssetStore::AssetManager()->GetAssetData(FirstEntry(this)->assetName, 1));
}

// 0x00493040
uint32_t CRenderModel::GetAssetHierarchy()
{
    if (m_numAssetGeoms != 1)
        return 0;
    return SceneB4(AssetStore::AssetManager()->GetAssetData(FirstEntry(this)->assetName, 1));
}

// 0x00493410
bool CRenderModel::CopyBones(uint8_t* skeleton)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        CopyGeomBones(Geom(node), SkeletonBones(skeleton), SkeletonBoneCount(skeleton));
    return true;
}

// 0x00493B30
uint32_t CRenderModel::GetStripBreakFlags()
{
    uint32_t flags = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        sModelGeom* entry = Entry(node);
        if (entry->scene) {
            flags |= *reinterpret_cast<uint32_t*>(entry->scene + 0xB0);
        } else if (entry->assetName) {
            auto* scene = static_cast<uint8_t*>(AssetStore::AssetManager()->GetAssetData(entry->assetName, 1));
            if (scene)
                flags |= *reinterpret_cast<uint32_t*>(scene + 0xB0);
        }
    }
    return flags;
}

// 0x00493B90
void CRenderModel::BreakStrips(uint32_t mask)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        BreakGeomStrips(Geom(node), mask);
}

// 0x00494700
void CRenderModel::CleanInstanceStrips()
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        CleanGeomInstanceStrips(Geom(node));
}

// 0x00494B30
bool CRenderModel::BreakOwnStrips()
{
    uint32_t mask = GetStripBreakFlags();
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        BreakGeomStrips(Geom(node), mask);
    return true;
}

// 0x00493BD0
bool CRenderModel::CreateLights()
{
    mp_lights = NewLights();
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomLights(Geom(node), mp_lights);
    return true;
}

// 0x00493C10
bool CRenderModel::DestroyLights()
{
    DiscardScene(mp_lights);
    mp_lights = nullptr;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomLights(Geom(node), nullptr);
    return true;
}

// 0x00493C60
bool CRenderModel::SetActive(uint8_t active)
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
bool CRenderModel::Hide(uint8_t hide)
{
    m_flags = SetFlag(m_flags, kModelHidden, hide & 1);
    return SetActive(hide == 0);
}

// 0x00493CD0
void* CRenderModel::GetGeomByName(uint32_t name)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if (name == Entry(node)->name)
            return Geom(node);
    }
    return nullptr;
}

// 0x00494640
void* CRenderModel::FetchGeom(int index)
{
    int i = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++i) {
        if (index == i)
            return Geom(node);
    }
    return nullptr;
}

// 0x00494680
uint32_t CRenderModel::GetGeomName(int index)
{
    int i = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++i) {
        if (index == i)
            return Entry(node)->name;
    }
    return 0;
}

// 0x004946C0
uint8_t* CRenderModel::GetGeomTextures(int index)
{
    int i = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++i) {
        if (index == i)
            return Entry(node)->textures;
    }
    return nullptr;
}

// 0x00493FB0
bool CRenderModel::ClearGeomColor(uint32_t name)
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
bool CRenderModel::ChooseUvShift(uint32_t material, int pass, float u, float v)
{
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        any |= GeomCall<bool>(44, Geom(node), material, pass, u, v);
    return any;
}

// 0x00494160
bool CRenderModel::ChooseUvMatrix(uint32_t material, int pass, const Maths::Matrix* matrix)
{
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        any |= GeomCall<bool>(46, Geom(node), material, pass, matrix);
    return any;
}

// 0x004941B0
bool CRenderModel::CreateUvAnimParams(uint32_t material, int pass)
{
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        any |= GeomCall<bool>(47, Geom(node), material, pass);
    return any;
}

// 0x00494800
bool CRenderModel::ConcealMesh(uint32_t name, bool hide)
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
bool CRenderModel::IsGeomHidden(uint32_t name)
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
bool CRenderModel::SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t rgba = r | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
                    (static_cast<uint32_t>(a) << 24);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomColor(Geom(node), rgba);
    return true;
}

// 0x00494930
uint32_t CRenderModel::GetVisibility()
{
    uint32_t mask = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        mask |= GetGeomVisibility(Geom(node));
    return mask;
}

// 0x00494970
bool CRenderModel::ToggleShown(uint32_t mask)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomVisibility(Geom(node), mask);
    return true;
}

// 0x004949B0
void CRenderModel::ActivateShadow(uint8_t on)
{
    m_flags = SetFlag(m_flags, kModelShadow, on & 1);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
        SetGeomShadow(Geom(node), on);
}

} // namespace Nx
