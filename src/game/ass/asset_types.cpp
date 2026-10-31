// The asset types: animations, cutscenes, node arrays, scenes, scene collision, skeletons, skinned models, skin
// collision and texture dictionaries - how each is read and turned into the engine's data.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/ass/resource_entry.h"
#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/file/async.h"
#include "game/file/packs.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_mesh.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/rig.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/engine_glue.h"
#include "game/obj/cutscene.h"
#include "game/obj/entity.h"
#include "game/script/node_manager.h"
#include "game/script/qb.h"
#include "game/script/script_startup.h"
#include "game/script/param_struct.h"

namespace AssetStore {

uint32_t g_paramsInUse;

namespace {

using Scripts::CStruct;

// --- the engine's loaders ---------------------------------------------------------------------------------
// Gfx2D::CJointClipFrames (0x3C bytes): 0x00482AF0 the constructor, 0x00482B30 the destructor, 0x004856C0 Load.
void* ConstructAnimFrames(void* frames) { return static_cast<Gfx2D::CJointClipFrames*>(frames)->MakeNew(); }
void DestroyAnimFrames(void* frames) { static_cast<Gfx2D::CJointClipFrames*>(frames)->DestroyBody(); }
bool LoadAnimFrames(void* frames, void* buffer, uint32_t size)
{
    return static_cast<Gfx2D::CJointClipFrames*>(frames)->ReadIn(static_cast<const uint8_t*>(buffer), size, true);
}
// The skeleton data (0x598 bytes): 0x004A2C20 the constructor, 0x004A39C0 Load.
void* ConstructSkeletonData(void* skeleton) { return static_cast<Gfx2D::CRigData*>(skeleton)->MakeNew(); }
bool LoadSkeletonData(void* skeleton, void* buffer, uint32_t size)
{
    return static_cast<Gfx2D::CRigData*>(skeleton)->ReadIn(static_cast<const uint32_t*>(buffer), size, true);
}
// Scripts: 0x005A5990 parses a .qb file's contents, 0x005A59B0 unloads one (by its name's checksum).
void ParseQbFile(const char* name, void* buffer)
{
    Scripts::LoadQBFileData(name, static_cast<const uint8_t*>(buffer), 1, 1);
}
void UnloadQbFile(uint32_t name) { Scripts::UnloadQBFile(name); }
// Scenes (Nx): the file name, made from the buffer (with the textures), unloaded.
void SceneFileName(char* out, const char* name, uint8_t flag) { Engine3D::Engine::SceneFileName(out, name, flag); }
void* MakeScene(const char* name, void* buffer, void* textures, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return Engine3D::LoadNamedSceneFromMemory(name, static_cast<const uint8_t*>(buffer), static_cast<Engine3D::CTextureSet*>(textures),
                                        a != 0, b != 0, c != 0, d);
}
void UnloadScene(void* scene, int flag) { Engine3D::Engine::UnloadScene(static_cast<Engine3D::CWorldScene*>(scene), flag); }
// Scene collision: its file name, added to a scene, the scene's collision, removed.
void CollisionFileName(char* out, const char* name, uint8_t flag)
{
    (void)flag; // (not read)
    Engine3D::MakeCollisionFileName(out, name);
}
void AddCollision(void* scene, void* buffer, uint8_t flag)
{
    static_cast<Engine3D::CWorldScene*>(scene)->AddCollisionData(static_cast<const uint8_t*>(buffer), flag);
}
void* SceneCollision(void* scene) { return static_cast<Engine3D::CWorldScene*>(scene)->FetchStaticCollision(0); }
void RemoveCollision(void* scene) { static_cast<Engine3D::CWorldScene*>(scene)->UnloadCollision(); }
// Skinned models: whether a name is a model with a .cas file (its base name out), the model made, unloaded.
bool SplitSkinName(const char* name, char* base) { return Engine3D::Engine::SplitSkinName(name, base); }
void* MakeSkinnedModel(uint32_t checksum, void* buffer, uint32_t size, void* cas, void* textures, int value,
                       uint8_t hasCas, uint8_t flag)
{
    return Engine3D::Engine::LoadModelFromMemory(checksum, static_cast<const uint8_t*>(buffer), static_cast<int>(size), cas,
                                           textures, value, hasCas, flag);
}
void UnloadSkinnedModel(void* model, int flag)
{
    (void)flag; // 0x0048C080 is a jump to DeleteModelLights (its second argument isn't read)
    Engine3D::DeleteModelLights(static_cast<Engine3D::CPcModelLights*>(model));
}
// Skin collision: its file name, added to a model, removed.
void SkinCollisionFileName(char* out, const char* name) { Engine3D::SkinCollisionFileName(out, name); }
void AddSkinCollision(void* model, void* buffer)
{
    static_cast<Engine3D::CSubMesh*>(model)->AddCollision(static_cast<const uint8_t*>(buffer));
}
void RemoveSkinCollision(void* model) { static_cast<Engine3D::CSubMesh*>(model)->RemoveCollision(); }
// Texture dictionaries: the file name, made, unloaded.
void TexDictFileName(char* out, const char* name) { Engine3D::TexDictFileName(out, name); }
void* MakeTexDict(uint32_t checksum, void* buffer, uint32_t size, uint8_t a, int value, uint8_t b, uint8_t c)
{
    return Engine3D::LoadTexDictFromMemory(checksum, static_cast<const uint8_t*>(buffer), size, a, static_cast<uint32_t>(value),
                                     b, c);
}
void UnloadTexDict(void* textures) { Engine3D::UnloadTexDict(static_cast<Engine3D::CTextureSet*>(textures)); }

// --- shared steps ---------------------------------------------------------------------------------------------
uint8_t g_paramsPool[0x180]; // 4 blocks of 0x60 bytes
uint8_t* ParamsPool() { return g_paramsPool; }
uint32_t& ParamsInUse() { return g_paramsInUse; }

// strcpy (no bounds, as the original's).
void CopyString(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The file opened (null: false) and read into a new buffer; an async read calls back when it's done.
bool ReadFile(CResource* asset, const char* file, bool async, uint32_t* size)
{
    asset->m_fileRef = FileLoad::AsyncOpen(file, !async, 100);
    if (!asset->m_fileRef)
        return false;
    *size = static_cast<uint32_t>(asset->m_fileRef->GetFileSize());
    asset->m_octets = MemSys::AllocBlock(*size);
    return true;
}
void StartReading(CResource* asset, FileLoad::AsyncFileHandle* file, void* buffer, uint32_t size, bool async)
{
    if (async) {
        file->m_callbackMask = 0;
        file->m_callbackMask |= 1u << FileLoad::kAsyncRead;
        file->ChangeHook(&CResource::ReadCallback, asset, nullptr);
    }
    file->TakeIn(buffer, 1, static_cast<int>(size));
}
// An archive file through Pip: its lowercased name's checksum kept (`lowered` false: the name is already).
void ReadArchive(CResource* asset, uint32_t* archive, const char* file, uint32_t* size, bool lower)
{
    char lowered[256];
    const char* key = file;
    if (lower) {
        CopyString(lowered, file);
        TextUtil::ToLowerText(lowered);
        key = lowered;
    }
    *archive = Checksums::HashFromText(key);
    asset->m_octets = Packs::ReadIn(file, false, nullptr, nullptr, nullptr);
    *size = static_cast<uint32_t>(Packs::GetFileSize(file));
}
// A blocking load finishes at once.
void FinishNow(CResource* asset, uint32_t size)
{
    asset->m_extent = size;
    asset->VProcessBuffer();
}
// LoadFromBuffer's copy: the data copied into a buffer of the asset's own.
void* CopyData(CResource* asset, void* data, uint32_t size)
{
    asset->m_octets = MemSys::AllocBlock(size);
    std::memcpy(asset->m_octets, data, size);
    return asset->m_octets;
}
void CloseFile(FileLoad::AsyncFileHandle*& file)
{
    if (file) {
        FileLoad::AsyncClose(file);
        file = nullptr;
    }
}
void FreeMallocBuffer(void*& buffer)
{
    if (buffer) {
        MemSys::FreeMalloc(buffer);
        buffer = nullptr;
    }
}
void* GroupData(CResource* asset) { return asset->m_bundle->VData(); }
const char* PlatformName(const char* name, char* out)
{
    std::snprintf(out, 256, "%s.%s", name, Engine3D::PlatformExtension());
    return out;
}
void CopyWords(void* to, const void* from, int words) { std::memcpy(to, from, 4 * static_cast<size_t>(words)); }

} // namespace

// 0x00407730
void* AllocLoadParams(uint32_t /*size*/)
{
    for (int i = 0; i < 4; ++i) {
        if (!(ParamsInUse() & (1u << i))) {
            ParamsInUse() |= 1u << i;
            return ParamsPool() + 0x60 * i;
        }
    }
    return nullptr;
}

// 0x00407770
void FreeLoadParams(void* params)
{
    for (int i = 0; i < 4; ++i) {
        if (ParamsPool() + 0x60 * i == params) {
            ParamsInUse() &= ~(1u << i);
            return;
        }
    }
}

// 0x00408A70
bool RefreshResource(CResource* asset, const char* name)
{
    asset->VUnload();
    int result = reinterpret_cast<int(__thiscall*)(CResource*, const char*, int, int, int, int)>(asset->m_vtable[5])(
        asset, name, 0, 0, 0, 0);
    return result == 0;
}

// --- animation ------------------------------------------------------------------------------------------------

// 0x004072E0
int CAnimResource::FetchKind() const { return kAnimation; }
// 0x004072D0
const char* CAnimResource::FetchTitle() const { return "Animation"; }

// 0x004071C0
int CAnimResource::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    MemSys::PushMemProfile("Animation Sequence processing");
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    if (copy) {
        MemSys::PushMemProfile("anim from data stream");
        data = CopyData(this, data, size);
        MemSys::PopMemProfile();
    }
    VFinishLoading(data, size);
    MemSys::PopMemProfile();
    return 0;
}

// 0x00407300
int CAnimResource::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    Scripts::FetchIntValue(0x25DC7904u); // (a debug leftover: looked up and not used)
    MemSys::PushMemProfile(name);
    char file[256];
    PlatformName(name, file);
    uint32_t size;
    if (useArchive) {
        ReadArchive(this, &m_archive, file, &size, true);
    } else {
        if (!ReadFile(this, file, async, &size))
            return -1;
        StartReading(this, m_fileRef, m_octets, size, async);
    }
    if (!async)
        FinishNow(this, size); // (the original reads the checksum first and doesn't use it)
    MemSys::PopMemProfile();
    return 0;
}

