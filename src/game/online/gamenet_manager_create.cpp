// The network manager made (online::Hub, 0x0053EE90 / 0x0053F520): its lists, preferences, tasks, the lobby,
// vault, presence and stats managers, and the CD-key challenge. See gamenet_manager.h.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "engine/memory/arena_alloc.h"
#include "engine/memory/manager.h"
#include "game/online/gamenet_handler_util.h" // IWYU pragma: keep - the detail namespace (using-directive)
#include "platform/gamespy.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "game/online/buddy_man.h"
#include "game/online/chat.h"
#include "game/online/gamenet_handlers.h"
#include "game/online/gamenet_lobby.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "game/online/service.h"
#include "game/online/stats_man.h"
#include "game/online/vault_man.h"
#include "engine/script/preferences.h"
#include "platform/crt.h"
#include "engine/assets/resource_registry.h"
#include "engine/memory/fwd.h"
#include "game/online/server_list.h"
#include "game/online/score_list.h"
#include "engine/sys/mlp.h"
#include <gsl/pointers>
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"
#include "wire/message_ids.h"
#include "game/online/online_identity.h"

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kNetSettingsKey = 0x7D1C1C0F; // (the global structure of the network preferences)
constexpr uint32_t kTauntSettingsKey = 0x878CB88F;
constexpr int kEarlyTaskPriority = -2000;
constexpr int kChallengeDigits = 10;
constexpr int kChallengeLetters = 36;
constexpr int kChallengeLength = 8;


// A list head (lists::Foremost<T>; 0x14 bytes at its offset): empty.
void setup_list_sentinel(memory::ListNode* head) { head->make_head(); }

// From the memory manager's current heap (its operator new: the manager made first if need be).
void* carve(uint32_t size)
{
    if (!memory::manager_ready_flag()) {
        memory::ensure_manager();
    }
    return memory::Hub::get_pull()->carve(size, 1, nullptr);
}

} // namespace

// 0x0053EE90
Hub::Hub()
{
    // (flag_bits_: a word in the original - the three bytes after it are zero already)
    setup_list_sentinel(&roster_.players()); // the players
    setup_list_sentinel(&roster_.joiners()); // the joining players
    setup_list_sentinel(&script_queue_.queued_scripts()); // the queued scripts
    setup_list_sentinel(&servers_);        // the servers
    setup_list_sentinel(&waiting_servers_);
    tasks::PileUp* tasks = (&assets::get_main_loop_manager()->logic_tasks());
    net::Hub::sole()->set_msg_options(MessageId::kObjectUpdates, 1); // (the object update message's)
    settings_.read_in(kNetSettingsKey, kTauntSettingsKey);
    tasks_.make_all(this);
    tasks->queue_job(tasks_.connection_job().get());
    void* memory = carve(sizeof(RoomManager));
    lobby_service_.reset(memory ? memory::construct_in<RoomManager>(memory) : nullptr);
    memory = carve(sizeof(VaultMan)); // (the original's is 0x3C8 bytes: its search has a vtable)
    vault_man_.reset(memory ? new (memory) VaultMan : nullptr);
    memory = carve(sizeof(FriendsManager));
    presence_service_.reset(memory ? new (memory) FriendsManager : nullptr);
    memory = carve(sizeof(RankingsManager));
    rating_service_.reset(memory ? new (memory) RankingsManager : nullptr);
    game_spy::cd_key_init(net::kGameSpyGameId); // the CD-key SDK (gcd_init)
    // The CD-key challenge: eight random letters or digits (the game's C runtime srand(time(0)) / rand).
    crt::get_srand(crt::get_instant());
    for (int i = 0; i < kChallengeLength; ++i) {
        const int pick = crt::get_rand() % 62;
        int character = 'a' + pick - kChallengeLetters; // (a digit, a capital or a small letter)
        if (pick < kChallengeDigits) {
            character = '0' + pick;
        } else if (pick < kChallengeLetters) {
            character = 'A' + pick - kChallengeDigits;
        }
        core::at(challenge_, i) = static_cast<char>(character);
    }
    core::at(challenge_, kChallengeLength) = '\0';
}

// NOT ORIGINAL (the hub's constructor makes them in its body, 0x0053EE90)
void HubTasks::make_all(Hub* hub)
{
    connection_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::connection_task), hub, kEarlyTaskPriority);
    metrics_job_ = std::make_unique<tasks::Task<Hub>>(&net_metrics_task, hub, 0);
    server_list_job_ = std::make_unique<tasks::Task<Hub>>((&online::server_list_task), hub, 0);
    auto_serve_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::auto_serve_task), hub, 0);
    score_broadcast_job_ = std::make_unique<tasks::Task<Hub>>((&online::score_broadcast_tick), hub, 0);
    guest_admit_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::guest_player_intake_tick), hub, kEarlyTaskPriority);
    host_admit_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::host_player_intake_tick), hub, kEarlyTaskPriority);
    keyboard_job_ = std::make_unique<tasks::Task<Hub>>(&keyboard_task, hub, 0);
    dialup_job_ = std::make_unique<tasks::Task<Hub>>(&dialup_progress_tick, hub, 0);
    join_expiry_job_ = std::make_unique<tasks::Task<Hub>>(&join_deadline_tick, hub, 0);
    service_job_ = std::make_unique<tasks::Task<Hub>>((&net::service_task), hub, 0);
    join_phase_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::join_progress_tick), hub, 0);
    start_game_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::start_game_task), hub, 0);
    level_change_job_ = std::make_unique<tasks::Task<Hub>>(&level_change_task, hub, 0);
    flag_carry_job_ = std::make_unique<tasks::Task<Hub>>(&carried_flags_task, hub, 0);
}

