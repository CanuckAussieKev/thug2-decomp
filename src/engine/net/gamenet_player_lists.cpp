// The network manager's player records (online::Hub, 0x00536820-0x00537F40): the players' and joining players'
// lists - walking, finding and removing them.

#include <cstdint>
#include <initializer_list>
#include "platform/code_address.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/d3dx.h"
#include "platform/compiler.h"
#include <cstring>
#include <utility>
#include <array>

THUG2_MSVC_WARNING(disable : 4996) // (strcpy / strcat: the original's own copies into its fixed buffers)

#include "engine/core/checksums.h"
#include "engine/net/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/buddy_status.h"
#include "engine/net/gamenet_game_cfuncs.h"
#include "engine/net/net_client.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "game/skate/profile_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lists.h"
#include "engine/net/gamenet_manager.h"
#include "engine/pad_input/input_hub.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "game/skate/cas_options.h"
#include "engine/objects/compositemgr.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/rider/skater_score.h"
#include "engine/objects/skeleton_component.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kSkaterListFirst =
    0x30; // the skate module's skater list's first node (its head at +0x28; the original's +0x34)

// Messages.
constexpr uint8_t kMsgFlipProSetting = 0x70;

// Checksums.
constexpr uint32_t kSkaterScore = 0xD262802F;           // SkaterScore (the component)
constexpr uint32_t kText = 0xC4745838;                  // text
constexpr uint32_t kId = 0x40C698AF;                    // id
constexpr uint32_t kNetPanelMsg = 0x85BE419B;           // net_panel_msg
constexpr uint32_t kMsgTime = 0xEDD17FEB;               // msg_time
constexpr uint32_t kCreateNetPanelMessage = 0xD9BB372E; // create_net_panel_message
constexpr uint32_t kPreRunQueuedScripts = 0xD4DAFEB2;   // PreRunQueuedScripts
constexpr uint32_t kPostRunQueuedScripts = 0xE1BDBE73;  // PostRunQueuedScripts

const char* const kStringSlot = "%s0"; // "%s0"

memory::ListNode* player_list(Hub* manager) { return &manager->players_; }
memory::ListNode* new_player_list(Hub* manager) { return &manager->joiners_; }
memory::ListNode* queued_script_list(Hub* manager) { return &manager->queued_scripts_; }
bool is_head(const memory::ListNode* node) { return static_cast<intptr_t>(cell::address(node->data)) == -1; }
// The node after `node`; null at the list's end (the head).
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next) ? nullptr : node->next; }
template <typename T>
T* data_of(const memory::ListNode* node)
{
    return node ? static_cast<T*>(node->data) : nullptr;
}
// The nodes from `node` to the list's end (the original's loop: the head's mark tested before a null node).
unsigned int tally_nodes(const memory::ListNode* node)
{
    unsigned int count = 0;
    while (!is_head(node) && node) {
        node = node->next;
        ++count;
    }
    return count;
}
// --- 0x00539610-0x0053A580's ---
constexpr uint32_t kNewPlayerSize = 0x44; // JoinerRecord (constructor 0x005434D0)
constexpr uint16_t kFaceSize = 0x4498;
constexpr uint8_t kMsgFace = 0x7E;
constexpr uint8_t kGroupFace = 9;
const char* const kFaceDesc = "Face Data"; // "Face Data"
constexpr uint8_t kMsgTagOwner = 0x96;
constexpr uint8_t kMsgTagTexture = 0x97;
constexpr uint8_t kMsgTagPalette = 0x98;
constexpr uint8_t kMsgTagLogos = 0x9A;
constexpr uint8_t kGroupTag = 10;
const char* const kTagPaletteDesc = "CAG Palette";      // "CAG Palette"
const char* const kTagTextureDesc = "CAG Texture";      // "CAG Texture"
constexpr uint32_t kHatLogo = 0x5B288E0B;               // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;             // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;              // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;           // deck_graphic
constexpr uint32_t kNetMessageJoining = 0xA41ECAAF;     // net_message_joining
constexpr uint32_t kNetMessageObserving = 0x8A7E0A70;   // net_message_observing
constexpr uint32_t kNetMessageJoinPending = 0xF33706D5; // net_message_join_pending
constexpr uint32_t kNetCtf = 0x6C5FF266;                // netctf
constexpr uint32_t kTrgCtfRed = 0x36EA35EB;             // TRG_CTF_Red
constexpr uint32_t kTrgCtfBlue = 0x06270B12;            // TRG_CTF_Blue
constexpr uint32_t kTrgCtfGreen = 0x3DB516B0;           // TRG_CTF_Green
constexpr uint32_t kTrgCtfYellow = 0x85E38F38;          // TRG_CTF_Yellow
constexpr uint32_t kSkeleton = 0x222756D5;              // Skeleton (the component)
constexpr uint32_t kBoneHead = 0xDDEC28AF;              // Bone_Head
constexpr float kQuarterTurn = 90.0f;                   // (make_pitch_matrix's angle, as the original passes it)

