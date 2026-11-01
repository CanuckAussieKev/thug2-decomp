// The network manager's members after its tasks (online::Hub, 0x00533F20-0x00534FC0): the network mode flags,
// the team flags, the player / observer counts and password among the network preferences, the server's end and the
// custom tag texture a client sends.

#include <cstdint>
#include <initializer_list>
#include "engine/core/code_address.h"
#include "engine/gfx/engine_texture.h"
#include "platform/compiler.h"
#include <cstdio>

THUG2_MSVC_WARNING(disable : 4996) // (the original's sprintf, kept)

#include "engine/core/checksums.h"
#include "engine/net/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/maths/random.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lists.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/net/stats_man.h"
#include "engine/net/vault_man.h"
#include "engine/objects/entity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/rider/tagging.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/cas_options.h"
#include "engine/assets/resource_registry.h"
#include "game/park_editing/park_editor.h"
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
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/sys/mlp.h"

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kFlagsServer = 1;   // flag_bits_ bit 0
constexpr uint32_t kFlagsInternet = 4; // bit 2
constexpr uint32_t kFlagsLan = 8;      // bit 3

// Checksums.
constexpr uint32_t kKillTeamFlags = 0xEBB3642E;   // Kill_Team_Flags
constexpr uint32_t kCreateTeamFlags = 0x20B145E1; // create_team_flags
constexpr uint32_t kNumTeams = 0x2EBE6CD1;        // num_teams
constexpr uint32_t kNumPlayers = 0x1CB2A6B0;      // num_players
constexpr uint32_t kNumObservers = 0xA8DA5CB7;    // num_observers
constexpr uint32_t kPassword = 0xCA3DB92A;        // password
constexpr uint32_t kUiString = 0x96875C0F;        // ui_string
constexpr uint32_t kChecksum = 0x21902065;        // checksum
constexpr uint32_t kName = 0xA1DC81F9;            // name
constexpr uint32_t kStrPlayers = 0x50589935;      // netmenu_str_players
constexpr uint32_t kStrNoObservers = 0x8AD4F7EA;  // netmenu_str_no_observers
constexpr uint32_t kStrObserver = 0x201D7F4E;     // netmenu_str_observer
constexpr uint32_t kStrObservers = 0x5848516E;    // netmenu_str_observers
constexpr uint32_t kPlayerCollision = 0x043EE978; // player_collision
constexpr uint32_t kBooleanTrue = 0xF81BC89B;     // boolean_true
constexpr uint32_t kScoreDisplay = 0x92A6A8C8;    // score_display
constexpr uint32_t kScoreTeams = 0xD071112F;      // score_teams
constexpr uint32_t kHatLogo = 0x5B288E0B;         // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;       // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;        // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;     // deck_graphic
// The game modes.
constexpr uint32_t kFreeskate = 0x34861A16;        // freeskate
constexpr uint32_t kHorse = 0x9D65D0E7;            // horse
constexpr uint32_t kNetLobby = 0x1C471C60;         // netlobby
constexpr uint32_t kNetSlap = 0xF9D5D933;          // netslap
constexpr uint32_t kSlap = 0xCA1F360F;             // slap
constexpr uint32_t kNetFirefight = 0xBFF33600;     // netfirefight
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;    // netgoalattack
constexpr uint32_t kNetCtf = 0x6C5FF266;           // netctf
constexpr uint32_t kNetScavengerHunt = 0xFC13B531; // netscavengerhunt
constexpr uint32_t kNetElimiskate = 0x10F84276;    // netelimiskate
constexpr uint32_t kIsHorse = 0xD1993397;          // is_horse
constexpr uint32_t kOff = 0xD443A2BC;              // off

// Messages.
constexpr uint8_t kMsgTagTexture = 0x97;
constexpr uint8_t kMsgTagPalette = 0x98;
constexpr uint8_t kMsgTagLogos = 0x9A;
constexpr uint8_t kGroupTag = 10;

const char* const kTagPaletteDesc = "CAG Palette"; // "CAG Palette"
const char* const kTagTextureDesc = "CAG Texture"; // "CAG Texture"

// The game mode's members (not rewritten): its max_players, the flags accumulate_score / track_best_combo /
// is_elimiskate, a property set (IsTrue).
int mode_max_players() { return get_game_mode()->get_rider_ceiling(); }
bool accumulates_score() { return get_game_mode()->keeps_running_total(); }
bool tracks_best_combo() { return get_game_mode()->keeps_best_chain(); }
bool is_elimiskate() { return get_game_mode()->finish_rules_reached(); }
bool mode_is_true(uint32_t property) { return get_game_mode()->is_true(property); }

tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }

// The Function InSplitScreenGame (0x005A1180), called with no parameters.
bool in_split_screen_game() { return commands::cfunc_in_split_screen_game(nullptr, nullptr) != 0; }

// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool profile_has_part(void* profile, uint32_t part) { return objects::part_is_created_graphic(profile, part); }

// The custom tag texture's parts (render::BitmapTexture's virtual slots 25-28).
// (On the PC all four are the shared stubs: null / 0.)
void* texture_pixels(void* texture)
{
    return const_cast<void*>(static_cast<render::BitmapTexture*>(texture)->get_pixels());
}
uint16_t get_texture_size(void* texture)
{
    return static_cast<uint16_t>(static_cast<render::BitmapTexture*>(texture)->get_pixels_size());
}
void* texture_palette(void* texture)
{
    return const_cast<void*>(static_cast<render::BitmapTexture*>(texture)->get_palette());
}
uint16_t texture_palette_size(void* texture)
{
    return static_cast<uint16_t>(static_cast<render::BitmapTexture*>(texture)->get_palette_size());
}

// A network preference set from `values` (the original's new CStruct, never freed).
void set_count_preference(Hub* manager, uint32_t preference, const char* ui_string, uint32_t checksum)
{
    script::Struct* values = new_params();
    values->append_word(kUiString, script::kQbvalString, ui_string);
    values->add_component_of_type(kChecksum, script::kQbvalName, checksum);
    manager->get_online_settings()->change_setting(preference, values);
}

} // namespace

// 0x00533F20
void Hub::set_network_mode(int mode)
{
    flag_bits_ &= ~(kFlagsInternet | kFlagsLan);
    if (mode == 1)
        flag_bits_ |= kFlagsLan;
    else if (mode == 2)
        flag_bits_ |= kFlagsInternet;
}

// 0x00533F50
void Hub::set_server_mode(bool on)
{
    if (on)
        flag_bits_ |= kFlagsServer;
    else
        flag_bits_ &= ~kFlagsServer;
}

// 0x00533F70
int Hub::is_local_network_play() { return (flag_bits_ >> 3) & 1; }

// 0x00533FA0
int Hub::is_client_side() { return (flag_bits_ >> 1) & 1; }

// 0x00533FB0
void Hub::build_team_flag_set(int num_teams)
{
    get_run(kKillTeamFlags);
    if (!num_teams)
        return;
    script::Struct* params = new_params();
    params->put_int(kNumTeams, num_teams);
    get_run(kCreateTeamFlags, params);
    delete_params(params);
}

// 0x00534090
void Hub::set_max_players_preference(int count)
{
    char num_name[32];
    char ui_string[32];
    std::sprintf(num_name, "num_%d", count);
    uint32_t checksum = checksums::hash_from_text(num_name);
    std::sprintf(ui_string, "%d %s", count, script::fetch_text(kStrPlayers));
    set_count_preference(this, kNumPlayers, ui_string, checksum);
}

// 0x005341B0
void Hub::set_max_observers_preference(int count)
{
    char num_name[32];
    char ui_string[32];
    std::sprintf(num_name, "num_%d", count);
    uint32_t checksum = checksums::hash_from_text(num_name);
    if (count == 0)
        std::sprintf(ui_string, script::fetch_text(kStrNoObservers)); // (the text is the format, as in the original)
    else if (count == 1)
        std::sprintf(ui_string, "1 %s", script::fetch_text(kStrObserver));
    else
        std::sprintf(ui_string, "%d %s", count, script::fetch_text(kStrObservers));
    set_count_preference(this, kNumObservers, ui_string, checksum);
}

// 0x00534310
const char* Hub::fetch_secret()
{
    script::Struct* preference = get_online_settings()->fetch_setting(kPassword);
    // On a miss the original returns its uninitialised local: the slot ECX was pushed into - the manager.
    auto password = reinterpret_cast<const char*>(this);
    preference->fetch_text(kUiString, &password, 1);
    return password;
}

