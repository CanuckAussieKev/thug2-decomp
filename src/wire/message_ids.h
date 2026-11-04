#pragma once

#include <cstdint>

// NOT ORIGINAL (the ids are the original's) - the ids of the network messages: the transport's own (net_wire.h has
// their layouts), the game's with a reader / writer in wire/ (gamenet_messages.h, play_messages.h, skater_messages.h,
// skate_net_messages.h) and those without one (their bodies are a byte or two, a buffer of another format - the park
// map, the rails, the goals, a texture -, or nothing); and the sequenced groups the game's messages go in.
namespace online {

// The sequenced groups the game's messages go in (the group byte of a sequenced message, net_wire.h): each group's
// messages arrive in the order they were sent.
enum class MessageGroup : uint8_t {
    kNone = 0, // (not a sequenced message)
    kGame = 8, // the game's messages
    kFaceData = 9, // a player's face texture
    kGraffiti = 10, // the graffiti tags: their placing and their art (the create-a-graphic data)
};

// The id byte of every message (a transport message or the game's, one space of ids). Ids the game doesn't
// list stay representable: a packet is read as its byte (THUGPRO's own ids: game/online/thugpro_peer.h).
enum class MessageId : uint8_t {
    kNone = 0x00, // (no message: an id not set yet)
    // the transport's (net_wire.h: their layouts)
    kLatencyTest = 0x01,
    kLatencyResponse = 0x02,
    kConnectionRequest = 0x03,
    kConnectionAccepted = 0x04,
    kConnectionRefused = 0x05,
    kConnectionTerminated = 0x06,
    kSequenced = 0x07,
    kAck = 0x08,
    kClockStamp = 0x09, // (the one default-queue message sent to a connection not yet ready)
    kDisconnRequest = 0x0B, // a client leaves the server
    kDisconnAccepted = 0x0C,
    kBulkBegin = 0x0E, // a stream's start (net_wire.h)
    kBulkChunk = 0x0F, // a stream's piece
    kCompressed = 0x12,
    // with a reader / writer in gamenet_messages.h
    kSessionBrief = 0x33,
    kStartGame = 0x34,
    kServerInfo = 0x10,
    kPlayerArrived = 0x22,
    kHudNotice = 0x3F,
    kStartRequest = 0x60,
    kKickRequest = 0x61,
    kLevelSwitch = 0x46,
    kRequestGameData = 0x6B,
    kGoalCompleted = 0x6E,
    kGoalStarted = 0x6F,
    kChat = 0x4C,
    // with a reader / writer in play_messages.h
    kHuntPlacements = 0x8F,
    kHuntItemPickedUp = 0x90,
    kHuntItemsLeft = 0x95,
    kBoardVehicle = 0x89,
    kProjectile = 0x83,
    kPlaceTag = 0x99,
    kTerrainSound = 0x35,
    kToggleCheat = 0x7F,
    kCheats = 0x82,
    kRunScript = 0x48,
    kSpawnScript = 0x49,
    kPlayerGone = 0x2D,
    kHit = 0x84,
    kHitOwner = 0x85,
    kRanInto = 0x43,
    kRanIntoAck = 0x44,
    // with a reader / writer in skater_messages.h
    kPlayAnim = 0x29,
    kBoardRotated = 0x2A,
    kWobbleTarget = 0x2B,
    kFlip = 0x2C,
    kWobbleDetails = 0x3C,
    kLoopType = 0x3D,
    kHideGeom = 0x3E,
    kAnimSpeed = 0x41,
    kSpinModel = 0x76,
    kCreateSpecialItem = 0x77,
    kDestroySpecialItem = 0x78,
    kCancelSpins = 0x8C,
    kTrickObjects = 0x3B,
    kObservedTrickObjects = 0x5B,
    kTagStolen = 0x4B,
    kLandedScore = 0x52,
    kBestCombo = 0x86,
    // with a reader / writer in skate_net_messages.h
    kScores = 0x40,
    kCheatsCheck = 0x80,
    // without one
    kServerQuery = 0x11, // a client asks the server for its description
    kObjectUpdates = 0x23, // the skaters' states (the object-update stream, object_updates.h)
    kReadyQuery = 0x25, // are you ready (a time stamp)
    kReadyResponse = 0x26, // the answer to it
    kServerQuit = 0x32, // the server is going away
    kTerrainSoundB = 0x36, // the second terrain sound message (play_messages.h's layout)
    kSparksOn = 0x37, // a skater's sparks (relayed)
    kSparksOff = 0x38,
    kLookUpObjectA = 0x39, // an object looked up by id (two messages, one handler)
    kLookUpObjectB = 0x3A,
    kGameStateSent = 0x42, // the host has sent a joining player the game's state
    kJoined = 0x45, // a local skater's client tells the host its skater is in (by script)
    kJoinRefused = 0x47, // a joining client: the server said no
    kObserveRequest = 0x4A, // a player asks to watch (the server's handle_observe_request)
    kObserve = 0x4D, // the server lets it watch
    kObserversFull = 0x4E, // or has no room for another observer
    kRemovedYou = 0x51, // 'Q': kicked or banned (to its address)
    kJoinAccepted = 0x53,
    kReadyToReceive = 0x54,
    kJoinProceed = 0x4F, // ... or go on
    kJoinRequest = 0x50, // a client asks to join
    kKing = 0x55, // the new king of the hill, or in a game of tag the one tagged (its id)
    kMovedOn = 0x56, // 'V': the game went on without it
    kRiderReady = 0x57, // the server is told a skater is ready, in some game modes
    kCrownSpawn = 0x58, // where the king of the hill's crown appears
    kRunEnded = 0x59, // a client tells the server its run ended (the game's id)
    kGameOver = 0x5A,
    kGraffitiColors = 0x5C, // the trick objects' owners (graffiti), to an observer or a client
    kPlayerFullyIn = 0x5E,
    kFcfsNotice = 0x5F, // (the client's handle_fcfs_notice)
    // A client's requests to the host (the family of gamenet_messages.h's kMsgStartRequest 0x60 / kMsgKickRequest 0x61).
    kChangeLevelRequest = 0x62,
    kToggleProSetRequest = 0x63,
    kToggleGoalRequest = 0x64, // flip an objective on or off
    kEndGameRequest = 0x65,
    kTeamCountRequest = 0x66,
    kJoinTeamRequest = 0x67,
    kTeamChange = 0x68, // a player joined a team (its id, the team)
    kTeamCount = 0x69,
    kGameEnded = 0x6A, // the host ended the game: to everyone else
    kParkMap = 0x6C, // the park editor's map ("level data")
    kSelectedGoals = 0x6D, // the goals chosen (goal attack)
    kFlipProSetting = 0x70,
    kTookFlag = 0x71, // capture the flag
    kCapturedFlag = 0x72,
    kTeamTagged = 0x73, // team tag, the same team: who (and the team), 2 bytes
    kTeamConverted = 0x74, // team tag, another team
    kFlagReturned = 0x75,
    kFlushImportant = 0x79,
    kTeamGame = 0x7A,
    kKeepAlive = 0x7B, // the server says wait n seconds (handle_keep_alive)
    kStatsChallenge = 0x7C, // the host's CD-key challenge to a player's client
    kCdKeyAnswer = 0x7D,
    kFaceTexture = 0x7E, // a player's face texture
    kDigest = 0x81,
    kGoalsData = 0x87, // the created goals ("goals data")
    kRailsData = 0x88, // the park editor's rails ("rail data")
    kRequestGoals = 0x8A,
    kRequestRails = 0x8B,
    kEliminated = 0x8D, // the player of an object id eliminated (-1: none, a tie)
    kEliminatedB = 0x8E, // handled as kEliminated
    // The scavenger hunt (with play_messages.h's 0x8F, 0x90, 0x95).
    kHuntNeedToPlace = 0x91, // AnnounceNeedToPlaceObjects (a client runs its add-time script)
    kHuntEnd = 0x92,
    kHuntStarted = 0x93, // AnnounceHuntStarted (a client starts collecting)
    kHuntCollectEnd = 0x94,
    // The graffiti tags' art (play_messages.h's layouts of 0x96 and 0x9A): the player they are for, texture, palette, parts.
    kGraffitiPlayer = 0x96,
    kGraffitiTexture = 0x97,
    kGraffitiPalette = 0x98,
    kGraffitiParts = 0x9A,
};

} // namespace online

// The transport (net_wire.h) names them in its own namespace.
namespace net::wire {
using MessageId = online::MessageId;
using MessageGroup = online::MessageGroup;
} // namespace net::wire