// 0x004074F0
bool CAnimResource::FinishLoading(void* buffer, uint32_t size)
{
    void* memory = MemSys::Alloc(0x3C);
    void* frames = memory ? ConstructAnimFrames(memory) : nullptr;
    if (!LoadAnimFrames(frames, buffer, size)) { // (called on a failed allocation too, as in the original)
        Scripts::FetchIntValue(0x25DC7904u);
        if (frames) {
            DestroyAnimFrames(frames);
            MemSys::Hub::Pull()->Dealloc(frames);
        }
        frames = nullptr;
    }
    VSetData(frames);
    CloseFile(m_fileRef);
    return CResource::FinishLoading(buffer, size);
}

// 0x004074A0
int CAnimResource::Offload()
{
    if (VData()) {
        if (void* frames = VData()) {
            DestroyAnimFrames(frames);
            MemSys::Hub::Pull()->Dealloc(frames);
        }
        VSetData(nullptr);
    }
    VFreeBuffer();
    return CResource::Offload();
}

// 0x00407290
void CAnimResource::FreeBuffer()
{
    if (m_archive) {
        Packs::Offload(m_archive);
        m_archive = 0;
        m_octets = nullptr;
        return;
    }
    if (m_octets)
        MemSys::FreeMalloc(m_octets);
    m_archive = 0;
    m_octets = nullptr;
}

