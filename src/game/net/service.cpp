// The network manager's online service task: GameSpy availability, news, message of the day, lobby list.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/net/service.h"
#include "game/net/gamespy.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/server_list.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

namespace Net {

namespace {

// The manager's fields this task uses (the network manager isn't decompiled yet: reached by offset).
constexpr uint32_t kLobbyList = 0x10;       // an object: the lobby (server) list
constexpr uint32_t kServiceTaskField = 0xE0; // the task running this function
constexpr uint32_t kState = 0x1B8;
constexpr uint32_t kNextState = 0x1BC;      // state 0 moves on to it at the wake time
constexpr uint32_t kWakeTime = 0x1C0;       // Tmr::GetTime() milliseconds; 0 = none
constexpr uint32_t kMotdText = 0xFDF4;      // char*: the downloaded message of the day
constexpr uint32_t kRequest = 0xFE00;       // the current ghttp request
constexpr uint32_t kRequestStart = 0xFE04;  // when it was started
constexpr uint32_t kNewsFinished = 0xFE08;  // bool: the news download has finished
constexpr uint32_t kNewsOk = 0xFE09;        // bool (PartyMod sets it; meaning not known beyond that)
constexpr uint32_t kAvailable = 0xFE0A;     // bool: the GameSpy services are available

template <typename T>
T& Field(uint8_t* manager, uint32_t offset)
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
const auto kNewsThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(CodeAddress(GameNet::NewsThread));
const auto kMotdThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(CodeAddress(GameNet::MotdThread));

void SetState(uint8_t* manager, int state)
{
    reinterpret_cast<GameNet::Manager*>(manager)->SetServiceState(state);
}
// Take this task out of the task list (Tsk::BaseTask::Remove).
void RemoveServiceTask(uint8_t* manager)
{
    Field<Tsk::BaseTask*>(manager, kServiceTaskField)->Remove();
}
GameNet::LobbyMan* LobbyList(uint8_t* manager) { return Field<GameNet::LobbyMan*>(manager, kLobbyList); }

void PushInternetHeap()
{
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_internetBottomUpHeap);
}
void PopHeap() { Mem::Manager::Get()->PopContext(); }

void RunScript(uint32_t name, Script::CStruct* params = nullptr)
{
    Script::RunScript(name, params, nullptr, false, nullptr);
}
void SpawnScript(uint32_t name, Script::CStruct* params = nullptr)
{
    Script::SpawnScript(name, params, 0, nullptr, 0, 0, false, false, false, false);
}
Script::CStruct* NewParams()
{
    Script::CStruct* params = Script::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// Pause (1), unpause (0) or restore (-1) the music streams.
void PauseStream(int pause) { Pcm::PauseStream(pause); }

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
    reinterpret_cast<GameNet::Manager*>(manager)->SetLobbyId(id); // (+0x9950 = id)
    ResumeThread(handle);
}

// "No servers found" (the lobby list is empty).
void ShowNoServers()
{
    Script::CStruct* params = NewParams();
    params->AddString(kText, Script::ESYMBOLTYPE_STRING, Script::GetString(0x4AF78404));
    RunScript(0x43D52414, params);
    DeleteParams(params);
}

// Done: state 2, and this task stops.
void Finish(uint8_t* manager)
{
    Field<int>(manager, kState) = 2;
    RemoveServiceTask(manager);
}

} // namespace