// A joining player who will play: flags 0x18, or not observing.
bool will_play(const JoinerRecord* info)
{
    const uint32_t flags = info->flag_bits_;
    return (flags & 0x18) || !(flags & 4);
}
net::Conn* player_conn(const PlayerRecord* player) { return player->conn_; }
// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool profile_has_part(void* profile, uint32_t part) { return objects::part_is_created_graphic(profile, part); }
// A custom tag texture's parts (render::BitmapTexture's virtual slots 25-28).
// (On the PC all four are the shared stubs: null / 0.)
void* texture_pixels(void* texture)
{
    return static_cast<render::BitmapTexture*>(texture)->get_pixels();
}
uint16_t get_texture_size(void* texture)
{
    return static_cast<uint16_t>(static_cast<render::BitmapTexture*>(texture)->get_pixels_size());
}
void* texture_palette(void* texture)
{
    return static_cast<render::BitmapTexture*>(texture)->get_palette();
}
uint16_t texture_palette_size(void* texture)
{
    return static_cast<uint16_t>(static_cast<render::BitmapTexture*>(texture)->get_palette_size());
}
// D3DXMatrixMultiply (the D3DX library's, stdcall): out = a * b.
void d_3d_x_matrix_multiply(void* out, const void* a, const void* b) { d3dx::matrix_multiply(out, a, b); }

// The first player (observers too) for whom the PlayerRecord member `test` is true (0 / 1 in EAX: tested as AL).
PlayerRecord* find_player(Hub* manager, int (PlayerRecord::*test)())
{
    for (const memory::ListNode* node = get_next_node(player_list(manager)); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if ((player->*test)() != 0)
            return player;
    }
    return nullptr;
}

// A joining player's record deleted (JoinerRecord's destructor body 0x00543560, then freed).
void delete_new_player(JoinerRecord* player)
{
    player->~JoinerRecord();
    memory::Hub::get_pull()->dealloc(player);
}

} // namespace

// 0x00536820
void Hub::remove_all_new_players()
{
    memory::ListNode* node = new_player_list(this)->next;
    if (is_head(node))
        return;
    while (node) {
        auto* player = static_cast<JoinerRecord*>(node->data);
        memory::ListNode* next = is_head(node->next) ? nullptr : node->next;
        if (host_)
            host_->sever_link(player->conn_);
        memory::ListNode::destroy(node); // (0x005367E0)
        if (player)
            delete_new_player(player);
        node = next;
    }
}

// 0x00536B10
void Hub::remove_new_player(JoinerRecord* player)
{
    memory::ListNode* node = new_player_list(this)->next;
    // (the original tests for the list's end before it tests for a null node)
    if (is_head(node) || !node)
        return;
    while (node->data != player) {
        node = node->next;
        if (is_head(node) || !node)
            return;
    }
    net::Conn* conn = player->conn_;
    if (host_)
        host_->sever_link(conn);
    else if (conn)
        client_app_->sever_link(conn);
    memory::ListNode::destroy(node);
    delete_new_player(player);
}

