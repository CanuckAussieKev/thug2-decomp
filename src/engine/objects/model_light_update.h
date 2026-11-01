#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Struct;
} // namespace script

namespace objects {

class MeshInstanceModule;

// MeshLightingModule::light_flags_ (+0x18)
constexpr uint32_t kModelLightOnce = 0x1;    // (flag 0xE7375C4C) sampled once, then the component suspends itself
constexpr uint32_t kModelLightSampled = 0x2; // the first sample taken

// The model light update component (type 0xE3FCA939 "modellightupdate", vtable 0x00646198, 0x2C bytes; made by
// 0x00422DC0): an object's model lit by the ground under it - the brightness of the collision face below (two feelers:
// down from just above the object, nudged a thousandth along its up vector, then again from the hit) given to the model
// component (apply_brightness), every `interval` frames (staggered by the component's address), its model's lights lerped
// towards it in between.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize; the rest the base's / shared.
class MeshLightingModule final : public ObjectModule {
public:
    MeshLightingModule(const MeshLightingModule&) = delete;
    MeshLightingModule& operator=(const MeshLightingModule&) = delete;
    static constexpr uint32_t kType = 0xE3FCA939;

    // 0x00422DC0 (registered with the composite object manager): a new one (the constructor inlined: the base's, its
    // vtable, no flags, interval or brightness, its type; 0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    MeshLightingModule();
    // 0x00422D00: the base's.
    ~MeshLightingModule() override;
    // 0x00422D40 (vtable slot 2): kModelLightOnce from flag 0xE7375C4C; the interval from 0x470B8AE9 (default 0: every
    // frame), the counter starting at (this component's address % interval) + 1 - spreading objects' samples over the
    // frames (an interval of 0 given: a division by zero, as the original).
    void configure_from_data(script::Struct* params) override;
    // 0x00422DA0 (vtable slot 3): its object's model component.
    void seal() override;
    // 0x004230B0 (vtable slot 1): the first time, a sample (set at once); then, until the counter passes the
    // interval, the lights lerped towards the brightness (by 0.02); once it does, back to 0 and a sample (lerped) -
    // or, kModelLightOnce, the component suspended.
    void tick_frame() override;
    // 0x00422E40: the brightness of the face under the object given to the model component (lerped by `rate` with
    // `lerp`) and kept; nothing hit: nothing.
    void get_sample(bool lerp, float rate);

    uint32_t light_flags_{0};             // +0x18 MODEL_LIGHT_*
    uint32_t gap_time_{0};                // +0x1C 0x470B8AE9 (frames)
    uint32_t counter_{0};                 // +0x20
    float brightness_{0.0f};              // +0x24 the last sample's
    MeshInstanceModule* mesh_instance_{}; // +0x28
};

} // namespace objects