// 0x00534340
bool Hub::players_can_collide()
{
    if ((match_ended_ || get_game_type() == kFreeskate || get_game_type() == kHorse) && get_game_type() != kNetLobby)
        return false;
    if (in_split_screen_game())
        return true;
    if (get_online_settings()->get_preference_checksum(kPlayerCollision, kChecksum) == kBooleanTrue)
        return true;
    return get_game_type() == kNetSlap || get_game_type() == kSlap;
}

// 0x005343E0
bool Hub::show_team_scores()
{
    if (get_game_type() == kNetLobby || !is_squad_match())
        return false;
    return get_online_settings()->get_preference_checksum(kScoreDisplay, kChecksum) == kScoreTeams;
}

// 0x00534CF0
void Hub::destroy_server()
{
    net::Hub* net = net::Hub::sole();
    if (host_) {
        if (!host_->is_host_local() && (flag_bits_ & kFlagsInternet)) {
            lobby_service_->stop_hosting();    // the lobby's reporting stopped
            game_spy::nat_free_negotiations(); // (GameSpy)
        }
        net->release_app(host_);
        host_admit_job_->drop_out();
        start_game_job_->drop_out();
        host_ = nullptr;
    }
    flag_bits_ &= ~kFlagsServer;
}

// 0x00534D50
void Hub::destroy_match_client()
{
    if (browser_client_) {
        net::Hub::sole()->release_app(browser_client_);
        browser_client_ = nullptr;
    }
}

// 0x00534D70
bool Hub::send_custom_tag_texture()
{
    net::ConnSearch search;
    bool on_server = (flag_bits_ & kFlagsServer) != 0;
    auto* skate = detail::get_skate();
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem || on_server)
        return false;
    auto* skater = reinterpret_cast<modules::Skate*>(skate)->get_home_rider(); // the local skater
    if (!skater)
        return false;
    auto* tagging = static_cast<objects::TaggingComponent*>(skater->fetch_module(objects::TaggingComponent::kType));
    if (!tagging || !tagging->has_custom_tag_texture())
        return false;

    net::Conn* server = client_app_->begin_conn_walk(&search);
    if (server->get_link_speed_class() == 0) {
        void* profile = detail::get_skate()->fetch_account(0);
        uint32_t logos = profile_has_part(profile, kHatLogo) ? 1 : 0;
        if (profile_has_part(profile, kFrontLogo))
            logos |= 2;
        if (profile_has_part(profile, kBackLogo))
            logos |= 4;
        if (profile_has_part(profile, kDeckGraphic))
            logos |= 8;
        void* texture = reinterpret_cast<goals::Objective*>(tagging)->fetch_args(); // the custom tag texture
        if (texture) {
            net::NetMessageInfo desc;
            desc.ident_ = kMsgTagLogos;
            desc.content_ = &logos;
            desc.byte_count_ = 4;
            desc.queue_ = net::NetMessageInfo::kLaneOrdered;
            desc.group_id_ = kGroupTag;
            void* palette = texture_palette(texture);
            client_app_->send_bulk_to_conn(server, kMsgTagPalette, texture_palette_size(texture), palette,
                                           kTagPaletteDesc, kGroupTag, false, true);
            void* pixels = texture_pixels(texture);
            client_app_->send_bulk_to_conn(server, kMsgTagTexture, get_texture_size(texture), pixels, kTagTextureDesc,
                                           kGroupTag, false, true);
            send_to_server(client_app_, &desc);
        }
    }
    return true;
}

// 0x00534F40
uint32_t Hub::fetch_stage() { return level_; }

// 0x00534F50
const char* Hub::get_choice_name(uint32_t list, uint32_t choice)
{
    script::Array* choices = script::fetch_list(list);
    const char* name = nullptr;
    // The checksum is read into the argument's slot: an entry without one compares what the slot last held (at
    // first the list's own name).
    uint32_t checksum = list;
    for (int i = 0; i < static_cast<int>(choices->extent_); ++i) {
        auto* entry = reinterpret_cast<script::Struct*>(choices->fetch_part(i));
        entry->read_hash_value(kChecksum, &checksum, 1);
        if (checksum == choice)
            entry->fetch_text(kName, &name, 1);
    }
    return name;
}

// 0x00534FC0
int Hub::get_choice_integer(uint32_t list, uint32_t choice, uint32_t field)
{
    script::Array* choices = script::fetch_list(list);
    int value = 0;
    // (as above, the checksum goes into `choice`'s slot: an entry without one matches)
    uint32_t checksum = choice;
    for (int i = 0; i < static_cast<int>(choices->extent_); ++i) {
        auto* entry = reinterpret_cast<script::Struct*>(choices->fetch_part(i));
        entry->read_hash_value(kChecksum, &checksum, 1);
        if (checksum == choice)
            entry->fetch_int_value(field, &value, 1);
    }
    return value;
}