// NOT ORIGINAL (the hub's constructor reads them in its body, 0x0053EE90)
void SessionSettings::read_in(uint32_t network_key, uint32_t taunt_key)
{
    net_options_.read_in(network_key);
    taunt_options_.read_in(taunt_key);
}

// 0x0053F520
Hub* Hub::fabricate(bool create)
{
    Hub* instance = net_game_state().online_hub();
    if (!instance && create) {
        void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1);
        instance = memory ? memory::construct_in<Hub>(memory) : nullptr;
        net_game_state().set_online_hub(instance);
    }
    ++net_game_state().game_net_references(); // (the references)
    return instance;
}

// NOT ORIGINAL
void Hub::check_layout()
{
    // Each field's offset: the original's, less what the smaller members before it save here - the two preference
    // sets are 8 bytes, not 0xC (4 each), the five list heads 0x10, not 0x14 (4 each). The size counts in the heaps.
    struct FieldAt {
        std::size_t now;
        std::size_t original;
        std::size_t smaller_by;
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(Hub, flag_bits_), .original = 0x0, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, unknown1_), .original = 0x1, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, host_), .original = 0x4, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, client_app_), .original = 0x8, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, client1_), .original = 0xC, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, lobby_service_), .original = 0x10, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, vault_man_), .original = 0x14, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, presence_service_), .original = 0x18, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, rating_service_), .original = 0x1C, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, challenge_), .original = 0x20, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, browser_client_), .original = 0x2C, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, game_type_), .original = 0x30, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, level_), .original = 0x34, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, game_id_), .original = 0x38, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, ended_game_id_), .original = 0x39, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, match_ended_), .original = 0x3A, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, cheating_noted_), .original = 0x3B, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, join_mode_), .original = 0x3C, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, host_mode_), .original = 0x40, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, final_scores_received_), .original = 0x44, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, show_metrics_), .original = 0x45, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, show_scores_), .original = 0x46, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, show_names_), .original = 0x47, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, start_points_), .original = 0x48, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, crown_start_), .original = 0x68, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, crown_), .original = 0x6C, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, team_objects_), .original = 0x70, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, tasks_), .original = 0xC0, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, spectator_pad_listener_), .original = 0xFC, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, spectate_request_), .original = 0x100, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, settings_), .original = 0x104, .smaller_by = 0},
        FieldAt{.now = offsetof(Hub, roster_), .original = 0x11C, .smaller_by = 8},
        FieldAt{.now = offsetof(Hub, script_queue_), .original = 0x144, .smaller_by = 16},
        FieldAt{.now = offsetof(Hub, servers_), .original = 0x158, .smaller_by = 20},
        FieldAt{.now = offsetof(Hub, waiting_servers_), .original = 0x16C, .smaller_by = 24},
        FieldAt{.now = offsetof(Hub, unknown180_), .original = 0x180, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, refused_reason_), .original = 0x184, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, last_ready_probe_id_), .original = 0x188, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, cleared_to_start_), .original = 0x18C, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, score_synced_at_), .original = 0x190, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, script_flag_bits_), .original = 0x194, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, join_phase_), .original = 0x198, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, join_ip_), .original = 0x19C, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, join_private_ip_), .original = 0x1A0, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, join_port_), .original = 0x1A4, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, modem_state_shown_), .original = 0x1A8, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, join_began_at_), .original = 0x1AC, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, lobby_since_), .original = 0x1B0, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, match_being_set_up_), .original = 0x1B4, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, awaiting_start_), .original = 0x1B5, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, service_state_), .original = 0x1B8, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, next_service_state_), .original = 0x1BC, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, service_wake_at_), .original = 0x1C0, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, online_script_), .original = 0x1C4, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, online_fail_script_), .original = 0x1C8, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, viewed_player_), .original = 0x1CC, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, last_leader_), .original = 0x1D0, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, last_leading_team_), .original = 0x1D4, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, created_goals_), .original = 0x1D8, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, created_goals_size_), .original = 0x1948, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, created_goals_level_), .original = 0x194C, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, unknown1950_), .original = 0x1950, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, room_id_), .original = 0x9950, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, master_servers_), .original = 0x9954, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, news_buffer_), .original = 0x99F4, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, motd_text_), .original = 0xFDF4, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, pending_data_), .original = 0xFDF8, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, pending_data_size_), .original = 0xFDFC, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, request_), .original = 0xFE00, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, request_started_at_), .original = 0xFE04, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, download_done_), .original = 0xFE08, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, motd_received_), .original = 0xFE09, .smaller_by = 28},
        FieldAt{.now = offsetof(Hub, services_available_), .original = 0xFE0A, .smaller_by = 28},
        FieldAt{.now = sizeof(Hub), .original = 0xFE0C, .smaller_by = 28},
    };
    static_assert(std::ranges::all_of(
                      kLayout, [](const FieldAt& field) { return field.now + field.smaller_by == field.original; }),
                  "layout must match the original's, but for the smaller members");
}

