// The network manager's player records (online::Hub, 0x00536820-0x00537F40): the players' and joining players'
// lists - walking, finding and removing them.

#include <cstdint>
#include "game/online/player_info.h"
#include "base/core/enum_ops.h"
#include "engine/gfx/engine_texture.h"
#include "base/core/compiler.h"
#include <cstring>
#include <iterator>
#include <span>
#include <utility>
#include <array>
#include <gsl/assert>

THUG2_MSVC_WARNING(disable : 4996) // (strcpy / strcat: the original's own copies into its fixed buffers)

#include "base/core/checksums.h"
#include "engine/pad_input/sdl_input.h"
#include "engine/pad_input/input_hub.h" // IWYU pragma: keep - the complete InputHandler type (deleted here)
#include "platform/gamespy.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "game/online/buddy_status.h"
#include "game/online/gamenet_game_cfuncs.h"
#include "engine/net/net_client.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/objects/entity.h"
#include "game/skate/profile_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/online/gamenet_handler_util.h"
#include "game/online/gamenet_lists.h"
#include "game/online/gamenet_manager.h"
#include "game/online/hub_roster.h"
#include "game/online/hub_script_queue.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/gfx/cas_options.h"
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
#include "game/online/server_list.h"
#include "game/online/score_list.h"
#include "game/online/buddy_man.h"
#include "game/online/stats_man.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/str.h"
#include "game/online/net_game_state.h"
#include "wire/message_ids.h"
#include "game/online/online_identity.h"

namespace online {

using namespace detail;

namespace {
constexpr PlayerFlag kPlayerSessionFlags = PlayerFlag::kReady | PlayerFlag::kEliminated; // cleared when a game starts
constexpr uint32_t kBitMask = 31;
constexpr int kLogoDeckGraphic = 8;
constexpr uint32_t kAppearanceBufferBytes = 0x1000;
constexpr int kJoinNoticeTime = 2000; // ms
constexpr float kLogoLiftHeight = 12.0F;

constexpr uint32_t kSkaterListFirst =
    0x30; // the skate module's skater list's first node (its head at +0x28; the original's +0x34)

// Messages.

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

bool is_head(const memory::ListNode* node) { return node->is_head(); }
// The node after `node`; null at the list's end (the head).
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next()) ? nullptr : node->next(); }
template <typename T>
T* data_of(const memory::ListNode* node)
{
    return node ? static_cast<T*>(node->data()) : nullptr;
}
// The nodes from `node` to the list's end (the original's loop: the head's mark tested before a null node).
unsigned int tally_nodes(const memory::ListNode* node)
{
    unsigned int count = 0;
    while (!is_head(node) && node) {
        node = node->next();
        ++count;
    }
    return count;
}
// --- 0x00539610-0x0053A580's ---
constexpr uint32_t kNewPlayerSize = 0x44; // JoinerRecord (constructor 0x005434D0)
constexpr uint16_t kFaceSize = 0x4498;
const char* const kFaceDesc = "Face Data"; // "Face Data"
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
constexpr float kQuarterTurn = 90.0F;                   // (make_pitch_matrix's angle, as the original passes it)

// A joining player who will play: flags 0x18, or not observing.
bool will_play(const JoinerRecord* info)
{
    const PlayerFlag flags = info->flag_bits();
    return core::any(flags & kPlayerCountedFlags) || !core::any(flags & PlayerFlag::kObserver);
}
net::Conn* player_conn(const PlayerRecord* player) { return player->conn(); }
// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool profile_has_part(void* profile, uint32_t part) { return objects::part_is_created_graphic(profile, part); }
// A custom tag texture's parts (render::BitmapTexture's virtual slots 25-28).
// (On the PC all four are the shared stubs: null / 0.)
void* texture_pixels(void* /*texture*/) { return render::BitmapTexture::get_pixels(); }
uint16_t get_texture_size(void* /*texture*/) { return static_cast<uint16_t>(render::BitmapTexture::get_pixels_size()); }
void* texture_palette(void* /*texture*/) { return render::BitmapTexture::get_palette(); }
uint16_t texture_palette_size(void* /*texture*/)
{
    return static_cast<uint16_t>(render::BitmapTexture::get_palette_size());
}
// D3DXMatrixMultiply (the D3DX library's, stdcall): out = a * b.
void d_3d_x_matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    maths::matrix_multiply(out, a, b);
}

