// The network manager's tasks (tasks::Task<online::Hub>: the manager at the task's +0x1C) and its GameSpy query
// callbacks. See gamenet_handlers.h.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <array>

#include "engine/core/dbg.h"
#include "platform/gamespy.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/net/net_metrics.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kText = 0xC4745838;                  // text
constexpr uint32_t kRedialPause = 0x645456E7;           // net_modem_state_redial_pause
constexpr uint32_t kConnecting = 0x23E04F62;            // net_modem_state_conencting
constexpr uint32_t kDialing = 0x02F1330D;               // net_modem_state_dialing
constexpr uint32_t kConnected = 0x685FBA50;             // net_modem_state_connected
constexpr uint32_t kAuthDone = 0xE0DE5630;              // net_modem_state_logged_in
constexpr uint32_t kDisconnecting = 0x8B0A8838;         // net_modem_state_disconnecting
constexpr uint32_t kHangingUp = 0x2967A529;             // net_modem_state_hanging_up
constexpr uint32_t kDisconnected = 0xF850742F;          // net_modem_state_disconnected
constexpr uint32_t kStateDialog = 0xA37134DC;           // create_modem_state_dialog
// How long the modem dialog waits before it dials again.
constexpr uint32_t kRedialPauseTime = 5000; // ms
constexpr uint32_t kStatusDialog = 0x40766B63;          // create_modem_status_dialog
constexpr uint32_t kFinalStateDialog = 0x551ABB16;      // create_modem_final_state_dialog
constexpr uint32_t kUpdateNetMetrics = 0xC244B02B;      // update_net_metrics
constexpr uint32_t kShowJoinTimeoutNotice = 0x3F7009B4; // ShowJoinTimeoutNotice

constexpr uint32_t kJoinTimeout = 60000; // ms

Hub* task_manager(tasks::Task<Hub>* task) { return task->content(); }

// The connection's line of the network metrics: "(n) Lag: ... Resends: ... Bps: <in> <out>".
void show_connection_metrics(net::Conn* conn, int index)
{
    const int lag = conn->is_host_local() ? 0 : conn->ping_probe().average_latency();
    const int sent = conn->send_metrics().get_throughput();
    const int received = conn->receive_metrics().get_throughput();
    const int resends = conn->get_highest_resend_count();
    constexpr size_t kLineSize = 1024;
    std::array<char, kLineSize> line{};
    text::format_to(line, "(%d) Lag: %d Resends: %d Bps: %d %d", index, lag, resends, received, sent);
    const script::StructPtr params = script::make_struct();
    params->append_word(kText, line.data());
    get_run(kUpdateNetMetrics, params.get());
}

// Each connection of `app` that isn't local.
void show_app_metrics(net::AppHost* app)
{
    net::ConnSearch search;
    int index = 0;
    for (net::Conn* conn = app->begin_conn_walk(&search); conn; conn = app->step_conn_walk(&search)) {
        if (conn->is_host_local())
            continue;
        show_connection_metrics(conn, index);
        ++index;
    }
}

} // namespace

// 0x0052F4A0
void level_change_task(tasks::Task<Hub>* task)
{
    PlayerSearch search;
    Hub* manager = task_manager(task);
    if (frame_clock::get_time() < net_game_state().level_change_time())
        return;
    get_skate()->change_level(manager->level()); // the level changed
    if (PlayerRecord* king = is_crown_rule_active(manager))
        king->give_crown(false);
    for (PlayerRecord* p = start_roster_sweep(manager, &search, true); p; p = step_roster_sweep(manager, &search, true))
        p->clear_flags(); // carries no flag
    manager->level_change_job()->drop_out();
}

namespace {
uint32_t& modem_dialog_end()
{
    static uint32_t modem_dialog_end{};
    return modem_dialog_end;
}
} // namespace

