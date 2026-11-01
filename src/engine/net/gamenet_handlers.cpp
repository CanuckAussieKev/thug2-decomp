// The network manager's message handlers (online::Hub) and their helpers. See gamenet_handlers.h.

#include "engine/net/gamenet_handlers.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "game/park_editing/park_editor.h"
#include "engine/net/gamenet_game_cfuncs.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/stats_man.h"
#include "engine/objects/entity.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "game/skate/skate_net.h"
#include "engine/util/bitstream.h"
#include "engine/script/game_cfuncs.h"
#include "game/park_editing/rail_editor.h"
#include "engine/core/code_address.h"
#include "engine/assets/resource_registry.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/sys/mlp.h"
#include "engine/net/gamenet_messages.h"
#include "engine/util/byte_stream.h"

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kLaunchAutoServerNotification = 0x6553898E; // launch_auto_server_notification
constexpr uint32_t kKillTeamFlags = 0xEBB3642E;                // Kill_Team_Flags
constexpr uint32_t kProfilePassword = 0x3357D89B;              // profile_password
constexpr uint32_t kUiString = 0x96875C0F;                     // ui_string
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;                // netgoalattack
constexpr uint32_t kCreateRankings = 0xFCE4F72D;               // create_rankings
constexpr uint32_t kCreateGameEndedDialog = 0x21ECD114;        // create_game_ended_dialog
constexpr uint32_t kChooseTeamMessage = 0x751F873A;            // ChooseTeamMessage
constexpr uint32_t kPrepareSkaterForMove = 0xF1B110ED;         // PrepareSkaterForMove

// Messages.
constexpr uint8_t kMsgReadyToReceive = 0x54;
constexpr uint8_t kMsgCdKeyAnswer = 0x7D;
constexpr uint8_t kMsgDigest = 0x81;
constexpr uint8_t kMsgRequestRails = 0x8B;
constexpr uint8_t kMsgRequestGoals = 0x8A;
constexpr uint8_t kMsgGoalsData = 0x87;
constexpr uint8_t kMsgRailsData = 0x88;

constexpr int kCdKeyAnswerSize = 0x21;
constexpr uint16_t kRailDataSize = 0x1132;
constexpr int kParkMapSize = 15000;

} // namespace

