// The network manager's online service task: GameSpy availability, news, message of the day, lobby list.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/net/service.h"
#include "engine/net/gamespy.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/server_list.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

namespace NetLink {

namespace {

// The manager's fields this task uses (the network manager isn't decompiled yet: reached by offset).
constexpr uint32_t kLobbyList = 0x10;       // an object: the lobby (server) list
constexpr uint32_t kServiceTaskField = 0xE0; // the task running this function
constexpr uint32_t kState = 0x1B8;
constexpr uint32_t kNextState = 0x1BC;      // state 0 moves on to it at the wake time
constexpr uint32_t kWakeTime = 0x1C0;       // FrameClock::GetTime() milliseconds; 0 = none
constexpr uint32_t kMotdText = 0xFDF4;      // char*: the downloaded message of the day
constexpr uint32_t kRequest = 0xFE00;       // the current ghttp request
constexpr uint32_t kRequestStart = 0xFE04;  // when it was started
constexpr uint32_t kNewsFinished = 0xFE08;  // bool: the news download has finished
constexpr uint32_t kNewsOk = 0xFE09;        // bool (PartyMod sets it; meaning not known beyond that)
constexpr uint32_t kAvailable = 0xFE0A;     // bool: the GameSpy services are available

template <typename T>
T& Prop(uint8_t* manager, uint32_t offset)
{
    return *reinterpret_cast<T*>(manager + offset);
}

// PartyMod's MOTD fix (its patchMotdFix): the news download's time limit (original: 15000 ms)
// and the script run when it ends, with the "news ok" flag set (original: 0xD4E84C8D with the flag cleared).
constexpr uint32_t kNewsTimeout = 500;
constexpr uint32_t kNewsDoneScript = 0x588ED87C;
constexpr bool kNewsOkValue = true;
// The message of the day still waits 15 s.
constexpr uint32_t kMotdTimeout = 15000;

constexpr uint32_t kText = 0x4942CF80; // "text"

// The two download threads (stdcall, the manager as their argument).
const auto kNewsThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(CodeAddress(OnlinePlay::NewsThread));
const auto kMotdThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(CodeAddress(OnlinePlay::MotdThread));

void ChangePhase(uint8_t* manager, int state)
{
    reinterpret_cast<OnlinePlay::Hub*>(manager)->SetServiceState(state);
}
// Take this task out of the task list (Scheduling::SchedulerTask::DropOut).
void RemoveServiceTask(uint8_t* manager)
{
    Prop<Scheduling::SchedulerTask*>(manager, kServiceTaskField)->DropOut();
}
OnlinePlay::RoomManager* LobbyList(uint8_t* manager) { return Prop<OnlinePlay::RoomManager*>(manager, kLobbyList); }

void PushInternetHeap()
{
    MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_internetBottomUpHeap);
}
void PopHeap() { MemSys::Hub::Pull()->LeaveHeapScope(); }

void LaunchScript(uint32_t name, Scripts::CStruct* params = nullptr)
{
    Scripts::LaunchScript(name, params, nullptr, false, nullptr);
}
void SpawnScript(uint32_t name, Scripts::CStruct* params = nullptr)
{
    Scripts::SpawnScript(name, params, 0, nullptr, 0, 0, false, false, false, false);
}
Scripts::CStruct* NewParams()
{
    Scripts::CStruct* params = Scripts::SpawnParamBag();
    return params ? params->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// Pause (1), unpause (0) or restore (-1) the music streams.
void FreezeStream(int pause) { AudioStream::FreezeStream(pause); }

// GameSpy (library code, not decompiled).
void GsiStartAvailableCheck(const char* game) { GameSpy::StartAvailableCheck(game); }
int GsiAvailableCheckThink() { return GameSpy::AvailableCheckThink(); }
void GhttpThink() { GameSpy::HttpThink(); }
void GhttpCancelRequest(int request) { GameSpy::HttpCancelRequest(request); }
void GhttpCleanup() { GameSpy::HttpCleanup(); }

// A download thread, started suspended so that the manager knows its id first. (The handle is never closed.)
void StartThread(uint8_t* manager, LPTHREAD_START_ROUTINE thread)
{
    DWORD id;
    HANDLE handle = CreateThread(nullptr, 0x8000, thread, manager, CREATE_SUSPENDED, &id);
    reinterpret_cast<OnlinePlay::Hub*>(manager)->SetRoomId(id); // (+0x9950 = id)
    ResumeThread(handle);
}

// "No servers found" (the lobby list is empty).
void ShowNoServers()
{
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kText, Scripts::QBVAL_STRING, Scripts::FetchText(0x4AF78404));
    LaunchScript(0x43D52414, params);
    DeleteParams(params);
}

// Done: state 2, and this task stops.
void Finish(uint8_t* manager)
{
    Prop<int>(manager, kState) = 2;
    RemoveServiceTask(manager);
}

} // namespace

