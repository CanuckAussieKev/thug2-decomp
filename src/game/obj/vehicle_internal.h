#pragma once

// What the Vehicle component's source files share (vehicle_component.cpp, vehicle_physics.cpp,
// vehicle_collision.cpp): the game's strings and constants, and calls into code not rewritten yet.

#include <bit>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/gfx/d3dx.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/nx/coll_rect.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/trigger_component.h"

namespace GameObjects::Vehicle {

inline const float kGravity = std::bit_cast<float>(0x43C13333u); // 0x006466A4: 386.4 (inches a second squared)

// The debug messages' format and file.
inline const char* const kDebugFormat = "M:%s:%i: %s\n"; // "M:%s:%i: %s\n"
inline const char* const kDebugFile = "omponents\\vehiclecomponent.cpp";   // "omponents\vehiclecomponent.cpp"

// The trigger component's 0x00434BF0 / 0x00434CE0: a feeler / a rect feeler reported for
// `events` (const int[3]).
inline void TriggerFeeler(CObjectModule* trigger, const int* events, Engine3D::CProbeRay* feeler)
{
    static_cast<CActivationZoneModule*>(trigger)->CheckFeeler(events, feeler);
}
inline void TriggerRect(CObjectModule* trigger, const int* events, Engine3D::CBoxProbe* feeler)
{
    // (TriggerHits reads the rect feeler's hit flags)
    static_cast<CActivationZoneModule*>(trigger)->TriggerHits(events, reinterpret_cast<const uint8_t*>(feeler));
}

// Quaternions: 0x0042BC40 a quaternion's rotation matrix (its 3x3), 0x0042E1F0 a matrix's
// quaternion, 0x0042E310 normalised to a length, 0x00420140 *=.
inline void QuatToMatrix(const Maths::Vector* quat, Maths::Matrix* matrix)
{
    reinterpret_cast<const Maths::QuatRotation*>(quat)->GetMatrix(matrix);
}
inline void MatrixToQuat(Maths::Vector* quat, const Maths::Matrix* matrix)
{
    reinterpret_cast<Maths::QuatRotation*>(quat)->FromMatrix(*matrix);
}
inline void QuatNormalize(Maths::Vector* quat, float length)
{
    reinterpret_cast<Maths::QuatRotation*>(quat)->Normalize(length);
}
inline void QuatMultiply(Maths::Vector* a, const Maths::Vector* b)
{
    *reinterpret_cast<Maths::QuatRotation*>(a) *= *reinterpret_cast<const Maths::QuatRotation*>(b);
}

// D3DXMatrixMultiply (linked library code; stdcall).
inline void MatrixMultiply(Maths::Matrix* out, const Maths::Matrix* a, const Maths::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

} // namespace GameObjects::Vehicle