// 0x0052D6A0
void __cdecl get_cd_key(char* key)
{
    static const char kPath[] = "Software\\Activision\\Tony Hawk's Underground 2";
    char value[100] = {};
    DWORD size = sizeof(value);
    DWORD type;
    HKEY handle;
    auto query = [&] { RegQueryValueExA(handle, "key", nullptr, &type, reinterpret_cast<BYTE*>(value), &size); };
    bool opened;
    if (RegCreateKeyA(HKEY_CURRENT_USER, kPath, &handle) == ERROR_SUCCESS &&
        RegQueryValueExA(handle, "key", nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS) {
        opened = true;
    } else {
        // (an opened HKEY_CURRENT_USER key without the value is left open: the original's)
        opened = RegCreateKeyA(HKEY_LOCAL_MACHINE, kPath, &handle) == ERROR_SUCCESS;
        if (opened)
            query();
    }
    if (opened)
        RegCloseKey(handle);
    // (a value filling all 100 bytes has no terminator: the original's strcpy runs on into its stack - here it stops)
    size_t length = strnlen(value, sizeof(value));
    std::memcpy(key, value, length);
    key[length] = '\0';
}

// 0x0052D7B0
int __cdecl on_ready_ask_msg(net::InboundMsgCtx* context)
{
    manager_of(context)->last_ready_probe_id_ = message_word(context, 0);
    return kContinue;
}

// 0x0052D7D0
int __cdecl on_ready_reply_msg(net::InboundMsgCtx* context)
{
    context->acknowledge_ = true;
    if (PlayerRecord* player = find_player_by_sender(manager_of(context), context->sender_conn_))
        player->receive_ready_response(message_word(context, 0));
    return kContinue;
}

// 0x0052D800
int __cdecl on_spectate_msg(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    cfunc_exit_surveyor_mode(nullptr, nullptr); // (the script command, without parameters)
    manager->enter_observe_mode();
    return kContinue;
}

// 0x0052D830
int __cdecl handle_flush_important(net::InboundMsgCtx* context)
{
    if (context->sender_conn_->is_off_machine())
        context->sender_conn_->destroy_important_messages();
    return kContinue;
}

namespace {
// msg[0] the player's object id, msg[1] the team (both signed bytes).
int flag_handler(net::InboundMsgCtx* context, void (PlayerRecord::*method)(int))
{
    const uint8_t* msg = get_message(context);
    if (PlayerRecord* player = find_player_slot_by_obj(manager_of(context), msg))
        (player->*method)(static_cast<int>(static_cast<int8_t>(msg[1])));
    return kContinue;
}
} // namespace

// 0x0052D850 / 0x0052D880 / 0x0052D8B0
int __cdecl on_flag_swiped_msg(net::InboundMsgCtx* context)
{
    return flag_handler(context, &PlayerRecord::note_flag_steal);
}
int __cdecl on_flag_grabbed_msg(net::InboundMsgCtx* context)
{
    return flag_handler(context, &PlayerRecord::note_flag_pickup);
}
int __cdecl on_flag_scored_msg(net::InboundMsgCtx* context)
{
    return flag_handler(context, &PlayerRecord::score_flag_capture);
}

// 0x0052D8E0
int __cdecl on_flag_returned_msg(net::InboundMsgCtx* context)
{
    if (PlayerRecord* player = find_player_slot_by_obj(manager_of(context), get_message(context)))
        player->note_flag_returned();
    return kContinue;
}

// 0x0052D910 / 0x0052D930
int __cdecl handle_auto_server_notice(net::InboundMsgCtx*)
{
    get_run(kLaunchAutoServerNotification);
    return kContinue;
}
int __cdecl handle_kill_team_flags(net::InboundMsgCtx*)
{
    get_run(kKillTeamFlags);
    return kContinue;
}

// 0x0052D950
int __cdecl on_hud_notice_msg(net::InboundMsgCtx* context)
{
    // (the message: gamenet_messages.h; NOT ORIGINAL: one too short is dropped and its strings end inside their
    // fields - the original read them up to a terminator that may not be there)
    util::ByteReader reader(context->body_, context->body_size_);
    HudNotice notice;
    if (!read_hud_notice(reader, &notice))
        return kDone;
    // 0x005376F0: (broadcast, text, string 1, string 2, object, properties, important, time)
    manager_of(context)->build_net_notice(false, notice.text, notice.string1, notice.string2, nullptr, nullptr, false,
                                          notice.time);
    return kContinue;
}

// 0x0052D980
int __cdecl handle_object_update_length(net::InboundMsgCtx* context)
{
    util::PackedBitBuffer stream;
    stream.change_intake_contents(context->body_, 0x400);
    uint32_t present = stream.read_bits(8) & 0xFF;
    for (int skater = 0; skater < 8; ++skater) {
        if (!(present & (1u << skater)))
            continue;
        stream.read_bits(16); // the time
        uint32_t flags = stream.read_bits(9);
        for (int field = 0; field < 6; ++field) { // the position and the rotation
            if (flags & (1u << field))
                stream.read_signed_bits(16);
        }
        if (flags & 0x40) {
            stream.read_bits(4);
            stream.read_bits(6);
            stream.read_bits(1);
            stream.read_bits(1);
        }
        if (flags & 0x80)
            stream.read_bits(8);
        if (flags & 0x100)
            stream.read_signed_bits(32);
    }
    context->body_size_ = stream.get_byte_count();
    return kContinue;
}

// 0x0052DAC0
int __cdecl handle_nothing(net::InboundMsgCtx*) { return kContinue; }

// 0x0052DAD0
int __cdecl handle_cd_key_challenge(net::InboundMsgCtx* context)
{
    net::NetMessageInfo desc;
    Hub* manager = manager_of(context);
    const char* password = manager->get_online_settings()->get_preference_string(kProfilePassword, kUiString);
    char answer[36];
    // 0x0054CA10: the answer to the challenge (the message itself). (Static: the original passes the online service,
    // +0x1C, in ECX, unused.)
    RankingsManager::generate_auth(context->body_, password, answer);
    desc.ident_ = kMsgCdKeyAnswer;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    desc.content_ = answer;
    desc.byte_count_ = kCdKeyAnswerSize;
    send_to_server(context->receiver_app_, &desc);
    return kContinue;
}

// 0x0052DCA0
int __cdecl handle_keep_alive(net::InboundMsgCtx* context)
{
    context->sender_conn_->stamp_last_heard(static_cast<int8_t>(get_message(context)[0]) * 1000);
    return kContinue;
}

// 0x0052DCC0
int __cdecl on_admitted_msg(net::InboundMsgCtx* context)
{
    uint8_t* skate = get_skate();
    net::NetMessageInfo desc;
    Hub* manager = manager_of(context);
    manager->set_entry_phase(6);
    desc.ident_ = kMsgReadyToReceive;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_server(context->receiver_app_, &desc);
    if (!manager->get_on_server()) {
        for (int i = 0; i < 2; ++i) {
            if (net::AppHost* client = manager->fetch_patron(i))
                reinterpret_cast<modules::SkateNet*>(skate)->add_client_handlers(
                    client, i); // the skate module's handlers for that client
        }
    }
    return kContinue;
}

// 0x0052E1A0
int __cdecl handle_ready_to_play(net::InboundMsgCtx* context)
{
    auto* logic_tasks = (&assets::g_main_loop_manager->logic_tasks_);
    Hub* manager = manager_of(context);
    manager->set_ready_state(true); // (cleared_to_start_ set, the skaters told)
    if (!context->receiver_app_->is_host_local())
        logic_tasks->queue_job(manager->guest_admit_job_.get());
    return kContinue;
}

// 0x0052EB70
int __cdecl handle_park_map(net::InboundMsgCtx* context)
{
    // NOT ORIGINAL: a map shorter than a map is dropped (the original copied a whole map's worth from it)
    if (context->body_size_ < static_cast<uint32_t>(kParkMapSize))
        return kContinue;
    park_editing::ParkBuilderMode::sole()->set_packed_park_data(
        reinterpret_cast<const park_editing::CompressedMap*>(context->body_), true);
    void* map = park_editing::ParkBuilderMode::sole()->get_packed_park_data(false);
    checksums::generate_crc_from_buffer(map, kParkMapSize); // (not used)
    return kContinue;
}

// 0x0052ED80
int __cdecl on_match_end_msg(net::InboundMsgCtx* context)
{
    manager_of(context)->mark_match_ended();
    if (commands::cfunc_is_observing(nullptr, nullptr) && get_game_type() == kNetGoalAttack)
        get_run(kCreateRankings); // (IsObserving)
    return kContinue;
}

// 0x0052EDD0
int __cdecl handle_game_ended(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    objects::g_trick_spot_hub->remove_all_clusters();
    get_run(kCreateGameEndedDialog);
    manager->clear_roster();
    return kContinue;
}

// 0x0052F2D0
int __cdecl handle_choose_team(net::InboundMsgCtx* context)
{
    uint8_t* skate = get_skate();
    int teams = static_cast<int8_t>(get_message(context)[0]);
    if (teams <= 0 || is_squad_match()) {
        get_call(code_address(&goals::MatchRules::set_num_teams), get_game_mode(), teams);
        return kContinue;
    }
    get_call(code_address(&goals::MatchRules::set_num_teams), get_game_mode(), teams);
    get_run(kChooseTeamMessage);
    if (reinterpret_cast<modules::Skate*>(skate)->get_home_rider()) { // the local skater
        get_run(kPrepareSkaterForMove, nullptr, reinterpret_cast<modules::Skate*>(skate)->get_home_rider());
        get_call(code_address(&modules::Skate::teleport_to_spawn), skate,
                 reinterpret_cast<modules::Skate*>(skate)->get_home_rider(), 0, 0);
    }
    return kContinue;
}

// 0x0052F370
int __cdecl handle_checksum_request(net::InboundMsgCtx* context)
{
    uint8_t* skate = get_skate();
    net::NetMessageInfo desc;
    char* msg = context->body_;
    *reinterpret_cast<uint32_t*>(msg + 4) = get_call<uint32_t>(code_address(&modules::Skate::get_cheats), skate);
    desc.group_id_ = kGroupGame;
    desc.byte_count_ = 8;
    desc.ident_ = kMsgDigest;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.content_ = msg;
    send_to_server(context->receiver_app_, &desc);
    return kContinue;
}

// 0x0052EBB0
int __cdecl handle_team_object(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    if (!is_squad_match())
        return kContinue;
    PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    if (!player)
        return kDone;
    uint32_t object = message_word(context, 0);
    uint32_t* objects = manager->team_objects_[player->team_]; // five per team
    for (int i = 0; i < 5; ++i) {
        if (!objects[i]) {
            objects[i] = object;
            return kContinue;
        }
        if (objects[i] == object)
            return kDone;
    }
    return kContinue;
}

// 0x0052F3D0 (no check that the sender is a player: the original's)
int __cdecl handle_cd_key_answer(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    uint32_t id = player->skater_->ident_; // its skater's id
    manager->rating_service_->set_player_auth(static_cast<int>(id), context->body_);
    return kContinue;
}

// 0x0052F400
int __cdecl handle_created_data_request(net::InboundMsgCtx* context)
{
    uint8_t id = context->type_code_;
    net::AppHost* app = context->receiver_app_;
    Hub* manager = manager_of(context);
    if (id == kMsgRequestRails) {
        int handle = context->sender_conn_->hdl_;
        auto rail_editor = [] { return park_editing::RailAuthoring::sole(); };
        rail_editor(); // (looked up once for nothing)
        void* rails =
            get_call<void*>(code_address(&park_editing::RailAuthoring::get_packed), rail_editor()); // the rails, packed
        app->send_bulk_message(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    } else if (id == kMsgRequestGoals && manager->has_created_goals()) {
        int handle = context->sender_conn_->hdl_;
        void* goals = manager->get_created_goals_message();
        auto size = static_cast<uint16_t>(manager->get_created_goals_size());
        app->send_bulk_message(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    return kContinue;
}

} // namespace online
