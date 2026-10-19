// The rail manager, 0x0057C170-0x0057EE65. See rail_manager.h.

#include "game/obj/rail_manager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/smtptr.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

// Names.
constexpr uint32_t kClass = 0x12B4E660;          // Class
constexpr uint32_t kClimbingNode = 0x30C19600;   // ClimbingNode
constexpr uint32_t kType = 0x7321A8D6;           // type
constexpr uint32_t kLadder = 0xC84243DA;         // Ladder
constexpr uint32_t kCreatedAtStart = 0x7C2552B9; // CreatedAtStart
constexpr uint32_t kLipOverride = 0xF4C67F0F;    // LipOverride
constexpr uint32_t kDefaultLine = 0x4DE721DE;    // DefaultLine
constexpr uint32_t kHangLeft = 0xF6DD21CA;       // HangLeft
constexpr uint32_t kHangRight = 0x5E9CE29B;      // HangRight
constexpr uint32_t kNoClimbing = 0xF22BFC6D;     // NoClimbing
constexpr uint32_t kTerrainType = 0x54CF8532;    // TerrainType
constexpr uint32_t kClimbMaxSnap = 0x30CE7F2C;   // Climb_Max_Snap (a script float)
constexpr uint32_t kRailMaxSnap = 0xF840753E;    // Rail_Max_Snap (a script float)
constexpr uint32_t kRailNode = 0x8E6B02AD;       // RailNode
constexpr uint32_t kTrickObject = 0x1645B830;    // TrickObject
constexpr uint32_t kAllRailsTrickObjects = 0xB675F8F6; // (a script integer: every rail a trick object)
constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kCluster = 0x1A3A966B;        // Cluster
constexpr uint32_t kLinks = 0x2E7D5EE7;          // Links
constexpr uint32_t kLadderTrigger = 0xF5B17B09;  // TRG_Ladder03 (a node: counted 4 times as far)

// A collision probe along a line (Nx's feeler, 0xB0 bytes; as park_floor.cpp's): constructed by 0x00417760, its line
// set by 0x004171D0 (from, to), tested by 0x004172F0; what it hit (m_point) and the object hit, held by a smart pointer
// (+0x98). Going away: the smart pointer let go of (the destructor inlined), or the destructor 0x0041C350 - the
// original uses either (no C++ destructor here: callers say which).
struct Feeler {
    Mth::Vector m_start;   // +0x00
    Mth::Vector m_end;     // +0x10
    uint8_t m_unknown20[0x58];
    Mth::Vector m_point;   // +0x78
    uint8_t m_unknown88[0x10];
    Obj::CRefCounted* m_object; // +0x98 (a CSmtPtr<CRefCounted>: the object, then its links)
    uint32_t m_links[2];
    uint8_t m_unknownA4[0xC];

    Feeler* Construct() { return GameFunction<Feeler*(__thiscall*)(Feeler*)>(0x00417760)(this); }
    void SetLine(const Mth::Vector* from, const Mth::Vector* to)
    {
        GameFunction<void(__thiscall*)(Feeler*, const Mth::Vector*, const Mth::Vector*)>(0x004171D0)(this, from, to);
    }
    bool GetCollision(int a, int b) { return GameFunction<bool(__thiscall*)(Feeler*, int, int)>(0x004172F0)(this, a, b); }
    void ReleaseObject()
    {
        if (m_object)
            m_object->RemoveSmartPointer(reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>*>(&m_object));
    }
    void Destroy() { GameFunction<void(__thiscall*)(Feeler*)>(0x0041C350)(this); }
};
static_assert(offsetof(Feeler, m_point) == 0x78 && offsetof(Feeler, m_object) == 0x98 && sizeof(Feeler) == 0xB0);

// The matrix times `in` (all four components; 0x0040AF60, returns `out`).
Mth::Vector* Transform(const Mth::Matrix* matrix, Mth::Vector* out, const Mth::Vector* in)
{
    return GameFunction<Mth::Vector*(__thiscall*)(const Mth::Matrix*, Mth::Vector*, const Mth::Vector*)>(0x0040AF60)(
        matrix, out, in);
}

} // namespace

// 0x0057C170
double __cdecl Span4(float a, float b, float c, float d)
{
    double low = a;
    if (b < a)
        low = b;
    if (c < low)
        low = c;
    if (d < low)
        low = d;
    double high = a;
    if (b > a)
        high = b;
    if (c > high)
        high = c;
    if (d > high)
        return d - low;
    return high - low;
}

// 0x0057C200
bool CRailNode::IsNamed(uint32_t name) { return m_name == name; }

// 0x0057C210
void BoxPair::Swap()
{
    Mth::Vector held = second;
    second = first;
    first = held;
}

// 0x0057C260
void __cdecl BoxOf(const Mth::Vector* a, const Mth::Vector* b, Mth::Vector* min, Mth::Vector* max)
{
    float minX, maxX, minY, maxY, minZ, maxZ;
    if (a->x < b->x)
        minX = a->x, maxX = b->x;
    else
        minX = b->x, maxX = a->x;
    if (a->y < b->y)
        minY = a->y, maxY = b->y;
    else
        minY = b->y, maxY = a->y;
    if (a->z < b->z)
        minZ = a->z, maxZ = b->z;
    else
        minZ = b->z, maxZ = a->z;
    min->x = minX;
    min->y = minY;
    min->w = 1.0f;
    min->z = minZ;
    max->y = maxY;
    max->x = maxX;
    max->w = 1.0f;
    max->z = maxZ;
}

// 0x0057C330
void CRailManager::Cleanup()
{
    m_count = 0;
    Mem::FreeMalloc(m_nodes);
    m_nodes = nullptr;
    m_onObject = false;
    m_nodeArray = nullptr;
}

