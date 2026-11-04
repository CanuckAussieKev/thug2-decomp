#pragma once

// The world's collision as a line query sees it: the static objects in the scenes' collision grids, and the list of
// movable collision objects (cars, level objects...).

#include "engine/memory/arena_alloc.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_line.h"

namespace objects {
class GameEntity;
}

namespace render {

class HitMeshCache;
class HitMesh;

// The movable collision objects (lists::Foremost<HitMesh> at 0x00691978; its first node: 0x00691984).
// 0x004177B0 (cdecl): `object` onto the end of the list (a failed allocation crashes, as in the original).
// 0x00417800 (cdecl): every node holding `object` deleted.
void register_movable(HitMesh* object);
void unregister_movable(HitMesh* object);

// 0x004106C0 (cdecl): the static collision objects against `line` (the nearest hit, or the farthest while
// 0x00691910 is set), filling `hit`; `callback` is called for every accepted face. Through `cache` when it's given
// and holds the line's box: its static objects whose boxes touch the line's box (grown by 0.5); otherwise the
// scenes' grids along the line, each object asked first whether it may touch that box (slot 13), the hit's
// scene noted. Whether anything was hit. The hit's distance starts at 1.01 (-0.01 for the farthest).
bool collide_with_statics(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                          HitMeshCache* cache);
// 0x00410930 (cdecl): the same, the farthest hit.
bool collide_with_statics_farthest(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                   HitMeshCache* cache);
// 0x004118C0 (cdecl): the movable objects the same way (the cache's movable ones, or the list's - those with
// neither flag 2 nor 4); while each is tested the hit's owner is the object's (slot 10). The owner of the last
// object hit, or null. The distance is only reset when the hit has nothing yet (a static hit stays to beat).
objects::GameEntity* collide_with_movables(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                           HitMeshCache* cache);
// 0x00411AE0 (cdecl): the same, the farthest hit.
objects::GameEntity* collide_with_movables_farthest(const maths::Seg* line, CollisionHit* hit,
                                                    CollisionHitCallback callback, HitMeshCache* cache);

// The movable objects' list (its head; the first movable is its next, 0x00691984).

} // namespace render