// 0x00537010
PlayerRecord* Hub::step_player_walk(PlayerSearch* search, bool observers)
{
    PlayerSearch* s = search;
    auto* node = get_next_node(s->list_link_);
    s->list_link_ = node;
    auto* player = data_of<PlayerRecord>(node);
    if (!observers && player && is_observer(player))
        return step_player_walk(search, false);
    return player;
}

// 0x005372C0
PlayerRecord* Hub::begin_player_walk(PlayerSearch* search, bool observers)
{
    PlayerSearch* s = search;
    memory::ListNode* head = player_list(this);
    s->list_link_ = head;
    memory::ListNode* node = get_next_node(head);
    s->list_link_ = node;
    auto* player = data_of<PlayerRecord>(node);
    if (!observers && player && is_observer(player))
        return step_player_walk(search, false);
    return player;
}

// 0x00537070
JoinerRecord* Hub::begin_joiner_walk(NewPlayerSearch* search)
{
    NewPlayerSearch* s = search;
    memory::ListNode* head = new_player_list(this);
    s->list_link_ = head;
    memory::ListNode* node = get_next_node(head);
    s->list_link_ = node;
    return data_of<JoinerRecord>(node);
}

// (no address: the lists' generic next, folded at 0x004573D0 - see the header)
JoinerRecord* Hub::step_joiner_walk(NewPlayerSearch* search)
{
    NewPlayerSearch* s = search;
    memory::ListNode* next = s->list_link_->next;
    s->list_link_ = next->data == memory::ListNode::head_marker() ? nullptr : next;
    return s->list_link_ ? static_cast<JoinerRecord*>(next->data) : nullptr;
}

// 0x005370A0
JoinerRecord* Hub::fetch_incoming_player_by_obj(uint16_t id)
{
    const memory::ListNode* node = get_next_node(new_player_list(this));
    for (auto* player = data_of<JoinerRecord>(node); player; player = data_of<JoinerRecord>(node)) {
        if (std::cmp_equal(static_cast<int8_t>(player->entity_key_), id))
            return player;
        node = get_next_node(node);
    }
    return nullptr;
}

// 0x00537140
void Hub::set_ready_state(bool ready)
{
    if (static_cast<uint8_t>(ready) == *raw::bytes(&cleared_to_start_))
        return;
    cleared_to_start_ = ready;
    modules::Skate* skate = modules::g_skate_module;
    const unsigned int count = tally_nodes(skate->riders_.list_link_.next);
    for (unsigned int i = 0; i < count; ++i) {
        auto* skater = core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
        if (skater) { // (the original's skater slots 12 / 11)
            if (ready)
                skater->unpause();
            else
                skater->get_freeze();
        }
    }
}

// 0x00537320
PlayerRecord* Hub::get_own_player() { return find_player(this, &PlayerRecord::is_own_player); }

// 0x005373D0
PlayerRecord* Hub::get_host_player() { return find_player(this, &PlayerRecord::is_host_player); }

// 0x00537480
PlayerRecord* Hub::get_hill_ruler() { return find_player(this, &PlayerRecord::holds_crown); }

// 0x00537530
int Hub::count_other_players()
{
    PlayerSearch search;
    int count = 0;
    for (PlayerRecord* player = begin_player_walk(&search, false); player; player = step_player_walk(&search, false)) {
        if (!player->has_finished_joining()) // (flags 0x21)
            ++count;
    }
    return count;
}

