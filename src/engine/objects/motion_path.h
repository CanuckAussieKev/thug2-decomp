#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/objects/motion.h"
#include "engine/script/cscript.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// MotionPathMembers::motion_flags_ (+0x18)
constexpr uint32_t kMotionFlagMoving = 0x18;   // (two bits: Obj_IsMoving / Obj_StopMoving)
constexpr uint32_t kMotionFlagOnPath = 0x10;   // following a path (the speed updated)
constexpr uint32_t kMotionFlagRotating = 0x27; // (Obj_IsRotating; Obj_StopRotating also clears 0x40)
// MotionPathMembers::path_flags_ (+0x1C)
constexpr uint32_t kMotionPathStopping = 0x8;   // Obj_StopAlongPath: slowing to a stop over a distance
constexpr uint32_t kMotionPathNoHeading = 0x10; // Obj_PathHeading off
constexpr uint32_t kMotionPathRandom = 0x200;   // Obj_RandomPathMode: a random link at each node


} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<script::MemberFunctionEntry, 49> g_motion_path_member_functions;
} // namespace objects
