// The asset types: animations, cutscenes, node arrays, scenes, scene collision, skeletons, skinned models, skin
// collision and texture dictionaries - how each is read and turned into the engine's data.

#include <cstdio>
#include <cstring>

#include "game/ass/asset.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/file/async.h"
#include "game/file/pip.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_mesh.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/engine_glue.h"
#include "game/obj/cutscene.h"
#include "game/obj/object.h"
#include "game/script/node_manager.h"
#include "game/script/qb.h"
#include "game/script/script_startup.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ass {

namespace {

using Script::CStruct;

// --- the engine's loaders ---------------------------------------------------------------------------------
// Gfx::CBonedAnimFrameData (0x3C bytes): 0x00482AF0 the constructor, 0x00482B30 the destructor, 0x004856C0 Load.
void* ConstructAnimFrames(void* frames) { return static_cast<Gfx::CBonedAnimFrameData*>(frames)->Construct(); }
void DestroyAnimFrames(void* frames) { static_cast<Gfx::CBonedAnimFrameData*>(frames)->DestroyBody(); }
bool LoadAnimFrames(void* frames, void* buffer, uint32_t size)
{
    return static_cast<Gfx::CBonedAnimFrameData*>(frames)->Load(static_cast<const uint8_t*>(buffer), size, true);
}
// The skeleton data (0x598 bytes): 0x004A2C20 the constructor, 0x004A39C0 Load.
void* ConstructSkeletonData(void* skeleton) { return static_cast<Gfx::CSkeletonData*>(skeleton)->Construct(); }
bool LoadSkeletonData(void* skeleton, void* buffer, uint32_t size)
{
    return static_cast<Gfx::CSkeletonData*>(skeleton)->Load(static_cast<const uint32_t*>(buffer), size, true);
}
// Scripts: 0x005A5990 parses a .qb file's contents, 0x005A59B0 unloads one (by its name's checksum).
void ParseQbFile(const char* name, void* buffer)
{
    Script::LoadQBFileData(name, static_cast<const uint8_t*>(buffer), 1, 1);
}
void UnloadQbFile(uint32_t name) { Script::UnloadQBFile(name); }
// Scenes (Nx): the file name, made from the buffer (with the textures), unloaded.
void SceneFileName(char* out, const char* name, uint8_t flag) { Nx::Engine::SceneFileName(out, name, flag); }
void* MakeScene(const char* name, void* buffer, void* textures, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return Nx::LoadNamedSceneFromMemory(name, static_cast<const uint8_t*>(buffer), static_cast<Nx::CTexDict*>(textures),
                                        a != 0, b != 0, c != 0, d);
}
void UnloadScene(void* scene, int flag) { Nx::Engine::UnloadScene(static_cast<Nx::CScene*>(scene), flag); }
// Scene collision: its file name, added to a scene, the scene's collision, removed.
void CollisionFileName(char* out, const char* name, uint8_t flag)
{
    (void)flag; // (not read)
    Nx::MakeCollisionFileName(out, name);
}
void AddCollision(void* scene, void* buffer, uint8_t flag)
{
    static_cast<Nx::CScene*>(scene)->AddCollisionData(static_cast<const uint8_t*>(buffer), flag);
}
void* SceneCollision(void* scene) { return static_cast<Nx::CScene*>(scene)->GetCollStatic(0); }
void RemoveCollision(void* scene) { static_cast<Nx::CScene*>(scene)->UnloadCollision(); }
// Skinned models: whether a name is a model with a .cas file (its base name out), the model made, unloaded.
bool SplitSkinName(const char* name, char* base) { return Nx::Engine::SplitSkinName(name, base); }
void* MakeSkinnedModel(uint32_t checksum, void* buffer, uint32_t size, void* cas, void* textures, int value,
                       uint8_t hasCas, uint8_t flag)
{
    return Nx::Engine::LoadModelFromMemory(checksum, static_cast<const uint8_t*>(buffer), static_cast<int>(size), cas,
                                           textures, value, hasCas, flag);
}
void UnloadSkinnedModel(void* model, int flag)
{
    (void)flag; // 0x0048C080 is a jump to DeleteModelLights (its second argument isn't read)
    Nx::DeleteModelLights(static_cast<Nx::CPcModelLights*>(model));
}
// Skin collision: its file name, added to a model, removed.
void SkinCollisionFileName(char* out, const char* name) { Nx::SkinCollisionFileName(out, name); }
void AddSkinCollision(void* model, void* buffer)
{
    static_cast<Nx::CMesh*>(model)->AddCollision(static_cast<const uint8_t*>(buffer));
}
void RemoveSkinCollision(void* model) { static_cast<Nx::CMesh*>(model)->RemoveCollision(); }
// Texture dictionaries: the file name, made, unloaded.
void TexDictFileName(char* out, const char* name) { Nx::TexDictFileName(out, name); }
void* MakeTexDict(uint32_t checksum, void* buffer, uint32_t size, uint8_t a, int value, uint8_t b, uint8_t c)
{
    return Nx::LoadTexDictFromMemory(checksum, static_cast<const uint8_t*>(buffer), size, a, static_cast<uint32_t>(value),
                                     b, c);
}
void UnloadTexDict(void* textures) { Nx::UnloadTexDict(static_cast<Nx::CTexDict*>(textures)); }

// --- shared steps ---------------------------------------------------------------------------------------------
uint8_t* ParamsPool() { return reinterpret_cast<uint8_t*>(0x00691758); }
uint32_t& ParamsInUse() { return GameVariable<uint32_t>(0x006918D8); }

// strcpy (no bounds, as the original's).
void CopyString(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The file opened (null: false) and read into a new buffer; an async read calls back when it's done.
bool ReadFile(CAsset* asset, const char* file, bool async, uint32_t* size)
{
    asset->m_file = File::AsyncOpen(file, !async, 100);
    if (!asset->m_file)
        return false;
    *size = static_cast<uint32_t>(asset->m_file->GetFileSize());
    asset->m_buffer = Mem::Malloc(*size);
    return true;
}
void StartReading(CAsset* asset, File::AsyncFileHandle* file, void* buffer, uint32_t size, bool async)
{
    if (async) {
        file->m_callbackMask = 0;
        file->m_callbackMask |= 1u << File::kAsyncRead;
        file->SetCallback(&CAsset::ReadCallback, asset, nullptr);
    }
    file->Read(buffer, 1, static_cast<int>(size));
}
// An archive file through Pip: its lowercased name's checksum kept (`lowered` false: the name is already).
void ReadArchive(CAsset* asset, uint32_t* archive, const char* file, uint32_t* size, bool lower)
{
    char lowered[256];
    const char* key = file;
    if (lower) {
        CopyString(lowered, file);
        Str::LowerCase(lowered);
        key = lowered;
    }
    *archive = Crc::GenerateCRCFromString(key);
    asset->m_buffer = Pip::Load(file, false, nullptr, nullptr, nullptr);
    *size = static_cast<uint32_t>(Pip::GetFileSize(file));
}
// A blocking load finishes at once.
void FinishNow(CAsset* asset, uint32_t size)
{
    asset->m_size = size;
    asset->VProcessBuffer();
}
// LoadFromBuffer's copy: the data copied into a buffer of the asset's own.
void* CopyData(CAsset* asset, void* data, uint32_t size)
{
    asset->m_buffer = Mem::Malloc(size);
    std::memcpy(asset->m_buffer, data, size);
    return asset->m_buffer;
}
void CloseFile(File::AsyncFileHandle*& file)
{
    if (file) {
        File::AsyncClose(file);
        file = nullptr;
    }
}
void FreeMallocBuffer(void*& buffer)
{
    if (buffer) {
        Mem::FreeMalloc(buffer);
        buffer = nullptr;
    }
}
void* GroupData(CAsset* asset) { return asset->m_group->VData(); }
const char* PlatformName(const char* name, char* out)
{
    std::snprintf(out, 256, "%s.%s", name, Nx::PlatformExtension());
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
bool ReloadAsset(CAsset* asset, const char* name)
{
    asset->VUnload();
    int result = reinterpret_cast<int(__thiscall*)(CAsset*, const char*, int, int, int, int)>(asset->m_vtable[5])(
        asset, name, 0, 0, 0, 0);
    return result == 0;
}

// --- animation ------------------------------------------------------------------------------------------------

// 0x004072E0
int CAnimAsset::GetType() const { return kAnimation; }
// 0x004072D0
const char* CAnimAsset::GetName() const { return "Animation"; }

// 0x004071C0
int CAnimAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    Mem::PushMemProfile("Animation Sequence processing");
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    if (copy) {
        Mem::PushMemProfile("anim from data stream");
        data = CopyData(this, data, size);
        Mem::PopMemProfile();
    }
    VFinishLoading(data, size);
    Mem::PopMemProfile();
    return 0;
}

// 0x00407300
int CAnimAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    Script::GetInteger(0x25DC7904u); // (a debug leftover: looked up and not used)
    Mem::PushMemProfile(name);
    char file[256];
    PlatformName(name, file);
    uint32_t size;
    if (useArchive) {
        ReadArchive(this, &m_archive, file, &size, true);
    } else {
        if (!ReadFile(this, file, async, &size))
            return -1;
        StartReading(this, m_file, m_buffer, size, async);
    }
    if (!async)
        FinishNow(this, size); // (the original reads the checksum first and doesn't use it)
    Mem::PopMemProfile();
    return 0;
}

// 0x004074F0
bool CAnimAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* memory = Mem::New(0x3C);
    void* frames = memory ? ConstructAnimFrames(memory) : nullptr;
    if (!LoadAnimFrames(frames, buffer, size)) { // (called on a failed allocation too, as in the original)
        Script::GetInteger(0x25DC7904u);
        if (frames) {
            DestroyAnimFrames(frames);
            Mem::Manager::Get()->Free(frames);
        }
        frames = nullptr;
    }
    VSetData(frames);
    CloseFile(m_file);
    return CAsset::FinishLoading(buffer, size);
}

