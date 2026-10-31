// Following a path of linked nodes (0x00469B00-0x0046AA9F). See path_follower.h.
//
// The original keeps many intermediate values on the x87 stack between instructions; where it does, they're kept
// in double here (and rounded to float where it stores them), in the original's order of operations.

#include "game/obj/path_follower.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/entity.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/maths/random.h"
#include "game/core/stubs.h"

namespace GameObjects {

namespace {

extern void** const kPathFollowerVtable;

constexpr uint32_t kPathComponent = 0x0A015E17;  // the owner's component that picks links and hears of nodes

void** Vtable() { return kPathFollowerVtable; }

CObjectModule* PathComponent(CGameEntity* owner) { return owner->FetchModule(kPathComponent); }
// The component's choice: a random link (true) or the first (not rewritten yet).
bool PicksRandomLink(CObjectModule* component)
{
    return reinterpret_cast<CMotionPathMembers*>(component)->PicksRandomLink();
}
// The component told that `node` has been passed (not rewritten yet).
void NodePassed(CObjectModule* component, uint32_t node)
{
    static_cast<CKinematicsModule*>(component)->ArriveAtNode(node);
}
// x, y and z scaled to `length` (0x0042BBF0, not rewritten yet).
void SetLength(Maths::Vec4& v, float length)
{
    v.SetLength(length);
}
// D3DXMatrixMultiply (linked library code).
void D3dxMultiply(Maths::Mat4& out, const Maths::Mat4& a, const Maths::Mat4& b)
{
    D3dx::MatrixMultiply(&out, &a, &b);
}

// The length of a - b, unrounded: (z, y, then x) squared and summed.
double SpanTo(const Maths::Vec4& a, const Maths::Vec4& b)
{
    double x = static_cast<double>(a.x) - b.x;
    double y = static_cast<double>(a.y) - b.y;
    double z = static_cast<double>(a.z) - b.z;
    return std::sqrt(z * z + y * y + x * x);
}
// A vector's length, unrounded (0x004023C0's maths: x, y, then z).
double LenOf(const Maths::Vec4& v)
{
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
}
// 0x00402530's maths, unrounded (z, y, then x).
double Dot(const Maths::Vec4& a, const Maths::Vec4& b)
{
    return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x;
}

} // namespace

// 0x0046A1D0
CPathFollower* CPathFollower::MakeNew(CGameEntity* owner)
{
    m_owner = owner;
    m_vtable = Vtable();
    m_tick.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_tick.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_tick.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_tick.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_startPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_unknown84 = {0.0f, 0.0f, 0.0f, 1.0f};
    m_endPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x00469B50
CPathFollower* CPathFollower::Teardown(unsigned int flags)
{
    m_vtable = Vtable();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00469B00
void CPathFollower::ReadNodePosition()
{
    LevelNodes::FetchPlace(LevelNodes::FetchVertex(m_listLink, true), &m_endPoint);
    if (LevelNodes::GetLinkCount(m_listLink) == 0)
        m_flagBits |= FLAG_LAST_NODE;
    else
        m_flagBits &= ~FLAG_LAST_NODE;
}

// 0x0046A120
bool CPathFollower::NextNode()
{
    uint32_t links = LevelNodes::GetLinkCount(m_listLink);
    if (links == 0)
        return false;
    m_lastNode = m_listLink;
    if (PicksRandomLink(PathComponent(m_owner)))
        m_listLink = LevelNodes::FetchLink(m_listLink, Maths::Rnd(static_cast<int>(links)));
    else
        m_listLink = LevelNodes::FetchLink(m_listLink, 0);
    m_startPoint = m_endPoint;
    ReadNodePosition();
    return true;
}

// 0x00469F60
void CPathFollower::RefreshPlacement()
{
    m_atPoint = m_startPoint;
    double dx = static_cast<double>(m_endPoint.x) - m_startPoint.x;
    double dy = static_cast<double>(m_endPoint.y) - m_startPoint.y;
    float dz = m_endPoint.z - m_startPoint.z;
    float dw = m_endPoint.w - m_startPoint.w;
    float t = static_cast<float>(static_cast<double>(m_along) / m_byteCount);
    float sx = static_cast<float>(dx * t);
    double sy = dy * t;
    double sz = static_cast<double>(dz) * t;
    float sw = static_cast<float>(static_cast<double>(dw) * t);
    m_atPoint.x = static_cast<float>(static_cast<double>(sx) + m_atPoint.x);
    m_atPoint.y = static_cast<float>(sy + m_atPoint.y);
    m_atPoint.z = static_cast<float>(sz + m_atPoint.z);
    m_atPoint.w = static_cast<float>(static_cast<double>(sw) + m_atPoint.w);
}

// 0x0046A000
bool CPathFollower::CheckCorner()
{
    if (m_cornerDistance != 0.0f) {
        if (LevelNodes::GetLinkCount(m_listLink) == 0)
            return false;
        Maths::Vec4 next;
        LevelNodes::FetchPlace(LevelNodes::FetchNeighbourNode(m_listLink, 0), &next);
        double between = SpanTo(next, m_endPoint);
        if (between < m_cornerDistance) {
            double cut = between * 0.5;
            m_cut = static_cast<float>(cut);
            float left = static_cast<float>(SpanTo(m_endPoint, m_atPoint));
            if (cut > left)
                m_cut = left;
            m_flagBits |= FLAG_CORNER;
            return true;
        }
    }
    m_flagBits &= ~FLAG_CORNER;
    m_cut = m_cornerDistance;
    return true;
}

// 0x0046A250
bool CPathFollower::StartSegment()
{
    double along = SpanTo(m_atPoint, m_startPoint);
    m_along = static_cast<float>(along);
    m_byteCount = static_cast<float>(SpanTo(m_startPoint, m_endPoint));
    m_along = static_cast<float>(along + m_carry);
    m_carry = 0.0f;
    Maths::Vec4& heading = m_tick.row[2];
    heading = {m_endPoint.x - m_startPoint.x, m_endPoint.y - m_startPoint.y, m_endPoint.z - m_startPoint.z, m_endPoint.w - m_startPoint.w};
    heading.MakeUnit();
    CheckCorner();
    return true;
}

// 0x00469B80
int CPathFollower::ComputeArc()
{
    // the corner point: as far along the next segment from the node as the position is short of it
    double dx = static_cast<double>(m_atPoint.x) - m_startPoint.x;
    double dy = static_cast<double>(m_atPoint.y) - m_startPoint.y;
    double dz = static_cast<double>(m_atPoint.z) - m_startPoint.z;
    m_bendPoint = m_startPoint;
    float shortOf = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    double ex = static_cast<double>(m_endPoint.x) - m_startPoint.x;
    double ey = static_cast<double>(m_endPoint.y) - m_startPoint.y;
    float ez = m_endPoint.z - m_startPoint.z;
    float ew = m_endPoint.w - m_startPoint.w;
    double segment = std::sqrt(ex * ex + static_cast<double>(ez) * ez + ey * ey);
    double scale = shortOf / segment;
    float scalef = static_cast<float>(scale);
    float cx = static_cast<float>(ex * scale);
    double cy = ey * scalef;
    double cz = static_cast<double>(ez) * scalef;
    float cw = static_cast<float>(static_cast<double>(ew) * scalef);
    m_bendPoint.x = static_cast<float>(static_cast<double>(cx) + m_bendPoint.x);
    m_bendPoint.y = static_cast<float>(cy + m_bendPoint.y);
    m_bendPoint.z = static_cast<float>(cz + m_bendPoint.z);
    m_bendPoint.w = static_cast<float>(static_cast<double>(cw) + m_bendPoint.w);
    if (m_flagBits & FLAG_CORNER)
        return 0;
    if (shortOf == 0.0f)
        return 2;

    // the arc's centre: on the bisector, through the middle of position - corner
    float mx = static_cast<float>((static_cast<double>(m_bendPoint.x) - m_atPoint.x) * 0.5);
    double my = (static_cast<double>(m_bendPoint.y) - m_atPoint.y) * 0.5;
    double mz = static_cast<double>(m_bendPoint.z - m_atPoint.z) * 0.5;
    float mw = static_cast<float>(static_cast<double>(m_bendPoint.w - m_atPoint.w) * 0.5);
    float halfChord =
        static_cast<float>(std::sqrt(static_cast<double>(mx) * mx + mz * mz + my * my));
    float midX = static_cast<float>(static_cast<double>(mx) + m_atPoint.x);
    float midY = static_cast<float>(my + m_atPoint.y);
    double midZ = mz + m_atPoint.z;
    double midW = static_cast<double>(mw) + m_atPoint.w;
    float qx = static_cast<float>(static_cast<double>(midX) - m_startPoint.x);
    float qy = static_cast<float>(static_cast<double>(midY) - m_startPoint.y);
    float qz = static_cast<float>(midZ - m_startPoint.z);
    double qw = midW - m_startPoint.w;
    double bisector = std::sqrt(static_cast<double>(qz) * qz + static_cast<double>(qy) * qy +
                                static_cast<double>(qx) * qx);
    float bisectorf = static_cast<float>(bisector);
    bool tight = 6.0 * bisector < halfChord;
    if (tight || bisector == 0.0)
        return 0;
    double radius = static_cast<double>(halfChord) / bisector * shortOf;
    m_radius = static_cast<float>(radius);
    if (radius == 0.0)
        return 2;
    m_center.w = static_cast<float>(qw);
    float toCenter = static_cast<float>(static_cast<double>(shortOf) * shortOf / bisectorf);
    m_center.x = qx;
    m_center.y = qy;
    m_center.z = qz;
    SetLength(m_center, toCenter);
    m_center += m_startPoint;

    // the frame: from the centre to the position, its axis, and the heading
    Maths::Vec4& out = m_tick.row[0];
    Maths::Vec4& axis = m_tick.row[1];
    Maths::Vec4& heading = m_tick.row[2];
    out = m_center - m_atPoint;
    heading = m_center - m_bendPoint;
    if (LenOf(out) == 0.0 || LenOf(heading) == 0.0)
        return 2;
    out.MakeUnit();
    heading.MakeUnit();
    double angle = std::acos(Dot(out, heading));
    m_arcAngle = static_cast<float>(angle);
    m_arcLength = static_cast<float>(angle * m_radius);
    axis = Maths::CrossVec(out, heading);
    if (LenOf(axis) == 0.0)
        return 2;
    axis.MakeUnit();
    heading = Maths::CrossVec(out, axis);
    if (LenOf(heading) == 0.0)
        return 2;
    heading.MakeUnit();
    m_along = m_carry;
    m_carry = 0.0f;
    return 1;
}

// 0x0046A320
void CPathFollower::ArcStep(float distance)
{
    double step = static_cast<double>(distance) / m_arcLength * m_arcAngle;
    m_step = static_cast<float>(step);
    Maths::Mat4 turn;
    D3dxMultiply(m_tick, Maths::MakeYawMatrix(turn, static_cast<float>(step)), m_tick);
    if (m_tick.row[1].y > 0.0f)
        m_step = m_step * -1.0f;
    double out = -static_cast<double>(m_radius);
    const Maths::Vec4& fromCenter = m_tick.row[0];
    m_atPoint.z = static_cast<float>(out * fromCenter.z);
    m_atPoint.w = static_cast<float>(out * fromCenter.w);
    m_atPoint.x = static_cast<float>(out * fromCenter.x);
    m_atPoint.y = static_cast<float>(out * fromCenter.y);
    m_atPoint.x = m_atPoint.x + m_center.x;
    m_atPoint.y = m_center.y + m_atPoint.y;
    m_atPoint.z = m_center.z + m_atPoint.z;
    m_atPoint.w = m_center.w + m_atPoint.w;
}

// 0x0046A550
void CPathFollower::StartCorner()
{
    switch (ComputeArc()) {
    case 0:
        // no arc: blend into the next segment from here
        m_center = m_atPoint;
        m_along = m_carry;
        m_curPhase = STATE_BLEND;
        m_byteCount = static_cast<float>(SpanTo(m_atPoint, m_startPoint));
        break;
    case 1:
        m_curPhase = STATE_ARC;
        break;
    case 2: {
        uint32_t passed = m_lastNode;
        NodePassed(PathComponent(m_owner), passed);
        m_along = m_byteCount - m_along;
        m_flagBits &= ~FLAG_TELL_NODE;
        StartSegment();
        RefreshPlacement();
        m_curPhase = STATE_STRAIGHT;
        break;
    }
    default:
        break;
    }
}

// 0x0046A3F0
void CPathFollower::Kickoff(uint32_t node, const Maths::Vec4* position)
{
    m_curPhase = STATE_START;
    m_flagBits = 0;
    m_carry = 0.0f;
    m_along = 0.0f;
    m_listLink = node;
    m_startPoint = *position;
    m_atPoint = m_startPoint;
    ReadNodePosition();
    while (SpanTo(m_atPoint, m_endPoint) < 1.0) {
        if (!NextNode())
            return;
    }
    Maths::Vec4& heading = m_tick.row[2];
    heading = {m_endPoint.x - m_atPoint.x, m_endPoint.y - m_atPoint.y, m_endPoint.z - m_atPoint.z, m_endPoint.w - m_atPoint.w};
    heading.MakeUnit();
}

// 0x0046A640
bool CPathFollower::TickFrame(float distance, const Maths::Vec4* direction)
{
    if (m_curPhase == STATE_DONE)
        return true;
    if (direction) {
        double along = Dot(m_tick.row[2], *direction);
        distance = along == 0.0 ? 1.0f : static_cast<float>(along * distance);
    }
    switch (m_curPhase) {
    case STATE_START:
        StartSegment();
        m_step = 0.0f;
        m_curPhase = STATE_STRAIGHT;
        [[fallthrough]];
    case STATE_STRAIGHT: {
        double along = static_cast<double>(distance) + m_along;
        m_along = static_cast<float>(along);
        if (m_flagBits & FLAG_LAST_NODE) {
            if (!(along >= m_byteCount)) { // (unordered too, as the original's comparisons below)
                RefreshPlacement();
                return false;
            }
            m_lastNode = m_listLink;
            m_startPoint = m_endPoint;
            m_atPoint = m_startPoint;
            m_curPhase = STATE_DONE;
            return true;
        }
        if (!(along >= static_cast<double>(m_byteCount) - m_cut)) {
            RefreshPlacement();
            return false;
        }
        if (m_cut == 0.0f) {
            m_atPoint = m_endPoint;
            NodePassed(PathComponent(m_owner), m_listLink);
            if (NextNode()) {
                m_curPhase = STATE_START;
                return false;
            }
            return true; // (the state isn't set to done)
        }
        RefreshPlacement();
        NextNode();
        m_flagBits |= FLAG_TELL_NODE;
        StartCorner();
        return false;
    }
    case STATE_ARC: {
        double along = static_cast<double>(distance) + m_along;
        m_along = static_cast<float>(along);
        if ((m_flagBits & FLAG_TELL_NODE) && along >= static_cast<double>(m_arcLength) * 0.5) {
            uint32_t passed = m_lastNode;
            m_flagBits &= ~FLAG_TELL_NODE;
            NodePassed(PathComponent(m_owner), passed);
        }
        if (!(m_along > m_arcLength)) {
            ArcStep(distance);
            return false;
        }
        double over = static_cast<double>(m_along) - m_arcLength;
        m_carry = static_cast<float>(over);
        ArcStep(static_cast<float>(static_cast<double>(distance) - over));
        m_carry = 0.0f;
        StartSegment();
        RefreshPlacement();
        m_step = 0.0f;
        m_curPhase = STATE_STRAIGHT;
        return false;
    }
    case STATE_BLEND: {
        double along = static_cast<double>(distance) + m_along;
        m_along = static_cast<float>(along);
        float t = static_cast<float>(along / m_byteCount);
        Maths::Vec4 offset;
        if (along < m_byteCount) {
            m_atPoint = m_center;
            offset = m_startPoint - m_center;
            offset *= t;
        } else {
            m_atPoint = m_startPoint;
            offset = m_bendPoint - m_startPoint;
            offset *= static_cast<float>(static_cast<double>(t) - 1.0);
        }
        m_atPoint += offset;
        Maths::Vec4 into = m_startPoint - m_center;
        Maths::Vec4 next = m_bendPoint - m_startPoint;
        float blend = t * 0.5f;
        if (blend >= 0.5f && (m_flagBits & FLAG_TELL_NODE)) {
            uint32_t passed = m_lastNode;
            m_flagBits &= ~FLAG_TELL_NODE;
            NodePassed(PathComponent(m_owner), passed);
        }
        if (blend >= 1.0f) {
            blend = 1.0f;
            m_carry = 0.0f;
            StartSegment();
            m_curPhase = STATE_STRAIGHT;
        }
        Maths::Vec4& heading = m_tick.row[2];
        heading = into * static_cast<float>(1.0 - blend);
        heading += next * blend;
        heading.MakeUnit();
        return false;
    }
    default:
        return false;
    }
}

// 0x0046A1B0
void CPathFollower::GetHeading(Maths::Vec4* out)
{
    *out = m_tick.row[2];
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kPathFollowerVtable_slots[] = {&GameObjects::CPathFollower::Teardown,
         Stubs::IgnoreArgument};
void** const kPathFollowerVtable = reinterpret_cast<void**>(kPathFollowerVtable_slots);

} // namespace
} // namespace Obj
