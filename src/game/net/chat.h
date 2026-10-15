#pragma once

namespace GameNet {

class Manager; // the network game manager (not decompiled yet; one instance, see GetManager)

Manager* GetManager();

// Send a chat line to the other players and show it locally, as the local player.
void SendChatMessage(Manager* manager, const char* text);

// Send a chat line under another name - every player sees "<name> : <text>". Other players need nothing
// special to see it: it's an ordinary chat message whose sender name is `name` (up to 15 characters).
void SendNamedChatMessage(Manager* manager, const char* name, const char* text);

// Add a line to this player's chat display only (nothing is sent). Text can use \cN colour codes.
void ShowLocalChatLine(const char* line);

// Chat commands typed into the chat box (lines starting with '/'). Returns true if `text` was a command
// and has been handled, in which case it isn't sent as chat.
//   /hello   - sends "Hello, World!" in two colours as a message from "Server"
//   /colours - shows (locally) each \cN colour code in its own colour
bool HandleChatCommand(Manager* manager, const char* text);

} // namespace GameNet