// 0x004074A0
int CAnimAsset::Unload()
{
    if (VData()) {
        if (void* frames = VData()) {
            DestroyAnimFrames(frames);
            Mem::Manager::Get()->Free(frames);
        }
        VSetData(nullptr);
    }
    VFreeBuffer();
    return CAsset::Unload();
}

// 0x00407290
void CAnimAsset::FreeBuffer()
{
    if (m_archive) {
        Pip::Unload(m_archive);
        m_archive = 0;
        m_buffer = nullptr;
        return;
    }
    if (m_buffer)
        Mem::FreeMalloc(m_buffer);
    m_archive = 0;
    m_buffer = nullptr;
}

// --- cutscene -------------------------------------------------------------------------------------------------

// 0x00408AC0
int CCutsceneAsset::GetType() const { return kCutscene; }
// 0x00408AB0
const char* CCutsceneAsset::GetName() const { return "Cutscene"; }

// 0x00408AD0 (no StartLoading: the state stays as it was)
int CCutsceneAsset::Load(const char* name, bool async, bool /*useArchive*/, void* params, void* /*structure*/)
{
    Mem::PushMemProfile(name);
    void* memory = Mem::Calloc(0x34D0);
    auto* cutscene = memory ? static_cast<Obj::CCutsceneData*>(memory)->Construct() : nullptr;
    char file[256];
    PlatformName(name, file);
    if (!cutscene->Load(file, 1, async, static_cast<const uint32_t*>(params)))
        return -1;
    VSetData(cutscene);
    Mem::PopMemProfile();
    return 0;
}

