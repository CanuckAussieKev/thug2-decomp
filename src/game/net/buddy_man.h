#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Tsk {
class BaseTask;
} // namespace Tsk

// The online profile and buddy list ("homies"): GameNet::Manager's m_buddyMan (+0x18), a wrapper of the GameSpy
// presence SDK (GP: library code, not decompiled - called by address). The profile is created or logged into from
// a thread; the GP callbacks keep the buddy list and the buddy messages up to date and run the lobby menus' scripts.
// 0x00548560-0x0054A660 (the rest of the class, 0x00548040-0x00548560, is with the lobby code before it).
namespace GameNet {

// A buddy (0x24C bytes, Mem::Calloc'd): an Lst::Node<Buddy> (vtable 0x0064D140, its scalar deleting destructor
// 0x00548CF0) carrying the GP buddy's details.
struct Buddy {
    Mem::ListNode m_node;   // +0x00 data = this
    char m_nick[32];        // +0x14
    int m_profile;          // +0x34 the GameSpy profile id
    int m_status;           // +0x38 GP status: 0 offline, 1 online, 2 playing (in a THUG2 game: the location is one)
    bool m_pending;         // +0x3C asked to be a buddy, no answer yet (not shown in the list)
    uint8_t m_pad3D[3];
    uint32_t m_joinIp;      // +0x40 the game the buddy plays in (from the location string, kThug2Location)
    uint32_t m_joinPrivateIp; // +0x44
    int m_joinPort;         // +0x48
    char m_location[256];   // +0x4C the game's name (or the GP location string)
    char m_statusText[256]; // +0x14C

    // 0x00548CF0 (vtable 0x0064D140 slot 0, scalar deleting): the node destructor (0x005484A0: unlinked), freed with
    // flag 1. Returns this.
    Buddy* Destroy(unsigned int flags);
};
static_assert(sizeof(Buddy) == 0x24C && offsetof(Buddy, m_profile) == 0x34 && offsetof(Buddy, m_joinIp) == 0x40 &&
                  offsetof(Buddy, m_location) == 0x4C && offsetof(Buddy, m_statusText) == 0x14C,
              "layout must match the original");

// A buddy message waiting to be shown (0x68 bytes): an Lst::Node<BuddyMessage> (vtable 0x0064D14C, its scalar
// deleting destructor 0x00548D20). GP messages are "nick\ntext" (SendBuddyMessage); a message of one line comes
// from a buddy and gets the buddy's nick.
struct BuddyMessage {
    Mem::ListNode m_node;   // +0x00 data = this
    int m_profile;          // +0x14 the sender
    char m_nick[16];        // +0x18
    char m_text[64];        // +0x28

    // 0x00548CB0: unlinked, profile 0, nick "Proskater", no text. Returns this.
    BuddyMessage* Construct();
    // 0x00548D20 (vtable 0x0064D14C slot 0, scalar deleting): the node destructor (0x005484C0: unlinked), freed with
    // flag 1. Returns this.
    BuddyMessage* Destroy(unsigned int flags);
};
static_assert(sizeof(BuddyMessage) == 0x68 && offsetof(BuddyMessage, m_nick) == 0x18 &&
                  offsetof(BuddyMessage, m_text) == 0x28,
              "layout must match the original");

class BuddyMan {
public:
    // 0x0054A480: GP set up (product 0x2880) with this as the connection and the callbacks (error, buddy request -
    // 0x00548100, buddy status, buddy message; two ignored), on the network heap; the task (BuddyTask, not added
    // yet). Returns this.
    BuddyMan* Construct();
    // 0x005488B0 (destructor body): the task deleted, the two list heads unlinked (the buddies themselves aren't
    // freed).
    void DestroyBody();

    // 0x0054A1A0: log into the profile: logged off first, state 2, a thread (0x00548140) connects; the task joins
    // the main loop's logic tasks (once). Starts the 10 s login timer (0x007CE458).
    void LogIn();
    // 0x0054A5C0: create the profile (state 1): a thread (CreateProfileThread) asks GP for a new user; the task
    // joins the logic tasks.
    void CreateProfile();
    // 0x00549B90: GP disconnected, the task out of its list, not logged in; every buddy and message deleted.
    void LogOff();

    // 0x00548D50: ask `profile` (nick `nick`) to be a buddy: a pending buddy until GP's status for it comes.
    void AddBuddy(const char* nick, int profile);
    // 0x00549A50: `profile` off the list (and GP's), the removed_buddy script run for its menu item.
    void RemoveBuddy(int profile);
    // 0x005499A0: the first pending buddy deleted (the request isn't taken back from GP).
    void CancelAddBuddy();
    // 0x00549CB0: whether `profile` is on the list (pending or not).
    bool IsBuddy(int profile);
    // 0x00548E10: the buddies (pending ones too).
    int CountBuddies();
    // 0x005498E0: a menu item for each buddy that isn't pending (ShowBuddy); with `clear`, the old items destroyed
    // first.
    void FillBuddyList(bool clear, bool allowJoin, bool allowRemove);
    // 0x00549F50: a menu item (add_buddy) for each player of the current network game who has a profile, isn't on
    // this machine and isn't a buddy yet.
    void FillProspectiveBuddyList();

    // 0x00548560 (stdcall, `this` unused): the homie_list_add_item script for `buddy`: its name, status, location
    // and id; choosing it does nothing, or (`allowJoin` / `allowRemove`) present_buddy_options with its details.
    static void __stdcall ShowBuddy(Buddy* buddy, bool allowJoin, bool allowRemove);

