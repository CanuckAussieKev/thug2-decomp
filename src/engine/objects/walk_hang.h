#pragma once

namespace objects {
class ObjectModule;
class PadSnapshot;
class GameEntity;
} // namespace objects

// The walk component's hanging, ladder and rail-climbing code (WalkHangUtil.cpp in the original: 0x0043DA20-
// 0x004425B0): its member functions are declared with the walk component (GameObjects::FootMovement, game/rider/
// walk_physics.h - the "walk_hang.cpp" block); walk_hang.cpp names the fields they use. Here: the helper on the pad
// that lives in the same code. (Its interpolation, get_lerp, is engine/objects/walk_lerp.h; the WaitAndDie
// component's maker, also in this code in the original, is engine/objects/wait_and_die.h.)
namespace walk {

// 0x0043DA20 (thiscall on the walk component's pad): whether its second stick's angle is within 45 degrees of a half
// turn, the stick pushed and no down lock on it. Returns 1 / 0.
int turned_round(const objects::PadSnapshot* pad);

} // namespace walk