// 0x00537620
void Hub::clear_roster()
{
    for (const memory::ListNode* node = get_next_node(player_list(this)); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        player->flag_bits_ &= ~0x00020100u;
        player->accumulated_score_ = 0;
        player->eliminated_time_ = 0;
        player->num_placed_ = 0;
        auto* skater = raw::as<objects::GameEntity>(player_skater(player));
        if (skater) {
            // (not checked for null, as in the original)
            auto* score = core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore));
            if (score->points_held_)
                score->points_held_->get_reset();
        }
    }
}

// 0x005376F0
void Hub::build_net_notice(bool broadcast, uint32_t text, const char* string1, const char* string2, void*,
                           const char* properties, bool important, int time)
{
    if (broadcast) {
        net::NetMessageInfo desc;
        // (the panel message: gamenet_messages.h - the strings cut to their 16 bytes; the original copied them whole,
        // the bytes after a terminator as its stack held them)
        HudNotice message;
        message.text = text;
        message.time = time;
        message.properties = checksums::hash_from_text(properties);
        if (string1)
            std::strncpy(message.string1.data(), string1, kHudNoticeTextSize);
        if (string2)
            std::strncpy(message.string2.data(), string2, kHudNoticeTextSize);
        std::array<uint8_t, kHudNoticeSize> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        write_hud_notice(writer, message);
        desc.content_ = bytes.data();
        desc.byte_count_ = sizeof(bytes);
        desc.ident_ = kMsgHudNotice;
        if (important) {
            desc.queue_ = net::NetMessageInfo::kLaneOrdered;
            desc.group_id_ = kGroupGame;
        }
        for (const memory::ListNode* node = get_next_node(player_list(this));
             auto* player = data_of<PlayerRecord>(node); node = get_next_node(node))
            host_->put_on_outbox(player_handle(player), &desc);
        return;
    }
    script::Struct* params = new_params();
    const char* format = script::fetch_text(text);
    std::array<char, 256> copy{};
    std::array<char, 256> line{};
    std::strcpy(copy.data(), format);
    const char* shown = format;
    // "%s0" in the text stands for string 1.
    if (char* slot = std::strstr(copy.data(), kStringSlot)) {
        *slot = 0;
        std::strcpy(line.data(), copy.data());
        std::strcat(line.data(), string1);
        std::strcat(line.data(), slot + 3);
        shown = line.data();
    }
    params->append_word(kText, script::kQbvalString, shown);
    params->add_component_of_type(kId, script::kQbvalName, kNetPanelMsg);
    params->put_int(kMsgTime, time);
    get_run(kCreateNetPanelMessage, params);
    delete_params(params);
}

