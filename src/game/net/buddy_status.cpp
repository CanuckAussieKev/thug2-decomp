// The buddy list's GP requests, messages and status, its list types, and the folded getters (see buddy_status.h).
// GameSpy presence (GP) is library code, called by address.

#include "game/net/buddy_status.h"

#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_lobby.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/winmain.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint32_t kUiString = 0x96875C0F; // ui_string
constexpr uint32_t kString = 0x61414D56;   // string
constexpr int kStatusOnline = 1;
constexpr int kStatusChatting = 4;

inline void** const kBuddySearchVtable = reinterpret_cast<void**>(0x0064CECC);
inline void** const kMessageSearchVtable = reinterpret_cast<void**>(0x0064CED0);
inline void** const kBuddyNodeVtable = reinterpret_cast<void**>(0x0064CF60);
inline void** const kMessageNodeVtable = reinterpret_cast<void**>(0x0064CF64);

Manager* TheManager() { return g_gameNetManager; }
Mem::Manager* Memory() { return Mem::Manager::Get(); }
BuddyMan* Buddies() { return static_cast<BuddyMan*>(TheManager()->m_buddyMan); }

template <typename R = void, typename... A>
R Gp(uint32_t address, A... args)
{
    return GameFunction<R(__cdecl*)(A...)>(address)(args...);
}

void UnlinkNode(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// 0x00548100
void __cdecl BuddyMan::OnBuddyRequest(void* connection, const void* arg, BuddyMan* man)
{
    (void)connection;
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    Gp(0x006001A0, man, *static_cast<const int*>(arg)); // (the request's profile accepted)
    Memory()->PopContext();
}

GAME_GLOBAL(char[256], g_commandLineEmail, 0x007CCA90);
GAME_GLOBAL(char[16], g_commandLinePassword, 0x007CCB90);

// 0x00548140
unsigned long __stdcall BuddyMan::LoginThread(void* man)
{
    Manager* manager = TheManager();
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    Prefs::Preferences* preferences = manager->GetNetworkPreferences();
    const char* email;
    const char* password;
    if (Sys::g_internetGameHost || Sys::g_joiningInternetGame) {
        email = g_commandLineEmail;
        password = g_commandLinePassword;
    } else {
        email = preferences->GetPreferenceString(Crc::GenerateCRCFromString("profile_email"),
                                                 Crc::GenerateCRCFromString("ui_string"));
        password = preferences->GetPreferenceString(Crc::GenerateCRCFromString("profile_password"),
                                                    Crc::GenerateCRCFromString("ui_string"));
    }
    Gp(0x005FFDE0, man, "proskater", email, password, 1, 0, &BuddyMan::OnConnectNewUser, man); // (gpConnect)
    Memory()->PopContext();
    return 0;
}

// 0x00548200
void BuddyMan::SendBuddyMessage(int profile, const char* text)
{
    const char* nick;
    TheManager()->GetNetworkPreferences()->GetPreference(Crc::GenerateCRCFromString("network_id"))->GetString(
        kUiString, &nick, 1);
    char message[128];
    std::sprintf(message, "%s\n%s", nick, text);
    Gp(0x006005D0, this, profile, message); // (gpSendBuddyMessage)
}

// 0x005482A0
void BuddyMan::SetLoggedIn(int profile)
{
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    m_loggedIn = true;
    m_profile = profile;
    m_state = 4;
    Script::RunScript("profile_logged_in", nullptr, nullptr, false);
    const char* location = Script::GetString("homie_status_logging_in");
    Gp(0x00600330, this, kStatusOnline, Script::GetString("homie_status_online"), location); // (gpSetStatus)
    Memory()->PopContext();
}

// 0x00548320
void BuddyMan::SetStatus(int status, const char* text, const char* location)
{
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    Gp(0x00600330, this, status, text, location); // (gpSetStatus)
    Memory()->PopContext();
}

// --- The list types ---

// 0x005480D0 / 0x00548440
void BuddySearch::DestroyBody() { m_vtable = kBuddySearchVtable; }
BuddySearch* BuddySearch::Destroy(unsigned int flags)
{
    m_vtable = kBuddySearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}
// 0x005480E0 / 0x00548470
void BuddyMessageSearch::DestroyBody() { m_vtable = kMessageSearchVtable; }
BuddyMessageSearch* BuddyMessageSearch::Destroy(unsigned int flags)
{
    m_vtable = kMessageSearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}
// 0x005484A0 / 0x005484E0
void BuddyNode::DestroyBody()
{
    vtable = kBuddyNodeVtable;
    UnlinkNode(this);
}
BuddyNode* BuddyNode::Destroy(unsigned int flags)
{
    vtable = kBuddyNodeVtable;
    UnlinkNode(this);
    if (flags & 1)
        Memory()->Free(this);
    return this;
}
// 0x005484C0 / 0x00548520
void BuddyMessageNode::DestroyBody()
{
    vtable = kMessageNodeVtable;
    UnlinkNode(this);
}
BuddyMessageNode* BuddyMessageNode::Destroy(unsigned int flags)
{
    vtable = kMessageNodeVtable;
    UnlinkNode(this);
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// --- The folded getters ---

// 0x005480B0 / 0x005480C0 / 0x005480F0
bool FoldedGetters::Byte10() { return Field<bool>(this, 0x10); }
uint32_t FoldedGetters::Word04() { return Field<uint32_t>(this, 0x04); }
uint32_t FoldedGetters::Word18() { return Field<uint32_t>(this, 0x18); }

// --- The script commands ---

// 0x00548360: whether we're logged in to presence.
bool __cdecl ProfileLoggedIn(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    return Buddies()->m_loggedIn;
}

// 0x00548370: the message `string` sent to `profile` (false for none or an empty one).
bool __cdecl SendBuddyMessage(Script::CStruct* params, Script::CScript* script)
{
    (void)script;
    Manager* manager = TheManager();
    const char* text = nullptr;
    params->GetString(kString, &text, 0);
    if (!text || !*text)
        return false;
    int profile;
    params->GetInteger("profile", &profile, 1);
    static_cast<BuddyMan*>(manager->m_buddyMan)->SendBuddyMessage(profile, text);
    return true;
}

// 0x005483E0: our presence status: chatting in the lobby.
bool __cdecl SetLobbyStatus(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    const char* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan)->m_roomName; // (0x0053F820)
    const char* text = Script::GetString("homie_status_chatting");
    BuddyMan* buddies = static_cast<BuddyMan*>(manager->m_buddyMan);
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    Gp(0x00600330, buddies, kStatusChatting, text, lobby); // (gpSetStatus)
    Memory()->PopContext();
    return true;
}

} // namespace GameNet

