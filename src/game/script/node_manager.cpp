// SkateScript::CNodeManager: the level's node arrays and the hash from node names to nodes (node_manager.h).

#include "game/script/node_manager.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/memory_sizes.h"
#include "game/obj/path_manager.h"
#include "game/script/node_prefixes.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace SkateScript {

using Script::CArray;
using Script::CStruct;

GAME_GLOBAL(uint32_t, g_nodeManagerReferences, 0x006F0D88);

namespace {

constexpr uint32_t kName = 0xA1DC81F9;     // name
constexpr uint32_t kLinks = 0x2E7D5EE7;    // links
constexpr uint32_t kPosition = 0xB9D31B0A; // position
constexpr uint32_t kPos = 0x7F261953;      // pos

CNodeManager*& Instance() { return GameVariable<CNodeManager*>(0x006F0D84); }
uint32_t& References() { return g_nodeManagerReferences; }

// NOT ORIGINAL - a node asked for that must exist (the release build's assertion did nothing) and doesn't: logged, the
// first time for each name. Many callers then use the null as a node and crash (e.g. IsAlive's 0x005A3D10 -> a crash
// in CStruct::GetChecksum: a converted level's script naming a node its NodeArray hasn't).
void LogMissingNode(uint32_t name)
{
    static uint32_t logged[32];
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (logged[i] == name)
            return;
    if (count < 32)
        logged[count++] = name;
    const char* text = Script::FindChecksumName(name);
    Log("nodes: no node named 0x%08X%s%s%s in the level's node arrays - the caller expected one (it may crash on it)",
        name, text ? " (" : "", text ? text : "", text ? ")" : "");
}

constexpr uint32_t kClass = 0x12B4E660;    // class
constexpr uint32_t kLink = 0xC953660E;     // link (a class= whose nodes aren't hashed)

// NOT ORIGINAL - the level editor's overlay array (see node_manager.h).
uint32_t g_editorArray;
NodeArrayRemovedHook g_removedHook;
NodeArrayAddedHook g_addedHook;

// NOT ORIGINAL - the name hash, bigger than the original's (Mem::Sizes::NodeHashSlots): the game's node manager (the one
// CreateNodeManager made) keeps its slots here, node indices in 32 bits (a node array may hold more than 32,767), its
// walk too; its own m_slots / m_walk are left unused. Any other manager (the self-tests' own) uses its m_slots as the
// original does.
struct BigSlot {
    uint32_t m_array;
    int32_t m_node; // -1: empty
};
std::vector<BigSlot> g_bigSlots;
BigSlot g_bigWalk = {0, -1};
const CNodeManager* g_bigOwner;

bool IsBig(const CNodeManager* m) { return m == g_bigOwner && !g_bigSlots.empty(); }

// The slots of a manager, whichever kind.
class Slots {
public:
    explicit Slots(CNodeManager* m)
        : m_small(m->m_slots), m_big(IsBig(m) ? g_bigSlots.data() : nullptr),
          m_mask(m_big ? static_cast<uint32_t>(g_bigSlots.size()) - 1 : CNodeManager::kSlots - 1)
    {
    }
    uint32_t Mask() const { return m_mask; }
    bool Empty(uint32_t i) const { return Node(i) == -1; }
    int Node(uint32_t i) const { return m_big ? m_big[i].m_node : m_small[i].m_node; }
    uint32_t Array(uint32_t i) const { return m_big ? m_big[i].m_array : m_small[i].m_array; }
    void Set(uint32_t i, uint32_t array, int node)
    {
        if (m_big)
            m_big[i] = {array, node};
        else
            m_small[i] = {static_cast<uint16_t>(array), static_cast<int16_t>(node)};
    }
    void Clear(uint32_t i) { Set(i, Array(i), -1); }

private:
    NodeSlot* m_small;
    BigSlot* m_big;
    uint32_t m_mask;
};

// The walk's place, whichever kind (as the slots).
struct Walk {
    explicit Walk(CNodeManager* m) : big(IsBig(m)), small(m->m_walk) {}
    int Node() const { return big ? g_bigWalk.m_node : small.m_node; }
    uint32_t Array() const { return big ? g_bigWalk.m_array : small.m_array; }
    void Set(uint32_t array, int node)
    {
        if (big)
            g_bigWalk = {array, node};
        else
            small = {static_cast<uint16_t>(array), static_cast<int16_t>(node)};
    }
    bool big;
    NodeSlot& small;
};

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
    Slots slots(this);
    uint32_t slot = name & slots.Mask();
    while (!slots.Empty(slot)) {
        slot = (slot + 1) & slots.Mask();
        // NOT ORIGINAL: a full hash - the original went round it for ever (a level of more named nodes than slots) -
        // logged, the node left out.
        if (slot == (name & slots.Mask())) {
            static bool logged;
            if (!logged)
                Log("nodes: the node name hash is full (%u slots - [memory] node_hash_slots): node %u of array %u "
                    "and later ones not hashed",
                    slots.Mask() + 1, index, array);
            logged = true;
            return;
        }
    }
    slots.Set(slot, array, static_cast<int>(index));
}

