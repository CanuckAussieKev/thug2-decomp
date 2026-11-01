#pragma once
#include <array>

// The event lists the trigger component's probe_touches_zone / get_trigger_node are given (const tables of three ints: an event,
// a sub-event, 0), in the exe's read-only data. Defined in trigger_component.cpp.
extern const std::array<int, 3> kWheelLeavesEvents;      // vehicle wheel leaving the ground
extern const std::array<int, 3> kWheelLandsEvents;       // vehicle wheel landing
extern const std::array<int, 3> kCrossingEvents;         // a vehicle or wheel crossing a trigger
extern const std::array<int, 3> kPullUpEvents;           // pulling up from a hang
extern const std::array<int, 3> kHangEvents;             // hanging
extern const std::array<int, 3> kExitHangEvents;         // leaving a hang
extern const std::array<int, 3> kLadderOffBottomEvents;  // leaving a ladder at the bottom
extern const std::array<int, 3> kLadderOffTopEvents;     // leaving a ladder at the top
extern const std::array<int, 3> kOntoLadderEvents;       // getting onto a ladder
extern const std::array<int, 3> kExitLadderEvents;       // leaving a ladder
extern const std::array<int, 3> kProjectileTouchEvents;  // a projectile touching something
extern const std::array<int, 3> kOtherFeelerLeaveEvents; // the other feeler leaving
extern const std::array<int, 3> kJumpTriggerEvents;      // jumping from the ground (the other feeler)
extern const std::array<int, 3> kSkaterEvents0064bB98;
extern const std::array<int, 3> kKickEvents; // kicked off a point rail
extern const std::array<int, 3> kSkaterEvents0064bBb0;
extern const std::array<int, 3> kBonkEvents;   // hitting a wall
extern const std::array<int, 3> kLipEvents;    // a lip trick
extern const std::array<int, 3> kOffLipEvents; // off a lip
extern const std::array<int, 3> kLipOffEvents; // getting off a lip
extern const std::array<int, 3> kSkaterEvents0064bBec;
extern const std::array<int, 3> kWallplantEvents; // leaving a wallplant
extern const std::array<int, 3> kSpinEvents;      // a point rail spin
extern const std::array<int, 3> kJumpEvents;      // jumping off a rail
extern const std::array<int, 3> kGrindOffEvents;  // getting off a rail
extern const std::array<int, 3> kGrindEvents;     // getting onto a rail
extern const std::array<int, 3> kTaggingEvents;   // the tagging component's trigger
extern const std::array<int, 3> kWalkEvents0064c630;
extern const std::array<int, 3> kWalkEvents0064c63c;
extern const std::array<int, 3> kWalkEvents0064c648;
extern const std::array<int, 3> kWalkEvents0064c654;
extern const std::array<int, 3> kWalkEvents0064c660;