// 0x0057C360
double __cdecl Overlap(float a, float b, float c, float d)
{
    float lengths = static_cast<float>(std::fabs(static_cast<double>(d) - c) + std::fabs(static_cast<double>(b) - a));
    return lengths - Span4(a, b, c, d);
}

// 0x0057C3A0
int __cdecl NearlyEqual4(float a, float b, float c, float d, float tolerance)
{
    if (std::fabs(static_cast<double>(a) - b) < tolerance && std::fabs(static_cast<double>(b) - c) < tolerance &&
        std::fabs(static_cast<double>(c) - d) < tolerance)
        return 1;
    return 0;
}

// 0x0057C3F0
void CRailManager::MoveNode(uint32_t name, const Mth::Vector* pos)
{
    int index = 0;
    CRailNode* node = m_nodes;
    for (; index < m_count; ++index, ++node) {
        if (node->m_name == name)
            break;
    }
    if (index >= m_count)
        return;
    node->m_pos = *pos;
    if (CRailNode* next = node->m_next) {
        Mth::Vector here = node->m_pos;
        Mth::Vector there = next->m_pos;
        BoxOf(&here, &there, &node->m_min, &node->m_max);
    }
    if (node->m_prev) {
        // (the original tests the found node's own link each time, not each node's)
        for (int i = 0; i < m_count; ++i) {
            CRailNode* each = &m_nodes[index];
            if (each->m_next == node)
                BoxOf(&each->m_pos, &node->m_pos, &each->m_min, &each->m_max);
        }
    }
}

// 0x0057C4F0
Mth::Matrix& CRailManager::SetObjectMatrix(const Mth::Matrix& matrix)
{
    m_onObject = true;
    return m_objectMatrix = matrix;
}

// 0x0057C500
Record24* Record24::Assign(const Record24* other)
{
    for (int i = 0; i < 9; ++i)
        words[i] = other->words[i];
    return this;
}

// 0x0057C540
Record34* Record34::Assign(const Record34* other)
{
    for (int i = 0; i < 13; ++i)
        words[i] = other->words[i];
    return this;
}

// 0x0057C5A0
bool __stdcall NodesTogether(const CRailNode* a, const CRailNode* b)
{
    // (NaN counts as near: the original only fails on "greater")
    return !(std::fabs(static_cast<double>(a->m_pos.x) - b->m_pos.x) > 1.0) &&
           !(std::fabs(static_cast<double>(a->m_pos.y) - b->m_pos.y) > 1.0) &&
           !(std::fabs(static_cast<double>(a->m_pos.z) - b->m_pos.z) > 1.0);
}

// 0x0057C600
bool __cdecl RailClear(Mth::Vector a, Mth::Vector b)
{
    if (*reinterpret_cast<uint32_t*>(GameVariable<uint8_t*>(0x007CE5D8) + 0x40) == 0xD443A2BC) // (the park editor's)
        return true;
    Feeler feeler;
    feeler.Construct();
    // A quarter of the way from a to b, 6 above (the original's rounding: some sums kept unrounded).
    double sumW = static_cast<double>(b.w) + a.w;
    double sumZ = static_cast<double>(b.z) + a.z;
    float sumY = static_cast<float>(static_cast<double>(b.y) + a.y);
    float sumX = static_cast<float>(static_cast<double>(b.x) + a.x);
    float halfW = static_cast<float>(sumW * 0.5);
    double halfZ = sumZ * 0.5;
    double halfY = sumY * 0.5;
    float halfX = static_cast<float>(sumX * 0.5);
    float toW = static_cast<float>(static_cast<double>(halfW) - a.w);
    float toZ = static_cast<float>(halfZ - a.z);
    double toY = halfY - a.y;
    double toX = static_cast<double>(halfX) - a.x;
    float quarterW = static_cast<float>(toW * 0.5);
    float quarterZ = static_cast<float>(toZ * 0.5);
    float quarterY = static_cast<float>(toY * 0.5);
    double quarterX = toX * 0.5;
    float atW = static_cast<float>(static_cast<double>(a.w) + quarterW);
    float atZ = static_cast<float>(static_cast<double>(a.z) + quarterZ);
    double atY = static_cast<double>(a.y) + quarterY;
    float atX = static_cast<float>(static_cast<double>(a.x) + quarterX);
    float highY = static_cast<float>(atY + 6.0);
    // Across the rail (horizontally), 7 long (0x0042BBF0: the length set).
    Mth::Vector along = {static_cast<float>(static_cast<double>(b.x) - a.x), 0.0f,
                         static_cast<float>(static_cast<double>(b.z) - a.z),
                         static_cast<float>(static_cast<double>(b.w) - a.w)};
    GameFunction<void(__thiscall*)(Mth::Vector*, float)>(0x0042BBF0)(&along, 7.0f);
    const float acrossX = along.z, acrossY = along.y, acrossZ = static_cast<float>(-static_cast<double>(along.x)),
                acrossW = along.w;
    auto sub = [](float p, float q) { return static_cast<float>(static_cast<double>(p) - q); };
    auto add = [](float p, float q) { return static_cast<float>(static_cast<double>(p) + q); };
    const Mth::Vector minus = {sub(atX, acrossX), sub(highY, acrossY), sub(atZ, acrossZ), sub(atW, acrossW)};
    const Mth::Vector plus = {add(acrossX, atX), add(acrossY, highY), add(acrossZ, atZ), add(acrossW, atW)};
    // Anything across it (either way): not clear.
    feeler.SetLine(&plus, &minus);
    if (feeler.GetCollision(1, 0)) {
        feeler.ReleaseObject();
        return false;
    }
    reinterpret_cast<BoxPair*>(&feeler)->Swap();
    if (feeler.GetCollision(1, 0)) {
        feeler.ReleaseObject();
        return false;
    }
    // Down 12 from one side: nothing there, clear.
    Mth::Vector top = plus;
    Mth::Vector bottom = {plus.x, static_cast<float>(plus.y - 12.0), plus.z, static_cast<float>(plus.w + 1.0)};
    feeler.SetLine(&top, &bottom);
    if (!feeler.GetCollision(1, 0)) {
        feeler.ReleaseObject();
        return true;
    }
    float base = static_cast<float>(highY - 6.0);
    float heightOne = static_cast<float>(static_cast<double>(feeler.m_point.y) - base);
    // And from the other.
    top = minus;
    bottom = {minus.x, static_cast<float>(minus.y - 12.0), minus.z, static_cast<float>(minus.w + 1.0)};
    feeler.SetLine(&top, &bottom);
    if (!feeler.GetCollision(1, 0)) {
        feeler.Destroy();
        return true;
    }
    double heightTwo = static_cast<double>(feeler.m_point.y) - base;
    // Ground on both sides: clear only if one side is at the rail's height (within 1) or 2 below it.
    if (heightOne > -1.0f && heightTwo > -1.0) { // (NaN: on to the next test)
        feeler.Destroy();
        return false;
    }
    double higher, lower;
    if (heightOne < heightTwo) {
        higher = static_cast<float>(heightTwo);
        lower = heightOne;
    } else {
        higher = heightOne;
        lower = heightTwo;
    }
    feeler.Destroy();
    if (!(higher > 1.0))
        return true;
    return !(lower > -2.0);
}

