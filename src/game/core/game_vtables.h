#pragma once

// The virtual tables (and node / member tables) of the exe that several files use, by name. Each is defined once, with
// the functions it holds (GAME_VTABLE, hook/game_code.h), in the file given here.

namespace Vt {

extern void** const kCollMulti; // 0x00645AA8: front/level_editor_scale.cpp
extern void** const kCollMovBBox; // 0x00645BC0: front/level_editor_scale.cpp
extern void** const kCollMovTri; // 0x00645CE0: front/level_editor_scale.cpp
extern void** const kCollStatic; // 0x00645C18: gfx/engine_scene.cpp
extern void** const kCollGridNode; // 0x00645D7C: gfx/engine_scene_sectors.cpp
extern void** const kCollGridHead; // 0x00645D80: gfx/engine_scene_sectors.cpp
extern void** const kBlenderChannelNode; // 0x00645D84: gfx/anim_channel.cpp
extern void** const kProjectionNode; // 0x00646600: gfx/engine_texture_projection.cpp
extern void** const kAnimListNode; // 0x006486CC: gfx/anim_channel.cpp
extern void** const kAnimTrack; // 0x00648794: gfx/anim_track.cpp
extern void** const kAnimEventBase; // 0x006487B0: gfx/anim_track.cpp
extern void** const kAnimEvent; // 0x006487BC: gfx/anim_track.cpp
extern void** const kEngineParticles; // 0x00649380: gfx/particles.cpp
extern void** const kParticleNode; // 0x00649420: gfx/particle_system.cpp
extern void** const kModelParticlesNode; // 0x0064949C: gfx/particle_system.cpp
extern void** const kModelParticles; // 0x006494A0: gfx/particle_system.cpp
extern void** const kPcGeom; // 0x0064A368: gfx/geom.cpp
extern void** const kSaveNode; // 0x0065797C: core/alloc.cpp
extern void** const kDeviceNode; // 0x00658A70: sio/device.cpp

} // namespace Vt