// 0x00408A40
int CCutsceneAsset::Unload()
{
    if (void* cutscene = VData()) {
        reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(cutscene))[0])(cutscene, 1);
        VSetData(nullptr);
    }
    return 0;
}

// 0x00408AA0
bool CCutsceneAsset::IsLoaded()
{
    return static_cast<Obj::CCutsceneData*>(VData())->IsLoaded();
}

// --- node array -----------------------------------------------------------------------------------------------

// 0x00408BE0
int CNodeArrayAsset::GetType() const { return m_owned ? kOwnedNodeArray : kNodeArray; }
// 0x00408BD0
const char* CNodeArrayAsset::GetName() const { return "Node Array"; }

// 0x00408EA0
int CNodeArrayAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_params = static_cast<char*>(AllocLoadParams(0x40));
    if (params)
        CopyWords(m_params, params, 0x10);
    VFinishLoading(data, size);
    return 0;
}

// 0x00408D10
int CNodeArrayAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_params = static_cast<char*>(AllocLoadParams(0x40));
    if (params)
        CopyWords(m_params, params, 0x10);
    CopyString(m_params, name);
    Mem::PushContext(Mem::TopDownHeap());
    uint32_t size;
    if (useArchive) {
        ReadArchive(this, &m_archive, name, &size, true);
    } else {
        if (!ReadFile(this, name, async, &size))
            return -1; // (with the heap context still pushed, as in the original)
        StartReading(this, m_file, m_buffer, size, async);
    }
    if (!async)
        FinishNow(this, size);
    Mem::PopContext();
    return 0;
}

