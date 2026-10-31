// The engine's model: made and destroyed, its geoms added (from a model file, the asset manager or as they are) and
// removed, drawn, tested against bone volumes, their textures replaced and their colours set from a script. See
// model.h (the simpler members: model.cpp).

#include <bit>
#include <cstdint>
#include <cstring>

#include "game/assets/resource_entry.h"
#include "game/assets/resource_registry.h"
#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/gfx/bone_volumes.h"
#include "game/gfx/debug_lines.h"
#include "game/gfx/engine.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/gfx/model_internal.h"
#include "game/gfx/particle_system.h"
#include "game/gfx/particle_sets.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/rig.h"
#include "game/gfx/texture_dict.h"
#include "game/net/buddy_status.h"
#include "game/obj/pager_message.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/net/net_manager.h"
#include "game/core/stubs.h"
#include "game/save_cards/memcard.h"

namespace Engine3D {

using namespace ModelGeoms;

namespace {

extern void** const kModelVtable;

// Checksums: "none" (a new model's file name), the names a geom gets when it's the first, the debug switch read
// when no texture was replaced, the script parameters.
constexpr uint32_t kNone = 0x6607DD3E;
constexpr uint32_t kFirstAssetGeom = 0x7DE76579;
constexpr uint32_t kFirstAddedGeom = 0x9FACC1A5;
constexpr uint32_t kTextureReplaceDebug = 0x2A648514;
constexpr uint32_t kName = 0xB6F08F39;
constexpr uint32_t kMaterials = 0x64E8E94A;
constexpr uint32_t kMaterial = 0x83418A6A;
constexpr uint32_t kPass = 0x318F2BDB;
constexpr uint32_t kSkinColor = 0xA435752C; // the profile's skin colour structure
constexpr uint32_t kFaceImage = 0xB00B0DC0; // the face texture's name in the dictionary

// A geom list node (std::list<sModelGeom*>, the game's): one inserted before `where` (0x004964A0), a range erased
// (0x004ECC30; the result iterator out).
void InsertGeom(CRenderModel* model, sModelGeomNode* where, sModelGeom** entry)
{
    reinterpret_cast<GameObjects::PointerList*>(&model->m_geoms)
        ->Splice(reinterpret_cast<GameObjects::PointerListNode*>(where), reinterpret_cast<void* const*>(entry));
}
void EraseGeoms(CRenderModel* model, sModelGeomNode* first, sModelGeomNode* last)
{
    sModelGeomNode* result;
    reinterpret_cast<GameObjects::PointerList*>(&model->m_geoms)
        ->Erase(reinterpret_cast<GameObjects::PointerListNode**>(&result), reinterpret_cast<GameObjects::PointerListNode*>(first),
                reinterpret_cast<GameObjects::PointerListNode*>(last));
}

// A node's memory: from the default heap, or the current one (0x00658555: whether to assert when out of it).
sModelGeom* NewEntry(bool defaultHeap)
{
    if (defaultHeap)
        return static_cast<sModelGeom*>(MemSys::Alloc(sizeof(sModelGeom)));
    return static_cast<sModelGeom*>(
        MemSys::Hub::Pull()->Carve(sizeof(sModelGeom), g_allocAssertFlag, nullptr));
}

// A scene's texture dictionary (its +4).
uint8_t* TexturesOf(uint8_t* scene) { return *reinterpret_cast<uint8_t**>(scene + 4); }

// A geom's own scene deleted with its textures.
void DeleteOwnScene(uint8_t* scene)
{
    uint8_t* textures = TexturesOf(scene);
    DiscardScene(scene, 1);
    UnloadTextures(textures);
}

// The engine (not rewritten yet): a model file's textures (0x0049F180) and scene (0x0048C3D0) loaded; bone volumes
// let go (0x0048B400); a line tested against bone volumes (0x0048B580) and a bone's volume (0x0048BC40); a skeleton
// drawn (0x004A31E0).
void* LoadTextures(const char* fileName, int texDictOffset, bool forceTexDict)
{
    return LoadModelTextures(fileName, texDictOffset, forceTexDict);
}
uint8_t* LoadScene(const char* fileName, void* textures, int texDictOffset, bool shadowVolume)
{
    return static_cast<uint8_t*>(Engine::LoadModelFile(fileName, textures, texDictOffset, shadowVolume));
}
void ReleaseBoneVolumes(void* volumes) { Gfx2D::ReleaseBoneVolumes(reinterpret_cast<uint32_t>(volumes)); }

// The texture dictionary (0x00700F1C): a texture by checksum (0x0049F540), one taken out (0x0049F500), a texture
// made from an image (0x0049FA20) or loaded (0x0049F9B0); a texture's own replaced in a dictionary by name (0x0049FB20)
// or by a texture (0x0049F800).
void* Textures() { return reinterpret_cast<void*>(g_gameTextureDict); }
void* FindTexture(uint32_t checksum)
{
    return static_cast<CTextureSet*>(Textures())->FetchSkin(checksum);
}
void DetachTexture(void* texture)
{
    static_cast<CTextureSet*>(Textures())->UnloadTexture(static_cast<CBitmapTexture*>(texture));
}

// The texture's checksum (+4, the folded getter 0x005480C0).
uint32_t TextureHash(void* texture) { return static_cast<OnlinePlay::FoldedGetters*>(texture)->Word04(); }

// A colour channel (0-1) as a byte: x * 255 + 0.5, truncated (the original's x87 and _ftol).
uint32_t ColorByte(float value)
{
    return static_cast<uint8_t>(static_cast<int64_t>(static_cast<double>(value) * 255.0 + 0.5));
}
uint32_t RgbaFromHsv(float h, float s, float v)
{
    float r, g, b;
    HsvToRgb(&r, &g, &b, h, s, v);
    return ColorByte(r) | (ColorByte(g) << 8) | (ColorByte(b) << 16) | 0xFF000000u;
}

// Strings copied and appended (no length limit, as the original's).
void CopyString(char* into, const char* from) { std::memcpy(into, from, std::strlen(from) + 1); }
void AppendString(char* into, const char* from) { CopyString(into + std::strlen(into), from); }

// A texture name made lower case with ".png" on the end (unless it has it).
void PngName(char* into, const char* name)
{
    CopyString(into, name);
    TextUtil::ToLowerText(into);
    if (!std::strstr(into, ".png"))
        AppendString(into, ".png");
}

} // namespace

// 0x00494C40
CRenderModel* CRenderModel::MakeNew()
{
    vtable = reinterpret_cast<void**>(kModelVtable);
    m_geomListSize = 0;
    // The list's head: a node linked to itself (none when out of memory).
    auto* head = static_cast<sModelGeomNode*>(MemSys::Alloc(sizeof(sModelGeomNode)));
    if (head)
        head->next = head->prev = head;
    m_geoms = head;
    m_boneTotal = 0;
    m_boneMatrices = nullptr;
    if (m_displayMode != 6 && ModelVirtual<bool>(this, 3, 6))
        m_displayMode = 6;
    m_geomTotal = 0;
    m_sourcePath = kNone;
    m_flagBits = static_cast<uint8_t>((m_flagBits & ~kModelHidden) | kModelEnabledBit);
    m_sizeFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    m_flagBits &= ~(kModelSphereValid | kModelScaled | kModelShadow | kModelShadowVolume);
    m_lightsPtr = nullptr;
    return this;
}

// 0x00494D00
void CRenderModel::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kModelVtable);
    if (m_lightsPtr)
        DiscardScene(m_lightsPtr);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if (uint8_t* volumes = TableRow(node)->boneVolumes)
            ReleaseBoneVolumes(*reinterpret_cast<void**>(volumes + 4));
    }
    DropGeoms();
    EraseGeoms(this, First(this), m_geoms);
    MemSys::Hub::Pull()->Dealloc(m_geoms);
}

