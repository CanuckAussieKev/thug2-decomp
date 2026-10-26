#pragma once

// The world's collision as a line query sees it: the static objects in the scenes' collision grids, and the list of
// movable collision objects (cars, level objects...).

#include "game/core/alloc.h"
#include "game/mth/vector.h"
#include "game/nx/coll_line.h"

namespace Obj {
class CCompositeObject;
}

namespace Nx {

class CCollCache;
class CCollObj;

// The movable collision objects (Lst::Head<CCollObj> at 0x00691978; its first node: 0x00691984).
// 0x004177B0 (cdecl): `object` onto the end of the list (a failed allocation crashes, as in the original).
// 0x00417800 (cdecl): every node holding `object` deleted.
void RegisterMovable(CCollObj* object);
void UnregisterMovable(CCollObj* object);

// 0x004106C0 (cdecl): the static collision objects against `line` (the nearest hit, or the farthest while
// 0x00691910 is set), filling `hit`; `callback` is called for every accepted face. Through `cache` when it's given
// and holds the line's box: its static objects whose boxes touch the line's box (grown by 0.5); otherwise the
// scenes' grids along the line, each object asked first whether it may touch that box (slot 13), the hit's
// scene noted. Whether anything was hit. The hit's distance starts at 1.01 (-0.01 for the farthest).
bool CollideWithStatics(const Mth::Line* line, CollisionHit* hit, CollisionHitCallback callback, CCollCache* cache);
// 0x00410930 (cdecl): the same, the farthest hit.
bool CollideWithStaticsFarthest(const Mth::Line* line, CollisionHit* hit, CollisionHitCallback callback,
                                CCollCache* cache);
// 0x004118C0 (cdecl): the movable objects the same way (the cache's movable ones, or the list's - those with
// neither flag 2 nor 4); while each is tested the hit's owner is the object's (slot 10). The owner of the last
// object hit, or null. The distance is only reset when the hit has nothing yet (a static hit stays to beat).
Obj::CCompositeObject* CollideWithMovables(const Mth::Line* line, CollisionHit* hit, CollisionHitCallback callback,
                                           CCollCache* cache);
// 0x00411AE0 (cdecl): the same, the farthest hit.
Obj::CCompositeObject* CollideWithMovablesFarthest(const Mth::Line* line, CollisionHit* hit,
                                                   CollisionHitCallback callback, CCollCache* cache);

} // namespace Nx