// 0x00545880
void __cdecl ServiceTask(const uint8_t* task)
{
    uint8_t* manager = *reinterpret_cast<uint8_t* const*>(task + 0x1C);
    switch (Prop<int>(manager, kState)) {
    case 0: // waiting
        if (Prop<uint32_t>(manager, kWakeTime) != 0 && FrameClock::GetTime() > Prop<uint32_t>(manager, kWakeTime))
            ChangePhase(manager, Prop<int>(manager, kNextState));
        break;
    case 3: // start: a script, then the availability check a moment later
        PushInternetHeap();
        LaunchScript(0x15EC44CF);
        Prop<int>(manager, kState) = 0;
        Prop<int>(manager, kNextState) = 4;
        Prop<uint32_t>(manager, kWakeTime) = 100;
        PopHeap();
        break;
    case 4:
        Prop<int>(manager, kState) = 5;
        GsiStartAvailableCheck("mototrax");
        break;
    case 5: // the availability check's answer
        PushInternetHeap();
        switch (GsiAvailableCheckThink()) {
        case 1: // available
            Prop<bool>(manager, kAvailable) = true;
            LaunchScript(0xA8D71FCC);
            break;
        case 2: // unavailable
            LaunchScript(0xAA290EB3);
            Finish(manager);
            Prop<uint32_t>(manager, kWakeTime) = 0;
            break;
        case 3: // temporarily unavailable
            LaunchScript(0xCB856AF7);
            Finish(manager);
            Prop<uint32_t>(manager, kWakeTime) = 0;
            break;
        default: // still waiting
            break;
        }
        PopHeap();
        break;
    case 6:
    case 0xA: { // the news: a message (with the count of messages, if any), then the download thread
        FreezeStream(1);
        int count = OnlinePlay::g_newsMessageCount;
        if (count == 0) {
            SpawnScript(0xBDA86A12);
        } else {
            Scripts::CStruct* params = NewParams();
            char text[256];
            std::snprintf(text, sizeof(text), "(%d) %s", count, Scripts::FetchText(0x78D25623));
            params->AppendWord(kText, Scripts::QBVAL_STRING, text);
            SpawnScript(0xC072AC4E, params);
            DeleteParams(params);
        }
        Prop<int>(manager, kState) = 0;
        Prop<uint32_t>(manager, kWakeTime) = 0;
        Prop<bool>(manager, kNewsFinished) = false;
        StartThread(manager, kNewsThread); // (it sets state 7)
        break;
    }
    case 7: // the news download
        PushInternetHeap();
        // (PartyMod's time limit; the original's was 15000 ms)
        if (FrameClock::GetTime() - Prop<uint32_t>(manager, kRequestStart) > kNewsTimeout ||
            Prop<bool>(manager, kNewsFinished)) {
            GhttpCancelRequest(Prop<int>(manager, kRequest));
            Prop<int>(manager, kState) = 0xB;
        } else {
            GhttpThink();
        }
        PopHeap();
        break;
    case 8: // the message of the day: the download thread (it sets state 0xC)
        GhttpCleanup();
        FreezeStream(1);
        StartThread(manager, kMotdThread);
        break;
    case 0xC: // the message of the day's download
        PushInternetHeap();
        if (FrameClock::GetTime() - Prop<uint32_t>(manager, kRequestStart) > kMotdTimeout) {
            GhttpCancelRequest(Prop<int>(manager, kRequest));
            Prop<int>(manager, kState) = 9;
        } else {
            GhttpThink();
        }
        PopHeap();
        break;
    case 9: { // show the message of the day
        GhttpCleanup();
        Scripts::CStruct* params = NewParams();
        params->AppendWord(kText, Prop<const char*>(manager, kMotdText));
        LaunchScript(0xF17B894C, params);
        DeleteParams(params);
        Prop<int>(manager, kState) = 0;
        Prop<uint32_t>(manager, kWakeTime) = 0;
        break;
    }
    case 0xB: // the news download has ended
        GhttpCleanup();
        // Originally from PartyMod Fixes: the flag set and script 0x588ED87C (original: flag cleared, script 0xD4E84C8D)
        Prop<bool>(manager, kNewsOk) = kNewsOkValue;
        LaunchScript(kNewsDoneScript);
        Finish(manager);
        Prop<uint32_t>(manager, kWakeTime) = 0;
        break;
    case 0xD:
        LaunchScript(0xF68F8A86);
        LobbyList(manager)->ClearLobbies();
        LobbyList(manager)->StartListingGames();
        break;
    case 0xE:
        Prop<int>(manager, kState) = 0;
        Prop<uint32_t>(manager, kWakeTime) = 0;
        break;
    case 0xF: // the lobby list: empty or not
        if (!LobbyList(manager)->GetLobby(0)) {
            ShowNoServers();
            Finish(manager);
        } else {
            Prop<int>(manager, kState) = 0x11;
        }
        FreezeStream(0);
        break;
    case 0x10:
        LobbyList(manager)->ClearLobbies();
        ShowNoServers();
        Finish(manager);
        break;
    case 0x11:
        LobbyList(manager)->PopulateRoomList();
        Finish(manager);
        break;
    default: // 1, 2: nothing to do
        break;
    }
}

} // namespace Net
