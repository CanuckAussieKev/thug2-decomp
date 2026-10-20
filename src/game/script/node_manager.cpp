// SkateScript::CNodeManager: the level's node arrays and the hash from node names to nodes (node_manager.h).

#include "game/script/node_manager.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/node_prefixes.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace SkateScript {

using Script::CArray;
using Script::CStruct;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;     // name
constexpr uint32_t kLinks = 0x2E7D5EE7;    // links
constexpr uint32_t kPosition = 0xB9D31B0A; // position
constexpr uint32_t kPos = 0x7F261953;      // pos

CNodeManager*& Instance() { return GameVariable<CNodeManager*>(0x006F0D84); }
uint32_t& References() { return GameVariable<uint32_t>(0x006F0D88); }

constexpr uint32_t kClass = 0x12B4E660;    // class
constexpr uint32_t kLink = 0xC953660E;     // link (a class= whose nodes aren't hashed)

// NOT ORIGINAL - the level editor's overlay array (see node_manager.h).
uint32_t g_editorArray;
NodeArrayRemovedHook g_removedHook;
NodeArrayAddedHook g_addedHook;

} // namespace

void SetEditorNodeArray(uint32_t name) { g_editorArray = name; }
uint32_t EditorNodeArray() { return g_editorArray; }
void SetNodeArrayRemovedHook(NodeArrayRemovedHook hook) { g_removedHook = hook; }
void SetNodeArrayAddedHook(NodeArrayAddedHook hook) { g_addedHook = hook; }

// 0x0045CE60
void CNodeManager::HashNode(uint32_t array, uint32_t index, CStruct* node)
{
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (nodeClass == kLink)
        return;
    uint32_t name = 0;
    if (!node->GetChecksum(kName, &name, 0))
        return;
    uint32_t slot = name & (kSlots - 1);
    while (m_slots[slot].m_node != -1)
        slot = (slot + 1) & (kSlots - 1);
    m_slots[slot].m_node = static_cast<int16_t>(index);
    m_slots[slot].m_array = static_cast<uint16_t>(array);
}

// 0x0045CEF0
void CNodeManager::UnhashNodeArray(uint32_t array)
{
    for (NodeSlot& slot : m_slots) {
        if (slot.m_node != -1 && slot.m_array == array)
            slot.m_node = -1;
    }
    // (A node may land in a later slot and be met - and moved - again.)
    for (NodeSlot& slot : m_slots) {
        if (slot.m_node == -1)
            continue;
        NodeSlot moved = slot;
        slot.m_node = -1;
        auto* node = reinterpret_cast<CStruct*>(m_arrays[moved.m_array].mp_nodes->GetElement(moved.m_node));
        uint32_t name = 0;
        node->GetChecksum(kName, &name, 1);
        uint32_t to = name & (kSlots - 1);
        while (m_slots[to].m_node != -1)
            to = (to + 1) & (kSlots - 1);
        m_slots[to] = moved;
    }
}

// 0x0045CFC0
CStruct* CNodeManager::GetNode(uint32_t name, bool /*assert*/)
{
    for (uint32_t slot = name & (kSlots - 1); m_slots[slot].m_node != -1; slot = (slot + 1) & (kSlots - 1)) {
        const NodeSlot& s = m_slots[slot];
        auto* node = reinterpret_cast<CStruct*>(m_arrays[s.m_array].mp_nodes->GetElement(s.m_node));
        uint32_t nodeName = 0;
        node->GetChecksum(kName, &nodeName, 1);
        if (nodeName == name)
            return node;
    }
    return nullptr;
}

// 0x0045D370
CNodeManager* CNodeManager::Construct()
{
    m_count = 0;
    for (NodeSlot& slot : m_slots)
        slot = {0, -1};
    m_walk.m_node = -1;
    return this;
}

// 0x0045D050
void CNodeManager::StartWalk(uint32_t name)
{
    m_walk.m_array = 0;
    m_walk.m_node = 0;
    if (!name)
        return;
    uint32_t slot = name & (kSlots - 1);
    for (;;) {
        const NodeSlot& s = m_slots[slot];
        CStruct* node = reinterpret_cast<CStruct*>(m_arrays[s.m_array].mp_nodes->GetElement(s.m_node));
        uint32_t nodeName = 0;
        node->GetChecksum(kName, &nodeName, 1);
        if (nodeName == name)
            break;
        slot = (slot + 1) & (kSlots - 1);
    }
    m_walk = m_slots[slot];
}

