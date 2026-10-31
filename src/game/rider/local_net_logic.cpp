// The skater's local network logic component: its update to the server (0x00512EA0, 0x005131A0), the rest of it
// (0x00512E30-0x00512E94, 0x00513630-0x005136EA) and the graffiti steal message's handler (0x005136F0). See
// local_net_logic.h.

#include "game/rider/local_net_logic.h"

#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"
#include "engine/net/thugpro_compat.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/end_run.h"
#include "game/rider/rider.h"
#include "engine/util/bitstream.h"
#include "game/skate/skate_module.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/skater_camera_component.h"

namespace objects {

maths::Vec4 g_local_net_position;
uint32_t g_local_net_guard;

namespace {

namespace thugpro = online::thugpro;

constexpr uint32_t kComponent6c = 0xB1D5A3E1; // the component whose +0x18 goes out as the 0x80 field's 3 bits

online::Hub* net_session_slot() { return online::g_online_hub; }

extern void** const kVtable;
constexpr uint32_t kSkaterLocalNetLogic = 0x7CD3E6D5;
constexpr uint32_t kSkaterState = 0x43686585, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kString0 = 0xA4B08520, kGraffitiStealYou = 0x0B3FF911, kGraffitiStealOther = 0x4D7F6FFA;

// modules::Skate (0x007CE478; not rewritten yet): a multiplayer game (0x005651F0), a skater by its id (0x00566600).
bool is_versus_session() { return modules::Skate::is_multiplayer() != 0; }
Rider* rider_for_slot(uint32_t id) { return modules::Skate::sole()->get_rider_by_number(id); }
// The network manager's first local player and its run count (+0x38, which the manager's code calls the game id).
void* find_own_player_record() { return net_session_slot()->get_own_player(); }
uint8_t run_count() { return net_session_slot()->get_game_id(); }

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// The network manager's client app for a skater (0x00532EA0: its +8 + 4 * index) and whether this machine is the
// server (0x00533F90: its flags' bit 0).
net::AppHost* client_app(int index) { return net_session_slot()->fetch_patron(index); }
bool get_on_server() { return net_session_slot()->get_on_server(); }

// What the update reads of the skater: its orientation as angles, the core component's state and controls, the two
// other components' values.
struct UpdateState {
    maths::Vec4 angles;
    uint8_t state, flag74, value_7c_, flag75, flag76;
    uint32_t controls, value_6c_, value80;
};

int16_t pack_angle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * 4096.0)); }

} // namespace

// (shared by both: the original repeats it in each)
static void read_update_state(RiderHostSync* self, ObjectModule* component_6c, UpdateState& out)
{
    reinterpret_cast<maths::Mat4*>(reinterpret_cast<uint8_t*>(self->entity_) + 0xC0)->get_euler_angles(out.angles);
    uint8_t* core = self->core_;
    out.state = prop<uint8_t>(core, 0x18);
    out.value_7c_ = prop<uint8_t>(core, 0xE1);
    out.flag74 = prop<uint8_t>(core, 0xE0);
    out.flag75 = prop<int>(core, 0x1C) == 1 ? 1 : 0;
    out.flag76 = prop<uint8_t>(core, 0x118);
    uint8_t* board = prop<uint8_t*>(self->board_, 0x22C);
    out.value80 = board ? prop<uint32_t>(board, 0x4C) : 0;
    // A bit per control (the core component's 24, 8 bytes apart from +0x20): set while it's held.
    uint32_t controls = 0;
    for (int k = 0; k < 24; ++k) {
        if (prop<uint8_t>(core, 0x20 + 8 * k))
            controls |= 1u << k;
        else
            controls &= ~(1u << k);
    }
    out.controls = controls;
    uint32_t value;
    out.value_6c_ = *static_cast<RiderSessionEnd*>(component_6c)->fetch_bits(&value); // (SkaterEndRun)
}

