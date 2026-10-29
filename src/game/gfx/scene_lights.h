#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Lst {
class HashTable;
}
namespace Script {
class CArray;
class CStruct;
}

// The engine's scene lights (Nx; light manager statics, cdecl): point lights placed by level nodes that colour the
// vertices of the sectors they reach (150 at 0x006FAF80, 0x006FAEF4 in use), each with a dynamic light of its name
// when one exists; a queue of light nodes brought up to date a few a frame (0x006FAEF8 - 0x006FAF0C); and the
// vertex colouring of other geometry (texture splats) by the lights near it.
namespace Nx {

struct CScene;
class CSector;

struct sSceneLight {
    Mth::Vector m_pos;            // +0x00
    float m_intensity;            // +0x10 (0-1)
    float m_radius;               // +0x14
    uint32_t m_name;              // +0x18: the node's (its dynamic light's owner)
    uint32_t m_colour;            // +0x1C: RGBA
    Lst::HashTable* mp_sectors;   // +0x20: the sectors it reaches (keyed by their address)

    // 0x0048FF40 (the array's static constructor's callback): an empty table of 2^4 buckets, grey, nothing else
    // set. Returns this.
    sSceneLight* Construct();
    // 0x0048F1A0: a new radius (nothing when it's the same) - its dynamic light's too (1e10 inverse for 0).
    void SetRadius(float radius);
    // 0x0048F200: whether it reaches `sector`.
    bool AffectsSector(const CSector* sector);
    // 0x0048FFD0 (not a Ghidra function): the sectors forgotten (a jump to FlushAllItems).
    void ClearSectors();
    // 0x0048FFE0: the main scene's sectors it reaches found again: each with geometry (not of the terrains
    // 0xED82767B / 0xEC6542D3) whose box's bounding sphere its radius touches (half the diagonal + its radius past the
    // distance between the centres), unless its name is in `excluded`.
    void FindSectors(Script::CArray* excluded);
};
static_assert(offsetof(sSceneLight, m_name) == 0x18 && sizeof(sSceneLight) == 0x24, "layout must match the original");

// 0x0048F250: a sector's vertex colours made again: its own (CGeom::GetColors) with up to 8 lights that reach it
// added - each (radius - distance) / radius x its intensity x its colour, the sums added to the colour's RGB and
// clamped at 255.
void __cdecl ApplySceneLightsToSector(CSector* sector);
// 0x0048F620: `count` vertex colours from `in` to `out`, lit by up to 3 lights that reach the first position (and,
// with a `normal`, face it); unlit vertices left as they are in `out`, all copied when no light does.
void __cdecl LightVertexColours(const uint32_t* in, uint32_t* out, const Mth::Vector* positions,
                                const Mth::Vector* normal, uint32_t count);
// 0x0048FC00: the sectors the light `name` reaches coloured again (`scene` isn't used).
void __cdecl ApplySceneLight(CScene* scene, uint32_t name);
// 0x0048FCA0: the dynamic light and the scene light of `name` deleted (the last ones moved into their slots).
void __cdecl DeleteSceneLight(uint32_t name);
// 0x0048FDA0: every scene light's sectors forgotten; none in use.
void __cdecl DeleteAllSceneLights();
// 0x0048FDE0: the queue let go of.
void __cdecl ClearSceneLightQueue();
// 0x0048FE40: `count` light nodes queued to be brought up to date with a copy of `params`, spread over `frames`
// frames.
void __cdecl QueueSceneLights(const uint32_t* nodes, int count, int frames, Script::CStruct* params);
// 0x004904E0: the queue's share for this frame brought up to date (not while paused); let go of when it's done.
void __cdecl UpdateSceneLightQueue();
// 0x004901D0: the light `name` (a scene light, else a dynamic light) given the brightness (0x9E497FC6, in percent),
// radius (0x0999151B) and colour (0x059EA070 / 0x2F6511DE / 0x61C9354B) in `params`; a scene light's sectors
// coloured again.
void __cdecl UpdateSceneLight(uint32_t name, Script::CStruct* params);
// 0x00490400: a new scene light (null past 150) - its dynamic light's brightness and colour set too -, its sectors
// found and, when it's on, coloured.
sSceneLight* __cdecl AddSceneLight(uint32_t name, const Mth::Vector* pos, float brightness, uint32_t colour,
                                   float radius, Script::CArray* excluded);
// 0x00490570: a level node's light: a dynamic light (unless 0xE1509E4F) and a scene light (unless 0x6F050E18) from
// its brightness (0x2689291C, 100), radius (0x833950CC, 300), colour array (0x99A9B716, white) and excluded sectors
// (0xAF3F7288).
void __cdecl CreateLightFromNode(Script::CStruct* node);

} // namespace Nx