// 0x0057CB20
Mth::Vector* CRailManager::ToWorld(Mth::Vector* out, const Mth::Vector* in)
{
    if (!m_onObject) {
        *out = *in;
        return out;
    }
    Transform(&m_objectMatrix, out, in);
    return out;
}

// 0x0057CB60
Mth::Vector* CRailManager::GetWorldPos(Mth::Vector* out, const CRailNode* node)
{
    if (!m_onObject) {
        *out = node->m_pos;
        return out;
    }
    Mth::Vector world;
    Transform(&m_objectMatrix, &world, &node->m_pos);
    *out = world;
    return out;
}

// 0x0057CBD0
CRailNode* CRailManager::FindNode(uint32_t name)
{
    if (!m_nodes)
        return nullptr;
    for (int i = 0; i < m_count; ++i) {
        if (m_nodes[i].m_name == name)
            return &m_nodes[i];
    }
    return nullptr;
}

// 0x0057CC30
int CRailNode::GetWallSide(const Mth::Vector* direction)
{
    const Mth::Vector p = m_pos;
    const Mth::Vector q = m_next->m_pos;
    Feeler feeler;
    feeler.Construct();
    // A quarter of the way along the segment, 6 below it (the original's rounding: some sums kept unrounded).
    float sumY = static_cast<float>(static_cast<double>(q.y) + p.y);
    float sumX = static_cast<float>(static_cast<double>(q.x) + p.x);
    float halfW = static_cast<float>((static_cast<double>(q.w) + p.w) * 0.5);
    double halfZ = (static_cast<double>(q.z) + p.z) * 0.5;
    double halfY = sumY * 0.5;
    float halfX = static_cast<float>(sumX * 0.5);
    float toW = static_cast<float>(halfW - static_cast<double>(p.w));
    float toZ = static_cast<float>(halfZ - p.z);
    double toY = halfY - p.y;
    double toX = static_cast<double>(halfX) - p.x;
    float quarterW = static_cast<float>(toW * 0.5);
    float quarterZ = static_cast<float>(toZ * 0.5);
    float quarterY = static_cast<float>(toY * 0.5);
    double quarterX = toX * 0.5;
    float atW = static_cast<float>(static_cast<double>(p.w) + quarterW);
    float atZ = static_cast<float>(static_cast<double>(p.z) + quarterZ);
    double atY = static_cast<double>(p.y) + quarterY;
    float atX = static_cast<float>(quarterX + p.x);
    float lowY = static_cast<float>(atY - 6.0);
    // Across the segment (horizontally), 4 long.
    double dw = static_cast<double>(q.w) - p.w;
    float dz = static_cast<float>(static_cast<double>(q.z) - p.z);
    double dx = static_cast<double>(q.x) - p.x;
    float dxStored = static_cast<float>(dx);
    float acrossY = 0.0f;
    float acrossX = dz;
    double length = std::sqrt(dx * dx + static_cast<double>(dz) * dz);
    double alongX = dx;
    if (length > 0.0) {
        double scale = 4.0 / length;
        alongX = dx * scale;
        acrossY = static_cast<float>(0.0 * scale);
        acrossX = static_cast<float>(dz * scale);
    }
    float acrossZ = static_cast<float>(-alongX);
    Mth::Vector from = {static_cast<float>(static_cast<double>(acrossX) + atX),
                        static_cast<float>(static_cast<double>(acrossY) + lowY),
                        static_cast<float>(static_cast<double>(acrossZ) + atZ), static_cast<float>(dw + atW)};
    Mth::Vector to = {static_cast<float>(static_cast<double>(atX) - acrossX),
                      static_cast<float>(static_cast<double>(lowY) - acrossY),
                      static_cast<float>(static_cast<double>(atZ) - acrossZ),
                      static_cast<float>(static_cast<double>(atW) - dw)};
    feeler.SetLine(&from, &to);
    bool hitOneWay = feeler.GetCollision(1, 0);
    Mth::Vector start = feeler.m_start;
    feeler.m_start = feeler.m_end;
    feeler.m_end = start;
    bool hitOtherWay = feeler.GetCollision(1, 0);
    int side = 0;
    if (hitOneWay != hitOtherWay) {
        side = hitOneWay ? -1 : 1;
        double dot = static_cast<double>(dxStored) * direction->x +
                     ((static_cast<double>(q.y) - p.y) * direction->y + static_cast<double>(dz) * direction->z);
        if (dot < 0.0)
            side = -side;
    }
    feeler.ReleaseObject();
    return side;
}

