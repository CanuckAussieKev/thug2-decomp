#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Net {
struct MsgHandlerContext;
}

// The skater's state history component (SkaterStateHistory, type 0x18223FD6, vtable 0x0064C174, 0x1108 bytes; made by
// 0x0051AD30): the last 20 states a non-local skater was sent (+0x1C, as skate_net.cpp reads them) and the last 20
// messages of its other kinds (+0xA70), with their time stamps - and, in network games, the skater-against-skater
// collisions: each frame the skate module checks every pair of skaters (0x00566BE0), and a hit tells both players.
//
// Vtable: 0 the scalar deleting destructor (0x00519840); Update 0x0051B970 (shared: suspends the component), the
// empty 0x005BAB90 / 0x005480A0, the base's Suspend and member function entry, CallMemberFunction 0x0051ADC0 (shared:
// "not mine").
namespace Obj {

// One of the history's other messages (0x54 bytes, 20 of them at +0xA70; the newest at +0xA6C % 20): the message's
// id first, then its time stamp as two halves.
struct StateHistoryMessage {
    uint8_t m_id;        // +0x00 the network message's id (0x29-0x8C: 0x00516180's switch)
    uint8_t pad01[3];
    uint16_t m_time;     // +0x04 the low half of the 32-bit time stamp
    uint16_t m_timeHigh; // +0x06 the high half
    uint8_t m_data[0x4C];

    // 0x00519800: the 32-bit time stamp.
    uint32_t GetTime();
    // 0x00519810: the time stamp set (its two halves).
    void SetTime(uint32_t time);
};
static_assert(offsetof(StateHistoryMessage, m_time) == 0x04 && sizeof(StateHistoryMessage) == 0x54,
              "layout must match the original");

class CSkaterStateHistoryComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x18223FD6;
    static constexpr int kEntries = 20;

    // 0x0051AD30 (registered with the composite object manager): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0051ACB0: the base's constructor, its vtable and type, every entry and the counts cleared. Returns this.
    CSkaterStateHistoryComponent* Construct();
    // 0x00519840 (vtable slot 0): the destructor body (0x005197C0), then freed with flags & 1.
    CSkaterStateHistoryComponent* Destroy(unsigned int flags);

    // 0x00519FA0: how far the object moves this frame: its velocity times the frame's length (w from the unrounded
    // frame length, x / y / z from it rounded).
    void GetFrameMovement(Mth::Vector* out);
    // 0x00519FF0: the object's position.
    void GetPosition(Mth::Vector* out);
    // 0x0051A020: how far ahead of a skater a collision is looked for: the script float 0x25F78481 (0x0D24273B when
    // the object's +0xBC is set), with `driving` times 1 + the script float 0x0A49FF23. Unrounded (x87).
    double GetCollisionReach(bool driving);
    // 0x0051A080: how close two skaters must come to collide: one of four script floats (the object's +0xBC and the
    // network manager's flag 0x4 pick it), times 1 + 0xEEF4AFB3's number for each of the two that is driving.
    double GetCollisionRadius(bool myDriving, bool theirDriving);
    // 0x0051A150: whether the object is within 96 of the network manager's local skater (+0x6C).
    bool IsNearLocalSkater();
    // 0x0051A1C0: a network game's skater collisions, against skaters `first` to 7: each skater's frame of
    // movement ahead of it (the reach) as a segment, and a hit when the segments come within the radius (their
    // closest points, else the nearer of the four ends). Only between players allowed to hit each other (teams,
    // observers, the game's "it"); the faster one hits (a driver always beats one on foot). A hit is sent to both
    // (messages 0x43 / 0x44), and in a game of tag (0x6EF8FDA0 / 0x5D32129C) or of team tag (0x6C5FF266) the
    // tagged player is told to everyone else.
    void CheckForCollisions(int first);

    // --- network message handlers (the skater's client app; the handler's data is the skater) ---
    // 0x00519870: 0x84 - this skater was hit by another (`msg[0]` its object id, `msg[1]` whether it was driving,
    // `msg+4` the score to lose): knocked back away from it (300 * 1.5 horizontally, 200 up), into a bail on foot or
    // driving, and its score lowered (to 0 at least: then unbind_fireball_tricks). 0x85 - this skater hit another.
    // Each is an event to the skater with the other's name (0x915E5E39 / 0xA1021AF0). Returns kHandlerMsgDone.
    static int __cdecl HandleHitMessages(Net::MsgHandlerContext* context);
    // 0x00519C10: 0x43 - this skater was run into (`msg[0]` the other's object id, bit 7: it was driving; cleared in
    // the message; `msg+4` the push): knocked back unless driving, the push as a vector in the event 0x915E5E39.
    // 0x44 - this skater ran into another (event 0xA1021AF0). Returns kHandlerContinue.
    static int __cdecl HandleCollisionMessages(Net::MsgHandlerContext* context);
};
static_assert(sizeof(CSkaterStateHistoryComponent) == 0x18, "fields are reached by offset");

// The destructors behind Lst::Search<PlayerInfo>'s vtable (0x0064C1A8: the network manager's player searches).
struct PlayerSearchDestructors {
    void** m_vtable;
    void* m_node;

    // 0x00519830 (only resets the vtable)
    void DestroyBody();
    // 0x0051A120 (scalar deleting)
    PlayerSearchDestructors* Destroy(unsigned int flags);
};

} // namespace Obj
