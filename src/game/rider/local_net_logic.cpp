// The skater's local network logic component: its update to the server (0x00512EA0, 0x005131A0), the rest of it
// (0x00512E30-0x00512E94, 0x00513630-0x005136EA) and the graffiti steal message's handler (0x005136F0). See
// local_net_logic.h.

#include "game/rider/local_net_logic.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"
#include "engine/net/thugpro_compat.h"
#include "engine/script/cscript.h"
#include "formats/byte_stream.h"
#include "wire/skater_messages.h"
#include "engine/script/param_struct.h"
#include "game/rider/end_run.h"
#include "game/rider/rider.h"
#include "engine/util/bitstream.h"
#include "game/skate/skate_module.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/skater_camera_component.h"
#include "engine/objects/rail_manager.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/net/net_game_state.h"

namespace objects {

namespace {
// NOT ORIGINAL grouping - local_net_logic.cpp's state (the original's globals). Made on first use, never destroyed
// (local_net_logic_state()).
struct LocalNetLogicState {
    maths::Vec4 local_net_position_{};
    uint32_t local_net_guard_{};
};

LocalNetLogicState& local_net_logic_state()
{
    static core::NoDestroy<LocalNetLogicState> state;
    return state.get();
}

namespace thugpro = online::thugpro;

constexpr uint32_t kComponent6c = 0xB1D5A3E1; // the component whose +0x18 goes out as the 0x80 field's 3 bits


constexpr uint32_t kSkaterLocalNetLogic = 0x7CD3E6D5;
constexpr uint32_t kSkaterState = 0x43686585, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kString0 = 0xA4B08520, kGraffitiStealYou = 0x0B3FF911, kGraffitiStealOther = 0x4D7F6FFA;

// modules::Skate (0x007CE478; not rewritten yet): a multiplayer game (0x005651F0), a skater by its id (0x00566600).
bool is_versus_session() { return modules::Skate::is_multiplayer() != 0; }
Rider* rider_for_slot(uint32_t id) { return modules::Skate::sole()->get_rider_by_number(id); }
// The network manager's first local player and its run count (+0x38, which the manager's code calls the game id).
void* find_own_player_record() { return online::online_hub()->get_own_player(); }
uint8_t run_count() { return online::online_hub()->get_game_id(); }

// The network manager's client app for a skater (0x00532EA0: its +8 + 4 * index) and whether this machine is the
// server (0x00533F90: its flags' bit 0).
net::AppHost* client_app(int index) { return online::online_hub()->fetch_patron(index); }
bool get_on_server() { return online::online_hub()->get_on_server(); }

// What the update reads of the skater: its orientation as angles, the core component's state and controls, the two
// other components' values.
struct UpdateState {
    maths::Vec4 angles;
    uint8_t state, flag74, value_7c_, flag75, flag76;
    uint32_t controls, value_6c_, value80;
};

int16_t pack_angle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * 4096.0)); }

// (shared by both: the original repeats it in each)
void read_update_state(RiderHostSync* self, ObjectModule* component_6c, UpdateState& out)
{
    self->entity()->display_matrix().get_euler_angles(out.angles);
    const objects::RiderPoseState* core = self->pose();
    out.state = static_cast<uint8_t>(core->cur_phase());
    out.value_7c_ = core->surface_type();
    out.flag74 = core->in_trick_move();
    out.flag75 = core->physics_mode() == 1 ? 1 : 0;
    out.flag76 = core->in_vehicle();
    const objects::GrindPoint* rail = self->board()->rail_node();
    out.value80 = rail ? rail->label() : 0;
    // A bit per state flag (the state component's 24 timed flags): set while it's on.
    uint32_t controls = 0;
    for (int k = 0; k < 24; ++k) {
        if (core::at(core->timed_flags(), k).on)
            controls |= 1u << k;
        else
            controls &= ~(1u << k);
    }
    out.controls = controls;
    uint32_t value = 0;
    out.value_6c_ = *core::down_cast<RiderSessionEnd>(component_6c)->fetch_bits(&value); // (SkaterEndRun)
}
} // namespace

// 0x00512EA0
uint32_t RiderHostSync::fetch_sync_field_mask()
{
    uint32_t flags = 0;
    const net::AppHost* app = client_app(core::down_cast<Rider>(entity())->view_slot());
    ObjectModule* component_6c = entity()->fetch_module(kComponent6c);
    const bool force = get_on_server() || static_cast<uint32_t>(app->frame_count()) % 15 == 0;
    UpdateState now{};
    read_update_state(this, component_6c, now);
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        const int16_t value =
            k == 1 ? thugpro::pack_height(entity()->at_point().y) : thugpro::pack_horizontal(entity()->at_point()[k]);
        if (value != core::at(sent_pos_, k) || force)
            flags |= 1u << k;
    }
    for (int k = 0; k < 3; ++k) {
        const int16_t value = pack_angle(now.angles[k]);
        if (value != core::at(sent_angles_, k) || force) {
            core::at(sent_angles_, k) = value;
            flags |= 8u << k;
        }
    }
    if (now.state != sent_state_ || now.flag74 != sent_flag74_ || now.value_7c_ != sent_value_7c_ ||
        now.flag75 != sent_flag75_ || now.flag76 != sent_flag76_ || force)
        flags |= 0x40;
    if (now.controls != sent_controls_ || now.value_6c_ != sent_value_6c_ || force)
        flags |= 0x80;
    if (now.value80 != sent_value80_) // (not forced)
        flags |= 0x100;
    return flags;
}