// --- cutscene -------------------------------------------------------------------------------------------------

// 0x00408AC0
int CCutsceneResource::FetchKind() const { return kCutscene; }
// 0x00408AB0
const char* CCutsceneResource::FetchTitle() const { return "Cutscene"; }

// 0x00408AD0 (no StartLoading: the state stays as it was)
int CCutsceneResource::ReadIn(const char* name, bool async, bool /*useArchive*/, void* params, void* /*structure*/)
{
    MemSys::PushMemProfile(name);
    void* memory = MemSys::AllocZeroed(0x34D0);
    auto* cutscene = memory ? static_cast<GameObjects::CCinematicTable*>(memory)->MakeNew() : nullptr;
    char file[256];
    PlatformName(name, file);
    if (!cutscene->ReadIn(file, 1, async, static_cast<const uint32_t*>(params)))
        return -1;
    VSetData(cutscene);
    MemSys::PopMemProfile();
    return 0;
}

// 0x00408A40
int CCutsceneResource::Offload()
{
    if (void* cutscene = VData()) {
        reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(cutscene))[0])(cutscene, 1);
        VSetData(nullptr);
    }
    return 0;
}

// 0x00408AA0
bool CCutsceneResource::IsLoaded()
{
    return static_cast<GameObjects::CCinematicTable*>(VData())->IsLoaded();
}

