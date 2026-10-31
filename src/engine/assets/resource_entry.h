#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace FileLoad {
class AsyncFileHandle;
}

// Assets (Neversoft's Ass): files the asset manager loads and keeps by checksum - scenes, textures, collision,
// animations, skeletons, skinned models, cutscenes, node arrays. Each type is a CResource with its own vtable; the
// manager (CResourceRegistry, resource_registry.h) makes them, lists them and frees them.
namespace AssetStore {

// The asset types (each file extension maps to one, 0x0067C650).
enum ResourceKind : int {
    kBinary = 1,          // .BIN
    kScene = 2,           // .SCN, .GEOM
    kResTexDict = 3, // .TEX
    kSceneCollision = 4,  // .COL
    kAnimation = 5,       // .SKA, .FAM
    kSkeleton = 6,        // .SKE
    kSkinnedModel = 7,    // .MDL, .SKIN
    kSkinCollision = 8,
    kCutscene = 9,        // .CUT
    kNodeArray = 10,      // .QB
    kOwnedNodeArray = 11, // a node array that the node manager's owner (made by LoadAsset with this type)
};

// The load states (+0x08).
enum AssetState : int {
    kUnloaded = 0,
    kLoading = 1,   // the file is being read
    kRead = 2,      // the file is read (the async read's callback)
    kProcessing = 3,
    kLoaded = 4,
    kAborted = 5,
};

class CResource;
// Called when an asset the manager loaded asynchronously is ready: (the asset, its file's size, the two values).
using LoadCallback = void(__cdecl*)(CResource* asset, uint32_t size, void* firstWord, void* data1);

// The base of every asset (0x4C bytes, vtable 0x00645568). The methods are virtual in the game - each type has
// its own vtable - so the ones here that run another slot call it through the vtable; the slots are:
//   0 FetchKind, 1 GetData, 2 IsPermanent, 3 the scalar deleting destructor, 4 LoadFromBuffer, 5 Load, 6 Unload,
//   7 Reload, 8 IsLoaded, 9 FetchTitle, 10 ChangeContents, 11 PinResident, 12 ProcessBuffer, 13 FinishLoading, 14 Abort,
//   15 FreeBuffer, 16-21 the state setters (Loading, Read, Processing, Loaded, Unloaded, Aborted).
class CResource {
public:
    // 0x00407910: unloaded, no file, buffer or callback, an empty dependents list (returns this).
    CResource* MakeNew();
    // 0x00407810: the dependents list's head unlinked. 0x004093E0 (a jump to it): the types' destructor body.
    void Destruct();
    void DestructType();
    // 0x00407950 / 0x00407AF0: the scalar deleting destructors (base / the types', through 0x004093E0).
    CResource* ScalarDelete(uint8_t flags);
    CResource* TypeScalarDelete(uint8_t flags);