// 0x00494DC0
CRenderModel* CRenderModel::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00494BA0
void CRenderModel::DropGeoms()
{
    sModelGeomNode* head = m_geoms;
    for (sModelGeomNode* node = First(this); node != head; node = node->next) {
        sModelGeom* entry = TableRow(node);
        DeleteGeomObject(entry->geom);
        if (entry->scene)
            DeleteOwnScene(entry->scene);
        MemSys::Hub::Pull()->Dealloc(entry);
    }
    EraseGeoms(this, First(this), m_geoms);
    m_geomTotal = 0;
    m_numFileGeoms = 0;
    m_numAssetGeoms = 0;
}

// 0x00494730
void CRenderModel::DetachGeom(uint32_t name)
{
    sModelGeomNode* node = First(this);
    while (node != m_geoms && TableRow(node)->name != name)
        node = node->next;
    if (node == m_geoms)
        return;
    DebugOut::Printf("Uninitializing geometry named %s\n", Scripts::FindChecksumNameOrUnknown(name));
    sModelGeom* entry = TableRow(node);
    DeleteGeomObject(entry->geom);
    if (entry->scene)
        DeleteOwnScene(entry->scene);
    if (entry->scene)
        --m_numFileGeoms;
    else
        --m_numAssetGeoms;
    --m_geomTotal;
    MemSys::Hub::Pull()->Dealloc(entry);
    if (node != m_geoms) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        MemSys::Hub::Pull()->Dealloc(node);
        --m_geomListSize;
    }
}