// --- node array -----------------------------------------------------------------------------------------------

// 0x00408BE0
int CNodeTableResource::FetchKind() const { return m_owned ? kOwnedNodeArray : kNodeArray; }
// 0x00408BD0
const char* CNodeTableResource::FetchTitle() const { return "Node Array"; }

// 0x00408EA0
int CNodeTableResource::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_args = static_cast<char*>(AllocLoadParams(0x40));
    if (params)
        CopyWords(m_args, params, 0x10);
    VFinishLoading(data, size);
    return 0;
}

// 0x00408D10
int CNodeTableResource::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_args = static_cast<char*>(AllocLoadParams(0x40));
    if (params)
        CopyWords(m_args, params, 0x10);
    CopyString(m_args, name);
    MemSys::EnterHeapScope(MemSys::TopDownHeap());
    uint32_t size;
    if (useArchive) {
        ReadArchive(this, &m_archive, name, &size, true);
    } else {
        if (!ReadFile(this, name, async, &size))
            return -1; // (with the heap context still pushed, as in the original)
        StartReading(this, m_fileRef, m_octets, size, async);
    }
    if (!async)
        FinishNow(this, size);
    MemSys::LeaveHeapScope();
    return 0;
}

// 0x00408C10
bool CNodeTableResource::FinishLoading(void* buffer, uint32_t size)
{
    m_sourcePath = Checksums::HashFromText(m_args);
    char arrayName[128];
    LevelNodes::MakeNodeArrayName(m_args, arrayName);
    ParseQbFile(m_args, buffer);
    LevelNodes::NodeManagerInstance()->AddNodeArray(LevelNodes::NodeArrayChecksum(m_args), m_owned);
    Scripts::FreeScriptChecksumNames();
    Scripts::CArray* nodes = Scripts::FetchList(arrayName);
    m_arrayName = Checksums::HashFromText(arrayName);
    VSetData(nodes);
    m_nodes = VData();
    CloseFile(m_fileRef);
    VFreeBuffer();
    FreeLoadParams(m_args);
    m_args = nullptr;
    return CResource::FinishLoading(buffer, size);
}