// The first player (observers too) for whom the PlayerRecord member `test` is true (0 / 1 in EAX: tested as AL).
PlayerRecord* find_player(PlayerRoster* roster, int (PlayerRecord::*test)())
{
    for (const memory::ListNode* node = get_next_node(&roster->players()); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if ((player->*test)() != 0) {
            return player;
        }
    }
    return nullptr;
}

// A joining player's record deleted (JoinerRecord's destructor body 0x00543560, then freed).
void delete_new_player(JoinerRecord* player)
{
    player->~JoinerRecord();
    memory::Hub::dealloc(player);
}

} // namespace

// 0x00536820
void Hub::remove_all_new_players()
{
    memory::ListNode* node = roster_.joiners().next();
    if (is_head(node)) {
        return;
    }
    while (node) {
        auto* player = static_cast<JoinerRecord*>(node->data());
        memory::ListNode* next = is_head(node->next()) ? nullptr : node->next();
        if (host_) {
            net::AppHost::sever_link(player->conn());
        }
        memory::ListNode::destroy(node); // (0x005367E0)
        if (player) {
            delete_new_player(player);
        }
        node = next;
    }
}

// 0x00536B10
void Hub::remove_new_player(JoinerRecord* player)
{
    memory::ListNode* node = roster_.joiners().next();
    // (the original tests for the list's end before it tests for a null node)
    if (is_head(node) || !node) {
        return;
    }
    while (node->data() != player) {
        node = node->next();
        if (is_head(node) || !node) {
            return;
        }
    }
    net::Conn* conn = player->conn();
    if (host_ || conn) {
        net::AppHost::sever_link(conn);
    }
    memory::ListNode::destroy(node);
    delete_new_player(player);
}

// 0x00537010
PlayerRecord* PlayerRoster::step_player_walk(PlayerSearch* search, bool observers)
{
    PlayerSearch* sweep = search;
    auto* node = get_next_node(sweep->list_link_);
    sweep->list_link_ = node;
    auto* player = data_of<PlayerRecord>(node);
    if (!observers && player && is_observer(player)) {
        return step_player_walk(search, false);
    }
    return player;
}

// 0x005372C0
PlayerRecord* PlayerRoster::begin_player_walk(PlayerSearch* search, bool observers)
{
    PlayerSearch* sweep = search;
    memory::ListNode* head = &players();
    sweep->list_link_ = head;
    memory::ListNode* node = get_next_node(head);
    sweep->list_link_ = node;
    auto* player = data_of<PlayerRecord>(node);
    if (!observers && player && is_observer(player)) {
        return step_player_walk(search, false);
    }
    return player;
}

// 0x00537070
JoinerRecord* PlayerRoster::begin_joiner_walk(NewPlayerSearch* search)
{
    NewPlayerSearch* sweep = search;
    memory::ListNode* head = &joiners();
    sweep->list_link_ = head;
    memory::ListNode* node = get_next_node(head);
    sweep->list_link_ = node;
    return data_of<JoinerRecord>(node);
}

// (no address: the lists' generic next, folded at 0x004573D0 - see the header)
JoinerRecord* PlayerRoster::step_joiner_walk(NewPlayerSearch* search)
{
    NewPlayerSearch* sweep = search;
    memory::ListNode* next = sweep->list_link_->next();
    sweep->list_link_ = next->data() == memory::ListNode::head_marker() ? nullptr : next;
    return sweep->list_link_ ? static_cast<JoinerRecord*>(next->data()) : nullptr;
}

// 0x005370A0
JoinerRecord* PlayerRoster::fetch_incoming_player_by_obj(uint16_t id)
{
    const memory::ListNode* node = get_next_node(&joiners());
    for (auto* player = data_of<JoinerRecord>(node); player; player = data_of<JoinerRecord>(node)) {
        if (std::cmp_equal(static_cast<int8_t>(player->entity_key()), id)) {
            return player;
        }
        node = get_next_node(node);
    }
    return nullptr;
}

