#pragma once

#include <cstdint>

namespace online {
// The online service's states (Hub::service_state; service_task steps through them).
enum class ServiceState : uint8_t {
    kWaiting = 0, // until the wake-up time, then the next state
    kWake = 1, // queues the service task
    kStopped = 2, // the task dropped out
    kStart = 3, // a script, then the availability check a moment later
    kAvailabilityStart = 4,
    kAvailabilityAnswer = 5,
    kNewsMessage = 6, // also DownloadMotd's
    kNewsDownload = 7,
    kMotdStart = 8,
    kMotdShow = 9,
    kNewsMessageRetry = 0xA,
    kNewsEnded = 0xB,
    kMotdDownload = 0xC,
    kLobbyListStart = 0xD,
    kLobbyListIdle = 0xE, // (the server list's state)
    kLobbyListCheck = 0xF,
    kLobbyListEmpty = 0x10,
    kLobbyListShow = 0x11,
};
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
