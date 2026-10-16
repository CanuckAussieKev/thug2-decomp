#pragma once

#include <cstdint>

namespace Net {

// 0x00545880: the network manager's online service task (a Tsk::Task callback; the manager is the task's data,
// +0x1C). A state machine in the manager (+0x1B8): the GameSpy availability check, the news and the message of
// the day downloaded by two threads, and the lobby list check. The PC port's "MOTD fix" is built in: the news
// download gives up after 500 ms (not 15 s) and then runs a different script with the "news ok" flag set.
void __cdecl ServiceTask(const uint8_t* task);

} // namespace Net