// 0x00537A10
void Hub::queue_script(int node, uint32_t object_id, uint32_t script)
{
    memory::ListNode* head = queued_script_list(this);
    for (const memory::ListNode* n = get_next_node(head); auto* queued = data_of<PendingScriptItem>(n);
         n = get_next_node(n)) {
        if (queued->own_script_ == script)
            return;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap_);
    auto* queued = static_cast<PendingScriptItem*>(memory::alloc_zeroed(sizeof(PendingScriptItem)));
    if (queued) { // (a null one is written to anyway, as in the original)
        queued->make_node(queued);
    }
    queued->list_link_ = node;
    queued->entity_key_ = object_id;
    queued->own_script_ = script;
    // Onto the end of the list.
    queued->prev = head->prev;
    queued->next = head;
    head->prev->next = queued;
    head->prev = queued;
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00537B30
void Hub::clear_queued_scripts()
{
    const memory::ListNode* node = get_next_node(queued_script_list(this));
    for (auto* queued = data_of<PendingScriptItem>(node); queued;) {
        node = get_next_node(node);
        auto* next = data_of<PendingScriptItem>(node);
        queued->destroy_record(1);
        queued = next;
    }
}

// 0x00537D90
void Hub::launch_queued_scripts()
{
    memory::Hub* memory = memory::Hub::get_pull();
    auto* sound = sound::g_audio_mixer;
    modules::Skate* skate = modules::g_skate_module;
    memory->enter_heap_scope(memory->bottom_up_heap_);
    static_cast<modules::Skate*>(skate)->begin_queued_scripts();
    const float volume = sound::fetch_master_loudness();
    sound->change_master_loudness(0.0f);
    get_run(kPreRunQueuedScripts);
    const memory::ListNode* node = get_next_node(queued_script_list(this));
    for (auto* queued = data_of<PendingScriptItem>(node); queued;) {
        node = get_next_node(node);
        auto* next = data_of<PendingScriptItem>(node);
        script::Script* script = script::spawn_script(queued->own_script_, nullptr, 0, nullptr, queued->list_link_, 0,
                                                      false, false, false, false);
        objects::Entity* object = memory::bits_as<objects::ObjectHubBase*>(objects::g_composite_registry)
                                      ->look_up_entity(queued->entity_key_);
        script->entity_.assign_value(object);
        while (script->tick_frame() != script::kScriptOutcomeFinished) {
        }
        script::kill_spawned_script(script);
        queued->destroy_record(1);
        queued = next;
    }
    get_run(kPostRunQueuedScripts);
    sound->change_master_loudness(volume);
    static_cast<modules::Skate*>(skate)->end_queued_scripts();
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00537F40
void Hub::toggle_pro_set(uint8_t bit, uint32_t param)
{
    net::NetMessageInfo desc;
    struct {
        uint32_t param;
        uint8_t bit;
        std::array<uint8_t, 3> pad; // (the original's stack)
    } message = {param, bit, {0, 0, 0}};
    script_flag_bits_ ^= 1u << (bit & 31);
    desc.byte_count_ = sizeof(message);
    desc.group_id_ = kGroupGame;
    desc.content_ = &message;
    desc.ident_ = kMsgFlipProSetting;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    net::AppHost* server = host_;
    for (const memory::ListNode* node = get_next_node(player_list(this)); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if (!is_own_player(player))
            server->put_on_outbox(player_handle(player), &desc);
    }
}

// 0x00539880
int Hub::count_team_players(int team)
{
    PlayerSearch search;
    int count = 0;
    for (auto* player = begin_player_walk(&search, false); player; player = step_player_walk(&search, false)) {
        if (player_team(player) == team)
            ++count;
    }
    for (const memory::ListNode* node = get_next_node(new_player_list(this)); auto* info = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        if (will_play(info) && info->team_ == team)
            ++count;
    }
    return count;
}

// 0x00539B00
int Hub::count_players()
{
    // The players' skater ids (zeroed first: the original's check below reads the byte after the last one noted).
    std::array<uint8_t, 12> ids = {};
    int noted = 0;
    int count = 0;
    for (const memory::ListNode* node = get_next_node(player_list(this)); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if (!is_observer(player) || (player->flag_bits_ & 8)) {
            if (const objects::Entity* skater = player_skater(player))
                core::at(ids, noted++) = static_cast<uint8_t>(get_skater_id(skater));
            ++count;
        }
    }
    for (const memory::ListNode* node = get_next_node(new_player_list(this)); auto* info = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        if (!will_play(info))
            continue;
        bool listed = false;
        for (int i = 0; i < noted && !listed; ++i)
            listed = info->entity_key_ == core::at(ids, noted); // (ids[noted], as in the original)
        if (!listed)
            ++count;
    }
    return count;
}

// 0x00539C70
int Hub::count_observers()
{
    int count = 0;
    for (const memory::ListNode* node = get_next_node(player_list(this)); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if (is_observer(player) && !player->is_pending_observer() && (!is_own_player(player) || host_mode_ == 0))
            ++count;
    }
    for (const memory::ListNode* node = get_next_node(new_player_list(this)); auto* info = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        const uint32_t flags = info->flag_bits_;
        if ((flags & 4) && !(flags & 8))
            ++count;
    }
    return count;
}

// 0x005399F0
void Hub::disconnect_clients()
{
    net::Hub* net = net::Hub::sole();
    for (int i = 0; i < 2; ++i) {
        if (!client_slot(i))
            continue;
        if (i == 0 && !client_app_->is_host_local())
            guest_admit_job_->drop_out();
        net->release_app(client_slot(i));
        client_slot(i) = nullptr;
    }
    if (spectator_pad_listener_) {
        delete spectator_pad_listener_;
        spectator_pad_listener_ = nullptr;
    }
    g_text_entry = false;
    if (g_query_reporting) {
        game_spy::get_qr2_shutdown(nullptr);
        g_query_reporting = false;
    }
    game_spy::cd_key_disconnect_all(1003);
    keyboard_job_->drop_out();
    score_broadcast_job_->drop_out();
    level_change_job_->drop_out();
    clear_queued_scripts();
    if (cleared_to_start_) {
        cleared_to_start_ = false;
        modules::Skate* skate = modules::g_skate_module;
        const unsigned int count = tally_nodes(skate->riders_.list_link_.next);
        for (unsigned int i = 0; i < count; ++i) {
            auto* skater = core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
            if (skater)
                skater->get_freeze(); // (the original's skater slot 11)
        }
    }
}

// 0x00539DA0
void Hub::send_tag_textures_to(PlayerRecord* player)
{
    PlayerSearch search;
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem || is_own_player(player))
        return;
    for (auto* other = begin_player_walk(&search, false); other; other = step_player_walk(&search, false)) {
        void* texture = other->get_graffiti_texture(); // its custom tag texture
        if (!texture || other == player || player_conn(player)->get_link_speed_class() == 1 || !other->has_custom_tag())
            continue;
        // (the logos are read from the receiving player's profile, as in the original)
        objects::RiderIdentity* profile = player->account_;
        uint32_t logos = profile_has_part(profile, kHatLogo) ? 1 : 0;
        if (profile_has_part(profile, kFrontLogo))
            logos |= 2;
        if (profile_has_part(profile, kBackLogo))
            logos |= 4;
        if (profile_has_part(profile, kDeckGraphic))
            logos |= 8;
        net::NetMessageInfo desc;
        auto skater_id = static_cast<uint8_t>(get_skater_id(player_skater(other)));
        desc.content_ = &skater_id;
        desc.ident_ = kMsgTagOwner;
        desc.byte_count_ = 1;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupTag;
        host_->put_on_outbox(player_handle(player), &desc);
        void* palette = texture_palette(texture);
        host_->send_bulk_to_conn(player_conn(player), kMsgTagPalette, texture_palette_size(texture), palette,
                                 kTagPaletteDesc, kGroupTag, false, true);
        void* pixels = texture_pixels(texture);
        host_->send_bulk_to_conn(player_conn(player), kMsgTagTexture, get_texture_size(texture), pixels,
                                 kTagTextureDesc, kGroupTag, false, true);
        desc.content_ = &logos;
        desc.ident_ = kMsgTagLogos;
        desc.byte_count_ = 4;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupTag;
        host_->put_on_outbox(player_handle(player), &desc);
    }
}

// 0x0053A040
bool Hub::prepare_local_face()
{
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem)
        return false;
    auto* profile = static_cast<modules::Skate*>(modules::g_skate_module)->fetch_account(0);
    gfx::FaceSkin* face = profile->appearance()->face_skin_; // its face texture (0x00576E60)
    if (!face || !face->is_sound())
        return false;
    memory::Hub* memory = memory::Hub::get_pull();
    if (flag_bits_ & 1) {
        auto* local = get_own_player();
        memory->enter_heap_scope(memory->net_misc_heap_);
        local->face_texture_data_ = static_cast<uint8_t*>(memory::alloc(kFaceSize + 1));
        memory::Hub::get_pull()->leave_heap_scope();
        local->face_texture_data_[0] = 0;
        face->write_to(local->face_texture_data_ + 1, kFaceSize);
    } else {
        net::ConnSearch search;
        if (client_app_->begin_conn_walk(&search)->get_link_speed_class() != 0)
            return false;
        memory->enter_heap_scope(memory->network_heap_);
        void* data = memory::alloc(kFaceSize);
        memory::Hub::get_pull()->leave_heap_scope();
        face->write_to(static_cast<uint8_t*>(data), kFaceSize);
        core::down_cast<net::Guest>(client_app_)
            ->send_bulk_to_host(kMsgFace, kFaceSize, data, kFaceDesc, kGroupFace, false, false);
        memory::dealloc(data);
    }
    return true;
}