// 0x00537140
void Hub::set_ready_state(bool ready)
{
    if (ready == cleared_to_start_) {
        return;
    }
    cleared_to_start_ = ready;
    modules::Skate* skate = modules::Skate::sole();
    const unsigned int count = tally_nodes(skate->riders().list_link().next());
    for (unsigned int i = 0; i < count; ++i) {
        auto* skater = core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
        if (skater) { // (the original's skater slots 12 / 11)
            if (ready) {
                skater->unpause();
            } else {
                skater->get_freeze();
            }
        }
    }
}

// 0x00537320
PlayerRecord* PlayerRoster::get_own_player() { return find_player(this, &PlayerRecord::is_own_player); }

// 0x005373D0
PlayerRecord* PlayerRoster::get_host_player() { return find_player(this, &PlayerRecord::is_host_player); }

// 0x00537480
PlayerRecord* PlayerRoster::get_hill_ruler() { return find_player(this, &PlayerRecord::holds_crown); }

// 0x00537530
int PlayerRoster::count_other_players()
{
    PlayerSearch search;
    int count = 0;
    for (PlayerRecord* player = begin_player_walk(&search, false); player; player = step_player_walk(&search, false)) {
        if (player->has_finished_joining() == 0) { // (flags 0x21)
            ++count;
        }
    }
    return count;
}

// 0x00537620
void PlayerRoster::clear_roster()
{
    for (const memory::ListNode* node = get_next_node(&players()); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        player->flag_bits() &= ~kPlayerSessionFlags;
        player->set_accumulated_score(0);
        player->set_eliminated_time(0);
        player->num_placed() = 0;
        auto* skater = player_skater(player);
        if (skater) {
            // (not checked for null, as in the original)
            auto* score = core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore));
            if (score->points_held()) {
                score->points_held()->get_reset();
            }
        }
    }
}

// 0x005376F0
void Hub::build_net_notice(bool broadcast, uint32_t text, const char* string1, const char* string2, void* /*unused*/,
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
        if (string1) {
            std::strncpy(message.string1.data(), string1, kHudNoticeTextSize);
        }
        if (string2) {
            std::strncpy(message.string2.data(), string2, kHudNoticeTextSize);
        }
        std::array<uint8_t, kHudNoticeSize> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        write_hud_notice(writer, message);
        desc.set_content(bytes.data());
        desc.set_byte_count(sizeof(bytes));
        desc.set_ident(MessageId::kHudNotice);
        if (important) {
            desc.set_queue(net::NetMessageInfo::kLaneOrdered);
            desc.set_group_id(MessageGroup::kGame);
        }
        for (const memory::ListNode* node = get_next_node(&roster_.players());
             auto* player = data_of<PlayerRecord>(node); node = get_next_node(node)) {
            host_->put_on_outbox(player_handle(player), &desc);
        }
        return;
    }
    const script::StructPtr params = script::make_struct();
    const char* format = script::fetch_text(text);
    constexpr size_t kCopySize = 256;
    std::array<char, kCopySize> copy{};
    constexpr size_t kLineSize = 256;
    std::array<char, kLineSize> line{};
    text::copy_into(copy, format);
    const char* shown = format;
    // "%s0" in the text stands for string 1.
    if (char* slot = std::strstr(copy.data(), kStringSlot)) {
        *slot = 0;
        text::copy_into(line, copy.data());
        text::append_into(line, string1);
        text::append_into(line, std::next(slot, 3));
        shown = line.data();
    }
    params->append_word(kText, script::QbValueKind::kString, shown);
    params->add_component_of_type(kId, script::QbValueKind::kName, kNetPanelMsg);
    params->put_int(checksums::Name{kMsgTime}, time);
    get_run(kCreateNetPanelMessage, params.get());
}

