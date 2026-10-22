#pragma once

// What the Vehicle component's source files share (vehicle_component.cpp, vehicle_physics.cpp,
// vehicle_collision.cpp): the game's strings and constants, and calls into code not rewritten yet.

#include <bit>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_rect.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "hook/game.h"

namespace Obj::Vehicle {

inline const float kGravity = std::bit_cast<float>(0x43C13333u); // 0x006466A4: 386.4 (inches a second squared)

// The debug messages' format and file.
inline const char* const kDebugFormat = reinterpret_cast<const char*>(0x006466B0); // "M:%s:%i: %s\n"
inline const char* const kDebugFile = reinterpret_cast<const char*>(0x006466CC);   // "omponents\vehiclecomponent.cpp"

// The trigger component's 0x00434BF0 / 0x00434CE0 (not rewritten yet): a feeler / a rect feeler reported for
// `events` (const int[3]).
inline void TriggerFeeler(CBaseComponent* trigger, const int* events, Nx::CFeeler* feeler)
{
    GameFunction<void(__thiscall*)(CBaseComponent*, const int*, Nx::CFeeler*)>(0x00434BF0)(trigger, events, feeler);
}
inline void TriggerRect(CBaseComponent* trigger, const int* events, Nx::CRectFeeler* feeler)
{
    GameFunction<void(__thiscall*)(CBaseComponent*, const int*, Nx::CRectFeeler*)>(0x00434CE0)(trigger, events, feeler);
}

// Quaternions (not rewritten yet): 0x0042BC40 a quaternion's rotation matrix (its 3x3), 0x0042E1F0 a matrix's
// quaternion, 0x0042E310 normalised to a length, 0x00420140 *=.
inline void QuatToMatrix(const Mth::Vector* quat, Mth::Matrix* matrix)
{
    GameFunction<void(__thiscall*)(const Mth::Vector*, Mth::Matrix*)>(0x0042BC40)(quat, matrix);
}
inline void MatrixToQuat(Mth::Vector* quat, const Mth::Matrix* matrix)
{
    GameFunction<Mth::Vector*(__thiscall*)(Mth::Vector*, const Mth::Matrix*)>(0x0042E1F0)(quat, matrix);
}
inline void QuatNormalize(Mth::Vector* quat, float length)
{
    GameFunction<Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(0x0042E310)(quat, length);
}
inline void QuatMultiply(Mth::Vector* a, const Mth::Vector* b)
{
    GameFunction<void(__thiscall*)(Mth::Vector*, const Mth::Vector*)>(0x00420140)(a, b);
}

// D3DXMatrixMultiply (linked library code; stdcall).
inline void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}

} // namespace Obj::Vehicle
