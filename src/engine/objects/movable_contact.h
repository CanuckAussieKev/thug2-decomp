#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/mem.h"

namespace render {
class ProbeRay;
}

namespace objects {

struct RideTracker;

// The movable contact component (type 0x408D2AA9 "MovableContact", vtable 0x00646250, 0x74 bytes; made by 0x00427AB0):
// what a skater stands or rides on when it's a moving object - the ride tracker that carries it along with the
// object, where on the object it is, and the last object ridden. The skater's core physics keeps it (+0x152C).
//
// Vtable: 0 Destroy, 1 0x0051B970 (shared), 2 / 8 / 10 the empty 0x005BAB90, 3 / 4 / 9 the empty 0x005480A0, 5
// 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 11 the base's 0x0045D720, 12 0x0051ADC0 (shared).
class MovingSurfaceModule final : public ObjectModule {
public:
    MovingSurfaceModule(const MovingSurfaceModule&) = delete;
    MovingSurfaceModule& operator=(const MovingSurfaceModule&) = delete;
    static constexpr uint32_t kType = 0x408D2AA9;

    // 0x00427AB0 (registered with the component types): a new one (memory::alloc_zeroed; the constructor inlined: the base's,
    // its vtable and type, no ride, no stop time).
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    MovingSurfaceModule();
    // 0x004276C0: the destructor body (the base's - the ride tracker isn't freed).
    ~MovingSurfaceModule() override;

    // 0x00427700 (thiscall, RET 8; `this` unused): the moving object under `point` - a feeler from 400 over the point
    // down to 6 over it hits a movable object that's still there, and nothing is in the way back up from the point
    // to just over the hit (else the feeler starts again just under that hit; 9 tries). The hit point into `hit`;
    // null when there's none.
    GameEntity* find_contact(const maths::Vec4* point, maths::Vec4* hit);
    // 0x00427B30: the object under `pos` (null: none). Nothing under `next`: `pos` = `next`. Else, nothing two
    // frames of the object's velocity ahead of `pos`: `pos` = that point; else `pos` = the hit under it, a unit
    // up.
    GameEntity* find_contact_ahead(maths::Vec4* pos, const maths::Vec4* next);
    // 0x00427A00: the contact point in the world (relative to the last object ridden, by its orientation and
    // position; none: as kept); returns `out` (callers read it).
    maths::Vec4* get_contact_point(maths::Vec4* out);
    // 0x00427C70: no longer riding: the ride's object becomes the last ridden, the tracker freed, the time noted.
    void stop_riding();
    // 0x004FC400 (with the skater's core physics): the time since the skater stopped riding (+0x70).
    int time_since_stop();
    // 0x00427CB0: whether riding an object that's still there (one that's gone: stops riding).
    bool is_riding();
    // 0x00427CD0: riding what `feeler` hit - false (no longer riding) when that's gone; false when already riding
    // it; else a new ride tracker on it (from the manager: none made still counts) and true.
    bool start_riding(render::ProbeRay* feeler);
    // 0x0043EB80 (walk_hang.cpp): riding `object` from now: an old ride let go (its object kept as the last ridden,
    // the time noted), a new ride tracker made for it.
    void ride_on(GameEntity* object);
    // 0x00427DD0: the contact point kept relative to the object ridden (or the last one ridden): its own position
    // less the object's, through the object's inverted orientation.
    void update_contact_point();

    memory::HeapUnique<RideTracker> ride_; // +0x18
    maths::Vec4 contact_point_{}; // +0x1C in the object's frame
    std::array<uint8_t, 0x40> unknown2c_{};   // +0x2C
    GameEntity* last_object_{};   // +0x6C the object last ridden
    int stop_time_{0};            // +0x70 the game clock when it stopped riding
};

} // namespace objects
