// The network library's manager (NetLink::Hub): its settings getters and setters. See net_manager.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname, inet_ntoa: what the game uses

#include "game/net/net_manager.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>

#include "game/core/task_list.h"
#include "game/net/net_app.h"
#include "game/sys/mlp.h"
#include "game/assets/resource_registry.h"

namespace NetLink {

namespace {

// strcpy: the settings are copied with no length limit.
void CopyString(char* out, const char* source)
{
    while ((*out++ = *source++) != '\0') {
    }
}

// The setters that copy only a different string.
void CopyIfDifferent(char* out, const char* source)
{
    if (std::strcmp(source, out) != 0)
        CopyString(out, source);
}

// Adds a task to the main loop's logic tasks (FrameLoop::Hub +0x04).
void RegisterLogicTask(uint8_t* mlp, void* task)
{
    reinterpret_cast<FrameLoop::Hub*>(mlp)->m_logicTasks.QueueJob(static_cast<TaskList::SchedulerTask*>(task));
}

} // namespace

Hub* Hub::Sole() { return g_netCore; }

// 0x00454580
bool Hub::ConnectToInternet()
{
    m_isNetworked = true;
    return true;
}

// 0x00454590
bool Hub::DisconnectFromInternet()
{
    m_isNetworked = false;
    return true;
}

// 0x004545D0
void Hub::SetDialupNumber(int index, const char* number) { CopyString(m_dialupNumbers[index], number); }

// 0x00454600
void Hub::SetDialupUserName(const char* name) { CopyString(m_dialupUserName, name); }

// 0x00454620
void Hub::SetDialupPassword(const char* password) { CopyString(m_dialupPassword, password); }

// 0x00454640
void Hub::SetRouterAddr(const char* ip) { CopyIfDifferent(m_routerAddr, ip); }

// 0x004546A0
void Hub::SetAddressMask(const char* ip) { CopyIfDifferent(m_subnetMask, ip); }

// 0x00454710
void Hub::SetOwnAddress(const char* ip) { CopyIfDifferent(m_localIP, ip); }

// 0x00454770
void Hub::SetNetLinkKind(int type)
{
    if (m_connectionType == type)
        return;
    m_connectionType = type;
    switch (type) {
    case kConnectionModem:
        m_linkSpeed = 4200;
        break;
    case kConnection2:
    case kConnection4:
        m_linkSpeed = 400000;
        break;
    case kConnection3:
        m_linkSpeed = 300000;
        break;
    default: // (the bandwidth stays)
        break;
    }
}

// 0x004547D0
void Hub::SetAdapterKind(int type)
{
    if (m_deviceType != type)
        m_deviceType = type;
}

// 0x004547F0
void Hub::SetAutoAddress(bool useDHCP)
{
    if (m_useDHCP != useDHCP)
        m_useDHCP = useDHCP;
}

// 0x00454820
void Hub::SetNameResolver(int index, const char* ip) { CopyIfDifferent(m_dnsServers[index], ip); }

// 0x00454890
void Hub::SetMachineLabel(const char* name)
{
    if (!name)
        m_hostName[0] = '\0'; // (and the copy below still reads from the null pointer, as in the original)
    CopyString(m_hostName, name);
}

// 0x004548C0
void Hub::SetNetworkDomain(const char* name)
{
    if (!name)
        m_domainName[0] = '\0'; // (likewise)
    CopyString(m_domainName, name);
}

// 0x004548F0
uint32_t Hub::GetLocalAddress()
{
    if (m_localAddress)
        return m_localAddress;
    return inet_addr(m_localIP);
}

// 0x00454910
const char* Hub::FetchOwnAddress()
{
    hostent* localhost = gethostbyname("localhost");
    if (!localhost)
        return "<Unknown>";
    hostent* host = gethostbyname(localhost->h_name);
    // (Without the host, localhost's address - read from Winsock's per-thread hostent after the second lookup
    // has used it, as the original does.)
    if (!host)
        host = localhost;
    return inet_ntoa(*reinterpret_cast<in_addr*>(host->h_addr_list[0]));
}

// 0x004549C0
bool Hub::LoadNetworkDrivers(const char* /*ip*/, const char* /*subnetMask*/, const char* /*gateway*/)
{
    return true;
}

// 0x00454CB0
void Hub::AddAppTasks(AppHost* app)
{
    uint8_t* mlp = reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager); // the FrameLoop::Hub
    RegisterLogicTask(mlp, app->ReceiveTask());
    RegisterLogicTask(mlp, app->SendTask());
    RegisterLogicTask(mlp, app->ProcessTask());
    RegisterLogicTask(mlp, app->MetricsTask());
}

// 0x00454DE0
const char* Hub::GetDialupNumber()
{
    if (m_connectionType == kConnection3)
        return "";
    return m_dialupNumbers[m_dialupNumberIndex];
}

} // namespace Net
