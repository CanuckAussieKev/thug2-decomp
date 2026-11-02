// The network manager's online service task: GameSpy availability, news, message of the day, lobby list.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/net/service.h"
#include "platform/gamespy.h"

#include <windows.h>

#include <cstdint>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/server_list.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

namespace net {

namespace {

// PartyMod's MOTD fix (its patchMotdFix): the news download's time limit (original: 15000 ms)
// and the script run when it ends, with the "news ok" flag set (original: 0xD4E84C8D with the flag cleared).
constexpr uint32_t kNewsTimeout = 500;
constexpr uint32_t kNewsDoneScript = 0x588ED87C;
constexpr bool kNewsOkValue = true;
// The message of the day still waits 15 s.
constexpr uint32_t kMotdTimeout = 15000;

constexpr uint32_t kText = 0x4942CF80; // "text"

// The two download threads (stdcall, the manager as their argument).
const auto kNewsThread = &online::news_thread;
const auto kMotdThread = &online::motd_thread;


void push_internet_heap()
{
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->internet_bottom_up_heap());
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

// Pause (1), unpause (0) or restore (-1) the music streams.
void freeze_stream(int pause) { audio::freeze_stream(pause); }

// GameSpy (library code, not decompiled).
void gsi_start_available_check(const char* game) { game_spy::start_available_check(game); }
int gsi_available_check_think() { return game_spy::available_check_think(); }
void ghttp_think() { game_spy::http_think(); }
void ghttp_cancel_request(int request) { game_spy::http_cancel_request(request); }
void ghttp_cleanup() { game_spy::http_cleanup(); }

// A download thread, started suspended so that the manager knows its id first. (The handle is never closed.)
void start_thread(online::Hub* manager, LPTHREAD_START_ROUTINE thread)
{
    DWORD id = 0;
    HANDLE handle = CreateThread(nullptr, 0x8000, thread, manager, CREATE_SUSPENDED, &id);
    manager->set_room_id(id); // (+0x9950 = id)
    ResumeThread(handle);
}

// "No servers found" (the lobby list is empty).
void show_no_servers()
{
    const script::StructPtr params = script::make_struct();
    params->append_word(kText, script::kQbvalString, script::fetch_text(0x4AF78404));
    launch_script(0x43D52414, params.get());
}

// Done: state 2, and this task stops.
void get_finish(online::Hub* manager)
{
    manager->assign_service_state(2);
    manager->service_job()->drop_out();
}

} // namespace

// 0x00545880
void service_task(tasks::Task<online::Hub>* task)
{
    auto* manager = static_cast<online::Hub*>(task->content());
    switch (manager->service_state()) {
    case 0: // waiting
        if (manager->service_wake_at() != 0 && frame_clock::get_time() > manager->service_wake_at())
            manager->set_service_state((manager->next_service_state()));
        break;
    case 3: // start: a script, then the availability check a moment later
        push_internet_heap();
        launch_script(0x15EC44CF);
        manager->assign_service_state(0);
        manager->assign_next_service_state(4);
        manager->set_service_wake_at(100);
        pop_heap();
        break;
    case 4:
        manager->assign_service_state(5);
        gsi_start_available_check("mototrax");
        break;
    case 5: // the availability check's answer
        push_internet_heap();
        switch (gsi_available_check_think()) {
        case 1: // available
            manager->set_services_available(true);
            launch_script(0xA8D71FCC);
            break;
        case 2: // unavailable
            launch_script(0xAA290EB3);
            get_finish(manager);
            manager->set_service_wake_at(0);
            break;
        case 3: // temporarily unavailable
            launch_script(0xCB856AF7);
            get_finish(manager);
            manager->set_service_wake_at(0);
            break;
        default: // still waiting
            break;
        }
        pop_heap();
        break;
    case 6:
    case 0xA: { // the news: a message (with the count of messages, if any), then the download thread
        freeze_stream(1);
        const int count = static_cast<int>(online::net_game_state().news_message_count());
        if (count == 0) {
            spawn_script(0xBDA86A12);
        } else {
            const script::StructPtr params = script::make_struct();
            std::array<char, 256> text{};
            text::format_to(text, "(%d) %s", count, script::fetch_text(0x78D25623));
            params->append_word(kText, script::kQbvalString, text.data());
            spawn_script(0xC072AC4E, params.get());
        }
        manager->assign_service_state(0);
        manager->set_service_wake_at(0);
        manager->set_download_done(false);
        start_thread(manager, kNewsThread); // (it sets state 7)
        break;
    }
    case 7: // the news download
        push_internet_heap();
        // (PartyMod's time limit; the original's was 15000 ms)
        if (frame_clock::get_time() - manager->request_started_at() > kNewsTimeout || manager->download_done()) {
            ghttp_cancel_request(manager->request());
            manager->assign_service_state(0xB);
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
        if (frame_clock::get_time() - manager->request_started_at() > kMotdTimeout) {
            ghttp_cancel_request(manager->request());
            manager->assign_service_state(9);
        } else {
            ghttp_think();
        }
        pop_heap();
        break;
    case 9: { // show the message of the day
        ghttp_cleanup();
        script::StructPtr params = script::make_struct();
        params->append_word(kText, manager->motd_text());
        launch_script(0xF17B894C, params.get());
        params.reset();
        manager->assign_service_state(0);
        manager->set_service_wake_at(0);
        break;
    }
    case 0xB: // the news download has ended
        ghttp_cleanup();
        // Originally from PartyMod Fixes: the flag set and script 0x588ED87C (original: flag cleared, script 0xD4E84C8D)
        manager->set_motd_received(kNewsOkValue);
        launch_script(kNewsDoneScript);
        get_finish(manager);
        manager->set_service_wake_at(0);
        break;
    case 0xD:
        launch_script(0xF68F8A86);
        manager->lobby_service().get()->clear_lobbies();
        manager->lobby_service().get()->start_listing_games();
        break;
    case 0xE:
        manager->assign_service_state(0);
        manager->set_service_wake_at(0);
        break;
    case 0xF: // the lobby list: empty or not
        if (!manager->lobby_service().get()->get_lobby(0)) {
            show_no_servers();
            get_finish(manager);
        } else {
            manager->assign_service_state(0x11);
        }
        freeze_stream(0);
        break;
    case 0x10:
        manager->lobby_service().get()->clear_lobbies();
        show_no_servers();
        get_finish(manager);
        break;
    case 0x11:
        manager->lobby_service().get()->populate_room_list();
        get_finish(manager);
        break;
    default: // 1, 2: nothing to do
        break;
    }
}

} // namespace net