    // 0x004075F0: the base LoadFromBuffer and Load (slots 4 and 5): the state set to loading; 0.
    int StartLoading(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    // 0x00407600: the base Unload (slot 6): the state set to unloaded; 0.
    int Offload();
    // 0x00407610 (slot 8)
    bool IsLoaded() const;
    // 0x00407620 (slot 12): FinishLoading with the buffer and its size.
    bool ProcessBuffer();
    // 0x00407630: the base FinishLoading (slot 13): loaded; true.
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x00407640 (slot 14): the file read cancelled and closed; aborted; true.
    bool CancelNow();
    // 0x004076D0 (slot 9)
    const char* FetchTitle() const;
    // 0x004076E0 (slot 10), 0x004072F0 (slot 11), 0x004076F0, 0x00407700, 0x004075D0
    void ChangeContents(void* data);
    void PinResident(bool permanent);
    void ChangeCluster(CResource* group);
    void SetChecksum(uint32_t checksum);
    void ChangeHook(LoadCallback callback, void* firstWord, void* data1);
    // The state setters (slots 16-21): 0x00407670 (returns 1), 0x00407680, 0x00407690, 0x004076A0, 0x004076B0,
    // 0x004076C0 (each returns true).
    int SetLoading();
    bool SetRead();
    bool SetProcessing();
    bool SetLoaded();
    bool SetUnloaded();
    bool SetAborted();

    // 0x00407840: `asset` onto the end of the dependents list (a failed allocation crashes, as in the original).
    void AddDependent(CResource* asset);
    // 0x00407890: the first dependents-list node of `asset` deleted.
    void RemoveDependent(CResource* asset);

    // 0x00407710: the async read's callback (the asset is the first value): its size kept, the state set to read
    // (unless the second value is set).
    static void __cdecl ReadCallback(FileLoad::AsyncFileHandle* file, int event, int size, void* asset, void* other);

    // The virtual slots, called through the vtable.
    int VType() { return Call<int>(0); }
    void* VData() { return Call<void*>(1); }
    void VDelete() { reinterpret_cast<void(__thiscall*)(CResource*, int)>(m_vtable[3])(this, 1); }
    int VUnload() { return Call<int>(6); }
    bool VIsLoaded() { return Call<bool>(8); }
    void VSetData(void* data) { reinterpret_cast<void(__thiscall*)(CResource*, void*)>(m_vtable[10])(this, data); }
    bool VProcessBuffer() { return Call<bool>(12); }
    bool VFinishLoading(void* buffer, uint32_t size)
    {
        return reinterpret_cast<bool(__thiscall*)(CResource*, void*, uint32_t)>(m_vtable[13])(this, buffer, size);
    }
    bool VAbort() { return Call<bool>(14); }
    void VFreeBuffer() { Call<void>(15); }

    template <typename R>
    R Call(int slot)
    {
        return reinterpret_cast<R(__thiscall*)(CResource*)>(m_vtable[slot])(this);
    }

    void** m_vtable;
    uint32_t m_checksum;          // +0x04
    int m_curPhase;                  // +0x08 AssetState
    bool m_pinned;             // +0x0C not unloaded by UnloadAllAssets(false)
    uint8_t pad0D[3];
    void* m_content;                 // +0x10 what was loaded (a scene, a texture dictionary...)
    LoadCallback m_callback;      // +0x14
    void* m_callbackData0;        // +0x18
    void* m_callbackData1;        // +0x1C
    FileLoad::AsyncFileHandle* m_fileRef; // +0x20 the file being read
    void* m_octets;               // +0x24 its contents
    uint32_t m_extent;              // +0x28 their size
    CResource* m_lastValue;               // +0x2C in the manager's list (circular)
    CResource* m_succ;               // +0x30
    CResource* m_bundle;              // +0x34 the asset it depends on (a model's textures)
    MemSys::ListNode m_dependents;   // +0x38 the assets depending on it (ListKit::Foremost<CResource>, vtable 0x006455C0)
};
static_assert(sizeof(CResource) == 0x4C, "layout must match the original");
static_assert(offsetof(CResource, m_fileRef) == 0x20 && offsetof(CResource, m_dependents) == 0x38, "layout");

// The types' constructors (each the base's, then its vtable and fields; each returns the asset).
struct AssetConstructors : CResource {
    // 0x004071A0 animation (0x50 bytes, vtable 0x006454B0; +0x4C the archive the file came from)
    CResource* Animation();
    // 0x00409220 scene (0x50, 0x00645700; +0x4C its load parameters)
    CResource* Scene();
    // 0x004095F0 scene collision (0x50, 0x00645778; +0x4C its load parameters)
    CResource* SceneCollision();
    // 0x00409970 skeleton (0x4C, 0x00645810)
    CResource* Skeleton();
    // 0x00409C40 skinned model (0x60, 0x00645888; +0x50-0x5C cleared)
    CResource* SkinnedModel();
    // 0x0040A210 skin collision (0x50, 0x00645928)
    CResource* SkinCollision();
    // 0x0040A580 texture dictionary (0x50, 0x006459A8)
    CResource* TextureBank();
    // 0x00407A30 cutscene (0x4C, 0x00645600)
    CResource* Cutscene();
    // 0x00408BB0 node array (0x64, 0x00645698)
    CResource* NodeArray();
    // 0x00408BF0: a node array made the node manager's owner (+0x60 set).
    void MakeNodeArrayOwner();
};

// The load parameters' pool: 4 blocks of 0x60 bytes (0x00691758, in use: the bits of 0x006918D8). 0x00407730: a
// free block (the size is ignored; null if all four are in use). 0x00407770: one given back.
void* AllocLoadParams(uint32_t size);
void FreeLoadParams(void* params);

// The types' own methods (the slots their vtables override, and their helpers). Each Load is (name, async,
// useArchive, params, structure) and LoadFromBuffer (data, size, copy, params, structure): `params` a copy of the
// type's parameters to start from (or null), `structure` a script structure to read them from (or null). Both
// return 0, or -1 if the file couldn't be opened.

// Animation (+0x4C the checksum of the archive file it came from).
struct CAnimResource : CResource {
    uint32_t m_archive;
    // 0x004072E0 / 0x004072D0
    int FetchKind() const;
    const char* FetchTitle() const;
    // 0x004071C0 / 0x00407300 (an archive file: through Pip, the lowercased name's checksum kept)
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x004074F0: the frames made from the buffer (null if that fails).
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x004074A0: the frames deleted, the buffer freed.
    int Offload();
    // 0x00407290 (also the node arrays'): the archive file unloaded, or the buffer freed.
    void FreeBuffer();
};
static_assert(sizeof(CAnimResource) == 0x50, "layout");

// 0x00408A70: the types' Reload (the animation's, the cutscene's, the node array's): unloaded, then ReadIn(name);
// whether that worked.
bool RefreshResource(CResource* asset, const char* name);
struct ReloadAdapter : CResource {
    bool Reload(const char* name) { return RefreshResource(this, name); }
};

// Cutscene (the data a CCinematicTable).
struct CCutsceneResource : CResource {
    // 0x00408AC0 / 0x00408AB0
    int FetchKind() const;
    const char* FetchTitle() const;
    // 0x00408AD0: the cutscene data made and loaded from "<name>.<platform extension>" (the data leaks if that
    // fails, as in the original).
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x00408A40: the cutscene data deleted.
    int Offload();
    // 0x00408AA0: its cutscene data's.
    bool IsLoaded();
};

// Node array (a .qb file: its nodes, added to the node manager).
struct CNodeTableResource : CResource {
    uint32_t m_archive;    // +0x4C
    uint32_t m_sourcePath;   // +0x50 the checksum of the file name (its script's)
    uint32_t m_arrayName;  // +0x54 the node array's checksum
    void* m_nodes;         // +0x58 its script array
    char* m_args;        // +0x5C (0x40 bytes, the file name)
    bool m_owned;          // +0x60 the node manager's owner
    uint8_t pad61[3];
    // 0x00408BE0: kNodeArray, or kOwnedNodeArray when owned; 0x00408BD0
    int FetchKind() const;
    const char* FetchTitle() const;
    // 0x00408EA0 / 0x00408D10 (the file read on the top-down heap)
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x00408C10: the script parsed, the node array added to the node manager.
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x00408F00: each node's object told (slot 6), the node array removed, the script unloaded.
    int Offload();
    // 0x004090D0: a new array of `count` empty structures made the global symbol `name` (any old one removed)
    // and the node array's (returns it).
    void* BuildNodeTable(uint32_t name, int count);
};
static_assert(sizeof(CNodeTableResource) == 0x64, "layout");

// Scene (+0x4C its load parameters: +0x00 the textures' checksum, +0x04-0x07 four flags, +0x08 the name).
struct CSceneAsset : CResource {
    uint8_t* m_args;
    // 0x004093D0
    const char* FetchTitle() const;
    // 0x004092D0: the parameters from the pool (from `params`, then `structure`'s values).
    void SetUpParams(void* params, void* structure);
    // 0x00409520 / 0x004093F0
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x00409240: the scene made, with its group's (the textures') data.
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x00409380
    int Offload();
};

// Scene collision (+0x4C: +0x00 a checksum, +0x04 a flag, +0x05 the name).
struct CSceneCollisionAsset : CResource {
    uint8_t* m_args;
    // 0x00409750
    const char* FetchTitle() const;
    // 0x00409690
    void SetUpParams(void* params, void* structure);
    // 0x00409890 / 0x00409760
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x00409610: the collision added to its group's (the scene's) data.
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x00409700
    int Offload();
};

