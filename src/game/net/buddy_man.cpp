// The online profile and buddy list (OnlinePlay::FriendsManager). See buddy_man.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_ntoa: what the game uses

#include "game/net/buddy_man.h"
#include "game/net/gamespy.h"

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/str.h"
#include "game/core/task.h"
#include "game/lst/search.h"
#include "game/mth/random.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/online_text.h"
#include "game/net/server_list.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/ass/assman.h"
#include "game/net/task_vtable.h"
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
#include "game/net/stats_man.h"

namespace OnlinePlay {

namespace {

using Detail::DeleteParams;
using Detail::NewParams;

static VtableSlot kBuddyVtable_slots[] = {&OnlinePlay::Buddy::Destroy};
void** const kBuddyVtable = reinterpret_cast<void**>(kBuddyVtable_slots); // ListKit::Node<Buddy> (Buddy)
static VtableSlot kMessageVtable_slots[] = {&OnlinePlay::BuddyMessage::Destroy};
void** const kMessageVtable = reinterpret_cast<void**>(kMessageVtable_slots); // ListKit::Node<BuddyMessage>

constexpr int kMaxBuddies = 30;          // BuddyListFull
constexpr int kMaxMessages = 20;         // more than this waiting: new ones are dropped
constexpr uint32_t kLoginTimeout = 10000; // ms
constexpr uint32_t kProcessInterval = 1000; // ms between gpProcess calls once logged in
constexpr int kJoinPort = 0x1421;        // THUG2's join port (a buddy behind the same public address)
constexpr int kStatusPlaying = 2;        // GP_PLAYING

// GP error codes (OnError).
constexpr int kErrorLogin = 0x107;
constexpr int kErrorNewUserNick = 0x201;
constexpr int kErrorPassword = 0x202;
constexpr int kErrorAlreadyBuddy = 0x603;

bool g_creatingProfile;
uint32_t g_buddyLoginStart;
uint32_t g_lastProcess;

// The login's start (the task's 10 s limit), when GP was last processed once logged in, and "a new profile is being
// made" (its gpConnectNewUser answer "nick taken" logs in instead).
uint32_t& LoginStart() { return g_buddyLoginStart; }
uint32_t& LastProcess() { return g_lastProcess; }
bool& CreatingProfile() { return g_creatingProfile; }

Manager* NetManager() { return g_gameNetManager; }
MemSys::Manager* Memory() { return MemSys::Manager::Get(); }
void PushTopDown() { Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap); }
void PushBottomUp() { Memory()->EnterHeapScope(Memory()->m_internetBottomUpHeap); }
void PopHeap() { Memory()->LeaveHeapScope(); }
TaskList::Stack* LogicTasks() { return reinterpret_cast<TaskList::Stack*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04); }

uint32_t Checksums(const char* name) { return Checksums::HashFromText(name); }
void Run(const char* script, Scripts::CStruct* params = nullptr) { Scripts::LaunchScript(script, params, nullptr, false); }

// The GameSpy presence SDK (library code, not decompiled; cdecl).
void GpInitialize(void* connection, int productId, int namespaceId)
{
    GameSpy::GpInitialize(connection, productId, namespaceId);
}
void GpSetCallback(void* connection, int type, const void* callback, void* param)
{
    GameSpy::GpSetCallback(connection, type, callback, param);
}
void GpProcess(void* connection) { GameSpy::GpProcess(connection); }
void GpDisconnect(void* connection) { GameSpy::GpDisconnect(connection); }
void GpSendBuddyRequest(void* connection, int profile, const char* reason)
{
    GameSpy::GpSendBuddyRequest(connection, profile, reason);
}
void GpDeleteBuddy(void* connection, int profile)
{
    GameSpy::GpDeleteBuddy(connection, profile);
}
void GpSetStatus(void* connection, int status, const char* text, const char* location)
{
    GameSpy::GpSetStatus(connection, status, text, location);
}
void GpConnectNewUser(void* connection, const char* nick, const char* uniqueNick, const char* email,
                      const char* password, const char* cdKey, int firewall, int blocking, const void* callback,
                      void* param)
{
    GameSpy::GpConnectNewUser(connection, nick, uniqueNick, email, password, cdKey,
                                                                firewall, blocking, callback, param);
}

