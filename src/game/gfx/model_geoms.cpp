// The engine's model: made and destroyed, its geoms added (from a model file, the asset manager or as they are) and
// removed, drawn, tested against bone volumes, their textures replaced and their colours set from a script. See
// model.h (the simpler members: model.cpp).

#include <cstring>

#include "game/ass/asset.h"
#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/gfx/debug_lines.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/gfx/model_internal.h"
#include "game/gfx/pc_texture.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

using namespace ModelGeoms;

namespace {

constexpr uint32_t kModelVtable = 0x00649348;

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
void InsertGeom(CModel* model, sModelGeomNode* where, sModelGeom** entry)
{
    GameFunction<void(__thiscall*)(void*, sModelGeomNode*, sModelGeom**)>(0x004964A0)(&model->m_geoms, where, entry);
}
void EraseGeoms(CModel* model, sModelGeomNode* first, sModelGeomNode* last)
{
    sModelGeomNode* result;
    GameFunction<sModelGeomNode**(__thiscall*)(void*, sModelGeomNode**, sModelGeomNode*, sModelGeomNode*)>(
        0x004ECC30)(&model->m_geoms, &result, first, last);
}

// A node's memory: from the default heap, or the current one (0x00658555: whether to assert when out of it).
sModelGeom* NewEntry(bool defaultHeap)
{
    if (defaultHeap)
        return static_cast<sModelGeom*>(Mem::New(sizeof(sModelGeom)));
    return static_cast<sModelGeom*>(
        Mem::Manager::Get()->Allocate(sizeof(sModelGeom), GameVariable<uint8_t>(0x00658555), nullptr));
}

// A scene's texture dictionary (its +4).
uint8_t* TexturesOf(uint8_t* scene) { return *reinterpret_cast<uint8_t**>(scene + 4); }

// A geom's own scene deleted with its textures.
void DeleteOwnScene(uint8_t* scene)
{
    uint8_t* textures = TexturesOf(scene);
    DeleteScene(scene, 1);
    UnloadTextures(textures);
}

// The engine (not rewritten yet): a model file's textures (0x0049F180) and scene (0x0048C3D0) loaded; bone volumes
// let go (0x0048B400); a line tested against bone volumes (0x0048B580) and a bone's volume (0x0048BC40); a skeleton
// drawn (0x004A31E0).
void* LoadTextures(const char* fileName, int texDictOffset, bool forceTexDict)
{
    return GameFunction<void*(__cdecl*)(const char*, int, bool)>(0x0049F180)(fileName, texDictOffset, forceTexDict);
}
uint8_t* LoadScene(const char* fileName, void* textures, int texDictOffset, bool shadowVolume)
{
    return GameFunction<uint8_t*(__cdecl*)(const char*, void*, int, bool)>(0x0048C3D0)(fileName, textures,
                                                                                         texDictOffset, shadowVolume);
}
void ReleaseBoneVolumes(void* volumes) { GameFunction<void(__cdecl*)(void*)>(0x0048B400)(volumes); }

// The texture dictionary (0x00700F1C): a texture by checksum (0x0049F540), one taken out (0x0049F500), a texture
// made from an image (0x0049FA20) or loaded (0x0049F9B0); a texture's own replaced in a dictionary by name (0x0049FB20)
// or by a texture (0x0049F800).
void* Textures() { return GameVariable<void*>(0x00700F1C); }
void* FindTexture(uint32_t checksum)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x0049F540)(Textures(), checksum);
}
void RemoveTexture(void* texture) { GameFunction<bool(__thiscall*)(void*, void*)>(0x0049F500)(Textures(), texture); }

// The texture's checksum (+4, the folded getter 0x005480C0).
uint32_t TextureChecksum(void* texture) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x005480C0)(texture); }

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
    Str::LowerCase(into);
    if (!std::strstr(into, ".png"))
        AppendString(into, ".png");
}

} // namespace

// 0x00494C40
CModel* CModel::Construct()
{
    vtable = reinterpret_cast<void**>(kModelVtable);
    m_geomListSize = 0;
    // The list's head: a node linked to itself (none when out of memory).
    auto* head = static_cast<sModelGeomNode*>(Mem::New(sizeof(sModelGeomNode)));
    if (head)
        head->next = head->prev = head;
    m_geoms = head;
    m_numBones = 0;
    m_boneMatrices = nullptr;
    if (m_displayMode != 6 && ModelVirtual<bool>(this, 3, 6))
        m_displayMode = 6;
    m_numGeoms = 0;
    m_fileName = kNone;
    m_flags = static_cast<uint8_t>((m_flags & ~kModelHidden) | kModelActive);
    m_scale = {1.0f, 1.0f, 1.0f, 1.0f};
    m_flags &= ~(kModelSphereValid | kModelScaled | kModelShadow | kModelShadowVolume);
    mp_lights = nullptr;
    return this;
}

