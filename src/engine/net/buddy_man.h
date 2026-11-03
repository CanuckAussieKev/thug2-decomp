#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "engine/core/arena_alloc.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script
namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks

// The online profile and buddy list ("homies"): online::Hub's presence_service_ (+0x18), a wrapper of the GameSpy
// presence SDK (GP: library code, not decompiled - called by address). The profile is created or logged into from
// a thread; the GP callbacks keep the buddy list and the buddy messages up to date and run the lobby menus' scripts.
// 0x00548560-0x0054A660 (the rest of the class, 0x00548040-0x00548560, is with the lobby code before it).
namespace online {

// A buddy (0x24C bytes, memory::alloc_zeroed'd): an lists::Elem<Buddy> (its scalar deleting destructor 0x00548CF0)
// carrying the GP buddy's details. (The buddy records' nodes keep the original's size.)
struct Buddy {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    std::array<char, 32>& nick() { return nick_; }
    [[nodiscard]] const std::array<char, 32>& nick() const { return nick_; }
    [[nodiscard]] int account() const { return account_; }
    void set_account(int value) { account_ = value; }
    [[nodiscard]] int cur_standing() const { return cur_standing_; }
    void set_cur_standing(int value) { cur_standing_ = value; }
    [[nodiscard]] bool pending() const { return pending_; }
    void set_pending(bool value) { pending_ = value; }
    [[nodiscard]] uint32_t join_ip() const { return join_ip_; }
    void set_join_ip(uint32_t value) { join_ip_ = value; }
    [[nodiscard]] uint32_t join_private_ip() const { return join_private_ip_; }
    void set_join_private_ip(uint32_t value) { join_private_ip_ = value; }
    [[nodiscard]] int join_port() const { return join_port_; }
    void set_join_port(int value) { join_port_ = value; }
    std::array<char, 256>& location() { return location_; }
    [[nodiscard]] const std::array<char, 256>& location() const { return location_; }
    std::array<char, 256>& status_text() { return status_text_; }
    [[nodiscard]] const std::array<char, 256>& status_text() const { return status_text_; }

private:
    memory::ListNode list_link_; // +0x00 data = this
    std::array<char, 32> nick_;              // +0x14
    int account_;                // +0x34 the GameSpy profile id
    int cur_standing_; // +0x38 GP status: 0 offline, 1 online, 2 playing (in a THUG2 game: the location is one)
    bool pending_;     // +0x3C asked to be a buddy, no answer yet (not shown in the list)
    std::array<uint8_t, 3> pad3d_;
    uint32_t join_ip_;         // +0x40 the game the buddy plays in (from the location string, kThug2Location)
    uint32_t join_private_ip_; // +0x44
    int join_port_;            // +0x48
    std::array<char, 256> location_;       // +0x4C the game's name (or the GP location string)
    std::array<char, 256> status_text_;    // +0x14C

public:

    // 0x00548CF0 (scalar deleting): the node destructor (0x005484A0: unlinked), freed with
    // flag 1. Returns this.
    Buddy* teardown(unsigned int flags);
};

// A buddy message waiting to be shown (0x68 bytes): an lists::Elem<FriendNote> (its scalar deleting destructor
// 0x00548D20). GP messages are "nick\ntext" (SendBuddyMessage); a message of one line comes
// from a buddy and gets the buddy's nick.
struct FriendNote {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] int account() const { return account_; }
    void set_account(int value) { account_ = value; }
    std::array<char, 16>& nick() { return nick_; }
    [[nodiscard]] const std::array<char, 16>& nick() const { return nick_; }
    std::array<char, 64>& text() { return text_; }
    [[nodiscard]] const std::array<char, 64>& text() const { return text_; }

private:
    memory::ListNode list_link_{}; // +0x00 data = this
    int account_{0};               // +0x14 the sender
    std::array<char, 16> nick_{};            // +0x18
    std::array<char, 64> text_{};            // +0x28

public:

