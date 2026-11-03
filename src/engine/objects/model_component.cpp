// The model component (GameObjects::MeshInstanceModule): its display turns (ModelSpin / ModelSpinAxis), the display matrix and
// the smaller members - see model_component.h. From the disassembly of 0x00420070-0x00420A46 and 0x004213E0-0x004219EC.
// The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the FPU stack are
// worked in double.

#include "engine/objects/model_component.h"
#include "wire/message_ids.h"
#include "platform/d3dx.h"

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/model.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/render/probe_ray.h"
#include "engine/script/names.h"
#include "engine/core/dbg.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "engine/core/checksums.h"

namespace objects {

namespace {
constexpr double kHalf = 0.5;
constexpr float kPivotHeight = 30.0F;

constexpr float kHalfPi = 1.57079637F;             // 0x00645484
constexpr float kFramesToMs = 16.666666F;          // 0x006460DC (1000 / 60)
constexpr float kRadiansPerDegree = 0.0174532924F; // 0x00645464
constexpr float kLerpRateScale = 0.02F;            // 0x00646124
constexpr double kTwoTo32 = 4294967296.0;          // 0x006453B4

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// render::RenderModel (not rewritten yet): hide it (0x00494B70), a geom hidden or shown (0x00494800), its texture `src`
// replaced (0x00493E80), geom `index` (0x00494640), the bone volumes (0x00492FC0) and a line tested against them
// (0x004939E0), and its lights (+0x2C, the folded getter 0x00532EB0).
void model_hide(render::RenderModel* model, bool hide) { model->conceal(hide); }
void model_hide_geom(render::RenderModel* model, uint32_t name, bool hide) { model->conceal_mesh(name, hide); }
bool model_replace_texture(render::RenderModel* model, uint32_t mesh, const char* src, uint32_t dest)
{
    return model->replace_texture_with(mesh, src, dest);
}
render::Geom* model_geom(render::RenderModel* model, int index) { return model->fetch_geom(index); }
void* model_bone_volumes(render::RenderModel* model) { return model->get_bone_volumes(); }
int model_test_collision(render::RenderModel* model, const maths::Seg* line, gfx::Rig* skeleton,
                         const maths::Mat4* matrix, const render::BoneLineTest& test)
{
    return model->test_bone_volumes(line, skeleton, matrix, test);
}
void* model_lights(render::RenderModel* model) { return model->lights_ptr().get(); }
// The model's lights (not rewritten yet): their brightness from the level's (0x0048E660), set at once (0x0048E8A0)
// or lerped towards (0x0048E830); without model lights, the global brightness (0x0048EDB0).
void lights_update_brightness(void* lights) { static_cast<render::ModelLights*>(lights)->get_update_brightness(); }
void lights_set_brightness(void* lights, float brightness)
{
    static_cast<render::ModelLights*>(lights)->change_brightness(brightness);
}
void lights_lerp_brightness(void* lights, float brightness, float rate)
{
    static_cast<render::ModelLights*>(lights)->move_brightness({.target = brightness, .step = rate});
}
void set_global_brightness(float brightness) { render::set_world_brightness(brightness); }

// A skeleton component's skeleton (0x005480F0: its +0x18).
gfx::Rig* skeleton_of(ObjectModule* component) { return core::down_cast<objects::BoneRigModule>(component)->bone_rig(); }

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The network manager (0x007CCE14) and its players (not rewritten yet): the player with an object id (0x0053AEA0),
// whether it's this machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
void* find_player_slot_by_obj(uint32_t id)
{
    return static_cast<online::Hub*>(online::online_hub())->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
bool is_own_player(void* player) { return static_cast<online::PlayerRecord*>(player)->is_own_player() != 0; }
int player_skater_index(void* player)
{
    return static_cast<int>(static_cast<online::PlayerRecord*>(player)->get_skater_view_slot());
}
net::Guest* client_app(void* manager, int index)
{
    return core::down_cast<net::Guest>(static_cast<online::Hub*>(manager)->fetch_patron(index));
}

// A message's id and its length in bytes.
struct MessageHead {
    online::MessageId id{};
    uint16_t length{};
};

// A message about `object` to the server, when its player is this machine's.
void send_to_server(GameEntity* object, MessageHead message_head, void* data)
{
    const auto id = message_head.id;
    const auto length = message_head.length;
    void* manager = online::online_hub();
    void* player = find_player_slot_by_obj(object->ident());
    if (!player || !is_own_player(player)) {
        return;
    }
    net::NetMessageInfo message;
    net::Guest* app = client_app(manager, player_skater_index(player));
    message.set_content(data);
    message.set_byte_count(length);
    message.set_ident(id);
    app->send_to_host(&message);
}

} // namespace

// 0x00420590
double bearing(ModelSpin& spin)
{
    if (!(spin.flag_bits_ & kModelSpinOn)) {
        return 0.0;
    }
    const int elapsed = spin.flag_bits_ & kModelSpinRealTime ? static_cast<int>(frame_clock::get_time() - spin.outset_)
                                                        : frame_clock::game_clock_time_since(static_cast<int>(spin.outset_));
    auto t = static_cast<double>(elapsed);
    if (elapsed < 0) {
        t += kTwoTo32; // (as unsigned)
    }
    if (t > spin.period_) { // (a NaN period never ends)
        if (spin.flag_bits_ & kModelSpinHold) {
            return static_cast<double>(spin.range_) + spin.base_;
        }
        spin.flag_bits_ &= ~kModelSpinOn;
        return 0.0;
    }
    if (spin.power_ == 0) {
        return (static_cast<double>(spin.range_) / spin.period_ * t) + spin.base_;
    }
    const double phase = t / spin.period_ * kHalfPi;
    if (spin.power_ < 0) {
        return ((1.0 - std::sin(kHalfPi - phase)) * spin.range_) + spin.base_;
    }
    double s = std::sin(phase);
    for (int i = spin.power_ - 1; i > 0; --i) {
        s *= s;
    }
    return (s * spin.range_) + spin.base_;
}

// 0x004204F0
void get_clear(ModelSpinAxis& axis)
{
    for (ModelSpin& spin : axis.spins_) {
        spin.flag_bits_ &= ~(kModelSpinOn | kModelSpinHold);
    }
    axis.active_ = false;
}

// 0x00420520
void append_one(ModelSpinAxis& axis, SpinWave spin_wave, bool hold,
                               bool real_time)
{
    const auto period = spin_wave.period;
    const auto start = spin_wave.start;
    const auto base = spin_wave.base;
    const auto range = spin_wave.range;
    const auto power = spin_wave.power;
    for (ModelSpin& spin : axis.spins_) {
        if (spin.flag_bits_ & kModelSpinOn) {
            continue;
        }
        spin.period_ = period;
        spin.outset_ = start;
        spin.base_ = base;
        spin.range_ = range;
        spin.power_ = power;
        spin.flag_bits_ =
            static_cast<uint8_t>((spin.flag_bits_ & ~(kModelSpinHold | kModelSpinRealTime)) |
                                 (real_time ? kModelSpinRealTime : 0) | (hold ? kModelSpinHold : 0) | kModelSpinOn);
        axis.active_ = true;
        return;
    }
}

// 0x004213E0
float bearing(ModelSpinAxis& axis)
{
    float angle = 0.0F;
    if (!axis.active_) {
        return angle;
    }
    axis.active_ = false;
    for (ModelSpin& spin : axis.spins_) {
        if (spin.flag_bits_ & kModelSpinOn) {
            axis.active_ = true;
            angle = static_cast<float>(bearing(spin) + angle);
        }
    }
    return angle;
}

// 0x004215F0
void MeshInstanceModule::get_draw_matrix(maths::Mat4* out)
{
    *out = entity()->display_matrix();
    core::at(out->row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (flip_) {
        core::at(out->row, 2) = {.x = -core::at(out->row, 2).x, .y = -core::at(out->row, 2).y, .z = -core::at(out->row, 2).z, .w = -core::at(out->row, 2).w};
        core::at(out->row, 0) = {.x = -core::at(out->row, 0).x, .y = -core::at(out->row, 0).y, .z = -core::at(out->row, 0).z, .w = -core::at(out->row, 0).w};
    }
    // The spins: about X, then Y, then Z (each one that isn't 0 multiplied in).
    maths::Mat4 rotation;
    bool none = true;
    using CreateFn = maths::Mat4& (*)(maths::Mat4&, float);
    static const std::array<CreateFn, 3> kCreate = {{maths::make_pitch_matrix, maths::make_yaw_matrix, maths::make_roll_matrix}};
    for (int axis = 0; axis < 3; ++axis) {
        if (!core::at(twist_rate_, axis).active_) {
            continue;
        }
        const float angle = bearing(core::at(twist_rate_, axis));
        if (angle == 0.0F) {
            continue;
        }
        if (none) {
            rotation.reset_to_identity();
        }
        none = false;
        maths::Mat4 turn;
        const CreateFn create = core::at(kCreate, axis);
        matrix_multiply(&rotation, &rotation, &create(turn, angle * kRadiansPerDegree));
    }
    maths::Vec4 shift = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    if (!none) {
        // About the pivot: the pivot less where the turn takes it, in the display's frame.
        shift = pivot_;
        if (shift.x != 0.0F || shift.y != 0.0F || shift.z != 0.0F) {
            const maths::Vec4 turned = shift * rotation;
            shift = {.x = shift.x - turned.x, .y = shift.y - turned.y, .z = shift.z - turned.z, .w = shift.w - turned.w};
            shift = shift * *out;
        }
        matrix_multiply(out, &rotation, out);
    }
    maths::Vec4& pos = core::at(out->row, 3);
    pos = entity()->at_point();
    pos.x = shift.x + pos.x;
    pos.y = shift.y + pos.y;
    pos.z = shift.z + pos.z;
    pos.w = shift.w + pos.w;
    pos.x = shift_.x + pos.x;
    pos.y = shift_.y + pos.y;
    pos.z = shift_.z + pos.z;
    pos.w = shift_.w + pos.w;
    pos.w = 1.0F;
}

// 0x00421960
int MeshInstanceModule::test_collision(const maths::Seg* line, const render::BoneLineTest& test)
{
    gfx::Rig* skeleton = skeleton_of(bone_rig_); // (read before the check: the original's crashes without one)
    const void* volumes = model_bone_volumes(mesh_instance_.get());
    if (!bone_rig_) {
        return 0;
    }
    if (!volumes) {
        debug::printf("No collision data for %s model\n", script::find_checksum_name_or_unknown(entity()->ident()));
        return 0;
    }
    maths::Mat4 matrix;
    get_draw_matrix(&matrix);
    return model_test_collision(mesh_instance_.get(), line, skeleton, &matrix, test);
}

// 0x00420070
void MeshInstanceModule::conceal(bool hide)
{
    if (mesh_instance_) {
        model_hide(mesh_instance_.get(), hide);
    }
}

// 0x00420090
void MeshInstanceModule::set_color_value(int index, float value)
{
    if (mesh_instance_.get() && index >= 1 && index <= color_value_count_) {
        core::at(color_values_, index - 1) = value;
    }
}

// 0x004200C0
void MeshInstanceModule::get_update_brightness() { lights_update_brightness(model_lights(mesh_instance_.get())); }

// 0x004200E0
void MeshInstanceModule::rebuild_from_profile(void* profile, uint32_t build_script)
{
    if (mesh_instance_) {
        set_up_mesh_from_profile(profile, true, 0, checksums::Name{build_script});
    }
}

// 0x00420110
bool MeshInstanceModule::swap_texture(uint32_t mesh, const char* src, uint32_t dest)
{
    return mesh_instance_.get() && model_replace_texture(mesh_instance_.get(), mesh, src, dest);
}

// 0x004202A0
bool MeshInstanceModule::conceal_mesh(uint32_t name, bool hide, bool send)
{
    if (send) {
        online::HideGeomMessage sent;
        sent.hide = hide;
        sent.name = name;
        sent.object_id = static_cast<uint8_t>(entity()->ident());
        std::array<uint8_t, online::kHideGeomSize> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_hide_geom(writer, sent);
        send_to_server(entity(), {.id = online::MessageId::kHideGeom, .length = sizeof(data)}, data.data());
    }
    if (mesh_instance_) {
        model_hide_geom(mesh_instance_.get(), name, hide);
    }
    return true;
}

// 0x00420670
void MeshInstanceModule::apply_brightness(render::ProbeRay* feeler, bool lerp, float rate)
{
    if (!feeler->is_hit_triangle_collision()) {
        return;
    }
    void* lights = model_lights(mesh_instance_.get());
    const double value = feeler->get_hit_vertex_value();
    if (!lights) {
        set_global_brightness(static_cast<float>(value));
        return;
    }
    const double doubled = value + value;
    brightness_ = static_cast<float>(doubled);
    if (lerp) {
        lights_lerp_brightness(lights, static_cast<float>(doubled * kHalf), rate * kLerpRateScale);
    } else {
        lights_set_brightness(lights, static_cast<float>(doubled * kHalf));
    }
}

// 0x00420700
void MeshInstanceModule::change_tint(uint32_t color)
{
    if (!sector_model_) {
        return;
    }
    render::Geom* geom = model_geom(mesh_instance_.get(), 0);
    geom->change_tint(color);
}

// 0x00420730
void MeshInstanceModule::rotate_display(float duration, float start, float end, int power, bool hold, bool x, bool y,
                                        bool z)
{
    const float period = duration * kFramesToMs;
    pivot_ = {.x = 0.0F, .y = kPivotHeight, .z = 0.0F, .w = 1.0F};
    auto now = static_cast<uint32_t>(frame_clock::game_clock_time());
    uint8_t axes = 0;
    const std::array<bool, 3> flags = {{x, y, z}};
    for (int axis = 0; axis < 3; ++axis) {
        if (!core::at(flags, axis)) {
            continue;
        }
        append_one(core::at(twist_rate_, axis), {.period = period, .start = now, .base = start, .range = end - start, .power = power}, hold, false);
        axes |= static_cast<uint8_t>(1 << axis);
    }
    online::SpinModelMessage sent;
    void* manager = online::online_hub();
    void* player = find_player_slot_by_obj(entity()->ident());
    if (!player || !is_own_player(player)) {
        return;
    }
    net::NetMessageInfo message;
    net::Guest* app = client_app(manager, player_skater_index(player));
    sent.period = get_truncated(period);
    sent.start = static_cast<int16_t>(get_truncated(start));
    sent.range = static_cast<int16_t>(get_truncated(static_cast<double>(end) - start));
    sent.power = power;
    sent.object_id = static_cast<uint8_t>(entity()->ident());
    sent.hold = hold;
    sent.axes = axes;
    std::array<uint8_t, online::kSpinModelSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_spin_model(writer, sent);
    message.set_content(data.data());
    message.set_byte_count(sizeof(data));
    message.set_ident(online::MessageId::kSpinModel);
    app->send_to_host(&message);
}

// 0x004208D0
void MeshInstanceModule::cancel_rotate_display()
{
    for (ModelSpinAxis& axis : twist_rate_) {
        for (ModelSpin& spin : axis.spins_) {
            spin.flag_bits_ &= ~(kModelSpinOn | kModelSpinHold);
        }
        axis.active_ = false;
    }
    auto object = static_cast<uint8_t>(entity()->ident());
    send_to_server(entity(), {.id = online::MessageId::kCancelSpins, .length = 1}, &object);
}

} // namespace objects

namespace frame_clock {

// 0x00420250
int game_clock_time_since(int start) { return game_clock_elapsed() + (objects::clock_offset() - start); }

} // namespace frame_clock
