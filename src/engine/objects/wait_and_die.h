#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Struct;
} // namespace script

// The WaitAndDie component (GameObjects::WaitAndDieComponent, type 0x03AC8065 "WaitAndDie", vtable 0x00646898, 0x28 bytes;
// made by 0x0043D930): its object dies (flag_as_dead) when its LifeTime is up or, given an object to wait for, as soon
// as that object is gone.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data; the empty 0x005480A0 for Finalize, 0x004E3F90, 0x0048DCD0, the
// base's Suspend, 11 0x0045D720, 12 invoke_method_by_name 0x0051ADC0 (not mine). (Create, 0x0043D930, is outside this
// file's range: see component_types.cpp.)
namespace objects {

class WaitAndDieComponent final : public ObjectModule {
public:
    WaitAndDieComponent(const WaitAndDieComponent&) = delete;
    WaitAndDieComponent& operator=(const WaitAndDieComponent&) = delete;
    static constexpr uint32_t kType = 0x03AC8065;

    // (0x0043D930's inlined constructor: create_wait_and_die_component) Its type.
    WaitAndDieComponent() { kind_tag_ = kType; }
    // 0x0043D840 (0x0043D850 the deleting one): the base's.
    ~WaitAndDieComponent() override;
    // 0x0043D880 (vtable slot 2): the object to wait for (0x1869F559, a checksum; none: 0) - without one, the
    // LifeTime (0xC218CF77, seconds, asserted) in ms, truncated; with one the lifetime is 0. Started now.
    void configure_from_data(script::Struct* params) override;
    // 0x0043D8E0 (vtable slot 1): the time since the start kept; the object dies (vtable slot 6, flag_as_dead) when the
    // object it waits for can't be found, or - waiting for none - once the time is past the lifetime.
    void tick_frame() override;

    int outset_;        // +0x18 the game clock (with its offset) at configure_from_data
    uint32_t elapsed_;  // +0x1C ms since then (each update)
    uint32_t lifespan_; // +0x20 ms
    uint32_t wait_for_; // +0x24 the id of the object to wait for (0: none)
};

} // namespace objects
