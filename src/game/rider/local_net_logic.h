#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace net {
struct InboundMsgCtx;
} // namespace net

// The skater's local network logic component (SkaterLocalNetLogic, type 0x7CD3E6D5, its factory 0x00513630): a local
// skater's updates to the server (message 0x23) - its position, orientation, state and controls, each sent when it
// changed (and everything every 15th frame of its client app, unless it's the server's own skater). The update
// (0x00512EA0 / 0x005131A0) carries the big-level positions, TP-R6 - engine/net/thugpro_compat.h. Vtable 0x0064BEF0:
// 0 Destroy, 1 Update, 2 the shared 0x004FBDA0, 3 Finalize, 12 the shared invoke_method_by_name 0x0051ADC0.
namespace objects {

class RiderHostSync final : public ObjectModule {
public:
    // 0x00513630 (registered by register_component_types): a new one (0 if out of memory): nothing sent yet (the state
    // and +0x35 0xFF, so the first update sends them).
    static ObjectModule* __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderHostSync();
    // 0x00512E30: the destructor body (the base's).
    ~RiderHostSync() override;
    // 0x004FBDA0 (slot 2, the cleanup component's, folded: its +0x18 cleared): no update sent yet.
    void configure_from_data(script::Struct* /*params*/) override { last_transmit_at_ = 0; }
    // 0x00512E70 (vtable +0x0C): the state and core physics components found.
    void seal() override;
    // 0x005136C0 (vtable +0x04): in a multiplayer game with a local player (the network manager's 0x00537320),
    // send_update.
    void tick_frame() override;
    // 0x005136F0 (the client's handler for message 0x4B, registered by 0x0056EB50; not a Ghidra function): someone's
    // graffiti was stolen - the message's +4 the victim's skater id, +8 the thief's, +0 the network manager's run
    // count (+0x38) it belongs to (another: kResultConsumed). The handler's data is the local skater: its victim runs
    // GraffitiStealYou with String0 = the thief's name, its thief GraffitiStealOther with the victim's.
    static int __cdecl handle_graffiti_steal(net::InboundMsgCtx* context);

    // 0x00512EA0: which fields differ from what was last sent (bits: 0-2 the position x / y / z, 3-5 the angles,
    // 0x40 the state, 0x80 the controls, 0x100 the board component's value) - all but 0x100 when the update is
    // forced. The angles that do are recorded as sent here.
    uint32_t fetch_sync_field_mask();
    // 0x005131A0: the skater's update sent to the server when its client app's frame calls for it (every 15th, or
    // when the connection's send interval has gone by since the last; always on the server): a 32-bit time stamp,
    // the 9 field bits, the changed fields; recorded as sent.
    void send_update();

    uint32_t last_transmit_at_; // +0x18 the client app's time when the last update went
    uint8_t sent_value_7c_;     // +0x1C (the core component's +0xE1)
    uint8_t pad1d_;
    int16_t sent_pos_[3];    // +0x1E (x, z in half units - y in quarters; whole units in a big level)
    int16_t sent_angles_[3]; // +0x24 (in 1/4096ths)
    uint8_t pad2a_[2];
    uint32_t sent_controls_;             // +0x2C (a bit per control the core component holds)
    uint32_t sent_value_6c_;             // +0x30 (the 0xB1D5A3E1 component's +0x18)
    uint8_t sent_state_;                 // +0x34 (the core component's +0x18)
    uint8_t sent_flag74_;                // +0x35 (+0xE0)
    uint8_t sent_flag75_;                // +0x36 (+0x1C is 1)
    uint8_t sent_flag76_;                // +0x37 (+0x118)
    uint32_t sent_value80_;              // +0x38 (the board's: +0x40's +0x22C's +0x4C)
    uint8_t* core_;                      // +0x3C the skater's core component (its state and controls)
    skater::SkaterPhysicsModule* board_; // +0x40 the core physics component
};
static_assert(offsetof(RiderHostSync, last_transmit_at_) == 0x18 && offsetof(RiderHostSync, sent_value_7c_) == 0x1C &&
                  offsetof(RiderHostSync, sent_pos_) == 0x1E && offsetof(RiderHostSync, sent_angles_) == 0x24 &&
                  offsetof(RiderHostSync, sent_controls_) == 0x2C && offsetof(RiderHostSync, sent_state_) == 0x34 &&
                  offsetof(RiderHostSync, sent_value80_) == 0x38 && offsetof(RiderHostSync, core_) == 0x3C &&
                  offsetof(RiderHostSync, board_) == 0x40,
              "layout must match the original");

} // namespace objects
