// The network manager made (online::Hub, 0x0053EE90 / 0x0053F520): its lists, preferences, tasks, the lobby,
// vault, presence and stats managers, and the CD-key challenge. See gamenet_manager.h.

#include <cstdint>

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
#include "engine/net/task_vtable.h"
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

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kNetSettingsKey = 0x7D1C1C0F; // (the global structure of the network preferences)
constexpr uint32_t kTauntSettingsKey = 0x878CB88F;
constexpr int kGameSpyGameId = 1003;
constexpr int kChallengeLength = 8;

// tasks::Task<Manager> (0x20 bytes, vtable 0x006476F0).
struct ManagerTask {
    uint8_t list_link_[0x18];
    void* code_value_;
    Hub* content_;
};
static_assert(sizeof(ManagerTask) == 0x20);

tasks::SchedulerTask* new_task(Hub* manager, void* code, int priority)
{
    auto* task = static_cast<ManagerTask*>(memory::alloc_zeroed(sizeof(ManagerTask)));
    if (task) {
        reinterpret_cast<tasks::SchedulerTask*>(task)->make_new(priority);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(game_code::kTaskVtable);
        task->code_value_ = code;
        task->content_ = manager;
    }
    return reinterpret_cast<tasks::SchedulerTask*>(task);
}
template <typename F>
void* get_code(F function)
{
    return reinterpret_cast<void*>(function);
}

// A list head (lists::Foremost<T>, its own vtable): empty.
void setup_list_sentinel(memory::ListNode* head, uint32_t vtable)
{
    head->priority = 0;
    head->data = reinterpret_cast<void*>(-1);
    head->next = head;
    head->prev = head;
    head->vtable = reinterpret_cast<void**>(vtable);
}

// From the memory manager's current heap (its operator new: the manager made first if need be).
void* carve(uint32_t size)
{
    if (!memory::g_manager_ready)
        memory::ensure_manager();
    return memory::Hub::get_pull()->carve(size, 1, nullptr);
}

} // namespace

