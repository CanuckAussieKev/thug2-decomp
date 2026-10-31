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

namespace Ass {

namespace {

extern void** const kAssetVtable;
extern void** const kDependentNodeVtable; // Lst::Node<CAsset>
extern void** const kDependentHeadVtable; // Lst::Head<CAsset>

// The destructors behind the dependents list's vtables.
struct DependentNode : Mem::ListNode {
    // 0x004077B0
    void DestroyBody() { UnlinkAs(kDependentNodeVtable); }
    // 0x004077D0 (scalar deleting; the body inlined)
    DependentNode* Destroy(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
    // 0x004078E0: the head's scalar deleting destructor (the node's body).
    DependentNode* DestroyHead(uint8_t flags)
    {
        DestroyBody();
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
};

static VtableSlot kAssetVtable_slots[] = {Stubs::ReturnZero, &Ed::CPiece::GetId, &Nx::CPcTexture::GetUnknownC,
        &Ass::CAsset::ScalarDelete, &Ass::CAsset::StartLoading, &Ass::CAsset::StartLoading, &Ass::CAsset::Unload,
        Stubs::ReturnZero1, &Ass::CAsset::IsLoaded, &Ass::CAsset::GetName, &Ass::CAsset::SetData,
        &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer, &Ass::CAsset::FinishLoading, &Ass::CAsset::Abort,
        Stubs::DoNothing, &Ass::CAsset::SetLoading, &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing,
        &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kAssetVtable = reinterpret_cast<void**>(kAssetVtable_slots);
static VtableSlot kDependentNodeVtable_slots[] = {&Ass::DependentNode::Destroy};
void** const kDependentNodeVtable = reinterpret_cast<void**>(kDependentNodeVtable_slots); // Lst::Node<CAsset>
static VtableSlot kDependentHeadVtable_slots[] = {&Ass::DependentNode::DestroyHead};
void** const kDependentHeadVtable = reinterpret_cast<void**>(kDependentHeadVtable_slots); // Lst::Head<CAsset>

static VtableSlot kAnimationVtable_slots[] = {&Ass::CAnimAsset::GetType, &Ed::CPiece::GetId, &Nx::CPcTexture::GetUnknownC,
        &Ass::CAsset::TypeScalarDelete, &Ass::CAnimAsset::LoadFromBuffer, &Ass::CAnimAsset::Load,
        &Ass::CAnimAsset::Unload, &Ass::ReloadAdapter::Reload, &Ass::CAsset::IsLoaded, &Ass::CAnimAsset::GetName,
        &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer, &Ass::CAnimAsset::FinishLoading,
        &Ass::CAsset::Abort, &Ass::CAnimAsset::FreeBuffer, &Ass::CAsset::SetLoading, &Ass::CAsset::SetRead,
        &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kAnimationVtable = reinterpret_cast<void**>(kAnimationVtable_slots);
static VtableSlot kSceneVtable_slots[] = {&Nx::CParticleCounts::TwoColours, &Ed::CPiece::GetId, &Nx::CPcTexture::GetUnknownC,
        &Ass::CAsset::TypeScalarDelete, &Ass::CSceneAsset::LoadFromBuffer, &Ass::CSceneAsset::Load,
        &Ass::CSceneAsset::Unload, Stubs::ReturnZero1, &Ass::CAsset::IsLoaded, &Ass::CSceneAsset::GetName,
        &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer, &Ass::CSceneAsset::FinishLoading,
        &Ass::CAsset::Abort, Stubs::DoNothing, &Ass::CAsset::SetLoading, &Ass::CAsset::SetRead,
        &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kSceneVtable = reinterpret_cast<void**>(kSceneVtable_slots);
static VtableSlot kSceneCollisionVtable_slots[] = {&Nx::CParticleCounts::FourColours, &Ed::CPiece::GetId,
        &Nx::CPcTexture::GetUnknownC, &Ass::CAsset::TypeScalarDelete, &Ass::CSceneCollisionAsset::LoadFromBuffer,
        &Ass::CSceneCollisionAsset::Load, &Ass::CSceneCollisionAsset::Unload, Stubs::ReturnZero1, &Ass::CAsset::IsLoaded,
        &Ass::CSceneCollisionAsset::GetName, &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent,
        &Ass::CAsset::ProcessBuffer, &Ass::CSceneCollisionAsset::FinishLoading, &Ass::CAsset::Abort, Stubs::DoNothing,
        &Ass::CAsset::SetLoading, &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded,
        &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kSceneCollisionVtable = reinterpret_cast<void**>(kSceneCollisionVtable_slots);
static VtableSlot kSkeletonVtable_slots[] = {&Ass::CSkeletonAsset::GetType, &Ed::CPiece::GetId, &Nx::CPcTexture::GetUnknownC,
        &Ass::CAsset::TypeScalarDelete, &Ass::CSkeletonAsset::LoadFromBuffer, &Ass::CSkeletonAsset::Load,
        &Ass::CSkeletonAsset::Unload, Stubs::ReturnZero1, &Ass::CAsset::IsLoaded, &Ass::CSkeletonAsset::GetName,
        &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer,
        &Ass::CSkeletonAsset::FinishLoading, &Ass::CAsset::Abort, &Ass::CSkeletonAsset::FreeBuffer,
        &Ass::CAsset::SetLoading, &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded,
        &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kSkeletonVtable = reinterpret_cast<void**>(kSkeletonVtable_slots);
static VtableSlot kSkinnedModelVtable_slots[] = {&Ass::CSkinAsset::GetType, &Ed::CPiece::GetId, &Nx::CPcTexture::GetUnknownC,
        &Ass::CAsset::TypeScalarDelete, &Ass::CSkinAsset::LoadFromBuffer, &Ass::CSkinAsset::Load,
        &Ass::CSkinAsset::Unload, Stubs::ReturnZero1, &Ass::CAsset::IsLoaded, &Ass::CSkinAsset::GetName,
        &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer, &Ass::CSkinAsset::FinishLoading,
        &Ass::CSkinAsset::Abort, &Ass::CSkinAsset::FreeBuffer, &Ass::CAsset::SetLoading, &Ass::CSkinAsset::SetRead,
        &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kSkinnedModelVtable = reinterpret_cast<void**>(kSkinnedModelVtable_slots);
static VtableSlot kSkinCollisionVtable_slots[] = {&Ass::CSkinCollisionAsset::GetType, &Ed::CPiece::GetId,
        &Nx::CPcTexture::GetUnknownC, &Ass::CAsset::TypeScalarDelete, &Ass::CSkinCollisionAsset::LoadFromBuffer,
        &Ass::CSkinCollisionAsset::Load, &Ass::CSkinCollisionAsset::Unload, Stubs::ReturnZero1, &Ass::CAsset::IsLoaded,
        &Ass::CSkinCollisionAsset::GetName, &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent,
        &Ass::CAsset::ProcessBuffer, &Ass::CSkinCollisionAsset::FinishLoading, &Ass::CAsset::Abort, Stubs::DoNothing,
        &Ass::CAsset::SetLoading, &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded,
        &Ass::CAsset::SetUnloaded, &Ass::CAsset::SetAborted};
void** const kSkinCollisionVtable = reinterpret_cast<void**>(kSkinCollisionVtable_slots);
static VtableSlot kTextureDictionaryVtable_slots[] = {&Nx::CParticleCounts::ThreeColours, &Ed::CPiece::GetId,
        &Nx::CPcTexture::GetUnknownC, &Ass::CAsset::TypeScalarDelete, &Ass::CTexDictAsset::LoadFromBuffer,
        &Ass::CTexDictAsset::Load, &Ass::CTexDictAsset::Unload, Stubs::ReturnZero1, &Ass::CAsset::IsLoaded,
        &Ass::CTexDictAsset::GetName, &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer,
        &Ass::CTexDictAsset::FinishLoading, &Ass::CAsset::Abort, Stubs::DoNothing, &Ass::CAsset::SetLoading,
        &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded,
        &Ass::CAsset::SetAborted};
void** const kTextureDictionaryVtable = reinterpret_cast<void**>(kTextureDictionaryVtable_slots);
static VtableSlot kCutsceneVtable_slots[] = {&Ass::CCutsceneAsset::GetType, &Ed::CPiece::GetId, &Nx::CPcTexture::GetUnknownC,
        &Ass::CAsset::TypeScalarDelete, &Ass::CAsset::StartLoading, &Ass::CCutsceneAsset::Load,
        &Ass::CCutsceneAsset::Unload, &Ass::ReloadAdapter::Reload, &Ass::CCutsceneAsset::IsLoaded,
        &Ass::CCutsceneAsset::GetName, &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer,
        &Ass::CAsset::FinishLoading, &Ass::CAsset::Abort, Stubs::DoNothing, &Ass::CAsset::SetLoading,
        &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded,
        &Ass::CAsset::SetAborted};
void** const kCutsceneVtable = reinterpret_cast<void**>(kCutsceneVtable_slots);
static VtableSlot kNodeArrayVtable_slots[] = {&Ass::CNodeArrayAsset::GetType, &Ed::CPiece::GetId,
        &Nx::CPcTexture::GetUnknownC, &Ass::CAsset::TypeScalarDelete, &Ass::CNodeArrayAsset::LoadFromBuffer,
        &Ass::CNodeArrayAsset::Load, &Ass::CNodeArrayAsset::Unload, &Ass::ReloadAdapter::Reload, &Ass::CAsset::IsLoaded,
        &Ass::CNodeArrayAsset::GetName, &Ass::CAsset::SetData, &Ass::CAsset::SetPermanent, &Ass::CAsset::ProcessBuffer,
        &Ass::CNodeArrayAsset::FinishLoading, &Ass::CAsset::Abort, &Ass::CAnimAsset::FreeBuffer, &Ass::CAsset::SetLoading,
        &Ass::CAsset::SetRead, &Ass::CAsset::SetProcessing, &Ass::CAsset::SetLoaded, &Ass::CAsset::SetUnloaded,
        &Ass::CAsset::SetAborted};
void** const kNodeArrayVtable = reinterpret_cast<void**>(kNodeArrayVtable_slots);

CAsset* ConstructType(CAsset* asset, void** vtable)
{
    asset->Construct();
    asset->m_vtable = vtable;
    return asset;
}
// The extra field most types have at +0x4C.
void ClearExtra(CAsset* asset) { *reinterpret_cast<void**>(asset + 1) = nullptr; }

} // namespace

// 0x00407910
CAsset* CAsset::Construct()
{
    m_vtable = kAssetVtable;
    m_dependents.data = reinterpret_cast<void*>(-1);
    m_dependents.priority = 0;
    m_dependents.next = &m_dependents;
    m_dependents.prev = &m_dependents;
    m_dependents.vtable = kDependentHeadVtable;
    m_group = nullptr;
    m_state = kUnloaded;
    m_file = nullptr;
    m_buffer = nullptr;
    m_size = 0;
    m_callback = nullptr;
    return this;
}

// 0x00407810
void CAsset::Destruct()
{
    m_vtable = kAssetVtable;
    m_dependents.UnlinkAs(kDependentNodeVtable);
}

// 0x004093E0 (a jump to 0x00407810, the types' destructor body)
void CAsset::DestructType()
{
    Destruct();
}

// 0x00407950
CAsset* CAsset::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00407AF0
CAsset* CAsset::TypeScalarDelete(uint8_t flags)
{
    DestructType();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004075F0
int CAsset::StartLoading(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
{
    Call<int>(16); // SetLoading
    return 0;
}

// 0x00407600
int CAsset::Unload()
{
    Call<bool>(20); // SetUnloaded
    return 0;
}

// 0x00407610
bool CAsset::IsLoaded() const
{
    return m_state == kLoaded;
}

// 0x00407620
bool CAsset::ProcessBuffer()
{
    return VFinishLoading(m_buffer, m_size);
}

// 0x00407630
bool CAsset::FinishLoading(void* /*buffer*/, uint32_t /*size*/)
{
    Call<bool>(19); // SetLoaded
    return true;
}

// 0x00407640
bool CAsset::Abort()
{
    if (m_file) {
        m_file->Cancel();
        File::AsyncClose(m_file);
        m_file = nullptr;
    }
    Call<bool>(21); // SetAborted
    return true;
}

// 0x004076D0
const char* CAsset::GetName() const
{
    return "Unnamed Asset";
}

// 0x004076E0
void CAsset::SetData(void* data)
{
    m_data = data;
}

// 0x004072F0
void CAsset::SetPermanent(bool permanent)
{
    m_permanent = permanent;
}

// 0x004076F0
void CAsset::SetGroup(CAsset* group)
{
    m_group = group;
}

// 0x00407700
void CAsset::SetChecksum(uint32_t checksum)
{
    m_checksum = checksum;
}

// 0x004075D0
void CAsset::SetCallback(LoadCallback callback, void* data0, void* data1)
{
    m_callback = callback;
    m_callbackData0 = data0;
    m_callbackData1 = data1;
}

// 0x00407670
int CAsset::SetLoading()
{
    m_state = kLoading;
    return 1;
}

// 0x00407680
bool CAsset::SetRead()
{
    m_state = kRead;
    return true;
}

// 0x00407690
bool CAsset::SetProcessing()
{
    m_state = kProcessing;
    return true;
}

// 0x004076A0
bool CAsset::SetLoaded()
{
    m_state = kLoaded;
    return true;
}

// 0x004076B0
bool CAsset::SetUnloaded()
{
    m_state = kUnloaded;
    return true;
}

// 0x004076C0
bool CAsset::SetAborted()
{
    m_state = kAborted;
    return true;
}

// 0x00407840
void CAsset::AddDependent(CAsset* asset)
{
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
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
void CAsset::RemoveDependent(CAsset* asset)
{
    Mem::ListNode* node = m_dependents.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        Mem::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
        if (node->data == asset) {
            reinterpret_cast<void(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1);
            return;
        }
        node = next;
    }
}

// 0x00407710
void __cdecl CAsset::ReadCallback(File::AsyncFileHandle* /*file*/, int /*event*/, int size, void* asset, void* other)
{
    if (other)
        return;
    auto* self = static_cast<CAsset*>(asset);
    self->m_size = static_cast<uint32_t>(size);
    self->Call<bool>(17); // SetRead
}

// 0x004071A0
CAsset* AssetConstructors::Animation()
{
    ConstructType(this, kAnimationVtable);
    ClearExtra(this);
    return this;
}

// 0x00409220
CAsset* AssetConstructors::Scene()
{
    ConstructType(this, kSceneVtable);
    ClearExtra(this);
    return this;
}

// 0x004095F0
CAsset* AssetConstructors::SceneCollision()
{
    ConstructType(this, kSceneCollisionVtable);
    ClearExtra(this);
    return this;
}

// 0x00409970
CAsset* AssetConstructors::Skeleton()
{
    return ConstructType(this, kSkeletonVtable);
}

// 0x00409C40
CAsset* AssetConstructors::SkinnedModel()
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
CAsset* AssetConstructors::SkinCollision()
{
    ConstructType(this, kSkinCollisionVtable);
    ClearExtra(this);
    return this;
}

// 0x0040A580
CAsset* AssetConstructors::TextureDictionary()
{
    ConstructType(this, kTextureDictionaryVtable);
    ClearExtra(this);
    return this;
}

// 0x00407A30
CAsset* AssetConstructors::Cutscene()
{
    return ConstructType(this, kCutsceneVtable);
}

// 0x00408BB0
CAsset* AssetConstructors::NodeArray()
{
    ConstructType(this, kNodeArrayVtable);
    ClearExtra(this);
    return this;
}

// 0x00408BF0
void AssetConstructors::MakeNodeArrayOwner()
{
    reinterpret_cast<uint8_t*>(this)[0x60] = 1;
    SkateScript::NodeManagerInstance()->SetOwner(this);
}

} // namespace Ass