    // 0x00548CB0: unlinked, profile 0, nick "Proskater", no text.
    FriendNote();
    // 0x00548D20 (scalar deleting): the node destructor (0x005484C0: unlinked), freed with
    // flag 1. Returns this.
    FriendNote* teardown(unsigned int flags);
};

class FriendsManager {
public:
    // 0x0054A480: GP set up (product 0x2880) with this as the connection and the callbacks (error, buddy request -
    // 0x00548100, buddy status, buddy message; two ignored), on the network heap; the task (buddy_task, not added
    // yet).
    FriendsManager();
    // 0x005488B0: the task deleted, the two list heads unlinked (the buddies themselves aren't freed).
    ~FriendsManager();
    FriendsManager(const FriendsManager&) = delete;
    FriendsManager& operator=(const FriendsManager&) = delete;

    // 0x0054A1A0: log into the profile: logged off first, state 2, a thread (0x00548140) connects; the task joins
    // the main loop's logic tasks (once). Starts the 10 s login timer (0x007CE458).
    void log_in();
    // 0x0054A5C0: create the profile (state 1): a thread (create_profile_thread) asks GP for a new user; the task
    // joins the logic tasks.
    void spawn_account();
    // 0x00549B90: GP disconnected, the task out of its list, not logged in; every buddy and message deleted.
    void log_off();

    // 0x00548D50: ask `profile` (nick `nick`) to be a buddy: a pending buddy until GP's status for it comes.
    void add_buddy(const char* nick, int profile);
    // 0x00549A50: `profile` off the list (and GP's), the removed_buddy script run for its menu item.
    void remove_buddy(int profile);
    // 0x005499A0: the first pending buddy deleted (the request isn't taken back from GP).
    void cancel_add_buddy();
    // 0x00549CB0: whether `profile` is on the list (pending or not).
    bool is_buddy(int profile);
    // 0x00548E10: the buddies (pending ones too).
    int count_buddies();
    // 0x005498E0: a menu item for each buddy that isn't pending (show_buddy); with `clear`, the old items destroyed
    // first.
    void fill_buddy_list(bool clear, bool allow_join, bool allow_remove);
    // 0x00549F50: a menu item (add_buddy) for each player of the current network game who has a profile, isn't on
    // this machine and isn't a buddy yet.
    void fill_prospective_buddy_list();

    // 0x00548560 (stdcall, `this` unused): the homie_list_add_item script for `buddy`: its name, status, location
    // and id; choosing it does nothing, or (`allow_join` / `allow_remove`) present_buddy_options with its details.
    static void show_buddy(Buddy* buddy, bool allow_join, bool allow_remove);

    // The GP callbacks (cdecl: the connection, the callback's argument, this).
    // 0x00549ED0: errors: login failed (0x107: logged off, CreateBuddyLoginFailedDialog), wrong password (0x202),
    // the nick taken by a new user (0x201: state 3, log in instead), already a buddy (0x603).
    static void on_error(void* connection, const void* arg, FriendsManager* man);
    // 0x00548F50: a buddy's status: the buddy updated (a pending one becomes a buddy: added_buddy, and it goes into
    // the buddy_array preference) or a new buddy made (its nick from buddy_array); update_buddy_status run.
    static void on_buddy_status(void* connection, const void* arg, FriendsManager* man);
    // 0x005496F0: a buddy message (at most 21 kept): "nick\ntext", or a buddy's text with its nick (from someone
    // else: dropped). Script 0x10C3647C run for a new one.
    static void on_buddy_message(void* connection, const void* arg, FriendsManager* man);
    // 0x0054A2C0: gpConnectNewUser's answer: success (0) -> logged in (0x005482A0); the nick taken (4) while a new
    // profile is being made -> log in instead.
    static void on_connect_new_user(void* connection, const void* arg, FriendsManager* man);

