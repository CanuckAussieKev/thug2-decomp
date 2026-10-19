// Following a path of linked nodes (0x00469B00-0x0046AA9F). See path_follower.h.
//
// The original keeps many intermediate values on the x87 stack between instructions; where it does, they're kept
// in double here (and rounded to float where it stores them), in the original's order of operations.

#include "game/obj/path_follower.h"

#include <cmath>

#include "game/core/manager.h"
#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/mth/random.h"

namespace Obj {

namespace {

constexpr uint32_t kPathComponent = 0x0A015E17;  // the owner's component that picks links and hears of nodes

void** Vtable() { return reinterpret_cast<void**>(0x00647A84); }

CBaseComponent* PathComponent(CCompositeObject* owner) { return owner->GetComponent(kPathComponent); }
// The component's choice: a random link (true) or the first (not rewritten yet).
bool PicksRandomLink(CBaseComponent* component)
{
    return GameFunction<bool(__thiscall*)(CBaseComponent*)>(0x00423520)(component);
}
// The component told that `node` has been passed (not rewritten yet).
void NodePassed(CBaseComponent* component, uint32_t node)
{
    GameFunction<void(__thiscall*)(CBaseComponent*, uint32_t)>(0x00424020)(component, node);
}
// x, y and z scaled to `length` (0x0042BBF0, not rewritten yet).
void SetLength(Mth::Vector& v, float length)
{
    GameFunction<Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(0x0042BBF0)(&v, length);
}
// D3DXMatrixMultiply (linked library code).
void D3dxMultiply(Mth::Matrix& out, const Mth::Matrix& a, const Mth::Matrix& b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(&out, &a, &b);
}

// The length of a - b, unrounded: (z, y, then x) squared and summed.
double Distance(const Mth::Vector& a, const Mth::Vector& b)
{
    double x = static_cast<double>(a.x) - b.x;
    double y = static_cast<double>(a.y) - b.y;
    double z = static_cast<double>(a.z) - b.z;
    return std::sqrt(z * z + y * y + x * x);
}
// A vector's length, unrounded (0x004023C0's maths: x, y, then z).
double Length(const Mth::Vector& v)
{
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
}
// 0x00402530's maths, unrounded (z, y, then x).
double Dot(const Mth::Vector& a, const Mth::Vector& b)
{
    return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x;
}

} // namespace

// 0x0046A1D0
CPathFollower* CPathFollower::Construct(CCompositeObject* owner)
{
    m_owner = owner;
    m_vtable = Vtable();
    m_frame.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_frame.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_frame.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_from = {0.0f, 0.0f, 0.0f, 1.0f};
    m_unknown84 = {0.0f, 0.0f, 0.0f, 1.0f};
    m_to = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x00469B50
CPathFollower* CPathFollower::Destroy(unsigned int flags)
{
    m_vtable = Vtable();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00469B00
void CPathFollower::ReadNodePosition()
{
    SkateScript::GetPosition(SkateScript::GetNode(m_node, true), &m_to);
    if (SkateScript::GetLinkCount(m_node) == 0)
        m_flags |= FLAG_LAST_NODE;
    else
        m_flags &= ~FLAG_LAST_NODE;
}

// 0x0046A120
bool CPathFollower::NextNode()
{
    uint32_t links = SkateScript::GetLinkCount(m_node);
    if (links == 0)
        return false;
    m_lastNode = m_node;
    if (PicksRandomLink(PathComponent(m_owner)))
        m_node = SkateScript::GetLink(m_node, Mth::Rnd(static_cast<int>(links)));
    else
        m_node = SkateScript::GetLink(m_node, 0);
    m_from = m_to;
    ReadNodePosition();
    return true;
}

// 0x00469F60
void CPathFollower::UpdatePosition()
{
    m_pos = m_from;
    double dx = static_cast<double>(m_to.x) - m_from.x;
    double dy = static_cast<double>(m_to.y) - m_from.y;
    float dz = m_to.z - m_from.z;
    float dw = m_to.w - m_from.w;
    float t = static_cast<float>(static_cast<double>(m_along) / m_length);
    float sx = static_cast<float>(dx * t);
    double sy = dy * t;
    double sz = static_cast<double>(dz) * t;
    float sw = static_cast<float>(static_cast<double>(dw) * t);
    m_pos.x = static_cast<float>(static_cast<double>(sx) + m_pos.x);
    m_pos.y = static_cast<float>(sy + m_pos.y);
    m_pos.z = static_cast<float>(sz + m_pos.z);
    m_pos.w = static_cast<float>(static_cast<double>(sw) + m_pos.w);
}

// 0x0046A000
bool CPathFollower::CheckCorner()
{
    if (m_cornerDistance != 0.0f) {
        if (SkateScript::GetLinkCount(m_node) == 0)
            return false;
        Mth::Vector next;
        SkateScript::GetPosition(SkateScript::GetLinkedNode(m_node, 0), &next);
        double between = Distance(next, m_to);
        if (between < m_cornerDistance) {
            double cut = between * 0.5;
            m_cut = static_cast<float>(cut);
            float left = static_cast<float>(Distance(m_to, m_pos));
            if (cut > left)
                m_cut = left;
            m_flags |= FLAG_CORNER;
            return true;
        }
    }
    m_flags &= ~FLAG_CORNER;
    m_cut = m_cornerDistance;
    return true;
}

// 0x0046A250
bool CPathFollower::StartSegment()
{
    double along = Distance(m_pos, m_from);
    m_along = static_cast<float>(along);
    m_length = static_cast<float>(Distance(m_from, m_to));
    m_along = static_cast<float>(along + m_carry);
    m_carry = 0.0f;
    Mth::Vector& heading = m_frame.row[2];
    heading = {m_to.x - m_from.x, m_to.y - m_from.y, m_to.z - m_from.z, m_to.w - m_from.w};
    heading.Normalize();
    CheckCorner();
    return true;
}

// 0x00469B80
int CPathFollower::ComputeArc()
{
    // the corner point: as far along the next segment from the node as the position is short of it
    double dx = static_cast<double>(m_pos.x) - m_from.x;
    double dy = static_cast<double>(m_pos.y) - m_from.y;
    double dz = static_cast<double>(m_pos.z) - m_from.z;
    m_corner = m_from;
    float shortOf = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    double ex = static_cast<double>(m_to.x) - m_from.x;
    double ey = static_cast<double>(m_to.y) - m_from.y;
    float ez = m_to.z - m_from.z;
    float ew = m_to.w - m_from.w;
    double segment = std::sqrt(ex * ex + static_cast<double>(ez) * ez + ey * ey);
    double scale = shortOf / segment;
    float scalef = static_cast<float>(scale);
    float cx = static_cast<float>(ex * scale);
    double cy = ey * scalef;
    double cz = static_cast<double>(ez) * scalef;
    float cw = static_cast<float>(static_cast<double>(ew) * scalef);
    m_corner.x = static_cast<float>(static_cast<double>(cx) + m_corner.x);
    m_corner.y = static_cast<float>(cy + m_corner.y);
    m_corner.z = static_cast<float>(cz + m_corner.z);
    m_corner.w = static_cast<float>(static_cast<double>(cw) + m_corner.w);
    if (m_flags & FLAG_CORNER)
        return 0;
    if (shortOf == 0.0f)
        return 2;

    // the arc's centre: on the bisector, through the middle of position - corner
    float mx = static_cast<float>((static_cast<double>(m_corner.x) - m_pos.x) * 0.5);
    double my = (static_cast<double>(m_corner.y) - m_pos.y) * 0.5;
    double mz = static_cast<double>(m_corner.z - m_pos.z) * 0.5;
    float mw = static_cast<float>(static_cast<double>(m_corner.w - m_pos.w) * 0.5);
    float halfChord =
        static_cast<float>(std::sqrt(static_cast<double>(mx) * mx + mz * mz + my * my));
    float midX = static_cast<float>(static_cast<double>(mx) + m_pos.x);
    float midY = static_cast<float>(my + m_pos.y);
    double midZ = mz + m_pos.z;
    double midW = static_cast<double>(mw) + m_pos.w;
    float qx = static_cast<float>(static_cast<double>(midX) - m_from.x);
    float qy = static_cast<float>(static_cast<double>(midY) - m_from.y);
    float qz = static_cast<float>(midZ - m_from.z);
    double qw = midW - m_from.w;
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
    m_center += m_from;

    // the frame: from the centre to the position, its axis, and the heading
    Mth::Vector& out = m_frame.row[0];
    Mth::Vector& axis = m_frame.row[1];
    Mth::Vector& heading = m_frame.row[2];
    out = m_center - m_pos;
    heading = m_center - m_corner;
    if (Length(out) == 0.0 || Length(heading) == 0.0)
        return 2;
    out.Normalize();
    heading.Normalize();
    double angle = std::acos(Dot(out, heading));
    m_arcAngle = static_cast<float>(angle);
    m_arcLength = static_cast<float>(angle * m_radius);
    axis = Mth::CrossProduct(out, heading);
    if (Length(axis) == 0.0)
        return 2;
    axis.Normalize();
    heading = Mth::CrossProduct(out, axis);
    if (Length(heading) == 0.0)
        return 2;
    heading.Normalize();
    m_along = m_carry;
    m_carry = 0.0f;
    return 1;
}

// 0x0046A320
void CPathFollower::ArcStep(float distance)
{
    double step = static_cast<double>(distance) / m_arcLength * m_arcAngle;
    m_step = static_cast<float>(step);
    Mth::Matrix turn;
    D3dxMultiply(m_frame, Mth::CreateRotateYMatrix(turn, static_cast<float>(step)), m_frame);
    if (m_frame.row[1].y > 0.0f)
        m_step = m_step * -1.0f;
    double out = -static_cast<double>(m_radius);
    const Mth::Vector& fromCenter = m_frame.row[0];
    m_pos.z = static_cast<float>(out * fromCenter.z);
    m_pos.w = static_cast<float>(out * fromCenter.w);
    m_pos.x = static_cast<float>(out * fromCenter.x);
    m_pos.y = static_cast<float>(out * fromCenter.y);
    m_pos.x = m_pos.x + m_center.x;
    m_pos.y = m_center.y + m_pos.y;
    m_pos.z = m_center.z + m_pos.z;
    m_pos.w = m_center.w + m_pos.w;
}

// 0x0046A550
void CPathFollower::StartCorner()
{
    switch (ComputeArc()) {
    case 0:
        // no arc: blend into the next segment from here
        m_center = m_pos;
        m_along = m_carry;
        m_state = STATE_BLEND;
        m_length = static_cast<float>(Distance(m_pos, m_from));
        break;
    case 1:
        m_state = STATE_ARC;
        break;
    case 2: {
        uint32_t passed = m_lastNode;
        NodePassed(PathComponent(m_owner), passed);
        m_along = m_length - m_along;
        m_flags &= ~FLAG_TELL_NODE;
        StartSegment();
        UpdatePosition();
        m_state = STATE_STRAIGHT;
        break;
    }
    default:
        break;
    }
}

// 0x0046A3F0
void CPathFollower::Start(uint32_t node, const Mth::Vector* position)
{
    m_state = STATE_START;
    m_flags = 0;
    m_carry = 0.0f;
    m_along = 0.0f;
    m_node = node;
    m_from = *position;
    m_pos = m_from;
    ReadNodePosition();
    while (Distance(m_pos, m_to) < 1.0) {
        if (!NextNode())
            return;
    }
    Mth::Vector& heading = m_frame.row[2];
    heading = {m_to.x - m_pos.x, m_to.y - m_pos.y, m_to.z - m_pos.z, m_to.w - m_pos.w};
    heading.Normalize();
}

// 0x0046A640
bool CPathFollower::Update(float distance, const Mth::Vector* direction)
{
    if (m_state == STATE_DONE)
        return true;
    if (direction) {
        double along = Dot(m_frame.row[2], *direction);
        distance = along == 0.0 ? 1.0f : static_cast<float>(along * distance);
    }
    switch (m_state) {
    case STATE_START:
        StartSegment();
        m_step = 0.0f;
        m_state = STATE_STRAIGHT;
        [[fallthrough]];
    case STATE_STRAIGHT: {
        double along = static_cast<double>(distance) + m_along;
        m_along = static_cast<float>(along);
        if (m_flags & FLAG_LAST_NODE) {
            if (!(along >= m_length)) { // (unordered too, as the original's comparisons below)
                UpdatePosition();
                return false;
            }
            m_lastNode = m_node;
            m_from = m_to;
            m_pos = m_from;
            m_state = STATE_DONE;
            return true;
        }
        if (!(along >= static_cast<double>(m_length) - m_cut)) {
            UpdatePosition();
            return false;
        }
        if (m_cut == 0.0f) {
            m_pos = m_to;
            NodePassed(PathComponent(m_owner), m_node);
            if (NextNode()) {
                m_state = STATE_START;
                return false;
            }
            return true; // (the state isn't set to done)
        }
        UpdatePosition();
        NextNode();
        m_flags |= FLAG_TELL_NODE;
        StartCorner();
        return false;
    }
    case STATE_ARC: {
        double along = static_cast<double>(distance) + m_along;
        m_along = static_cast<float>(along);
        if ((m_flags & FLAG_TELL_NODE) && along >= static_cast<double>(m_arcLength) * 0.5) {
            uint32_t passed = m_lastNode;
            m_flags &= ~FLAG_TELL_NODE;
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
        UpdatePosition();
        m_step = 0.0f;
        m_state = STATE_STRAIGHT;
        return false;
    }
    case STATE_BLEND: {
        double along = static_cast<double>(distance) + m_along;
        m_along = static_cast<float>(along);
        float t = static_cast<float>(along / m_length);
        Mth::Vector offset;
        if (along < m_length) {
            m_pos = m_center;
            offset = m_from - m_center;
            offset *= t;
        } else {
            m_pos = m_from;
            offset = m_corner - m_from;
            offset *= static_cast<float>(static_cast<double>(t) - 1.0);
        }
        m_pos += offset;
        Mth::Vector into = m_from - m_center;
        Mth::Vector next = m_corner - m_from;
        float blend = t * 0.5f;
        if (blend >= 0.5f && (m_flags & FLAG_TELL_NODE)) {
            uint32_t passed = m_lastNode;
            m_flags &= ~FLAG_TELL_NODE;
            NodePassed(PathComponent(m_owner), passed);
        }
        if (blend >= 1.0f) {
            blend = 1.0f;
            m_carry = 0.0f;
            StartSegment();
            m_state = STATE_STRAIGHT;
        }
        Mth::Vector& heading = m_frame.row[2];
        heading = into * static_cast<float>(1.0 - blend);
        heading += next * blend;
        heading.Normalize();
        return false;
    }
    default:
        return false;
    }
}

// 0x0046A1B0
void CPathFollower::GetHeading(Mth::Vector* out)
{
    *out = m_frame.row[2];
}

} // namespace Obj

REPLACE(0x00469B00, &Obj::CPathFollower::ReadNodePosition);
REPLACE(0x00469B50, &Obj::CPathFollower::Destroy);
REPLACE(0x00469B80, &Obj::CPathFollower::ComputeArc);
REPLACE(0x00469F60, &Obj::CPathFollower::UpdatePosition);
REPLACE(0x0046A000, &Obj::CPathFollower::CheckCorner);
REPLACE(0x0046A120, &Obj::CPathFollower::NextNode);
REPLACE(0x0046A1B0, &Obj::CPathFollower::GetHeading);
REPLACE(0x0046A1D0, &Obj::CPathFollower::Construct);
REPLACE(0x0046A250, &Obj::CPathFollower::StartSegment);
REPLACE(0x0046A320, &Obj::CPathFollower::ArcStep);
REPLACE(0x0046A3F0, &Obj::CPathFollower::Start);
REPLACE(0x0046A550, &Obj::CPathFollower::StartCorner);
REPLACE(0x0046A640, &Obj::CPathFollower::Update);