// 0x00537A10
void ScriptQueue::queue_script(ScriptCall script_call)
{
    const auto node = script_call.node;
    const auto object_id = script_call.object_id_;
    const auto script = script_call.script;
    memory::ListNode* head = &queued_scripts_;
    for (const memory::ListNode* queued_node = get_next_node(head); auto* queued = data_of<PendingScriptItem>(queued_node);
         queued_node = get_next_node(queued_node)) {
        if (queued->own_script() == script) {
            return;
        }
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap());
    auto* queued = static_cast<PendingScriptItem*>(memory::alloc_zeroed(sizeof(PendingScriptItem)));
    if (queued) { // (a null one is written to anyway, as in the original)
        queued->make_node(queued);
    }
    Expects(queued != nullptr);
    queued->set_list_link(node);
    queued->set_entity_key(object_id);
    queued->set_own_script(script);
    // Onto the end of the list.
    queued->set_prev(head->prev());
    queued->set_next(head);
    head->prev()->set_next(queued);
    head->set_prev(queued);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00537B30
void ScriptQueue::clear_queued_scripts()
{
    const memory::ListNode* node = get_next_node(&queued_scripts_);
    for (auto* queued = data_of<PendingScriptItem>(node); queued;) {
        node = get_next_node(node);
        auto* next = data_of<PendingScriptItem>(node);
        queued->destroy_record(1);
        queued = next;
    }
}

// 0x00537D90
void ScriptQueue::launch_queued_scripts()
{
    memory::Hub* memory = memory::Hub::get_pull();
    modules::Skate* skate = modules::Skate::sole();
    memory->enter_heap_scope(memory->bottom_up_heap());
    skate->begin_queued_scripts();
    const float volume = sound::fetch_master_loudness();
    sound::AudioMixer::change_master_loudness(0.0F);
    get_run(kPreRunQueuedScripts);
    const memory::ListNode* node = get_next_node(&queued_scripts_);
    for (auto* queued = data_of<PendingScriptItem>(node); queued;) {
        node = get_next_node(node);
        auto* next = data_of<PendingScriptItem>(node);
        script::Script* script = script::spawn_script(queued->own_script(), nullptr, {.node = queued->list_link()});
        objects::Entity* object = objects::EntityHub::look_up_entity(queued->entity_key());
        script->entity().assign_value(object);
        while (script::host().tick_frame(script) != script::ScriptOutcome::kFinished) {
        }
        script::kill_spawned_script(script);
        queued->destroy_record(1);
        queued = next;
    }
    get_run(kPostRunQueuedScripts);
    sound::AudioMixer::change_master_loudness(volume);
    skate->end_queued_scripts();
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
    } message = {.param = param, .bit = bit, .pad = {0, 0, 0}};
    script_flag_bits_ ^= 1U << (bit & kBitMask);
    desc.set_byte_count(sizeof(message));
    desc.set_group_id(MessageGroup::kGame);
    desc.set_content(&message);
    desc.set_ident(MessageId::kFlipProSetting);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    net::AppHost* server = host_;
    for (const memory::ListNode* node = get_next_node(&roster_.players()); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if (!is_own_player(player)) {
            server->put_on_outbox(player_handle(player), &desc);
        }
    }
}

// 0x00539880
int PlayerRoster::count_team_players(int team)
{
    PlayerSearch search;
    int count = 0;
    for (auto* player = begin_player_walk(&search, false); player; player = step_player_walk(&search, false)) {
        if (player_team(player) == team) {
            ++count;
        }
    }
    for (const memory::ListNode* node = get_next_node(&joiners()); auto* info = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        if (will_play(info) && info->team() == team) {
            ++count;
        }
    }
    return count;
}

// 0x00539B00
int PlayerRoster::count_players()
{
    // The players' skater ids (zeroed first: the original's check below reads the byte after the last one noted).
    constexpr size_t kIdsSize = 12;
    std::array<uint8_t, kIdsSize> ids = {};
    int noted = 0;
    int count = 0;
    for (const memory::ListNode* node = get_next_node(&players()); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if (!is_observer(player) || core::any(player->flag_bits() & PlayerFlag::kPendingObserver)) {
            if (const objects::Entity* skater = player_skater(player)) {
                core::at(ids, noted++) = static_cast<uint8_t>(get_skater_id(skater));
            }
            ++count;
        }
    }
    for (const memory::ListNode* node = get_next_node(&joiners()); auto* info = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        if (!will_play(info)) {
            continue;
        }
        bool listed = false;
        for (int i = 0; i < noted && !listed; ++i) {
            listed = info->entity_key() == core::at(ids, noted); // (ids[noted], as in the original)
        }
        if (!listed) {
            ++count;
        }
    }
    return count;
}

// 0x00539C70
int Hub::count_observers()
{
    int count = 0;
    for (const memory::ListNode* node = get_next_node(&roster_.players()); auto* player = data_of<PlayerRecord>(node);
         node = get_next_node(node)) {
        if (is_observer(player) && (player->is_pending_observer() == 0) &&
            (!is_own_player(player) || host_mode_ == 0)) {
            ++count;
        }
    }
    for (const memory::ListNode* node = get_next_node(&roster_.joiners()); auto* info = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        const PlayerFlag flags = info->flag_bits();
        if (core::any(flags & PlayerFlag::kObserver) && !core::any(flags & PlayerFlag::kPendingObserver)) {
            ++count;
        }
    }
    return count;
}