// 0x0057CFB0
Mth::Matrix* CRailManager::GetClimbMatrix(Mth::Matrix* out, const CRailNode* node)
{
    Mth::Matrix local;
    GameFunction<void(__thiscall*)(const Mth::Vector*, Mth::Matrix*)>(0x0042BC40)(&node->m_climb, &local); // the quaternion's
    local.row[0].w = 0.0f;
    local.row[1].w = 0.0f;
    local.row[2].w = 0.0f;
    local.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!m_onObject) {
        *out = local;
    } else {
        Mth::Matrix world;
        Mth::Multiply(world, m_objectMatrix, local);
        *out = world;
    }
    return out;
}

// 0x0057D090
void CRailManager::AddNode(uint32_t name, Script::CStruct* params)
{
    CRailNode* node = &m_nodes[m_adding];
    uint32_t nodeClass = 0; // (the original's are uninitialised: read only if the node has them)
    params->GetChecksum(kClass, &nodeClass, 0);
    bool climbing = nodeClass == kClimbingNode;
    uint32_t type = 0;
    params->GetChecksum(kType, &type, 0);
    node->m_next = nullptr;
    node->m_prev = nullptr;
    node->m_name = name;
    node->m_flags = 0;
    if (params->ContainsFlag(kCreatedAtStart))
        node->m_flags |= RAIL_FLAG_ACTIVE;
    else
        node->m_flags &= ~RAIL_FLAG_ACTIVE;
    SkateScript::GetPosition(params, &node->m_pos);
    if (params->ContainsFlag(kLipOverride))
        node->m_flags |= RAIL_FLAG_LIP_OVERRIDE;
    if (params->ContainsFlag(kDefaultLine))
        node->m_flags |= RAIL_FLAG_DEFAULT_LINE;
    if (climbing) {
        node->m_flags |= RAIL_FLAG_CLIMBING;
        if (type == kLadder)
            node->m_flags |= RAIL_FLAG_LADDER;
        else
            node->m_flags &= ~RAIL_FLAG_LADDER;
    }
    auto flag = [&](uint32_t name, uint32_t bit) {
        if (params->ContainsFlag(name))
            node->m_flags |= bit;
        else
            node->m_flags &= ~bit;
    };
    flag(kHangLeft, RAIL_FLAG_HANG_LEFT);
    flag(kHangRight, RAIL_FLAG_HANG_RIGHT);
    flag(kNoClimbing, RAIL_FLAG_NO_CLIMBING);
    if (climbing) {
        // Its orientation from its angles, as a quaternion.
        Mth::Vector angles;
        SkateScript::GetAngles(params, &angles);
        Mth::Matrix matrix;
        Mth::Matrix* built = GameFunction<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*)>(0x0041A730)(&matrix, &angles);
        Mth::Vector quat;
        GameFunction<void(__thiscall*)(Mth::Vector*, const Mth::Matrix*)>(0x0042E1F0)(&quat, built);
        node->m_climb = quat;
    } else {
        node->m_terrain = 0;
        uint32_t terrain = 0; // (the original's: its params argument's slot, the low byte the climbing flag)
        params->GetChecksum(kTerrainType, &terrain, 1);
        node->m_terrain = GameFunction<uint8_t(__cdecl*)(uint32_t)>(0x004442C0)(terrain);
    }
    BoxOf(&node->m_pos, &node->m_pos, &node->m_min, &node->m_max);
}

// 0x0057D260
void CRailManager::RemoveOverlaps()
{
    Dbg::Printf("Starting overlapping rail removal\n");
    int removed = 0;
    for (int i = 0; i < m_count; ++i) {
        CRailNode* a = &m_nodes[i];
        CRailNode* aNext = a->m_next;
        if (!aNext)
            continue;
        const Mth::Vector p = a->m_pos;
        const Mth::Vector q = aNext->m_pos;
        // Its direction (each step stored as a float, as the original), made unit length.
        float dx = static_cast<float>(static_cast<double>(q.x) - p.x);
        float dy = static_cast<float>(static_cast<double>(q.y) - p.y);
        float dz = static_cast<float>(static_cast<double>(q.z) - p.z);
        const float rawX = dx, rawY = dy, rawZ = dz;
        double length = std::sqrt(static_cast<double>(dy) * dy + static_cast<double>(dx) * dx + static_cast<double>(dz) * dz);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            dx = static_cast<float>(dx * inverse);
            dy = static_cast<float>(dy * inverse);
            dz = static_cast<float>(dz * inverse);
        }
        // Its box, 16 bigger each way.
        const float loX = static_cast<float>(a->m_min.x - 16.0), loY = static_cast<float>(a->m_min.y - 16.0),
                    loZ = static_cast<float>(a->m_min.z - 16.0);
        const float hiX = static_cast<float>(a->m_max.x + 16.0), hiY = static_cast<float>(a->m_max.y + 16.0),
                    hiZ = static_cast<float>(a->m_max.z + 16.0);
        for (int j = i + 1; j < m_count; ++j) {
            if (!(a->m_flags & RAIL_FLAG_ACTIVE))
                break;
            CRailNode* b = &m_nodes[j];
            CRailNode* bNext = b->m_next;
            if (!bNext)
                continue;
            // (the tests as the original's: NaN passes them)
            if (b->m_min.x > hiX || b->m_max.x < loX || b->m_min.z > hiZ || b->m_max.z < loZ || b->m_min.y > hiY ||
                b->m_max.y < loY)
                continue;
            const Mth::Vector r = b->m_pos;
            const Mth::Vector s = bNext->m_pos;
            Mth::Vector along = {static_cast<float>(static_cast<double>(s.x) - r.x),
                                 static_cast<float>(static_cast<double>(s.y) - r.y),
                                 static_cast<float>(static_cast<double>(s.z) - r.z),
                                 static_cast<float>(static_cast<double>(s.w) - r.w)};
            along.Normalize();
            double dot = static_cast<double>(along.y) * dy + static_cast<double>(along.x) * dx +
                         static_cast<double>(along.z) * dz;
            // Parallel (or opposite) to within about 8 degrees.
            if (!(dot < -0.99f) && !(dot > 0.99f))
                continue;
            Mth::Vector offset = {static_cast<float>(static_cast<double>(r.x) - p.x),
                                  static_cast<float>(static_cast<double>(r.y) - p.y),
                                  static_cast<float>(static_cast<double>(r.z) - p.z),
                                  static_cast<float>(static_cast<double>(r.w) - p.w)};
            Mth::Vector direction = {rawX, rawY, rawZ, static_cast<float>(static_cast<double>(q.w) - p.w)};
            direction.Normalize();
            Mth::Vector apart = offset;
            apart.ProjectToPlane(direction);
            double distance = std::sqrt(static_cast<double>(apart.x) * apart.x + static_cast<double>(apart.z) * apart.z +
                                        static_cast<double>(apart.y) * apart.y);
            if (!(distance < 6.0))
                continue;
            int matches = Overlap(p.x, q.x, r.x, s.x) > 6.0 ? 1 : 0;
            if (Overlap(p.z, q.z, r.z, s.z) > 6.0)
                ++matches;
            int same = NearlyEqual4(p.x, q.x, r.x, s.x, 6.0f) ? 1 : 0;
            if (NearlyEqual4(p.z, q.z, r.z, s.z, 6.0f))
                ++same;
            if (same + matches < 2)
                continue;
            // Overlapping: the lower one goes.
            ++removed;
            if (a->m_pos.y < b->m_pos.y)
                a->m_flags &= ~RAIL_FLAG_ACTIVE;
            else
                b->m_flags &= ~RAIL_FLAG_ACTIVE;
            break;
        }
    }
    Dbg::Printf("Done overlapping rail removal, removed %d\n", removed);
}