// 0x00408F00
int CNodeTableResource::Offload()
{
    auto* nodes = static_cast<Scripts::CArray*>(m_nodes);
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        uint32_t id;
        reinterpret_cast<CStruct*>(nodes->FetchPart(i))->ReadHashValue(0xA1DC81F9u, &id, 0); // name
        if (GameObjects::CEntity* object = GameObjects::FindObject(id))
            reinterpret_cast<void(__thiscall*)(GameObjects::CEntity*)>((*reinterpret_cast<void***>(object))[6])(object);
        nodes = static_cast<Scripts::CArray*>(m_nodes);
    }
    if (LevelNodes::CNodeManager* manager = LevelNodes::NodeManagerInstance())
        manager->RemoveNodeArray(m_arrayName);
    if (VData())
        UnloadQbFile(m_sourcePath);
    VFreeBuffer();
    return CResource::Offload();
}

// 0x004090D0
void* CNodeTableResource::BuildNodeTable(uint32_t name, int count)
{
    Scripts::CArray* array = Scripts::NewArray();
    if (array)
        array->PrepareAll();
    array->ConfigureSizeKind(count, Scripts::QBVAL_STRUCT);
    for (int i = 0; i < count; ++i) {
        CStruct* node = Scripts::NewStruct();
        if (node)
            node->PrepareAll();
        array->SetElement(i, reinterpret_cast<uint32_t>(node));
    }
    if (Scripts::CSymbolTableEntry* old = Scripts::FindSymbol(name))
        Scripts::CleanUpAndRemoveSymbol(old);
    Scripts::CSymbolTableEntry* symbol = Scripts::CreateSymbol(name);
    symbol->raw = reinterpret_cast<uint32_t>(array);
    symbol->type = Scripts::QBVAL_ARRAY;
    m_arrayName = name;
    m_nodes = array;
    return array;
}

// --- scene ----------------------------------------------------------------------------------------------------

// 0x004093D0
const char* CSceneAsset::FetchTitle() const { return "Scene"; }

// 0x004092D0
void CSceneAsset::SetUpParams(void* params, void* structure)
{
    m_args = static_cast<uint8_t*>(AllocLoadParams(0x48));
    *reinterpret_cast<uint32_t*>(m_args) = 0;
    m_args[4] = 1;
    m_args[5] = 0;
    m_args[6] = 0;
    m_args[7] = 0;
    if (params)
        CopyWords(m_args, params, 0x12);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->ReadHashValue(0xDC8116B2u, reinterpret_cast<uint32_t*>(m_args), 0);
        m_args[4] = s->HasBareFlag(0x9E6EA452u);
        m_args[5] = s->HasBareFlag(0xB5BD28D8u);
        m_args[6] = s->HasBareFlag(0x3D559E7Du);
        m_args[7] = s->HasBareFlag(0x417149C8u);
    }
}

// 0x00409520
int CSceneAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy) {
        MemSys::PushMemProfile("Scene from data stream");
        data = CopyData(this, data, size);
        MemSys::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x004093F0
int CSceneAsset::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_args + 8), name);
    char file[256];
    SceneFileName(file, name, m_args[7]);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_fileRef, m_octets, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x00409240
bool CSceneAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* textures = GroupData(this);
    VSetData(MakeScene(reinterpret_cast<char*>(m_args + 8), buffer, textures, m_args[4], m_args[5], m_args[6],
                       m_args[7]));
    FreeLoadParams(m_args);
    m_args = nullptr;
    CloseFile(m_fileRef);
    return CResource::FinishLoading(buffer, size);
}

// 0x00409380
int CSceneAsset::Offload()
{
    if (VData()) {
        UnloadScene(VData(), 0);
        FreeMallocBuffer(m_octets);
        VSetData(nullptr);
    }
    return CResource::Offload();
}

// --- scene collision ------------------------------------------------------------------------------------------

// 0x00409750
const char* CSceneCollisionAsset::FetchTitle() const { return "SceneColl"; }

