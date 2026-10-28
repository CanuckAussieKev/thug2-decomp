#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

// The engine's flexible particle manager (Nx; the one at 0x006FA7C0 - the flexible particle component's systems are
// made, found and destroyed through it): its systems in a list, on a heap of their own (or a temporary one a
// cutscene sets up), drawn far to near; the level's colour passed on to the systems that take it.
namespace Nx {

struct CParticleSystem;

// (0x28 bytes, vtable 0x006494B0: 0 the scalar deleting destructor.)
struct CFlexibleParticleManager {
    void** m_vtable;
    Mem::Region* mp_region;     // +0x04: its own memory (the script integer 0x91CB7552 bytes, else 0x5000)
    Mem::Heap* mp_heap;         // +0x08 "FlxParticles"
    Mem::Region* mp_tempRegion; // +0x0C
    Mem::Heap* mp_tempHeap;     // +0x10 "TEMPFlxPrtcl": systems are made on it while it's there
    Mem::ListNode* mp_systems;  // +0x14: the list's head (Lst::Head<CParticleSystem>, vtable 0x006494C4)
    float m_colourScale[4];     // +0x18: the level's colour (given to the systems with flag 0x10000)

    // 0x0049AF20: its memory, the list, the colour white; the frame time scale (0x00700EF4) 60 / the script float
    // 0x3214C818 - 60 when that's 0. Returns this.
    CFlexibleParticleManager* Construct();
    // 0x0049AA10: the list's head deleted, the temporary heap, the heap and the region.
    void DestroyBody();
    // 0x0049B040 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    CFlexibleParticleManager* Destroy(unsigned int flags);
    // 0x0049A9D0: the temporary heap (and its region) gone, if there is one.
    void DeleteTempHeap();
    // 0x0049AA80: a temporary heap of `size` bytes (any old one gone first). Whether it was made.
    bool CreateTempHeap(uint32_t size);
    // 0x0049A8E0 (not a Ghidra function): whether there's a temporary heap.
    bool HasTempHeap();
    // 0x0049AB50: the systems that want drawing (+0x12A) drawn far to near (no more than 256 - the original doesn't
    // check), each with RenderIfVisible(`force` or the game paused).
    void RenderSystems(bool force);
    // 0x0049AC20: the system named `name` (the first), or null.
    CParticleSystem* FindSystem(uint32_t name);
    // 0x0049AC60: a new system (CreateFlexibleParticles' arguments, on the temporary heap if there is one, else its
    // own), unless it couldn't allocate everything (then destroyed: null): `perm` (not 0) kept by
    // DestroyNonPersistent, `useLevelColour` given the level's colour; updating, not emitting, added at the list's end.
    CParticleSystem* CreateSystem(uint32_t name, uint32_t type, int maxParticles, int maxStreams, uint32_t texture,
                                  uint32_t blendMode, int fixedAlpha, int segments, float unknown, int history, int perm,
                                  bool useLevelColour, const char* model);
    // 0x0049AD60: `system` taken out of the list (null: nothing).
    void RemoveSystem(CParticleSystem* system);
    // 0x0049AD90: the system named `name` destroyed (if there's one).
    void DestroySystemNow(uint32_t name);
    // 0x0049ADB0: the system named `name` left to die (no more emission; destroyed once empty).
    void DestroySystemWhenEmpty(uint32_t name);
    // 0x0049ADE0: every system of group `group` destroyed.
    void DestroyGroup(uint32_t group);
    // 0x0049AE30: every system not made persistent destroyed.
    void DestroyNonPersistent();
    // 0x0049AE80: the level's colour: given to every system that takes it, and kept.
    void SetColourScale(const float* rgba);

    // 0x0049A8F0 (cdecl; qsort's): the order of two systems (pointers to them) - the farther from viewport 0x006FA7B8's
    // camera first (the difference of the squared distances, truncated), else the higher priority (+0x124) first.
    static int __cdecl CompareDistance(const void* a, const void* b);
};
static_assert(offsetof(CFlexibleParticleManager, mp_systems) == 0x14 &&
                  offsetof(CFlexibleParticleManager, m_colourScale) == 0x18 &&
                  sizeof(CFlexibleParticleManager) == 0x28,
              "layout must match the original");

// 0x0049AEF0 (the head's vtable 0x006494C4, slot 0): the list's head (Lst::Head<CParticleSystem>) destroyed - the
// node's body 0x00496D10 -, freed with flags & 1. Returns it.
struct CParticleSystemListHead {
    Mem::ListNode* Destroy(unsigned int flags);
};

} // namespace Nx