// 0x00494D00
void CModel::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kModelVtable);
    if (mp_lights)
        DeleteScene(mp_lights);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if (uint8_t* volumes = Entry(node)->boneVolumes)
            ReleaseBoneVolumes(*reinterpret_cast<void**>(volumes + 4));
    }
    ClearGeoms();
    EraseGeoms(this, First(this), m_geoms);
    Mem::Manager::Get()->Free(m_geoms);
}

// 0x00494DC0
CModel* CModel::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00494BA0
void CModel::ClearGeoms()
{
    sModelGeomNode* head = m_geoms;
    for (sModelGeomNode* node = First(this); node != head; node = node->next) {
        sModelGeom* entry = Entry(node);
        DeleteGeomObject(entry->geom);
        if (entry->scene)
            DeleteOwnScene(entry->scene);
        Mem::Manager::Get()->Free(entry);
    }
    EraseGeoms(this, First(this), m_geoms);
    m_numGeoms = 0;
    m_numFileGeoms = 0;
    m_numAssetGeoms = 0;
}

// 0x00494730
void CModel::RemoveGeom(uint32_t name)
{
    sModelGeomNode* node = First(this);
    while (node != m_geoms && Entry(node)->name != name)
        node = node->next;
    if (node == m_geoms)
        return;
    Dbg::Printf("Uninitializing geometry named %s\n", Script::FindChecksumNameOrUnknown(name));
    sModelGeom* entry = Entry(node);
    DeleteGeomObject(entry->geom);
    if (entry->scene)
        DeleteOwnScene(entry->scene);
    if (entry->scene)
        --m_numFileGeoms;
    else
        --m_numAssetGeoms;
    --m_numGeoms;
    Mem::Manager::Get()->Free(entry);
    if (node != m_geoms) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        Mem::Manager::Get()->Free(node);
        --m_geomListSize;
    }
}

// 0x004934A0
bool CModel::AddAssetGeom(uint32_t assetName, uint32_t name, bool multicolor, bool defaultHeap)
{
    m_flags &= ~kModelSphereValid;
    auto* scene = static_cast<uint8_t*>(Ass::AssetManager()->GetAssetData(assetName, 0));
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
    SetGeomShadow(entry->geom, (m_flags >> 4) & 1);
    entry->name = name;
    m_visibleGeoms |= GeomBit(m_numGeoms);
    if (mp_lights)
        SetGeomLights(entry->geom, mp_lights);
    ++m_numGeoms;
    if (m_displayMode != 0 && ModelVirtual<bool>(this, 3, 0))
        m_displayMode = 0;
    entry->boneVolumes = nullptr;
    InsertGeom(this, m_geoms, &entry);
    if (m_numGeoms == 1)
        m_fileName = kFirstAssetGeom;
    return true;
}