// 0x005131A0
void RiderHostSync::send_update()
{
    net::NetMessageInfo desc;
    util::PackedBitBuffer stream;
    auto* app = core::down_cast<net::Guest>(client_app(core::down_cast<Rider>(entity())->view_slot()));
    ObjectModule* component_6c = entity()->fetch_module(kComponent6c);
    const bool on_server = get_on_server();
    if (!on_server && static_cast<uint32_t>(app->frame_count()) % 15 != 0) {
        // Between the 15th frames: only once the connection's send interval has gone by.
        net::ConnSearch search;
        net::Conn* conn = app->begin_conn_walk(&search);
        if (app->clock() - last_transmit_at_ < conn->get_transmit_period())
            return;
    }
    last_transmit_at_ = app->clock();
    std::array<uint8_t, 0x400> buffer{};
    stream.bind_output_buffer(buffer.data(), 0x400);
    UpdateState now{};
    read_update_state(this, component_6c, now);
    stream.write_bits(app->clock(), 32);
    const uint32_t flags = fetch_sync_field_mask();
    stream.write_bits(flags, 9);
    if (on_server) {
        // (a copy of the server's own skater's position, written and never read: the original's static)
        uint32_t& guard = local_net_logic_state().local_net_guard_;
        if (!(guard & 1))
            guard |= 1;
        local_net_logic_state().local_net_position_ = entity()->at_point();
    }
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        const int16_t value =
            k == 1 ? thugpro::pack_height(entity()->at_point().y) : thugpro::pack_horizontal(entity()->at_point()[k]);
        if (flags & (1u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        core::at(sent_pos_, k) = value;
    }
    for (int k = 0; k < 3; ++k) {
        const int16_t value = pack_angle(now.angles[k]);
        if (flags & (8u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        core::at(sent_angles_, k) = value;
    }
    // (the bytes go out sign-extended: write_bits doesn't mask them, as the original)
    auto signed_byte = [](uint8_t value) {
        return static_cast<uint32_t>(static_cast<int>(static_cast<int8_t>(value)));
    };
    if (flags & 0x40) {
        sent_value_7c_ = now.value_7c_;
        sent_flag75_ = now.flag75;
        sent_state_ = now.state;
        sent_flag74_ = now.flag74;
        sent_flag76_ = now.flag76;
        stream.write_bits(signed_byte(now.flag74 ? static_cast<uint8_t>(now.state | 0x80) : now.state), 4);
        stream.write_bits(signed_byte(now.value_7c_), 6);
        stream.write_bits(signed_byte(now.flag75), 1);
        stream.write_bits(signed_byte(now.flag76), 1);
    }
    if (flags & 0x80) {
        sent_controls_ = now.controls;
        sent_value_6c_ = now.value_6c_;
        stream.write_bits(now.controls, 5);
        stream.write_bits(now.value_6c_, 3);
    }
    if (flags & 0x100) {
        sent_value80_ = now.value80;
        stream.write_bits(now.value80, 32);
    }
    stream.drain();
    const int size = stream.get_byte_count();
    desc.set_content(buffer.data());
    desc.set_ident(0x23);
    desc.set_byte_count(static_cast<uint16_t>(size));
    desc.set_singular(true);
    desc.set_rank(0x81);
    app->send_to_host(&desc);
}

// 0x00513630
gsl::owner<ObjectModule*> RiderHostSync::fabricate() { return new RiderHostSync; }

// (the constructor: inlined in fabricate in the original)
RiderHostSync::RiderHostSync()
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set twice, as the original does
    sent_controls_ = 0;
    set_kind_tag(kSkaterLocalNetLogic);
    sent_controls_ = 0;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00512E30 (0x00512E40 the deleting one)
RiderHostSync::~RiderHostSync() = default;

// 0x00512E70
void RiderHostSync::seal()
{
    pose_ = core::down_cast<RiderPoseState>(entity()->fetch_module(kSkaterState));
    board_ = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
}

// 0x005136C0
void RiderHostSync::tick_frame()
{
    if (is_versus_session() && find_own_player_record())
        send_update();
}

// 0x005136F0
int RiderHostSync::handle_graffiti_steal(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body(), context->body_size());
    online::TagStolenMessage message;
    // (NOT ORIGINAL: too short - dropped)
    if (!online::read_tag_stolen(reader, &message) || message.game_id != run_count())
        return net::kResultConsumed;
    // (the thief first, as the senders write it: score_trick_objects / PointTally::note_tagged_prop)
    const uint32_t thief_id = message.thief_id;
    const uint32_t victim_id = message.victim_id;
    auto* skater = static_cast<Rider*>(context->content());
    Rider* thief = rider_for_slot(thief_id);
    if (!thief)
        return net::kResultPassOn;
    Rider* victim = rider_for_slot(victim_id);
    if (!victim)
        return net::kResultPassOn;
    uint32_t script = 0;
    Rider* other = nullptr;
    if (skater->ident() == thief_id) {
        script = kGraffitiStealYou;
        other = victim;
    } else if (skater->ident() == victim_id) {
        script = kGraffitiStealOther;
        other = thief;
    } else {
        return net::kResultPassOn;
    }
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    if (params)
        params = new (params) script::Struct;
    params->get_clear(); // (on the new structure, as the original)
    params->append_word(kString0, script::kQbvalString, other->fetch_shown_name());
    script::launch_script(script, params, skater, false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    return net::kResultPassOn;
}

} // namespace objects