// 0x0053EE90
Hub* Hub::make_new()
{
    prop<uint32_t>(this, 0) = 0; // the flags
    reinterpret_cast<prefs::Preferences*>(net_options_)->prepare_all();
    auto* taunts = &prop<prefs::Preferences>(this, 0x110);
    taunts->prepare_all();
    setup_list_sentinel(&prop<memory::ListNode>(this, 0x11C), reinterpret_cast<uint32_t>(kPlayerHeadVtable)); // the players
    setup_list_sentinel(&prop<memory::ListNode>(this, 0x130), reinterpret_cast<uint32_t>(kNewPlayerHeadVtable)); // the joining players
    setup_list_sentinel(&prop<memory::ListNode>(this, 0x144), reinterpret_cast<uint32_t>(kQueuedScriptVtable));
    setup_list_sentinel(&prop<memory::ListNode>(this, 0x158), reinterpret_cast<uint32_t>(kServerHeadVtable));
    setup_list_sentinel(&prop<memory::ListNode>(this, 0x16C), reinterpret_cast<uint32_t>(kServerHeadVtable));
    script_flag_bits_ = 0;
    tasks::PileUp* tasks =
        reinterpret_cast<tasks::PileUp*>(reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(assets::g_main_loop_manager)) + 4);
    net::Hub::sole()->set_msg_options(0x23, 1); // (the object update message's)
    client_app_ = nullptr;
    client1_ = nullptr;
    browser_client_ = nullptr;
    host_ = nullptr;
    prop<bool>(this, 0x45) = false;
    prop<bool>(this, 0x46) = false;
    prop<bool>(this, 0x3B) = false;
    prop<bool>(this, 0x47) = true; // the players' names shown
    prop<uint32_t>(this, 0x180) = 0;
    host_mode_ = 0;
    prop<int>(this, 0x1B8) = 2;
    prop<int>(this, 0x1BC) = 2;
    reinterpret_cast<prefs::Preferences*>(net_options_)->read_in(kNetSettingsKey);
    taunts->read_in(kTauntSettingsKey);
    prop<tasks::SchedulerTask*>(this, 0xC0) = new_task(this, get_code(&Hub::connection_task), -2000);
    prop<tasks::SchedulerTask*>(this, 0xC4) = new_task(this, get_code(&net_metrics_task), 0);
    prop<tasks::SchedulerTask*>(this, 0xF0) = new_task(this, code_address(online::server_list_task), 0);
    prop<tasks::SchedulerTask*>(this, 0xF4) = new_task(this, get_code(&Hub::auto_serve_task), 0);
    prop<tasks::SchedulerTask*>(this, 0xC8) = new_task(this, code_address(online::score_broadcast_tick), 0);
    guest_admit_job_ = new_task(this, get_code(&Hub::guest_player_intake_tick), -2000);
    host_admit_job_ = new_task(this, get_code(&Hub::host_player_intake_tick), -2000);
    prop<tasks::SchedulerTask*>(this, 0xD4) = new_task(this, get_code(&keyboard_task), 0);
    prop<tasks::SchedulerTask*>(this, 0xD8) = new_task(this, get_code(&dialup_progress_tick), 0);
    join_expiry_job_ = new_task(this, get_code(&join_deadline_tick), 0);
    prop<tasks::SchedulerTask*>(this, 0xE0) = new_task(this, get_code(&net::service_task), 0);
    join_phase_job_ = new_task(this, get_code(&Hub::join_progress_tick), 0);
    prop<tasks::SchedulerTask*>(this, 0xE8) = new_task(this, get_code(&Hub::start_game_task), 0);
    prop<tasks::SchedulerTask*>(this, 0xEC) = new_task(this, get_code(&level_change_task), 0);
    prop<tasks::SchedulerTask*>(this, 0xF8) = new_task(this, code_address(carried_flags_task), 0);
    tasks->queue_job(prop<tasks::SchedulerTask*>(this, 0xC0));
    prop<int>(this, 0x1A8) = 5; // the modem state shown last
    prop<uint32_t>(this, 0x1948) = 0; // the created goals' message
    prop<uint32_t>(this, 0x194C) = 0;
    join_mode_ = 0;
    prop<bool>(this, 0xFE09) = false;
    void* memory = carve(0x154);
    lobby_service_ = memory ? static_cast<RoomManager*>(memory)->make_new() : nullptr;
    memory = carve(0x3C8);
    vault_man_ = memory ? static_cast<VaultMan*>(memory)->make_new() : nullptr;
    memory = carve(0x3C);
    presence_service_ = memory ? static_cast<FriendsManager*>(memory)->make_new() : nullptr;
    memory = carve(0x200);
    rating_service_ = memory ? static_cast<RankingsManager*>(memory)->make_new() : nullptr;
    game_spy::cd_key_init(kGameSpyGameId); // the CD-key SDK (gcd_init)
    // The CD-key challenge: eight random letters or digits (the game's C runtime srand(time(0)) / rand).
    crt::get_srand(crt::get_instant());
    for (int i = 0; i < kChallengeLength; ++i) {
        int c = crt::get_rand() % 62;
        challenge_[i] = static_cast<char>(c < 10 ? '0' + c : c < 36 ? 'A' + c - 10 : 'a' + c - 36);
    }
    challenge_[kChallengeLength] = '\0';
    return this;
}

uint32_t g_game_net_references;

// 0x0053F520
Hub* __cdecl Hub::fabricate(bool create)
{
    Hub*& instance = g_online_hub;
    if (!instance && create) {
        void* memory = memory::alloc_zeroed(0xFE0C, nullptr, 1);
        instance = memory ? static_cast<Hub*>(memory)->make_new() : nullptr;
    }
    ++g_game_net_references; // (the references)
    return instance;
}

} // namespace online