// 0x0045CEF0
void CNodeManager::UnhashNodeArray(uint32_t array)
{
    Slots slots(this);
    for (uint32_t i = 0; i <= slots.Mask(); ++i) {
        if (!slots.Empty(i) && slots.Array(i) == array)
            slots.Clear(i);
    }
    // (A node may land in a later slot and be met - and moved - again.)
    for (uint32_t i = 0; i <= slots.Mask(); ++i) {
        if (slots.Empty(i))
            continue;
        uint32_t movedArray = slots.Array(i);
        int movedNode = slots.Node(i);
        slots.Clear(i);
        auto* node = reinterpret_cast<CStruct*>(m_arrays[movedArray].mp_nodes->GetElement(movedNode));
        uint32_t name = 0;
        node->GetChecksum(kName, &name, 1);
        uint32_t to = name & slots.Mask();
        while (!slots.Empty(to))
            to = (to + 1) & slots.Mask();
        slots.Set(to, movedArray, movedNode);
    }
}

// 0x0045CFC0
CStruct* CNodeManager::GetNode(uint32_t name, bool assert)
{
    Slots slots(this);
    // (NOT ORIGINAL: at most every slot once - a full hash without the name went round for ever)
    uint32_t tries = slots.Mask() + 1;
    for (uint32_t slot = name & slots.Mask(); tries-- && !slots.Empty(slot); slot = (slot + 1) & slots.Mask()) {
        auto* node = reinterpret_cast<CStruct*>(m_arrays[slots.Array(slot)].mp_nodes->GetElement(slots.Node(slot)));
        uint32_t nodeName = 0;
        node->GetChecksum(kName, &nodeName, 1);
        if (nodeName == name)
            return node;
    }
    if (assert)
        LogMissingNode(name);
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
    Walk walk(this);
    walk.Set(0, 0);
    if (!name)
        return;
    Slots slots(this);
    uint32_t slot = name & slots.Mask();
    for (;;) {
        CStruct* node = reinterpret_cast<CStruct*>(m_arrays[slots.Array(slot)].mp_nodes->GetElement(slots.Node(slot)));
        uint32_t nodeName = 0;
        node->GetChecksum(kName, &nodeName, 1);
        if (nodeName == name)
            break;
        slot = (slot + 1) & slots.Mask();
    }
    walk.Set(slots.Array(slot), slots.Node(slot));
}

// 0x0045D110
void CNodeManager::EndWalk()
{
    Walk walk(this);
    walk.Set(walk.Array(), -1);
}

// 0x0045D3F0
CStruct* CNodeManager::NextNode()
{
    Walk walk(this);
    if (walk.Node() == -1)
        return nullptr;
    uint32_t array = walk.Array();
    CArray* nodes = m_arrays[array].mp_nodes;
    auto* node = reinterpret_cast<CStruct*>(nodes->GetElement(walk.Node()));
    int next = walk.Node() + 1;
    if (!walk.big)
        next = static_cast<int16_t>(next); // (the original's 16-bit count)
    if (next >= static_cast<int>(nodes->m_size)) {
        ++array;
        next = 0;
        if (array >= m_count)
            next = -1;
    }
    walk.Set(array, next);
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
        Obj::CPathManager::Instance()->NumberPaths();
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
        Slots slots(this);
        for (uint32_t i = 0; i <= slots.Mask(); ++i) {
            if (slots.Array(i) == m_count)
                slots.Set(i, index, slots.Node(i));
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

// NOT ORIGINAL (see BigSlot)
void UseBigNameHash(CNodeManager* manager, uint32_t slots)
{
    g_bigOwner = manager;
    std::vector<BigSlot>().swap(g_bigSlots);
    if (manager && slots)
        g_bigSlots.assign(slots, BigSlot{0, -1});
    g_bigWalk = {0, -1};
}

// 0x0045D690
CNodeManager* CreateNodeManager(bool create)
{
    if (!Instance() && create) {
        void* memory = Mem::Calloc(sizeof(CNodeManager), nullptr, 1);
        Instance() = memory ? static_cast<CNodeManager*>(memory)->Construct() : nullptr;
        // NOT ORIGINAL: its name hash bigger than the original's (Mem::Sizes::NodeHashSlots - see BigSlot).
        if (Instance() && Mem::Sizes::NodeHashSlots() > static_cast<uint32_t>(CNodeManager::kSlots))
            UseBigNameHash(Instance(), Mem::Sizes::NodeHashSlots());
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
    // NOT ORIGINAL: its bigger name hash goes with it.
    if (Instance() == g_bigOwner)
        UseBigNameHash(nullptr, 0);
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