// 0x00409690
void CSceneCollisionAsset::SetUpParams(void* params, void* structure)
{
    m_args = static_cast<uint8_t*>(AllocLoadParams(0x48));
    *reinterpret_cast<uint32_t*>(m_args) = 0;
    m_args[4] = 0;
    if (params)
        CopyWords(m_args, params, 0x12);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->ReadHashValue(0x26861025u, reinterpret_cast<uint32_t*>(m_args), 0);
        m_args[4] = s->HasBareFlag(0x417149C8u);
    }
}

// 0x00409890
int CSceneCollisionAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    MemSys::PushMemProfile("Collision processing");
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy) {
        MemSys::PushMemProfile("SceneColl from data stream");
        data = CopyData(this, data, size);
        MemSys::PopMemProfile();
    }
    VFinishLoading(data, size);
    MemSys::PopMemProfile();
    return 0;
}

// 0x00409760
int CSceneCollisionAsset::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_args + 5), name);
    char file[256];
    CollisionFileName(file, name, m_args[4]);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_fileRef, m_octets, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x00409610
bool CSceneCollisionAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* scene = GroupData(this);
    AddCollision(scene, buffer, m_args[4]);
    VSetData(SceneCollision(scene));
    FreeLoadParams(m_args);
    m_args = nullptr;
    CloseFile(m_fileRef);
    return CResource::FinishLoading(buffer, size);
}

// 0x00409700
int CSceneCollisionAsset::Offload()
{
    if (VData()) {
        RemoveCollision(GroupData(this));
        FreeMallocBuffer(m_octets);
        VSetData(nullptr);
    }
    return CResource::Offload();
}

// --- skeleton -------------------------------------------------------------------------------------------------

// 0x00409A40
int CRigResource::FetchKind() const { return kSkeleton; }
// 0x00409A30
const char* CRigResource::FetchTitle() const { return "Skeleton"; }

// 0x00409990 (the data isn't copied, whatever `copy` says)
int CRigResource::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    MemSys::PushMemProfile("Skeleton processing");
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    VFinishLoading(data, size);
    MemSys::PopMemProfile();
    return 0;
}

// 0x00409A50
int CRigResource::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    char file[256];
    PlatformName(name, file);
    m_fileRef = FileLoad::AsyncOpen(file, !async, 100);
    if (!m_fileRef)
        return -1;
    uint32_t size = static_cast<uint32_t>(m_fileRef->GetFileSize());
    MemSys::EnterHeapScope(MemSys::TopDownHeap());
    m_octets = MemSys::AllocBlock(size);
    MemSys::LeaveHeapScope();
    StartReading(this, m_fileRef, m_octets, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x00409B80
bool CRigResource::FinishLoading(void* buffer, uint32_t size)
{
    void* memory = MemSys::AllocZeroed(0x598);
    void* skeleton = memory ? ConstructSkeletonData(memory) : nullptr;
    if (!LoadSkeletonData(skeleton, buffer, size))
        return false; // (the skeleton data leaks, as in the original)
    VSetData(skeleton);
    CloseFile(m_fileRef);
    VFreeBuffer();
    return CResource::FinishLoading(buffer, size);
}

// 0x004099E0
int CRigResource::Offload()
{
    if (void* skeleton = VData()) {
        reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(skeleton))[0])(skeleton, 1);
        VSetData(nullptr);
        VFreeBuffer();
    }
    return CResource::Offload();
}

// 0x00409A10
void CRigResource::FreeBuffer()
{
    FreeMallocBuffer(m_octets);
}

// --- skinned model --------------------------------------------------------------------------------------------

// 0x00409E90
SkinParams* SkinParams::Clear()
{
    flag0 = 0;
    value = 0;
    casData = nullptr;
    hasCas = 0;
    onHeap = 0;
    return this;
}

// 0x00409E00
int CSkinResource::FetchKind() const { return kSkinnedModel; }
// 0x00409DF0
const char* CSkinResource::FetchTitle() const { return "Skin"; }