// GPBuddyStatus as gpGetBuddyStatus fills it (the end padded: the SDK's own size isn't known exactly).
struct GpBuddyStatus {
    int profile;
    int status;
    char statusString[256];
    char locationString[256];
    uint32_t ip;
    int port;
    uint8_t rest[0x20];
};
void GpGetBuddyStatus(void* connection, int index, GpBuddyStatus* status)
{
    GameSpy::GpGetBuddyStatus(connection, index, status);
}

// The callbacks' arguments (the parts read).
struct GpErrorArg {
    int result;
    int errorCode;
};
struct GpConnectResponseArg {
    int result;
    int profile;
};
struct GpRecvBuddyStatusArg {
    int profile;
    uint32_t date;
    int index;
};
struct GpRecvBuddyMessageArg {
    int profile;
    uint32_t date;
    char* message;
};

// The lobby code before this class (server_list.cpp, buddy_status.cpp).
// 0x00548040: whether a GP location string is a THUG2 game's: "10thug211:<ip>:<private ip>:<port>:<name>".
// (IsThug2Location itself, from server_list.h)
// 0x005482A0: logged in as `profile` (state 4, the status set online).
void LoggedIn(FriendsManager* man, int profile) { man->SetLoggedIn(profile); }
// 0x005484A0 / 0x005484C0: ListKit::Node<Buddy> / ListKit::Node<BuddyMessage>'s destructors (unlinked).
void DestroyBuddyNode(Buddy* buddy) { static_cast<BuddyNode*>(&buddy->m_listLink)->DestroyBody(); }
void DestroyMessageNode(BuddyMessage* message) { static_cast<BuddyMessageNode*>(&message->m_listLink)->DestroyBody(); }
// 0x00548100: GP's buddy request callback (the request accepted); 0x00548140: the login thread.
const void* const kOnBuddyRequest = CodeAddress(FriendsManager::OnBuddyRequest);
const auto kLoginThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(CodeAddress(FriendsManager::LoginThread));
// The lobby (m_lobbyMan): its peer (0x0053F880), and the peer SDK's public address of this machine (0x005FC940).
void* LobbyPeer(void* lobby) { return static_cast<RoomManager*>(lobby)->GetLobbyHandle(); }
uint32_t PeerPublicIp(void* peer) { return GameSpy::PeerGetPublicIp(peer); }

// A node deleted through its vtable (the scalar deleting destructor, slot 0).
void UnlinkListNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}