// 0x005399F0
void Hub::disconnect_clients()
{
    net::Hub* net = net::Hub::sole();
    for (int i = 0; i < 2; ++i) {
        if (!client_slot(i)) {
            continue;
        }
        if (i == 0 && !client_app_->is_host_local()) {
            tasks_.guest_admit_job()->drop_out();
        }
        net->release_app(client_slot(i));
        client_slot(i) = nullptr;
    }
    if (spectator_pad_listener_) {
        delete_spectator_pad_listener();
    }
    sio::text_entry_flag() = false;
    if (net_game_state().query_reporting()) {
        game_spy::get_qr2_shutdown(nullptr);
        net_game_state().set_query_reporting(false);
    }
    game_spy::cd_key_disconnect_all(net::kGameSpyGameId);
    tasks_.keyboard_job()->drop_out();
    tasks_.score_broadcast_job()->drop_out();
    tasks_.level_change_job()->drop_out();
    script_queue_.clear_queued_scripts();
    if (cleared_to_start_) {
        cleared_to_start_ = false;
        modules::Skate* skate = modules::Skate::sole();
        const unsigned int count = tally_nodes(skate->riders().list_link().next());
        for (unsigned int i = 0; i < count; ++i) {
            auto* skater = core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
            if (skater) {
                skater->get_freeze(); // (the original's skater slot 11)
            }
        }
    }
}

// 0x00539DA0
void Hub::send_tag_textures_to(PlayerRecord* player)
{
    PlayerSearch search;
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem || is_own_player(player)) {
        return;
    }
    for (auto* other = roster_.begin_player_walk(&search, false); other;
         other = roster_.step_player_walk(&search, false)) {
        void* texture = other->get_graffiti_texture(); // its custom tag texture
        if (!texture || other == player || player_conn(player)->get_link_speed_class() == 1 ||
            !other->has_custom_tag()) {
            continue;
        }
        // (the logos are read from the receiving player's profile, as in the original)
        objects::RiderIdentity* profile = player->account();
        uint32_t logos = profile_has_part(profile, kHatLogo) ? 1 : 0;
        if (profile_has_part(profile, kFrontLogo)) {
            logos |= 2;
        }
        if (profile_has_part(profile, kBackLogo)) {
            logos |= 4;
        }
        if (profile_has_part(profile, kDeckGraphic)) {
            logos |= kLogoDeckGraphic;
        }
        net::NetMessageInfo desc;
        auto skater_id = static_cast<uint8_t>(get_skater_id(player_skater(other)));
        desc.set_content(&skater_id);
        desc.set_ident(MessageId::kGraffitiPlayer);
        desc.set_byte_count(1);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(MessageGroup::kGraffiti);
        host_->put_on_outbox(player_handle(player), &desc);
        void* palette = texture_palette(texture);
        host_->send_bulk_to_conn(player_conn(player), {.msg_id_ = MessageId::kGraffitiPalette, .size = texture_palette_size(texture)}, palette,
                                 kTagPaletteDesc, MessageGroup::kGraffiti, false, true);
        void* pixels = texture_pixels(texture);
        host_->send_bulk_to_conn(player_conn(player), {.msg_id_ = MessageId::kGraffitiTexture, .size = get_texture_size(texture)}, pixels,
                                 kTagTextureDesc, MessageGroup::kGraffiti, false, true);
        desc.set_content(&logos);
        desc.set_ident(MessageId::kGraffitiParts);
        desc.set_byte_count(4);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(MessageGroup::kGraffiti);
        host_->put_on_outbox(player_handle(player), &desc);
    }
}

