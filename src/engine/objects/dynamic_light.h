#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace render {
struct DynamicLightBase;
} // namespace render
namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The dynamic light component (type 0xD8BFC9F7 - its name isn't in THUG2's scripts -, vtable 0x00645F58, 0x48 bytes;
// made by 0x0041D0F0): a dynamic light at its object's position that changes over the object's life (its wait-and-die
// component's elapsed / total time): radius, intensity and colour from their start values to their end values -
// through a middle value at a fraction of the life when one is given.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize, 12 invoke_method_by_name; the rest
// the base's / shared (4 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 9 0x005480A0, 11
// 0x0045D720).
class DynamicLightComponent final : public ObjectModule {
public:
    DynamicLightComponent(const DynamicLightComponent&) = delete;
    DynamicLightComponent& operator=(const DynamicLightComponent&) = delete;
    static constexpr uint32_t kType = 0xD8BFC9F7;

    // 0x0041D0F0 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined: the
    // base's, its vtable and type, no light; 0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    DynamicLightComponent();
    // 0x0041CBD0: the light let go (its owner id cleared) and forgotten, the wait-and-die component forgotten; then
    // the base's body.
    ~DynamicLightComponent() override;
    // 0x0041CBF0 (vtable slot 2): the start values - StartRadius, StartIntensity, StartRGBA (an array of four bytes;
    // all asserted) - are also the middle and end values unless given: midTime (the middle's fraction of the life; -1
    // none), MidRadius, MidIntensity, MidRGBA; EndRadius, EndIntensity, EndRGBA.
    void configure_from_data(script::Struct* params) override;
    // 0x0041CDA0 (vtable slot 3): the object's wait-and-die component (0x03AC8065).
    void seal() override;
    // 0x0041CDC0 (vtable slot 1): the light made the first time (0x0048FB80(1), owned by the object); then placed at
    // the object and its radius, intensity and colour (each byte truncated) interpolated by the life's fraction (from
    // the start to the end, or to / from the middle).
    void tick_frame() override;
    // 0x0041CFC0 (vtable slot 12): 0xFAAD1C11 the light's radius (radius=), 0x86E1FA56 its intensity (intensity=),
    // 0x1A0F4C50 its colour (rgba=, four bytes; read whether given or not) - each asserted; 1. Anything else: 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    render::DynamicLightBase* light_; // +0x18
    uint32_t start_color_{};          // +0x1C
    uint32_t middle_color_{};         // +0x20
    uint32_t end_color_{};            // +0x24
    float start_radius_{};            // +0x28
    float middle_radius_{};           // +0x2C
    float end_radius_{};              // +0x30
    float start_intensity_{};         // +0x34
    float middle_intensity_{};        // +0x38
    float end_intensity_{};           // +0x3C
    float middle_time_{};             // +0x40 the middle's fraction of the life (-1: none)
    uint8_t* wait_and_die_{};         // +0x44 its +0x1C elapsed, +0x20 total (unsigned)
};

} // namespace objects