// 0x00408C10
bool CNodeArrayAsset::FinishLoading(void* buffer, uint32_t size)
{
    m_fileName = Crc::GenerateCRCFromString(m_params);
    char arrayName[128];
    SkateScript::MakeNodeArrayName(m_params, arrayName);
    ParseQbFile(m_params, buffer);
    SkateScript::NodeManagerInstance()->AddNodeArray(SkateScript::NodeArrayChecksum(m_params), m_owned);
    Script::FreeScriptChecksumNames();
    Script::CArray* nodes = Script::GetArray(arrayName);
    m_arrayName = Crc::GenerateCRCFromString(arrayName);
    VSetData(nodes);
    m_nodes = VData();
    CloseFile(m_file);
    VFreeBuffer();
    FreeLoadParams(m_params);
    m_params = nullptr;
    return CAsset::FinishLoading(buffer, size);
}

// 0x00408F00
int CNodeArrayAsset::Unload()
{
    auto* nodes = static_cast<Script::CArray*>(m_nodes);
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        uint32_t id;
        reinterpret_cast<CStruct*>(nodes->GetElement(i))->GetChecksum(0xA1DC81F9u, &id, 0); // name
        if (Obj::CObject* object = Obj::FindObject(id))
            reinterpret_cast<void(__thiscall*)(Obj::CObject*)>((*reinterpret_cast<void***>(object))[6])(object);
        nodes = static_cast<Script::CArray*>(m_nodes);
    }
    if (SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance())
        manager->RemoveNodeArray(m_arrayName);
    if (VData())
        UnloadQbFile(m_fileName);
    VFreeBuffer();
    return CAsset::Unload();
}

// 0x004090D0
void* CNodeArrayAsset::CreateNodeArray(uint32_t name, int count)
{
    Script::CArray* array = Script::NewArray();
    if (array)
        array->Init();
    array->SetSizeAndType(count, Script::ESYMBOLTYPE_STRUCTURE);
    for (int i = 0; i < count; ++i) {
        CStruct* node = Script::NewStruct();
        if (node)
            node->Init();
        array->SetElement(i, reinterpret_cast<uint32_t>(node));
    }
    if (Script::CSymbolTableEntry* old = Script::FindSymbol(name))
        Script::CleanUpAndRemoveSymbol(old);
    Script::CSymbolTableEntry* symbol = Script::CreateSymbol(name);
    symbol->raw = reinterpret_cast<uint32_t>(array);
    symbol->type = Script::ESYMBOLTYPE_ARRAY;
    m_arrayName = name;
    m_nodes = array;
    return array;
}

// --- scene ----------------------------------------------------------------------------------------------------

// 0x004093D0
const char* CSceneAsset::GetName() const { return "Scene"; }

// 0x004092D0
void CSceneAsset::SetUpParams(void* params, void* structure)
{
    m_params = static_cast<uint8_t*>(AllocLoadParams(0x48));
    *reinterpret_cast<uint32_t*>(m_params) = 0;
    m_params[4] = 1;
    m_params[5] = 0;
    m_params[6] = 0;
    m_params[7] = 0;
    if (params)
        CopyWords(m_params, params, 0x12);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->GetChecksum(0xDC8116B2u, reinterpret_cast<uint32_t*>(m_params), 0);
        m_params[4] = s->ContainsFlag(0x9E6EA452u);
        m_params[5] = s->ContainsFlag(0xB5BD28D8u);
        m_params[6] = s->ContainsFlag(0x3D559E7Du);
        m_params[7] = s->ContainsFlag(0x417149C8u);
    }
}

// 0x00409520
int CSceneAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy) {
        Mem::PushMemProfile("Scene from data stream");
        data = CopyData(this, data, size);
        Mem::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x004093F0
int CSceneAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_params + 8), name);
    char file[256];
    SceneFileName(file, name, m_params[7]);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_file, m_buffer, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x00409240
bool CSceneAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* textures = GroupData(this);
    VSetData(MakeScene(reinterpret_cast<char*>(m_params + 8), buffer, textures, m_params[4], m_params[5], m_params[6],
                       m_params[7]));
    FreeLoadParams(m_params);
    m_params = nullptr;
    CloseFile(m_file);
    return CAsset::FinishLoading(buffer, size);
}

