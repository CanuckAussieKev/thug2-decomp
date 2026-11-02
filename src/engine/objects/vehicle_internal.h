#pragma once

// What the Vehicle component's source files share (vehicle_component.cpp, vehicle_physics.cpp,
// vehicle_collision.cpp): the game's strings and constants, and calls into code not rewritten yet.

#include <array>
#include <bit>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "platform/d3dx.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_rect.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/trigger_component.h"
#include "engine/core/down_cast.h"

namespace objects::vehicle {

inline const float kGravity = std::bit_cast<float>(0x43C13333u); // 0x006466A4: 386.4 (inches a second squared)

// The debug messages' format and file.
inline constexpr const char* kDebugFormat = "M:%s:%i: %s\n";             // "M:%s:%i: %s\n"
inline const char* const kDebugFile = "omponents\\vehiclecomponent.cpp"; // "omponents\vehiclecomponent.cpp"

// The trigger component's 0x00434BF0 / 0x00434CE0: a feeler / a rect feeler reported for
// `events` (const int[3]).
inline void trigger_feeler(ObjectModule* trigger, const std::array<int, 3>& events, render::ProbeRay* feeler)
{
    core::down_cast<ActivationZoneModule>(trigger)->probe_touches_zone(events, feeler);
}
inline void trigger_rect(ObjectModule* trigger, const std::array<int, 3>& events, render::BoxProbe* feeler)
{
    core::down_cast<ActivationZoneModule>(trigger)->trigger_hits(events, feeler);
}

// Quaternions: 0x0042BC40 a quaternion's rotation matrix (its 3x3), 0x0042E1F0 a matrix's
// quaternion, 0x0042E310 normalised to a length, 0x00420140 *=.
inline void quat_to_matrix(const maths::Vec4* quat, maths::Mat4* matrix) { maths::quat_to_matrix(*quat, matrix); }
inline void matrix_to_quat(maths::Vec4* quat, const maths::Mat4* matrix) { *quat = maths::quat_from_matrix(*matrix); }
inline void quat_normalize(maths::Vec4* quat, float length) { maths::quat_make_unit(quat, length); }
inline void quat_multiply(maths::Vec4* a, const maths::Vec4* b) { maths::quat_multiply(a, *b); }

// D3DXMatrixMultiply (linked library code; stdcall).
inline void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

} // namespace objects::vehicle
