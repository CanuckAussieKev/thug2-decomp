// The network manager made (online::Hub, 0x0053EE90 / 0x0053F520): its lists, preferences, tasks, the lobby,
// vault, presence and stats managers, and the CD-key challenge. See gamenet_manager.h.

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/net/gamespy.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/chat.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lists.h"
#include "engine/net/net_manager.h"
#include "engine/net/service.h"
#include "engine/net/stats_man.h"
#include "engine/net/vault_man.h"
#include "engine/script/preferences.h"
#include "platform/crt.h"
#include "engine/core/code_address.h"
#include "engine/assets/resource_registry.h"
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
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/sys/mlp.h"
#include "engine/core/owner.h"

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kNetSettingsKey = 0x7D1C1C0F; // (the global structure of the network preferences)
constexpr uint32_t kTauntSettingsKey = 0x878CB88F;
constexpr int kGameSpyGameId = 1003;
constexpr int kChallengeLength = 8;

std::unique_ptr<tasks::SchedulerTask> new_task(Hub* manager, tasks::Task<Hub>::Code code, int priority)
{
    return std::make_unique<tasks::Task<Hub>>(code, manager, priority);
}

// A list head (lists::Foremost<T>; 0x14 bytes at its offset): empty.
void setup_list_sentinel(memory::ListNode* head) { head->make_head(); }

// From the memory manager's current heap (its operator new: the manager made first if need be).
void* carve(uint32_t size)
{
    if (!memory::g_manager_ready)
        memory::ensure_manager();
    return memory::Hub::get_pull()->carve(size, 1, nullptr);
}

} // namespace

// 0x0053EE90
Hub::Hub()
{
    // (flag_bits_: a word in the original - the three bytes after it are zero already)
    new (net_options_) prefs::Preferences;
    auto* taunts = new (taunt_options_) prefs::Preferences;
    setup_list_sentinel(&players_);        // the players
    setup_list_sentinel(&joiners_);        // the joining players
    setup_list_sentinel(&queued_scripts_); // the queued scripts
    setup_list_sentinel(&servers_);        // the servers
    setup_list_sentinel(&waiting_servers_);
    tasks::PileUp* tasks = (&assets::g_main_loop_manager->logic_tasks_);
    net::Hub::sole()->set_msg_options(0x23, 1); // (the object update message's)
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    client_app_ = nullptr;
    client1_ = nullptr;
    browser_client_ = nullptr;
    host_ = nullptr;
    show_metrics_ = false;
    show_scores_ = false;
    cheating_noted_ = false;
    show_names_ = true; // the players' names shown
    unknown180_ = 0;
    host_mode_ = 0;
    service_state_ = 2;
    next_service_state_ = 2;
    reinterpret_cast<prefs::Preferences*>(net_options_)->read_in(kNetSettingsKey);
    taunts->read_in(kTauntSettingsKey);
    connection_job_ = new_task(this, &Hub::connection_task, -2000);
    metrics_job_ = new_task(this, &net_metrics_task, 0);
    server_list_job_ = new_task(this, &online::server_list_task, 0);
    auto_serve_job_ = new_task(this, &Hub::auto_serve_task, 0);
    score_broadcast_job_ = new_task(this, &online::score_broadcast_tick, 0);
    guest_admit_job_ = new_task(this, &Hub::guest_player_intake_tick, -2000);
    host_admit_job_ = new_task(this, &Hub::host_player_intake_tick, -2000);
    keyboard_job_ = new_task(this, &keyboard_task, 0);
    dialup_job_ = new_task(this, &dialup_progress_tick, 0);
    join_expiry_job_ = new_task(this, &join_deadline_tick, 0);
    service_job_ = new_task(this, &net::service_task, 0);
    join_phase_job_ = new_task(this, &Hub::join_progress_tick, 0);
    start_game_job_ = new_task(this, &Hub::start_game_task, 0);
    level_change_job_ = new_task(this, &level_change_task, 0);
    flag_carry_job_ = new_task(this, &carried_flags_task, 0);
    tasks->queue_job(connection_job_.get());
    modem_state_shown_ = 5;  // the modem state shown last
    created_goals_size_ = 0; // the created goals' message
    created_goals_level_ = 0;
    join_mode_ = 0;
    motd_received_ = false;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    void* memory = carve(sizeof(RoomManager));
    lobby_service_.reset(memory ? new (memory) RoomManager : nullptr);
    memory = carve(sizeof(VaultMan)); // (the original's is 0x3C8 bytes: its search has a vtable)
    vault_man_.reset(memory ? new (memory) VaultMan : nullptr);
    memory = carve(sizeof(FriendsManager));
    presence_service_.reset(memory ? new (memory) FriendsManager : nullptr);
    memory = carve(sizeof(RankingsManager));
    rating_service_.reset(memory ? new (memory) RankingsManager : nullptr);
    game_spy::cd_key_init(kGameSpyGameId); // the CD-key SDK (gcd_init)
    // The CD-key challenge: eight random letters or digits (the game's C runtime srand(time(0)) / rand).
    crt::get_srand(crt::get_instant());
    for (int i = 0; i < kChallengeLength; ++i) {
        const int c = crt::get_rand() % 62;
        challenge_[i] = static_cast<char>(c < 10 ? '0' + c : c < 36 ? 'A' + c - 10 : 'a' + c - 36);
    }
    challenge_[kChallengeLength] = '\0';
}

uint32_t g_game_net_references;

// 0x0053F520
Hub* Hub::fabricate(bool create)
{
    Hub*& instance = g_online_hub;
    if (!instance && create) {
        void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1);
        instance = memory ? new (memory) Hub : nullptr;
    }
    ++g_game_net_references; // (the references)
    return instance;
}

} // namespace online