// 0x00409380
int CSceneAsset::Unload()
{
    if (VData()) {
        UnloadScene(VData(), 0);
        FreeMallocBuffer(m_buffer);
        VSetData(nullptr);
    }
    return CAsset::Unload();
}

// --- scene collision ------------------------------------------------------------------------------------------

// 0x00409750
const char* CSceneCollisionAsset::GetName() const { return "SceneColl"; }

// 0x00409690
void CSceneCollisionAsset::SetUpParams(void* params, void* structure)
{
    m_params = static_cast<uint8_t*>(AllocLoadParams(0x48));
    *reinterpret_cast<uint32_t*>(m_params) = 0;
    m_params[4] = 0;
    if (params)
        CopyWords(m_params, params, 0x12);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->GetChecksum(0x26861025u, reinterpret_cast<uint32_t*>(m_params), 0);
        m_params[4] = s->ContainsFlag(0x417149C8u);
    }
}

// 0x00409890
int CSceneCollisionAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    Mem::PushMemProfile("Collision processing");
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy) {
        Mem::PushMemProfile("SceneColl from data stream");
        data = CopyData(this, data, size);
        Mem::PopMemProfile();
    }
    VFinishLoading(data, size);
    Mem::PopMemProfile();
    return 0;
}

// 0x00409760
int CSceneCollisionAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_params + 5), name);
    char file[256];
    CollisionFileName(file, name, m_params[4]);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_file, m_buffer, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x00409610
bool CSceneCollisionAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* scene = GroupData(this);
    AddCollision(scene, buffer, m_params[4]);
    VSetData(SceneCollision(scene));
    FreeLoadParams(m_params);
    m_params = nullptr;
    CloseFile(m_file);
    return CAsset::FinishLoading(buffer, size);
}

// 0x00409700
int CSceneCollisionAsset::Unload()
{
    if (VData()) {
        RemoveCollision(GroupData(this));
        FreeMallocBuffer(m_buffer);
        VSetData(nullptr);
    }
    return CAsset::Unload();
}

// --- skeleton -------------------------------------------------------------------------------------------------

// 0x00409A40
int CSkeletonAsset::GetType() const { return kSkeleton; }
// 0x00409A30
const char* CSkeletonAsset::GetName() const { return "Skeleton"; }

// 0x00409990 (the data isn't copied, whatever `copy` says)
int CSkeletonAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    Mem::PushMemProfile("Skeleton processing");
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    VFinishLoading(data, size);
    Mem::PopMemProfile();
    return 0;
}

// 0x00409A50
int CSkeletonAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    char file[256];
    PlatformName(name, file);
    m_file = File::AsyncOpen(file, !async, 100);
    if (!m_file)
        return -1;
    uint32_t size = static_cast<uint32_t>(m_file->GetFileSize());
    Mem::PushContext(Mem::TopDownHeap());
    m_buffer = Mem::Malloc(size);
    Mem::PopContext();
    StartReading(this, m_file, m_buffer, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x00409B80
bool CSkeletonAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* memory = Mem::Calloc(0x598);
    void* skeleton = memory ? ConstructSkeletonData(memory) : nullptr;
    if (!LoadSkeletonData(skeleton, buffer, size))
        return false; // (the skeleton data leaks, as in the original)
    VSetData(skeleton);
    CloseFile(m_file);
    VFreeBuffer();
    return CAsset::FinishLoading(buffer, size);
}

// 0x004099E0
int CSkeletonAsset::Unload()
{
    if (void* skeleton = VData()) {
        reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(skeleton))[0])(skeleton, 1);
        VSetData(nullptr);
        VFreeBuffer();
    }
    return CAsset::Unload();
}

// 0x00409A10
void CSkeletonAsset::FreeBuffer()
{
    FreeMallocBuffer(m_buffer);
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
int CSkinAsset::GetType() const { return kSkinnedModel; }
// 0x00409DF0
const char* CSkinAsset::GetName() const { return "Skin"; }

// 0x00409E10
void CSkinAsset::SetUpParams(void* params, void* structure)
{
    m_params = static_cast<SkinParams*>(AllocLoadParams(0x14));
    m_params->Clear(); // (inlined)
    if (params)
        CopyWords(m_params, params, 5);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->GetInteger(0xBF34D221u, &m_params->value, 0);
        m_params->flag0 = s->ContainsFlag(0x01A1482Eu);
    }
}

