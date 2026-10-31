// LevelNodes::CNodeManager: the level's node arrays and the hash from node names to nodes (node_manager.h).

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
#include "game/sys/log.h"

namespace LevelNodes {

LevelNodes::CNodeManager* g_nodeManager;

using Scripts::CArray;
using Scripts::CStruct;

uint32_t g_nodeManagerReferences;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;     // name
constexpr uint32_t kLinks = 0x2E7D5EE7;    // links
constexpr uint32_t kPosition = 0xB9D31B0A; // position
constexpr uint32_t kPos = 0x7F261953;      // pos

CNodeManager*& Sole() { return g_nodeManager; }
uint32_t& RefTally() { return g_nodeManagerReferences; }

// NOT ORIGINAL - a node asked for that must exist (the release build's assertion did nothing) and doesn't: logged, the
// first time for each name. Many callers then use the null as a node and crash (e.g. IsAlive's 0x005A3D10 -> a crash
// in CStruct::ReadHashValue: a converted level's script naming a node its NodeArray hasn't).
void LogMissingNode(uint32_t name)
{
    static uint32_t logged[32];
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (logged[i] == name)
            return;
    if (count < 32)
        logged[count++] = name;
    const char* text = Scripts::LookUpHashName(name);
    Log("nodes: no node named 0x%08X%s%s%s in the level's node arrays - the caller expected one (it may crash on it)",
        name, text ? " (" : "", text ? text : "", text ? ")" : "");
}

constexpr uint32_t kClass = 0x12B4E660;    // class
constexpr uint32_t kLink = 0xC953660E;     // link (a class= whose nodes aren't hashed)

// NOT ORIGINAL - the level editor's overlay array (see node_manager.h).
uint32_t g_editorArray;
NodeArrayRemovedHook g_removedHook;
NodeArrayAddedHook g_addedHook;

// NOT ORIGINAL - the name hash, bigger than the original's (MemSys::Sizes::NodeHashSlots): the game's node manager (the one
// CreateNodeManager made) keeps its slots here, node indices in 32 bits (a node array may hold more than 32,767), its
// walk too; its own m_slots / m_walk are left unused. Any other manager (the self-tests' own) uses its m_slots as the
// original does.
struct BigSlot {
    uint32_t m_elementList;
    int32_t m_listLink; // -1: empty
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
          m_skipBits(m_big ? static_cast<uint32_t>(g_bigSlots.size()) - 1 : CNodeManager::kSlots - 1)
    {
    }
    uint32_t Mask() const { return m_skipBits; }
    bool Vacant(uint32_t i) const { return Elem(i) == -1; }
    int Elem(uint32_t i) const { return m_big ? m_big[i].m_listLink : m_small[i].m_listLink; }
    uint32_t Array(uint32_t i) const { return m_big ? m_big[i].m_elementList : m_small[i].m_elementList; }
    void AssignValue(uint32_t i, uint32_t array, int node)
    {
        if (m_big)
            m_big[i] = {array, node};
        else
            m_small[i] = {static_cast<uint16_t>(array), static_cast<int16_t>(node)};
    }
    void Clear(uint32_t i) { AssignValue(i, Array(i), -1); }

private:
    NodeSlot* m_small;
    BigSlot* m_big;
    uint32_t m_skipBits;
};

// The walk's place, whichever kind (as the slots).
struct Walk {
    explicit Walk(CNodeManager* m) : big(IsBig(m)), small(m->m_walk) {}
    int Elem() const { return big ? g_bigWalk.m_listLink : small.m_listLink; }
    uint32_t Array() const { return big ? g_bigWalk.m_elementList : small.m_elementList; }
    void AssignValue(uint32_t array, int node)
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
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (nodeClass == kLink)
        return;
    uint32_t name = 0;
    if (!node->ReadHashValue(kName, &name, 0))
        return;
    Slots slots(this);
    uint32_t slot = name & slots.Mask();
    while (!slots.Vacant(slot)) {
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
    slots.AssignValue(slot, array, static_cast<int>(index));
}

// 0x0045CEF0
void CNodeManager::UnhashNodeArray(uint32_t array)
{
    Slots slots(this);
    for (uint32_t i = 0; i <= slots.Mask(); ++i) {
        if (!slots.Vacant(i) && slots.Array(i) == array)
            slots.Clear(i);
    }
    // (A node may land in a later slot and be met - and moved - again.)
    for (uint32_t i = 0; i <= slots.Mask(); ++i) {
        if (slots.Vacant(i))
            continue;
        uint32_t movedArray = slots.Array(i);
        int movedNode = slots.Elem(i);
        slots.Clear(i);
        auto* node = reinterpret_cast<CStruct*>(m_arrays[movedArray].m_nodeList->FetchPart(movedNode));
        uint32_t name = 0;
        node->ReadHashValue(kName, &name, 1);
        uint32_t to = name & slots.Mask();
        while (!slots.Vacant(to))
            to = (to + 1) & slots.Mask();
        slots.AssignValue(to, movedArray, movedNode);
    }
}

// 0x0045CFC0
CStruct* CNodeManager::FetchVertex(uint32_t name, bool assert)
{
    Slots slots(this);
    // (NOT ORIGINAL: at most every slot once - a full hash without the name went round for ever)
    uint32_t tries = slots.Mask() + 1;
    for (uint32_t slot = name & slots.Mask(); tries-- && !slots.Vacant(slot); slot = (slot + 1) & slots.Mask()) {
        auto* node = reinterpret_cast<CStruct*>(m_arrays[slots.Array(slot)].m_nodeList->FetchPart(slots.Elem(slot)));
        uint32_t nodeName = 0;
        node->ReadHashValue(kName, &nodeName, 1);
        if (nodeName == name)
            return node;
    }
    if (assert)
        LogMissingNode(name);
    return nullptr;
}

// 0x0045D370
CNodeManager* CNodeManager::MakeNew()
{
    m_num = 0;
    for (NodeSlot& slot : m_slots)
        slot = {0, -1};
    m_walk.m_listLink = -1;
    return this;
}

// 0x0045D050
void CNodeManager::StartWalk(uint32_t name)
{
    Walk walk(this);
    walk.AssignValue(0, 0);
    if (!name)
        return;
    Slots slots(this);
    uint32_t slot = name & slots.Mask();
    for (;;) {
        CStruct* node = reinterpret_cast<CStruct*>(m_arrays[slots.Array(slot)].m_nodeList->FetchPart(slots.Elem(slot)));
        uint32_t nodeName = 0;
        node->ReadHashValue(kName, &nodeName, 1);
        if (nodeName == name)
            break;
        slot = (slot + 1) & slots.Mask();
    }
    walk.AssignValue(slots.Array(slot), slots.Elem(slot));
}

// 0x0045D110
void CNodeManager::EndWalk()
{
    Walk walk(this);
    walk.AssignValue(walk.Array(), -1);
}

// 0x0045D3F0
CStruct* CNodeManager::NextNode()
{
    Walk walk(this);
    if (walk.Elem() == -1)
        return nullptr;
    uint32_t array = walk.Array();
    CArray* nodes = m_arrays[array].m_nodeList;
    auto* node = reinterpret_cast<CStruct*>(nodes->FetchPart(walk.Elem()));
    int next = walk.Elem() + 1;
    if (!walk.big)
        next = static_cast<int16_t>(next); // (the original's 16-bit count)
    if (next >= static_cast<int>(nodes->m_extent)) {
        ++array;
        next = 0;
        if (array >= m_num)
            next = -1;
    }
    walk.AssignValue(array, next);
    return node;
}

// 0x0045D120
CArray* CNodeManager::GetLastNodeArray()
{
    // NOT ORIGINAL: the level editor's overlay array (SetEditorNodeArray) is passed over - callers mean the level's.
    uint32_t i = m_num;
    while (i && g_editorArray && m_arrays[i - 1].m_label == g_editorArray)
        --i;
    return i ? m_arrays[i - 1].m_nodeList : nullptr;
}

// 0x0045D150
uint32_t CNodeManager::GetLastNodeArrayName()
{
    // NOT ORIGINAL: as GetLastNodeArray, the editor's overlay array passed over.
    uint32_t i = m_num;
    while (i && g_editorArray && m_arrays[i - 1].m_label == g_editorArray)
        --i;
    return i ? m_arrays[i - 1].m_label : 0;
}

// 0x0045D140
void CNodeManager::SetOwner(void* owner) { mp_owner = owner; }

// 0x0045D470
uint32_t CNodeManager::GetOwnerValue58() { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mp_owner) + 0x58); }