// 0x0045D110
void CNodeManager::EndWalk() { m_walk.m_node = -1; }

// 0x0045D3F0
CStruct* CNodeManager::NextNode()
{
    if (m_walk.m_node == -1)
        return nullptr;
    CArray* nodes = m_arrays[m_walk.m_array].mp_nodes;
    auto* node = reinterpret_cast<CStruct*>(nodes->GetElement(m_walk.m_node));
    ++m_walk.m_node;
    if (m_walk.m_node >= static_cast<int>(nodes->m_size)) {
        ++m_walk.m_array;
        m_walk.m_node = 0;
        if (m_walk.m_array >= m_count)
            m_walk.m_node = -1;
    }
    return node;
}

// 0x0045D120
CArray* CNodeManager::GetLastNodeArray()
{
    // NOT ORIGINAL: the level editor's overlay array (SetEditorNodeArray) is passed over - callers mean the level's.
    uint32_t i = m_count;
    while (i && g_editorArray && m_arrays[i - 1].m_name == g_editorArray)
        --i;
    return i ? m_arrays[i - 1].mp_nodes : nullptr;
}

// 0x0045D150
uint32_t CNodeManager::GetLastNodeArrayName()
{
    // NOT ORIGINAL: as GetLastNodeArray, the editor's overlay array passed over.
    uint32_t i = m_count;
    while (i && g_editorArray && m_arrays[i - 1].m_name == g_editorArray)
        --i;
    return i ? m_arrays[i - 1].m_name : 0;
}

// 0x0045D140
void CNodeManager::SetOwner(void* owner) { mp_owner = owner; }

// 0x0045D470
uint32_t CNodeManager::GetOwnerValue58() { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mp_owner) + 0x58); }

// 0x0045D270
bool CNodeManager::HasNodeArray(uint32_t name)
{
    for (uint32_t i = 0; i < m_count; ++i) {
        if (m_arrays[i].m_name == name)
            return true;
    }
    return false;
}

// 0x0045D170
bool CNodeManager::HashNodeIn(uint32_t name, uint32_t a, uint32_t b)
{
    for (uint32_t i = 0; i < m_count; ++i) {
        if (m_arrays[i].m_name == name) {
            HashNode(i, a, reinterpret_cast<CStruct*>(b));
            return true;
        }
    }
    return false;
}

// 0x0045D3A0
void CNodeManager::HashNodes(uint32_t index)
{
    CArray* nodes = m_arrays[index].mp_nodes;
    for (uint32_t i = 0; i < nodes->m_size; ++i)
        HashNode(index, i, reinterpret_cast<CStruct*>(nodes->GetElement(i)));
}

// 0x0045D480
void CNodeManager::ResolveLinks(CArray* nodes)
{
    uint32_t count = nodes->m_size;
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<CStruct*>(nodes->GetElement(i));
        CArray* links = nullptr;
        if (!node->GetArray(kLinks, &links, 0) || !links->m_size)
            continue;
        uint32_t linkCount = links->m_size;
        for (uint32_t j = 0; j < linkCount; ++j) {
            auto* linked = reinterpret_cast<CStruct*>(nodes->GetElement(links->GetElement(j)));
            links->SetElement(j, GetNodeName(linked));
        }
        links->SetType(Script::ESYMBOLTYPE_NAME);
    }
}

// 0x0045D520
bool CNodeManager::AddNodeArray(uint32_t name, bool flag)
{
    if (HasNodeArray(name))
        return false;
    CArray* nodes = Script::GetArray(name);
    m_arrays[m_count].m_name = name;
    m_arrays[m_count].mp_nodes = nodes;
    m_arrays[m_count].m_flag = flag;
    ++m_count;
    HashNodes(m_count - 1);
    AddNodeArrayPrefixes(name);
    int count = static_cast<int>(nodes->m_size);
    for (int i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<CStruct*>(nodes->GetElement(i));
        Script::VectorValue position = {0.0f, 0.0f, 0.0f, 1.0f};
        if (node->GetVector(kPosition, &position, 0)) {
            node->RemoveComponent(kPosition);
            node->AddVector(kPos, position.x, position.y, -position.z);
        }
    }
    ResolveLinks(nodes);
    if (flag) {
        void* object = GameFunction<void*(__cdecl*)()>(0x004694E0)();
        GameFunction<void(__thiscall*)(void*)>(0x004696C0)(object);
    }
    // NOT ORIGINAL: the level editor's edited copy (see node_manager.h).
    if (g_addedHook)
        g_addedHook(name);
    return true;
}

