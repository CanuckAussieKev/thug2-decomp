#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace File {
class AsyncFileHandle;
}

// Assets (Neversoft's Ass): files the asset manager loads and keeps by checksum - scenes, textures, collision,
// animations, skeletons, skinned models, cutscenes, node arrays. Each type is a CAsset with its own vtable; the
// manager (CAssMan, assman.h) makes them, lists them and frees them.
namespace Ass {

// The asset types (each file extension maps to one, 0x0067C650).
enum AssetType : int {
    kBinary = 1,          // .BIN
    kScene = 2,           // .SCN, .GEOM
    kTextureDictionary = 3, // .TEX
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

class CAsset;
// Called when an asset the manager loaded asynchronously is ready: (the asset, its file's size, the two values).
using LoadCallback = void(__cdecl*)(CAsset* asset, uint32_t size, void* data0, void* data1);

// The base of every asset (0x4C bytes, vtable 0x00645568). The methods are virtual in the game - each type has
// its own vtable - so the ones here that run another slot call it through the vtable; the slots are:
//   0 GetType, 1 GetData, 2 IsPermanent, 3 the scalar deleting destructor, 4 LoadFromBuffer, 5 Load, 6 Unload,
//   7 Reload, 8 IsLoaded, 9 GetName, 10 SetData, 11 SetPermanent, 12 ProcessBuffer, 13 FinishLoading, 14 Abort,
//   15 FreeBuffer, 16-21 the state setters (Loading, Read, Processing, Loaded, Unloaded, Aborted).
class CAsset {
public:
    // 0x00407910: unloaded, no file, buffer or callback, an empty dependents list (returns this).
    CAsset* Construct();
    // 0x00407810: the dependents list's head unlinked. 0x004093E0 (a jump to it): the types' destructor body.
    void Destruct();
    void DestructType();
    // 0x00407950 / 0x00407AF0: the scalar deleting destructors (base / the types', through 0x004093E0).
    CAsset* ScalarDelete(uint8_t flags);
    CAsset* TypeScalarDelete(uint8_t flags);

    // 0x004075F0: the base LoadFromBuffer and Load (slots 4 and 5): the state set to loading; 0.
    int StartLoading(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    // 0x00407600: the base Unload (slot 6): the state set to unloaded; 0.
    int Unload();
    // 0x00407610 (slot 8)
    bool IsLoaded() const;
    // 0x00407620 (slot 12): FinishLoading with the buffer and its size.
    bool ProcessBuffer();
    // 0x00407630: the base FinishLoading (slot 13): loaded; true.
    bool FinishLoading(void* buffer, uint32_t size);
    // 0x00407640 (slot 14): the file read cancelled and closed; aborted; true.
    bool Abort();
    // 0x004076D0 (slot 9)
    const char* GetName() const;
    // 0x004076E0 (slot 10), 0x004072F0 (slot 11), 0x004076F0, 0x00407700, 0x004075D0
    void SetData(void* data);
    void SetPermanent(bool permanent);
    void SetGroup(CAsset* group);
    void SetChecksum(uint32_t checksum);
    void SetCallback(LoadCallback callback, void* data0, void* data1);
    // The state setters (slots 16-21): 0x00407670 (returns 1), 0x00407680, 0x00407690, 0x004076A0, 0x004076B0,
    // 0x004076C0 (each returns true).
    int SetLoading();
    bool SetRead();
    bool SetProcessing();
    bool SetLoaded();
    bool SetUnloaded();
    bool SetAborted();

    // 0x00407840: `asset` onto the end of the dependents list (a failed allocation crashes, as in the original).
    void AddDependent(CAsset* asset);
    // 0x00407890: the first dependents-list node of `asset` deleted.
    void RemoveDependent(CAsset* asset);

    // 0x00407710: the async read's callback (the asset is the first value): its size kept, the state set to read
    // (unless the second value is set).
    static void __cdecl ReadCallback(File::AsyncFileHandle* file, int event, int size, void* asset, void* other);

    // The virtual slots, called through the vtable.
    int VType() { return Call<int>(0); }
    void* VData() { return Call<void*>(1); }
    void VDelete() { reinterpret_cast<void(__thiscall*)(CAsset*, int)>(m_vtable[3])(this, 1); }
    int VUnload() { return Call<int>(6); }
    bool VIsLoaded() { return Call<bool>(8); }
    void VSetData(void* data) { reinterpret_cast<void(__thiscall*)(CAsset*, void*)>(m_vtable[10])(this, data); }
    bool VProcessBuffer() { return Call<bool>(12); }
    bool VFinishLoading(void* buffer, uint32_t size)
    {
        return reinterpret_cast<bool(__thiscall*)(CAsset*, void*, uint32_t)>(m_vtable[13])(this, buffer, size);
    }
    bool VAbort() { return Call<bool>(14); }
    void VFreeBuffer() { Call<void>(15); }

    template <typename R>
    R Call(int slot)
    {
        return reinterpret_cast<R(__thiscall*)(CAsset*)>(m_vtable[slot])(this);
    }

    void** m_vtable;
    uint32_t m_checksum;          // +0x04
    int m_state;                  // +0x08 AssetState
    bool m_permanent;             // +0x0C not unloaded by UnloadAllAssets(false)
    uint8_t pad0D[3];
    void* m_data;                 // +0x10 what was loaded (a scene, a texture dictionary...)
    LoadCallback m_callback;      // +0x14
    void* m_callbackData0;        // +0x18
    void* m_callbackData1;        // +0x1C
    File::AsyncFileHandle* m_file; // +0x20 the file being read
    void* m_buffer;               // +0x24 its contents
    uint32_t m_size;              // +0x28 their size
    CAsset* m_prev;               // +0x2C in the manager's list (circular)
    CAsset* m_next;               // +0x30
    CAsset* m_group;              // +0x34 the asset it depends on (a model's textures)
    Mem::ListNode m_dependents;   // +0x38 the assets depending on it (Lst::Head<CAsset>, vtable 0x006455C0)
};
static_assert(sizeof(CAsset) == 0x4C, "layout must match the original");
static_assert(offsetof(CAsset, m_file) == 0x20 && offsetof(CAsset, m_dependents) == 0x38, "layout");

// The types' constructors (each the base's, then its vtable and fields; each returns the asset).
struct AssetConstructors : CAsset {
    // 0x004071A0 animation (0x50 bytes, vtable 0x006454B0; +0x4C the archive the file came from)
    CAsset* Animation();
    // 0x00409220 scene (0x50, 0x00645700; +0x4C its load parameters)
    CAsset* Scene();
    // 0x004095F0 scene collision (0x50, 0x00645778; +0x4C its load parameters)
    CAsset* SceneCollision();
    // 0x00409970 skeleton (0x4C, 0x00645810)
    CAsset* Skeleton();
    // 0x00409C40 skinned model (0x60, 0x00645888; +0x50-0x5C cleared)
    CAsset* SkinnedModel();
    // 0x0040A210 skin collision (0x50, 0x00645928)
    CAsset* SkinCollision();
    // 0x0040A580 texture dictionary (0x50, 0x006459A8)
    CAsset* TextureDictionary();
    // 0x00407A30 cutscene (0x4C, 0x00645600)
    CAsset* Cutscene();
    // 0x00408BB0 node array (0x64, 0x00645698)
    CAsset* NodeArray();
    // 0x00408BF0: a node array made the node manager's owner (+0x60 set).
    void MakeNodeArrayOwner();
};

} // namespace Ass