using GameNet::BuddyMan;
REPLACE(0x00548100, BuddyMan::OnBuddyRequest); // (not a Ghidra function)
REPLACE(0x00548140, BuddyMan::LoginThread);    // (not a Ghidra function)
REPLACE(0x00548200, &BuddyMan::SendBuddyMessage);
REPLACE(0x005482A0, &BuddyMan::SetLoggedIn);
REPLACE(0x00548320, &BuddyMan::SetStatus);
REPLACE(0x005480D0, &GameNet::BuddySearch::DestroyBody);
REPLACE(0x00548440, &GameNet::BuddySearch::Destroy);
REPLACE(0x005480E0, &GameNet::BuddyMessageSearch::DestroyBody);
REPLACE(0x00548470, &GameNet::BuddyMessageSearch::Destroy);
REPLACE(0x005484A0, &GameNet::BuddyNode::DestroyBody);
REPLACE(0x005484E0, &GameNet::BuddyNode::Destroy);
REPLACE(0x005484C0, &GameNet::BuddyMessageNode::DestroyBody);
REPLACE(0x00548520, &GameNet::BuddyMessageNode::Destroy);
REPLACE_SOURCE_ONLY(0x005480B0, &GameNet::FoldedGetters::Byte10); // 4 bytes
REPLACE_SOURCE_ONLY(0x005480C0, &GameNet::FoldedGetters::Word04); // 4 bytes
REPLACE_SOURCE_ONLY(0x005480F0, &GameNet::FoldedGetters::Word18); // 4 bytes
REPLACE(0x00548360, GameNet::ProfileLoggedIn);
REPLACE(0x00548370, GameNet::SendBuddyMessage);
REPLACE(0x005483E0, GameNet::SetLobbyStatus);