// 0x0045D1C0
bool CNodeManager::RemoveNodeArray(uint32_t name)
{
    RemoveNodeArrayPrefixes(name);
    uint32_t index = 0;
    while (index < m_count && m_arrays[index].m_name != name)
        ++index;
    if (index >= m_count)
        return false;
    UnhashNodeArray(index);
    --m_count;
    if (index != m_count) {
        m_arrays[index] = m_arrays[m_count];
        for (NodeSlot& slot : m_slots) {
            if (slot.m_array == m_count)
                slot.m_array = static_cast<uint16_t>(index);
        }
    }
    // NOT ORIGINAL: the level editor's overlay array leaves with the level it was added after.
    if (g_removedHook)
        g_removedHook(name);
    return true;
}

// 0x0045D670
bool CNodeManager::RemoveNodeArrayOfFile(const char* path) { return RemoveNodeArray(NodeArrayChecksum(path)); }

// 0x0045D2A0
bool MakeNodeArrayName(const char* path, char* out)
{
    const char* p = path;
    while (*p)
        ++p;
    --p;
    while (*p != '\\' && *p != '/') // (no check for the start of the string)
        --p;
    ++p;
    char* d = out;
    while ((*d++ = *p++) != '\0') {
    }
    char* extension = out;
    while (*extension && *extension != '.')
        ++extension;
    std::memcpy(extension, "_nodearray", sizeof("_nodearray")); // (the original: sprintf(extension, "_nodearray"))
    return true;
}

// 0x0045D310
uint32_t NodeArrayChecksum(const char* path)
{
    char name[256] = {};
    MakeNodeArrayName(path, name);
    return Crc::GenerateCRCFromString(name);
}

// 0x0045D690
CNodeManager* CreateNodeManager(bool create)
{
    if (!Instance() && create) {
        void* memory = Mem::Calloc(sizeof(CNodeManager), nullptr, 1);
        Instance() = memory ? static_cast<CNodeManager*>(memory)->Construct() : nullptr;
    }
    ++References();
    return Instance();
}

// 0x0045D6E0
void ReleaseNodeManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (Instance())
        Mem::Manager::Get()->Free(Instance());
    Instance() = nullptr;
    References() = 0;
}

} // namespace SkateScript

REPLACE(0x0045CE60, &SkateScript::CNodeManager::HashNode);
REPLACE(0x0045CEF0, &SkateScript::CNodeManager::UnhashNodeArray);
REPLACE(0x0045CFC0, &SkateScript::CNodeManager::GetNode);
REPLACE(0x0045D370, &SkateScript::CNodeManager::Construct);
REPLACE(0x0045D050, &SkateScript::CNodeManager::StartWalk);
REPLACE(0x0045D110, &SkateScript::CNodeManager::EndWalk);
REPLACE(0x0045D3F0, &SkateScript::CNodeManager::NextNode);
REPLACE(0x0045D120, &SkateScript::CNodeManager::GetLastNodeArray);
REPLACE(0x0045D150, &SkateScript::CNodeManager::GetLastNodeArrayName);
REPLACE(0x0045D140, &SkateScript::CNodeManager::SetOwner);
REPLACE(0x0045D470, &SkateScript::CNodeManager::GetOwnerValue58);
REPLACE(0x0045D270, &SkateScript::CNodeManager::HasNodeArray);
REPLACE(0x0045D170, &SkateScript::CNodeManager::HashNodeIn);
REPLACE(0x0045D3A0, &SkateScript::CNodeManager::HashNodes);
REPLACE(0x0045D480, &SkateScript::CNodeManager::ResolveLinks);
REPLACE(0x0045D520, &SkateScript::CNodeManager::AddNodeArray);
REPLACE(0x0045D1C0, &SkateScript::CNodeManager::RemoveNodeArray);
REPLACE(0x0045D670, &SkateScript::CNodeManager::RemoveNodeArrayOfFile);
REPLACE(0x0045D2A0, SkateScript::MakeNodeArrayName);
REPLACE(0x0045D310, SkateScript::NodeArrayChecksum);
REPLACE(0x0045D690, SkateScript::CreateNodeManager);
REPLACE(0x0045D6E0, SkateScript::ReleaseNodeManager);
