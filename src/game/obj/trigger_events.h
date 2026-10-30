#pragma once

#include "hook/game_global.h"

// The event lists the trigger component's CheckFeeler / TriggerNode are given (const tables of three ints: an event,
// a sub-event, 0), in the exe's read-only data. Defined in trigger_component.cpp.
GAME_GLOBAL_EXTERN(const int[3], g_wheelLeavesEvents); // vehicle wheel leaving the ground
GAME_GLOBAL_EXTERN(const int[3], g_wheelLandsEvents); // vehicle wheel landing
GAME_GLOBAL_EXTERN(const int[3], g_crossingEvents); // a vehicle or wheel crossing a trigger
GAME_GLOBAL_EXTERN(const int[3], g_pullUpEvents); // pulling up from a hang
GAME_GLOBAL_EXTERN(const int[3], g_hangEvents); // hanging
GAME_GLOBAL_EXTERN(const int[3], g_exitHangEvents); // leaving a hang
GAME_GLOBAL_EXTERN(const int[3], g_ladderOffBottomEvents); // leaving a ladder at the bottom
GAME_GLOBAL_EXTERN(const int[3], g_ladderOffTopEvents); // leaving a ladder at the top
GAME_GLOBAL_EXTERN(const int[3], g_ontoLadderEvents); // getting onto a ladder
GAME_GLOBAL_EXTERN(const int[3], g_exitLadderEvents); // leaving a ladder
GAME_GLOBAL_EXTERN(const int[3], g_projectileTouchEvents); // a projectile touching something
GAME_GLOBAL_EXTERN(const int[3], g_otherFeelerLeaveEvents); // the other feeler leaving
GAME_GLOBAL_EXTERN(const int[3], g_jumpTriggerEvents); // jumping from the ground (the other feeler)
GAME_GLOBAL_EXTERN(const int[3], g_skaterEvents0064BB98);
GAME_GLOBAL_EXTERN(const int[3], g_kickEvents); // kicked off a point rail
GAME_GLOBAL_EXTERN(const int[3], g_skaterEvents0064BBB0);
GAME_GLOBAL_EXTERN(const int[3], g_bonkEvents); // hitting a wall
GAME_GLOBAL_EXTERN(const int[3], g_lipEvents); // a lip trick
GAME_GLOBAL_EXTERN(const int[3], g_offLipEvents); // off a lip
GAME_GLOBAL_EXTERN(const int[3], g_lipOffEvents); // getting off a lip
GAME_GLOBAL_EXTERN(const int[3], g_skaterEvents0064BBEC);
GAME_GLOBAL_EXTERN(const int[3], g_wallplantEvents); // leaving a wallplant
GAME_GLOBAL_EXTERN(const int[3], g_spinEvents); // a point rail spin
GAME_GLOBAL_EXTERN(const int[3], g_jumpEvents); // jumping off a rail
GAME_GLOBAL_EXTERN(const int[3], g_grindOffEvents); // getting off a rail
GAME_GLOBAL_EXTERN(const int[3], g_grindEvents); // getting onto a rail
GAME_GLOBAL_EXTERN(const int[3], g_taggingEvents); // the tagging component's trigger
GAME_GLOBAL_EXTERN(const int[3], g_walkEvents0064C630);
GAME_GLOBAL_EXTERN(const int[3], g_walkEvents0064C63C);
GAME_GLOBAL_EXTERN(const int[3], g_walkEvents0064C648);
GAME_GLOBAL_EXTERN(const int[3], g_walkEvents0064C654);
GAME_GLOBAL_EXTERN(const int[3], g_walkEvents0064C660);
