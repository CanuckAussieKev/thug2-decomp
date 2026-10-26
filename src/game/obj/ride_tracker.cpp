// What a point riding on a moving object is carried by. See ride_tracker.h.

#include "game/obj/ride_tracker.h"

#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// The rotation part only: no translation (the fourth row 0, 0, 0, 1) and a zero fourth column.
void KeepRotation(Mth::Matrix& m)
{
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[0].w = 0.0f;
    m.row[1].w = 0.0f;
    m.row[2].w = 0.0f;
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void D3dMatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<Mth::Matrix*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

} // namespace

// 0x00416E70
CRideTracker* CRideTracker::Construct(CCompositeObject* object)
{
    m_object.ConstructFrom(object);
    m_lastMatrix = object->m_matrix;
    m_lastPos = object->m_pos;
    m_carried = {0.0f, 0.0f, 0.0f, 1.0f};
    m_turned = false;
    return this;
}

// 0x00416F00
bool CRideTracker::Update(const Mth::Vector* point)
{
    CCompositeObject* object = m_object.Get();
    if (!object)
        return false;
    const Mth::Vector& position = object->m_pos;
    m_carried = {Difference(position.x, m_lastPos.x), Difference(position.y, m_lastPos.y),
                 Difference(position.z, m_lastPos.z), Difference(position.w, m_lastPos.w)};
    if (object->m_matrix == m_lastMatrix) {
        m_turned = false;
    } else {
        m_turned = true;
        // The point about the old position, turned by the orientation's change (new times the old's inverse).
        const Mth::Vector offset{Difference(point->x, m_lastPos.x), Difference(point->y, m_lastPos.y),
                                 Difference(point->z, m_lastPos.z), Difference(point->w, m_lastPos.w)};
        Mth::Matrix undo = m_lastMatrix;
        KeepRotation(undo);
        undo.InvertOrthonormal();
        m_turn = object->m_matrix;
        KeepRotation(m_turn);
        D3dMatrixMultiply(&m_turn, &m_turn, &undo);
        Mth::Vector turned;
        m_turn.Transform(turned, offset);
        m_carried.x = static_cast<float>((static_cast<double>(turned.x) - offset.x) + m_carried.x);
        m_carried.y = static_cast<float>((static_cast<double>(turned.y) - offset.y) + m_carried.y);
        m_carried.z = Difference(turned.z, offset.z) + m_carried.z;
        m_carried.w = Difference(turned.w, offset.w) + m_carried.w;
        m_lastMatrix = object->m_matrix;
    }
    m_lastPos = object->m_pos;
    return true;
}

} // namespace Obj

REPLACE(0x00416E70, &Obj::CRideTracker::Construct);
REPLACE(0x00416F00, &Obj::CRideTracker::Update);
