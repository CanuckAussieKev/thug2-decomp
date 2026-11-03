// The network manager's message handlers (online::Hub) and their helpers. See gamenet_handlers.h.

#include "engine/net/gamenet_handlers.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <array>

#include "engine/core/checksums.h"
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
#include "engine/script/preferences.h"
#include "game/skate/skate_net.h"
#include "engine/util/bitstream.h"
#include "engine/script/game_cfuncs.h"
#include "game/park_editing/rail_editor.h"
#include "engine/assets/resource_registry.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/sys/mlp.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"
#include "wire/net_wire.h"
#include "platform/registry.h"

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
void get_cd_key(char* key)
{
    static const auto kPath = std::to_array("Software\\Activision\\Tony Hawk's Underground 2");
    std::array<char, 100> value = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    HKEY handle = nullptr;
    auto query = [&] { sys::query_registry_value(handle, "key", &type, value.data(), &size); };
    bool opened = false;
    if (RegCreateKeyA(HKEY_CURRENT_USER, kPath.data(), &handle) == ERROR_SUCCESS &&
        sys::query_registry_value(handle, "key", &type, value.data(), &size) == ERROR_SUCCESS) {
        opened = true;
    } else {
        // (an opened HKEY_CURRENT_USER key without the value is left open: the original's)
        opened = RegCreateKeyA(HKEY_LOCAL_MACHINE, kPath.data(), &handle) == ERROR_SUCCESS;
        if (opened)
            query();
    }
    if (opened)
        RegCloseKey(handle);
    // (a value filling all 100 bytes has no terminator: the original's strcpy runs on into its stack - here it stops)
    const size_t length = strnlen(value.data(), sizeof(value));
    std::memcpy(key, value.data(), length);
    core::at(std::span<char>(key, sizeof(value) + 1), length) = '\0';
}

// 0x0052D7B0
int on_ready_ask_msg(net::InboundMsgCtx* context)
{
    manager_of(context)->set_last_ready_probe_id(message_word(context, 0));
    return kContinue;
}

// 0x0052D7D0
int on_ready_reply_msg(net::InboundMsgCtx* context)
{
    context->set_acknowledge(true);
    if (PlayerRecord* player = find_player_by_sender(manager_of(context), context->sender_conn()))
        player->receive_ready_response(message_word(context, 0));
    return kContinue;
}

// 0x0052D800
int on_spectate_msg(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    cfunc_exit_surveyor_mode(nullptr, nullptr); // (the script command, without parameters)
    manager->enter_observe_mode();
    return kContinue;
}

// 0x0052D830
int handle_flush_important(net::InboundMsgCtx* context)
{
    if (context->sender_conn()->is_off_machine())
        context->sender_conn()->destroy_important_messages();
    return kContinue;
}

namespace {
// msg[0] the player's object id, msg[1] the team (both signed bytes).
int flag_handler(net::InboundMsgCtx* context, void (PlayerRecord::*method)(int))
{
    const uint8_t* msg = get_message(context);
    if (PlayerRecord* player = find_player_slot_by_obj(manager_of(context), msg))
        (player->*method)(static_cast<int>(net::wire::value_at<int8_t>(msg, 1)));
    return kContinue;
}
} // namespace

// 0x0052D850 / 0x0052D880 / 0x0052D8B0
int on_flag_swiped_msg(net::InboundMsgCtx* context) { return flag_handler(context, &PlayerRecord::note_flag_steal); }
int on_flag_grabbed_msg(net::InboundMsgCtx* context) { return flag_handler(context, &PlayerRecord::note_flag_pickup); }
int on_flag_scored_msg(net::InboundMsgCtx* context) { return flag_handler(context, &PlayerRecord::score_flag_capture); }

// 0x0052D8E0
int on_flag_returned_msg(net::InboundMsgCtx* context)
{
    if (PlayerRecord* player = find_player_slot_by_obj(manager_of(context), get_message(context)))
        player->note_flag_returned();
    return kContinue;
}