// NOT ORIGINAL
void HubTasks::check_layout()
{
    constexpr std::size_t kAt = 0xC0; // (each field at its offset in the hub, less the tasks' own)
    struct FieldAt {
        std::size_t now;
        std::size_t original;
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(HubTasks, connection_job_), .original = 0xC0},
        FieldAt{.now = offsetof(HubTasks, metrics_job_), .original = 0xC4},
        FieldAt{.now = offsetof(HubTasks, score_broadcast_job_), .original = 0xC8},
        FieldAt{.now = offsetof(HubTasks, host_admit_job_), .original = 0xCC},
        FieldAt{.now = offsetof(HubTasks, guest_admit_job_), .original = 0xD0},
        FieldAt{.now = offsetof(HubTasks, keyboard_job_), .original = 0xD4},
        FieldAt{.now = offsetof(HubTasks, dialup_job_), .original = 0xD8},
        FieldAt{.now = offsetof(HubTasks, join_expiry_job_), .original = 0xDC},
        FieldAt{.now = offsetof(HubTasks, service_job_), .original = 0xE0},
        FieldAt{.now = offsetof(HubTasks, join_phase_job_), .original = 0xE4},
        FieldAt{.now = offsetof(HubTasks, start_game_job_), .original = 0xE8},
        FieldAt{.now = offsetof(HubTasks, level_change_job_), .original = 0xEC},
        FieldAt{.now = offsetof(HubTasks, server_list_job_), .original = 0xF0},
        FieldAt{.now = offsetof(HubTasks, auto_serve_job_), .original = 0xF4},
        FieldAt{.now = offsetof(HubTasks, flag_carry_job_), .original = 0xF8},
        FieldAt{.now = sizeof(HubTasks), .original = 0xFC},
    };
    static_assert(std::ranges::all_of(kLayout, [](const FieldAt& field) { return field.now + kAt == field.original; }),
                  "layout must match the original");
}

// NOT ORIGINAL
void SessionSettings::check_layout()
{
    constexpr std::size_t kAt = 0x104; // (each field at its offset in the hub, less the settings' own)
    struct FieldAt {
        std::size_t now;
        std::size_t original;
        std::size_t smaller_by; // (the network set is 8 bytes here, 0xC in the original)
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(SessionSettings, net_options_), .original = 0x104, .smaller_by = 0},
        FieldAt{.now = offsetof(SessionSettings, taunt_options_), .original = 0x110, .smaller_by = 4},
        FieldAt{.now = sizeof(SessionSettings), .original = 0x11C, .smaller_by = 8},
    };
    static_assert(
        std::ranges::all_of(kLayout,
                            [](const FieldAt& field) { return field.now + kAt + field.smaller_by == field.original; }),
        "layout must match the original's, but for the smaller members");
}

// NOT ORIGINAL
void PlayerRoster::check_layout()
{
    constexpr std::size_t kAt = 0x11C; // (each field at its offset in the hub, less the roster's own)
    struct FieldAt {
        std::size_t now;
        std::size_t original;
        std::size_t smaller_by; // (a list head is 0x10 bytes here, 0x14 in the original)
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(PlayerRoster, players_), .original = 0x11C, .smaller_by = 0},
        FieldAt{.now = offsetof(PlayerRoster, joiners_), .original = 0x130, .smaller_by = 4},
        FieldAt{.now = sizeof(PlayerRoster), .original = 0x144, .smaller_by = 8},
    };
    static_assert(
        std::ranges::all_of(kLayout,
                            [](const FieldAt& field) { return field.now + kAt + field.smaller_by == field.original; }),
        "layout must match the original's, but for the smaller members");
}

// NOT ORIGINAL
void ScriptQueue::check_layout()
{
    constexpr std::size_t kAt = 0x144; // (each field at its offset in the hub, less the queue's own)
    struct FieldAt {
        std::size_t now;
        std::size_t original;
        std::size_t smaller_by; // (a list head is 0x10 bytes here, 0x14 in the original)
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(ScriptQueue, queued_scripts_), .original = 0x144, .smaller_by = 0},
        FieldAt{.now = sizeof(ScriptQueue), .original = 0x158, .smaller_by = 4},
    };
    static_assert(
        std::ranges::all_of(kLayout,
                            [](const FieldAt& field) { return field.now + kAt + field.smaller_by == field.original; }),
        "layout must match the original's, but for the smaller members");
}

} // namespace online
