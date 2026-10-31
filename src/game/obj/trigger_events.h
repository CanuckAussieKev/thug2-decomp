#pragma once

// The event lists the trigger component's CheckFeeler / TriggerNode are given (const tables of three ints: an event,
// a sub-event, 0), in the exe's read-only data. Defined in trigger_component.cpp.
extern const int g_wheelLeavesEvents[3]; // vehicle wheel leaving the ground
extern const int g_wheelLandsEvents[3]; // vehicle wheel landing
extern const int g_crossingEvents[3]; // a vehicle or wheel crossing a trigger
extern const int g_pullUpEvents[3]; // pulling up from a hang
extern const int g_hangEvents[3]; // hanging
extern const int g_exitHangEvents[3]; // leaving a hang
extern const int g_ladderOffBottomEvents[3]; // leaving a ladder at the bottom
extern const int g_ladderOffTopEvents[3]; // leaving a ladder at the top
extern const int g_ontoLadderEvents[3]; // getting onto a ladder
extern const int g_exitLadderEvents[3]; // leaving a ladder
extern const int g_projectileTouchEvents[3]; // a projectile touching something
extern const int g_otherFeelerLeaveEvents[3]; // the other feeler leaving
extern const int g_jumpTriggerEvents[3]; // jumping from the ground (the other feeler)
extern const int g_skaterEvents0064BB98[3];
extern const int g_kickEvents[3]; // kicked off a point rail
extern const int g_skaterEvents0064BBB0[3];
extern const int g_bonkEvents[3]; // hitting a wall
extern const int g_lipEvents[3]; // a lip trick
extern const int g_offLipEvents[3]; // off a lip
extern const int g_lipOffEvents[3]; // getting off a lip
extern const int g_skaterEvents0064BBEC[3];
extern const int g_wallplantEvents[3]; // leaving a wallplant
extern const int g_spinEvents[3]; // a point rail spin
extern const int g_jumpEvents[3]; // jumping off a rail
extern const int g_grindOffEvents[3]; // getting off a rail
extern const int g_grindEvents[3]; // getting onto a rail
extern const int g_taggingEvents[3]; // the tagging component's trigger
extern const int g_walkEvents0064C630[3];
extern const int g_walkEvents0064C63C[3];
extern const int g_walkEvents0064C648[3];
extern const int g_walkEvents0064C654[3];
extern const int g_walkEvents0064C660[3];
