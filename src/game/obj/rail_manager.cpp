// The rail manager, 0x0057C170-0x0057EE65. See rail_manager.h.

#include "game/obj/rail_manager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/quat_rotation.h"
#include "game/nx/probe_ray.h"
#include "game/obj/smtptr.h"
#include "game/obj/trick_objects.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/sfx/surface_kind.h"
#include "game/skate/skate_module.h"
#include "game/skater/walk_physics.h"
#include "game/ed/park_editor.h"

namespace GameObjects {

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
// set by 0x004171D0 (from, to), tested by 0x004172F0; what it hit (m_contactPos) and the object hit, held by a smart pointer
// (+0x98). Going away: the smart pointer let go of (the destructor inlined), or the destructor 0x0041C350 - the
// original uses either (no C++ destructor here: callers say which).
struct Feeler {
    Maths::Vec4 m_outset;   // +0x00
    Maths::Vec4 m_finish;     // +0x10
    uint8_t m_unknown20[0x58];
    Maths::Vec4 m_contactPos;   // +0x78
    uint8_t m_unknown88[0x10];
    GameObjects::CSharedBase* m_entity; // +0x98 (a CSharedRef<CSharedBase>: the object, then its links)
    uint32_t m_links[2];
    uint8_t m_unknownA4[0xC];

