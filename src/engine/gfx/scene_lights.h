#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"

namespace lists {
class KeyMap;
}
namespace script {
class Array;
class Struct;
} // namespace script

// The engine's scene lights (Nx; light manager statics, cdecl): point lights placed by level nodes that colour the
// vertices of the sectors they reach (150 at 0x006FAF80, 0x006FAEF4 in use), each with a dynamic light of its name
// when one exists; a queue of light nodes brought up to date a few a frame (0x006FAEF8 - 0x006FAF0C); and the
// vertex colouring of other geometry (texture splats) by the lights near it.
namespace render {

struct WorldScene;
class WorldCell;

struct LevelLamp {
    maths::Vec4 at_point_;   // +0x00
    float strength_;         // +0x10 (0-1)
    float radius_;           // +0x14
    uint32_t label_;         // +0x18: the node's (its dynamic light's owner)
    uint32_t shade_;         // +0x1C: RGBA
    lists::KeyMap* sectors_; // +0x20: the sectors it reaches (keyed by their address)

    // 0x0048FF40 (the array's static constructor's callback): an empty table of 2^4 buckets, grey, nothing else
    // set. Returns this.
    LevelLamp* make_new();
    // 0x0048F1A0: a new radius (nothing when it's the same) - its dynamic light's too (1e10 inverse for 0).
    void set_reach(float radius);
    // 0x0048F200: whether it reaches `sector`.
    bool touches_sector(const WorldCell* sector);
    // 0x0048FFD0 (not a Ghidra function): the sectors forgotten (a jump to empty_table).
    void clear_sectors();
    // 0x0048FFE0: the main scene's sectors it reaches found again: each with geometry (not of the terrains
    // 0xED82767B / 0xEC6542D3) whose box's bounding sphere its radius touches (half the diagonal + its radius past the
    // distance between the centres), unless its name is in `excluded`.
    void find_sectors(script::Array* excluded);
};
static_assert(offsetof(LevelLamp, label_) == 0x18 && sizeof(LevelLamp) == 0x24, "layout must match the original");

// 0x0048F250: a sector's vertex colours made again: its own (CGeom::fetch_tints) with up to 8 lights that reach it
// added - each (radius - distance) / radius x its intensity x its colour, the sums added to the colour's RGB and
// clamped at 255.
void apply_scene_lights_to_sector(WorldCell* sector);
// 0x0048F620: `count` vertex colours from `in` to `out`, lit by up to 3 lights that reach the first position (and,
// with a `normal`, face it); unlit vertices left as they are in `out`, all copied when no light does.
void light_vertex_colours(const uint32_t* in, uint32_t* out, const maths::Vec4* positions, const maths::Vec4* normal,
                          uint32_t count);
// 0x0048FC00: the sectors the light `name` reaches coloured again (`scene` isn't used).
void apply_scene_light(WorldScene* scene, uint32_t name);
// 0x0048FCA0: the dynamic light and the scene light of `name` deleted (the last ones moved into their slots).
void delete_scene_light(uint32_t name);
// 0x0048FDA0: every scene light's sectors forgotten; none in use.
void delete_all_scene_lights();
// 0x0048FDE0: the queue let go of.
void clear_scene_light_queue();
// 0x0048FE40: `count` light nodes queued to be brought up to date with a copy of `params`, spread over `frames`
// frames.
void queue_scene_lights(const uint32_t* nodes, int count, int frames, script::Struct* params);
// 0x004904E0: the queue's share for this frame brought up to date (not while paused); let go of when it's done.
void update_scene_light_queue();
// 0x004901D0: the light `name` (a scene light, else a dynamic light) given the brightness (0x9E497FC6, in percent),
// radius (0x0999151B) and colour (0x059EA070 / 0x2F6511DE / 0x61C9354B) in `params`; a scene light's sectors
// coloured again.
void update_scene_light(uint32_t name, script::Struct* params);
// 0x00490400: a new scene light (null past 150) - its dynamic light's brightness and colour set too -, its sectors
// found and, when it's on, coloured.
LevelLamp* register_scene_light(uint32_t name, const maths::Vec4* pos, float brightness, uint32_t colour, float radius,
                                script::Array* excluded);
// 0x00490570: a level node's light: a dynamic light (unless 0xE1509E4F) and a scene light (unless 0x6F050E18) from
// its brightness (0x2689291C, 100), radius (0x833950CC, 300), colour array (0x99A9B716, white) and excluded sectors
// (0xAF3F7288).
void create_light_from_node(script::Struct* node);

extern int g_scene_light_count;
extern LevelLamp g_level_lamps[150];
} // namespace render
