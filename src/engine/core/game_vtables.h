#pragma once

// The virtual tables (and node / member tables) of the exe that several files use, by name. Each is defined once, with
// the functions it holds (a plain table of the functions), in the file given here.

namespace vt {

extern void** const kCollGridNode;       // 0x00645D7C: gfx/engine_scene_sectors.cpp
extern void** const kCollGridHead;       // 0x00645D80: gfx/engine_scene_sectors.cpp
extern void** const kBlenderChannelNode; // 0x00645D84: gfx/anim_channel.cpp
extern void** const kProjectionNode;     // 0x00646600: gfx/engine_texture_projection.cpp
extern void** const kAnimListNode;       // 0x006486CC: gfx/anim_channel.cpp
extern void** const kAnimTrack;          // 0x00648794: gfx/anim_track.cpp
extern void** const kParticleNode;       // 0x00649420: gfx/particle_system.cpp
extern void** const kParticleSystemNode; // 0x00649424 (and each type's): gfx/particle_system.cpp
extern void** const kSaveNode;           // 0x0065797C: core/alloc.cpp
extern void** const kDeviceNode;         // 0x00658A70: sio/device.cpp

} // namespace vt
