// NOT ORIGINAL - the objects' state their files share: the cutscene's camera slot, rotations, translations, fixed frame and playing flag, the event tracker and its references, and the rigid bodies' pushers, contacts, collision cache and debug drawing switches (objects_shared.h): made on first use, never destroyed.

#include "engine/objects/objects_shared.h"

#include "engine/core/no_destroy.h"

namespace objects {

ObjectsShared& objects_shared()
{
    static core::NoDestroy<ObjectsShared> state;
    return state.get();
}

} // namespace objects
