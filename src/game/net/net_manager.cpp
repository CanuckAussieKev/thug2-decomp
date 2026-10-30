// The network library's manager (Net::Manager): its settings getters and setters. See net_manager.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname, inet_ntoa: what the game uses

#include "game/net/net_manager.h"

#include <winsock2.h>

#include <cstring>

#include "game/core/task.h"
#include "game/net/net_app.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/ass/assman.h"

namespace Net {

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

// Adds a task to the main loop's logic tasks (Mlp::Manager +0x04).
void AddLogicTask(uint8_t* mlp, void* task)
{
    reinterpret_cast<Mlp::Manager*>(mlp)->m_logicTasks.AddTask(static_cast<Tsk::BaseTask*>(task));
}

} // namespace

Manager* Manager::Instance() { return g_netManager; }

// 0x00454580
bool Manager::ConnectToInternet()
{
    m_online = true;
    return true;
}

// 0x00454590
bool Manager::DisconnectFromInternet()
{
    m_online = false;
    return true;
}

// 0x004545D0
void Manager::SetDialupNumber(int index, const char* number) { CopyString(m_dialupNumbers[index], number); }

// 0x00454600
void Manager::SetDialupUserName(const char* name) { CopyString(m_dialupUserName, name); }

// 0x00454620
void Manager::SetDialupPassword(const char* password) { CopyString(m_dialupPassword, password); }

// 0x00454640
void Manager::SetGateway(const char* ip) { CopyIfDifferent(m_gateway, ip); }

// 0x004546A0
void Manager::SetSubnetMask(const char* ip) { CopyIfDifferent(m_subnetMask, ip); }

// 0x00454710
void Manager::SetLocalIP(const char* ip) { CopyIfDifferent(m_localIP, ip); }

// 0x00454770
void Manager::SetConnectionType(int type)
{
    if (m_connectionType == type)
        return;
    m_connectionType = type;
    switch (type) {
    case kConnectionModem:
        m_bandwidth = 4200;
        break;
    case kConnection2:
    case kConnection4:
        m_bandwidth = 400000;
        break;
    case kConnection3:
        m_bandwidth = 300000;
        break;
    default: // (the bandwidth stays)
        break;
    }
}

// 0x004547D0
void Manager::SetDeviceType(int type)
{
    if (m_deviceType != type)
        m_deviceType = type;
}

// 0x004547F0
void Manager::SetDHCP(bool useDHCP)
{
    if (m_useDHCP != useDHCP)
        m_useDHCP = useDHCP;
}

// 0x00454820
void Manager::SetDNSServer(int index, const char* ip) { CopyIfDifferent(m_dnsServers[index], ip); }

// 0x00454890
void Manager::SetHostName(const char* name)
{
    if (!name)
        m_hostName[0] = '\0'; // (and the copy below still reads from the null pointer, as in the original)
    CopyString(m_hostName, name);
}

// 0x004548C0
void Manager::SetDomainName(const char* name)
{
    if (!name)
        m_domainName[0] = '\0'; // (likewise)
    CopyString(m_domainName, name);
}

// 0x004548F0
uint32_t Manager::GetLocalAddress()
{
    if (m_localAddress)
        return m_localAddress;
    return inet_addr(m_localIP);
}

// 0x00454910
const char* Manager::GetLocalIP()
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
bool Manager::LoadNetworkDrivers(const char* /*ip*/, const char* /*subnetMask*/, const char* /*gateway*/)
{
    return true;
}

// 0x00454CB0
void Manager::AddAppTasks(App* app)
{
    uint8_t* mlp = reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager); // the Mlp::Manager
    AddLogicTask(mlp, app->ReceiveTask());
    AddLogicTask(mlp, app->SendTask());
    AddLogicTask(mlp, app->ProcessTask());
    AddLogicTask(mlp, app->MetricsTask());
}

// 0x00454DE0
const char* Manager::GetDialupNumber()
{
    if (m_connectionType == kConnection3)
        return "";
    return m_dialupNumbers[m_dialupNumberIndex];
}

} // namespace Net

REPLACE(0x00454580, &Net::Manager::ConnectToInternet);
REPLACE(0x00454590, &Net::Manager::DisconnectFromInternet);
REPLACE(0x004545A0, &Net::Manager::GetModemBaudRate);
REPLACE(0x004545B0, &Net::Manager::GetBandwidth);
REPLACE(0x004545C0, &Net::Manager::IsOnline);
REPLACE(0x004545D0, &Net::Manager::SetDialupNumber);
REPLACE(0x00454600, &Net::Manager::SetDialupUserName);
REPLACE(0x00454620, &Net::Manager::SetDialupPassword);
REPLACE(0x00454640, &Net::Manager::SetGateway);
REPLACE(0x004546A0, &Net::Manager::SetSubnetMask);
REPLACE(0x00454700, &Net::Manager::SetLocalAddress);
REPLACE(0x00454710, &Net::Manager::SetLocalIP);
REPLACE(0x00454770, &Net::Manager::SetConnectionType);
REPLACE(0x004547D0, &Net::Manager::SetDeviceType);
REPLACE(0x004547F0, &Net::Manager::SetDHCP);
REPLACE(0x00454810, &Net::Manager::SetFlag2E6);
REPLACE(0x00454820, &Net::Manager::SetDNSServer);
REPLACE(0x00454890, &Net::Manager::SetHostName);
REPLACE(0x004548C0, &Net::Manager::SetDomainName);
REPLACE(0x004548F0, &Net::Manager::GetLocalAddress);
REPLACE(0x00454910, &Net::Manager::GetLocalIP);
REPLACE(0x00454960, &Net::Manager::GetConnectionType);
REPLACE(0x00454970, &Net::Manager::GetLastError);
REPLACE(0x00454980, &Net::Manager::GetMessageFlags);
REPLACE(0x00454990, &Net::Manager::SetMessageFlags);
REPLACE(0x004549A0, &Net::Manager::GetModemState);
REPLACE(0x004549B0, &Net::Manager::SetModemState);
REPLACE(0x004549C0, &Net::Manager::LoadNetworkDrivers);
REPLACE(0x00454CB0, &Net::Manager::AddAppTasks);
REPLACE(0x00454DE0, &Net::Manager::GetDialupNumber);
