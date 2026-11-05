// The network manager's online service task: GameSpy availability, news, message of the day, lobby list.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/online/service.h"
#include "platform/gamespy.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/memory/manager.h"
#include "engine/memory/task_list.h"
#include "game/online/gamenet_lobby.h"
#include "game/online/gamenet_manager.h"
#include "game/online/server_list.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "engine/script/fwd.h"
#include "engine/memory/fwd.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/online/score_list.h"
#include "game/online/buddy_man.h"
#include "game/online/stats_man.h"
#include "base/core/format.h"
#include "game/online/net_game_state.h"
#include "game/online/online_identity.h"

namespace {
constexpr uint32_t kAvailabilityRecheckDelay = 100; // ms

// Names (the checksums of their text).
constexpr uint32_t kAvailabilityCheckWait = 0x15EC44CFU;        // availability_check_wait
constexpr uint32_t kCreateFailedLobbyListDialog = 0x43D52414U;  // CreateFailedLobbyListDialog
constexpr uint32_t kCreateGettingLobbyListDialog = 0xF68F8A86U; // CreateGettingLobbyListDialog
constexpr uint32_t kCreateMotdMenu = 0xF17B894CU;               // create_motd_menu
constexpr uint32_t kCreateMotdRetryDialog = 0xC072AC4EU;        // CreateMotdRetryDialog
constexpr uint32_t kLaunchMotdWaitDialog = 0xBDA86A12U;         // launch_motd_wait_dialog
constexpr uint32_t kNetStatusGamespyNoConnect = 0x4AF78404U;    // net_status_gamespy_no_connect
constexpr uint32_t kNetStatusRetryMotd = 0x78D25623U;           // net_status_retry_motd
constexpr uint32_t kRetrieveMotd = 0xA8D71FCCU;                 // retrieve_motd
constexpr uint32_t kServiceNeverAvailable = 0xAA290EB3U;        // service_never_available
constexpr uint32_t kServiceNotAvailable = 0xCB856AF7U;          // service_not_available

} // namespace

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
    HANDLE handle = CreateThread(nullptr, net::kSdkThreadStackBytes, thread, manager, CREATE_SUSPENDED, &id);
    manager->set_room_id(id); // (+0x9950 = id)
    ResumeThread(handle);
}

// "No servers found" (the lobby list is empty).
void show_no_servers()
{
    const script::StructPtr params = script::make_struct();
    params->append_word(kText, script::QbValueKind::kString, script::fetch_text(kNetStatusGamespyNoConnect));
    launch_script(kCreateFailedLobbyListDialog, params.get());
}

// Done: state 2, and this task stops.
void get_finish(online::Hub* manager)
{
    manager->assign_service_state(online::ServiceState::kStopped);
    manager->tasks().service_job()->drop_out();
}

} // namespace

