#pragma once

#include <cstdint>

// NOT ORIGINAL (the ids are the original's) - the ids of the network game's messages that have no reader / writer
// in wire/ (their bodies are a byte or two, a buffer of another format - the park map, the rails, the goals, a
// texture -, or nothing). The ids of the messages with a reader / writer are next to it: gamenet_messages.h,
// play_messages.h, skater_messages.h, skate_net_messages.h; the transport's own in net_wire.h.
namespace online {

// The sequenced groups the game's messages go in (the group byte of a sequenced message, net_wire.h): each group's
// messages arrive in the order they were sent.
constexpr uint8_t kGroupGame = 8;     // the game's messages
constexpr uint8_t kGroupFaceData = 9; // a player's face texture
constexpr uint8_t kGroupGraffiti = 10; // the graffiti tags: their placing and their art (the create-a-graphic data)

constexpr uint8_t kMsgServerQuery = 0x11;     // a client asks the server for its description
constexpr uint8_t kMsgObjectUpdates = 0x23;   // the skaters' states (the object-update stream, object_updates.h)
constexpr uint8_t kMsgReadyQuery = 0x25;      // are you ready (a time stamp)
constexpr uint8_t kMsgReadyResponse = 0x26;   // the answer to it
constexpr uint8_t kMsgServerQuit = 0x32;      // the server is going away
constexpr uint8_t kMsgTerrainSoundB = 0x36;   // the second terrain sound message (play_messages.h's layout)
constexpr uint8_t kMsgSparksOn = 0x37;        // a skater's sparks (relayed)
constexpr uint8_t kMsgSparksOff = 0x38;
constexpr uint8_t kMsgLookUpObjectA = 0x39;   // an object looked up by id (two messages, one handler)
constexpr uint8_t kMsgLookUpObjectB = 0x3A;
constexpr uint8_t kMsgGameStateSent = 0x42;   // the host has sent a joining player the game's state
constexpr uint8_t kMsgJoined = 0x45;          // a local skater's client tells the host its skater is in (by script)
constexpr uint8_t kMsgJoinRefused = 0x47;     // a joining client: the server said no
constexpr uint8_t kMsgObserveRequest = 0x4A;  // a player asks to watch (the server's handle_observe_request)
constexpr uint8_t kMsgObserve = 0x4D;         // the server lets it watch
constexpr uint8_t kMsgObserversFull = 0x4E;   // or has no room for another observer
constexpr uint8_t kMsgRemovedYou = 0x51;      // 'Q': kicked or banned (to its address)
constexpr uint8_t kMsgJoinAccepted = 0x53;
constexpr uint8_t kMsgReadyToReceive = 0x54;
constexpr uint8_t kMsgJoinProceed = 0x4F;     // ... or go on
constexpr uint8_t kMsgJoinRequest = 0x50;     // a client asks to join
constexpr uint8_t kMsgKing = 0x55;            // the new king of the hill, or in a game of tag the one tagged (its id)
constexpr uint8_t kMsgMovedOn = 0x56;         // 'V': the game went on without it
constexpr uint8_t kMsgRiderReady = 0x57;      // the server is told a skater is ready, in some game modes
constexpr uint8_t kMsgCrownSpawn = 0x58;      // where the king of the hill's crown appears
constexpr uint8_t kMsgRunEnded = 0x59;        // a client tells the server its run ended (the game's id)
constexpr uint8_t kMsgGameOver = 0x5A;
constexpr uint8_t kMsgGraffitiColors = 0x5C;  // the trick objects' owners (graffiti), to an observer or a client
constexpr uint8_t kMsgPlayerFullyIn = 0x5E;
constexpr uint8_t kMsgFcfsNotice = 0x5F;      // (the client's handle_fcfs_notice)
// A client's requests to the host (the family of gamenet_messages.h's kMsgStartRequest 0x60 / kMsgKickRequest 0x61).
constexpr uint8_t kMsgChangeLevelRequest = 0x62;
constexpr uint8_t kMsgToggleProSetRequest = 0x63;
constexpr uint8_t kMsgToggleGoalRequest = 0x64; // flip an objective on or off
constexpr uint8_t kMsgEndGameRequest = 0x65;
constexpr uint8_t kMsgTeamCountRequest = 0x66;
constexpr uint8_t kMsgJoinTeamRequest = 0x67;
constexpr uint8_t kMsgTeamChange = 0x68;       // a player joined a team (its id, the team)
constexpr uint8_t kMsgTeamCount = 0x69;
constexpr uint8_t kMsgGameEnded = 0x6A;        // the host ended the game: to everyone else
constexpr uint8_t kMsgParkMap = 0x6C;          // the park editor's map ("level data")
constexpr uint8_t kMsgSelectedGoals = 0x6D;    // the goals chosen (goal attack)
constexpr uint8_t kMsgFlipProSetting = 0x70;
constexpr uint8_t kMsgTookFlag = 0x71;         // capture the flag
constexpr uint8_t kMsgCapturedFlag = 0x72;
constexpr uint8_t kMsgTeamTagged = 0x73;       // team tag, the same team: who (and the team), 2 bytes
constexpr uint8_t kMsgTeamConverted = 0x74;    // team tag, another team
constexpr uint8_t kMsgFlagReturned = 0x75;
constexpr uint8_t kMsgFlushImportant = 0x79;
constexpr uint8_t kMsgTeamGame = 0x7A;
constexpr uint8_t kMsgStatsChallenge = 0x7C;   // the host's CD-key challenge to a player's client
constexpr uint8_t kMsgCdKeyAnswer = 0x7D;
constexpr uint8_t kMsgFaceTexture = 0x7E;      // a player's face texture
constexpr uint8_t kMsgDigest = 0x81;
constexpr uint8_t kMsgGoalsData = 0x87;        // the created goals ("goals data")
constexpr uint8_t kMsgRailsData = 0x88;        // the park editor's rails ("rail data")
constexpr uint8_t kMsgRequestGoals = 0x8A;
constexpr uint8_t kMsgRequestRails = 0x8B;
constexpr uint8_t kMsgEliminated = 0x8D;       // the player of an object id eliminated (-1: none, a tie)
// The scavenger hunt (with play_messages.h's 0x8F, 0x90, 0x95).
constexpr uint8_t kMsgHuntNeedToPlace = 0x91;  // AnnounceNeedToPlaceObjects (a client runs its add-time script)
constexpr uint8_t kMsgHuntEnd = 0x92;
constexpr uint8_t kMsgHuntStarted = 0x93;      // AnnounceHuntStarted (a client starts collecting)
constexpr uint8_t kMsgHuntCollectEnd = 0x94;
// The graffiti tags' art (play_messages.h's layouts of 0x96 and 0x9A): the player they are for, texture, palette, parts.
constexpr uint8_t kMsgGraffitiPlayer = 0x96;
constexpr uint8_t kMsgGraffitiTexture = 0x97;
constexpr uint8_t kMsgGraffitiPalette = 0x98;
constexpr uint8_t kMsgGraffitiParts = 0x9A;

} // namespace online