// 0x004934A0
bool CRenderModel::AddAssetGeom(uint32_t assetName, uint32_t name, bool multicolor, bool defaultHeap)
{
    m_flagBits &= ~kModelSphereValid;
    auto* scene = static_cast<uint8_t*>(AssetStore::AssetManager()->GetAssetData(assetName, 0));
    sModelGeom* entry = NewEntry(defaultHeap);
    if (!defaultHeap && !entry)
        return false;
    entry->assetName = assetName;
    ++m_numAssetGeoms;
    entry->scene = nullptr;
    entry->geom = CreateGeom();
    if (!defaultHeap && !entry->geom)
        return false; // (the node isn't freed)
    LoadGeom(entry->geom, scene, this, multicolor);
    entry->textures = TexturesOf(scene);
    SetGeomShadow(entry->geom, (m_flagBits >> 4) & 1);
    entry->name = name;
    m_visibleGeoms |= GeomBit(m_geomTotal);
    if (m_lightsPtr)
        SetGeomLights(entry->geom, m_lightsPtr);
    ++m_geomTotal;
    if (m_displayMode != 0 && ModelVirtual<bool>(this, 3, 0))
        m_displayMode = 0;
    entry->boneVolumes = nullptr;
    InsertGeom(this, m_geoms, &entry);
    if (m_geomTotal == 1)
        m_sourcePath = kFirstAssetGeom;
    return true;
}