// 0x0040A140
int CSkinAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy && !m_params->onHeap) {
        Mem::PushMemProfile("skin from data stream");
        data = CopyData(this, data, size);
        Mem::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x00409EB0
int CSkinAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    Mem::PushMemProfile(name);
    char base[512];
    bool hasCas = SplitSkinName(name, base);
    Str::LowerCase(base);
    m_params->onHeap = 0;
    // (0x00645904 / 0x00645900: "skin" for a model with a .cas file, else "mdl")
    const char* kind = reinterpret_cast<const char*>(hasCas ? 0x00645904 : 0x00645900);
    char file[512];
    std::snprintf(file, sizeof(file), "%s.%s.%s", base, kind, Nx::PlatformExtension());
    m_params->hasCas = hasCas;
    ++m_filesReading;
    uint32_t size;
    if (hasCas && !async) {
        ReadArchive(this, &m_archive, file, &size, false);
    } else {
        if (!ReadFile(this, file, async, &size))
            return -1;
        StartReading(this, m_file, m_buffer, size, async);
    }
    if (hasCas) {
        char cas[512];
        std::snprintf(cas, sizeof(cas), "%s.cas.%s", base, Nx::PlatformExtension());
        ++m_filesReading;
        m_casFile = File::AsyncOpen(cas, !async, 100);
        if (!m_casFile)
            return -1;
        uint32_t casSize = static_cast<uint32_t>(m_casFile->GetFileSize());
        m_casBuffer = Mem::Malloc(casSize);
        m_params->casData = m_casBuffer;
        StartReading(this, m_casFile, m_casBuffer, casSize, async);
    }
    if (m_params->onHeap)
        Mem::PopContext();
    if (!async) {
        if (m_casBuffer)
            --m_filesReading;
        --m_filesReading;
        FinishNow(this, size);
    }
    Mem::PopMemProfile();
    return 0;
}

// 0x00409C70
bool CSkinAsset::FinishLoading(void* buffer, uint32_t size)
{
    void* textures = GroupData(this);
    SkinParams* p = m_params;
    VSetData(MakeSkinnedModel(m_checksum, buffer, size, p->casData, textures, p->value, p->hasCas, p->flag0));
    if (m_params->onHeap && m_buffer)
        VFreeBuffer();
    FreeLoadParams(m_params);
    m_params = nullptr;
    CloseFile(m_file);
    CloseFile(m_casFile);
    FreeMallocBuffer(m_casBuffer);
    return CAsset::FinishLoading(buffer, size);
}

// 0x00409D30
int CSkinAsset::Unload()
{
    if (VData()) {
        UnloadSkinnedModel(VData(), 1);
        VSetData(nullptr);
    }
    VFreeBuffer();
    return CAsset::Unload();
}

// 0x00409D70
bool CSkinAsset::Abort()
{
    if (m_casFile) {
        m_casFile->Cancel();
        File::AsyncClose(m_casFile);
        m_casFile = nullptr;
    }
    return CAsset::Abort();
}

// 0x00409DA0
void CSkinAsset::FreeBuffer()
{
    FreeMallocBuffer(m_casBuffer);
    if (m_archive) {
        Pip::Unload(m_archive);
        m_archive = 0;
        m_buffer = nullptr;
        return;
    }
    if (m_buffer)
        Mem::FreeMalloc(m_buffer);
    m_archive = 0;
    m_buffer = nullptr;
}

// 0x00409C60
bool CSkinAsset::SetRead()
{
    if (--m_filesReading != 0)
        return false;
    return CAsset::SetRead();
}

// --- skin collision -------------------------------------------------------------------------------------------

// 0x0040A290
int CSkinCollisionAsset::GetType() const { return kSkinCollision; }
// 0x0040A280
const char* CSkinCollisionAsset::GetName() const { return "SkinColl"; }

namespace {
// The skin collision's parameters (0x44 bytes): from `params`, then the structure's checksum.
uint8_t* SkinCollisionParams(void* params, void* structure)
{
    auto* p = static_cast<uint8_t*>(AllocLoadParams(0x44));
    *reinterpret_cast<uint32_t*>(p) = 0;
    if (params)
        CopyWords(p, params, 0x11);
    if (structure)
        static_cast<CStruct*>(structure)->GetChecksum(0xFD8697E1u, reinterpret_cast<uint32_t*>(p), 0);
    return p;
}
} // namespace

