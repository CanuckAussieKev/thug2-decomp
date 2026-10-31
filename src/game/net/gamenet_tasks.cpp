// The network manager's tasks (TaskList::Task<OnlinePlay::Hub>: the manager at the task's +0x1C) and its GameSpy query
// callbacks. See gamenet_handlers.h.

#include <cstdint>
#include <initializer_list>
#include "game/core/code_address.h"
#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/net/gamespy.h"
#include "game/core/task.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/net/net_metrics.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace OnlinePlay {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kText = 0xC4745838;                // text
constexpr uint32_t kRedialPause = 0x645456E7;         // net_modem_state_redial_pause
constexpr uint32_t kConnecting = 0x23E04F62;          // net_modem_state_conencting
constexpr uint32_t kDialing = 0x02F1330D;             // net_modem_state_dialing
constexpr uint32_t kConnected = 0x685FBA50;           // net_modem_state_connected
constexpr uint32_t kLoggedIn = 0xE0DE5630;            // net_modem_state_logged_in
constexpr uint32_t kDisconnecting = 0x8B0A8838;       // net_modem_state_disconnecting
constexpr uint32_t kHangingUp = 0x2967A529;           // net_modem_state_hanging_up
constexpr uint32_t kDisconnected = 0xF850742F;        // net_modem_state_disconnected
constexpr uint32_t kStateDialog = 0xA37134DC;         // create_modem_state_dialog
constexpr uint32_t kStatusDialog = 0x40766B63;        // create_modem_status_dialog
constexpr uint32_t kFinalStateDialog = 0x551ABB16;    // create_modem_final_state_dialog
constexpr uint32_t kUpdateNetMetrics = 0xC244B02B;    // update_net_metrics
constexpr uint32_t kShowJoinTimeoutNotice = 0x3F7009B4; // ShowJoinTimeoutNotice

constexpr uint32_t kJoinTimeout = 60000; // ms

Hub* TaskManager(void* task) { return Prop<Hub*>(task, 0x1C); }

// The connection's line of the network metrics: "(n) Lag: ... Resends: ... Bps: <in> <out>".
void ShowConnectionMetrics(NetLink::Conn* conn, int index)
{
    int lag = conn->IsHostLocal() ? 0 : conn->m_latencyTest.m_averageLatency;
    int sent = conn->m_sendMetrics.GetThroughput();
    int received = conn->m_receiveMetrics.GetThroughput();
    int resends = conn->GetHighestResendCount();
    char line[1024];
    std::sprintf(line, "(%d) Lag: %d Resends: %d Bps: %d %d", index, lag, resends, received, sent);
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kText, line);
    Run(kUpdateNetMetrics, params);
    DeleteParams(params);
}

// Each connection of `app` that isn't local.
void ShowAppMetrics(NetLink::AppHost* app)
{
    NetLink::ConnSearch search;
    search.m_vtable = NetLink::kConnSearchVtable;
    search.m_listLink = nullptr;
    int index = 0;
    for (NetLink::Conn* conn = app->BeginConnWalk(&search); conn; conn = app->StepConnWalk(&search)) {
        if (conn->IsHostLocal())
            continue;
        ShowConnectionMetrics(conn, index);
        ++index;
    }
}

} // namespace

// 0x0052F4A0
void __cdecl LevelChangeTask(void* task)
{
    PlayerSearch search;
    Hub* manager = TaskManager(task);
    if (FrameClock::GetTime() < g_levelChangeTime)
        return;
    Call(CodeAddress(&GameModules::Skate::ChangeLevel), Skate(), manager->m_level); // the level changed
    if (uint8_t* king = KingOfTheHill(manager))
        AsPlayer(king)->GiveCrown(false);
    for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true))
        AsPlayer(p)->ClearFlags(); // carries no flag
    Prop<TaskList::SchedulerTask*>(manager, 0xEC)->DropOut();
}

uint32_t g_modemDialogEnd;