    // (Engine3D::CProbeRay, seen by its fields here)
    Engine3D::CProbeRay* AsFeeler() { return reinterpret_cast<Engine3D::CProbeRay*>(this); }
    Feeler* MakeNew()
    {
        AsFeeler()->MakeNew();
        return this;
    }
    void ChangeStrand(const Maths::Vec4* from, const Maths::Vec4* to) { AsFeeler()->ChangeStrand(from, to); }
    bool GetHitShape(int a, int b) { return AsFeeler()->GetHitShape(a != 0, b != 0) != 0; }
    void ReleaseObject()
    {
        if (m_entity)
            m_entity->DetachSharedRef(reinterpret_cast<GameObjects::CSharedRef<GameObjects::CSharedBase>*>(&m_entity));
    }
    void Teardown() { AsFeeler()->Destruct(); }
};
static_assert(offsetof(Feeler, m_contactPos) == 0x78 && offsetof(Feeler, m_entity) == 0x98 && sizeof(Feeler) == 0xB0);

// The matrix times `in` (all four components; 0x0040AF60, returns `out`).
Maths::Vec4* Xform(const Maths::Mat4* matrix, Maths::Vec4* out, const Maths::Vec4* in)
{
    return &matrix->Xform(*out, *in);
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
bool CGrindPoint::IsNamed(uint32_t name) { return m_label == name; }

// 0x0057C210
void BoxPair::Exchange()
{
    Maths::Vec4 held = second;
    second = first;
    first = held;
}

// 0x0057C260
void __cdecl BoxOf(const Maths::Vec4* a, const Maths::Vec4* b, Maths::Vec4* min, Maths::Vec4* max)
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
void CGrindRailHub::TidyUp()
{
    m_num = 0;
    MemSys::FreeMalloc(m_nodes);
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
void CGrindRailHub::MoveNode(uint32_t name, const Maths::Vec4* pos)
{
    int index = 0;
    CGrindPoint* node = m_nodes;
    for (; index < m_num; ++index, ++node) {
        if (node->m_label == name)
            break;
    }
    if (index >= m_num)
        return;
    node->m_atPoint = *pos;
    if (CGrindPoint* next = node->m_succ) {
        Maths::Vec4 here = node->m_atPoint;
        Maths::Vec4 there = next->m_atPoint;
        BoxOf(&here, &there, &node->m_lowerLimit, &node->m_upperLimit);
    }
    if (node->m_lastValue) {
        // (the original tests the found node's own link each time, not each node's)
        for (int i = 0; i < m_num; ++i) {
            CGrindPoint* each = &m_nodes[index];
            if (each->m_succ == node)
                BoxOf(&each->m_atPoint, &node->m_atPoint, &each->m_lowerLimit, &each->m_upperLimit);
        }
    }
}

// 0x0057C4F0
Maths::Mat4& CGrindRailHub::SetObjectMatrix(const Maths::Mat4& matrix)
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
bool __stdcall NodesTogether(const CGrindPoint* a, const CGrindPoint* b)
{
    // (NaN counts as near: the original only fails on "greater")
    return !(std::fabs(static_cast<double>(a->m_atPoint.x) - b->m_atPoint.x) > 1.0) &&
           !(std::fabs(static_cast<double>(a->m_atPoint.y) - b->m_atPoint.y) > 1.0) &&
           !(std::fabs(static_cast<double>(a->m_atPoint.z) - b->m_atPoint.z) > 1.0);
}

namespace {
RailClearOverride g_railClearOverride; // (NOT ORIGINAL: SetRailClearOverride)
} // namespace

void SetRailClearOverride(RailClearOverride override) { g_railClearOverride = override; }

// 0x0057C600
bool __cdecl RailClear(Maths::Vec4 a, Maths::Vec4 b)
{
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor) + 0x40) == 0xD443A2BC) // (the park editor's)
        return true;
    if (g_railClearOverride && g_railClearOverride(&a, &b)) // (NOT ORIGINAL: the level editor's flat-ground rails)
        return true;
    Feeler feeler;
    feeler.MakeNew();
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
    Maths::Vec4 along = {static_cast<float>(static_cast<double>(b.x) - a.x), 0.0f,
                         static_cast<float>(static_cast<double>(b.z) - a.z),
                         static_cast<float>(static_cast<double>(b.w) - a.w)};
    along.SetLength(7.0f);
    const float acrossX = along.z, acrossY = along.y, acrossZ = static_cast<float>(-static_cast<double>(along.x)),
                acrossW = along.w;
    auto sub = [](float p, float q) { return static_cast<float>(static_cast<double>(p) - q); };
    auto add = [](float p, float q) { return static_cast<float>(static_cast<double>(p) + q); };
    const Maths::Vec4 minus = {sub(atX, acrossX), sub(highY, acrossY), sub(atZ, acrossZ), sub(atW, acrossW)};
    const Maths::Vec4 plus = {add(acrossX, atX), add(acrossY, highY), add(acrossZ, atZ), add(acrossW, atW)};
    // Anything across it (either way): not clear.
    feeler.ChangeStrand(&plus, &minus);
    if (feeler.GetHitShape(1, 0)) {
        feeler.ReleaseObject();
        return false;
    }
    reinterpret_cast<BoxPair*>(&feeler)->Exchange();
    if (feeler.GetHitShape(1, 0)) {
        feeler.ReleaseObject();
        return false;
    }
    // Down 12 from one side: nothing there, clear.
    Maths::Vec4 top = plus;
    Maths::Vec4 bottom = {plus.x, static_cast<float>(plus.y - 12.0), plus.z, static_cast<float>(plus.w + 1.0)};
    feeler.ChangeStrand(&top, &bottom);
    if (!feeler.GetHitShape(1, 0)) {
        feeler.ReleaseObject();
        return true;
    }
    float base = static_cast<float>(highY - 6.0);
    float heightOne = static_cast<float>(static_cast<double>(feeler.m_contactPos.y) - base);
    // And from the other.
    top = minus;
    bottom = {minus.x, static_cast<float>(minus.y - 12.0), minus.z, static_cast<float>(minus.w + 1.0)};
    feeler.ChangeStrand(&top, &bottom);
    if (!feeler.GetHitShape(1, 0)) {
        feeler.Teardown();
        return true;
    }
    double heightTwo = static_cast<double>(feeler.m_contactPos.y) - base;
    // Ground on both sides: clear only if one side is at the rail's height (within 1) or 2 below it.
    if (heightOne > -1.0f && heightTwo > -1.0) { // (NaN: on to the next test)
        feeler.Teardown();
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
    feeler.Teardown();
    if (!(higher > 1.0))
        return true;
    return !(lower > -2.0);
}

// 0x0057CB20
Maths::Vec4* CGrindRailHub::ToWorld(Maths::Vec4* out, const Maths::Vec4* in)
{
    if (!m_onObject) {
        *out = *in;
        return out;
    }
    Xform(&m_objectMatrix, out, in);
    return out;
}

// 0x0057CB60
Maths::Vec4* CGrindRailHub::GetAbsolutePos(Maths::Vec4* out, const CGrindPoint* node)
{
    if (!m_onObject) {
        *out = node->m_atPoint;
        return out;
    }
    Maths::Vec4 world;
    Xform(&m_objectMatrix, &world, &node->m_atPoint);
    *out = world;
    return out;
}

// 0x0057CBD0
CGrindPoint* CGrindRailHub::FindNode(uint32_t name)
{
    if (!m_nodes)
        return nullptr;
    for (int i = 0; i < m_num; ++i) {
        if (m_nodes[i].m_label == name)
            return &m_nodes[i];
    }
    return nullptr;
}

// 0x0057CC30
int CGrindPoint::GetWallSide(const Maths::Vec4* direction)
{
    const Maths::Vec4 p = m_atPoint;
    const Maths::Vec4 q = m_succ->m_atPoint;
    Feeler feeler;
    feeler.MakeNew();
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
    Maths::Vec4 from = {static_cast<float>(static_cast<double>(acrossX) + atX),
                        static_cast<float>(static_cast<double>(acrossY) + lowY),
                        static_cast<float>(static_cast<double>(acrossZ) + atZ), static_cast<float>(dw + atW)};
    Maths::Vec4 to = {static_cast<float>(static_cast<double>(atX) - acrossX),
                      static_cast<float>(static_cast<double>(lowY) - acrossY),
                      static_cast<float>(static_cast<double>(atZ) - acrossZ),
                      static_cast<float>(static_cast<double>(atW) - dw)};
    feeler.ChangeStrand(&from, &to);
    bool hitOneWay = feeler.GetHitShape(1, 0);
    Maths::Vec4 start = feeler.m_outset;
    feeler.m_outset = feeler.m_finish;
    feeler.m_finish = start;
    bool hitOtherWay = feeler.GetHitShape(1, 0);
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
Maths::Mat4* CGrindRailHub::GetClimbMatrix(Maths::Mat4* out, const CGrindPoint* node)
{
    Maths::Mat4 local;
    reinterpret_cast<const Maths::QuatRotation*>(&node->m_climb)->FetchMat(&local); // the quaternion's
    local.row[0].w = 0.0f;
    local.row[1].w = 0.0f;
    local.row[2].w = 0.0f;
    local.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!m_onObject) {
        *out = local;
    } else {
        Maths::Mat4 world;
        Maths::Multiply(world, m_objectMatrix, local);
        *out = world;
    }
    return out;
}

// 0x0057D090
void CGrindRailHub::AppendVertex(uint32_t name, Scripts::CStruct* params)
{
    CGrindPoint* node = &m_nodes[m_adding];
    uint32_t nodeClass = 0; // (the original's are uninitialised: read only if the node has them)
    params->ReadHashValue(kClass, &nodeClass, 0);
    bool climbing = nodeClass == kClimbingNode;
    uint32_t type = 0;
    params->ReadHashValue(kType, &type, 0);
    node->m_succ = nullptr;
    node->m_lastValue = nullptr;
    node->m_label = name;
    node->m_flagBits = 0;
    if (params->HasBareFlag(kCreatedAtStart))
        node->m_flagBits |= RAIL_FLAG_ACTIVE;
    else
        node->m_flagBits &= ~RAIL_FLAG_ACTIVE;
    LevelNodes::FetchPlace(params, &node->m_atPoint);
    if (params->HasBareFlag(kLipOverride))
        node->m_flagBits |= RAIL_FLAG_LIP_OVERRIDE;
    if (params->HasBareFlag(kDefaultLine))
        node->m_flagBits |= RAIL_FLAG_DEFAULT_LINE;
    if (climbing) {
        node->m_flagBits |= RAIL_FLAG_CLIMBING;
        if (type == kLadder)
            node->m_flagBits |= RAIL_FLAG_LADDER;
        else
            node->m_flagBits &= ~RAIL_FLAG_LADDER;
    }
    auto flag = [&](uint32_t name, uint32_t bit) {
        if (params->HasBareFlag(name))
            node->m_flagBits |= bit;
        else
            node->m_flagBits &= ~bit;
    };
    flag(kHangLeft, RAIL_FLAG_HANG_LEFT);
    flag(kHangRight, RAIL_FLAG_HANG_RIGHT);
    flag(kNoClimbing, RAIL_FLAG_NO_CLIMBING);
    if (climbing) {
        // Its orientation from its angles, as a quaternion.
        Maths::Vec4 angles;
        LevelNodes::ReadTurnAngles(params, &angles);
        Maths::Mat4 matrix;
        Maths::Mat4* built = &matrix.InitFromAngles(angles);
        Maths::Vec4 quat;
        reinterpret_cast<Maths::QuatRotation*>(&quat)->FromMatrix(*built);
        node->m_climb = quat;
    } else {
        node->m_surfaceType = 0;
        uint32_t terrain = 0; // (the original's: its params argument's slot, the low byte the climbing flag)
        params->ReadHashValue(kTerrainType, &terrain, 1);
        node->m_surfaceType = static_cast<uint8_t>(SurfaceKind::GetTerrainIndex(terrain));
    }
    BoxOf(&node->m_atPoint, &node->m_atPoint, &node->m_lowerLimit, &node->m_upperLimit);
}

// 0x0057D260
void CGrindRailHub::RemoveOverlaps()
{
    DebugOut::Printf("Starting overlapping rail removal\n");
    int removed = 0;
    for (int i = 0; i < m_num; ++i) {
        CGrindPoint* a = &m_nodes[i];
        CGrindPoint* aNext = a->m_succ;
        if (!aNext)
            continue;
        const Maths::Vec4 p = a->m_atPoint;
        const Maths::Vec4 q = aNext->m_atPoint;
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
        const float loX = static_cast<float>(a->m_lowerLimit.x - 16.0), loY = static_cast<float>(a->m_lowerLimit.y - 16.0),
                    loZ = static_cast<float>(a->m_lowerLimit.z - 16.0);
        const float hiX = static_cast<float>(a->m_upperLimit.x + 16.0), hiY = static_cast<float>(a->m_upperLimit.y + 16.0),
                    hiZ = static_cast<float>(a->m_upperLimit.z + 16.0);
        for (int j = i + 1; j < m_num; ++j) {
            if (!(a->m_flagBits & RAIL_FLAG_ACTIVE))
                break;
            CGrindPoint* b = &m_nodes[j];
            CGrindPoint* bNext = b->m_succ;
            if (!bNext)
                continue;
            // (the tests as the original's: NaN passes them)
            if (b->m_lowerLimit.x > hiX || b->m_upperLimit.x < loX || b->m_lowerLimit.z > hiZ || b->m_upperLimit.z < loZ || b->m_lowerLimit.y > hiY ||
                b->m_upperLimit.y < loY)
                continue;
            const Maths::Vec4 r = b->m_atPoint;
            const Maths::Vec4 s = bNext->m_atPoint;
            Maths::Vec4 along = {static_cast<float>(static_cast<double>(s.x) - r.x),
                                 static_cast<float>(static_cast<double>(s.y) - r.y),
                                 static_cast<float>(static_cast<double>(s.z) - r.z),
                                 static_cast<float>(static_cast<double>(s.w) - r.w)};
            along.MakeUnit();
            double dot = static_cast<double>(along.y) * dy + static_cast<double>(along.x) * dx +
                         static_cast<double>(along.z) * dz;
            // Parallel (or opposite) to within about 8 degrees.
            if (!(dot < -0.99f) && !(dot > 0.99f))
                continue;
            Maths::Vec4 offset = {static_cast<float>(static_cast<double>(r.x) - p.x),
                                  static_cast<float>(static_cast<double>(r.y) - p.y),
                                  static_cast<float>(static_cast<double>(r.z) - p.z),
                                  static_cast<float>(static_cast<double>(r.w) - p.w)};
            Maths::Vec4 direction = {rawX, rawY, rawZ, static_cast<float>(static_cast<double>(q.w) - p.w)};
            direction.MakeUnit();
            Maths::Vec4 apart = offset;
            apart.FlattenOntoPlane(direction);
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
            if (a->m_atPoint.y < b->m_atPoint.y)
                a->m_flagBits &= ~RAIL_FLAG_ACTIVE;
            else
                b->m_flagBits &= ~RAIL_FLAG_ACTIVE;
            break;
        }
    }
    DebugOut::Printf("Done overlapping rail removal, removed %d\n", removed);
}

// 0x0057D7A0
void CGrindRailHub::RemoveOverlapsIfNotPark()
{
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor) + 0x40) == 0xD443A2BC) // (the park editor's level)
        return;
    RemoveOverlaps();
}