// 0x0040A400
int CSkinCollisionAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_params = SkinCollisionParams(params, structure);
    if (copy) {
        Mem::PushMemProfile("SkinColl from data stream");
        data = CopyData(this, data, size);
        Mem::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x0040A2A0
int CSkinCollisionAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    m_params = SkinCollisionParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_params + 4), name);
    char file[256];
    SkinCollisionFileName(file, name);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_file, m_buffer, size, async);
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
    FreeLoadParams(m_params);
    m_params = nullptr;
    CloseFile(m_file);
    return CAsset::FinishLoading(buffer, size);
}

// 0x0040A230
int CSkinCollisionAsset::Unload()
{
    if (VData()) {
        RemoveSkinCollision(GroupData(this));
        FreeMallocBuffer(m_buffer);
        VSetData(nullptr);
    }
    return CAsset::Unload();
}

// --- texture dictionary ---------------------------------------------------------------------------------------

// 0x0040A710
const char* CTexDictAsset::GetName() const { return "TexDict"; }

// 0x0040A620
void CTexDictAsset::SetUpParams(void* params, void* structure)
{
    m_params = static_cast<uint8_t*>(AllocLoadParams(0x4C));
    m_params[0] = 1;
    *reinterpret_cast<int*>(m_params + 4) = 0;
    m_params[8] = 0;
    m_params[9] = 0;
    if (params)
        CopyWords(m_params, params, 0x13);
    if (auto* s = static_cast<CStruct*>(structure)) {
        s->GetInteger(0xBF34D221u, reinterpret_cast<int*>(m_params + 4), 0);
        m_params[0] = s->ContainsFlag(0xE6237329u);
        m_params[8] = s->ContainsFlag(0x6875B9C0u);
        m_params[9] = s->ContainsFlag(0x86B52462u);
    }
}

// 0x0040A840
int CTexDictAsset::LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(data), size, copy, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    if (copy) {
        Mem::PushMemProfile("TexDict from data stream");
        data = CopyData(this, data, size);
        Mem::PopMemProfile();
    }
    VFinishLoading(data, size);
    return 0;
}

// 0x0040A720
int CTexDictAsset::Load(const char* name, bool async, bool useArchive, void* params, void* structure)
{
    StartLoading(reinterpret_cast<uint32_t>(name), async, useArchive, reinterpret_cast<uint32_t>(params),
                 reinterpret_cast<uint32_t>(structure));
    SetUpParams(params, structure);
    CopyString(reinterpret_cast<char*>(m_params + 0xA), name);
    char file[256];
    TexDictFileName(file, name);
    uint32_t size;
    if (!ReadFile(this, file, async, &size))
        return -1;
    StartReading(this, m_file, m_buffer, size, async);
    if (!async)
        FinishNow(this, size);
    return 0;
}

// 0x0040A5A0
bool CTexDictAsset::FinishLoading(void* buffer, uint32_t size)
{
    VSetData(MakeTexDict(m_checksum, buffer, size, m_params[0], *reinterpret_cast<int*>(m_params + 4), m_params[8],
                         m_params[9]));
    FreeLoadParams(m_params);
    m_params = nullptr;
    CloseFile(m_file);
    return CAsset::FinishLoading(buffer, size);
}

// 0x0040A6C0
int CTexDictAsset::Unload()
{
    if (VData()) {
        UnloadTexDict(VData());
        FreeMallocBuffer(m_buffer);
        VSetData(nullptr);
    }
    return CAsset::Unload();
}

} // namespace Ass