// 0x00545880
void service_task(tasks::Task<online::Hub>* task)
{
    auto* manager = static_cast<online::Hub*>(task->content());
    switch (manager->service_state()) {
    case online::ServiceState::kWaiting: // waiting
        if (manager->service_wake_at() != 0 && frame_clock::get_time() > manager->service_wake_at()) {
            manager->set_service_state((manager->next_service_state()));
        }
        break;
    case online::ServiceState::kStart: // start: a script, then the availability check a moment later
        push_internet_heap();
        launch_script(kAvailabilityCheckWait);
        manager->assign_service_state(online::ServiceState::kWaiting);
        manager->assign_next_service_state(online::ServiceState::kAvailabilityStart);
        manager->set_service_wake_at(kAvailabilityRecheckDelay);
        pop_heap();
        break;
    case online::ServiceState::kAvailabilityStart:
        manager->assign_service_state(online::ServiceState::kAvailabilityAnswer);
        gsi_start_available_check("mototrax");
        break;
    case online::ServiceState::kAvailabilityAnswer: // the availability check's answer
        push_internet_heap();
        switch (gsi_available_check_think()) {
        case 1: // available
            manager->set_services_available(true);
            launch_script(kRetrieveMotd);
            break;
        case 2: // unavailable
            launch_script(kServiceNeverAvailable);
            get_finish(manager);
            manager->set_service_wake_at(0);
            break;
        case 3: // temporarily unavailable
            launch_script(kServiceNotAvailable);
            get_finish(manager);
            manager->set_service_wake_at(0);
            break;
        default: // still waiting
            break;
        }
        pop_heap();
        break;
    case online::ServiceState::kNewsMessage:
    case online::ServiceState::kNewsMessageRetry: { // the news: a message (with the count of messages, if any), then the download thread
        freeze_stream(1);
        const int count = static_cast<int>(online::net_game_state().news_message_count());
        if (count == 0) {
            script::spawn_script(kLaunchMotdWaitDialog, nullptr);
        } else {
            const script::StructPtr params = script::make_struct();
            constexpr size_t kTextSize = 256;
            std::array<char, kTextSize> text{};
            text::format_to(text, "(%d) %s", count, script::fetch_text(kNetStatusRetryMotd));
            params->append_word(kText, script::QbValueKind::kString, text.data());
            script::spawn_script(kCreateMotdRetryDialog, params.get());
        }
        manager->assign_service_state(online::ServiceState::kWaiting);
        manager->set_service_wake_at(0);
        manager->set_download_done(false);
        start_thread(manager, kNewsThread); // (it sets state 7)
        break;
    }
    case online::ServiceState::kNewsDownload: // the news download
        push_internet_heap();
        // (PartyMod's time limit; the original's was 15000 ms)
        if (frame_clock::get_time() - manager->request_started_at() > kNewsTimeout || manager->download_done()) {
            ghttp_cancel_request(manager->request());
            manager->assign_service_state(online::ServiceState::kNewsEnded);
        } else {
            ghttp_think();
        }
        pop_heap();
        break;
    case online::ServiceState::kMotdStart: // the message of the day: the download thread (it sets state 0xC)
        ghttp_cleanup();
        freeze_stream(1);
        start_thread(manager, kMotdThread);
        break;
    case online::ServiceState::kMotdDownload: // the message of the day's download
        push_internet_heap();
        if (frame_clock::get_time() - manager->request_started_at() > kMotdTimeout) {
            ghttp_cancel_request(manager->request());
            manager->assign_service_state(online::ServiceState::kMotdShow);
        } else {
            ghttp_think();
        }
        pop_heap();
        break;
    case online::ServiceState::kMotdShow: { // show the message of the day
        ghttp_cleanup();
        script::StructPtr params = script::make_struct();
        params->append_word(kText, manager->motd_text());
        launch_script(kCreateMotdMenu, params.get());
        params.reset();
        manager->assign_service_state(online::ServiceState::kWaiting);
        manager->set_service_wake_at(0);
        break;
    }
    case online::ServiceState::kNewsEnded: // the news download has ended
        ghttp_cleanup();
        // Originally from PartyMod Fixes: the flag set and script 0x588ED87C (original: flag cleared, script 0xD4E84C8D)
        manager->set_motd_received(kNewsOkValue);
        launch_script(kNewsDoneScript);
        get_finish(manager);
        manager->set_service_wake_at(0);
        break;
    case online::ServiceState::kLobbyListStart:
        launch_script(kCreateGettingLobbyListDialog);
        manager->lobby_service().get()->clear_lobbies();
        manager->lobby_service().get()->start_listing_games();
        break;
    case online::ServiceState::kLobbyListIdle:
        manager->assign_service_state(online::ServiceState::kWaiting);
        manager->set_service_wake_at(0);
        break;
    case online::ServiceState::kLobbyListCheck: // the lobby list: empty or not
        if (!manager->lobby_service().get()->get_lobby(0)) {
            show_no_servers();
            get_finish(manager);
        } else {
            manager->assign_service_state(online::ServiceState::kLobbyListShow);
        }
        freeze_stream(0);
        break;
    case online::ServiceState::kLobbyListEmpty:
        manager->lobby_service().get()->clear_lobbies();
        show_no_servers();
        get_finish(manager);
        break;
    case online::ServiceState::kLobbyListShow:
        manager->lobby_service().get()->populate_room_list();
        get_finish(manager);
        break;
    default: // 1, 2: nothing to do
        break;
    }
}

} // namespace net
