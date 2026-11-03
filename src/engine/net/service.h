#pragma once

#include <cstdint>

namespace online {
// The online service's states (Hub::service_state; service_task steps through them).
constexpr int kServiceStateWaiting = 0; // until the wake-up time, then the next state
constexpr int kServiceStateWake = 1;    // queues the service task
constexpr int kServiceStateStopped = 2; // the task dropped out
constexpr int kServiceStateStart = 3;   // a script, then the availability check a moment later
constexpr int kServiceStateAvailabilityStart = 4;
constexpr int kServiceStateAvailabilityAnswer = 5;
constexpr int kServiceStateNewsMessage = 6; // also DownloadMotd's
constexpr int kServiceStateNewsDownload = 7;
constexpr int kServiceStateMotdStart = 8;
constexpr int kServiceStateMotdShow = 9;
constexpr int kServiceStateNewsMessageRetry = 0xA;
constexpr int kServiceStateNewsEnded = 0xB;
constexpr int kServiceStateMotdDownload = 0xC;
constexpr int kServiceStateLobbyListStart = 0xD;
constexpr int kServiceStateLobbyListIdle = 0xE; // (the server list's state)
constexpr int kServiceStateLobbyListCheck = 0xF;
constexpr int kServiceStateLobbyListEmpty = 0x10;
constexpr int kServiceStateLobbyListShow = 0x11;
} // namespace online

namespace online {
class Hub;
}
namespace tasks {
template <typename Owner>
class Task;
} // namespace tasks

namespace net {

// 0x00545880: the network manager's online service task (a tasks::Task callback; the manager is the task's data,
// +0x1C). A state machine in the manager (+0x1B8): the GameSpy availability check, the news and the message of
// the day downloaded by two threads, and the lobby list check. PartyMod's "MOTD fix" is built in: the news
// download gives up after 500 ms (not 15 s) and then runs a different script with the "news ok" flag set.
void service_task(tasks::Task<online::Hub>* task);

} // namespace net
