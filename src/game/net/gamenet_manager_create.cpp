// The network manager made (GameNet::Manager, 0x0053EE90 / 0x0053F520): its lists, preferences, tasks, the lobby,
// vault, presence and stats managers, and the CD-key challenge. See gamenet_manager.h.

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/buddy_man.h"
#include "game/net/chat.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/net/service.h"
#include "game/net/stats_man.h"
#include "game/net/vault_man.h"
#include "game/script/preferences.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint32_t kNetworkPreferences = 0x7D1C1C0F; // (the global structure of the network preferences)
constexpr uint32_t kTauntPreferences = 0x878CB88F;
constexpr int kGameSpyGameId = 1003;
constexpr int kChallengeLength = 8;

// Tsk::Task<Manager> (0x20 bytes, vtable 0x006476F0).
struct ManagerTask {
    uint8_t m_node[0x18];
    void* m_code;
    Manager* m_data;
};
static_assert(sizeof(ManagerTask) == 0x20);

Tsk::BaseTask* NewTask(Manager* manager, void* code, int priority)
{
    auto* task = static_cast<ManagerTask*>(Mem::Calloc(sizeof(ManagerTask)));
    if (task) {
        reinterpret_cast<Tsk::BaseTask*>(task)->Construct(priority);
        *reinterpret_cast<uint32_t*>(task) = 0x006476F0;
        task->m_code = code;
        task->m_data = manager;
    }
    return reinterpret_cast<Tsk::BaseTask*>(task);
}
template <typename F>
void* Code(F function)
{
    return reinterpret_cast<void*>(function);
}

// A list head (Lst::Head<T>, its own vtable): empty.
void InitHead(Mem::ListNode* head, uint32_t vtable)
{
    head->priority = 0;
    head->data = reinterpret_cast<void*>(-1);
    head->next = head;
    head->prev = head;
    head->vtable = reinterpret_cast<void**>(vtable);
}

// From the memory manager's current heap (its operator new: the manager made first if need be).
void* Allocate(uint32_t size)
{
    if (!GameVariable<bool>(0x007CC979))
        Mem::EnsureManager();
    return Mem::Manager::Get()->Allocate(size, 1, nullptr);
}

} // namespace