// 0x0057D7A0
void CRailManager::RemoveOverlapsIfNotPark()
{
    if (*reinterpret_cast<uint32_t*>(GameVariable<uint8_t*>(0x007CE5D8) + 0x40) == 0xD443A2BC) // (the park editor's level)
        return;
    RemoveOverlaps();
}

// 0x0057D7C0
void CRailManager::SetActive(uint32_t name, int active, bool wholeRail)
{
    int index = 0;
    CRailNode* node = m_nodes;
    for (; index < m_count; ++index, ++node) {
        if (node->m_name == name)
            break;
    }
    if (index >= m_count)
        return;
    if (wholeRail) {
        // This node and every one after it.
        for (; node; node = node->m_next) {
            if (active)
                node->m_flags |= RAIL_FLAG_ACTIVE;
            else
                node->m_flags &= ~RAIL_FLAG_ACTIVE;
        }
        return;
    }
    if (active)
        node->m_flags |= RAIL_FLAG_ACTIVE;
    else
        node->m_flags &= ~RAIL_FLAG_ACTIVE;
}

// 0x0057D840
bool CRailManager::IsActive(uint32_t name)
{
    for (int index = 0; index < m_count; ++index) {
        if (m_nodes[index].m_name == name)
            return (m_nodes[index].m_flags & RAIL_FLAG_ACTIVE) != 0;
    }
    return false;
}

// 0x0057D880
bool CRailManager::FindLadder(const Mth::Vector* pos, float radius, float height, bool up, void* checker, uint32_t extra,
                              CRailNode** found)
{
    float radiusSquared = static_cast<float>(static_cast<double>(radius) * radius);
    for (int index = m_count; index != 0;) {
        CRailNode* node = &m_nodes[--index];
        if (!(node->m_flags & RAIL_FLAG_LADDER) || !(node->m_flags & RAIL_FLAG_ACTIVE))
            continue;
        if (std::fabs(static_cast<double>(node->m_pos.x) - pos->x) > radius ||
            std::fabs(static_cast<double>(node->m_pos.z) - pos->z) > radius ||
            std::fabs(static_cast<double>(node->m_pos.y) - pos->y) > height)
            continue;
        double dx = static_cast<double>(node->m_pos.x) - pos->x;
        double dz = static_cast<double>(node->m_pos.z) - pos->z;
        if (dz * dz + dx * dx > radiusSquared)
            continue;
        CRailNode* other = node->m_next ? node->m_next : node->m_prev;
        Mth::Matrix matrix;
        if (up) {
            Mth::Matrix* orientation = GetClimbMatrix(&matrix, node);
            if (GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, Mth::Matrix*, uint32_t)>(0x004418B0)(
                    checker, &node->m_pos, &other->m_pos, orientation, extra)) {
                *found = node;
                return true;
            }
        } else {
            Mth::Matrix* orientation = GetClimbMatrix(&matrix, other);
            if (GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, Mth::Matrix*, uint32_t)>(0x00441260)(
                    checker, &other->m_pos, &node->m_pos, orientation, extra)) {
                *found = other;
                return true;
            }
        }
    }
    return false;
}