// 0x0057D7C0
void CGrindRailHub::ChangeLive(uint32_t name, int active, bool entireRail)
{
    int index = 0;
    CGrindPoint* node = m_nodes;
    for (; index < m_num; ++index, ++node) {
        if (node->m_label == name)
            break;
    }
    if (index >= m_num)
        return;
    if (entireRail) {
        // This node and every one after it.
        for (; node; node = node->m_succ) {
            if (active)
                node->m_flagBits |= RAIL_FLAG_ACTIVE;
            else
                node->m_flagBits &= ~RAIL_FLAG_ACTIVE;
        }
        return;
    }
    if (active)
        node->m_flagBits |= RAIL_FLAG_ACTIVE;
    else
        node->m_flagBits &= ~RAIL_FLAG_ACTIVE;
}

// 0x0057D840
bool CGrindRailHub::IsOperative(uint32_t name)
{
    for (int index = 0; index < m_num; ++index) {
        if (m_nodes[index].m_label == name)
            return (m_nodes[index].m_flagBits & RAIL_FLAG_ACTIVE) != 0;
    }
    return false;
}

// 0x0057D880
bool CGrindRailHub::FindLadder(const Maths::Vec4* pos, float radius, float height, bool up, void* checker, uint32_t extra,
                              CGrindPoint** found)
{
    float radiusSquared = static_cast<float>(static_cast<double>(radius) * radius);
    for (int index = m_num; index != 0;) {
        CGrindPoint* node = &m_nodes[--index];
        if (!(node->m_flagBits & RAIL_FLAG_LADDER) || !(node->m_flagBits & RAIL_FLAG_ACTIVE))
            continue;
        if (std::fabs(static_cast<double>(node->m_atPoint.x) - pos->x) > radius ||
            std::fabs(static_cast<double>(node->m_atPoint.z) - pos->z) > radius ||
            std::fabs(static_cast<double>(node->m_atPoint.y) - pos->y) > height)
            continue;
        double dx = static_cast<double>(node->m_atPoint.x) - pos->x;
        double dz = static_cast<double>(node->m_atPoint.z) - pos->z;
        if (dz * dz + dx * dx > radiusSquared)
            continue;
        CGrindPoint* other = node->m_succ ? node->m_succ : node->m_lastValue;
        Maths::Mat4 matrix;
        if (up) {
            Maths::Mat4* orientation = GetClimbMatrix(&matrix, node);
            if (static_cast<CFootMovement*>(checker)->CheckLadderBottom(&node->m_atPoint, &other->m_atPoint, orientation,
                                                                          reinterpret_cast<Record24*>(extra))) {
                *found = node;
                return true;
            }
        } else {
            Maths::Mat4* orientation = GetClimbMatrix(&matrix, other);
            if (static_cast<CFootMovement*>(checker)->CheckLadderTop(&other->m_atPoint, &node->m_atPoint, orientation,
                                                                       reinterpret_cast<Record24*>(extra))) {
                *found = other;
                return true;
            }
        }
    }
    return false;
}