// 0x0045D270
bool CNodeManager::HasNodeArray(uint32_t name)
{
    for (uint32_t i = 0; i < m_num; ++i) {
        if (m_arrays[i].m_label == name)
            return true;
    }
    return false;
}

// 0x0045D170
bool CNodeManager::HashNodeIn(uint32_t name, uint32_t a, uint32_t b)
{
    for (uint32_t i = 0; i < m_num; ++i) {
        if (m_arrays[i].m_label == name) {
            HashNode(i, a, reinterpret_cast<CStruct*>(b));
            return true;
        }
    }
    return false;
}

// 0x0045D3A0
void CNodeManager::HashNodes(uint32_t index)
{
    CArray* nodes = m_arrays[index].m_nodeList;
    for (uint32_t i = 0; i < nodes->m_extent; ++i)
        HashNode(index, i, reinterpret_cast<CStruct*>(nodes->FetchPart(i)));
}

// 0x0045D480
void CNodeManager::ResolveLinks(CArray* nodes)
{
    uint32_t count = nodes->m_extent;
    for (uint32_t i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<CStruct*>(nodes->FetchPart(i));
        CArray* links = nullptr;
        if (!node->FetchList(kLinks, &links, 0) || !links->m_extent)
            continue;
        uint32_t linkCount = links->m_extent;
        for (uint32_t j = 0; j < linkCount; ++j) {
            auto* linked = reinterpret_cast<CStruct*>(nodes->FetchPart(links->FetchPart(j)));
            links->SetElement(j, GetNodeName(linked));
        }
        links->ChangeSpecies(Scripts::QBVAL_NAME);
    }
}

