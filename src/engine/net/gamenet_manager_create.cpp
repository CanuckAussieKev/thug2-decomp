// The network manager made (online::Hub, 0x0053EE90 / 0x0053F520): its lists, preferences, tasks, the lobby,
// vault, presence and stats managers, and the CD-key challenge. See gamenet_manager.h.

#include <cstdint>
#include <memory>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/net/gamenet_handler_util.h" // IWYU pragma: keep - the detail namespace (using-directive)
#include "platform/gamespy.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/chat.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "engine/net/service.h"
#include "engine/net/stats_man.h"
#include "engine/net/vault_man.h"
#include "engine/script/preferences.h"
#include "platform/crt.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/fwd.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/sys/mlp.h"
#include <gsl/pointers>
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"
#include "engine/net/online_identity.h"

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
    setup_list_sentinel(&players_);        // the players
    setup_list_sentinel(&joiners_);        // the joining players
    setup_list_sentinel(&queued_scripts_); // the queued scripts
    setup_list_sentinel(&servers_);        // the servers
    setup_list_sentinel(&waiting_servers_);
    tasks::PileUp* tasks = (&assets::get_main_loop_manager()->logic_tasks());
    net::Hub::sole()->set_msg_options(MessageId::kObjectUpdates, 1); // (the object update message's)
    net_options_.read_in(kNetSettingsKey);
    taunt_options_.read_in(kTauntSettingsKey);
    connection_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::connection_task), this, kEarlyTaskPriority);
    metrics_job_ = std::make_unique<tasks::Task<Hub>>(&net_metrics_task, this, 0);
    server_list_job_ = std::make_unique<tasks::Task<Hub>>((&online::server_list_task), this, 0);
    auto_serve_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::auto_serve_task), this, 0);
    score_broadcast_job_ = std::make_unique<tasks::Task<Hub>>((&online::score_broadcast_tick), this, 0);
    guest_admit_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::guest_player_intake_tick), this, kEarlyTaskPriority);
    host_admit_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::host_player_intake_tick), this, kEarlyTaskPriority);
    keyboard_job_ = std::make_unique<tasks::Task<Hub>>(&keyboard_task, this, 0);
    dialup_job_ = std::make_unique<tasks::Task<Hub>>(&dialup_progress_tick, this, 0);
    join_expiry_job_ = std::make_unique<tasks::Task<Hub>>(&join_deadline_tick, this, 0);
    service_job_ = std::make_unique<tasks::Task<Hub>>((&net::service_task), this, 0);
    join_phase_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::join_progress_tick), this, 0);
    start_game_job_ = std::make_unique<tasks::Task<Hub>>((&Hub::start_game_task), this, 0);
    level_change_job_ = std::make_unique<tasks::Task<Hub>>(&level_change_task, this, 0);
    flag_carry_job_ = std::make_unique<tasks::Task<Hub>>(&carried_flags_task, this, 0);
    tasks->queue_job(connection_job_.get());
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

} // namespace online
