// Assets: the base class every asset type derives from, and the types' constructors.

#include <cstdint>

#include "game/ass/asset.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/script/node_manager.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/ed/park.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/particle_types.h"

namespace AssetStore {

namespace {

extern void** const kAssetVtable;
extern void** const kDependentNodeVtable; // ListKit::Node<CResource>
extern void** const kDependentHeadVtable; // ListKit::Head<CResource>

// The destructors behind the dependents list's vtables.
struct DependentNode : MemSys::ListNode {
    // 0x004077B0
    void DestroyBody() { UnlinkAs(kDependentNodeVtable); }
    // 0x004077D0 (scalar deleting; the body inlined)
    DependentNode* Destroy(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
    // 0x004078E0: the head's scalar deleting destructor (the node's body).
    DependentNode* DestroyHead(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Manager::Get()->Free(this);
        return this;
    }
};

static VtableSlot kAssetVtable_slots[] = {Stubs::ReturnZero, &ParkEditing::CParkTile::GetId, &Engine3D::CPcTexture::GetUnknownC,
        &AssetStore::CResource::ScalarDelete, &AssetStore::CResource::StartLoading, &AssetStore::CResource::StartLoading, &AssetStore::CResource::Unload,
        Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded, &AssetStore::CResource::GetName, &AssetStore::CResource::SetData,
        &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer, &AssetStore::CResource::FinishLoading, &AssetStore::CResource::Abort,
        Stubs::DoNothing, &AssetStore::CResource::SetLoading, &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing,
        &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kAssetVtable = reinterpret_cast<void**>(kAssetVtable_slots);
static VtableSlot kDependentNodeVtable_slots[] = {&AssetStore::DependentNode::Destroy};
void** const kDependentNodeVtable = reinterpret_cast<void**>(kDependentNodeVtable_slots); // ListKit::Node<CResource>
static VtableSlot kDependentHeadVtable_slots[] = {&AssetStore::DependentNode::DestroyHead};
void** const kDependentHeadVtable = reinterpret_cast<void**>(kDependentHeadVtable_slots); // ListKit::Head<CResource>

static VtableSlot kAnimationVtable_slots[] = {&AssetStore::CAnimResource::GetType, &ParkEditing::CParkTile::GetId, &Engine3D::CPcTexture::GetUnknownC,
        &AssetStore::CResource::TypeScalarDelete, &AssetStore::CAnimResource::LoadFromBuffer, &AssetStore::CAnimResource::Load,
        &AssetStore::CAnimResource::Unload, &AssetStore::ReloadAdapter::Reload, &AssetStore::CResource::IsLoaded, &AssetStore::CAnimResource::GetName,
        &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer, &AssetStore::CAnimResource::FinishLoading,
        &AssetStore::CResource::Abort, &AssetStore::CAnimResource::FreeBuffer, &AssetStore::CResource::SetLoading, &AssetStore::CResource::SetRead,
        &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kAnimationVtable = reinterpret_cast<void**>(kAnimationVtable_slots);
static VtableSlot kSceneVtable_slots[] = {&Engine3D::CParticleCounts::TwoColours, &ParkEditing::CParkTile::GetId, &Engine3D::CPcTexture::GetUnknownC,
        &AssetStore::CResource::TypeScalarDelete, &AssetStore::CSceneAsset::LoadFromBuffer, &AssetStore::CSceneAsset::Load,
        &AssetStore::CSceneAsset::Unload, Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded, &AssetStore::CSceneAsset::GetName,
        &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer, &AssetStore::CSceneAsset::FinishLoading,
        &AssetStore::CResource::Abort, Stubs::DoNothing, &AssetStore::CResource::SetLoading, &AssetStore::CResource::SetRead,
        &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kSceneVtable = reinterpret_cast<void**>(kSceneVtable_slots);
static VtableSlot kSceneCollisionVtable_slots[] = {&Engine3D::CParticleCounts::FourColours, &ParkEditing::CParkTile::GetId,
        &Engine3D::CPcTexture::GetUnknownC, &AssetStore::CResource::TypeScalarDelete, &AssetStore::CSceneCollisionAsset::LoadFromBuffer,
        &AssetStore::CSceneCollisionAsset::Load, &AssetStore::CSceneCollisionAsset::Unload, Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded,
        &AssetStore::CSceneCollisionAsset::GetName, &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident,
        &AssetStore::CResource::ProcessBuffer, &AssetStore::CSceneCollisionAsset::FinishLoading, &AssetStore::CResource::Abort, Stubs::DoNothing,
        &AssetStore::CResource::SetLoading, &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded,
        &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kSceneCollisionVtable = reinterpret_cast<void**>(kSceneCollisionVtable_slots);
static VtableSlot kSkeletonVtable_slots[] = {&AssetStore::CRigResource::GetType, &ParkEditing::CParkTile::GetId, &Engine3D::CPcTexture::GetUnknownC,
        &AssetStore::CResource::TypeScalarDelete, &AssetStore::CRigResource::LoadFromBuffer, &AssetStore::CRigResource::Load,
        &AssetStore::CRigResource::Unload, Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded, &AssetStore::CRigResource::GetName,
        &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer,
        &AssetStore::CRigResource::FinishLoading, &AssetStore::CResource::Abort, &AssetStore::CRigResource::FreeBuffer,
        &AssetStore::CResource::SetLoading, &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded,
        &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kSkeletonVtable = reinterpret_cast<void**>(kSkeletonVtable_slots);
static VtableSlot kSkinnedModelVtable_slots[] = {&AssetStore::CSkinResource::GetType, &ParkEditing::CParkTile::GetId, &Engine3D::CPcTexture::GetUnknownC,
        &AssetStore::CResource::TypeScalarDelete, &AssetStore::CSkinResource::LoadFromBuffer, &AssetStore::CSkinResource::Load,
        &AssetStore::CSkinResource::Unload, Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded, &AssetStore::CSkinResource::GetName,
        &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer, &AssetStore::CSkinResource::FinishLoading,
        &AssetStore::CSkinResource::Abort, &AssetStore::CSkinResource::FreeBuffer, &AssetStore::CResource::SetLoading, &AssetStore::CSkinResource::SetRead,
        &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kSkinnedModelVtable = reinterpret_cast<void**>(kSkinnedModelVtable_slots);
static VtableSlot kSkinCollisionVtable_slots[] = {&AssetStore::CSkinCollisionAsset::GetType, &ParkEditing::CParkTile::GetId,
        &Engine3D::CPcTexture::GetUnknownC, &AssetStore::CResource::TypeScalarDelete, &AssetStore::CSkinCollisionAsset::LoadFromBuffer,
        &AssetStore::CSkinCollisionAsset::Load, &AssetStore::CSkinCollisionAsset::Unload, Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded,
        &AssetStore::CSkinCollisionAsset::GetName, &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident,
        &AssetStore::CResource::ProcessBuffer, &AssetStore::CSkinCollisionAsset::FinishLoading, &AssetStore::CResource::Abort, Stubs::DoNothing,
        &AssetStore::CResource::SetLoading, &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded,
        &AssetStore::CResource::SetUnloaded, &AssetStore::CResource::SetAborted};
void** const kSkinCollisionVtable = reinterpret_cast<void**>(kSkinCollisionVtable_slots);
static VtableSlot kTextureDictionaryVtable_slots[] = {&Engine3D::CParticleCounts::ThreeColours, &ParkEditing::CParkTile::GetId,
        &Engine3D::CPcTexture::GetUnknownC, &AssetStore::CResource::TypeScalarDelete, &AssetStore::CTexDictAsset::LoadFromBuffer,
        &AssetStore::CTexDictAsset::Load, &AssetStore::CTexDictAsset::Unload, Stubs::ReturnZero1, &AssetStore::CResource::IsLoaded,
        &AssetStore::CTexDictAsset::GetName, &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer,
        &AssetStore::CTexDictAsset::FinishLoading, &AssetStore::CResource::Abort, Stubs::DoNothing, &AssetStore::CResource::SetLoading,
        &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded,
        &AssetStore::CResource::SetAborted};
void** const kTextureDictionaryVtable = reinterpret_cast<void**>(kTextureDictionaryVtable_slots);
static VtableSlot kCutsceneVtable_slots[] = {&AssetStore::CCutsceneResource::GetType, &ParkEditing::CParkTile::GetId, &Engine3D::CPcTexture::GetUnknownC,
        &AssetStore::CResource::TypeScalarDelete, &AssetStore::CResource::StartLoading, &AssetStore::CCutsceneResource::Load,
        &AssetStore::CCutsceneResource::Unload, &AssetStore::ReloadAdapter::Reload, &AssetStore::CCutsceneResource::IsLoaded,
        &AssetStore::CCutsceneResource::GetName, &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer,
        &AssetStore::CResource::FinishLoading, &AssetStore::CResource::Abort, Stubs::DoNothing, &AssetStore::CResource::SetLoading,
        &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded,
        &AssetStore::CResource::SetAborted};
void** const kCutsceneVtable = reinterpret_cast<void**>(kCutsceneVtable_slots);
static VtableSlot kNodeArrayVtable_slots[] = {&AssetStore::CNodeTableResource::GetType, &ParkEditing::CParkTile::GetId,
        &Engine3D::CPcTexture::GetUnknownC, &AssetStore::CResource::TypeScalarDelete, &AssetStore::CNodeTableResource::LoadFromBuffer,
        &AssetStore::CNodeTableResource::Load, &AssetStore::CNodeTableResource::Unload, &AssetStore::ReloadAdapter::Reload, &AssetStore::CResource::IsLoaded,
        &AssetStore::CNodeTableResource::GetName, &AssetStore::CResource::SetData, &AssetStore::CResource::PinResident, &AssetStore::CResource::ProcessBuffer,
        &AssetStore::CNodeTableResource::FinishLoading, &AssetStore::CResource::Abort, &AssetStore::CAnimResource::FreeBuffer, &AssetStore::CResource::SetLoading,
        &AssetStore::CResource::SetRead, &AssetStore::CResource::SetProcessing, &AssetStore::CResource::SetLoaded, &AssetStore::CResource::SetUnloaded,
        &AssetStore::CResource::SetAborted};
void** const kNodeArrayVtable = reinterpret_cast<void**>(kNodeArrayVtable_slots);

CResource* ConstructType(CResource* asset, void** vtable)
{
    asset->Construct();
    asset->m_vtable = vtable;
    return asset;
}
// The extra field most types have at +0x4C.
void ClearExtra(CResource* asset) { *reinterpret_cast<void**>(asset + 1) = nullptr; }

} // namespace

// 0x00407910
CResource* CResource::Construct()
{
    m_vtable = kAssetVtable;
    m_dependents.data = reinterpret_cast<void*>(-1);
    m_dependents.priority = 0;
    m_dependents.next = &m_dependents;
    m_dependents.prev = &m_dependents;
    m_dependents.vtable = kDependentHeadVtable;
    m_bundle = nullptr;
    m_state = kUnloaded;
    m_file = nullptr;
    m_octets = nullptr;
    m_size = 0;
    m_callback = nullptr;
    return this;
}

// 0x00407810
void CResource::Destruct()
{
    m_vtable = kAssetVtable;
    m_dependents.UnlinkAs(kDependentNodeVtable);
}

// 0x004093E0 (a jump to 0x00407810, the types' destructor body)
void CResource::DestructType()
{
    Destruct();
}

// 0x00407950
CResource* CResource::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00407AF0
CResource* CResource::TypeScalarDelete(uint8_t flags)
{
    DestructType();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004075F0
int CResource::StartLoading(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
{
    Call<int>(16); // SetLoading
    return 0;
}

// 0x00407600
int CResource::Unload()
{
    Call<bool>(20); // SetUnloaded
    return 0;
}

// 0x00407610
bool CResource::IsLoaded() const
{
    return m_state == kLoaded;
}

// 0x00407620
bool CResource::ProcessBuffer()
{
    return VFinishLoading(m_octets, m_size);
}

// 0x00407630
bool CResource::FinishLoading(void* /*buffer*/, uint32_t /*size*/)
{
    Call<bool>(19); // SetLoaded
    return true;
}

// 0x00407640
bool CResource::Abort()
{
    if (m_file) {
        m_file->Cancel();
        FileLoad::AsyncClose(m_file);
        m_file = nullptr;
    }
    Call<bool>(21); // SetAborted
    return true;
}

// 0x004076D0
const char* CResource::GetName() const
{
    return "Unnamed Asset";
}

// 0x004076E0
void CResource::SetData(void* data)
{
    m_data = data;
}

// 0x004072F0
void CResource::PinResident(bool permanent)
{
    m_pinned = permanent;
}

// 0x004076F0
void CResource::SetGroup(CResource* group)
{
    m_bundle = group;
}

// 0x00407700
void CResource::SetChecksum(uint32_t checksum)
{
    m_checksum = checksum;
}

// 0x004075D0
void CResource::SetCallback(LoadCallback callback, void* firstWord, void* data1)
{
    m_callback = callback;
    m_callbackData0 = firstWord;
    m_callbackData1 = data1;
}

// 0x00407670
int CResource::SetLoading()
{
    m_state = kLoading;
    return 1;
}

// 0x00407680
bool CResource::SetRead()
{
    m_state = kRead;
    return true;
}

// 0x00407690
bool CResource::SetProcessing()
{
    m_state = kProcessing;
    return true;
}

// 0x004076A0
bool CResource::SetLoaded()
{
    m_state = kLoaded;
    return true;
}

// 0x004076B0
bool CResource::SetUnloaded()
{
    m_state = kUnloaded;
    return true;
}

// 0x004076C0
bool CResource::SetAborted()
{
    m_state = kAborted;
    return true;
}

// 0x00407840
void CResource::AddDependent(CResource* asset)
{
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->vtable = kDependentNodeVtable;
        node->data = asset;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    node->prev = m_dependents.prev;
    node->next = &m_dependents;
    m_dependents.prev->next = node;
    m_dependents.prev = node;
}

// 0x00407890
void CResource::RemoveDependent(CResource* asset)
{
    MemSys::ListNode* node = m_dependents.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        MemSys::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
        if (node->data == asset) {
            reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, int)>(node->vtable[0])(node, 1);
            return;
        }
        node = next;
    }
}

// 0x00407710
void __cdecl CResource::ReadCallback(FileLoad::AsyncFileHandle* /*file*/, int /*event*/, int size, void* asset, void* other)
{
    if (other)
        return;
    auto* self = static_cast<CResource*>(asset);
    self->m_size = static_cast<uint32_t>(size);
    self->Call<bool>(17); // SetRead
}

// 0x004071A0
CResource* AssetConstructors::Animation()
{
    ConstructType(this, kAnimationVtable);
    ClearExtra(this);
    return this;
}

// 0x00409220
CResource* AssetConstructors::Scene()
{
    ConstructType(this, kSceneVtable);
    ClearExtra(this);
    return this;
}

// 0x004095F0
CResource* AssetConstructors::SceneCollision()
{
    ConstructType(this, kSceneCollisionVtable);
    ClearExtra(this);
    return this;
}

// 0x00409970
CResource* AssetConstructors::Skeleton()
{
    return ConstructType(this, kSkeletonVtable);
}

// 0x00409C40
CResource* AssetConstructors::SkinnedModel()
{
    Construct();
    auto* extra = reinterpret_cast<uint32_t*>(this + 1);
    extra[1] = 0; // +0x50 .. +0x5C
    extra[2] = 0;
    extra[3] = 0;
    extra[4] = 0;
    m_vtable = kSkinnedModelVtable;
    return this;
}

// 0x0040A210
CResource* AssetConstructors::SkinCollision()
{
    ConstructType(this, kSkinCollisionVtable);
    ClearExtra(this);
    return this;
}

// 0x0040A580
CResource* AssetConstructors::TextureDictionary()
{
    ConstructType(this, kTextureDictionaryVtable);
    ClearExtra(this);
    return this;
}

// 0x00407A30
CResource* AssetConstructors::Cutscene()
{
    return ConstructType(this, kCutsceneVtable);
}

// 0x00408BB0
CResource* AssetConstructors::NodeArray()
{
    ConstructType(this, kNodeArrayVtable);
    ClearExtra(this);
    return this;
}

// 0x00408BF0
void AssetConstructors::MakeNodeArrayOwner()
{
    reinterpret_cast<uint8_t*>(this)[0x60] = 1;
    LevelNodes::NodeManagerInstance()->SetOwner(this);
}

} // namespace Ass
