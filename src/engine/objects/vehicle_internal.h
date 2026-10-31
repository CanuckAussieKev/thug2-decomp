#pragma once

// What the Vehicle component's source files share (vehicle_component.cpp, vehicle_physics.cpp,
// vehicle_collision.cpp): the game's strings and constants, and calls into code not rewritten yet.

#include <bit>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_rect.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/trigger_component.h"

namespace GameObjects::Vehicle {

inline const float kGravity = std::bit_cast<float>(0x43C13333u); // 0x006466A4: 386.4 (inches a second squared)

// The debug messages' format and file.
inline const char* const kDebugFormat = "M:%s:%i: %s\n"; // "M:%s:%i: %s\n"
inline const char* const kDebugFile = "omponents\\vehiclecomponent.cpp";   // "omponents\vehiclecomponent.cpp"

// The trigger component's 0x00434BF0 / 0x00434CE0: a feeler / a rect feeler reported for
// `events` (const int[3]).
inline void TriggerFeeler(CObjectModule* trigger, const int* events, Engine3D::CProbeRay* feeler)
{
    static_cast<CActivationZoneModule*>(trigger)->ProbeTouchesZone(events, feeler);
}
inline void TriggerRect(CObjectModule* trigger, const int* events, Engine3D::CBoxProbe* feeler)
{
    // (TriggerHits reads the rect feeler's hit flags)
    static_cast<CActivationZoneModule*>(trigger)->TriggerHits(events, reinterpret_cast<const uint8_t*>(feeler));
}

// Quaternions: 0x0042BC40 a quaternion's rotation matrix (its 3x3), 0x0042E1F0 a matrix's
// quaternion, 0x0042E310 normalised to a length, 0x00420140 *=.
inline void QuatToMatrix(const Maths::Vec4* quat, Maths::Mat4* matrix)
{
    reinterpret_cast<const Maths::QuatRotation*>(quat)->FetchMat(matrix);
}
inline void MatrixToQuat(Maths::Vec4* quat, const Maths::Mat4* matrix)
{
    reinterpret_cast<Maths::QuatRotation*>(quat)->FromMatrix(*matrix);
}
inline void QuatNormalize(Maths::Vec4* quat, float length)
{
    reinterpret_cast<Maths::QuatRotation*>(quat)->MakeUnit(length);
}
inline void QuatMultiply(Maths::Vec4* a, const Maths::Vec4* b)
{
    *reinterpret_cast<Maths::QuatRotation*>(a) *= *reinterpret_cast<const Maths::QuatRotation*>(b);
}

// D3DXMatrixMultiply (linked library code; stdcall).
inline void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

} // namespace GameObjects::Vehicle