// 0x0057DA20
bool CRailManager::FindLadderOnLine(const Mth::Vector* start, const Mth::Vector* end, void* checker, Record24* result,
                                    CRailNode** found)
{
    *found = nullptr;
    Mth::Line line;
    line.start = *start;
    line.end = *end;
    Mth::Vector min, max;
    BoxOf(&line.start, &line.end, &min, &max);
    double snap = Script::GetFloat(kClimbMaxSnap);
    min.x = static_cast<float>(min.x - snap);
    float best = 1e14f;
    min.y = static_cast<float>(min.y - snap);
    min.z = static_cast<float>(min.z - snap);
    max.x = static_cast<float>(max.x + snap);
    max.y = static_cast<float>(max.y + snap);
    max.z = static_cast<float>(max.z + snap);
    float snapSquared = static_cast<float>(snap * snap);
    for (int index = 0; index < m_count; ++index) {
        CRailNode* node = &m_nodes[index];
        if (!(node->m_flags & RAIL_FLAG_LADDER) || !(node->m_flags & RAIL_FLAG_ACTIVE) || !node->m_next)
            continue;
        // (the tests as the original's: NaN passes them)
        if (min.x > node->m_max.x || max.x < node->m_min.x || min.z > node->m_max.z || max.z < node->m_min.z ||
            min.y > node->m_max.y || max.y < node->m_min.y)
            continue;
        CRailNode* next = node->m_next;
        Mth::Line rail;
        rail.start = node->m_pos;
        rail.end = next->m_pos;
        Mth::Vector onLine, onRail;
        float s, t;
        if (!Mth::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true))
            continue;
        Mth::Vector apart = onRail - onLine;
        float distanceSquared = apart.LengthSqr();
        if (node->m_name == kLadderTrigger || next->m_name == kLadderTrigger)
            distanceSquared = static_cast<float>(distanceSquared * 4.0);
        if (distanceSquared > snapSquared || distanceSquared > best)
            continue;
        Record24 record;
        Mth::Matrix matrix;
        if (node->m_pos.y < next->m_pos.y) {
            // Climbing up from this node.
            Mth::Matrix* orientation = GetClimbMatrix(&matrix, node);
            if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, Mth::Matrix*,
                                                const Mth::Vector*, float, Record24*)>(0x004413E0)(
                    checker, &node->m_pos, &next->m_pos, orientation, &onRail, t, &record))
                continue;
            *found = node;
        } else {
            Mth::Matrix* orientation = GetClimbMatrix(&matrix, next);
            if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, Mth::Matrix*,
                                                const Mth::Vector*, float, Record24*)>(0x004413E0)(
                    checker, &next->m_pos, &node->m_pos, orientation, &onRail, static_cast<float>(1.0 - t), &record))
                continue;
            *found = next;
        }
        best = distanceSquared;
        result->Assign(&record);
    }
    return *found != nullptr;
}

// 0x0057DDA0
bool CRailManager::FindRail(const Mth::Vector* start, const Mth::Vector* end, const Mth::Vector* direction, void* checker,
                            Record34* result, float margin)
{
    bool found = false;
    Mth::Line line;
    line.start = *start;
    line.end = *end;
    Mth::Vector min, max;
    BoxOf(&line.start, &line.end, &min, &max);
    min.x = static_cast<float>(static_cast<double>(min.x) - margin);
    float best = 1e14f;
    min.y = static_cast<float>(static_cast<double>(min.y) - margin);
    min.z = static_cast<float>(static_cast<double>(min.z) - margin);
    max.x = static_cast<float>(static_cast<double>(max.x) + margin);
    max.y = static_cast<float>(static_cast<double>(max.y) + margin);
    max.z = static_cast<float>(static_cast<double>(max.z) + margin);
    float marginSquared = static_cast<float>(static_cast<double>(margin) * margin);
    for (int index = 0; index < m_count; ++index) {
        CRailNode* node = &m_nodes[index];
        if ((node->m_flags & RAIL_FLAG_LADDER) || (node->m_flags & RAIL_FLAG_NO_CLIMBING) ||
            !(node->m_flags & RAIL_FLAG_ACTIVE) || !node->m_next)
            continue;
        // (the tests as the original's: NaN passes them)
        if (min.x > node->m_max.x || max.x < node->m_min.x || min.z > node->m_max.z || max.z < node->m_min.z ||
            min.y > node->m_max.y || max.y < node->m_min.y)
            continue;
        CRailNode* next = node->m_next;
        Mth::Line rail;
        rail.start = node->m_pos;
        rail.end = next->m_pos;
        Mth::Vector onLine, onRail;
        float s, t;
        if (!Mth::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true))
            continue;
        Mth::Vector apart = onRail - onLine;
        if (apart.LengthSqr() > marginSquared)
            continue;
        // Distance along `direction` counts a third.
        double along = static_cast<double>(apart.z) * direction->z + static_cast<double>(apart.y) * direction->y +
                       static_cast<double>(apart.x) * direction->x;
        if (along > 0.0)
            apart -= static_cast<float>(along * 0.6666667f) * *direction;
        float distanceSquared = apart.LengthSqr();
        if (distanceSquared > best)
            continue;
        Mth::Vector nodeWorld, nextWorld;
        GetWorldPos(&nextWorld, next);
        GetWorldPos(&nodeWorld, node);
        if (!RailClear(nodeWorld, nextWorld))
            continue;
        Record34 record;
        record.words[12] = reinterpret_cast<uint32_t>(node);
        Mth::Vector lineWorld, railWorld, nextAt, nodeAt;
        Mth::Vector* lineAt = ToWorld(&lineWorld, &onLine);
        Mth::Vector* railAt = ToWorld(&railWorld, &onRail);
        Mth::Vector* nextPos = GetWorldPos(&nextAt, next);
        Mth::Vector* nodePos = GetWorldPos(&nodeAt, node);
        if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, const Mth::Vector*,
                                            const Mth::Vector*, float, Record34*, int)>(0x00440690)(
                checker, nodePos, nextPos, railAt, lineAt, t, &record, 0))
            continue;
        best = distanceSquared;
        result->Assign(&record);
        found = true;
    }
    return found;
}