// 0x004935E0
bool CModel::AddFileGeom(const char* fileName, uint32_t name, bool useAssetManager, int texDictOffset,
                         bool forceTexDict, bool multicolor, bool defaultHeap, bool byName)
{
    m_flags &= ~kModelSphereValid;
    Ass::CAssMan* assets = Ass::AssetManager();
    sModelGeom* entry = NewEntry(defaultHeap);
    if (!defaultHeap && !entry)
        return false;
    uint8_t* scene;
    if (useAssetManager) {
        // The textures first ("<file without its .skin / .mdl>.tex"), then the file itself: the texture
        // dictionary's parameters (only these fields set - the rest as the stack left it in the original).
        struct TexDictParams {
            uint8_t flag0;
            uint8_t pad01[3];
            int texDictOffset;
            uint8_t isSkin;
            uint8_t forceTexDict;
            uint8_t rest[0x42];
        } texParams;
        std::memset(&texParams, 0, sizeof(texParams));
        char texName[0xF0];
        texParams.flag0 = 0;
        texParams.texDictOffset = texDictOffset;
        texParams.forceTexDict = forceTexDict;
        texParams.isSkin = GameFunction<bool(__cdecl*)(const char*, char*)>(0x0048BFE0)(fileName, texName);
        AppendString(texName, ".tex");
        if (byName)
            assets->GetAssetDataByName(texName, 1);
        else
            assets->LoadOrGetAsset(texName, 0, 0, 0, false, false, assets->GetPermanentByDefault(), &texParams, 0);
        Ass::SkinParams skinParams;
        skinParams.Clear();
        skinParams.flag0 = (m_flags >> 5) & 1;
        skinParams.value = texDictOffset;
        if (byName)
            scene = static_cast<uint8_t*>(assets->GetAssetDataByName(fileName, 1));
        else
            scene = static_cast<uint8_t*>(assets->LoadOrGetAsset(fileName, 0, 0, Crc::GenerateCRCFromString(texName),
                                                                 false, false, assets->GetPermanentByDefault(),
                                                                 &skinParams, 0));
        entry->assetName = Crc::GenerateCRCFromString(fileName);
        ++m_numAssetGeoms;
        entry->scene = nullptr;
    } else {
        Mem::PushMemProfile(fileName);
        void* textures = LoadTextures(fileName, texDictOffset, forceTexDict);
        scene = LoadScene(fileName, textures, texDictOffset, (m_flags >> 5) & 1);
        Mem::PopMemProfile();
        entry->scene = scene;
        ++m_numFileGeoms;
        entry->assetName = 0;
    }
    entry->geom = CreateGeom();
    if (!defaultHeap && !entry->geom)
        return false; // (the node isn't freed)
    LoadGeom(entry->geom, scene, this, multicolor);
    entry->textures = TexturesOf(scene);
    SetGeomShadow(entry->geom, (m_flags >> 4) & 1);
    entry->name = name;
    m_visibleGeoms |= GeomBit(m_numGeoms);
    if (mp_lights)
        SetGeomLights(entry->geom, mp_lights);
    ++m_numGeoms;
    if (m_displayMode != 0 && ModelVirtual<bool>(this, 3, 0))
        m_displayMode = 0;
    entry->boneVolumes = nullptr;
    InsertGeom(this, m_geoms, &entry);
    if (m_numGeoms == 1)
        m_fileName = Crc::GenerateCRCFromString(fileName);
    return true;
}

// 0x004938E0
bool CModel::AddGeom(void* geom, uint32_t name, bool defaultHeap)
{
    m_flags &= ~kModelSphereValid;
    sModelGeom* entry = NewEntry(defaultHeap);
    if (!defaultHeap && !entry)
        return false;
    entry->geom = geom;
    SetGeomShadow(geom, (m_flags >> 4) & 1);
    entry->name = name;
    m_visibleGeoms |= GeomBit(m_numGeoms);
    entry->textures = nullptr;
    if (mp_lights)
        SetGeomLights(entry->geom, mp_lights);
    entry->boneVolumes = nullptr;
    entry->scene = nullptr;
    entry->assetName = 0;
    InsertGeom(this, m_geoms, &entry);
    ++m_numGeoms;
    if (m_displayMode != 0 && ModelVirtual<bool>(this, 3, 0))
        m_displayMode = 0;
    if (m_numGeoms == 1)
        m_fileName = kFirstAddedGeom;
    return true;
}

// 0x00493090
bool CModel::Render(Mth::Matrix* matrix, bool noBones, uint8_t* skeleton)
{
    bool normal = m_displayMode == 0;
    int index = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next, ++index) {
        bool active = normal && (m_flags & kModelActive) && (m_visibleGeoms & GeomBit(index));
        SetGeomActive(Geom(node), active);
    }
    if (m_flags & kModelScaled) {
        for (int row = 0; row < 3; ++row) {
            float factor = (&m_scale.x)[row];
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
            GameFunction<void(__thiscall*)(uint8_t*, Mth::Matrix*, float, float, float)>(0x004A31E0)(skeleton, matrix,
                                                                                                 0.5f, 0.5f, 1.0f);
        break;
    case 2:
    case 3:
    case 4:
    case 6:
        break;
    default: {
        // A box about the position (compiled out).
        Mth::Matrix orientation = *matrix;
        Mth::Vector position = (*matrix)[3];
        orientation[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        Mth::Vector box[2] = {{10.0f, 10.0f, 10.0f, 1.0f}, {-10.0f, -10.0f, -10.0f, 1.0f}};
        Gfx::AddDebugBox(&orientation, &position, box, 0, 1, 0, 0xFF0000C8);
        break;
    }
    }
    return true;
}

// 0x004939E0
int CModel::TestBoneVolumes(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t* out)
{
    int hit = 0;
    uint32_t hitExtra = 0;
    float nearest = 0.01f;
    uint32_t extra = e; // (the original's argument slot: each test's extra result lands there, kept between geoms)
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        uint8_t* volumes = Entry(node)->boneVolumes;
        if (!volumes)
            continue;
        float distance = nearest;
        int result = GameFunction<int(__thiscall*)(uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                   uint32_t, float*, uint32_t*)>(0x0048B580)(volumes, a, b, c, d, e,
                                                                                             f, &distance, &extra);
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
float CModel::GetBoneVolume(uint32_t a, uint32_t b, uint32_t bone)
{
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        uint8_t* volumes = Entry(node)->boneVolumes;
        if (!volumes)
            continue;
        float value = 0.0f;
        if (GameFunction<bool(__thiscall*)(uint8_t*, uint32_t, uint32_t, uint32_t, float*)>(0x0048BC40)(volumes, a, b,
                                                                                                    bone, &value))
            return value;
    }
    return 0.0f;
}

// 0x00493D10
bool CModel::ReplaceTexture(uint32_t name, const char* source, const char* dest)
{
    bool all = name == kAllGeoms;
    bool replaced = false;
    ModelVirtual<bool>(this, 10);
    char from[0x200];
    PngName(from, source);
    char to[0x200];
    CopyString(to, dest);
    Str::LowerCase(to);
    if (char* extension = std::strstr(to, ".png"))
        *extension = 0;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        sModelGeom* entry = Entry(node);
        if ((name == entry->name || all || entry->name == 0) && entry->textures &&
            GameFunction<bool(__thiscall*)(uint8_t*, const char*, const char*)>(0x0049FB20)(entry->textures, from, to))
            replaced = true;
    }
    if (!replaced)
        Script::GetInteger(kTextureReplaceDebug);
    ModelVirtual<bool>(this, 11);
    return replaced;
}

