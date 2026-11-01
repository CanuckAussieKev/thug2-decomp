#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace gfx {
class ViewCam;
} // namespace gfx
namespace script {
class Struct;
} // namespace script

namespace objects {

// The camera component (type 0xC4E311FA "camera", vtable 0x00645E34, 0x1C bytes; made by 0x0041B840): a gfx::ViewCam
// of its own that follows its object - its position and orientation copied into the camera at Finalize and after each
// of the object's updates (the object's orientation becoming its display matrix too).
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize; the rest the base's / shared (4 /
// 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0045D720, 12 0x0051ADC0).
class ViewpointModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0xC4E311FA;

    // 0x0041B840 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x0041B730: its type, a new camera (0xC4 bytes, gfx::ViewCam::make_new; null
    // when out of memory).
    ViewpointModule();
    // 0x0041B6D0: the camera deleted (its vtable slot 0), then the base's body.
    ~ViewpointModule() override;
    // 0x0041B7F0 (vtable slot 2): the object's priority with the composite object manager -1000 (its slot 4: updated
    // after the others). `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x0041B810 (vtable slot 3): the camera at the object's position and orientation.
    void seal() override;
    // 0x0041B8A0 (vtable slot 1): the object's orientation made its display matrix; the camera at the object's
    // position and orientation (copies taken first).
    void tick_frame() override;

    std::unique_ptr<gfx::ViewCam> view_camera_; // +0x18
};
static_assert(offsetof(ViewpointModule, view_camera_) == 0x18 && sizeof(ViewpointModule) == 0x1C,
              "layout must match the original");

} // namespace objects