// 0x0053A040
bool Hub::prepare_local_face()
{
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem) {
        return false;
    }
    auto* profile = modules::Skate::fetch_account(0);
    gfx::FaceSkin* face = profile->appearance()->face_skin(); // its face texture (0x00576E60)
    if (!face || !face->is_sound()) {
        return false;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    if ((flag_bits_ & 1) != 0) {
        auto* local = roster_.get_own_player();
        memory->enter_heap_scope(memory->net_misc_heap());
        local->assign_face_texture_data(static_cast<uint8_t*>(memory::alloc(kFaceSize + 1)));
        memory::Hub::get_pull()->leave_heap_scope();
        const std::span<uint8_t> stored(local->face_texture_data(), kFaceSize + 1U);
        core::at(stored, 0) = 0;
        face->write_to(stored.subspan(1));
    } else {
        net::ConnSearch search;
        if (client_app_->begin_conn_walk(&search)->get_link_speed_class() != 0) {
            return false;
        }
        memory->enter_heap_scope(memory->network_heap());
        void* data = memory::alloc(kFaceSize);
        memory::Hub::get_pull()->leave_heap_scope();
        face->write_to({static_cast<uint8_t*>(data), kFaceSize});
        core::down_cast<net::Guest>(client_app_)
            ->send_bulk_to_host(MessageId::kFaceTexture, kFaceSize, data, kFaceDesc, MessageGroup::kFaceData, false, false);
        memory::dealloc(data);
    }
    return true;
}

// 0x0053A1B0
void Hub::send_faces_to(PlayerRecord* player)
{
    PlayerSearch search;
    if (net::Hub::sole()->get_net_link_kind() == net::Hub::kConnectionModem) {
        return;
    }
    for (auto* other = roster_.begin_player_walk(&search, false); other;
         other = roster_.step_player_walk(&search, false)) {
        if (other == player || player_conn(player)->get_link_speed_class() == 1) {
            continue;
        }
        if (!other->get_face_texture_data() && !(is_own_player(other) && prepare_local_face())) {
            continue;
        }
        host_->send_bulk_to_conn(player_conn(player), {.msg_id_ = MessageId::kFaceTexture, .size = kFaceSize + 1}, other->get_face_texture_data(),
                                 kFaceDesc, MessageGroup::kFaceData, false, true);
    }
}

