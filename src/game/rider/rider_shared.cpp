// NOT ORIGINAL - the rider files' shared state: the acid drop's hold velocity (kept for debugging: the grind reports it) (rider_shared.h): made on first use, never destroyed.

#include "game/rider/rider_shared.h"

#include "engine/core/no_destroy.h"

namespace skater {

RiderShared& rider_shared()
{
    static core::NoDestroy<RiderShared> state;
    return state.get();
}

} // namespace skater
