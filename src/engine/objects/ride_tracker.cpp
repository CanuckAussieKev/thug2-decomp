// What a point riding on a moving object is carried by. See ride_tracker.h.

#include "engine/objects/ride_tracker.h"
#include "engine/gfx/d3dx.h"

namespace GameObjects {

namespace {

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// The rotation part only: no translation (the fourth row 0, 0, 0, 1) and a zero fourth column.
void KeepRotation(Maths::Mat4& m)
{
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[0].w = 0.0f;
    m.row[1].w = 0.0f;
    m.row[2].w = 0.0f;
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void D3dMatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

} // namespace

// 0x00416E70
CRideTracker* CRideTracker::MakeNew(CGameEntity* object)
{
    m_entity.ConstructFrom(object);
    m_previousXform = object->m_basisFrame;
    m_lastPos = object->m_atPoint;
    m_carried = {0.0f, 0.0f, 0.0f, 1.0f};
    m_turned = false;
    return this;
}

// 0x00416F00
bool CRideTracker::TickFrame(const Maths::Vec4* point)
{
    CGameEntity* object = m_entity.Pull();
    if (!object)
        return false;
    const Maths::Vec4& position = object->m_atPoint;
    m_carried = {Difference(position.x, m_lastPos.x), Difference(position.y, m_lastPos.y),
                 Difference(position.z, m_lastPos.z), Difference(position.w, m_lastPos.w)};
    if (object->m_basisFrame == m_previousXform) {
        m_turned = false;
    } else {
        m_turned = true;
        // The point about the old position, turned by the orientation's change (new times the old's inverse).
        const Maths::Vec4 offset{Difference(point->x, m_lastPos.x), Difference(point->y, m_lastPos.y),
                                 Difference(point->z, m_lastPos.z), Difference(point->w, m_lastPos.w)};
        Maths::Mat4 undo = m_previousXform;
        KeepRotation(undo);
        undo.InvertOrthonormal();
        m_turn = object->m_basisFrame;
        KeepRotation(m_turn);
        D3dMatrixMultiply(&m_turn, &m_turn, &undo);
        Maths::Vec4 turned;
        m_turn.Xform(turned, offset);
        m_carried.x = static_cast<float>((static_cast<double>(turned.x) - offset.x) + m_carried.x);
        m_carried.y = static_cast<float>((static_cast<double>(turned.y) - offset.y) + m_carried.y);
        m_carried.z = Difference(turned.z, offset.z) + m_carried.z;
        m_carried.w = Difference(turned.w, offset.w) + m_carried.w;
        m_previousXform = object->m_basisFrame;
    }
    m_lastPos = object->m_atPoint;
    return true;
}

} // namespace Obj