REPLACE(0x00407730, Ass::AllocLoadParams);
REPLACE(0x00407770, Ass::FreeLoadParams);
REPLACE(0x00408A70, &Ass::ReloadAdapter::Reload);
REPLACE(0x004072E0, &Ass::CAnimAsset::GetType);
REPLACE(0x004072D0, &Ass::CAnimAsset::GetName);
REPLACE(0x004071C0, &Ass::CAnimAsset::LoadFromBuffer);
REPLACE(0x00407300, &Ass::CAnimAsset::Load);
REPLACE(0x004074F0, &Ass::CAnimAsset::FinishLoading);
REPLACE(0x004074A0, &Ass::CAnimAsset::Unload);
REPLACE(0x00407290, &Ass::CAnimAsset::FreeBuffer);
REPLACE(0x00408AC0, &Ass::CCutsceneAsset::GetType);
REPLACE(0x00408AB0, &Ass::CCutsceneAsset::GetName);
REPLACE(0x00408AD0, &Ass::CCutsceneAsset::Load);
REPLACE(0x00408A40, &Ass::CCutsceneAsset::Unload);
REPLACE(0x00408AA0, &Ass::CCutsceneAsset::IsLoaded);
REPLACE(0x00408BE0, &Ass::CNodeArrayAsset::GetType);
REPLACE(0x00408BD0, &Ass::CNodeArrayAsset::GetName);
REPLACE(0x00408EA0, &Ass::CNodeArrayAsset::LoadFromBuffer);
REPLACE(0x00408D10, &Ass::CNodeArrayAsset::Load);
REPLACE(0x00408C10, &Ass::CNodeArrayAsset::FinishLoading);
REPLACE(0x00408F00, &Ass::CNodeArrayAsset::Unload);
REPLACE(0x004090D0, &Ass::CNodeArrayAsset::CreateNodeArray);
REPLACE(0x004093D0, &Ass::CSceneAsset::GetName);
REPLACE(0x004092D0, &Ass::CSceneAsset::SetUpParams);
REPLACE(0x00409520, &Ass::CSceneAsset::LoadFromBuffer);
REPLACE(0x004093F0, &Ass::CSceneAsset::Load);
REPLACE(0x00409240, &Ass::CSceneAsset::FinishLoading);
REPLACE(0x00409380, &Ass::CSceneAsset::Unload);
REPLACE(0x00409750, &Ass::CSceneCollisionAsset::GetName);
REPLACE(0x00409690, &Ass::CSceneCollisionAsset::SetUpParams);
REPLACE(0x00409890, &Ass::CSceneCollisionAsset::LoadFromBuffer);
REPLACE(0x00409760, &Ass::CSceneCollisionAsset::Load);
REPLACE(0x00409610, &Ass::CSceneCollisionAsset::FinishLoading);
REPLACE(0x00409700, &Ass::CSceneCollisionAsset::Unload);
REPLACE(0x00409A40, &Ass::CSkeletonAsset::GetType);
REPLACE(0x00409A30, &Ass::CSkeletonAsset::GetName);
REPLACE(0x00409990, &Ass::CSkeletonAsset::LoadFromBuffer);
REPLACE(0x00409A50, &Ass::CSkeletonAsset::Load);
REPLACE(0x00409B80, &Ass::CSkeletonAsset::FinishLoading);
REPLACE(0x004099E0, &Ass::CSkeletonAsset::Unload);
REPLACE(0x00409A10, &Ass::CSkeletonAsset::FreeBuffer);
REPLACE(0x00409E90, &Ass::SkinParams::Clear);
REPLACE(0x00409E00, &Ass::CSkinAsset::GetType);
REPLACE(0x00409DF0, &Ass::CSkinAsset::GetName);
REPLACE(0x00409E10, &Ass::CSkinAsset::SetUpParams);
REPLACE(0x0040A140, &Ass::CSkinAsset::LoadFromBuffer);
REPLACE(0x00409EB0, &Ass::CSkinAsset::Load);
REPLACE(0x00409C70, &Ass::CSkinAsset::FinishLoading);
REPLACE(0x00409D30, &Ass::CSkinAsset::Unload);
REPLACE(0x00409D70, &Ass::CSkinAsset::Abort);
REPLACE(0x00409DA0, &Ass::CSkinAsset::FreeBuffer);
REPLACE(0x00409C60, &Ass::CSkinAsset::SetRead);
REPLACE(0x0040A290, &Ass::CSkinCollisionAsset::GetType);
REPLACE(0x0040A280, &Ass::CSkinCollisionAsset::GetName);
REPLACE(0x0040A400, &Ass::CSkinCollisionAsset::LoadFromBuffer);
REPLACE(0x0040A2A0, &Ass::CSkinCollisionAsset::Load);
REPLACE(0x0040A510, &Ass::CSkinCollisionAsset::FinishLoading);
REPLACE(0x0040A230, &Ass::CSkinCollisionAsset::Unload);
REPLACE(0x0040A710, &Ass::CTexDictAsset::GetName);
REPLACE(0x0040A620, &Ass::CTexDictAsset::SetUpParams);
REPLACE(0x0040A840, &Ass::CTexDictAsset::LoadFromBuffer);
REPLACE(0x0040A720, &Ass::CTexDictAsset::Load);
REPLACE(0x0040A5A0, &Ass::CTexDictAsset::FinishLoading);
REPLACE(0x0040A6C0, &Ass::CTexDictAsset::Unload);
