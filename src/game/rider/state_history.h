#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "game/rider/skater_state.h"
#include "game/rider/skater_state.h"
#include "engine/core/owner.h"

namespace net {
struct InboundMsgCtx;
}

// The skater's state history component (SkaterStateHistory, type 0x18223FD6, vtable 0x0064C174, 0x1108 bytes; made by
// 0x0051AD30): the last 20 states a non-local skater was sent (+0x1C, as skate_net.cpp reads them) and the last 20
// messages of its other kinds (+0xA70), with their time stamps - and, in network games, the skater-against-skater
// collisions: each frame the skate module checks every pair of skaters (0x00566BE0), and a hit tells both players.
//
// Vtable: 0 the scalar deleting destructor (0x00519840); Update 0x0051B970 (shared: suspends the component), the
// empty 0x005BAB90 / 0x005480A0, the base's Suspend and member function entry, invoke_method_by_name 0x0051ADC0 (shared:
// "not mine").
namespace objects {

// One of the history's other messages (0x54 bytes, 20 of them at +0xA70; the newest at +0xA6C % 20): the message's
// id, its time stamp as two halves, then what the message said (engine/net/skater_messages.h; filled by
// SkateNet::handle_skater_messages, played by RiderRemoteSync::play_history_events). Each kind uses its own fields.
struct StateHistoryMessage {
    uint8_t ident_;      // +0x00 the network message's id (0x29-0x8C: 0x00516180's switch)
    uint8_t object_id_;  // +0x01 the skater's object id (0x2A / 0x2C pass it on as the rotation's / flip's object)
    uint8_t loop_type_;  // +0x02 0x29 / 0x3D
    uint8_t spin_axes_;  // +0x03 0x76: the axes that spin (bit 0 x...)
    uint16_t clock_;     // +0x04 the low half of the 32-bit time stamp
    uint16_t time_high_; // +0x06 the high half
    bool flipped_;       // +0x08 0x2C
    bool rotated_;       // +0x09 0x2A
    bool hidden_;        // +0x0A 0x3E
    uint8_t pad0b;
    float wobble_target_;     // +0x0C 0x2B
    float from_;              // +0x10 0x29
    float to_;                // +0x14 0x29
    float blend_period_;      // +0x18 0x29
    float speed_;             // +0x1C 0x29 / 0x41
    uint32_t name_;           // +0x20 0x29 the animation / 0x3E the geom / 0x77 the item's params structure
    uint32_t bone_;           // +0x24 0x77
    float wobble_details_[5]; // +0x28 0x3C
    int spin_period_;         // +0x3C 0x76
    int spin_power_;          // +0x40 0x76
    int item_slot_;           // +0x44 0x77 / 0x78
    float spin_start_;        // +0x48 0x76
    float spin_range_;        // +0x4C 0x76
    bool spin_hold_;          // +0x50 0x76
    uint8_t pad51[3];

    // 0x00519800: the 32-bit time stamp.
    uint32_t get_time();
    // 0x00519810: the time stamp set (its two halves).
    void change_moment(uint32_t time);
};

class RiderFrameLog final : public ObjectModule {
public:
    RiderFrameLog(const RiderFrameLog&) = delete;
    RiderFrameLog& operator=(const RiderFrameLog&) = delete;
    static constexpr uint32_t kType = 0x18223FD6;
    static constexpr int kEntries = 20;

    // 0x0051AD30 (registered with the composite object manager): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // 0x0051ACB0: its type, every entry and the counts cleared.
    RiderFrameLog();
    // 0x005197C0: the destructor body (the base's; in skater_state.cpp).
    ~RiderFrameLog() override;

    // 0x00519FA0: how far the object moves this frame: its velocity times the frame's length (w from the unrounded
    // frame length, x / y / z from it rounded).
    void get_frame_movement(maths::Vec4* out);
    // 0x00519FF0: the object's position.
    void fetch_place(maths::Vec4* out);
    // 0x0051A020: how far ahead of a skater a collision is looked for: the script float 0x25F78481 (0x0D24273B when
    // the object's +0xBC is set), with `driving` times 1 + the script float 0x0A49FF23. Unrounded (x87).
    double get_collision_reach(bool driving);
    // 0x0051A080: how close two skaters must come to collide: one of four script floats (the object's +0xBC and the
    // network manager's flag 0x4 pick it), times 1 + 0xEEF4AFB3's number for each of the two that is driving.
    double fetch_hit_radius(bool my_driving, bool their_driving);
    // 0x0051A150: whether the object is within 96 of the network manager's local skater (+0x6C).
    bool is_near_local_skater();
    // 0x0051A1C0: a network game's skater collisions, against skaters `first` to 7: each skater's frame of
    // movement ahead of it (the reach) as a segment, and a hit when the segments come within the radius (their
    // closest points, else the nearer of the four ends). Only between players allowed to hit each other (teams,
    // observers, the game's "it"); the faster one hits (a driver always beats one on foot). A hit is sent to both
    // (messages 0x43 / 0x44), and in a game of tag (0x6EF8FDA0 / 0x5D32129C) or of team tag (0x6C5FF266) the
    // tagged player is told to everyone else.
    void check_for_collisions(int first);
    // 0x00569680: the newest state sent (+0x1C, 0x84 bytes each; the count at +0x18). 0x005696A0: the newest other
    // message (+0xA70; the count at +0xA6C - its slot, as the history fills it: count % 20).
    SkaterStateHistoryFrame* fetch_newest_snapshot();
    StateHistoryMessage* get_latest_message();

    // --- network message handlers (the skater's client app; the handler's data is the skater) ---
    // 0x00519870: 0x84 - this skater was hit by another (`msg[0]` its object id, `msg[1]` whether it was driving,
    // `msg+4` the score to lose): knocked back away from it (300 * 1.5 horizontally, 200 up), into a bail on foot or
    // driving, and its score lowered (to 0 at least: then unbind_fireball_tricks). 0x85 - this skater hit another.
    // Each is an event to the skater with the other's name (0x915E5E39 / 0xA1021AF0). Returns kResultConsumed.
    static int handle_hit_messages(net::InboundMsgCtx* context);
    // 0x00519C10: 0x43 - this skater was run into (`msg[0]` the other's object id, bit 7: it was driving; cleared in
    // the message; `msg+4` the push): knocked back unless driving, the push as a vector in the event 0x915E5E39.
    // 0x44 - this skater ran into another (event 0xA1021AF0). Returns kResultPassOn.
    static int handle_collision_messages(net::InboundMsgCtx* context);

    int state_count_;                          // +0x18 the states received
    SkaterStateHistoryFrame states_[kEntries]{}; // +0x1C (the newest at (state_count_ - 1) % 20)
    int message_count_;                        // +0xA6C the messages received
    StateHistoryMessage messages_[kEntries]{}; // +0xA70 (the newest at message_count_ % 20)
    uint32_t unknown1100_;                     // +0x1100 (cleared)
    uint32_t vehicle_control_type_{};          // +0x1104 the vehicle's control type (message 0x?)
};

} // namespace objects