// 0x0053EE90
Manager* Manager::Construct()
{
    Field<uint32_t>(this, 0) = 0; // the flags
    reinterpret_cast<Prefs::Preferences*>(m_networkPreferences)->Init();
    auto* taunts = &Field<Prefs::Preferences>(this, 0x110);
    taunts->Init();
    InitHead(&Field<Mem::ListNode>(this, 0x11C), 0x0064C9F0); // the players
    InitHead(&Field<Mem::ListNode>(this, 0x130), 0x0064C9F4); // the joining players
    InitHead(&Field<Mem::ListNode>(this, 0x144), 0x0064C9F8);
    InitHead(&Field<Mem::ListNode>(this, 0x158), 0x0064C9FC);
    InitHead(&Field<Mem::ListNode>(this, 0x16C), 0x0064C9FC);
    m_proSetFlags = 0;
    Tsk::Stack* tasks =
        reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(GameVariable<void*>(0x00696D68)) + 4);
    Net::Manager::Instance()->SetMessageFlags(0x23, 1); // (the object update message's)
    m_client = nullptr;
    m_client1 = nullptr;
    m_matchClient = nullptr;
    m_server = nullptr;
    Field<bool>(this, 0x45) = false;
    Field<bool>(this, 0x46) = false;
    Field<bool>(this, 0x3B) = false;
    Field<bool>(this, 0x47) = true; // the players' names shown
    Field<uint32_t>(this, 0x180) = 0;
    m_hostMode = 0;
    Field<int>(this, 0x1B8) = 2;
    Field<int>(this, 0x1BC) = 2;
    reinterpret_cast<Prefs::Preferences*>(m_networkPreferences)->Load(kNetworkPreferences);
    taunts->Load(kTauntPreferences);
    Field<Tsk::BaseTask*>(this, 0xC0) = NewTask(this, Code(&Manager::ConnectionTask), -2000);
    Field<Tsk::BaseTask*>(this, 0xC4) = NewTask(this, Code(&NetMetricsTask), 0);
    Field<Tsk::BaseTask*>(this, 0xF0) = NewTask(this, reinterpret_cast<void*>(0x00547440), 0);
    Field<Tsk::BaseTask*>(this, 0xF4) = NewTask(this, Code(&Manager::AutoServeTask), 0);
    Field<Tsk::BaseTask*>(this, 0xC8) = NewTask(this, reinterpret_cast<void*>(0x00544780), 0);
    m_clientAddNewPlayersTask = NewTask(this, Code(&Manager::ClientAddNewPlayersTask), -2000);
    m_serverAddNewPlayersTask = NewTask(this, Code(&Manager::ServerAddNewPlayersTask), -2000);
    Field<Tsk::BaseTask*>(this, 0xD4) = NewTask(this, Code(&KeyboardTask), 0);
    Field<Tsk::BaseTask*>(this, 0xD8) = NewTask(this, Code(&ModemStateTask), 0);
    m_joinTimeoutTask = NewTask(this, Code(&JoinTimeoutTask), 0);
    Field<Tsk::BaseTask*>(this, 0xE0) = NewTask(this, Code(&Net::ServiceTask), 0);
    m_joinStateTask = NewTask(this, Code(&Manager::JoinStateTask), 0);
    Field<Tsk::BaseTask*>(this, 0xE8) = NewTask(this, Code(&Manager::StartGameTask), 0);
    Field<Tsk::BaseTask*>(this, 0xEC) = NewTask(this, Code(&LevelChangeTask), 0);
    Field<Tsk::BaseTask*>(this, 0xF8) = NewTask(this, reinterpret_cast<void*>(0x00539610), 0);
    tasks->AddTask(Field<Tsk::BaseTask*>(this, 0xC0));
    Field<int>(this, 0x1A8) = 5; // the modem state shown last
    Field<uint32_t>(this, 0x1948) = 0; // the created goals' message
    Field<uint32_t>(this, 0x194C) = 0;
    m_joinMode = 0;
    Field<bool>(this, 0xFE09) = false;
    void* memory = Allocate(0x154);
    m_lobbyMan = memory ? static_cast<LobbyMan*>(memory)->Construct() : nullptr;
    memory = Allocate(0x3C8);
    m_vaultMan = memory ? static_cast<VaultMan*>(memory)->Construct() : nullptr;
    memory = Allocate(0x3C);
    m_buddyMan = memory ? static_cast<BuddyMan*>(memory)->Construct() : nullptr;
    memory = Allocate(0x200);
    m_statsMan = memory ? static_cast<StatsMan*>(memory)->Construct() : nullptr;
    GameFunction<int(__cdecl*)(int)>(0x005F9540)(kGameSpyGameId); // the CD-key SDK (gcd_init)
    // The CD-key challenge: eight random letters or digits (the game's C runtime srand(time(0)) / rand).
    GameFunction<void(__cdecl*)(unsigned int)>(0x00623B96)(
        static_cast<unsigned int>(GameFunction<int(__cdecl*)(void*)>(0x00625138)(nullptr)));
    for (int i = 0; i < kChallengeLength; ++i) {
        int c = GameFunction<int(__cdecl*)()>(0x00623BA3)() % 62;
        m_challenge[i] = static_cast<char>(c < 10 ? '0' + c : c < 36 ? 'A' + c - 10 : 'a' + c - 36);
    }
    m_challenge[kChallengeLength] = '\0';
    return this;
}

GAME_GLOBAL(uint32_t, g_gameNetReferences, 0x007CCE18);

// 0x0053F520
Manager* __cdecl Manager::Create(bool create)
{
    Manager*& instance = g_gameNetManager;
    if (!instance && create) {
        void* memory = Mem::Calloc(0xFE0C, nullptr, 1);
        instance = memory ? static_cast<Manager*>(memory)->Construct() : nullptr;
    }
    ++g_gameNetReferences; // (the references)
    return instance;
}

} // namespace GameNet

REPLACE(0x0053EE90, &GameNet::Manager::Construct);
REPLACE(0x0053F520, GameNet::Manager::Create);