// 0x0053A310
void Hub::add_joining_player(JoinerRecord* info, int timeout)
{
    memory::ListNode* head = &roster_.joiners();
    for (const memory::ListNode* node = get_next_node(head); auto* listed = data_of<JoinerRecord>(node);
         node = get_next_node(node)) {
        if (listed->entity_key() == info->entity_key()) {
            return;
        }
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap());
    void* record_memory = memory::alloc_zeroed(kNewPlayerSize);
    auto* record = record_memory ? memory::construct_in<JoinerRecord>(record_memory) : nullptr;
    Expects(record != nullptr);
    text::copy_into(record->label(), info->label().data());
    record->set_conn(info->conn());
    record->set_entity_key(info->entity_key());
    record->flag_bits() = info->flag_bits();
    record->set_team(info->team());
    record->set_profile_id(info->profile_id());
    record->set_skill_points(info->skill_points());
    record->set_score(info->score());
    record->set_accumulated_score(info->accumulated_score());
    record->set_eliminated_time(info->eliminated_time());
    record->set_drive_control_mode(info->drive_control_mode());
    PlayerFlag flags = record->flag_bits();
    if (core::any(flags & PlayerFlag::kRejoining) && ((flag_bits_ & 1) != 0)) {
        record->set_rejoin_group(host_->frame_count());
    }
    if (!core::any(flags & PlayerFlag::kObserver) || core::any(flags & PlayerFlag::kPendingObserver)) {
        // The appearance, through a buffer.
        if (!memory::manager_ready_flag()) {
            memory::ensure_manager();
        }
        auto* buffer = static_cast<uint8_t*>(memory::Hub::get_pull()->carve(kAppearanceBufferBytes, 1, nullptr));
        info->account()->emit_into_buffer(buffer, kAppearanceBufferBytes, 0);
        record->account()->decode_from_bytes(buffer, kAppearanceBufferBytes);
        memory::Hub::dealloc(buffer);
    }
    if ((flag_bits_ & 1) != 0) {
        flags = record->flag_bits();
        record->set_add_at_frame(host_->frame_count() + timeout);
        if (!core::any(flags & PlayerFlag::kLocal)) {
            const char* name = info->label().data();
            if (core::any(flags & PlayerFlag::kObserver)) {
                build_net_notice(true, core::any(flags & PlayerFlag::kPendingObserver) ? kNetMessageJoinPending : kNetMessageObserving, name, nullptr,
                                 nullptr, nullptr, false, kJoinNoticeTime);
            } else {
                build_net_notice(false, kNetMessageJoining, name, nullptr, nullptr, nullptr, false, kJoinNoticeTime);
            }
        }
    } else {
        record->set_add_at_frame(client_app_->frame_count() + timeout);
    }
    // Its node, onto the end of the list.
    // (a null one is written to anyway, as in the original)
    memory::ListNode* node = memory::ListNode::create(record);
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0053A580
PlayerRecord* Hub::next_cam_player()
{
    PlayerSearch search;
    auto can_watch = [](PlayerRecord* player) { return !player->is_scouting_level() && !player->is_eliminated(); };
    if (viewed_player_) {
        PlayerRecord* player = roster_.begin_player_walk(&search, false);
        while (player && player != viewed_player_) {
            player = roster_.step_player_walk(&search, false);
        }
        if (player) {
            // The ones after it; past the end, from the start again.
            for (player = roster_.step_player_walk(&search, false); player;
                 player = roster_.step_player_walk(&search, false)) {
                if (can_watch(player)) {
                    return player;
                }
            }
            viewed_player_ = nullptr;
            return next_cam_player();
        }
    }
    for (PlayerRecord* player = roster_.begin_player_walk(&search, false); player;
         player = roster_.step_player_walk(&search, false)) {
        if (can_watch(player)) {
            return player;
        }
    }
    return nullptr;
}

// 0x00539610 (a task's code: the manager's flag task): in capture the flag, each flag a player (no observers)
// carries (flags 0x7800) put on the carrier's head - the flag object of the team it belongs to (TRG_CTF_Red / Blue /
// Green / Yellow; none: the task stops for the frame) gets the head bone's matrix, turned 90 degrees about x and
// raised 12, then the skater's own matrix; its position the bone's offset transformed, plus the skater's position.
void carried_flags_task(tasks::Task<Hub>* /*unused*/)
{
    Hub* manager = net_game_state().online_hub();
    PlayerSearch search;
    if (get_game_type() != kNetCtf) {
        return;
    }
    for (auto* player = manager->roster().begin_player_walk(&search, false); player;
         player = manager->roster().step_player_walk(&search, false)) {
        if (player->is_carrying_flag() == 0) { // (carries a flag)
            continue;
        }
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
        if (!flag) {
            return;
        }
        auto* skater = core::down_cast<objects::GameEntity>(player_skater(player));
        gfx::Rig* skeleton = core::down_cast<objects::BoneRigModule>(skater->fetch_module(kSkeleton))->bone_rig();
        maths::Mat4* matrix = &flag->basis_frame();
        skeleton->read_joint_matrix(kBoneHead, matrix); // the head bone's matrix
        maths::Mat4 turn{};
        d_3d_x_matrix_multiply(matrix, &maths::make_pitch_matrix(turn, kQuarterTurn), matrix);
        matrix->offset_local(
            maths::Vec4{.x = 0.0F, .y = kLogoLiftHeight, .z = 0.0F, .w = 0.0F}); // (moved along its own axes)
        const maths::Vec4 offset = core::at(matrix->row, 3);                  // its translation row, taken out
        core::at(matrix->row, 3) = maths::Vec4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        const maths::Mat4* skater_matrix = &skater->basis_frame();
        d_3d_x_matrix_multiply(matrix, matrix, skater_matrix);
        maths::Vec4 moved{};
        const maths::Vec4& out = skater_matrix->xform(moved, offset);
        flag->at_point() = out;
        for (int i = 0; i < 4; ++i) {
            flag->at_point()[i] = skater->at_point()[i] + flag->at_point()[i];
        }
    }
}

// 0x005395C0 (no `this`): the manager released - its reference count (0x007CCE18) down, or (the last) destroyed.
void release_manager()
{
    uint32_t& references = net_game_state().game_net_references();
    if (references > 1) {
        --references;
        return;
    }
    Hub* manager = net_game_state().online_hub();
    if (manager) {
        manager->~Hub();
        memory::Hub::dealloc(manager);
    }
    net_game_state().set_online_hub(nullptr);
    references = 0;
}

void Hub::delete_spectator_pad_listener()
{
    delete spectator_pad_listener_;
    spectator_pad_listener_ = nullptr;
}

} // namespace online