    // The GP callbacks (cdecl: the connection, the callback's argument, this).
    // 0x00549ED0: errors: login failed (0x107: logged off, CreateBuddyLoginFailedDialog), wrong password (0x202),
    // the nick taken by a new user (0x201: state 3, log in instead), already a buddy (0x603).
    static void __cdecl OnError(void* connection, const void* arg, BuddyMan* man);
    // 0x00548F50: a buddy's status: the buddy updated (a pending one becomes a buddy: added_buddy, and it goes into
    // the buddy_array preference) or a new buddy made (its nick from buddy_array); update_buddy_status run.
    static void __cdecl OnBuddyStatus(void* connection, const void* arg, BuddyMan* man);
    // 0x005496F0: a buddy message (at most 21 kept): "nick\ntext", or a buddy's text with its nick (from someone
    // else: dropped). Script 0x10C3647C run for a new one.
    static void __cdecl OnBuddyMessage(void* connection, const void* arg, BuddyMan* man);
    // 0x0054A2C0: gpConnectNewUser's answer: success (0) -> logged in (0x005482A0); the nick taken (4) while a new
    // profile is being made -> log in instead.
    static void __cdecl OnConnectNewUser(void* connection, const void* arg, BuddyMan* man);

    // 0x0054A300: the task: state 1 / 2 (creating / logging in): GP processed, or after 10 s logged off and
    // CreateBuddyLoginFailedDialog; state 3: LogIn; state 4 (logged in): GP processed once a second.
    static void __cdecl BuddyTask(Tsk::BaseTask* task);
    // 0x0054A3D0 (thread, stdcall): gpConnectNewUser for nick "proskater" with the profile_email / profile_password
    // preferences. Returns 0.
    static unsigned long __stdcall CreateProfileThread(void* man);

    // --- buddy_status.cpp (0x00548100-0x00548560) ---
    // 0x00548100 (GP's buddy request callback): the request accepted (gpAuthBuddyRequest, 0x006001A0).
    static void __cdecl OnBuddyRequest(void* connection, const void* arg, BuddyMan* man);
    // 0x00548140 (thread, stdcall): gpConnect for nick "proskater" with the profile_email / profile_password
    // preferences (or the command line's, 0x007CCA90 / 0x007CCB90, when 0x007CC260 or 0x007CCA80 is set),
    // OnConnectNewUser for the answer. Returns 0.
    static unsigned long __stdcall LoginThread(void* man);
    // 0x00548200: "<our network_id>\n<text>" sent to `profile` (gpSendBuddyMessage, 0x006005D0).
    void SendBuddyMessage(int profile, const char* text);
    // 0x005482A0: logged in as `profile` (state 4): profile_logged_in, the status online ("logging in").
    void SetLoggedIn(int profile);
    // 0x00548320: the presence status (gpSetStatus, 0x00600330: status, its text, the location).
    void SetStatus(int status, const char* text, const char* location);

    void* m_connection;       // +0x00 GPConnection (this object is passed as its address)
    int m_profile;            // +0x04 the logged-in profile id
    int m_state;              // +0x08 0 none, 1 creating the profile, 2 logging in, 3 log in again, 4 logged in
    Tsk::BaseTask* m_task;    // +0x0C Tsk::Task<BuddyMan> (BuddyTask)
    bool m_loggedIn;          // +0x10
    uint8_t m_pad11[3];
    Mem::ListNode m_buddies;  // +0x14 Lst::Head<Buddy>
    Mem::ListNode m_messages; // +0x28 Lst::Head<BuddyMessage>
};
static_assert(offsetof(BuddyMan, m_loggedIn) == 0x10 && offsetof(BuddyMan, m_buddies) == 0x14 &&
                  offsetof(BuddyMan, m_messages) == 0x28 && sizeof(BuddyMan) == 0x3C,
              "layout must match the original");

// The script commands (CFunctions: the parameters, the script).
bool __cdecl SetUniqueId(Script::CStruct* params, Script::CScript* script);            // 0x00548940
bool __cdecl JoinBuddy(Script::CStruct* params, Script::CScript* script);              // 0x00548A40
bool __cdecl AddBuddy(Script::CStruct* params, Script::CScript* script);               // 0x00548E40
bool __cdecl HasBuddies(Script::CStruct* params, Script::CScript* script);             // 0x00548E90
bool __cdecl BuddyListFull(Script::CStruct* params, Script::CScript* script);          // 0x00548ED0
bool __cdecl AnyBuddyMessagesLeft(Script::CStruct* params, Script::CScript* script);   // 0x00548F10
bool __cdecl ProfileLogOff(Script::CStruct* params, Script::CScript* script);          // 0x00549D10
bool __cdecl FillBuddyList(Script::CStruct* params, Script::CScript* script);          // 0x00549D20
bool __cdecl CancelAddBuddy(Script::CStruct* params, Script::CScript* script);         // 0x00549D80
bool __cdecl RemoveBuddy(Script::CStruct* params, Script::CScript* script);            // 0x00549D90
bool __cdecl ShowNextBuddyMessage(Script::CStruct* params, Script::CScript* script);   // 0x00549DC0
bool __cdecl IsAlreadyMyBuddy(Script::CStruct* params, Script::CScript* script);       // 0x00549EA0
bool __cdecl ProfileLogIn(Script::CStruct* params, Script::CScript* script);           // 0x0054A250
bool __cdecl FillProspectiveBuddyList(Script::CStruct* params, Script::CScript* script); // 0x0054A2B0
bool __cdecl CreateProfile(Script::CStruct* params, Script::CScript* script);          // 0x0054A650

} // namespace GameNet
