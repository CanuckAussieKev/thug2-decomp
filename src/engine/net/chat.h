#pragma once

namespace script {
class Struct;
class Script;
} // namespace script

namespace net {
struct InboundMsgCtx;
}

namespace tasks {
template <typename Owner>
class Task;
} // namespace tasks
namespace online {

class Hub; // the network game manager (not decompiled yet; one instance, see fetch_hub)

Hub* fetch_hub();

// Send a chat line to the other players and show it locally, as the local player.
void send_chat_message(Hub* manager, const char* text);

// Send a chat line under another name - every player sees "<name> : <text>". Other players need nothing
// special to see it: it's an ordinary chat message whose sender name is `name` (up to 15 characters).
void send_named_chat_message(Hub* manager, const char* name, const char* text);

// Add a line to this player's chat display only (nothing is sent). Text can use \cN colour codes.
void show_local_chat_line(const char* line);

// Chat commands typed into the chat box (lines starting with '/'). Returns true if `text` was a command
// and has been handled, in which case it isn't sent as chat.
//   /hello   - sends "Hello, World!" in two colours as a message from "Server"
//   /colours - shows (locally) each \cN colour code in its own colour
bool handle_chat_command(Hub* manager, const char* text);

// 0x005334D0: the manager's keyboard task (tasks::Task, data the manager - not used): Enter opens the chat box
// (script 0x3B4548B8) unless a dialog is up; F1-F4 run the taunt script (0x2C43B5BA) with a taunt name, at
// most one every two seconds. With PartyMod's keyboard input built in: F1 is key 0x1F.
void keyboard_task(tasks::Task<Hub>* task);

// 0x00532050: the chat handler (message 76, registered by the join): "\c<colour + 2><name>\c0 : <text>" added to
// the chat display (create_console_message text=...). 1. In the THUGPRO peer mode (thugpro_peer.h) the message is
// THUGPRO's layout (a machine id first) and a malformed one is dropped.
int on_chat_msg(net::InboundMsgCtx* context);

} // namespace online

// Script function SendChatMessage (in the Function table).
bool cfunc_send_chat_message(script::Struct* params, script::Script* script);
