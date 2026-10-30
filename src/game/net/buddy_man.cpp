// The online profile and buddy list (GameNet::BuddyMan). See buddy_man.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_ntoa: what the game uses

#include "game/net/buddy_man.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
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
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace GameNet {

namespace {

using Detail::DeleteParams;
using Detail::NewParams;

void** const kBuddyVtable = reinterpret_cast<void**>(0x0064D140);        // Lst::Node<Buddy> (Buddy)
void** const kMessageVtable = reinterpret_cast<void**>(0x0064D14C);      // Lst::Node<BuddyMessage>
void** const kBuddyHeadVtable = reinterpret_cast<void**>(0x0064CF60);    // as the heads are destroyed
void** const kMessageHeadVtable = reinterpret_cast<void**>(0x0064CF64);
void** const kManagerTaskVtable = reinterpret_cast<void**>(0x006476F0);  // Tsk::Task<BuddyMan>

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

GAME_GLOBAL(bool, g_creatingProfile, 0x007CE454);
GAME_GLOBAL(uint32_t, g_buddyLoginStart, 0x007CE458);
GAME_GLOBAL(uint32_t, g_lastProcess, 0x007CE45C);

// The login's start (the task's 10 s limit), when GP was last processed once logged in, and "a new profile is being
// made" (its gpConnectNewUser answer "nick taken" logs in instead).
uint32_t& LoginStart() { return g_buddyLoginStart; }
uint32_t& LastProcess() { return g_lastProcess; }
bool& CreatingProfile() { return g_creatingProfile; }

Manager* NetManager() { return g_gameNetManager; }
Mem::Manager* Memory() { return Mem::Manager::Get(); }
void PushTopDown() { Memory()->PushContext(Memory()->m_internetTopDownHeap); }
void PushBottomUp() { Memory()->PushContext(Memory()->m_internetBottomUpHeap); }
void PopHeap() { Memory()->PopContext(); }
Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(GameVariable<uint8_t*>(0x00696D68) + 0x04); }

uint32_t Crc(const char* name) { return Crc::GenerateCRCFromString(name); }
void Run(const char* script, Script::CStruct* params = nullptr) { Script::RunScript(script, params, nullptr, false); }

// The GameSpy presence SDK (library code, not decompiled; cdecl).
void GpInitialize(void* connection, int productId, int namespaceId)
{
    GameFunction<int(__cdecl*)(void*, int, int)>(0x005FFD40)(connection, productId, namespaceId);
}
void GpSetCallback(void* connection, int type, const void* callback, void* param)
{
    GameFunction<int(__cdecl*)(void*, int, const void*, void*)>(0x005FFD90)(connection, type, callback, param);
}
void GpProcess(void* connection) { GameFunction<int(__cdecl*)(void*)>(0x005FFD60)(connection); }
void GpDisconnect(void* connection) { GameFunction<void(__cdecl*)(void*)>(0x00600020)(connection); }
void GpSendBuddyRequest(void* connection, int profile, const char* reason)
{
    GameFunction<int(__cdecl*)(void*, int, const char*)>(0x00600050)(connection, profile, reason);
}
void GpDeleteBuddy(void* connection, int profile)
{
    GameFunction<int(__cdecl*)(void*, int)>(0x006002E0)(connection, profile);
}
void GpSetStatus(void* connection, int status, const char* text, const char* location)
{
    GameFunction<int(__cdecl*)(void*, int, const char*, const char*)>(0x00600330)(connection, status, text, location);
}
void GpConnectNewUser(void* connection, const char* nick, const char* uniqueNick, const char* email,
                      const char* password, const char* cdKey, int firewall, int blocking, const void* callback,
                      void* param)
{
    GameFunction<int(__cdecl*)(void*, const char*, const char*, const char*, const char*, const char*, int, int,
                               const void*, void*)>(0x005FFEF0)(connection, nick, uniqueNick, email, password, cdKey,
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
    GameFunction<int(__cdecl*)(void*, int, GpBuddyStatus*)>(0x006001F0)(connection, index, status);
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
void LoggedIn(BuddyMan* man, int profile) { man->SetLoggedIn(profile); }
// 0x005484A0 / 0x005484C0: Lst::Node<Buddy> / Lst::Node<BuddyMessage>'s destructors (unlinked).
void DestroyBuddyNode(Buddy* buddy) { static_cast<BuddyNode*>(&buddy->m_node)->DestroyBody(); }
void DestroyMessageNode(BuddyMessage* message) { static_cast<BuddyMessageNode*>(&message->m_node)->DestroyBody(); }
// 0x00548100: GP's buddy request callback (the request accepted); 0x00548140: the login thread.
const void* const kOnBuddyRequest = reinterpret_cast<const void*>(0x00548100);
const auto kLoginThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(0x00548140);
// The lobby (m_lobbyMan): its peer (0x0053F880), and the peer SDK's public address of this machine (0x005FC940).
void* LobbyPeer(void* lobby) { return static_cast<LobbyMan*>(lobby)->GetPeer(); }
uint32_t PeerPublicIp(void* peer) { return GameFunction<uint32_t(__cdecl*)(void*)>(0x005FC940)(peer); }

// A node deleted through its vtable (the scalar deleting destructor, slot 0).
void DeleteNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}

void InitHead(Mem::ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
void InitNode(Mem::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}
// `node` at the end of the list.
void Append(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// The items of a list (the original's count checks each node's data for the head before checking it for null).
int Count(const Mem::ListNode* head)
{
    int count = 0;
    for (const Mem::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

// A new buddy (the constructor inlined in the original): offline, not pending, no location. (A failed allocation
// isn't checked by the callers.)
Buddy* NewBuddy()
{
    auto* buddy = static_cast<Buddy*>(Mem::Calloc(sizeof(Buddy)));
    if (buddy) {
        InitNode(&buddy->m_node, kBuddyVtable);
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
    InitNode(&m_node, kMessageVtable);
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
BuddyMan* BuddyMan::Construct()
{
    InitHead(&m_buddies, kBuddyVtable);
    InitHead(&m_messages, kMessageVtable);
    Memory()->PushContext(Memory()->m_networkHeap);
    GpInitialize(this, 0x2880, 0);
    GpSetCallback(this, 0, reinterpret_cast<const void*>(&OnError), this);
    GpSetCallback(this, 1, kOnBuddyRequest, this);
    GpSetCallback(this, 2, reinterpret_cast<const void*>(&OnBuddyStatus), this);
    GpSetCallback(this, 3, reinterpret_cast<const void*>(&OnBuddyMessage), this);
    GpSetCallback(this, 4, reinterpret_cast<const void*>(&Stubs::DoNothing), this);
    GpSetCallback(this, 5, reinterpret_cast<const void*>(&Stubs::DoNothing), this);
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (task) {
        task->Construct(0);
        task->m_vtable = kManagerTaskVtable;
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
void BuddyMan::DestroyBody()
{
    if (m_task) // (its scalar deleting destructor, vtable slot 2)
        reinterpret_cast<void*(__thiscall*)(Tsk::BaseTask*, unsigned int)>(m_task->m_vtable[2])(m_task, 1);
    m_messages.UnlinkAs(kMessageHeadVtable);
    m_buddies.UnlinkAs(kBuddyHeadVtable);
}

// 0x0054A1A0
void BuddyMan::LogIn()
{
    if (m_loggedIn)
        LogOff();
    PushBottomUp();
    m_state = 2;
    LoginStart() = Tmr::GetTime();
    Run("log_in_profile");
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, kLoginThread, this, CREATE_SUSPENDED, &id);
    NetManager()->SetLobbyId(id);
    ResumeThread(thread); // (the handle is never closed)
    if (!m_task->InList())
        LogicTasks()->AddTask(m_task);
    PopHeap();
}

// 0x0054A5C0
void BuddyMan::CreateProfile()
{
    if (m_loggedIn)
        LogOff();
    m_state = 1;
    LoginStart() = Tmr::GetTime();
    PushTopDown();
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, &CreateProfileThread, this, CREATE_SUSPENDED, &id);
    NetManager()->SetLobbyId(id);
    ResumeThread(thread);
    LogicTasks()->AddTask(m_task); // (not checked for being in the list already, unlike LogIn)
    PopHeap();
}

// 0x00549B90
void BuddyMan::LogOff()
{
    GpDisconnect(this);
    m_task->Remove();
    m_loggedIn = false;
    Lst::Search<Buddy> buddies;
    for (Buddy* buddy = buddies.FirstItem(&m_buddies); buddy;) {
        Buddy* next = buddies.NextItem();
        DeleteNode(buddy);
        buddy = next;
    }
    Lst::Search<BuddyMessage> messages;
    for (BuddyMessage* message = messages.FirstItem(&m_messages); message;) {
        BuddyMessage* next = messages.NextItem();
        DeleteNode(message);
        message = next;
    }
}

// 0x00548D50
void BuddyMan::AddBuddy(const char* nick, int profile)
{
    PushBottomUp();
    Buddy* buddy = NewBuddy();
    Text::Copy(buddy->m_nick, nick);
    buddy->m_profile = profile;
    buddy->m_status = 0;
    buddy->m_pending = true;
    Append(&m_buddies, &buddy->m_node);
    GpSendBuddyRequest(this, profile, "");
    PopHeap();
}

// 0x00549A50
void BuddyMan::RemoveBuddy(int profile)
{
    Lst::Search<Buddy> search;
    for (Buddy* buddy = search.FirstItem(&m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_profile == profile) {
            DeleteNode(buddy);
            break;
        }
    }
    GpDeleteBuddy(this, profile);
    Script::CStruct* params = NewParams();
    params->AddChecksum("id", Crc("first_lobby_buddy") + profile);
    Run("removed_buddy", params);
    DeleteParams(params);
}

// 0x005499A0
void BuddyMan::CancelAddBuddy()
{
    Lst::Search<Buddy> search;
    for (Buddy* buddy = search.FirstItem(&m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_pending) {
            DeleteNode(buddy);
            return;
        }
    }
}

// 0x00549CB0
bool BuddyMan::IsBuddy(int profile)
{
    Lst::Search<Buddy> search;
    for (Buddy* buddy = search.FirstItem(&m_buddies); buddy; buddy = search.NextItem()) {
        if (buddy->m_profile == profile)
            return true;
    }
    return false;
}

// 0x00548E10
int BuddyMan::CountBuddies() { return Count(&m_buddies); }

// 0x005498E0
void BuddyMan::FillBuddyList(bool clear, bool allowJoin, bool allowRemove)
{
    if (clear)
        Run("destroy_lobby_buddy_list_children");
    Lst::Search<Buddy> search;
    for (Buddy* buddy = search.FirstItem(&m_buddies); buddy; buddy = search.NextItem()) {
        if (!buddy->m_pending)
            ShowBuddy(buddy, allowJoin, allowRemove);
    }
}

// 0x00549F50
void BuddyMan::FillProspectiveBuddyList()
{
    Manager* manager = NetManager();
    Detail::PlayerSearch search;
    PushTopDown();
    for (uint8_t* player = Detail::FirstPlayer(manager, &search, true); player;
         player = Detail::NextPlayer(manager, &search, true)) {
        int profile = Detail::Field<int>(player, 0x28);
        if (Detail::IsLocalPlayer(player) || profile == 0 || IsBuddy(profile))
            continue;
        const char* name = Detail::PlayerName(player);
        Script::CStruct* params = NewParams();
        params->AddString("text", name);
        params->AddChecksum("id", static_cast<uint32_t>(profile));
        params->AddChecksum("pad_choose_script", Crc("add_buddy"));
        params->AddChecksum("parent_menu_id", Crc("lobby_buddy_list_menu"));
        Script::CStruct* choose = NewParams();
        choose->AddInteger("profile", profile);
        choose->AddString("nick", name);
        params->AddStructure("pad_choose_params", choose);
        params->AddFloat("scale", 1.1f);
        Run("make_text_sub_menu_item", params);
        DeleteParams(params);
        DeleteParams(choose);
    }
    PopHeap();
}

// 0x00548560
void __stdcall BuddyMan::ShowBuddy(Buddy* buddy, bool allowJoin, bool allowRemove)
{
    PushTopDown();
    Script::CStruct* params = NewParams();
    // (the original uses the nick as sprintf's format: a '%' in a nick would be expanded)
    char nick[1024];
    std::snprintf(nick, sizeof(nick), "%s", buddy->m_nick);
    params->AddString("name", buddy->m_nick);
    params->AddString("status", buddy->m_statusText);
    params->AddString("location", buddy->m_location);
    params->AddChecksum("id", Crc("first_lobby_buddy") + buddy->m_profile);
    const char* chooseScript;
    if (!allowJoin && !allowRemove) {
        chooseScript = "do_nothing";
    } else {
        Script::CStruct* choose = NewParams();
        choose->AddInteger("profile", buddy->m_profile);
        Str::UpperCase(nick);
        choose->AddString("nick", nick);
        choose->AddString("status", buddy->m_statusText);
        choose->AddString("location", buddy->m_location);
        choose->AddInteger("join_ip", static_cast<int>(buddy->m_joinIp));
        choose->AddInteger("join_private_ip", static_cast<int>(buddy->m_joinPrivateIp));
        choose->AddInteger("join_port", buddy->m_joinPort);
        if (allowRemove)
            choose->AddChecksum(0u, Crc("allow_remove"));
        if (allowJoin) {
            if (buddy->m_status == kStatusPlaying)
                choose->AddChecksum(0u, Crc("allow_join"));
            choose->AddChecksum(0u, Crc("in_lobby"));
        } else {
            choose->AddChecksum("back_script", Crc("back_from_shell_buddy_options"));
            choose->AddChecksum("remove_script", Crc("shell_remove_buddy"));
        }
        params->AddStructure("pad_choose_params", choose); // (choose is never freed - as in the original)
        chooseScript = "present_buddy_options";
    }
    params->AddChecksum("pad_choose_script", Crc(chooseScript));
    params->AddChecksum("parent_menu_id", Crc("lobby_buddy_list_menu"));
    if (allowJoin)
        params->AddFloat("scale", 1.1f);
    Run("homie_list_add_item", params);
    DeleteParams(params);
    PopHeap();
}

// 0x00549ED0
void __cdecl BuddyMan::OnError(void*, const void* arg, BuddyMan* man)
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
void __cdecl BuddyMan::OnBuddyStatus(void*, const void* argument, BuddyMan* man)
{
    auto* arg = static_cast<const GpRecvBuddyStatusArg*>(argument);
    PushBottomUp();
    GpBuddyStatus status;
    GpGetBuddyStatus(man, arg->index, &status);
    Lst::Search<Buddy> search;
    for (Buddy* buddy = search.FirstItem(&man->m_buddies); buddy; buddy = search.NextItem()) {
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
            Script::CStruct* params = NewParams();
            params->AddString("net_name", buddy->m_nick);
            Run("added_buddy", params);
            DeleteParams(params);
            buddy->m_pending = false;
            Prefs::Preferences* prefs = NetManager()->GetNetworkPreferences();
            Script::CArray* old = prefs->GetPreferenceArray(Crc("buddy_array"));
            Script::CArray* memory = Script::NewArray();
            Script::CArray* buddies = memory ? memory->Init() : nullptr;
            buddies->SetSizeAndType(old->m_size + 1, Script::ESYMBOLTYPE_STRUCTURE);
            uint32_t i = 0;
            for (; i < old->m_size; ++i) {
                Script::CStruct* entry = reinterpret_cast<Script::CStruct*>(old->GetElement(i));
                Script::CStruct* copy = NewParams();
                copy->Assign(entry);
                buddies->SetElement(i, reinterpret_cast<uint32_t>(copy));
            }
            Script::CStruct* entry = NewParams();
            entry->AddInteger("profile", buddy->m_profile);
            entry->AddString("nick", buddy->m_nick);
            buddies->SetElement(i, reinterpret_cast<uint32_t>(entry));
            prefs->SetPreferenceArray(Crc("buddy_array"), buddies);
        }
        Script::CStruct* params = NewParams();
        params->AddString(Crc("name"), Script::ESYMBOLTYPE_STRING, buddy->m_nick);
        params->AddString(Crc("status"), Script::ESYMBOLTYPE_STRING, buddy->m_statusText);
        params->AddString(Crc("location"), Script::ESYMBOLTYPE_STRING, buddy->m_location);
        params->AddComponentOfType(Crc("id"), Script::ESYMBOLTYPE_NAME, Crc("first_lobby_buddy") + buddy->m_profile);
        Run("update_buddy_status", params);
        DeleteParams(params);
        PopHeap();
        return;
    }

    // Not on the list: someone who made us their buddy.
    Prefs::Preferences* prefs = NetManager()->GetNetworkPreferences();
    Buddy* buddy = NewBuddy();
    buddy->m_profile = status.profile;
    buddy->m_status = status.status;
    Text::Copy(buddy->m_statusText, status.statusString);
    std::snprintf(buddy->m_nick, sizeof(buddy->m_nick), "%s", "ProSkater");
    if (buddy->m_status == kStatusPlaying) {
        if (!IsThug2Location(status.locationString)) {
            DeleteNode(buddy);
            PopHeap();
            return;
        }
        ParseLocation(buddy, status.locationString, false);
    } else {
        Text::Copy(buddy->m_location, status.locationString);
    }
    Append(&man->m_buddies, &buddy->m_node);
    // Its nick, if the buddy_array preference knows the profile.
    Script::CArray* buddies = prefs->GetPreferenceArray(Crc("buddy_array"));
    if (buddies) {
        for (uint32_t i = 0; i < buddies->m_size; ++i) {
            auto* entry = reinterpret_cast<Script::CStruct*>(buddies->GetElement(i));
            int profile;
            entry->GetInteger("profile", &profile, 1);
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
void __cdecl BuddyMan::OnBuddyMessage(void*, const void* argument, BuddyMan* man)
{
    auto* arg = static_cast<const GpRecvBuddyMessageArg*>(argument);
    if (Count(&man->m_messages) > kMaxMessages)
        return;
    PushTopDown();
    size_t length = std::strlen(arg->message);
    void* memory = Mem::Calloc(sizeof(BuddyMessage));
    BuddyMessage* message = memory ? static_cast<BuddyMessage*>(memory)->Construct() : nullptr;
    message->m_profile = arg->profile;
    char* text = Text::Token(arg->message, "\n");
    if (length == std::strlen(text)) {
        // One line: from a buddy (else dropped). (A nick of 16 or more characters runs into the text, as in the
        // original.)
        Text::Copy(message->m_text, text);
        Buddy* sender = nullptr;
        Lst::Search<Buddy> search;
        for (Buddy* buddy = search.FirstItem(&man->m_buddies); buddy; buddy = search.NextItem()) {
            if (buddy->m_profile == arg->profile) {
                sender = buddy;
                break;
            }
        }
        if (!sender) {
            DeleteNode(message);
            PopHeap();
            return;
        }
        Text::Copy(message->m_nick, sender->m_nick);
    } else {
        // "nick\ntext"
        Text::Copy(message->m_nick, text);
        Text::Copy(message->m_text, text + std::strlen(text) + 1);
    }
    Script::RunScript(0x10C3647Cu, nullptr, nullptr, false, nullptr);
    Append(&man->m_messages, &message->m_node);
    PopHeap();
}

// 0x0054A2C0
void __cdecl BuddyMan::OnConnectNewUser(void*, const void* argument, BuddyMan* man)
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
void __cdecl BuddyMan::BuddyTask(Tsk::BaseTask* task)
{
    PushTopDown();
    auto* man = reinterpret_cast<BuddyMan*>(reinterpret_cast<void**>(task)[7]);
    switch (man->m_state) {
    case 1: // creating the profile
    case 2: // logging in
        if (Tmr::GetTime() - LoginStart() > kLoginTimeout) {
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
        if (Tmr::GetTime() - LastProcess() > kProcessInterval) {
            GpProcess(man);
            LastProcess() = Tmr::GetTime();
        }
        break;
    default:
        break;
    }
    PopHeap();
}

// 0x0054A3D0
unsigned long __stdcall BuddyMan::CreateProfileThread(void* parameter)
{
    auto* man = static_cast<BuddyMan*>(parameter);
    PushBottomUp();
    Prefs::Preferences* prefs = NetManager()->GetNetworkPreferences();
    const char* email = prefs->GetPreferenceString(Crc("profile_email"), Crc("ui_string"));
    const char* password = prefs->GetPreferenceString(Crc("profile_password"), Crc("ui_string"));
    CreatingProfile() = true;
    GpConnectNewUser(man, "proskater", nullptr, email, password, nullptr, 1, 0,
                     reinterpret_cast<const void*>(&OnConnectNewUser), man);
    PopHeap();
    return 0;
}

// --- The script commands ---

// 0x00548940: the unique_id preference: the time now (ms) as text.
bool __cdecl SetUniqueId(Script::CStruct*, Script::CScript*)
{
    uint32_t now = Tmr::GetTime();
    Prefs::Preferences* prefs = NetManager()->GetNetworkPreferences();
    Script::CStruct* params = NewParams();
    char text[32];
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(now));
    params->AddString("ui_string", text);
    prefs->SetPreference(Crc("unique_id"), params);
    DeleteParams(params);
    return true;
}

// 0x00548A40: join (or with `observe` watch) the game of the buddy chosen (its location, join_ip,
// join_private_ip, join_port): the status set playing / observing there, then net_chosen_join_server with the port
// and address - a buddy behind this machine's public address is joined at its private address on THUG2's port.
bool __cdecl JoinBuddy(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    bool observe = params->ContainsFlag("observe");
    const char* location;
    int ip, privateIp, port;
    params->GetString("location", &location, 1);
    params->GetInteger("join_ip", &ip, 1);
    params->GetInteger("join_private_ip", &privateIp, 1);
    params->GetInteger("join_port", &port, 1);
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
        text = Script::GetString("homie_status_observing");
    } else {
        manager->SetJoinMode(0);
        text = Script::GetString("homie_status_playing");
    }
    BuddyMan* man = static_cast<BuddyMan*>(manager->m_buddyMan);
    PushTopDown();
    GpSetStatus(man, kStatusPlaying, text, status);
    PopHeap();
    Script::CStruct* join = NewParams();
    in_addr address;
    address.S_un.S_addr = static_cast<uint32_t>(ip);
    const char* dotted = inet_ntoa(address);
    join->AddComponentOfType(0, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(port));
    join->AddString(0, Script::ESYMBOLTYPE_STRING, dotted);
    if (!sameAddress)
        join->AddComponentOfType(0x751F4599, Script::ESYMBOLTYPE_NAME, static_cast<uint32_t>(Mth::Rnd(0xFFFF)));
    Run("net_chosen_join_server", join);
    DeleteParams(join);
    return true;
}

namespace {
BuddyMan* Buddies() { return static_cast<BuddyMan*>(NetManager()->m_buddyMan); }
} // namespace

// 0x00548E40: AddBuddy profile nick
bool __cdecl AddBuddy(Script::CStruct* params, Script::CScript*)
{
    int profile;
    const char* nick;
    params->GetInteger("profile", &profile, 1);
    params->GetString("nick", &nick, 1);
    Buddies()->AddBuddy(nick, profile);
    return true;
}

// 0x00548E90
bool __cdecl HasBuddies(Script::CStruct*, Script::CScript*) { return Count(&Buddies()->m_buddies) > 0; }

// 0x00548ED0
bool __cdecl BuddyListFull(Script::CStruct*, Script::CScript*) { return Count(&Buddies()->m_buddies) >= kMaxBuddies; }

// 0x00548F10
bool __cdecl AnyBuddyMessagesLeft(Script::CStruct*, Script::CScript*) { return Count(&Buddies()->m_messages) != 0; }

// 0x00549D10
bool __cdecl ProfileLogOff(Script::CStruct*, Script::CScript*)
{
    Buddies()->LogOff();
    return true;
}

// 0x00549D20: FillBuddyList [clear_list] [allow_join] [allow_remove]
bool __cdecl FillBuddyList(Script::CStruct* params, Script::CScript*)
{
    bool clear = params->ContainsFlag("clear_list");
    bool allowJoin = params->ContainsFlag("allow_join");
    bool allowRemove = params->ContainsFlag("allow_remove");
    Buddies()->FillBuddyList(clear, allowJoin, allowRemove);
    return true;
}

// 0x00549D80
bool __cdecl CancelAddBuddy(Script::CStruct*, Script::CScript*)
{
    Buddies()->CancelAddBuddy();
    return true;
}

// 0x00549D90: RemoveBuddy profile
bool __cdecl RemoveBuddy(Script::CStruct* params, Script::CScript*)
{
    int profile;
    params->GetInteger("profile", &profile, 1);
    Buddies()->RemoveBuddy(profile);
    return true;
}

// 0x00549DC0: the oldest buddy message into the script's parameters (its nick twice, the text, the sender's
// profile) and deleted. (There must be one: AnyBuddyMessagesLeft.)
bool __cdecl ShowNextBuddyMessage(Script::CStruct*, Script::CScript* script)
{
    Lst::Search<BuddyMessage> search;
    BuddyMessage* message = search.FirstItem(&Buddies()->m_messages);
    script->m_params->AddString(0xD6F4D0C8, message->m_nick);
    script->m_params->AddString(0xD3A5478A, message->m_nick);
    script->m_params->AddString(0x59EDEC1C, message->m_text);
    script->m_params->AddInteger(0x7EA855F0, message->m_profile);
    DeleteNode(message);
    // (the original then counts the messages left and ignores the count)
    return true;
}

// 0x00549EA0: IsAlreadyMyBuddy <profile 0x7EA855F0>
bool __cdecl IsAlreadyMyBuddy(Script::CStruct* params, Script::CScript*)
{
    int profile = 0; // (left unset in the original when the parameter is missing)
    params->GetInteger(0x7EA855F0, &profile, 0);
    return Buddies()->IsBuddy(profile);
}

// 0x0054A250: log in if the profile_success preference is boolean_true (else false).
bool __cdecl ProfileLogIn(Script::CStruct*, Script::CScript*)
{
    Prefs::Preferences* prefs = NetManager()->GetNetworkPreferences();
    if (prefs->GetPreferenceChecksum(Crc("profile_success"), Crc("checksum")) != Crc("boolean_true"))
        return false;
    Buddies()->LogIn();
    return true;
}

// 0x0054A2B0
bool __cdecl FillProspectiveBuddyList(Script::CStruct*, Script::CScript*)
{
    Buddies()->FillProspectiveBuddyList();
    return true;
}

// 0x0054A650
bool __cdecl CreateProfile(Script::CStruct*, Script::CScript*)
{
    Buddies()->CreateProfile();
    return true;
}

} // namespace GameNet

using GameNet::BuddyMan;
REPLACE(0x00548560, BuddyMan::ShowBuddy);
REPLACE(0x005488B0, &BuddyMan::DestroyBody);
REPLACE(0x00548940, GameNet::SetUniqueId);
REPLACE(0x00548A40, GameNet::JoinBuddy);
REPLACE(0x00548CB0, &GameNet::BuddyMessage::Construct);
REPLACE(0x00548CF0, &GameNet::Buddy::Destroy);
REPLACE(0x00548D20, &GameNet::BuddyMessage::Destroy);
REPLACE(0x00548D50, &BuddyMan::AddBuddy);
REPLACE(0x00548E10, &BuddyMan::CountBuddies);
REPLACE(0x00548E40, GameNet::AddBuddy);
REPLACE(0x00548E90, GameNet::HasBuddies);
REPLACE(0x00548ED0, GameNet::BuddyListFull);
REPLACE(0x00548F10, GameNet::AnyBuddyMessagesLeft);
REPLACE(0x00548F50, BuddyMan::OnBuddyStatus);
REPLACE(0x005496F0, BuddyMan::OnBuddyMessage);
REPLACE(0x005498E0, &BuddyMan::FillBuddyList);
REPLACE(0x005499A0, &BuddyMan::CancelAddBuddy);
REPLACE(0x00549A50, &BuddyMan::RemoveBuddy);
REPLACE(0x00549B90, &BuddyMan::LogOff);
REPLACE(0x00549CB0, &BuddyMan::IsBuddy);
REPLACE(0x00549D10, GameNet::ProfileLogOff);
REPLACE(0x00549D20, GameNet::FillBuddyList);
REPLACE(0x00549D80, GameNet::CancelAddBuddy);
REPLACE(0x00549D90, GameNet::RemoveBuddy);
REPLACE(0x00549DC0, GameNet::ShowNextBuddyMessage);
REPLACE(0x00549EA0, GameNet::IsAlreadyMyBuddy);
REPLACE(0x00549ED0, BuddyMan::OnError);
REPLACE(0x00549F50, &BuddyMan::FillProspectiveBuddyList);
REPLACE(0x0054A1A0, &BuddyMan::LogIn);
REPLACE(0x0054A250, GameNet::ProfileLogIn);
REPLACE(0x0054A2B0, GameNet::FillProspectiveBuddyList);
REPLACE(0x0054A2C0, BuddyMan::OnConnectNewUser);
REPLACE(0x0054A300, BuddyMan::BuddyTask);
REPLACE(0x0054A3D0, BuddyMan::CreateProfileThread);
REPLACE(0x0054A480, &BuddyMan::Construct);
REPLACE(0x0054A5C0, &BuddyMan::CreateProfile);
REPLACE(0x0054A650, GameNet::CreateProfile);