// 0x004935E0
bool CRenderModel::AddFileGeom(const char* fileName, uint32_t name, bool viaResourceStore, int texDictOffset,
                         bool forceTexDict, bool multicolor, bool defaultHeap, bool byName)
{
    m_flagBits &= ~kModelSphereValid;
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    sModelGeom* entry = NewEntry(defaultHeap);
    if (!defaultHeap && !entry)
        return false;
    uint8_t* scene;
    if (viaResourceStore) {
        // The textures first ("<file without its .skin / .mdl>.tex"), then the file itself: the texture
        // dictionary's parameters (only these fields set - the rest as the stack left it in the original).
        struct TexDictParams {
            uint8_t flag0;
            uint8_t pad01[3];
            int texDictOffset;
            uint8_t hasSkinning;
            uint8_t forceTexDict;
            uint8_t rest[0x42];
        } texParams;
        std::memset(&texParams, 0, sizeof(texParams));
        char texName[0xF0];
        texParams.flag0 = 0;
        texParams.texDictOffset = texDictOffset;
        texParams.forceTexDict = forceTexDict;
        texParams.hasSkinning = Engine::SplitSkinName(fileName, texName);
        AppendString(texName, ".tex");
        if (byName)
            assets->GetAssetDataByName(texName, 1);
        else
            assets->FetchOrLoadResource(texName, 0, 0, 0, false, false, assets->GetPermanentByDefault(), &texParams, 0);
        AssetStore::SkinParams skinParams;
        skinParams.Clear();
        skinParams.flag0 = (m_flagBits >> 5) & 1;
        skinParams.value = texDictOffset;
        if (byName)
            scene = static_cast<uint8_t*>(assets->GetAssetDataByName(fileName, 1));
        else
            scene = static_cast<uint8_t*>(assets->FetchOrLoadResource(fileName, 0, 0, Checksums::HashFromText(texName),
                                                                 false, false, assets->GetPermanentByDefault(),
                                                                 &skinParams, 0));
        entry->assetName = Checksums::HashFromText(fileName);
        ++m_numAssetGeoms;
        entry->scene = nullptr;
    } else {
        MemSys::PushMemProfile(fileName);
        void* textures = LoadTextures(fileName, texDictOffset, forceTexDict);
        scene = LoadScene(fileName, textures, texDictOffset, (m_flagBits >> 5) & 1);
        MemSys::PopMemProfile();
        entry->scene = scene;
        ++m_numFileGeoms;
        entry->assetName = 0;
    }
    entry->geom = CreateGeom();
    if (!defaultHeap && !entry->geom)
        return false; // (the node isn't freed)
    LoadGeom(entry->geom, scene, this, multicolor);
    entry->textures = TexturesOf(scene);
    SetGeomShadow(entry->geom, (m_flagBits >> 4) & 1);
    entry->name = name;
    m_visibleGeoms |= GeomBit(m_geomTotal);
    if (m_lightsPtr)
        SetGeomLights(entry->geom, m_lightsPtr);
    ++m_geomTotal;
    if (m_displayMode != 0 && ModelVirtual<bool>(this, 3, 0))
        m_displayMode = 0;
    entry->boneVolumes = nullptr;
    InsertGeom(this, m_geoms, &entry);
    if (m_geomTotal == 1)
        m_sourcePath = Checksums::HashFromText(fileName);
    return true;
}

// 0x004938E0
bool CRenderModel::AddGeom(void* geom, uint32_t name, bool defaultHeap)
{
    m_flagBits &= ~kModelSphereValid;
    sModelGeom* entry = NewEntry(defaultHeap);
    if (!defaultHeap && !entry)
        return false;
    entry->geom = geom;
    SetGeomShadow(geom, (m_flagBits >> 4) & 1);
    entry->name = name;
    m_visibleGeoms |= GeomBit(m_geomTotal);
    entry->textures = nullptr;
    if (m_lightsPtr)
        SetGeomLights(entry->geom, m_lightsPtr);
    entry->boneVolumes = nullptr;
    entry->scene = nullptr;
    entry->assetName = 0;
    InsertGeom(this, m_geoms, &entry);
    ++m_geomTotal;
    if (m_displayMode != 0 && ModelVirtual<bool>(this, 3, 0))
        m_displayMode = 0;
    if (m_geomTotal == 1)
        m_sourcePath = kFirstAddedGeom;
    return true;
}