// 0x0057DA20
bool CGrindRailHub::FindLadderOnLine(const Maths::Vec4* start, const Maths::Vec4* end, void* checker, Record24* result,
                                    CGrindPoint** found)
{
    *found = nullptr;
    Maths::Seg line;
    line.start = *start;
    line.end = *end;
    Maths::Vec4 min, max;
    BoxOf(&line.start, &line.end, &min, &max);
    double snap = Scripts::FetchFloat(kClimbMaxSnap);
    min.x = static_cast<float>(min.x - snap);
    float best = 1e14f;
    min.y = static_cast<float>(min.y - snap);
    min.z = static_cast<float>(min.z - snap);
    max.x = static_cast<float>(max.x + snap);
    max.y = static_cast<float>(max.y + snap);
    max.z = static_cast<float>(max.z + snap);
    float snapSquared = static_cast<float>(snap * snap);
    for (int index = 0; index < m_num; ++index) {
        CGrindPoint* node = &m_nodes[index];
        if (!(node->m_flagBits & RAIL_FLAG_LADDER) || !(node->m_flagBits & RAIL_FLAG_ACTIVE) || !node->m_succ)
            continue;
        // (the tests as the original's: NaN passes them)
        if (min.x > node->m_upperLimit.x || max.x < node->m_lowerLimit.x || min.z > node->m_upperLimit.z || max.z < node->m_lowerLimit.z ||
            min.y > node->m_upperLimit.y || max.y < node->m_lowerLimit.y)
            continue;
        CGrindPoint* next = node->m_succ;
        Maths::Seg rail;
        rail.start = node->m_atPoint;
        rail.end = next->m_atPoint;
        Maths::Vec4 onLine, onRail;
        float s, t;
        if (!Maths::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true))
            continue;
        Maths::Vec4 apart = onRail - onLine;
        float distanceSquared = apart.MagnitudeSquared();
        if (node->m_label == kLadderTrigger || next->m_label == kLadderTrigger)
            distanceSquared = static_cast<float>(distanceSquared * 4.0);
        if (distanceSquared > snapSquared || distanceSquared > best)
            continue;
        Record24 record;
        Maths::Mat4 matrix;
        if (node->m_atPoint.y < next->m_atPoint.y) {
            // Climbing up from this node.
            Maths::Mat4* orientation = GetClimbMatrix(&matrix, node);
            if (!static_cast<CFootMovement*>(checker)->CheckLadderLine(&node->m_atPoint, &next->m_atPoint, orientation, &onRail, t,
                                                                         &record))
                continue;
            *found = node;
        } else {
            Maths::Mat4* orientation = GetClimbMatrix(&matrix, next);
            if (!static_cast<CFootMovement*>(checker)->CheckLadderLine(&next->m_atPoint, &node->m_atPoint, orientation, &onRail,
                                                                         static_cast<float>(1.0 - t), &record))
                continue;
            *found = next;
        }
        best = distanceSquared;
        result->Assign(&record);
    }
    return *found != nullptr;
}

