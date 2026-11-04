#pragma once

#include <cstdint>

#include "base/maths/vec4.h"
#include <gsl/pointers>

namespace objects {
class ObjectModule;
class PadSnapshot;
class GameEntity;
} // namespace objects

// The walk component's hanging, ladder and rail-climbing code (WalkHangUtil.cpp in the original: 0x0043DA20-
// 0x004425B0): its member functions are declared with the walk component (GameObjects::FootMovement, game/rider/
// walk_physics.h - the "walk_hang.cpp" block); walk_hang.cpp names the fields they use. Here: the helpers on other
// classes that live in the same code.
namespace walk {

// A composite object's movable contact component (the walk component's +0x1784, the skater core's +0x152C): what the
// skater rides on.
// 0x0043D990 (cdecl): out = a + (b - a) * t, each component rounded as the original's x87 code does. Returns out.
maths::Vec4* get_lerp(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float blend);
// 0x0043DA20 (thiscall on the walk component's pad): whether its second stick's angle is within 45 degrees of a half
// turn, the stick pushed and no down lock on it. Returns 1 / 0.
int turned_round(const objects::PadSnapshot* pad);

} // namespace walk

namespace objects {
// 0x0043D930: a component of type 0x03AC8065 (WaitAndDie): the base component made (0x28 bytes, cleared), its vtable
// 0x00646898 and its type. (Its other code, 0x0043D850-0x0043D8E0, isn't rewritten here.)
gsl::owner<ObjectModule*> create_wait_and_die_component();
} // namespace objects