// 0x00409E10
void CSkinResource::SetUpParams(void* params, void* structure)
{
    m_args = static_cast<SkinParams*>(AllocLoadParams(0x14));
    m_args->Clear(); // (inlined)
    if (params)
        CopyWords(m_args, params, 5);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->FetchIntValue(0xBF34D221u, &m_args->value, 0);
        m_args->flag0 = s->HasBareFlag(0x01A1482Eu);
    }
}

// 0x0040A140
int CSkinResource::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy && !m_args->onHeap) {
        MemSys::PushMemProfile("skin from data stream");
        data = CopyData(this, data, size);
        MemSys::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x00409EB0
int CSkinResource::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    MemSys::PushMemProfile(name);
    char base[512];
    bool hasCas = SplitSkinName(name, base);
    TextUtil::ToLowerText(base);
    m_args->onHeap = 0;
    // (0x00645904 / 0x00645900: "skin" for a model with a .cas file, else "mdl")
    const char* kind = hasCas ? "skin" : "mdl";
    char file[512];
    std::snprintf(file, sizeof(file), "%s.%s.%s", base, kind, Engine3D::PlatformExtension());
    m_args->hasCas = hasCas;
    ++m_filesReading;
    uint32_t size;
    if (hasCas && !async) {
        ReadArchive(this, &m_archive, file, &size, false);
    } else {
        if (!ReadFile(this, file, async, &size))
            return -1;
        StartReading(this, m_fileRef, m_octets, size, async);
    }
    if (hasCas) {
        char cas[512];
        std::snprintf(cas, sizeof(cas), "%s.cas.%s", base, Engine3D::PlatformExtension());
        ++m_filesReading;
        m_casFile = FileLoad::AsyncOpen(cas, !async, 100);
        if (!m_casFile)
            return -1;
        uint32_t casSize = static_cast<uint32_t>(m_casFile->GetFileSize());
        m_casBuffer = MemSys::AllocBlock(casSize);
        m_args->casData = m_casBuffer;
        StartReading(this, m_casFile, m_casBuffer, casSize, async);
    }
    if (m_args->onHeap)
        MemSys::LeaveHeapScope();
    if (!async) {
        if (m_casBuffer)
            --m_filesReading;
        --m_filesReading;
        FinishNow(this, size);
    }
    MemSys::PopMemProfile();
    return 0;
}

// 0x00409C70
bool CSkinResource::FinishLoading(void* buffer, uint32_t size)
{
    void* textures = GroupData(this);
    SkinParams* p = m_args;
    VSetData(MakeSkinnedModel(m_checksum, buffer, size, p->casData, textures, p->value, p->hasCas, p->flag0));
    if (m_args->onHeap && m_octets)
        VFreeBuffer();
    FreeLoadParams(m_args);
    m_args = nullptr;
    CloseFile(m_fileRef);
    CloseFile(m_casFile);
    FreeMallocBuffer(m_casBuffer);
    return CResource::FinishLoading(buffer, size);
}

// 0x00409D30
int CSkinResource::Offload()
{
    if (VData()) {
        UnloadSkinnedModel(VData(), 1);
        VSetData(nullptr);
    }
    VFreeBuffer();
    return CResource::Offload();
}

// 0x00409D70
bool CSkinResource::CancelNow()
{
    if (m_casFile) {
        m_casFile->Cancel();
        FileLoad::AsyncClose(m_casFile);
        m_casFile = nullptr;
    }
    return CResource::CancelNow();
}

// 0x00409DA0
void CSkinResource::FreeBuffer()
{
    FreeMallocBuffer(m_casBuffer);
    if (m_archive) {
        Packs::Offload(m_archive);
        m_archive = 0;
        m_octets = nullptr;
        return;
    }
    if (m_octets)
        MemSys::FreeMalloc(m_octets);
    m_archive = 0;
    m_octets = nullptr;
}