// 0x00493E80
bool CModel::ReplaceTextureWith(uint32_t name, const char* source, uint32_t dest)
{
    bool all = name == kAllGeoms;
    bool replaced = false;
    ModelVirtual<bool>(this, 10);
    char from[0x200];
    PngName(from, source);
    void* texture = FindTexture(dest);
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        sModelGeom* entry = Entry(node);
        if ((name == entry->name || all) && entry->textures &&
            GameFunction<bool(__thiscall*)(uint8_t*, const char*, void*)>(0x0049F800)(entry->textures, from, texture))
            replaced = true;
    }
    if (!replaced)
        Script::GetInteger(kTextureReplaceDebug);
    ModelVirtual<bool>(this, 11);
    return replaced;
}

// 0x00494010
bool CModel::SetGeomColorHsv(uint32_t name, float h, float s, float v)
{
    bool all = name == kAllGeoms;
    bool any = false;
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        if ((name == Entry(node)->name || all) && !IsGeomMulticolor(Geom(node))) {
            SetGeomColor(Geom(node), RgbaFromHsv(h, s, v));
            any = true;
        }
    }
    return any;
}

// 0x00494200
bool CModel::SetColorFromScript(Script::CStruct* params, float h, float s, float v)
{
    bool any = false;
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    uint32_t rgba = RgbaFromHsv(h, s, v);
    bool all = name == kAllGeoms;
    uint32_t material = 0; // (uninitialised in the original; kept from one geom and pass to the next)
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        void* geom = Geom(node);
        int pass = 0;
        if (!IsGeomMulticolor(geom)) {
            if (name == Entry(node)->name || all) {
                SetGeomColor(geom, rgba);
                any = true;
            }
            continue;
        }
        // A multicolour geom: its material passes.
        Script::CArray* materials;
        if (params->GetArray(kMaterials, &materials, 0)) {
            for (uint32_t i = 0; i < materials->m_size; ++i) {
                auto* entry = reinterpret_cast<Script::CStruct*>(materials->GetElement(i));
                entry->GetChecksum(0u, &material, 1);
                entry->GetInteger(0u, &pass, 1);
                any |= SetGeomMaterialColor(geom, material, pass, rgba);
            }
        } else if (params->GetChecksum(kMaterial, &material, 0)) {
            Script::CArray* passes;
            if (params->GetArray(kPass, &passes, 0)) {
                for (uint32_t i = 0; i < passes->m_size; ++i)
                    any |= SetGeomMaterialColor(geom, material, static_cast<int>(passes->GetElement(i)), rgba);
            } else if (params->GetInteger(kPass, &pass, 1)) {
                any |= SetGeomMaterialColor(geom, material, pass, rgba);
            }
        }
    }
    return any;
}