// 0x0052D910 / 0x0052D930
int handle_auto_server_notice(net::InboundMsgCtx*)
{
    get_run(kLaunchAutoServerNotification);
    return kContinue;
}
int handle_kill_team_flags(net::InboundMsgCtx*)
{
    get_run(kKillTeamFlags);
    return kContinue;
}

// 0x0052D950
int on_hud_notice_msg(net::InboundMsgCtx* context)
{
    // (the message: gamenet_messages.h; NOT ORIGINAL: one too short is dropped and its strings end inside their
    // fields - the original read them up to a terminator that may not be there)
    util::ByteReader reader(context->body(), context->body_size());
    HudNotice notice;
    if (!read_hud_notice(reader, &notice))
        return kDone;
    // 0x005376F0: (broadcast, text, string 1, string 2, object, properties, important, time)
    manager_of(context)->build_net_notice(false, notice.text, notice.string1.data(), notice.string2.data(), nullptr, nullptr, false,
                                          notice.time);
    return kContinue;
}

// 0x0052D980
int handle_object_update_length(net::InboundMsgCtx* context)
{
    util::PackedBitBuffer stream;
    stream.change_intake_contents(context->body(), 0x400);
    const uint32_t present = stream.read_bits(8) & 0xFF;
    for (int skater = 0; skater < 8; ++skater) {
        if (!(present & (1u << skater)))
            continue;
        stream.read_bits(16); // the time
        const uint32_t flags = stream.read_bits(9);
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
    context->set_body_size(stream.get_byte_count());
    return kContinue;
}

// 0x0052DAC0
int handle_nothing(net::InboundMsgCtx*) { return kContinue; }

// 0x0052DAD0
int handle_cd_key_challenge(net::InboundMsgCtx* context)
{
    net::NetMessageInfo desc;
    Hub* manager = manager_of(context);
    const char* password = manager->get_online_settings()->get_preference_string({.preference = kProfilePassword, .field = kUiString});
    std::array<char, 36> answer{};
    // 0x0054CA10: the answer to the challenge (the message itself). (Static: the original passes the online service,
    // +0x1C, in ECX, unused.)
    RankingsManager::generate_auth(net::wire::text_of(context->body()), password, answer.data());
    desc.set_ident(kMsgCdKeyAnswer);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(kGroupGame);
    desc.set_content(answer.data());
    desc.set_byte_count(kCdKeyAnswerSize);
    send_to_server(context->receiver_app(), &desc);
    return kContinue;
}

// 0x0052DCA0
int handle_keep_alive(net::InboundMsgCtx* context)
{
    context->sender_conn()->stamp_last_heard(net::wire::value_at<int8_t>(get_message(context)) * 1000);
    return kContinue;
}

// 0x0052DCC0
int on_admitted_msg(net::InboundMsgCtx* context)
{
    auto* skate = get_skate();
    net::NetMessageInfo desc;
    Hub* manager = manager_of(context);
    manager->set_entry_phase(6);
    desc.set_ident(kMsgReadyToReceive);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(kGroupGame);
    send_to_server(context->receiver_app(), &desc);
    if (!manager->get_on_server()) {
        for (int i = 0; i < 2; ++i) {
            if (net::AppHost* client = manager->fetch_patron(i))
                modules::SkateNet::add_client_handlers(skate, client, i); // the skate module's handlers for that client
        }
    }
    return kContinue;
}

// 0x0052E1A0
int handle_ready_to_play(net::InboundMsgCtx* context)
{
    auto* logic_tasks = (&assets::get_main_loop_manager()->logic_tasks());
    Hub* manager = manager_of(context);
    manager->set_ready_state(true); // (cleared_to_start_ set, the skaters told)
    if (!context->receiver_app()->is_host_local())
        logic_tasks->queue_job(manager->guest_admit_job().get());
    return kContinue;
}

// 0x0052EB70
int handle_park_map(net::InboundMsgCtx* context)
{
    // NOT ORIGINAL: a map shorter than a map is dropped (the original copied a whole map's worth from it)
    if (context->body_size() < static_cast<uint32_t>(kParkMapSize))
        return kContinue;
    park_editing::ParkBuilderMode::sole()->set_packed_park_bytes(context->body(), true);
    const void* map = park_editing::ParkBuilderMode::sole()->get_packed_park_data(false)->bytes().data();
    checksums::generate_crc_from_buffer(map, kParkMapSize); // (not used)
    return kContinue;
}

// 0x0052ED80
int on_match_end_msg(net::InboundMsgCtx* context)
{
    manager_of(context)->mark_match_ended();
    if (commands::cfunc_is_observing(nullptr, nullptr) && get_game_type() == kNetGoalAttack)
        get_run(kCreateRankings); // (IsObserving)
    return kContinue;
}

// 0x0052EDD0
int handle_game_ended(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    objects::trick_spot_hub()->remove_all_clusters();
    get_run(kCreateGameEndedDialog);
    manager->clear_roster();
    return kContinue;
}

// 0x0052F2D0
int handle_choose_team(net::InboundMsgCtx* context)
{
    auto* skate = get_skate();
    const int teams = int{net::wire::value_at<int8_t>(get_message(context))};
    if (teams <= 0 || is_squad_match()) {
        get_game_mode()->set_num_teams(teams);
        return kContinue;
    }
    get_game_mode()->set_num_teams(teams);
    get_run(kChooseTeamMessage);
    if (skate->get_home_rider()) { // the local skater
        get_run(kPrepareSkaterForMove, nullptr, skate->get_home_rider());
        static_cast<modules::Skate*>(skate)->teleport_to_spawn(
            skate->get_home_rider(), 0, 0);
    }
    return kContinue;
}

// 0x0052F370
int handle_checksum_request(net::InboundMsgCtx* context)
{
    auto* skate = get_skate();
    net::NetMessageInfo desc;
    uint8_t* msg = context->body();
    const uint32_t cheats = static_cast<modules::Skate*>(skate)->get_cheats();
    std::memcpy(std::span(msg, 8).last(4).data(), &cheats, sizeof(cheats)); // (its second word)
    desc.set_group_id(kGroupGame);
    desc.set_byte_count(8);
    desc.set_ident(kMsgDigest);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_content(msg);
    send_to_server(context->receiver_app(), &desc);
    return kContinue;
}

// 0x0052EBB0
int handle_team_object(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    if (!is_squad_match())
        return kContinue;
    const PlayerRecord* player = find_player_by_sender(manager, context->sender_conn());
    if (!player)
        return kDone;
    const uint32_t object = message_word(context, 0);
    auto& objects = core::at(manager->team_objects(), player->team()); // five per team
    for (uint32_t& slot : objects) {
        if (!slot) {
            slot = object;
            return kContinue;
        }
        if (slot == object)
            return kDone;
    }
    return kContinue;
}

// 0x0052F3D0 (no check that the sender is a player: the original's)
int handle_cd_key_answer(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    const PlayerRecord* player = find_player_by_sender(manager, context->sender_conn());
    const uint32_t id = player->skater()->ident(); // its skater's id
    manager->rating_service()->set_player_auth(static_cast<int>(id), net::wire::text_of(context->body()));
    return kContinue;
}

// 0x0052F400
int handle_created_data_request(net::InboundMsgCtx* context)
{
    const uint8_t id = context->type_code();
    net::AppHost* app = context->receiver_app();
    Hub* manager = manager_of(context);
    if (id == kMsgRequestRails) {
        const int handle = context->sender_conn()->hdl();
        auto rail_editor = [] { return park_editing::RailAuthoring::sole(); };
        rail_editor(); // (looked up once for nothing)
        void* rails = static_cast<park_editing::RailAuthoring*>(rail_editor())->get_packed(); // the rails, packed
        app->send_bulk_message(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    } else if (id == kMsgRequestGoals && manager->has_created_goals()) {
        const int handle = context->sender_conn()->hdl();
        void* goals = manager->get_created_goals_message();
        auto size = static_cast<uint16_t>(manager->get_created_goals_size());
        app->send_bulk_message(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    return kContinue;
}

} // namespace online