// 0x00493090
bool CRenderModel::Render(Maths::Mat4* matrix, bool noBones, uint8_t* skeleton)
{
    bool normal = m_displayMode == 0;
    int index = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++index) {
        bool active = normal && (m_flagBits & kModelEnabledBit) && (m_visibleGeoms & GeomBit(index));
        SetGeomActive(Geom(node), active);
    }
    if (m_flagBits & kModelScaled) {
        for (int row = 0; row < 3; ++row) {
            float factor = (&m_sizeFactor.x)[row];
            for (int i = 0; i < 4; ++i)
                (*matrix)[row][i] *= factor;
        }
    }
    switch (m_displayMode) {
    case 0:
        (*matrix)[0][3] = 0.0f;
        (*matrix)[1][3] = 0.0f;
        (*matrix)[2][3] = 0.0f;
        (*matrix)[3][3] = 1.0f;
        if (!skeleton || noBones) {
            for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next)
                SetGeomBoneMatrices(Geom(node), matrix, nullptr, 0, 0, 0);
        } else {
            uint8_t* bones = SkeletonBones(skeleton);
            int count = SkeletonBoneCount(skeleton);
            int flag = ModelVirtual<int>(this, 2, count);
            int geom = 0;
            for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++geom)
                SetGeomBoneMatrices(Geom(node), matrix, bones, count, flag, geom);
        }
        break;
    case 1:
        if (skeleton)
            // (the original passes the floats 0.5, 0.5 and 1 as its colours and frame count: their bits)
            reinterpret_cast<Gfx2D::CRig*>(skeleton)->Blit(matrix, std::bit_cast<uint32_t>(0.5f),
                                                              std::bit_cast<uint32_t>(0.5f),
                                                              std::bit_cast<int>(1.0f));
        break;
    case 2:
    case 3:
    case 4:
    case 6:
        break;
    default: {
        // A box about the position (compiled out).
        Maths::Mat4 orientation = *matrix;
        Maths::Vec4 position = (*matrix)[3];
        orientation[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        Maths::Vec4 box[2] = {{10.0f, 10.0f, 10.0f, 1.0f}, {-10.0f, -10.0f, -10.0f, 1.0f}};
        Gfx2D::DrawDebugCube(&orientation, &position, box, 0, 1, 0, 0xFF0000C8);
        break;
    }
    }
    return true;
}

// 0x004939E0
int CRenderModel::TestBoneVolumes(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t* out)
{
    int hit = 0;
    uint32_t hitExtra = 0;
    float nearest = 0.01f;
    uint32_t extra = e; // (the original's argument slot: each test's extra result lands there, kept between geoms)
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        uint8_t* volumes = TableRow(node)->boneVolumes;
        if (!volumes)
            continue;
        float distance = nearest;
        int result = reinterpret_cast<Gfx2D::CBoneVolumes*>(volumes)->SegmentHitsBone(
            reinterpret_cast<const Maths::Seg*>(a), reinterpret_cast<Gfx2D::CRig*>(b),
            reinterpret_cast<const Maths::Mat4*>(c), d, std::bit_cast<float>(e), std::bit_cast<float>(f), &distance,
            reinterpret_cast<float*>(&extra));
        if (!hit || distance < nearest) {
            nearest = distance;
            hitExtra = extra;
            hit = result;
        }
    }
    if (out)
        *out = hitExtra;
    return hit;
}

// 0x00493AB0
float CRenderModel::GetBoneVolume(uint32_t a, uint32_t b, uint32_t bone)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        uint8_t* volumes = TableRow(node)->boneVolumes;
        if (!volumes)
            continue;
        float value = 0.0f;
        if (reinterpret_cast<Gfx2D::CBoneVolumes*>(volumes)->GetBoneVolume(
                reinterpret_cast<Gfx2D::CRig*>(a), reinterpret_cast<const Maths::Mat4*>(b), bone, &value))
            return value;
    }
    return 0.0f;
}

// 0x00493D10
bool CRenderModel::SwapTexture(uint32_t name, const char* source, const char* dest)
{
    bool all = name == kAllGeoms;
    bool replaced = false;
    ModelVirtual<bool>(this, 10);
    char from[0x200];
    PngName(from, source);
    char to[0x200];
    CopyString(to, dest);
    TextUtil::ToLowerText(to);
    if (char* extension = std::strstr(to, ".png"))
        *extension = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        sModelGeom* entry = TableRow(node);
        if ((name == entry->name || all || entry->name == 0) && entry->textures &&
            reinterpret_cast<CTextureSet*>(entry->textures)->ReplaceTextureFromFile(from, to))
            replaced = true;
    }
    if (!replaced)
        Scripts::FetchIntValue(kTextureReplaceDebug);
    ModelVirtual<bool>(this, 11);
    return replaced;
}