// 0x005359F0
void Hub::choose_start_points()
{
    int* points = reinterpret_cast<int*>(start_points_);
    int offset = maths::rnd(256);
    int count = 0;
    if (in_net_game()) {
        count = 8;
    } else {
        count = mode_max_players();
        if (park_editing::g_park_editor->cur_phase_ != kOff && !mode_is_true(kIsHorse)) {
            count = 2;
            offset = 0;
        }
    }
    // (no bounds check: a mode with more than 8 players writes past the eight words, as in the original)
    for (int i = 0; i < count; ++i)
        points[i] = -1;
    for (int i = 0; i < count; ++i) {
        // Drawn again until it's one no other slot has.
        bool taken = false;
        do {
            points[i] = maths::rnd(count);
            taken = false;
            for (int j = 0; j < count && !taken; ++j)
                taken = i != j && points[i] == points[j];
        } while (taken);
    }
    for (int i = 0; i < count; ++i)
        points[i] += offset;
    crown_start_ = maths::rnd(10);
}

// 0x00535BC0
void Hub::toggle_net_metrics()
{
    tasks::PileUp* tasks = get_logic_tasks();
    show_metrics_ = !show_metrics_;
    if (show_metrics_)
        tasks->queue_job(metrics_job_.get());
    else
        metrics_job_->drop_out();
}

// 0x00535BF0
void Hub::toggle_scores()
{
    accumulates_score(); // (asked and not used)
    show_scores_ = !show_scores_;
}

// 0x00535D00
bool Hub::keeps_score()
{
    if (!accumulates_score() && !tracks_best_combo() && get_game_type() != kNetFirefight &&
        get_game_type() != kNetGoalAttack && get_game_type() != kNetCtf && get_game_type() != kNetScavengerHunt)
        return false;
    return !is_elimiskate() || get_game_type() == kNetFirefight || get_game_type() == kNetElimiskate;
}

// 0x00535D90
void Hub::set_preset_option_bits(char flags) { script_flag_bits_ = static_cast<uint32_t>(static_cast<int>(flags)); }

// (no address: the list code folded at 0x00588B70 - see the header; the first node's data read before the null check,
// as there)
void Hub::unlink_all_nodes(memory::ListNode* head)
{
    memory::ListNode* node = head->next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        memory::ListNode* following = node->next;
        node->prev->next = following;
        following->prev = node->prev;
        node->next = node;
        node->prev = node;
        node = following->data == reinterpret_cast<void*>(-1) ? nullptr : following;
    }
}

bool g_query_reporting;

// 0x00536D70
Hub::~Hub()
{
    // GameSpy shut down (the SDK's), its reporting if it's up.
    game_spy::cd_key_disconnect_all(1003);
    game_spy::cd_key_shutdown();
    if (g_query_reporting) {
        game_spy::get_qr2_shutdown(nullptr);
        g_query_reporting = false;
    }
    // The lobby, the vault, the presence and the stats: each one's destructor, then freed (each while the hub still
    // points at it).
    memory::destroy_then_clear(lobby_service_);
    memory::destroy_then_clear(vault_man_);
    memory::destroy_then_clear(presence_service_);
    memory::destroy_then_clear(rating_service_);
    // The tasks, in the original's order. (NOT ORIGINAL: the five the original never deleted - score broadcast,
    // dial-up, join expiry, auto serve, flag carry - go with the hub's members after.)
    for (std::unique_ptr<tasks::SchedulerTask>* job :
         {&guest_admit_job_, &host_admit_job_, &connection_job_, &metrics_job_, &keyboard_job_, &service_job_,
          &join_phase_job_, &start_game_job_, &level_change_job_, &server_list_job_})
        memory::destroy_then_clear(*job);
    // The lists: the players' nodes unlinked, then each head's destructor.
    unlink_all_nodes(&players_);
    waiting_servers_.uncouple();
    servers_.uncouple();
    queued_scripts_.uncouple();
    joiners_.uncouple();
    players_.uncouple();
    get_taunt_settings()->~Preferences();
    get_online_settings()->~Preferences();
}

} // namespace online