// 0x0045D520
bool CNodeManager::AddNodeArray(uint32_t name, bool flag)
{
    if (HasNodeArray(name))
        return false;
    CArray* nodes = Scripts::FetchList(name);
    m_arrays[m_num].m_label = name;
    m_arrays[m_num].m_nodeList = nodes;
    m_arrays[m_num].m_flag = flag;
    ++m_num;
    HashNodes(m_num - 1);
    AddNodeArrayPrefixes(name);
    int count = static_cast<int>(nodes->m_extent);
    for (int i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<CStruct*>(nodes->FetchPart(i));
        Scripts::VectorValue position = {0.0f, 0.0f, 0.0f, 1.0f};
        if (node->FetchVec(kPosition, &position, 0)) {
            node->EraseModule(kPosition);
            node->PutVec3(kPos, position.x, position.y, -position.z);
        }
    }
    ResolveLinks(nodes);
    if (flag) {
        GameObjects::CPathManager::Sole()->NumberPaths();
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
    while (index < m_num && m_arrays[index].m_label != name)
        ++index;
    if (index >= m_num)
        return false;
    UnhashNodeArray(index);
    --m_num;
    if (index != m_num) {
        m_arrays[index] = m_arrays[m_num];
        Slots slots(this);
        for (uint32_t i = 0; i <= slots.Mask(); ++i) {
            if (slots.Array(i) == m_num)
                slots.AssignValue(i, index, slots.Elem(i));
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
    return Checksums::HashFromText(name);
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
    if (!Sole() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CNodeManager), nullptr, 1);
        Sole() = memory ? static_cast<CNodeManager*>(memory)->MakeNew() : nullptr;
        // NOT ORIGINAL: its name hash bigger than the original's (MemSys::Sizes::NodeHashSlots - see BigSlot).
        if (Sole() && MemSys::Sizes::NodeHashSlots() > static_cast<uint32_t>(CNodeManager::kSlots))
            UseBigNameHash(Sole(), MemSys::Sizes::NodeHashSlots());
    }
    ++RefTally();
    return Sole();
}

// 0x0045D6E0
void ReleaseNodeManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Sole())
        MemSys::Hub::Pull()->Dealloc(Sole());
    // NOT ORIGINAL: its bigger name hash goes with it.
    if (Sole() == g_bigOwner)
        UseBigNameHash(nullptr, 0);
    Sole() = nullptr;
    RefTally() = 0;
}

} // namespace LevelNodes