// 0x00533810
void __cdecl ModemStateTask(void* task)
{
    Hub* manager = TaskManager(task);
    NetLink::Hub* net = NetLink::Hub::Sole();
    int state = net->GetDialupStatus();
    int& shown = Prop<int>(manager, 0x1A8); // the state shown last
    if (state == shown && state != 8) {
        shown = state;
        return;
    }
    Scripts::CStruct* params = NewParams();
    char line[128];
    char line2[128];
    switch (state) {
    case 0: // connecting / dialing
        if (net->GetNetLinkKind() == 3)
            std::sprintf(line, "%s", Scripts::FetchText(kConnecting));
        else
            std::sprintf(line, "%s %s", Scripts::FetchText(kDialing), net->GetDialupNumber());
        params->AppendWord(kText, Scripts::QBVAL_STRING, line);
        Run(kStateDialog, params);
        break;
    case 1:
        params->AppendWord(kText, Scripts::QBVAL_STRING, Scripts::FetchText(kConnected));
        Run(kStateDialog, params);
        break;
    case 2: // logged in (with the speed on a modem)
        if (net->GetNetLinkKind() == 1)
            std::sprintf(line2, "%s %d bps", Scripts::FetchText(kLoggedIn), net->GetDialupSpeed());
        else
            std::sprintf(line2, "%s", Scripts::FetchText(kLoggedIn));
        params->AppendWord(kText, Scripts::QBVAL_STRING, line2);
        Run(kFinalStateDialog, params);
        break;
    case 3:
    case 4:
        params->AppendWord(kText, Scripts::QBVAL_STRING, Scripts::FetchText(state == 3 ? kDisconnecting : kHangingUp));
        Run(kStatusDialog, params);
        break;
    case 5:
        params->AppendWord(kText, Scripts::QBVAL_STRING, Scripts::FetchText(kDisconnected));
        Run(kFinalStateDialog, params);
        break;
    case 7: // redial in five seconds
        std::sprintf(line, "%s", Scripts::FetchText(kRedialPause));
        params->AppendWord(kText, Scripts::QBVAL_STRING, line);
        Run(kStateDialog, params);
        g_modemDialogEnd = FrameClock::GetTime() + 5000;
        net->SetDialupStatus(8);
        break;
    case 8:
        if (FrameClock::GetTime() > g_modemDialogEnd) {
            shown = -1;
            net->ConnectToInternet();
            return; // (the parameters never deleted: the original's)
        }
        break;
    default: // (6: nothing)
        break;
    }
    DeleteParams(params);
    shown = state;
}

// 0x00533B50 (each list is shown twice: the original's loop)
void __cdecl NetMetricsTask(void* task)
{
    Hub* manager = TaskManager(task);
    for (int pass = 0; pass < 2; ++pass) {
        if (NetLink::AppHost* client = manager->m_clientApp; client && !client->IsHostLocal())
            ShowAppMetrics(client);
        if (NetLink::AppHost* server = manager->m_host)
            ShowAppMetrics(server);
    }
}

// 0x00533ED0
void __cdecl JoinTimeoutTask(void* task)
{
    Hub* manager = TaskManager(task);
    if (manager->m_joinState == 8 || FrameClock::GetTime() - static_cast<uint32_t>(manager->m_joinStartTime) <= kJoinTimeout)
        return;
    manager->m_joinStateTask->DropOut();
    Run(kShowJoinTimeoutNotice);
}

// 0x00533400
void __cdecl PlayerTeamKeyCallback(int, int, void* buffer, void*)
{
    GameSpy::Qr2BufferAdd(buffer, "");
}

// 0x00533420
void __cdecl KeyListCallback(int type, void* keys, void*)
{
    auto add = [keys](int key) { GameSpy::Qr2KeyBufferAdd(keys, key); };
    if (type == 0) { // the server's
        for (int key : {1, 2, 4, 5, 6, 8, 0x14, 0xA, 0xB, 0x13})
            add(key);
    } else if (type == 1) { // the players'
        for (int key : {0x15, 0x18, 0x19})
            add(key);
    }
}

// 0x005334B0
void __cdecl AddErrorCallback(int error, const char* message, void*)
{
    DebugOut::Printf("Error adding server: %d, %s\n", error, message);
}

} // namespace OnlinePlay
