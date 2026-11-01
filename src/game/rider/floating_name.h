#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Struct;
} // namespace script

// The skater's floating name component (SkaterFloatingName, type 0x125044E2, vtable 0x0064BE54, 0x1C bytes): in a
// network game, each skater's name shown above it - a screen element label (skater_name_0 ... skater_name_7, by the
// skater's object id) made by the script create_object_label, moved each frame, destroyed (destroy_object_label)
// when the skater is hidden or gone. From the disassembly of 0x00511780-0x00511B9E.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 12 the shared invoke_method_by_name 0x0051ADC0.
namespace objects {

class RiderOverheadTag final : public ObjectModule {
public:
    RiderOverheadTag(const RiderOverheadTag&) = delete;
    RiderOverheadTag& operator=(const RiderOverheadTag&) = delete;
    // 0x00511810 (registered by register_component_types): a new one (0 if out of memory). The label's id isn't set.
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderOverheadTag();
    // 0x00511AA0: the destructor body: the label destroyed (destroy_object_label, id = its id), then the base's.
    ~RiderOverheadTag() override;
    // 0x00511780 (vtable +0x08): the label's id from the skater's object id (0-7; any other id: left as it was).
    // `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x00511870 (vtable +0x04): outside a network game where names are shown (the network manager's 0x00533140)
    // the component suspends itself. A skater that is shown (Rider +0x2C4) and not hidden has its label made or
    // updated: its id, the text "\c<n><name>" (n: its team + 2 in a team game, else its object id + 2 - the colour)
    // and pos3D, 96 above the skater for this machine's player and 120 for the others; otherwise an existing label
    // is destroyed.
    void tick_frame() override;

    uint32_t label_id_{}; // +0x18 skater_name_<n>
};

} // namespace objects