// 0x00533810
void dialup_progress_tick(tasks::Task<Hub>* task)
{
    Hub* manager = task_manager(task);
    net::Hub* net = net::Hub::sole();
    const int state = net->get_dialup_status();
    int& shown = manager->modem_state_shown(); // the state shown last
    if (state == shown && state != net::kDialupStatusRedialWaiting) {
        shown = state;
        return;
    }
    gsl::owner<script::Struct*> params = script::new_struct();
    constexpr size_t kLineSize = 128;
    std::array<char, kLineSize> line{};
    constexpr size_t kLine2Size = 128;
    std::array<char, kLine2Size> line2{};
    switch (state) {
    case 0: // connecting / dialing
        if (net->get_net_link_kind() == 3)
            text::format_to(line, "%s", script::fetch_text(kConnecting));
        else
            text::format_to(line, "%s %s", script::fetch_text(kDialing), net->get_dialup_number());
        params->append_word(kText, script::QbValueKind::kString, line.data());
        get_run(kStateDialog, params);
        break;
    case 1:
        params->append_word(kText, script::QbValueKind::kString, script::fetch_text(kConnected));
        get_run(kStateDialog, params);
        break;
    case 2: // logged in (with the speed on a modem)
        if (net->get_net_link_kind() == 1)
            text::format_to(line2, "%s %d bps", script::fetch_text(kAuthDone), net->get_dialup_speed());
        else
            text::format_to(line2, "%s", script::fetch_text(kAuthDone));
        params->append_word(kText, script::QbValueKind::kString, line2.data());
        get_run(kFinalStateDialog, params);
        break;
    case 3:
    case 4:
        params->append_word(kText, script::QbValueKind::kString, script::fetch_text(state == 3 ? kDisconnecting : kHangingUp));
        get_run(kStatusDialog, params);
        break;
    case net::kDialupStatusDisconnected:
        params->append_word(kText, script::QbValueKind::kString, script::fetch_text(kDisconnected));
        get_run(kFinalStateDialog, params);
        break;
    case net::kDialupStatusRedialPause: // redial in five seconds
        text::format_to(line, "%s", script::fetch_text(kRedialPause));
        params->append_word(kText, script::QbValueKind::kString, line.data());
        get_run(kStateDialog, params);
        modem_dialog_end() = frame_clock::get_time() + kRedialPauseTime;
        net->set_dialup_status(net::kDialupStatusRedialWaiting);
        break;
    case net::kDialupStatusRedialWaiting:
        if (frame_clock::get_time() > modem_dialog_end()) {
            shown = -1;
            net->connect_to_internet();
            return; // (the parameters never deleted: the original's)
        }
        break;
    default: // (6: nothing)
        break;
    }
    script::free_struct(params);
    shown = state;
}

// 0x00533B50 (each list is shown twice: the original's loop)
void net_metrics_task(tasks::Task<Hub>* task)
{
    const Hub* manager = task_manager(task);
    for (int pass = 0; pass < 2; ++pass) {
        if (net::AppHost* client = manager->client_app(); client && !client->is_host_local())
            show_app_metrics(client);
        if (net::AppHost* server = manager->host())
            show_app_metrics(server);
    }
}

// 0x00533ED0
void join_deadline_tick(tasks::Task<Hub>* task)
{
    const Hub* manager = task_manager(task);
    if (manager->join_phase() == EntryPhase::kOver ||
        frame_clock::get_time() - static_cast<uint32_t>(manager->join_began_at()) <= kJoinTimeout)
        return;
    manager->join_phase_job()->drop_out();
    get_run(kShowJoinTimeoutNotice);
}

// 0x00533400
void player_team_key_callback(int /*unused*/, int /*unused*/, void* buffer, void* /*unused*/)
{
    game_spy::call_qr2_buffer_add(buffer, "");
}

// 0x00533420
void key_list_callback(int type, void* keys, void* /*unused*/)
{
    auto add = [keys](int key) { game_spy::qr2_key_buffer_add(keys, key); };
    if (type == 0) { // the server's
        for (const int key : {1, 2, 4, 5, 6, 8, 0x14, 0xA, 0xB, 0x13})
            add(key);
    } else if (type == 1) { // the players'
        for (const int key : {0x15, 0x18, 0x19})
            add(key);
    }
}

// 0x005334B0
void add_error_callback(int error, const char* message, void* /*unused*/)
{
    debug::printf("Error adding server: %d, %s\n", error, message);
}

} // namespace online