// 0x00493E80
bool CRenderModel::ReplaceTextureWith(uint32_t name, const char* source, uint32_t dest)
{
    bool all = name == kAllGeoms;
    bool replaced = false;
    ModelVirtual<bool>(this, 10);
    char from[0x200];
    PngName(from, source);
    void* texture = FindTexture(dest);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        sModelGeom* entry = TableRow(node);
        if ((name == entry->name || all) && entry->textures &&
            reinterpret_cast<CTextureSet*>(entry->textures)->SwapTexture(from, static_cast<CBitmapTexture*>(texture)))
            replaced = true;
    }
    if (!replaced)
        Scripts::FetchIntValue(kTextureReplaceDebug);
    ModelVirtual<bool>(this, 11);
    return replaced;
}

// 0x00494010
bool CRenderModel::SetGeomColorHsv(uint32_t name, float h, float s, float v)
{
    bool all = name == kAllGeoms;
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if ((name == TableRow(node)->name || all) && !IsGeomMulticolor(Geom(node))) {
            SetGeomColor(Geom(node), RgbaFromHsv(h, s, v));
            any = true;
        }
    }
    return any;
}

// 0x00494200
bool CRenderModel::SetColorFromScript(Scripts::CStruct* params, float h, float s, float v)
{
    bool any = false;
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    uint32_t rgba = RgbaFromHsv(h, s, v);
    bool all = name == kAllGeoms;
    uint32_t material = 0; // (uninitialised in the original; kept from one geom and pass to the next)
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        void* geom = Geom(node);
        int pass = 0;
        if (!IsGeomMulticolor(geom)) {
            if (name == TableRow(node)->name || all) {
                SetGeomColor(geom, rgba);
                any = true;
            }
            continue;
        }
        // A multicolour geom: its material passes.
        Scripts::CArray* materials;
        if (params->FetchList(kMaterials, &materials, 0)) {
            for (uint32_t i = 0; i < materials->m_extent; ++i) {
                auto* entry = reinterpret_cast<Scripts::CStruct*>(materials->FetchPart(i));
                entry->ReadHashValue(0u, &material, 1);
                entry->FetchIntValue(0u, &pass, 1);
                any |= SetGeomMaterialColor(geom, material, pass, rgba);
            }
        } else if (params->ReadHashValue(kMaterial, &material, 0)) {
            Scripts::CArray* passes;
            if (params->FetchList(kPass, &passes, 0)) {
                for (uint32_t i = 0; i < passes->m_extent; ++i)
                    any |= SetGeomMaterialColor(geom, material, static_cast<int>(passes->FetchPart(i)), rgba);
            } else if (params->FetchIntValue(kPass, &pass, 1)) {
                any |= SetGeomMaterialColor(geom, material, pass, rgba);
            }
        }
    }
    return any;
}