// 0x0057DDA0
bool CGrindRailHub::FindRail(const Maths::Vec4* start, const Maths::Vec4* end, const Maths::Vec4* direction, void* checker,
                            Record34* result, float margin)
{
    bool found = false;
    Maths::Seg line;
    line.start = *start;
    line.end = *end;
    Maths::Vec4 min, max;
    BoxOf(&line.start, &line.end, &min, &max);
    min.x = static_cast<float>(static_cast<double>(min.x) - margin);
    float best = 1e14f;
    min.y = static_cast<float>(static_cast<double>(min.y) - margin);
    min.z = static_cast<float>(static_cast<double>(min.z) - margin);
    max.x = static_cast<float>(static_cast<double>(max.x) + margin);
    max.y = static_cast<float>(static_cast<double>(max.y) + margin);
    max.z = static_cast<float>(static_cast<double>(max.z) + margin);
    float marginSquared = static_cast<float>(static_cast<double>(margin) * margin);
    for (int index = 0; index < m_num; ++index) {
        CGrindPoint* node = &m_nodes[index];
        if ((node->m_flagBits & RAIL_FLAG_LADDER) || (node->m_flagBits & RAIL_FLAG_NO_CLIMBING) ||
            !(node->m_flagBits & RAIL_FLAG_ACTIVE) || !node->m_succ)
            continue;
        // (the tests as the original's: NaN passes them)
        if (min.x > node->m_upperLimit.x || max.x < node->m_lowerLimit.x || min.z > node->m_upperLimit.z || max.z < node->m_lowerLimit.z ||
            min.y > node->m_upperLimit.y || max.y < node->m_lowerLimit.y)
            continue;
        CGrindPoint* next = node->m_succ;
        Maths::Seg rail;
        rail.start = node->m_atPoint;
        rail.end = next->m_atPoint;
        Maths::Vec4 onLine, onRail;
        float s, t;
        if (!Maths::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true))
            continue;
        Maths::Vec4 apart = onRail - onLine;
        if (apart.MagnitudeSquared() > marginSquared)
            continue;
        // Distance along `direction` counts a third.
        double along = static_cast<double>(apart.z) * direction->z + static_cast<double>(apart.y) * direction->y +
                       static_cast<double>(apart.x) * direction->x;
        if (along > 0.0)
            apart -= static_cast<float>(along * 0.6666667f) * *direction;
        float distanceSquared = apart.MagnitudeSquared();
        if (distanceSquared > best)
            continue;
        Maths::Vec4 nodeWorld, nextWorld;
        GetAbsolutePos(&nextWorld, next);
        GetAbsolutePos(&nodeWorld, node);
        if (!RailClear(nodeWorld, nextWorld))
            continue;
        Record34 record;
        record.words[12] = reinterpret_cast<uint32_t>(node);
        Maths::Vec4 lineWorld, railWorld, nextAt, nodeAt;
        Maths::Vec4* lineAt = ToWorld(&lineWorld, &onLine);
        Maths::Vec4* railAt = ToWorld(&railWorld, &onRail);
        Maths::Vec4* nextPos = GetAbsolutePos(&nextAt, next);
        Maths::Vec4* nodePos = GetAbsolutePos(&nodeAt, node);
        if (!static_cast<CFootMovement*>(checker)->CheckRail(nodePos, nextPos, railAt, lineAt, t, &record, false))
            continue;
        best = distanceSquared;
        result->Assign(&record);
        found = true;
    }
    return found;
}