// 0x00512EA0
uint32_t RiderHostSync::fetch_sync_field_mask()
{
    uint32_t flags = 0;
    net::AppHost* app = client_app(prop<int>(entity_, 0x2BC));
    ObjectModule* component_6c = entity_->fetch_module(kComponent6c);
    bool force = get_on_server() || static_cast<uint32_t>(app->frame_count_) % 15 == 0;
    UpdateState now;
    read_update_state(this, component_6c, now);
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value =
            k == 1 ? thugpro::pack_height(entity_->at_point_.y) : thugpro::pack_horizontal((&entity_->at_point_.x)[k]);
        if (value != sent_pos_[k] || force)
            flags |= 1u << k;
    }
    for (int k = 0; k < 3; ++k) {
        int16_t value = pack_angle((&now.angles.x)[k]);
        if (value != sent_angles_[k] || force) {
            sent_angles_[k] = value;
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
    desc.make_new();
    util::PackedBitBuffer stream;
    auto* app = static_cast<net::Guest*>(client_app(prop<int>(entity_, 0x2BC)));
    ObjectModule* component_6c = entity_->fetch_module(kComponent6c);
    bool on_server = get_on_server();
    if (!on_server && static_cast<uint32_t>(app->frame_count_) % 15 != 0) {
        // Between the 15th frames: only once the connection's send interval has gone by.
        net::ConnSearch search;
        net::Conn* conn = app->begin_conn_walk(&search);
        if (app->clock_ - last_transmit_at_ < conn->get_transmit_period())
            return;
    }
    last_transmit_at_ = app->clock_;
    uint8_t buffer[0x400];
    stream.bind_output_buffer(buffer, 0x400);
    UpdateState now;
    read_update_state(this, component_6c, now);
    stream.write_bits(app->clock_, 32);
    uint32_t flags = fetch_sync_field_mask();
    stream.write_bits(flags, 9);
    if (on_server) {
        // (a copy of the server's own skater's position, written and never read: the original's static)
        uint32_t& guard = g_local_net_guard;
        if (!(guard & 1))
            guard |= 1;
        g_local_net_position = entity_->at_point_;
    }
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value =
            k == 1 ? thugpro::pack_height(entity_->at_point_.y) : thugpro::pack_horizontal((&entity_->at_point_.x)[k]);
        if (flags & (1u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        sent_pos_[k] = value;
    }
    for (int k = 0; k < 3; ++k) {
        int16_t value = pack_angle((&now.angles.x)[k]);
        if (flags & (8u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        sent_angles_[k] = value;
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
    int size = stream.get_byte_count();
    desc.content_ = buffer;
    desc.ident_ = 0x23;
    desc.byte_count_ = static_cast<uint16_t>(size);
    desc.singular_ = true;
    desc.rank_ = 0x81;
    app->send_to_host(&desc);
}

// 0x00513630
ObjectModule* __cdecl RiderHostSync::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(RiderHostSync));
    if (!memory)
        return nullptr;
    auto* component = static_cast<RiderHostSync*>(memory);
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    component->sent_controls_ = 0;
    component->sent_value_6c_ = 0;
    component->core_ = nullptr;
    component->board_ = nullptr;
    component->last_transmit_at_ = 0;
    component->sent_value_7c_ = 0xFF;
    component->kind_tag_ = kSkaterLocalNetLogic;
    component->sent_controls_ = 0;
    component->sent_flag74_ = 0xFF;
    component->sent_state_ = 0;
    component->sent_value80_ = 0;
    return component;
}

// 0x00512E30
void RiderHostSync::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x00512E40
RiderHostSync* RiderHostSync::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00512E70
void RiderHostSync::seal()
{
    core_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterState));
    board_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterCorePhysics));
}

// 0x005136C0
void RiderHostSync::tick_frame()
{
    if (is_versus_session() && find_own_player_record())
        send_update();
}

// 0x005136F0
int __cdecl RiderHostSync::handle_graffiti_steal(net::InboundMsgCtx* context)
{
    const char* message = context->body_;
    if (static_cast<uint8_t>(message[0]) != run_count())
        return net::kResultConsumed;
    uint32_t victim_id = *reinterpret_cast<const uint32_t*>(message + 4);
    uint32_t thief_id = *reinterpret_cast<const uint32_t*>(message + 8);
    auto* skater = static_cast<Rider*>(context->content_);
    Rider* victim = rider_for_slot(victim_id);
    if (!victim)
        return net::kResultPassOn;
    Rider* thief = rider_for_slot(thief_id);
    if (!thief)
        return net::kResultPassOn;
    uint32_t script;
    Rider* other;
    if (skater->ident_ == victim_id) {
        script = kGraffitiStealYou;
        other = thief;
    } else if (skater->ident_ == thief_id) {
        script = kGraffitiStealOther;
        other = victim;
    } else {
        return net::kResultPassOn;
    }
    script::Struct* params = script::spawn_param_bag();
    if (params)
        params = params->prepare_all();
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

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {
    &objects::RiderHostSync::teardown,
    &objects::RiderHostSync::tick_frame,
    &RiderStateReset::configure_from_data,
    &objects::RiderHostSync::seal,
    stubs::do_nothing,
    objects::reapply_from_data,
    stubs::ignore_arguments2,
    &objects::ObjectModule::hold_off,
    stubs::ignore_argument,
    stubs::do_nothing,
    stubs::ignore_argument,
    static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::ObjectModule::invoke_method_by_name),
    &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace objects