// 0x00494450
bool CRenderModel::ClearColorFromScript(Scripts::CStruct* params)
{
    constexpr uint32_t kGray = 0x80808080;
    bool any = false;
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    bool all = name == kAllGeoms;
    uint32_t material = 0; // (as SetColorFromScript)
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        void* geom = Geom(node);
        int pass = 0;
        if (!IsGeomMulticolor(geom)) {
            if (name == TableRow(node)->name || all) {
                ModelGeoms::ClearGeomColor(geom);
                any = true;
            }
            continue;
        }
        Scripts::CArray* materials;
        if (params->FetchList(kMaterials, &materials, 0)) {
            for (uint32_t i = 0; i < materials->m_extent; ++i) {
                auto* entry = reinterpret_cast<Scripts::CStruct*>(materials->FetchPart(i));
                entry->ReadHashValue(0u, &material, 1);
                entry->FetchIntValue(0u, &pass, 1);
                any |= SetGeomMaterialColor(geom, material, pass, kGray);
            }
        } else if (params->ReadHashValue(kMaterial, &material, 0)) {
            Scripts::CArray* passes;
            if (params->FetchList(kPass, &passes, 0)) {
                for (uint32_t i = 0; i < passes->m_extent; ++i)
                    any |= SetGeomMaterialColor(geom, material, static_cast<int>(passes->FetchPart(i)), kGray);
            } else if (params->FetchIntValue(kPass, &pass, 1)) {
                any |= SetGeomMaterialColor(geom, material, pass, kGray);
            }
        }
    }
    return any;
}

// 0x00494A00
bool CRenderModel::ReplaceFaceTexture(void* face, const char* source, uint32_t name)
{
    // The face texture: its image's size (0x00487540) and data (0x00487530), its overlay's file name (0x004875B0),
    // its colour parameters (0x00487550, into a buffer).
    auto* faceTexture = static_cast<Gfx2D::CFaceSkin*>(face);
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    int size = faceTexture->GetImageSize();
    void* data = faceTexture->GetImage();
    void* image = static_cast<CTextureSet*>(Textures())
                      ->ReadTextureBuffer(static_cast<const uint8_t*>(data), size, kFaceImage, 1, 1, false, 1);
    void* overlay = static_cast<CTextureSet*>(Textures())->IngestSkin(faceTexture->GetOverlayName(), 1, 1, true);
    MemSys::Hub::Pull()->LeaveHeapScope();
    // The profile's skin colour (0x004869D0 reads it, 0x004869A0 makes it current) and the overlay (0x004869C0)
    // applied to the image (0x00486F70) with the face's colour parameters.
    uint8_t color[0x30];
    Gfx2D::ReadFacePoints(reinterpret_cast<Gfx2D::SFaceAnchors*>(color), Scripts::ReadSubStruct(kSkinColor));
    Gfx2D::ChangeFaceAnchors(reinterpret_cast<const Gfx2D::SFaceAnchors*>(color));
    Gfx2D::SetFaceOverlay(static_cast<CBitmapTexture*>(overlay));
    uint8_t parameters[0x38];
    auto getParameters = [faceTexture](uint8_t* out) {
        return reinterpret_cast<uint8_t*>(faceTexture->GetPoints(reinterpret_cast<Gfx2D::SFaceAnchors*>(out)));
    };
    uint8_t generatePalette = getParameters(parameters)[0x20]; // (the byte as it is - not made 0 / 1)
    Gfx2D::MassageFaceTexture(static_cast<CBitmapTexture*>(image),
                            reinterpret_cast<const Gfx2D::SFaceAnchors*>(getParameters(parameters)), generatePalette,
                            false, 0x80808080);
    bool replaced = ReplaceTextureWith(name, source, TextureHash(image));
    DetachTexture(image);
    DetachTexture(overlay);
    return replaced;
}

namespace {
static VtableSlot kModelVtable_slots[] = {&Engine3D::CRenderModel::Teardown, &MemCard::SaveCard::AlwaysTrue, Stubs::ReturnZero1,
                &MemCard::SaveCard::AlwaysTrue, &Engine3D::CRenderModel::VirtualTrue4, &MemCard::SaveCard::AlwaysTrue, &MemCard::SaveCard::AlwaysTrue,
                &MemCard::SaveCard::AlwaysTrue, Stubs::ReturnTrue2, &NetLink::Hub::LoadNetworkDrivers, Stubs::ReturnTrue,
                Stubs::ReturnTrue, &Engine3D::CRenderModel::PlatGetBoundingSphere, Stubs::IgnoreArgument};
void** const kModelVtable = reinterpret_cast<void**>(kModelVtable_slots);
} // namespace

} // namespace Nx