// 0x0057E170
bool CRailManager::FindJoiningRail(const CRailNode* from, uint32_t excludeFlags, CRailNode** found)
{
    for (int index = 0; index < m_count; ++index) {
        CRailNode* node = &m_nodes[index];
        // (the tests as the original's: NaN passes them)
        if (std::fabs(static_cast<double>(node->m_pos.x) - from->m_pos.x) > 1.0 ||
            std::fabs(static_cast<double>(node->m_pos.y) - from->m_pos.y) > 1.0 ||
            std::fabs(static_cast<double>(node->m_pos.z) - from->m_pos.z) > 1.0)
            continue;
        CRailNode* next = node->m_next;
        if (!next && !node->m_prev)
            continue;
        if (node == from)
            continue;
        if (next && (node->m_flags & RAIL_FLAG_ACTIVE) && !(excludeFlags & node->m_flags)) {
            Mth::Vector nodeWorld, nextWorld;
            GetWorldPos(&nextWorld, next);
            GetWorldPos(&nodeWorld, node);
            if (RailClear(nodeWorld, nextWorld)) {
                *found = node;
                return true;
            }
        }
        CRailNode* prev = node->m_prev;
        if (prev && (prev->m_flags & RAIL_FLAG_ACTIVE) && !(excludeFlags & prev->m_flags)) {
            Mth::Vector prevWorld, nodeWorld;
            GetWorldPos(&nodeWorld, node);
            GetWorldPos(&prevWorld, node->m_prev);
            if (RailClear(prevWorld, nodeWorld)) {
                *found = node->m_prev;
                return true;
            }
        }
    }
    return false;
}

// 0x0057E960
void CRailManager::AddFromNodeArray(Script::CArray* nodes)
{
    m_nodeArray = nodes;
    void* skate = GameVariable<void*>(0x007CE478);
    auto skipped = [skate](Script::CStruct* node) { // (the skate module's 0x00565BB0: a node this game leaves out)
        return GameFunction<bool(__thiscall*)(void*, Script::CStruct*)>(0x00565BB0)(skate, node);
    };
    auto isRail = [](Script::CStruct* node) {
        uint32_t nodeClass = 0;
        node->GetChecksum(kClass, &nodeClass, 0);
        return nodeClass == kRailNode || nodeClass == kClimbingNode;
    };
    m_count = 0;
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(i));
        if (!skipped(node) && isRail(node))
            ++m_count;
    }
    if (!m_count)
        return;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_bottomUpHeap);
    m_nodes = static_cast<CRailNode*>(Mem::Malloc(m_count * sizeof(CRailNode)));
    // A hash table of the nodes' names (0x4000 slots, linear probing) to their indexes.
    auto* table = static_cast<int16_t*>(Mem::Malloc(0x8000));
    Mem::Manager::Get()->PopContext();
    std::memset(table, 0xFF, 0x8000);
    m_adding = 0;
    bool allTrickObjects = Script::GetInteger(kAllRailsTrickObjects) != 0;
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(i));
        if (skipped(node) || !isRail(node))
            continue;
        uint32_t name = SkateScript::GetNodeName(node);
        AddNode(name, node);
        uint16_t slot = static_cast<uint16_t>(name & 0x3FFF);
        while (table[slot] != -1)
            slot = static_cast<uint16_t>((slot + 1) & 0x3FFF);
        table[slot] = static_cast<int16_t>(m_adding);
        bool trickObject = node->ContainsComponentNamed(kTrickObject);
        if (allTrickObjects || trickObject) {
            // A trick object of its cluster (the park / trick object manager 0x007CE500).
            uint32_t nodeName = 0;
            node->GetChecksum(kName, &nodeName, 1);
            uint32_t cluster = nodeName;
            node->GetChecksum(kCluster, &cluster, 1);
            void* trickObjects = GameVariable<void*>(0x007CE500);
            GameFunction<void(__thiscall*)(void*, uint32_t)>(0x005896D0)(trickObjects, cluster);
            GameFunction<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00589080)(trickObjects, nodeName, cluster);
        }
        ++m_adding;
    }
    // The links: each node's Links (by name, through the table).
    for (int i = 0; i < m_count; ++i) {
        CRailNode* node = &m_nodes[i];
        Script::CStruct* params = SkateScript::GetNode(node->m_name, 1);
        Script::CArray* links = nullptr;
        params->GetArray(kLinks, &links, 0);
        if (!links)
            continue;
        int linkCount = static_cast<int>(links->m_size);
        for (int l = 0; l < linkCount; ++l) {
            uint32_t link = links->GetElement(l);
            uint16_t slot = static_cast<uint16_t>(link & 0x3FFF);
            CRailNode* other = &m_nodes[table[slot]];
            while (other->m_name != link) {
                slot = static_cast<uint16_t>((slot + 1) & 0x3FFF);
                other = &m_nodes[table[slot]];
            }
            if (!node->m_next || (other->m_flags & RAIL_FLAG_DEFAULT_LINE)) {
                node->m_next = other;
                BoxOf(&node->m_pos, &other->m_pos, &node->m_min, &node->m_max);
            }
            if (!other->m_prev || (node->m_flags & RAIL_FLAG_DEFAULT_LINE))
                other->m_prev = node;
        }
    }
    Mem::FreeMalloc(table);
}