// 0x0057E170
bool CGrindRailHub::FindJoiningRail(const CGrindPoint* from, uint32_t excludeFlags, CGrindPoint** found)
{
    for (int index = 0; index < m_num; ++index) {
        CGrindPoint* node = &m_nodes[index];
        // (the tests as the original's: NaN passes them)
        if (std::fabs(static_cast<double>(node->m_atPoint.x) - from->m_atPoint.x) > 1.0 ||
            std::fabs(static_cast<double>(node->m_atPoint.y) - from->m_atPoint.y) > 1.0 ||
            std::fabs(static_cast<double>(node->m_atPoint.z) - from->m_atPoint.z) > 1.0)
            continue;
        CGrindPoint* next = node->m_succ;
        if (!next && !node->m_lastValue)
            continue;
        if (node == from)
            continue;
        if (next && (node->m_flagBits & RAIL_FLAG_ACTIVE) && !(excludeFlags & node->m_flagBits)) {
            Maths::Vec4 nodeWorld, nextWorld;
            GetAbsolutePos(&nextWorld, next);
            GetAbsolutePos(&nodeWorld, node);
            if (RailClear(nodeWorld, nextWorld)) {
                *found = node;
                return true;
            }
        }
        CGrindPoint* prev = node->m_lastValue;
        if (prev && (prev->m_flagBits & RAIL_FLAG_ACTIVE) && !(excludeFlags & prev->m_flagBits)) {
            Maths::Vec4 prevWorld, nodeWorld;
            GetAbsolutePos(&nodeWorld, node);
            GetAbsolutePos(&prevWorld, node->m_lastValue);
            if (RailClear(prevWorld, nodeWorld)) {
                *found = node->m_lastValue;
                return true;
            }
        }
    }
    return false;
}