// Skeleton.
struct CRigResource : CResource {
    // 0x00409A40 / 0x00409A30
    int FetchKind() const;
    const char* FetchTitle() const;
    // 0x00409990 / 0x00409A50 (the buffer from the top-down heap)
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x00409B80: the skeleton data made (false and nothing kept if that fails).
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x004099E0 / 0x00409A10
    int Offload();
    void FreeBuffer();
};

// The skinned model's load parameters (0x14 bytes from the pool): 0x00409E90 clears them (returns them).
struct SkinParams {
    uint8_t flag0;      // +0x00
    uint8_t pad01[3];
    int value;          // +0x04
    void* casData;      // +0x08 the .cas file's contents
    uint32_t unknown0C;
    uint8_t hasCas;     // +0x10
    uint8_t onHeap;     // +0x11 (cleared by Load; a pop of the heap context when set)
    uint8_t pad12[2];
    SkinParams* Clear();
};

// Skinned model (+0x4C archive, +0x50 parameters, +0x54 / +0x58 the .cas file and its contents, +0x5C the files
// still being read).
struct CSkinResource : CResource {
    uint32_t m_archive;
    SkinParams* m_args;
    FileLoad::AsyncFileHandle* m_casFile;
    void* m_casBuffer;
    int m_filesReading;
    // 0x00409E00 / 0x00409DF0
    int FetchKind() const;
    const char* FetchTitle() const;
    // 0x00409E10
    void SetUpParams(void* params, void* structure);
    // 0x0040A140 / 0x00409EB0 (the model file "<base>.skin|mdl.<ext>", and the base's .cas file if it has one)
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x00409C70: the model made (with its group's textures).
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x00409D30 / 0x00409D70 / 0x00409DA0
    int Offload();
    bool CancelNow();
    void FreeBuffer();
    // 0x00409C60: read only when both files are.
    bool SetRead();
};
static_assert(sizeof(CSkinResource) == 0x60, "layout");

// Skin collision (+0x4C: +0x00 a checksum, +0x04 the name).
struct CSkinCollisionAsset : CResource {
    uint8_t* m_args;
    // 0x0040A290 / 0x0040A280
    int FetchKind() const;
    const char* FetchTitle() const;
    // 0x0040A400 / 0x0040A2A0
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x0040A510: the collision added to its group's (the model's) data.
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x0040A230
    int Offload();
};

// Texture dictionary (+0x4C: +0x00 a flag (set), +0x04 a value, +0x08 / +0x09 flags, +0x0A the name).
struct CTexDictAsset : CResource {
    uint8_t* m_args;
    // 0x0040A710
    const char* FetchTitle() const;
    // 0x0040A620
    void SetUpParams(void* params, void* structure);
    // 0x0040A840 / 0x0040A720
    int LoadFromBuffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    int ReadIn(const char* name, bool async, bool useArchive, void* params, void* structure);
    // 0x0040A5A0
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x0040A6C0
    int Offload();
};

} // namespace Ass