// 0x00545880
void __cdecl ServiceTask(const uint8_t* task)
{
    uint8_t* manager = *reinterpret_cast<uint8_t* const*>(task + 0x1C);
    switch (Field<int>(manager, kState)) {
    case 0: // waiting
        if (Field<uint32_t>(manager, kWakeTime) != 0 && Tmr::GetTime() > Field<uint32_t>(manager, kWakeTime))
            SetState(manager, Field<int>(manager, kNextState));
        break;
    case 3: // start: a script, then the availability check a moment later
        PushInternetHeap();
        RunScript(0x15EC44CF);
        Field<int>(manager, kState) = 0;
        Field<int>(manager, kNextState) = 4;
        Field<uint32_t>(manager, kWakeTime) = 100;
        PopHeap();
        break;
    case 4:
        Field<int>(manager, kState) = 5;
        GsiStartAvailableCheck("mototrax");
        break;
    case 5: // the availability check's answer
        PushInternetHeap();
        switch (GsiAvailableCheckThink()) {
        case 1: // available
            Field<bool>(manager, kAvailable) = true;
            RunScript(0xA8D71FCC);
            break;
        case 2: // unavailable
            RunScript(0xAA290EB3);
            Finish(manager);
            Field<uint32_t>(manager, kWakeTime) = 0;
            break;
        case 3: // temporarily unavailable
            RunScript(0xCB856AF7);
            Finish(manager);
            Field<uint32_t>(manager, kWakeTime) = 0;
            break;
        default: // still waiting
            break;
        }
        PopHeap();
        break;
    case 6:
    case 0xA: { // the news: a message (with the count of messages, if any), then the download thread
        PauseStream(1);
        int count = GameNet::g_newsMessageCount;
        if (count == 0) {
            SpawnScript(0xBDA86A12);
        } else {
            Script::CStruct* params = NewParams();
            char text[256];
            std::snprintf(text, sizeof(text), "(%d) %s", count, Script::GetString(0x78D25623));
            params->AddString(kText, Script::ESYMBOLTYPE_STRING, text);
            SpawnScript(0xC072AC4E, params);
            DeleteParams(params);
        }
        Field<int>(manager, kState) = 0;
        Field<uint32_t>(manager, kWakeTime) = 0;
        Field<bool>(manager, kNewsFinished) = false;
        StartThread(manager, kNewsThread); // (it sets state 7)
        break;
    }
    case 7: // the news download
        PushInternetHeap();
        // (PartyMod's time limit; the original's was 15000 ms)
        if (Tmr::GetTime() - Field<uint32_t>(manager, kRequestStart) > kNewsTimeout ||
            Field<bool>(manager, kNewsFinished)) {
            GhttpCancelRequest(Field<int>(manager, kRequest));
            Field<int>(manager, kState) = 0xB;
        } else {
            GhttpThink();
        }
        PopHeap();
        break;
    case 8: // the message of the day: the download thread (it sets state 0xC)
        GhttpCleanup();
        PauseStream(1);
        StartThread(manager, kMotdThread);
        break;
    case 0xC: // the message of the day's download
        PushInternetHeap();
        if (Tmr::GetTime() - Field<uint32_t>(manager, kRequestStart) > kMotdTimeout) {
            GhttpCancelRequest(Field<int>(manager, kRequest));
            Field<int>(manager, kState) = 9;
        } else {
            GhttpThink();
        }
        PopHeap();
        break;
    case 9: { // show the message of the day
        GhttpCleanup();
        Script::CStruct* params = NewParams();
        params->AddString(kText, Field<const char*>(manager, kMotdText));
        RunScript(0xF17B894C, params);
        DeleteParams(params);
        Field<int>(manager, kState) = 0;
        Field<uint32_t>(manager, kWakeTime) = 0;
        break;
    }
    case 0xB: // the news download has ended
        GhttpCleanup();
        // Originally from PartyMod Fixes: the flag set and script 0x588ED87C (original: flag cleared, script 0xD4E84C8D)
        Field<bool>(manager, kNewsOk) = kNewsOkValue;
        RunScript(kNewsDoneScript);
        Finish(manager);
        Field<uint32_t>(manager, kWakeTime) = 0;
        break;
    case 0xD:
        RunScript(0xF68F8A86);
        LobbyList(manager)->ClearLobbies();
        LobbyList(manager)->StartListingGames();
        break;
    case 0xE:
        Field<int>(manager, kState) = 0;
        Field<uint32_t>(manager, kWakeTime) = 0;
        break;
    case 0xF: // the lobby list: empty or not
        if (!LobbyList(manager)->GetLobby(0)) {
            ShowNoServers();
            Finish(manager);
        } else {
            Field<int>(manager, kState) = 0x11;
        }
        PauseStream(0);
        break;
    case 0x10:
        LobbyList(manager)->ClearLobbies();
        ShowNoServers();
        Finish(manager);
        break;
    case 0x11:
        LobbyList(manager)->FillLobbyList();
        Finish(manager);
        break;
    default: // 1, 2: nothing to do
        break;
    }
}

} // namespace Net
