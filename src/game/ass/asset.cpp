// Assets: the base class every asset type derives from, and the types' constructors.

#include "game/ass/asset.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/script/node_manager.h"
#include "hook/hook.h"

namespace Ass {

namespace {

void** const kAssetVtable = reinterpret_cast<void**>(0x00645568);
void** const kDependentNodeVtable = reinterpret_cast<void**>(0x00645564); // Lst::Node<CAsset>
void** const kDependentHeadVtable = reinterpret_cast<void**>(0x006455C0); // Lst::Head<CAsset>

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

CAsset* ConstructType(CAsset* asset, uint32_t vtable)
{
    asset->Construct();
    asset->m_vtable = reinterpret_cast<void**>(vtable);
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
    ConstructType(this, 0x006454B0);
    ClearExtra(this);
    return this;
}

// 0x00409220
CAsset* AssetConstructors::Scene()
{
    ConstructType(this, 0x00645700);
    ClearExtra(this);
    return this;
}

// 0x004095F0
CAsset* AssetConstructors::SceneCollision()
{
    ConstructType(this, 0x00645778);
    ClearExtra(this);
    return this;
}

// 0x00409970
CAsset* AssetConstructors::Skeleton()
{
    return ConstructType(this, 0x00645810);
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
    m_vtable = reinterpret_cast<void**>(0x00645888);
    return this;
}

// 0x0040A210
CAsset* AssetConstructors::SkinCollision()
{
    ConstructType(this, 0x00645928);
    ClearExtra(this);
    return this;
}

// 0x0040A580
CAsset* AssetConstructors::TextureDictionary()
{
    ConstructType(this, 0x006459A8);
    ClearExtra(this);
    return this;
}

// 0x00407A30
CAsset* AssetConstructors::Cutscene()
{
    return ConstructType(this, 0x00645600);
}

// 0x00408BB0
CAsset* AssetConstructors::NodeArray()
{
    ConstructType(this, 0x00645698);
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

REPLACE(0x00407910, &Ass::CAsset::Construct);
REPLACE(0x00407810, &Ass::CAsset::Destruct);
REPLACE_SOURCE_ONLY(0x004093E0, &Ass::CAsset::DestructType); // a jump to 0x00407810
REPLACE(0x00407950, &Ass::CAsset::ScalarDelete);
REPLACE(0x00407AF0, &Ass::CAsset::TypeScalarDelete);
REPLACE(0x004075F0, &Ass::CAsset::StartLoading);
REPLACE(0x00407600, &Ass::CAsset::Unload);
REPLACE(0x00407610, &Ass::CAsset::IsLoaded);
REPLACE(0x00407620, &Ass::CAsset::ProcessBuffer);
REPLACE(0x00407630, &Ass::CAsset::FinishLoading);
REPLACE(0x00407640, &Ass::CAsset::Abort);
REPLACE(0x004076D0, &Ass::CAsset::GetName);
REPLACE(0x004076E0, &Ass::CAsset::SetData);
REPLACE(0x004072F0, &Ass::CAsset::SetPermanent);
REPLACE(0x004076F0, &Ass::CAsset::SetGroup);
REPLACE(0x00407700, &Ass::CAsset::SetChecksum);
REPLACE(0x004075D0, &Ass::CAsset::SetCallback);
REPLACE(0x00407670, &Ass::CAsset::SetLoading);
REPLACE(0x00407680, &Ass::CAsset::SetRead);
REPLACE(0x00407690, &Ass::CAsset::SetProcessing);
REPLACE(0x004076A0, &Ass::CAsset::SetLoaded);
REPLACE(0x004076B0, &Ass::CAsset::SetUnloaded);
REPLACE(0x004076C0, &Ass::CAsset::SetAborted);
REPLACE(0x00407840, &Ass::CAsset::AddDependent);
REPLACE(0x00407890, &Ass::CAsset::RemoveDependent);
REPLACE(0x00407710, Ass::CAsset::ReadCallback);
REPLACE(0x004077B0, &Ass::DependentNode::DestroyBody);
REPLACE(0x004077D0, &Ass::DependentNode::Destroy);
REPLACE(0x004078E0, &Ass::DependentNode::DestroyHead);
REPLACE(0x004071A0, &Ass::AssetConstructors::Animation);
REPLACE(0x00409220, &Ass::AssetConstructors::Scene);
REPLACE(0x004095F0, &Ass::AssetConstructors::SceneCollision);
REPLACE(0x00409970, &Ass::AssetConstructors::Skeleton);
REPLACE(0x00409C40, &Ass::AssetConstructors::SkinnedModel);
REPLACE(0x0040A210, &Ass::AssetConstructors::SkinCollision);
REPLACE(0x0040A580, &Ass::AssetConstructors::TextureDictionary);
REPLACE(0x00407A30, &Ass::AssetConstructors::Cutscene);
REPLACE(0x00408BB0, &Ass::AssetConstructors::NodeArray);
REPLACE(0x00408BF0, &Ass::AssetConstructors::MakeNodeArrayOwner);