// 0x00409C60
bool CSkinResource::SetRead()
{
    if (--m_filesReading != 0)
        return false;
    return CResource::SetRead();
}

// --- skin collision -------------------------------------------------------------------------------------------

// 0x0040A290
int CSkinCollisionAsset::FetchKind() const { return kSkinCollision; }
// 0x0040A280
const char* CSkinCollisionAsset::FetchTitle() const { return "SkinColl"; }

namespace {
// The skin collision's parameters (0x44 bytes): from `params`, then the structure's checksum.
uint8_t* SkinCollisionParams(void* params, void* structure)
{
    auto* p = static_cast<uint8_t*>(AllocLoadParams(0x44));
    *reinterpret_cast<uint32_t*>(p) = 0;
    if (params)
        CopyWords(p, params, 0x11);
    if (structure)
        static_cast<CStruct*>(structure)->ReadHashValue(0xFD8697E1u, reinterpret_cast<uint32_t*>(p), 0);
    return p;
}
} // namespace

// 0x0040A400
int CSkinCollisionAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_args = SkinCollisionParams(params, structure);
    if (copy) {
        MemSys::PushMemProfile("SkinColl from data stream");
        data = CopyData(this, data, size);
        MemSys::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x0040A2A0
int CSkinCollisionAsset::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_args = SkinCollisionParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_args + 4), name);
    char file[256];
    SkinCollisionFileName(file, name);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_fileRef, m_octets, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x0040A510
bool CSkinCollisionAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* model = GroupData(this);
    AddSkinCollision(model, buffer);
    VSetData(*reinterpret_cast<void**>(static_cast<uint8_t*>(model) + 0x8C));
    FreeLoadParams(m_args);
    m_args = nullptr;
    CloseFile(m_fileRef);
    return CResource::FinishLoading(buffer, size);
}

// 0x0040A230
int CSkinCollisionAsset::Offload()
{
    if (VData()) {
        RemoveSkinCollision(GroupData(this));
        FreeMallocBuffer(m_octets);
        VSetData(nullptr);
    }
    return CResource::Offload();
}

// --- texture dictionary ---------------------------------------------------------------------------------------

// 0x0040A710
const char* CTexDictAsset::FetchTitle() const { return "TexDict"; }

// 0x0040A620
void CTexDictAsset::SetUpParams(void* params, void* structure)
{
    m_args = static_cast<uint8_t*>(AllocLoadParams(0x4C));
    m_args[0] = 1;
    *reinterpret_cast<int*>(m_args + 4) = 0;
    m_args[8] = 0;
    m_args[9] = 0;
    if (params)
        CopyWords(m_args, params, 0x13);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->FetchIntValue(0xBF34D221u, reinterpret_cast<int*>(m_args + 4), 0);
        m_args[0] = s->HasBareFlag(0xE6237329u);
        m_args[8] = s->HasBareFlag(0x6875B9C0u);
        m_args[9] = s->HasBareFlag(0x86B52462u);
    }
}

// 0x0040A840
int CTexDictAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy) {
        MemSys::PushMemProfile("TexDict from data stream");
        data = CopyData(this, data, size);
        MemSys::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x0040A720
int CTexDictAsset::ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_args + 0xA), name);
    char file[256];
    TexDictFileName(file, name);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_fileRef, m_octets, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x0040A5A0
bool CTexDictAsset::FinishLoading(void* buffer, uint32_t size)
{
    VSetData(MakeTexDict(m_checksum, buffer, size, m_args[0], *reinterpret_cast<int*>(m_args + 4), m_args[8],
                         m_args[9]));
    FreeLoadParams(m_args);
    m_args = nullptr;
    CloseFile(m_fileRef);
    return CResource::FinishLoading(buffer, size);
}

// 0x0040A6C0
int CTexDictAsset::Offload()
{
    if (VData()) {
        UnloadTexDict(VData());
        FreeMallocBuffer(m_octets);
        VSetData(nullptr);
    }
    return CResource::Offload();
}

} // namespace Ass
