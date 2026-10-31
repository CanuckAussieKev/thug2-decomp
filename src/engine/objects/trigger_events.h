#pragma once

// The event lists the trigger component's probe_touches_zone / get_trigger_node are given (const tables of three ints: an event,
// a sub-event, 0), in the exe's read-only data. Defined in trigger_component.cpp.
extern const int kWheelLeavesEvents[3];      // vehicle wheel leaving the ground
extern const int kWheelLandsEvents[3];       // vehicle wheel landing
extern const int kCrossingEvents[3];         // a vehicle or wheel crossing a trigger
extern const int kPullUpEvents[3];           // pulling up from a hang
extern const int kHangEvents[3];             // hanging
extern const int kExitHangEvents[3];         // leaving a hang
extern const int kLadderOffBottomEvents[3];  // leaving a ladder at the bottom
extern const int kLadderOffTopEvents[3];     // leaving a ladder at the top
extern const int kOntoLadderEvents[3];       // getting onto a ladder
extern const int kExitLadderEvents[3];       // leaving a ladder
extern const int kProjectileTouchEvents[3];  // a projectile touching something
extern const int kOtherFeelerLeaveEvents[3]; // the other feeler leaving
extern const int kJumpTriggerEvents[3];      // jumping from the ground (the other feeler)
extern const int kSkaterEvents0064bB98[3];
extern const int kKickEvents[3]; // kicked off a point rail
extern const int kSkaterEvents0064bBb0[3];
extern const int kBonkEvents[3];   // hitting a wall
extern const int kLipEvents[3];    // a lip trick
extern const int kOffLipEvents[3]; // off a lip
extern const int kLipOffEvents[3]; // getting off a lip
extern const int kSkaterEvents0064bBec[3];
extern const int kWallplantEvents[3]; // leaving a wallplant
extern const int kSpinEvents[3];      // a point rail spin
extern const int kJumpEvents[3];      // jumping off a rail
extern const int kGrindOffEvents[3];  // getting off a rail
extern const int kGrindEvents[3];     // getting onto a rail
extern const int kTaggingEvents[3];   // the tagging component's trigger
extern const int kWalkEvents0064c630[3];
extern const int kWalkEvents0064c63c[3];
extern const int kWalkEvents0064c648[3];
extern const int kWalkEvents0064c654[3];
extern const int kWalkEvents0064c660[3];
