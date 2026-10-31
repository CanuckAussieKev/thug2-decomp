// The buddy list's GP requests, messages and status, its list types, and the folded getters (see buddy_status.h).
// GameSpy presence (GP) is library code, called by address.

#include "engine/net/buddy_status.h"
#include "engine/net/gamespy.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/winmain.h"
#include "engine/core/code_address.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace OnlinePlay {

using namespace Detail;

namespace {

constexpr uint32_t kUiString = 0x96875C0F; // ui_string
constexpr uint32_t kString = 0x61414D56;   // string
constexpr int kStatusOnline = 1;
constexpr int kStatusChatting = 4;

static VtableSlot kBuddySearchVtable_slots[] = {&OnlinePlay::BuddySearch::Teardown};
void** const kBuddySearchVtable = reinterpret_cast<void**>(kBuddySearchVtable_slots);
static VtableSlot kMessageSearchVtable_slots[] = {&OnlinePlay::BuddyMessageSearch::Teardown};
void** const kMessageSearchVtable = reinterpret_cast<void**>(kMessageSearchVtable_slots);

Hub* TheManager() { return g_onlineHub; }
MemSys::Hub* RamStats() { return MemSys::Hub::Pull(); }
FriendsManager* Buddies() { return static_cast<FriendsManager*>(TheManager()->m_presenceService); }

void UnlinkNode(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

static VtableSlot kBuddyNodeVtable_slots[] = {&OnlinePlay::BuddyNode::Teardown};
void** const kBuddyNodeVtable = reinterpret_cast<void**>(kBuddyNodeVtable_slots);
static VtableSlot kMessageNodeVtable_slots[] = {&OnlinePlay::BuddyMessageNode::Teardown};
void** const kMessageNodeVtable = reinterpret_cast<void**>(kMessageNodeVtable_slots);

// 0x00548100
void __cdecl FriendsManager::OnBuddyRequest(void* connection, const void* arg, FriendsManager* man)
{
    (void)connection;
    RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap);
    GameSpy::GpAuthBuddyRequest(man, *static_cast<const int*>(arg)); // (the request's profile accepted)
    RamStats()->LeaveHeapScope();
}

char g_commandLineEmail[256];
char g_commandLinePassword[16];

// 0x00548140
unsigned long __stdcall FriendsManager::LoginThread(void* man)
{
    Hub* manager = TheManager();
    RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap);
    OptionStore::Preferences* preferences = manager->GetOnlineSettings();
    const char* email;
    const char* password;
    if (SystemCore::g_internetGameHost || SystemCore::g_joiningInternetGame) {
        email = g_commandLineEmail;
        password = g_commandLinePassword;
    } else {
        email = preferences->GetPreferenceString(Checksums::HashFromText("profile_email"),
                                                 Checksums::HashFromText("ui_string"));
        password = preferences->GetPreferenceString(Checksums::HashFromText("profile_password"),
                                                    Checksums::HashFromText("ui_string"));
    }
    GameSpy::GpConnect(man, "proskater", email, password, 1, 0, &FriendsManager::OnConnectNewUser, man); // (gpConnect)
    RamStats()->LeaveHeapScope();
    return 0;
}

// 0x00548200
void FriendsManager::SendBuddyMessage(int profile, const char* text)
{
    const char* nick;
    TheManager()->GetOnlineSettings()->FetchSetting(Checksums::HashFromText("network_id"))->FetchText(
        kUiString, &nick, 1);
    char message[128];
    std::sprintf(message, "%s\n%s", nick, text);
    GameSpy::GpSendBuddyMessage(this, profile, message); // (gpSendBuddyMessage)
}

// 0x005482A0
void FriendsManager::ChangeOnlineFlag(int profile)
{
    RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap);
    m_authenticated = true;
    m_account = profile;
    m_curPhase = 4;
    Scripts::LaunchScript("profile_logged_in", nullptr, nullptr, false);
    const char* location = Scripts::FetchText("homie_status_logging_in");
    GameSpy::GpSetStatus(this, kStatusOnline, Scripts::FetchText("homie_status_online"), location); // (gpSetStatus)
    RamStats()->LeaveHeapScope();
}

// 0x00548320
void FriendsManager::ChangeStanding(int status, const char* text, const char* location)
{
    RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap);
    GameSpy::GpSetStatus(this, status, text, location); // (gpSetStatus)
    RamStats()->LeaveHeapScope();
}

// --- The list types ---

// 0x005480D0 / 0x00548440
void BuddySearch::DestroyBody() { m_vtable = kBuddySearchVtable; }
BuddySearch* BuddySearch::Teardown(unsigned int flags)
{
    m_vtable = kBuddySearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}
// 0x005480E0 / 0x00548470
void BuddyMessageSearch::DestroyBody() { m_vtable = kMessageSearchVtable; }
BuddyMessageSearch* BuddyMessageSearch::Teardown(unsigned int flags)
{
    m_vtable = kMessageSearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}
// 0x005484A0 / 0x005484E0
void BuddyNode::DestroyBody()
{
    vtable = kBuddyNodeVtable;
    UnlinkNode(this);
}
BuddyNode* BuddyNode::Teardown(unsigned int flags)
{
    vtable = kBuddyNodeVtable;
    UnlinkNode(this);
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}
// 0x005484C0 / 0x00548520
void BuddyMessageNode::DestroyBody()
{
    vtable = kMessageNodeVtable;
    UnlinkNode(this);
}
BuddyMessageNode* BuddyMessageNode::Teardown(unsigned int flags)
{
    vtable = kMessageNodeVtable;
    UnlinkNode(this);
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// --- The folded getters ---

// 0x005480B0 / 0x005480C0 / 0x005480F0
bool FoldedGetters::Byte10() { return Prop<bool>(this, 0x10); }
uint32_t FoldedGetters::Word04() { return Prop<uint32_t>(this, 0x04); }
uint32_t FoldedGetters::Word18() { return Prop<uint32_t>(this, 0x18); }

// --- The script commands ---

// 0x00548360: whether we're logged in to presence.
bool __cdecl ProfileLoggedIn(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    return Buddies()->m_authenticated;
}

// 0x00548370: the message `string` sent to `profile` (false for none or an empty one).
bool __cdecl SendBuddyMessage(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)script;
    Hub* manager = TheManager();
    const char* text = nullptr;
    params->FetchText(kString, &text, 0);
    if (!text || !*text)
        return false;
    int profile;
    params->FetchIntValue("profile", &profile, 1);
    static_cast<FriendsManager*>(manager->m_presenceService)->SendBuddyMessage(profile, text);
    return true;
}

// 0x005483E0: our presence status: chatting in the lobby.
bool __cdecl SetLobbyStatus(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    Hub* manager = TheManager();
    const char* lobby = static_cast<RoomManager*>(manager->m_lobbyService)->m_roomName; // (0x0053F820)
    const char* text = Scripts::FetchText("homie_status_chatting");
    FriendsManager* buddies = static_cast<FriendsManager*>(manager->m_presenceService);
    RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap);
    GameSpy::GpSetStatus(buddies, kStatusChatting, text, lobby); // (gpSetStatus)
    RamStats()->LeaveHeapScope();
    return true;
}

} // namespace OnlinePlay

using OnlinePlay::FriendsManager;