    // 0x0054A300: the task: state 1 / 2 (creating / logging in): GP processed, or after 10 s logged off and
    // CreateBuddyLoginFailedDialog; state 3: LogIn; state 4 (logged in): GP processed once a second.
    static void buddy_task(tasks::Task<FriendsManager>* task);
    // 0x0054A3D0 (thread, stdcall): gpConnectNewUser for nick "proskater" with the profile_email / profile_password
    // preferences. Returns 0.
    static unsigned long __stdcall create_profile_thread(void* man);

    // --- buddy_status.cpp (0x00548100-0x00548560) ---
    // 0x00548100 (GP's buddy request callback): the request accepted (gpAuthBuddyRequest, 0x006001A0).
    static void on_buddy_request(void* connection, const void* arg, FriendsManager* man);
    // 0x00548140 (thread, stdcall): gpConnect for nick "proskater" with the profile_email / profile_password
    // preferences (or the command line's, 0x007CCA90 / 0x007CCB90, when 0x007CC260 or 0x007CCA80 is set),
    // on_connect_new_user for the answer. Returns 0.
    static unsigned long __stdcall login_thread(void* man);
    // 0x00548200: "<our network_id>\n<text>" sent to `profile` (gpSendBuddyMessage, 0x006005D0).
    void send_buddy_message(int profile, const char* text);
    // 0x005482A0: logged in as `profile` (state 4): profile_logged_in, the status online ("logging in").
    void change_online_flag(int profile);
    // 0x00548320: the presence status (gpSetStatus, 0x00600330: status, its text, the location).
    void change_standing(int status, const char* text, const char* location);

    // The fields other code reads and sets.
    [[nodiscard]] int account() const { return account_; }
    [[nodiscard]] bool authenticated() const { return authenticated_; }
    memory::ListNode& friends() { return friends_; }
    [[nodiscard]] const memory::ListNode& friends() const { return friends_; }
    memory::ListNode& messages() { return messages_; }
    [[nodiscard]] const memory::ListNode& messages() const { return messages_; }

private:
    void* connection_{}; // +0x00 GPConnection (this object is passed as its address)
    int account_;      // +0x04 the logged-in profile id
    int cur_phase_;    // +0x08 0 none, 1 creating the profile, 2 logging in, 3 log in again, 4 logged in
    std::unique_ptr<tasks::SchedulerTask> task_; // +0x0C tasks::Task<FriendsManager> (buddy_task)
    bool authenticated_;                         // +0x10
    memory::ListNode friends_{};                 // +0x14 lists::Foremost<Buddy>
    memory::ListNode messages_{};                // +0x28 lists::Foremost<FriendNote>
};

// The script commands (CFunctions: the parameters, the script).
bool set_unique_id(script::Struct* params, script::Script* script);               // 0x00548940
bool join_buddy(script::Struct* params, script::Script* script);                  // 0x00548A40
bool add_buddy(script::Struct* params, script::Script* script);                   // 0x00548E40
bool has_buddies(script::Struct* params, script::Script* script);                 // 0x00548E90
bool buddy_list_full(script::Struct* params, script::Script* script);             // 0x00548ED0
bool any_buddy_messages_left(script::Struct* params, script::Script* script);     // 0x00548F10
bool profile_log_off(script::Struct* params, script::Script* script);             // 0x00549D10
bool fill_buddy_list(script::Struct* params, script::Script* script);             // 0x00549D20
bool cancel_add_buddy(script::Struct* params, script::Script* script);            // 0x00549D80
bool remove_buddy(script::Struct* params, script::Script* script);                // 0x00549D90
bool show_next_buddy_message(script::Struct* params, script::Script* script);     // 0x00549DC0
bool is_already_my_buddy(script::Struct* params, script::Script* script);         // 0x00549EA0
bool profile_log_in(script::Struct* params, script::Script* script);              // 0x0054A250
bool fill_prospective_buddy_list(script::Struct* params, script::Script* script); // 0x0054A2B0
bool spawn_account(script::Struct* params, script::Script* script);               // 0x0054A650

} // namespace online
