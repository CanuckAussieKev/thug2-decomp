#pragma once

namespace Obj {

// 0x004E4860: every component type (81: the skater's, peds', vehicles', cameras', the model, skeleton, animation
// blender, sound, collision, motion, input, ...) registered with the composite object manager, with the function
// that makes one and the one that registers its script member functions.
void __cdecl RegisterComponentTypes();

} // namespace Obj
