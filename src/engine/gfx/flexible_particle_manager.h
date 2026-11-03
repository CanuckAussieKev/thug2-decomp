#pragma once

#include <cstddef>
#include <cstdint>

#include <gsl/pointers>
#include <array>
#include <span>
#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"

// The engine's flexible particle manager (Nx; the one at 0x006FA7C0 - the flexible particle component's systems are
// made, found and destroyed through it): its systems in a list, on a heap of their own (or a temporary one a
// cutscene sets up), drawn far to near; the level's colour passed on to the systems that take it.
namespace render {

struct EmitterInstance;

// (0x28 bytes in the original, with a vtable 0x006494B0 holding only its destructor; one, made at start-up and never
// deleted: no vtable here, 0x24 bytes.)
struct FlexibleParticleManager : memory::HeapObject {
private:
    gsl::owner<memory::Zone*>
        zone_; // (a memory manager region: deleted after its heap) its own memory (the script integer 0x91CB7552 bytes, else 0x5000)
    memory::Arena* arena_;                                 // "FlxParticles"
    gsl::owner<memory::Zone*> temp_region_; // (likewise, after the temporary heap)
    memory::Arena* temp_heap_;                             // "TEMPFlxPrtcl": systems are made on it while it's there
    memory::ListNode* systems_ptr_;            // the list's head (lists::Foremost<EmitterInstance>)
    std::array<float, 4> colour_scale_{};                  // the level's colour (given to the systems with flag 0x10000)

public:

    // 0x0049AF20: its memory, the list, the colour white; the frame time scale (0x00700EF4) 60 / the script float
    // 0x3214C818 - 60 when that's 0.
    FlexibleParticleManager();
    // 0x0049AA10 (0x0049B040 the scalar deleting destructor): the list's head deleted, the temporary heap, the heap
    // and the region.
    ~FlexibleParticleManager();
    FlexibleParticleManager(const FlexibleParticleManager&) = delete;
    FlexibleParticleManager& operator=(const FlexibleParticleManager&) = delete;
    // 0x0049A9D0: the temporary heap (and its region) gone, if there is one.
    void delete_temp_heap();
    // 0x0049AA80: a temporary heap of `size` bytes (any old one gone first). Whether it was made.
    bool create_temp_heap(uint32_t size);
    // 0x0049A8E0 (not a Ghidra function): whether there's a temporary heap.
    bool has_temp_heap();
    // 0x0049AB50: the systems that want drawing (+0x12A) drawn far to near (no more than 256 - the original doesn't
    // check), each with render_if_visible(`force` or the game paused).
    void render_systems(bool force);
    // 0x0049AC20: the system named `name` (the first), or null.
    EmitterInstance* find_system(uint32_t name);
    // 0x0049AC60: a new system (create_flexible_particles' arguments, on the temporary heap if there is one, else its
    // own), unless it couldn't allocate everything (then destroyed: null): `perm` (not 0) kept by
    // destroy_non_persistent, `use_level_colour` given the level's colour; updating, not emitting, added at the list's end.
    EmitterInstance* start_subsystem(uint32_t name, uint32_t type, int particle_cap, int max_streams, uint32_t texture,
                                     uint32_t blend_mode, int fixed_alpha, int segments, float unknown, int history,
                                     bool perm, bool use_level_colour, const char* model);
    // 0x0049AD60: `system` taken out of the list (null: nothing).
    void remove_system(EmitterInstance* system);
    // 0x0049AD90: the system named `name` destroyed (if there's one).
    void destroy_system_now(uint32_t name);
    // 0x0049ADB0: the system named `name` left to die (no more emission; destroyed once empty).
    void destroy_system_when_empty(uint32_t name);
    // 0x0049ADE0: every system of group `group` destroyed.
    void destroy_group(uint32_t group);
    // 0x0049AE30: every system not made persistent destroyed.
    void destroy_non_persistent();
    // 0x0049AE80: the level's colour: given to every system that takes it, and kept.
    void set_colour_scale(std::span<const float, 4> rgba);

    // 0x0049A8F0 (cdecl; qsort's): the order of two systems (pointers to them) - the farther from viewport 0x006FA7B8's
    // camera first (the difference of the squared distances, truncated), else the higher priority (+0x124) first.
    static int compare_distance(const void* a, const void* b);
};
// (0x0049AEF0, the list head's scalar deleting destructor - the node's body 0x00496D10, freed - is
// memory::ListNode::destroy.)

} // namespace render