void SetupListSentinel(MemSys::ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
void InitNode(MemSys::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}
// `node` at the end of the list.
void Append(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// The items of a list (the original's count checks each node's data for the head before checking it for null).
int Count(const MemSys::ListNode* head)
{
    int count = 0;
    for (const MemSys::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

// A new buddy (the constructor inlined in the original): offline, not pending, no location. (A failed allocation
// isn't checked by the callers.)
Buddy* NewBuddy()
{
    auto* buddy = static_cast<Buddy*>(MemSys::AllocZeroed(sizeof(Buddy)));
    if (buddy) {
        InitNode(&buddy->m_listLink, kBuddyVtable);
        buddy->m_pending = false;
        buddy->m_status = 0;
        buddy->m_location[0] = '\0';
        std::snprintf(buddy->m_statusText, sizeof(buddy->m_statusText), "%s", "Offline");
    }
    return buddy;
}

// A THUG2 location string's parts into `buddy`: the game's name (after the fourth ':' - IsThug2Location has checked
// there are four) and the address. `checked`: the original checks each ':' is found when updating a buddy (and
// copies from address 0 if not - a crash), not when making one.
void ParseLocation(Buddy* buddy, char* location, bool checked)
{
    char* name = std::strchr(location, ':');
    if (checked) {
        if (name && (name = std::strchr(name + 1, ':')) && (name = std::strchr(name + 1, ':')) &&
            (name = std::strchr(name + 1, ':')))
            ++name;
    } else {
        name = std::strchr(name + 1, ':');
        name = std::strchr(name + 1, ':');
        name = std::strchr(name + 1, ':') + 1;
    }
    Text::Copy(buddy->m_location, name);
    Text::Token(location, ":"); // "10thug211"
    buddy->m_joinIp = static_cast<uint32_t>(std::atol(Text::Token(nullptr, ":")));
    buddy->m_joinPrivateIp = static_cast<uint32_t>(std::atol(Text::Token(nullptr, ":")));
    buddy->m_joinPort = std::atol(Text::Token(nullptr, ":"));
}

} // namespace

// 0x00548CB0
BuddyMessage* BuddyMessage::Construct()
{
    InitNode(&m_listLink, kMessageVtable);
    m_profile = 0;
    std::snprintf(m_nick, sizeof(m_nick), "%s", "Proskater");
    m_text[0] = '\0';
    return this;
}

// 0x00548CF0
Buddy* Buddy::Destroy(unsigned int flags)
{
    DestroyBuddyNode(this);
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x00548D20
BuddyMessage* BuddyMessage::Destroy(unsigned int flags)
{
    DestroyMessageNode(this);
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054A480
FriendsManager* FriendsManager::Construct()
{
    SetupListSentinel(&m_buddies, kBuddyVtable);
    SetupListSentinel(&m_messages, kMessageVtable);
    Memory()->EnterHeapScope(Memory()->m_networkHeap);
    GpInitialize(this, 0x2880, 0);
    GpSetCallback(this, 0, reinterpret_cast<const void*>(&OnError), this);
    GpSetCallback(this, 1, kOnBuddyRequest, this);
    GpSetCallback(this, 2, reinterpret_cast<const void*>(&OnBuddyStatus), this);
    GpSetCallback(this, 3, reinterpret_cast<const void*>(&OnBuddyMessage), this);
    GpSetCallback(this, 4, reinterpret_cast<const void*>(&Stubs::DoNothing), this);
    GpSetCallback(this, 5, reinterpret_cast<const void*>(&Stubs::DoNothing), this);
    auto* task = static_cast<TaskList::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (task) {
        task->Construct(0);
        task->m_vtable = GameCode::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&BuddyTask); // +0x18 the callback
        reinterpret_cast<void**>(task)[7] = this;                                // +0x1C the data
    }
    m_task = task;
    m_loggedIn = false;
    m_state = 0;
    m_profile = 0;
    PopHeap();
    return this;
}

// 0x005488B0
void FriendsManager::DestroyBody()
{
    if (m_task) // (its scalar deleting destructor, vtable slot 2)
        reinterpret_cast<void*(__thiscall*)(TaskList::SchedulerTask*, unsigned int)>(m_task->m_vtable[2])(m_task, 1);
    m_messages.UnlinkAs(kMessageNodeVtable);
    m_buddies.UnlinkAs(kBuddyNodeVtable);
}

// 0x0054A1A0
void FriendsManager::LogIn()
{
    if (m_loggedIn)
        LogOff();
    PushBottomUp();
    m_state = 2;
    LoginStart() = FrameClock::GetTime();
    Run("log_in_profile");
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, kLoginThread, this, CREATE_SUSPENDED, &id);
    NetManager()->SetRoomId(id);
    ResumeThread(thread); // (the handle is never closed)
    if (!m_task->IsOnTaskList())
        LogicTasks()->AddTask(m_task);
    PopHeap();
}

// 0x0054A5C0
void FriendsManager::CreateProfile()
{
    if (m_loggedIn)
        LogOff();
    m_state = 1;
    LoginStart() = FrameClock::GetTime();
    PushTopDown();
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, &CreateProfileThread, this, CREATE_SUSPENDED, &id);
    NetManager()->SetRoomId(id);
    ResumeThread(thread);
    LogicTasks()->AddTask(m_task); // (not checked for being in the list already, unlike LogIn)
    PopHeap();
}

// 0x00549B90
void FriendsManager::LogOff()
{
    GpDisconnect(this);
    m_task->Remove();
    m_loggedIn = false;
    ListKit::Search<Buddy> buddies;
    for (Buddy* buddy = buddies.HeadElement(&m_buddies); buddy;) {
        Buddy* next = buddies.NextItem();
        UnlinkListNode(buddy);
        buddy = next;
    }
    ListKit::Search<BuddyMessage> messages;
    for (BuddyMessage* message = messages.HeadElement(&m_messages); message;) {
        BuddyMessage* next = messages.NextItem();
        UnlinkListNode(message);
        message = next;
    }
}

// 0x00548D50
void FriendsManager::AddBuddy(const char* nick, int profile)
{
    PushBottomUp();
    Buddy* buddy = NewBuddy();
    Text::Copy(buddy->m_nick, nick);
    buddy->m_profile = profile;
    buddy->m_status = 0;
    buddy->m_pending = true;
    Append(&m_buddies, &buddy->m_listLink);
    GpSendBuddyRequest(this, profile, "");
    PopHeap();
}

// 0x00549A50
void FriendsManager::RemoveBuddy(int profile)
{
    ListKit::Search<Buddy> search;
    for (Buddy* buddy = search.HeadElement(&m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_profile == profile) {
            UnlinkListNode(buddy);
            break;
        }
    }
    GpDeleteBuddy(this, profile);
    Scripts::CStruct* params = NewParams();
    params->PutNameKey("id", Checksums("first_lobby_buddy") + profile);
    Run("removed_buddy", params);
    DeleteParams(params);
}

// 0x005499A0
void FriendsManager::CancelAddBuddy()
{
    ListKit::Search<Buddy> search;
    for (Buddy* buddy = search.HeadElement(&m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_pending) {
            UnlinkListNode(buddy);
            return;
        }
    }
}

// 0x00549CB0
bool FriendsManager::IsBuddy(int profile)
{
    ListKit::Search<Buddy> search;
    for (Buddy* buddy = search.HeadElement(&m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_profile == profile)
            return true;
    }
    return false;
}

// 0x00548E10
int FriendsManager::CountBuddies() { return Count(&m_buddies); }

// 0x005498E0
void FriendsManager::FillBuddyList(bool clear, bool allowJoin, bool allowRemove)
{
    if (clear)
        Run("destroy_lobby_buddy_list_children");
    ListKit::Search<Buddy> search;
    for (Buddy* buddy = search.HeadElement(&m_buddies); buddy; buddy = search.NextItem()) {
        if (!buddy->m_pending)
            ShowBuddy(buddy, allowJoin, allowRemove);
    }
}

// 0x00549F50
void FriendsManager::FillProspectiveBuddyList()
{
    Manager* manager = NetManager();
    Detail::PlayerSearch search;
    PushTopDown();
    for (uint8_t* player = Detail::FirstPlayer(manager, &search, true); player;
         player = Detail::NextPlayer(manager, &search, true)) {
        int profile = Detail::Field<int>(player, 0x28);
        if (Detail::IsOwnPlayer(player) || profile == 0 || IsBuddy(profile))
            continue;
        const char* name = Detail::PlayerName(player);
        Scripts::CStruct* params = NewParams();
        params->AddString("text", name);
        params->PutNameKey("id", static_cast<uint32_t>(profile));
        params->PutNameKey("pad_choose_script", Checksums("add_buddy"));
        params->PutNameKey("parent_menu_id", Checksums("lobby_buddy_list_menu"));
        Scripts::CStruct* choose = NewParams();
        choose->PutInt("profile", profile);
        choose->AddString("nick", name);
        params->PutSubStruct("pad_choose_params", choose);
        params->PushFloat("scale", 1.1f);
        Run("make_text_sub_menu_item", params);
        DeleteParams(params);
        DeleteParams(choose);
    }
    PopHeap();
}

// 0x00548560
void __stdcall FriendsManager::ShowBuddy(Buddy* buddy, bool allowJoin, bool allowRemove)
{
    PushTopDown();
    Scripts::CStruct* params = NewParams();
    // (the original uses the nick as sprintf's format: a '%' in a nick would be expanded)
    char nick[1024];
    std::snprintf(nick, sizeof(nick), "%s", buddy->m_nick);
    params->AddString("name", buddy->m_nick);
    params->AddString("status", buddy->m_statusText);
    params->AddString("location", buddy->m_location);
    params->PutNameKey("id", Checksums("first_lobby_buddy") + buddy->m_profile);
    const char* chooseScript;
    if (!allowJoin && !allowRemove) {
        chooseScript = "do_nothing";
    } else {
        Scripts::CStruct* choose = NewParams();
        choose->PutInt("profile", buddy->m_profile);
        TextUtil::ToCapitals(nick);
        choose->AddString("nick", nick);
        choose->AddString("status", buddy->m_statusText);
        choose->AddString("location", buddy->m_location);
        choose->PutInt("join_ip", static_cast<int>(buddy->m_joinIp));
        choose->PutInt("join_private_ip", static_cast<int>(buddy->m_joinPrivateIp));
        choose->PutInt("join_port", buddy->m_joinPort);
        if (allowRemove)
            choose->PutNameKey(0u, Checksums("allow_remove"));
        if (allowJoin) {
            if (buddy->m_status == kStatusPlaying)
                choose->PutNameKey(0u, Checksums("allow_join"));
            choose->PutNameKey(0u, Checksums("in_lobby"));
        } else {
            choose->PutNameKey("back_script", Checksums("back_from_shell_buddy_options"));
            choose->PutNameKey("remove_script", Checksums("shell_remove_buddy"));
        }
        params->PutSubStruct("pad_choose_params", choose); // (choose is never freed - as in the original)
        chooseScript = "present_buddy_options";
    }
    params->PutNameKey("pad_choose_script", Checksums(chooseScript));
    params->PutNameKey("parent_menu_id", Checksums("lobby_buddy_list_menu"));
    if (allowJoin)
        params->PushFloat("scale", 1.1f);
    Run("homie_list_add_item", params);
    DeleteParams(params);
    PopHeap();
}

// 0x00549ED0
void __cdecl FriendsManager::OnError(void*, const void* arg, FriendsManager* man)
{
    switch (static_cast<const GpErrorArg*>(arg)->errorCode) {
    case kErrorNewUserNick:
        man->m_state = 3;
        break;
    case kErrorLogin:
        man->LogOff();
        Run("CreateBuddyLoginFailedDialog");
        break;
    case kErrorPassword:
        Run("create_wrong_profile_password_dialog");
        man->LogOff();
        break;
    case kErrorAlreadyBuddy:
        Run("failed_add_buddy_already_buddy");
        break;
    default:
        break;
    }
}

// 0x00548F50
void __cdecl FriendsManager::OnBuddyStatus(void*, const void* argument, FriendsManager* man)
{
    auto* arg = static_cast<const GpRecvBuddyStatusArg*>(argument);
    PushBottomUp();
    GpBuddyStatus status;
    GpGetBuddyStatus(man, arg->index, &status);
    ListKit::Search<Buddy> search;
    for (Buddy* buddy = search.HeadElement(&man->m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_profile != status.profile)
            continue;
        buddy->m_status = status.status;
        Text::Copy(buddy->m_statusText, status.statusString);
        if (buddy->m_status == kStatusPlaying) {
            if (!IsThug2Location(status.locationString)) {
                PopHeap();
                return;
            }
            ParseLocation(buddy, status.locationString, true);
        } else {
            Text::Copy(buddy->m_location, status.locationString);
        }
        if (buddy->m_pending) {
            // An answer to our request: a buddy now, kept in the buddy_array preference too.
            Scripts::CStruct* params = NewParams();
            params->AddString("net_name", buddy->m_nick);
            Run("added_buddy", params);
            DeleteParams(params);
            buddy->m_pending = false;
            OptionStore::Preferences* prefs = NetManager()->GetOnlineSettings();
            Scripts::CArray* old = prefs->GetSettingList(Checksums("buddy_array"));
            Scripts::CArray* memory = Scripts::NewArray();
            Scripts::CArray* buddies = memory ? memory->Init() : nullptr;
            buddies->ConfigureSizeKind(old->m_size + 1, Scripts::QBVAL_STRUCT);
            uint32_t i = 0;
            for (; i < old->m_size; ++i) {
                Scripts::CStruct* entry = reinterpret_cast<Scripts::CStruct*>(old->GetElement(i));
                Scripts::CStruct* copy = NewParams();
                copy->Assign(entry);
                buddies->SetElement(i, reinterpret_cast<uint32_t>(copy));
            }
            Scripts::CStruct* entry = NewParams();
            entry->PutInt("profile", buddy->m_profile);
            entry->AddString("nick", buddy->m_nick);
            buddies->SetElement(i, reinterpret_cast<uint32_t>(entry));
            prefs->SetPreferenceArray(Checksums("buddy_array"), buddies);
        }
        Scripts::CStruct* params = NewParams();
        params->AddString(Checksums("name"), Scripts::QBVAL_STRING, buddy->m_nick);
        params->AddString(Checksums("status"), Scripts::QBVAL_STRING, buddy->m_statusText);
        params->AddString(Checksums("location"), Scripts::QBVAL_STRING, buddy->m_location);
        params->AddComponentOfType(Checksums("id"), Scripts::QBVAL_NAME, Checksums("first_lobby_buddy") + buddy->m_profile);
        Run("update_buddy_status", params);
        DeleteParams(params);
        PopHeap();
        return;
    }

    // Not on the list: someone who made us their buddy.
    OptionStore::Preferences* prefs = NetManager()->GetOnlineSettings();
    Buddy* buddy = NewBuddy();
    buddy->m_profile = status.profile;
    buddy->m_status = status.status;
    Text::Copy(buddy->m_statusText, status.statusString);
    std::snprintf(buddy->m_nick, sizeof(buddy->m_nick), "%s", "ProSkater");
    if (buddy->m_status == kStatusPlaying) {
        if (!IsThug2Location(status.locationString)) {
            UnlinkListNode(buddy);
            PopHeap();
            return;
        }
        ParseLocation(buddy, status.locationString, false);
    } else {
        Text::Copy(buddy->m_location, status.locationString);
    }
    Append(&man->m_buddies, &buddy->m_listLink);
    // Its nick, if the buddy_array preference knows the profile.
    Scripts::CArray* buddies = prefs->GetSettingList(Checksums("buddy_array"));
    if (buddies) {
        for (uint32_t i = 0; i < buddies->m_size; ++i) {
            auto* entry = reinterpret_cast<Scripts::CStruct*>(buddies->GetElement(i));
            int profile;
            entry->FetchIntValue("profile", &profile, 1);
            if (profile == status.profile) {
                const char* nick = nullptr;
                entry->GetString("nick", &nick, 1);
                Text::Copy(buddy->m_nick, nick);
            }
        }
    }
    PopHeap();
}

// 0x005496F0
void __cdecl FriendsManager::OnBuddyMessage(void*, const void* argument, FriendsManager* man)
{
    auto* arg = static_cast<const GpRecvBuddyMessageArg*>(argument);
    if (Count(&man->m_messages) > kMaxMessages)
        return;
    PushTopDown();
    size_t length = std::strlen(arg->message);
    void* memory = MemSys::AllocZeroed(sizeof(BuddyMessage));
    BuddyMessage* message = memory ? static_cast<BuddyMessage*>(memory)->Construct() : nullptr;
    message->m_profile = arg->profile;
    char* text = Text::Token(arg->message, "\n");
    if (length == std::strlen(text)) {
        // One line: from a buddy (else dropped). (A nick of 16 or more characters runs into the text, as in the
        // original.)
        Text::Copy(message->m_text, text);
        Buddy* sender = nullptr;
        ListKit::Search<Buddy> search;
        for (Buddy* buddy = search.HeadElement(&man->m_buddies); buddy; buddy = search.NextItem()) {
            if (buddy->m_profile == arg->profile) {
                sender = buddy;
                break;
            }
        }
        if (!sender) {
            UnlinkListNode(message);
            PopHeap();
            return;
        }
        Text::Copy(message->m_nick, sender->m_nick);
    } else {
        // "nick\ntext"
        Text::Copy(message->m_nick, text);
        Text::Copy(message->m_text, text + std::strlen(text) + 1);
    }
    Scripts::LaunchScript(0x10C3647Cu, nullptr, nullptr, false, nullptr);
    Append(&man->m_messages, &message->m_listLink);
    PopHeap();
}

// 0x0054A2C0
void __cdecl FriendsManager::OnConnectNewUser(void*, const void* argument, FriendsManager* man)
{
    auto* arg = static_cast<const GpConnectResponseArg*>(argument);
    if (arg->result == 0) {
        LoggedIn(man, arg->profile);
    } else if (arg->result == 4 && CreatingProfile()) { // the nick is taken: the profile exists - log in
        CreatingProfile() = false;
        man->LogIn();
    }
}

// 0x0054A300
void __cdecl FriendsManager::BuddyTask(TaskList::SchedulerTask* task)
{
    PushTopDown();
    auto* man = reinterpret_cast<FriendsManager*>(reinterpret_cast<void**>(task)[7]);
    switch (man->m_state) {
    case 1: // creating the profile
    case 2: // logging in
        if (FrameClock::GetTime() - LoginStart() > kLoginTimeout) {
            man->LogOff();
            Run("CreateBuddyLoginFailedDialog");
        } else {
            GpProcess(man);
        }
        break;
    case 3: // the nick was taken by a new user: log in
        man->LogIn();
        break;
    case 4: // logged in
        if (FrameClock::GetTime() - LastProcess() > kProcessInterval) {
            GpProcess(man);
            LastProcess() = FrameClock::GetTime();
        }
        break;
    default:
        break;
    }
    PopHeap();
}

// 0x0054A3D0
unsigned long __stdcall FriendsManager::CreateProfileThread(void* parameter)
{
    auto* man = static_cast<FriendsManager*>(parameter);
    PushBottomUp();
    OptionStore::Preferences* prefs = NetManager()->GetOnlineSettings();
    const char* email = prefs->GetPreferenceString(Checksums("profile_email"), Checksums("ui_string"));
    const char* password = prefs->GetPreferenceString(Checksums("profile_password"), Checksums("ui_string"));
    CreatingProfile() = true;
    GpConnectNewUser(man, "proskater", nullptr, email, password, nullptr, 1, 0,
                     reinterpret_cast<const void*>(&OnConnectNewUser), man);
    PopHeap();
    return 0;
}

// --- The script commands ---

// 0x00548940: the unique_id preference: the time now (ms) as text.
bool __cdecl SetUniqueId(Scripts::CStruct*, Scripts::CScript*)
{
    uint32_t now = FrameClock::GetTime();
    OptionStore::Preferences* prefs = NetManager()->GetOnlineSettings();
    Scripts::CStruct* params = NewParams();
    char text[32];
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(now));
    params->AddString("ui_string", text);
    prefs->SetPreference(Checksums("unique_id"), params);
    DeleteParams(params);
    return true;
}

// 0x00548A40: join (or with `observe` watch) the game of the buddy chosen (its location, join_ip,
// join_private_ip, join_port): the status set playing / observing there, then net_chosen_join_server with the port
// and address - a buddy behind this machine's public address is joined at its private address on THUG2's port.
bool __cdecl JoinBuddy(Scripts::CStruct* params, Scripts::CScript*)
{
    Manager* manager = NetManager();
    bool observe = params->HasBareFlag("observe");
    const char* location;
    int ip, privateIp, port;
    params->GetString("location", &location, 1);
    params->FetchIntValue("join_ip", &ip, 1);
    params->FetchIntValue("join_private_ip", &privateIp, 1);
    params->FetchIntValue("join_port", &port, 1);
    char status[1024];
    std::snprintf(status, sizeof(status), "10thug211:%d:%d:%d:%s", ip, privateIp, port, location);
    bool sameAddress = false;
    if (static_cast<uint32_t>(ip) == PeerPublicIp(LobbyPeer(manager->m_lobbyMan))) {
        ip = privateIp;
        port = kJoinPort;
        sameAddress = true;
    }
    const char* text;
    if (observe) {
        manager->SetJoinMode(1);
        text = Scripts::GetString("homie_status_observing");
    } else {
        manager->SetJoinMode(0);
        text = Scripts::GetString("homie_status_playing");
    }
    FriendsManager* man = static_cast<FriendsManager*>(manager->m_buddyMan);
    PushTopDown();
    GpSetStatus(man, kStatusPlaying, text, status);
    PopHeap();
    Scripts::CStruct* join = NewParams();
    in_addr address;
    address.S_un.S_addr = static_cast<uint32_t>(ip);
    const char* dotted = inet_ntoa(address);
    join->AddComponentOfType(0, Scripts::QBVAL_INTEGER, static_cast<uint32_t>(port));
    join->AddString(0, Scripts::QBVAL_STRING, dotted);
    if (!sameAddress)
        join->AddComponentOfType(0x751F4599, Scripts::QBVAL_NAME, static_cast<uint32_t>(Maths::Rnd(0xFFFF)));
    Run("net_chosen_join_server", join);
    DeleteParams(join);
    return true;
}

namespace {
FriendsManager* Buddies() { return static_cast<FriendsManager*>(NetManager()->m_buddyMan); }
} // namespace

// 0x00548E40: AddBuddy profile nick
bool __cdecl AddBuddy(Scripts::CStruct* params, Scripts::CScript*)
{
    int profile;
    const char* nick;
    params->FetchIntValue("profile", &profile, 1);
    params->GetString("nick", &nick, 1);
    Buddies()->AddBuddy(nick, profile);
    return true;
}

// 0x00548E90
bool __cdecl HasBuddies(Scripts::CStruct*, Scripts::CScript*) { return Count(&Buddies()->m_buddies) > 0; }

// 0x00548ED0
bool __cdecl BuddyListFull(Scripts::CStruct*, Scripts::CScript*) { return Count(&Buddies()->m_buddies) >= kMaxBuddies; }

// 0x00548F10
bool __cdecl AnyBuddyMessagesLeft(Scripts::CStruct*, Scripts::CScript*) { return Count(&Buddies()->m_messages) != 0; }

// 0x00549D10
bool __cdecl ProfileLogOff(Scripts::CStruct*, Scripts::CScript*)
{
    Buddies()->LogOff();
    return true;
}

// 0x00549D20: FillBuddyList [clear_list] [allow_join] [allow_remove]
bool __cdecl FillBuddyList(Scripts::CStruct* params, Scripts::CScript*)
{
    bool clear = params->HasBareFlag("clear_list");
    bool allowJoin = params->HasBareFlag("allow_join");
    bool allowRemove = params->HasBareFlag("allow_remove");
    Buddies()->FillBuddyList(clear, allowJoin, allowRemove);
    return true;
}

// 0x00549D80
bool __cdecl CancelAddBuddy(Scripts::CStruct*, Scripts::CScript*)
{
    Buddies()->CancelAddBuddy();
    return true;
}

// 0x00549D90: RemoveBuddy profile
bool __cdecl RemoveBuddy(Scripts::CStruct* params, Scripts::CScript*)
{
    int profile;
    params->FetchIntValue("profile", &profile, 1);
    Buddies()->RemoveBuddy(profile);
    return true;
}

// 0x00549DC0: the oldest buddy message into the script's parameters (its nick twice, the text, the sender's
// profile) and deleted. (There must be one: AnyBuddyMessagesLeft.)
bool __cdecl ShowNextBuddyMessage(Scripts::CStruct*, Scripts::CScript* script)
{
    ListKit::Search<BuddyMessage> search;
    BuddyMessage* message = search.HeadElement(&Buddies()->m_messages);
    script->m_params->AddString(0xD6F4D0C8, message->m_nick);
    script->m_params->AddString(0xD3A5478A, message->m_nick);
    script->m_params->AddString(0x59EDEC1C, message->m_text);
    script->m_params->PutInt(0x7EA855F0, message->m_profile);
    UnlinkListNode(message);
    // (the original then counts the messages left and ignores the count)
    return true;
}

// 0x00549EA0: IsAlreadyMyBuddy <profile 0x7EA855F0>
bool __cdecl IsAlreadyMyBuddy(Scripts::CStruct* params, Scripts::CScript*)
{
    int profile = 0; // (left unset in the original when the parameter is missing)
    params->FetchIntValue(0x7EA855F0, &profile, 0);
    return Buddies()->IsBuddy(profile);
}

// 0x0054A250: log in if the profile_success preference is boolean_true (else false).
bool __cdecl ProfileLogIn(Scripts::CStruct*, Scripts::CScript*)
{
    OptionStore::Preferences* prefs = NetManager()->GetOnlineSettings();
    if (prefs->GetPreferenceChecksum(Checksums("profile_success"), Checksums("checksum")) != Checksums("boolean_true"))
        return false;
    Buddies()->LogIn();
    return true;
}

// 0x0054A2B0
bool __cdecl FillProspectiveBuddyList(Scripts::CStruct*, Scripts::CScript*)
{
    Buddies()->FillProspectiveBuddyList();
    return true;
}

// 0x0054A650
bool __cdecl CreateProfile(Scripts::CStruct*, Scripts::CScript*)
{
    Buddies()->CreateProfile();
    return true;
}

} // namespace OnlinePlay

using OnlinePlay::FriendsManager;