// 0x0057E2D0
bool CRailManager::FindGrindRail(const Mth::Vector* start, const Mth::Vector* end, Mth::Vector* point, CRailNode** found,
                                 const CRailNode* exclude, float minAlignment, int side)
{
    Mth::Line line;
    line.start = *start;
    line.end = *end;
    Mth::Vector min, max;
    BoxOf(&line.start, &line.end, &min, &max);
    float snap = Script::GetFloat(kRailMaxSnap);
    min.x = static_cast<float>(static_cast<double>(min.x) - snap);
    float best = 1e7f;
    CRailNode* bestNode = nullptr;
    bool any = false;
    min.y = static_cast<float>(static_cast<double>(min.y) - snap);
    min.z = static_cast<float>(static_cast<double>(min.z) - snap);
    max.x = static_cast<float>(static_cast<double>(max.x) + snap);
    max.y = static_cast<float>(static_cast<double>(max.y) + snap);
    max.z = static_cast<float>(static_cast<double>(max.z) + snap);
    Mth::Vector bestPoint;
    for (int index = 0; index < m_count; ++index) {
        CRailNode* node = &m_nodes[index];
        if ((node->m_flags & RAIL_FLAG_CLIMBING) || node == exclude || !(node->m_flags & RAIL_FLAG_ACTIVE))
            continue;
        // (the tests as the original's: NaN passes them)
        bool outside = min.x > node->m_max.x || max.x < node->m_min.x || min.z > node->m_max.z ||
                       max.z < node->m_min.z || min.y > node->m_max.y || max.y < node->m_min.y;
        if (CRailNode* next = node->m_next) {
            if (outside || !RailClear(node->m_pos, next->m_pos))
                continue;
            Mth::Line rail;
            rail.start = node->m_pos;
            rail.end = next->m_pos;
            Mth::Vector onLine, onRail;
            float s, t;
            if (!Mth::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true))
                continue;
            float distance = (onRail - onLine).Length();
            // How well the rail lines up with the line (flat).
            Mth::Vector railDir = next->m_pos - node->m_pos;
            railDir.y = 0.0f;
            Mth::Vector lineDir = *end - *start;
            lineDir.y = 0.0f;
            railDir.Normalize();
            lineDir.Normalize();
            float alignment = static_cast<float>(std::fabs(
                static_cast<double>(lineDir.x) * railDir.x + static_cast<double>(lineDir.z) * railDir.z +
                static_cast<double>(lineDir.y) * railDir.y));
            if (lineDir.x == 0.0f && lineDir.z == 0.0f && alignment == 0.0f)
                alignment = 1.0f;
            float score = static_cast<float>((1.122f - static_cast<double>(alignment)) * distance);
            float reach = static_cast<float>((2.0 - alignment) * distance);
            if (side) {
                Mth::Vector direction = *end - *start;
                if (node->GetWallSide(&direction) != side)
                    score = score + score;
            }
            if (!(score < best))
                continue;
            if (minAlignment != 1.0f && std::fabs(alignment) < minAlignment)
                continue;
            best = score;
            if (reach > snap) { // (out of reach: but it still sets the best - and clears what was found)
                any = false;
                continue;
            }
            bestPoint = onRail;
            bestNode = node;
            any = true;
        } else if (!node->m_prev) {
            // A rail of one node: its point.
            if (outside)
                continue;
            Mth::Vector offset = node->m_pos - *start;
            Mth::Vector direction = *end - *start;
            Mth::Vector& unit = direction.Normalize();
            Mth::Vector along = unit;
            double k = static_cast<double>(offset.x) * along.x + static_cast<double>(offset.z) * along.z +
                       static_cast<double>(offset.y) * along.y;
            offset -= static_cast<float>(k) * along;
            double distance = std::sqrt(static_cast<double>(offset.x) * offset.x +
                                        static_cast<double>(offset.y) * offset.y +
                                        static_cast<double>(offset.z) * offset.z);
            if (distance > snap)
                continue;
            double twice = distance + distance;
            if (best < twice)
                continue;
            bestPoint = node->m_pos;
            best = static_cast<float>(twice);
            bestNode = node;
            any = true;
        }
    }
    if (any) {
        *point = bestPoint;
        *found = bestNode;
    }
    return any;
}

// 0x0057CC10
CRailManager* CRailManager::Init()
{
    m_nodes = nullptr;
    m_count = 0;
    m_onObject = false;
    m_nodeArray = nullptr;
    return this;
}

} // namespace Obj

REPLACE(0x0057C170, Obj::Span4);
REPLACE(0x0057C200, &Obj::CRailNode::IsNamed);
REPLACE(0x0057C210, &Obj::BoxPair::Swap);
REPLACE(0x0057C260, Obj::BoxOf);
REPLACE(0x0057C330, &Obj::CRailManager::Cleanup);
REPLACE(0x0057C360, Obj::Overlap);
REPLACE(0x0057C3A0, Obj::NearlyEqual4);
REPLACE(0x0057C3F0, &Obj::CRailManager::MoveNode);
REPLACE(0x0057C4F0, &Obj::CRailManager::SetObjectMatrix);
REPLACE(0x0057C500, &Obj::Record24::Assign);
REPLACE(0x0057C540, &Obj::Record34::Assign);
REPLACE(0x0057C5A0, Obj::NodesTogether);
REPLACE(0x0057C600, Obj::RailClear);
REPLACE(0x0057CB20, &Obj::CRailManager::ToWorld);
REPLACE(0x0057CB60, &Obj::CRailManager::GetWorldPos);
REPLACE(0x0057CBD0, &Obj::CRailManager::FindNode);
REPLACE(0x0057CC10, &Obj::CRailManager::Init);
REPLACE(0x0057CC30, &Obj::CRailNode::GetWallSide);
REPLACE(0x0057CFB0, &Obj::CRailManager::GetClimbMatrix);
REPLACE(0x0057D090, &Obj::CRailManager::AddNode);
REPLACE(0x0057D260, &Obj::CRailManager::RemoveOverlaps);
REPLACE(0x0057D7A0, &Obj::CRailManager::RemoveOverlapsIfNotPark);
REPLACE(0x0057D7C0, &Obj::CRailManager::SetActive);
REPLACE(0x0057D840, &Obj::CRailManager::IsActive);
REPLACE(0x0057D880, &Obj::CRailManager::FindLadder);
REPLACE(0x0057DA20, &Obj::CRailManager::FindLadderOnLine);
REPLACE(0x0057DDA0, &Obj::CRailManager::FindRail);
REPLACE(0x0057E170, &Obj::CRailManager::FindJoiningRail);
REPLACE(0x0057E2D0, &Obj::CRailManager::FindGrindRail);
REPLACE(0x0057E960, &Obj::CRailManager::AddFromNodeArray);
