// The network manager made (OnlinePlay::Hub, 0x0053EE90 / 0x0053F520): its lists, preferences, tasks, the lobby,
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

namespace OnlinePlay {

using namespace Detail;

namespace {

constexpr uint32_t kNetSettingsKey = 0x7D1C1C0F; // (the global structure of the network preferences)
constexpr uint32_t kTauntSettingsKey = 0x878CB88F;
constexpr int kGameSpyGameId = 1003;
constexpr int kChallengeLength = 8;

// Scheduling::Task<Manager> (0x20 bytes, vtable 0x006476F0).
struct ManagerTask {
    uint8_t m_listLink[0x18];
    void* m_codeValue;
    Hub* m_content;
};
static_assert(sizeof(ManagerTask) == 0x20);

Scheduling::SchedulerTask* NewTask(Hub* manager, void* code, int priority)
{
    auto* task = static_cast<ManagerTask*>(MemSys::AllocZeroed(sizeof(ManagerTask)));
    if (task) {
        reinterpret_cast<Scheduling::SchedulerTask*>(task)->MakeNew(priority);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task->m_codeValue = code;
        task->m_content = manager;
    }
    return reinterpret_cast<Scheduling::SchedulerTask*>(task);
}
template <typename F>
void* Code(F function)
{
    return reinterpret_cast<void*>(function);
}

// A list head (ListKit::Foremost<T>, its own vtable): empty.
void SetupListSentinel(MemSys::ListNode* head, uint32_t vtable)
{
    head->priority = 0;
    head->data = reinterpret_cast<void*>(-1);
    head->next = head;
    head->prev = head;
    head->vtable = reinterpret_cast<void**>(vtable);
}

// From the memory manager's current heap (its operator new: the manager made first if need be).
void* Carve(uint32_t size)
{
    if (!MemSys::g_managerReady)
        MemSys::EnsureManager();
    return MemSys::Hub::Pull()->Carve(size, 1, nullptr);
}

} // namespace

// 0x0053EE90
Hub* Hub::MakeNew()
{
    Prop<uint32_t>(this, 0) = 0; // the flags
    reinterpret_cast<OptionStore::Preferences*>(m_netOptions)->PrepareAll();
    auto* taunts = &Prop<OptionStore::Preferences>(this, 0x110);
    taunts->PrepareAll();
    SetupListSentinel(&Prop<MemSys::ListNode>(this, 0x11C), reinterpret_cast<uint32_t>(kPlayerHeadVtable)); // the players
    SetupListSentinel(&Prop<MemSys::ListNode>(this, 0x130), reinterpret_cast<uint32_t>(kNewPlayerHeadVtable)); // the joining players
    SetupListSentinel(&Prop<MemSys::ListNode>(this, 0x144), reinterpret_cast<uint32_t>(kQueuedScriptVtable));
    SetupListSentinel(&Prop<MemSys::ListNode>(this, 0x158), reinterpret_cast<uint32_t>(kServerHeadVtable));
    SetupListSentinel(&Prop<MemSys::ListNode>(this, 0x16C), reinterpret_cast<uint32_t>(kServerHeadVtable));
    m_scriptFlagBits = 0;
    Scheduling::PileUp* tasks =
        reinterpret_cast<Scheduling::PileUp*>(reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(AssetStore::g_mainLoopManager)) + 4);
    NetLink::Hub::Sole()->SetMsgOptions(0x23, 1); // (the object update message's)
    m_clientApp = nullptr;
    m_client1 = nullptr;
    m_browserClient = nullptr;
    m_host = nullptr;
    Prop<bool>(this, 0x45) = false;
    Prop<bool>(this, 0x46) = false;
    Prop<bool>(this, 0x3B) = false;
    Prop<bool>(this, 0x47) = true; // the players' names shown
    Prop<uint32_t>(this, 0x180) = 0;
    m_hostMode = 0;
    Prop<int>(this, 0x1B8) = 2;
    Prop<int>(this, 0x1BC) = 2;
    reinterpret_cast<OptionStore::Preferences*>(m_netOptions)->ReadIn(kNetSettingsKey);
    taunts->ReadIn(kTauntSettingsKey);
    Prop<Scheduling::SchedulerTask*>(this, 0xC0) = NewTask(this, Code(&Hub::ConnectionTask), -2000);
    Prop<Scheduling::SchedulerTask*>(this, 0xC4) = NewTask(this, Code(&NetMetricsTask), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xF0) = NewTask(this, CodeAddress(OnlinePlay::ServerListTask), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xF4) = NewTask(this, Code(&Hub::AutoServeTask), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xC8) = NewTask(this, CodeAddress(OnlinePlay::ScoreBroadcastTick), 0);
    m_guestAdmitJob = NewTask(this, Code(&Hub::GuestPlayerIntakeTick), -2000);
    m_hostAdmitJob = NewTask(this, Code(&Hub::HostPlayerIntakeTick), -2000);
    Prop<Scheduling::SchedulerTask*>(this, 0xD4) = NewTask(this, Code(&KeyboardTask), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xD8) = NewTask(this, Code(&DialupProgressTick), 0);
    m_joinExpiryJob = NewTask(this, Code(&JoinDeadlineTick), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xE0) = NewTask(this, Code(&NetLink::ServiceTask), 0);
    m_joinPhaseJob = NewTask(this, Code(&Hub::JoinProgressTick), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xE8) = NewTask(this, Code(&Hub::StartGameTask), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xEC) = NewTask(this, Code(&LevelChangeTask), 0);
    Prop<Scheduling::SchedulerTask*>(this, 0xF8) = NewTask(this, CodeAddress(CarriedFlagsTask), 0);
    tasks->QueueJob(Prop<Scheduling::SchedulerTask*>(this, 0xC0));
    Prop<int>(this, 0x1A8) = 5; // the modem state shown last
    Prop<uint32_t>(this, 0x1948) = 0; // the created goals' message
    Prop<uint32_t>(this, 0x194C) = 0;
    m_joinMode = 0;
    Prop<bool>(this, 0xFE09) = false;
    void* memory = Carve(0x154);
    m_lobbyService = memory ? static_cast<RoomManager*>(memory)->MakeNew() : nullptr;
    memory = Carve(0x3C8);
    m_vaultMan = memory ? static_cast<VaultMan*>(memory)->MakeNew() : nullptr;
    memory = Carve(0x3C);
    m_presenceService = memory ? static_cast<FriendsManager*>(memory)->MakeNew() : nullptr;
    memory = Carve(0x200);
    m_ratingService = memory ? static_cast<RankingsManager*>(memory)->MakeNew() : nullptr;
    GameSpy::CdKeyInit(kGameSpyGameId); // the CD-key SDK (gcd_init)
    // The CD-key challenge: eight random letters or digits (the game's C runtime srand(time(0)) / rand).
    Crt::Srand(Crt::Instant());
    for (int i = 0; i < kChallengeLength; ++i) {
        int c = Crt::Rand() % 62;
        m_challenge[i] = static_cast<char>(c < 10 ? '0' + c : c < 36 ? 'A' + c - 10 : 'a' + c - 36);
    }
    m_challenge[kChallengeLength] = '\0';
    return this;
}

uint32_t g_gameNetReferences;

// 0x0053F520
Hub* __cdecl Hub::Fabricate(bool create)
{
    Hub*& instance = g_onlineHub;
    if (!instance && create) {
        void* memory = MemSys::AllocZeroed(0xFE0C, nullptr, 1);
        instance = memory ? static_cast<Hub*>(memory)->MakeNew() : nullptr;
    }
    ++g_gameNetReferences; // (the references)
    return instance;
}

} // namespace OnlinePlay
