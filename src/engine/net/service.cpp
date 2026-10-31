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

namespace net {

namespace {

// The manager's fields this task uses (the network manager isn't decompiled yet: reached by offset).
constexpr uint32_t kLobbyList = 0x10;       // an object: the lobby (server) list
constexpr uint32_t kServiceTaskField = 0xE0; // the task running this function
constexpr uint32_t kState = 0x1B8;
constexpr uint32_t kNextState = 0x1BC;      // state 0 moves on to it at the wake time
constexpr uint32_t kWakeTime = 0x1C0;       // frame_clock::GetTime() milliseconds; 0 = none
constexpr uint32_t kMotdText = 0xFDF4;      // char*: the downloaded message of the day
constexpr uint32_t kRequest = 0xFE00;       // the current ghttp request
constexpr uint32_t kRequestStart = 0xFE04;  // when it was started
constexpr uint32_t kNewsFinished = 0xFE08;  // bool: the news download has finished
constexpr uint32_t kNewsOk = 0xFE09;        // bool (PartyMod sets it; meaning not known beyond that)
constexpr uint32_t kAvailable = 0xFE0A;     // bool: the GameSpy services are available

template <typename T>
T& prop(uint8_t* manager, uint32_t offset)
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
const auto kNewsThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(code_address(online::news_thread));
const auto kMotdThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(code_address(online::motd_thread));

void change_phase(uint8_t* manager, int state)
{
    reinterpret_cast<online::Hub*>(manager)->set_service_state(state);
}
// Take this task out of the task list (tasks::SchedulerTask::drop_out).
void remove_service_task(uint8_t* manager)
{
    prop<tasks::SchedulerTask*>(manager, kServiceTaskField)->drop_out();
}
online::RoomManager* lobby_list(uint8_t* manager) { return prop<online::RoomManager*>(manager, kLobbyList); }

void push_internet_heap()
{
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->internet_bottom_up_heap_);
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

void launch_script(uint32_t name, script::Struct* params = nullptr)
{
    script::launch_script(name, params, nullptr, false, nullptr);
}
void spawn_script(uint32_t name, script::Struct* params = nullptr)
{
    script::spawn_script(name, params, 0, nullptr, 0, 0, false, false, false, false);
}
script::Struct* new_params()
{
    script::Struct* params = script::spawn_param_bag();
    return params ? params->prepare_all() : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// Pause (1), unpause (0) or restore (-1) the music streams.
void freeze_stream(int pause) { audio::freeze_stream(pause); }

// GameSpy (library code, not decompiled).
void gsi_start_available_check(const char* game) { game_spy::start_available_check(game); }
int gsi_available_check_think() { return game_spy::available_check_think(); }
void ghttp_think() { game_spy::http_think(); }
void ghttp_cancel_request(int request) { game_spy::http_cancel_request(request); }
void ghttp_cleanup() { game_spy::http_cleanup(); }

// A download thread, started suspended so that the manager knows its id first. (The handle is never closed.)
void start_thread(uint8_t* manager, LPTHREAD_START_ROUTINE thread)
{
    DWORD id;
    HANDLE handle = CreateThread(nullptr, 0x8000, thread, manager, CREATE_SUSPENDED, &id);
    reinterpret_cast<online::Hub*>(manager)->set_room_id(id); // (+0x9950 = id)
    ResumeThread(handle);
}

// "No servers found" (the lobby list is empty).
void show_no_servers()
{
    script::Struct* params = new_params();
    params->append_word(kText, script::kQbvalString, script::fetch_text(0x4AF78404));
    launch_script(0x43D52414, params);
    delete_params(params);
}

// Done: state 2, and this task stops.
void get_finish(uint8_t* manager)
{
    prop<int>(manager, kState) = 2;
    remove_service_task(manager);
}

} // namespace

// 0x00545880
void __cdecl service_task(const uint8_t* task)
{
    uint8_t* manager = *reinterpret_cast<uint8_t* const*>(task + 0x1C);
    switch (prop<int>(manager, kState)) {
    case 0: // waiting
        if (prop<uint32_t>(manager, kWakeTime) != 0 && frame_clock::get_time() > prop<uint32_t>(manager, kWakeTime))
            change_phase(manager, prop<int>(manager, kNextState));
        break;
    case 3: // start: a script, then the availability check a moment later
        push_internet_heap();
        launch_script(0x15EC44CF);
        prop<int>(manager, kState) = 0;
        prop<int>(manager, kNextState) = 4;
        prop<uint32_t>(manager, kWakeTime) = 100;
        pop_heap();
        break;
    case 4:
        prop<int>(manager, kState) = 5;
        gsi_start_available_check("mototrax");
        break;
    case 5: // the availability check's answer
        push_internet_heap();
        switch (gsi_available_check_think()) {
        case 1: // available
            prop<bool>(manager, kAvailable) = true;
            launch_script(0xA8D71FCC);
            break;
        case 2: // unavailable
            launch_script(0xAA290EB3);
            get_finish(manager);
            prop<uint32_t>(manager, kWakeTime) = 0;
            break;
        case 3: // temporarily unavailable
            launch_script(0xCB856AF7);
            get_finish(manager);
            prop<uint32_t>(manager, kWakeTime) = 0;
            break;
        default: // still waiting
            break;
        }
        pop_heap();
        break;
    case 6:
    case 0xA: { // the news: a message (with the count of messages, if any), then the download thread
        freeze_stream(1);
        int count = online::g_news_message_count;
        if (count == 0) {
            spawn_script(0xBDA86A12);
        } else {
            script::Struct* params = new_params();
            char text[256];
            std::snprintf(text, sizeof(text), "(%d) %s", count, script::fetch_text(0x78D25623));
            params->append_word(kText, script::kQbvalString, text);
            spawn_script(0xC072AC4E, params);
            delete_params(params);
        }
        prop<int>(manager, kState) = 0;
        prop<uint32_t>(manager, kWakeTime) = 0;
        prop<bool>(manager, kNewsFinished) = false;
        start_thread(manager, kNewsThread); // (it sets state 7)
        break;
    }
    case 7: // the news download
        push_internet_heap();
        // (PartyMod's time limit; the original's was 15000 ms)
        if (frame_clock::get_time() - prop<uint32_t>(manager, kRequestStart) > kNewsTimeout ||
            prop<bool>(manager, kNewsFinished)) {
            ghttp_cancel_request(prop<int>(manager, kRequest));
            prop<int>(manager, kState) = 0xB;
        } else {
            ghttp_think();
        }
        pop_heap();
        break;
    case 8: // the message of the day: the download thread (it sets state 0xC)
        ghttp_cleanup();
        freeze_stream(1);
        start_thread(manager, kMotdThread);
        break;
    case 0xC: // the message of the day's download
        push_internet_heap();
        if (frame_clock::get_time() - prop<uint32_t>(manager, kRequestStart) > kMotdTimeout) {
            ghttp_cancel_request(prop<int>(manager, kRequest));
            prop<int>(manager, kState) = 9;
        } else {
            ghttp_think();
        }
        pop_heap();
        break;
    case 9: { // show the message of the day
        ghttp_cleanup();
        script::Struct* params = new_params();
        params->append_word(kText, prop<const char*>(manager, kMotdText));
        launch_script(0xF17B894C, params);
        delete_params(params);
        prop<int>(manager, kState) = 0;
        prop<uint32_t>(manager, kWakeTime) = 0;
        break;
    }
    case 0xB: // the news download has ended
        ghttp_cleanup();
        // Originally from PartyMod Fixes: the flag set and script 0x588ED87C (original: flag cleared, script 0xD4E84C8D)
        prop<bool>(manager, kNewsOk) = kNewsOkValue;
        launch_script(kNewsDoneScript);
        get_finish(manager);
        prop<uint32_t>(manager, kWakeTime) = 0;
        break;
    case 0xD:
        launch_script(0xF68F8A86);
        lobby_list(manager)->clear_lobbies();
        lobby_list(manager)->start_listing_games();
        break;
    case 0xE:
        prop<int>(manager, kState) = 0;
        prop<uint32_t>(manager, kWakeTime) = 0;
        break;
    case 0xF: // the lobby list: empty or not
        if (!lobby_list(manager)->get_lobby(0)) {
            show_no_servers();
            get_finish(manager);
        } else {
            prop<int>(manager, kState) = 0x11;
        }
        freeze_stream(0);
        break;
    case 0x10:
        lobby_list(manager)->clear_lobbies();
        show_no_servers();
        get_finish(manager);
        break;
    case 0x11:
        lobby_list(manager)->populate_room_list();
        get_finish(manager);
        break;
    default: // 1, 2: nothing to do
        break;
    }
}

} // namespace Net