// 0x0053A1B0
void Hub::send_faces_to(PlayerRecord* player)
{
    PlayerSearch search;
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem)
        return;
    for (auto* other = begin_player_walk(&search, false); other; other = step_player_walk(&search, false)) {
        if (other == player || player_conn(player)->get_link_speed_class() == 1)
            continue;
        if (!other->get_face_texture_data() && !(is_own_player(other) && prepare_local_face()))
            continue;
        host_->send_bulk_to_conn(player_conn(player), kMsgFace, kFaceSize + 1, other->get_face_texture_data(),
                                 kFaceDesc, kGroupFace, false, true);
    }
}

// 0x0053A310
void Hub::add_joining_player(JoinerRecord* info, int timeout)
{
    memory::ListNode* head = new_player_list(this);
    for (const memory::ListNode* node = get_next_node(head); auto* listed = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        if (listed->entity_key_ == info->entity_key_)
            return;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap_);
    void* record_memory = memory::alloc_zeroed(kNewPlayerSize);
    auto* record = record_memory ? memory::construct_in<JoinerRecord>(record_memory) : nullptr;
    std::strcpy(record->label_.data(), info->label_.data());
    record->conn_ = info->conn_;
    record->entity_key_ = info->entity_key_;
    record->flag_bits_ = info->flag_bits_;
    record->team_ = info->team_;
    record->profile_id_ = info->profile_id_;
    record->skill_points_ = info->skill_points_;
    record->score_ = info->score_;
    record->accumulated_score_ = info->accumulated_score_;
    record->eliminated_time_ = info->eliminated_time_;
    record->drive_control_mode_ = info->drive_control_mode_;
    uint32_t flags = record->flag_bits_;
    if ((flags & 0x10) && (flag_bits_ & 1))
        record->rejoin_group_ = host_->frame_count_;
    if (!(flags & 4) || (flags & 8)) {
        // The appearance, through a buffer.
        if (!memory::g_manager_ready)
            memory::ensure_manager();
        auto* buffer = static_cast<uint8_t*>(memory::Hub::get_pull()->carve(0x1000, 1, nullptr));
        info->account_->emit_into_buffer(buffer, 0x1000, 0);
        record->account_->decode_from_bytes(buffer, 0x1000);
        memory::Hub::get_pull()->dealloc(buffer);
    }
    if (flag_bits_ & 1) {
        flags = record->flag_bits_;
        record->add_at_frame_ = host_->frame_count_ + timeout;
        if (!(flags & 1)) {
            const char* name = info->label_.data();
            if (flags & 4)
                build_net_notice(true, (flags & 8) ? kNetMessageJoinPending : kNetMessageObserving, name, nullptr,
                                 nullptr, nullptr, false, 2000);
            else
                build_net_notice(false, kNetMessageJoining, name, nullptr, nullptr, nullptr, false, 2000);
        }
    } else {
        record->add_at_frame_ = client_app_->frame_count_ + timeout;
    }
    // Its node, onto the end of the list.
    // (a null one is written to anyway, as in the original)
    memory::ListNode* node = memory::ListNode::create(record);
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0053A580
PlayerRecord* Hub::next_cam_player()
{
    PlayerSearch search;
    auto can_watch = [](PlayerRecord* player) { return !player->is_scouting_level() && !player->is_eliminated(); };
    if (viewed_player_) {
        PlayerRecord* player = begin_player_walk(&search, false);
        while (player && player != viewed_player_)
            player = step_player_walk(&search, false);
        if (player) {
            // The ones after it; past the end, from the start again.
            for (player = step_player_walk(&search, false); player; player = step_player_walk(&search, false)) {
                if (can_watch(player))
                    return player;
            }
            viewed_player_ = nullptr;
            return next_cam_player();
        }
    }
    for (PlayerRecord* player = begin_player_walk(&search, false); player; player = step_player_walk(&search, false)) {
        if (can_watch(player))
            return player;
    }
    return nullptr;
}

Hub* g_online_hub;

// 0x00539610 (a task's code: the manager's flag task): in capture the flag, each flag a player (no observers)
// carries (flags 0x7800) put on the carrier's head - the flag object of the team it belongs to (TRG_CTF_Red / Blue /
// Green / Yellow; none: the task stops for the frame) gets the head bone's matrix, turned 90 degrees about x and
// raised 12, then the skater's own matrix; its position the bone's offset transformed, plus the skater's position.
void carried_flags_task(tasks::Task<Hub>*)
{
    Hub* manager = g_online_hub;
    PlayerSearch search;
    if (get_game_type() != kNetCtf)
        return;
    for (auto* player = manager->begin_player_walk(&search, false); player;
         player = manager->step_player_walk(&search, false)) {
        if (!player->is_carrying_flag()) // (carries a flag)
            continue;
        uint32_t name = 0;
        switch (player->get_flag_team()) {
        case 0:
            name = kTrgCtfRed;
            break;
        case 1:
            name = kTrgCtfBlue;
            break;
        case 2:
            name = kTrgCtfGreen;
            break;
        case 3:
            name = kTrgCtfYellow;
            break;
        default:
            name = 0;
            break;
        }
        auto* flag = core::down_cast<objects::GameEntity>(objects::find_object(name));
        if (!flag)
            return;
        auto* skater = core::down_cast<objects::GameEntity>(player_skater(player));
        auto* skeleton = raw::as<gfx::Rig>(core::down_cast<objects::BoneRigModule>(skater->fetch_module(kSkeleton))->bone_rig_);
        maths::Mat4* matrix = &flag->basis_frame_;
        skeleton->read_joint_matrix(kBoneHead, matrix); // the head bone's matrix
        maths::Mat4 turn{};
        d_3d_x_matrix_multiply(matrix, &maths::make_pitch_matrix(turn, kQuarterTurn), matrix);
        matrix->offset_local(maths::Vec4{0.0f, 12.0f, 0.0f, 0.0f}); // (moved along its own axes)
        const maths::Vec4 offset = core::at(matrix->row, 3);                  // its translation row, taken out
        core::at(matrix->row, 3) = maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
        const maths::Mat4* skater_matrix = &skater->basis_frame_;
        d_3d_x_matrix_multiply(matrix, matrix, skater_matrix);
        maths::Vec4 moved{};
        const maths::Vec4& out = skater_matrix->xform(moved, offset);
        auto* position = raw::as<float>(&flag->at_point_);
        std::memcpy(position, &out, 16);
        const auto* skater_position = raw::as<const float>(&skater->at_point_);
        for (int i = 0; i < 4; ++i)
            position[i] = skater_position[i] + position[i];
    }
}

// 0x005395C0 (no `this`): the manager released - its reference count (0x007CCE18) down, or (the last) destroyed.
void release_manager()
{
    uint32_t& references = g_game_net_references;
    if (references > 1) {
        --references;
        return;
    }
    Hub* manager = g_online_hub;
    if (manager) {
        manager->~Hub();
        memory::Hub::get_pull()->dealloc(manager);
    }
    g_online_hub = nullptr;
    references = 0;
}

} // namespace online