// 0x00494450
bool CModel::ClearColorFromScript(Script::CStruct* params)
{
    constexpr uint32_t kGray = 0x80808080;
    bool any = false;
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    bool all = name == kAllGeoms;
    uint32_t material = 0; // (as SetColorFromScript)
    for (sModelGeomNode* node = First(this); node != m_geoms; node = node->next) {
        void* geom = Geom(node);
        int pass = 0;
        if (!IsGeomMulticolor(geom)) {
            if (name == Entry(node)->name || all) {
                ModelGeoms::ClearGeomColor(geom);
                any = true;
            }
            continue;
        }
        Script::CArray* materials;
        if (params->GetArray(kMaterials, &materials, 0)) {
            for (uint32_t i = 0; i < materials->m_size; ++i) {
                auto* entry = reinterpret_cast<Script::CStruct*>(materials->GetElement(i));
                entry->GetChecksum(0u, &material, 1);
                entry->GetInteger(0u, &pass, 1);
                any |= SetGeomMaterialColor(geom, material, pass, kGray);
            }
        } else if (params->GetChecksum(kMaterial, &material, 0)) {
            Script::CArray* passes;
            if (params->GetArray(kPass, &passes, 0)) {
                for (uint32_t i = 0; i < passes->m_size; ++i)
                    any |= SetGeomMaterialColor(geom, material, static_cast<int>(passes->GetElement(i)), kGray);
            } else if (params->GetInteger(kPass, &pass, 1)) {
                any |= SetGeomMaterialColor(geom, material, pass, kGray);
            }
        }
    }
    return any;
}

// 0x00494A00
bool CModel::ReplaceFaceTexture(void* face, const char* source, uint32_t name)
{
    // The face texture (not rewritten yet): its image's size (0x00487540) and data (0x00487530), its overlay's file
    // name (0x004875B0), its colour parameters (0x00487550, into a buffer).
    auto faceCall = [face](uint32_t address) { return GameFunction<void*(__thiscall*)(void*)>(address)(face); };
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    int size = reinterpret_cast<int>(faceCall(0x00487540));
    void* data = faceCall(0x00487530);
    void* image = GameFunction<void*(__thiscall*)(void*, void*, int, uint32_t, bool, bool, bool, bool)>(0x0049FA20)(
        Textures(), data, size, kFaceImage, true, true, false, true);
    void* overlay = GameFunction<void*(__thiscall*)(void*, const char*, bool, bool, bool)>(0x0049F9B0)(
        Textures(), static_cast<const char*>(faceCall(0x004875B0)), true, true, true);
    Mem::Manager::Get()->PopContext();
    // The profile's skin colour (0x004869D0 reads it, 0x004869A0 makes it current) and the overlay (0x004869C0)
    // applied to the image (0x00486F70) with the face's colour parameters.
    uint8_t color[0x30];
    GameFunction<void(__cdecl*)(void*, Script::CStruct*)>(0x004869D0)(color, Script::GetStructure(kSkinColor));
    GameFunction<void(__cdecl*)(void*)>(0x004869A0)(color);
    GameFunction<void(__cdecl*)(void*)>(0x004869C0)(overlay);
    uint8_t parameters[0x38];
    auto getParameters = GameFunction<uint8_t*(__thiscall*)(void*, void*)>(0x00487550);
    uint8_t flag = getParameters(face, parameters)[0x20];
    GameFunction<bool(__cdecl*)(void*, uint8_t*, uint32_t, int, uint32_t)>(0x00486F70)(
        image, getParameters(face, parameters), flag, 0, 0x80808080);
    bool replaced = ReplaceTextureWith(name, source, TextureChecksum(image));
    RemoveTexture(image);
    RemoveTexture(overlay);
    return replaced;
}

} // namespace Nx

REPLACE(0x00494C40, &Nx::CModel::Construct);
REPLACE(0x00494D00, &Nx::CModel::DestroyBody);
REPLACE(0x00494DC0, &Nx::CModel::Destroy);
REPLACE(0x00494BA0, &Nx::CModel::ClearGeoms);
REPLACE(0x00494730, &Nx::CModel::RemoveGeom);
REPLACE(0x004934A0, &Nx::CModel::AddAssetGeom);
REPLACE(0x004935E0, &Nx::CModel::AddFileGeom);
REPLACE(0x004938E0, &Nx::CModel::AddGeom);
REPLACE(0x00493090, &Nx::CModel::Render);
REPLACE(0x004939E0, &Nx::CModel::TestBoneVolumes);
REPLACE(0x00493AB0, &Nx::CModel::GetBoneVolume);
REPLACE(0x00493D10, &Nx::CModel::ReplaceTexture);
REPLACE(0x00493E80, &Nx::CModel::ReplaceTextureWith);
REPLACE(0x00494010, &Nx::CModel::SetGeomColorHsv);
REPLACE(0x00494200, &Nx::CModel::SetColorFromScript);
REPLACE(0x00494450, &Nx::CModel::ClearColorFromScript);
REPLACE(0x00494A00, &Nx::CModel::ReplaceFaceTexture);