// 0x0057E960
void CGrindRailHub::AddFromNodeArray(Scripts::CArray* nodes)
{
    m_nodeArray = nodes;
    auto skipped = [](Scripts::CStruct* node) { // (the skate module's 0x00565BB0: a node this game leaves out)
        return GameModules::Skate::ShouldBeAbsent(node) != 0;
    };
    auto isRail = [](Scripts::CStruct* node) {
        uint32_t nodeClass = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        return nodeClass == kRailNode || nodeClass == kClimbingNode;
    };
    m_num = 0;
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
        if (!skipped(node) && isRail(node))
            ++m_num;
    }
    if (!m_num)
        return;
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_bottomUpHeap);
    m_nodes = static_cast<CGrindPoint*>(MemSys::AllocBlock(m_num * sizeof(CGrindPoint)));
    // A hash table of the nodes' names (0x4000 slots, linear probing) to their indexes.
    // NOT ORIGINAL: with 0x2000 rail nodes or more (THP8's whole town has ~18,000; past 0x4000 the original's table
    // was full and adding went round it for ever) a bigger one, 32-bit indexes, at most a quarter full. Original
    // behaviour: 0x4000 16-bit slots whatever the count.
    const bool big = m_num >= 0x2000;
    uint32_t slots = 0x4000;
    while (big && slots < static_cast<uint32_t>(m_num) * 4)
        slots <<= 1;
    const uint32_t mask = slots - 1;
    void* tableMemory = MemSys::AllocBlock(big ? slots * 4 : 0x8000);
    MemSys::Hub::Pull()->LeaveHeapScope();
    std::memset(tableMemory, 0xFF, big ? slots * 4 : 0x8000);
    auto* small = static_cast<int16_t*>(tableMemory);
    auto* wide = static_cast<int32_t*>(tableMemory);
    auto entry = [&](uint32_t slot) { return big ? wide[slot] : small[slot]; };
    m_adding = 0;
    bool allTrickObjects = Scripts::FetchIntValue(kAllRailsTrickObjects) != 0;
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
        if (skipped(node) || !isRail(node))
            continue;
        uint32_t name = LevelNodes::GetNodeName(node);
        AppendVertex(name, node);
        uint32_t slot = name & mask;
        while (entry(slot) != -1)
            slot = (slot + 1) & mask;
        if (big)
            wide[slot] = m_adding;
        else
            small[slot] = static_cast<int16_t>(m_adding);
        bool trickObject = node->HasEntryNamed(kTrickObject);
        if (allTrickObjects || trickObject) {
            // A trick object of its cluster (the park / trick object manager 0x007CE500).
            uint32_t nodeName = 0;
            node->ReadHashValue(kName, &nodeName, 1);
            uint32_t cluster = nodeName;
            node->ReadHashValue(kCluster, &cluster, 1);
            auto* trickObjects = g_trickObjectManager;
            trickObjects->AddCluster(cluster);
            trickObjects->AddObjectToCluster(nodeName, cluster);
        }
        ++m_adding;
    }
    // The links: each node's Links (by name, through the table).
    for (int i = 0; i < m_num; ++i) {
        CGrindPoint* node = &m_nodes[i];
        Scripts::CStruct* params = LevelNodes::FetchVertex(node->m_label, 1);
        Scripts::CArray* links = nullptr;
        params->FetchList(kLinks, &links, 0);
        if (!links)
            continue;
        int linkCount = static_cast<int>(links->m_extent);
        for (int l = 0; l < linkCount; ++l) {
            uint32_t link = links->FetchPart(l);
            uint32_t slot = link & mask;
            CGrindPoint* other = &m_nodes[entry(slot)];
            while (other->m_label != link) {
                slot = (slot + 1) & mask;
                other = &m_nodes[entry(slot)];
            }
            if (!node->m_succ || (other->m_flagBits & RAIL_FLAG_DEFAULT_LINE)) {
                node->m_succ = other;
                BoxOf(&node->m_atPoint, &other->m_atPoint, &node->m_lowerLimit, &node->m_upperLimit);
            }
            if (!other->m_lastValue || (node->m_flagBits & RAIL_FLAG_DEFAULT_LINE))
                other->m_lastValue = node;
        }
    }
    MemSys::FreeMalloc(tableMemory);
}

