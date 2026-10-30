#pragma once

// The checksums the goal code reads and writes (goal*.cpp, the goal types, the goal manager): their names, as the
// game's scripts have them (or "name unknown").

#include <cstdint>
#include "hook/game_code.h"

namespace Game::GoalNames {

constexpr uint32_t kNoName = 0;                         // (an unnamed component: a flag)
constexpr uint32_t kUnlimitedTime = 0xF0E712D2;         // unlimited_Time
constexpr uint32_t kTime = 0x906B67BA;                  // time
constexpr uint32_t kScore = 0xCD66C8AE;                 // score
constexpr uint32_t kNumFlags = 0xB3790F33;              // num_flags
constexpr uint32_t kNumFlagsSet = 0xCF6CD3C1;           // num_flags_set
constexpr uint32_t kNumFlagsToWin = 0x1171A555;         // num_flags_to_win
constexpr uint32_t kSkaterFlags = 0xE462E527;           // skater_flags
constexpr uint32_t kGraffiti = 0xC8A82B5A;              // Graffiti
constexpr uint32_t kSpecial = 0xB394C01C;               // special
constexpr uint32_t kTetris = 0x4147922B;                // tetris
constexpr uint32_t kEditedGoal = 0x981D3AD0;            // edited_goal
constexpr uint32_t kNumberCollected = 0x129DAA10;       // number_collected
constexpr uint32_t kUseHudCounter = 0xFD7AE1F4;         // use_hud_counter
constexpr uint32_t kGoalUpdateCounter = 0xF2E85A34;     // goal_update_counter
constexpr uint32_t kNumber = 0x696FE0AB;                // number
constexpr uint32_t kProsetPrefix = 0x955C6305;          // proset_prefix
constexpr uint32_t kCanRetryGoal = 0xDBEB46E2;          // can_retry_goal
constexpr uint32_t kCurrentTime = 0x5FD80EDC;           // current_time
constexpr uint32_t kCurrentTimeSeconds = 0x7B1FE9A4;    // current_time_seconds
constexpr uint32_t kCurrentTimeFraction = 0xB4DB9CB7;   // current_time_fraction
constexpr uint32_t kQuickStart = 0x38221DF4;            // quick_start
constexpr uint32_t kProGoal = 0xD303A1A3;               // pro_goal
constexpr uint32_t kNet = 0x0D15EA00;                   // net
constexpr uint32_t kCompetition = 0x4AF5D34E;           // competition
constexpr uint32_t kComponentA302D7CD = 0xA302D7CD;     // (name unknown)
constexpr uint32_t kHasBeaten = 0x49807745;             // hasBeaten
constexpr uint32_t kIsLocked = 0xD3E93882;              // isLocked
constexpr uint32_t kHasSeen = 0x2206B1E7;               // hasSeen
constexpr uint32_t kWinRecord = 0xC22A2B72;             // win_record
constexpr uint32_t kRecordType = 0x96963902;            // record_type
constexpr uint32_t kGoalFlags = 0xCC3C4CC4;             // goal_flags
constexpr uint32_t kLevelNum = 0x1A4CDE06;              // levelnum
constexpr uint32_t kClassicModeGoal = 0x32D771EE;       // classic_mode_goal
constexpr uint32_t kClassic = 0x4C10DE52;               // classic
constexpr uint32_t kCareer = 0x4DA4937B;                // career
constexpr uint32_t kSaveGoalFlags = 0xA36700EF;         // (name unknown: the goal flags are saved)
constexpr uint32_t kHorse = 0x9D65D0E7;                 // Horse
constexpr uint32_t kFindGaps = 0xC5EC08E6;              // findGaps
constexpr uint32_t kRequireCombo = 0xF8E31760;          // require_combo
constexpr uint32_t kUninit = 0xD4E0306A;                // uninit
constexpr uint32_t kFail = 0x79D8D4B6;                  // fail
constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1;          // SkaterEndRun (the component)
constexpr uint32_t kSkaterScore = 0xD262802F;           // SkaterScore (the component)
constexpr uint32_t kSkaterState = 0x43686585;           // SkaterState (the component)
constexpr uint32_t kActiveScript = 0xB4E103FD;          // active
constexpr uint32_t kTimerRunoutBeep = 0xCFC7C3AD;       // timer_runout_beep
constexpr uint32_t kComboStarted = 0x7100155D;          // combo_started
constexpr uint32_t kExpireScript = 0xD34EB52B;          // expire
constexpr uint32_t kRequiredGaps = 0x52D4489E;          // required_gaps
constexpr uint32_t kRequiredTrickName = 0x730844A3;     // required_trick_name
constexpr uint32_t kBsPrefix = 0x7491335E;              // the checksum of "BS "
constexpr uint32_t kFsPrefix = 0x73989B82;              // the checksum of "FS "
constexpr uint32_t kWaypoints = 0x160B3220;             // (name unknown: the race's / horse's spots)
constexpr uint32_t kWaypoints2 = 0x92C6E839;            // (name unknown: the same, asserted)
constexpr uint32_t kFlag = 0x2E0B1465;                  // flag
constexpr uint32_t kAddTime = 0xFB11CB01;               // (name unknown: a spot's time is added)
constexpr uint32_t kGoalIdChecksum = 0x9982E501;        // goal_id
constexpr uint32_t kFirstSpot = 0xB13F3AB8;             // first_spot
constexpr uint32_t kHorseSpotScript = 0x681BC776;       // (name unknown: a horse spot spawned)
constexpr uint32_t kRaceSpotScript = 0x4C938553;        // (name unknown: a race waypoint spawned)
constexpr uint32_t kKillClusters = 0x191F66E9;          // kill_clusters
constexpr uint32_t kTrickString = 0x3A54CC4B;           // trick_string
constexpr uint32_t kTrickType = 0x3D929B66;             // trick_type
constexpr uint32_t kGrind = 0x255ED86F;                 // grind
constexpr uint32_t kCreateATrick = 0x8E6014F6;          // create_a_trick
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;         // netgoalattack
constexpr uint32_t kNetLobby = 0x1C471C60;              // NetLobby
constexpr uint32_t kStartHeight = 0xC07D8D60;           // start_height
constexpr uint32_t kRecord = 0x78FFB85C;                // (name unknown: the height / distance record)
constexpr uint32_t kCurrentHeightFeet = 0x50815D42;     // current_height_feet
constexpr uint32_t kCurrentHeightInches = 0xB0C26927;   // current_height_inches
constexpr uint32_t kRecordFeet = 0x24BF3AE4;            // record_feet
constexpr uint32_t kRecordInches = 0x9B49F28D;          // record_inches
constexpr uint32_t kDistanceObject = 0x8E18F3B3;        // (name unknown: the object the distance is from)
constexpr uint32_t kCurrentDistanceFeet = 0xCB48C8E8;   // current_distance_feet
constexpr uint32_t kCurrentDistanceInches = 0x644FC146; // current_distance_inches
constexpr uint32_t kEndofRunDone = 0x4C58771E;          // EndofRunDone
constexpr uint32_t kGoalEndOfRunDone = 0x69A9E37F;      // Goal_EndOfRunDone

// Strings the code passes as names (the game's own).
inline const char* const kGoalIdName = GAME_TEXT(0x0064B5CC, "goal_id");        // "goal_id"
inline const char* const kViewGoalsTextName = GAME_TEXT(0x0064DD94, "view_goals_text"); // "view_goals_text"
inline const char* const kIdName = GAME_TEXT(0x0064C8F0, "id");            // "id"
inline const char* const kEmptyString = GAME_TEXT(0x006469B4, "");       // ""
inline const char* const kGotFormat = GAME_TEXT(0x0064B678, "got_%d");         // "got_%d"

} // namespace Game::GoalNames
