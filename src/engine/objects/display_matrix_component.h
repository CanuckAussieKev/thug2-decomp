#pragma once

#include <cstdint>

#include "engine/objects/entity.h"

namespace objects {

// A component that shows its object as it is turned: after each of the object's updates its display matrix is its
// orientation (type 0x1BBF844D - the name isn't known; vtable 0x00646464, 0x18 bytes, nothing of its own; made by
// 0x004312A0). Vtable: 0 the scalar deleting destructor; 1 Update; 2 / 8 the empty 0x005BAB90; 3 / 4 / 9 the empty
// 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 10 0x005144C0; 11 the base's member
// function table entry 0x0045D720; 12 invoke_method_by_name 0x0051ADC0 (none of its own).
class FaceHeadingModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x1BBF844D;

    // 0x004312A0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    FaceHeadingModule();
    // 0x00431240: the destructor body (the base's).
    ~FaceHeadingModule() override;
    // 0x00431280 (vtable slot 1): the object's display matrix = its orientation.
    void tick_frame() override;
};

} // namespace objects