// 0x0057E2D0
bool CGrindRailHub::FindGrindRail(const Maths::Vec4* start, const Maths::Vec4* end, Maths::Vec4* point, CGrindPoint** found,
                                 const CGrindPoint* exclude, float minAlignment, int side)
{
    Maths::Seg line;
    line.start = *start;
    line.end = *end;
    Maths::Vec4 min, max;
    BoxOf(&line.start, &line.end, &min, &max);
    float snap = Scripts::FetchFloat(kRailMaxSnap);
    min.x = static_cast<float>(static_cast<double>(min.x) - snap);
    float best = 1e7f;
    CGrindPoint* bestNode = nullptr;
    bool any = false;
    min.y = static_cast<float>(static_cast<double>(min.y) - snap);
    min.z = static_cast<float>(static_cast<double>(min.z) - snap);
    max.x = static_cast<float>(static_cast<double>(max.x) + snap);
    max.y = static_cast<float>(static_cast<double>(max.y) + snap);
    max.z = static_cast<float>(static_cast<double>(max.z) + snap);
    Maths::Vec4 bestPoint;
    for (int index = 0; index < m_num; ++index) {
        CGrindPoint* node = &m_nodes[index];
        if ((node->m_flagBits & RAIL_FLAG_CLIMBING) || node == exclude || !(node->m_flagBits & RAIL_FLAG_ACTIVE))
            continue;
        // (the tests as the original's: NaN passes them)
        bool outside = min.x > node->m_upperLimit.x || max.x < node->m_lowerLimit.x || min.z > node->m_upperLimit.z ||
                       max.z < node->m_lowerLimit.z || min.y > node->m_upperLimit.y || max.y < node->m_lowerLimit.y;
        if (CGrindPoint* next = node->m_succ) {
            if (outside || !RailClear(node->m_atPoint, next->m_atPoint))
                continue;
            Maths::Seg rail;
            rail.start = node->m_atPoint;
            rail.end = next->m_atPoint;
            Maths::Vec4 onLine, onRail;
            float s, t;
            if (!Maths::ClosestPointsBetweenLines(line, rail, onLine, onRail, s, t, true))
                continue;
            float distance = (onRail - onLine).LenOf();
            // How well the rail lines up with the line (flat).
            Maths::Vec4 railDir = next->m_atPoint - node->m_atPoint;
            railDir.y = 0.0f;
            Maths::Vec4 segDirection = *end - *start;
            segDirection.y = 0.0f;
            railDir.MakeUnit();
            segDirection.MakeUnit();
            float alignment = static_cast<float>(std::fabs(
                static_cast<double>(segDirection.x) * railDir.x + static_cast<double>(segDirection.z) * railDir.z +
                static_cast<double>(segDirection.y) * railDir.y));
            if (segDirection.x == 0.0f && segDirection.z == 0.0f && alignment == 0.0f)
                alignment = 1.0f;
            float score = static_cast<float>((1.122f - static_cast<double>(alignment)) * distance);
            float reach = static_cast<float>((2.0 - alignment) * distance);
            if (side) {
                Maths::Vec4 direction = *end - *start;
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
        } else if (!node->m_lastValue) {
            // A rail of one node: its point.
            if (outside)
                continue;
            Maths::Vec4 offset = node->m_atPoint - *start;
            Maths::Vec4 direction = *end - *start;
            Maths::Vec4& unit = direction.MakeUnit();
            Maths::Vec4 along = unit;
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
            bestPoint = node->m_atPoint;
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
CGrindRailHub* CGrindRailHub::PrepareAll()
{
    m_nodes = nullptr;
    m_num = 0;
    m_onObject = false;
    m_nodeArray = nullptr;
    return this;
}

} // namespace Obj
