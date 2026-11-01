// The model component (GameObjects::MeshInstanceModule): its display turns (ModelSpin / ModelSpinAxis), the display matrix and
// the smaller members - see model_component.h. From the disassembly of 0x00420070-0x00420A46 and 0x004213E0-0x004219EC.
// The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the FPU stack are
// worked in double.

#include "engine/objects/model_component.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/model.h"
#include "engine/maths/mat4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/render/probe_ray.h"
#include "engine/script/names.h"
#include "engine/core/dbg.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace objects {

namespace {

constexpr float kHalfPi = 1.57079637f;             // 0x00645484
constexpr float kFramesToMs = 16.666666f;          // 0x006460DC (1000 / 60)
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kLerpRateScale = 0.02f;            // 0x00646124
constexpr double kTwoTo32 = 4294967296.0;          // 0x006453B4

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// render::RenderModel (not rewritten yet): hide it (0x00494B70), a geom hidden or shown (0x00494800), its texture `src`
// replaced (0x00493E80), geom `index` (0x00494640), the bone volumes (0x00492FC0) and a line tested against them
// (0x004939E0), and its lights (+0x2C, the folded getter 0x00532EB0).
void model_hide(render::RenderModel* model, bool hide) { model->conceal(hide); }
void model_hide_geom(render::RenderModel* model, uint32_t name, bool hide) { model->conceal_mesh(name, hide); }
bool model_replace_texture(render::RenderModel* model, uint32_t mesh, const char* src, const char* dest)
{
    return model->replace_texture_with(mesh, src, cell::of(dest));
}
void** model_geom(render::RenderModel* model, int index) { return static_cast<void**>(model->fetch_geom(index)); }
void* model_bone_volumes(render::RenderModel* model) { return model->get_bone_volumes(); }
int model_test_collision(render::RenderModel* model, maths::Vec4* line, void* skeleton, maths::Mat4* matrix, uint32_t a,
                         uint32_t b, uint32_t c, uint32_t d)
{
    // (the original's seven stack arguments: the line, skeleton and matrix pointers, then a, b, c and d - the extra
    // result's pointer)
    return model->test_bone_volumes(cell::of(line), cell::of(skeleton),
                                    cell::of(matrix), a, b, c, cell::pointer<uint32_t>(d));
}
void* model_lights(render::RenderModel* model) { return model->lights_ptr_; }
// The model's lights (not rewritten yet): their brightness from the level's (0x0048E660), set at once (0x0048E8A0)
// or lerped towards (0x0048E830); without model lights, the global brightness (0x0048EDB0).
void lights_update_brightness(void* lights) { static_cast<render::ModelLights*>(lights)->get_update_brightness(); }
void lights_set_brightness(void* lights, float brightness)
{
    static_cast<render::ModelLights*>(lights)->change_brightness(brightness);
}
void lights_lerp_brightness(void* lights, float brightness, float rate)
{
    static_cast<render::ModelLights*>(lights)->move_brightness(brightness, rate);
}
void set_global_brightness(float brightness) { render::set_world_brightness(brightness); }

// A skeleton component's skeleton (0x005480F0: its +0x18).
void* skeleton_of(ObjectModule* component) { return core::down_cast<objects::BoneRigModule>(component)->bone_rig_; }

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The network manager (0x007CCE14) and its players (not rewritten yet): the player with an object id (0x0053AEA0),
// whether it's this machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
void* net_session_slot() { return raw::as<void>(online::g_online_hub); }
void* find_player_slot_by_obj(uint32_t id)
{
    return static_cast<online::Hub*>(net_session_slot())->find_player_slot_by_obj(static_cast<uint16_t>(id));
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

// A message about `object` to the server, when its player is this machine's.
void send_to_server(GameEntity* object, uint8_t id, uint16_t length, void* data)
{
    void* manager = net_session_slot();
    void* player = find_player_slot_by_obj(object->ident_);
    if (!player || !is_own_player(player))
        return;
    net::NetMessageInfo message;
    net::Guest* app = client_app(manager, player_skater_index(player));
    message.content_ = data;
    message.byte_count_ = length;
    message.ident_ = id;
    app->send_to_host(&message);
}

} // namespace

// 0x00420590
double ModelSpin::bearing()
{
    if (!(flag_bits_ & kModelSpinOn))
        return 0.0;
    const int elapsed = flag_bits_ & kModelSpinRealTime ? static_cast<int>(frame_clock::get_time() - outset_)
                                                        : frame_clock::game_clock_time_since(static_cast<int>(outset_));
    auto t = static_cast<double>(elapsed);
    if (elapsed < 0)
        t += kTwoTo32; // (as unsigned)
    if (t > period_) { // (a NaN period never ends)
        if (flag_bits_ & kModelSpinHold)
            return static_cast<double>(range_) + base_;
        flag_bits_ &= ~kModelSpinOn;
        return 0.0;
    }
    if (power_ == 0)
        return static_cast<double>(range_) / period_ * t + base_;
    const double phase = t / period_ * kHalfPi;
    if (power_ < 0)
        return (1.0 - std::sin(kHalfPi - phase)) * range_ + base_;
    double s = std::sin(phase);
    for (int i = power_ - 1; i > 0; --i)
        s *= s;
    return s * range_ + base_;
}

// 0x004204F0
void ModelSpinAxis::get_clear()
{
    for (ModelSpin& spin : spins_)
        spin.flag_bits_ &= ~(kModelSpinOn | kModelSpinHold);
    active_ = false;
}

// 0x00420520
void ModelSpinAxis::append_one(float period, uint32_t start, float base, float range, int power, bool hold,
                               bool real_time)
{
    for (ModelSpin& spin : spins_) {
        if (spin.flag_bits_ & kModelSpinOn)
            continue;
        spin.period_ = period;
        spin.outset_ = start;
        spin.base_ = base;
        spin.range_ = range;
        spin.power_ = power;
        spin.flag_bits_ =
            static_cast<uint8_t>((spin.flag_bits_ & ~(kModelSpinHold | kModelSpinRealTime)) |
                                 (real_time ? kModelSpinRealTime : 0) | (hold ? kModelSpinHold : 0) | kModelSpinOn);
        active_ = true;
        return;
    }
}

// 0x004213E0
float ModelSpinAxis::bearing()
{
    float angle = 0.0f;
    if (!active_)
        return angle;
    active_ = false;
    for (ModelSpin& spin : spins_) {
        if (spin.flag_bits_ & kModelSpinOn) {
            active_ = true;
            angle = static_cast<float>(spin.bearing() + angle);
        }
    }
    return angle;
}

// 0x004215F0
void MeshInstanceModule::get_draw_matrix(maths::Mat4* out)
{
    *out = entity_->display_matrix_;
    out->row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (flip_) {
        out->row[2] = {-out->row[2].x, -out->row[2].y, -out->row[2].z, -out->row[2].w};
        out->row[0] = {-out->row[0].x, -out->row[0].y, -out->row[0].z, -out->row[0].w};
    }
    // The spins: about X, then Y, then Z (each one that isn't 0 multiplied in).
    maths::Mat4 rotation;
    bool none = true;
    using CreateFn = maths::Mat4& (*)(maths::Mat4&, float);
    static const std::array<CreateFn, 3> kCreate = {{maths::make_pitch_matrix, maths::make_yaw_matrix, maths::make_roll_matrix}};
    for (int axis = 0; axis < 3; ++axis) {
        if (!twist_rate_[axis].active_)
            continue;
        const float angle = twist_rate_[axis].bearing();
        if (angle == 0.0f)
            continue;
        if (none)
            rotation.reset_to_identity();
        none = false;
        maths::Mat4 turn;
        const CreateFn create = kCreate[axis];
        matrix_multiply(&rotation, &rotation, &create(turn, angle * kRadiansPerDegree));
    }
    maths::Vec4 shift = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!none) {
        // About the pivot: the pivot less where the turn takes it, in the display's frame.
        shift = pivot_;
        if (shift.x != 0.0f || shift.y != 0.0f || shift.z != 0.0f) {
            const maths::Vec4 turned = shift * rotation;
            shift = {shift.x - turned.x, shift.y - turned.y, shift.z - turned.z, shift.w - turned.w};
            shift = shift * *out;
        }
        matrix_multiply(out, &rotation, out);
    }
    maths::Vec4& pos = out->row[3];
    pos = entity_->at_point_;
    pos.x = shift.x + pos.x;
    pos.y = shift.y + pos.y;
    pos.z = shift.z + pos.z;
    pos.w = shift.w + pos.w;
    pos.x = shift_.x + pos.x;
    pos.y = shift_.y + pos.y;
    pos.z = shift_.z + pos.z;
    pos.w = shift_.w + pos.w;
    pos.w = 1.0f;
}

// 0x00421960
int MeshInstanceModule::test_collision(maths::Vec4* line, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    void* skeleton = skeleton_of(bone_rig_); // (read before the check: the original's crashes without one)
    const void* volumes = model_bone_volumes(mesh_instance_);
    if (!bone_rig_)
        return 0;
    if (!volumes) {
        debug::printf("No collision data for %s model\n", script::find_checksum_name_or_unknown(entity_->ident_));
        return 0;
    }
    maths::Mat4 matrix;
    get_draw_matrix(&matrix);
    return model_test_collision(mesh_instance_, line, skeleton, &matrix, a, b, c, d);
}

// 0x00420070
void MeshInstanceModule::conceal(bool hide)
{
    if (mesh_instance_)
        model_hide(mesh_instance_, hide);
}

// 0x00420090
void MeshInstanceModule::set_color_value(int index, float value)
{
    if (mesh_instance_ && index >= 1 && index <= color_value_count_)
        color_values_[index - 1] = value;
}

// 0x004200C0
void MeshInstanceModule::get_update_brightness() { lights_update_brightness(model_lights(mesh_instance_)); }

// 0x004200E0
void MeshInstanceModule::rebuild_from_profile(void* profile, uint32_t build_script)
{
    if (mesh_instance_)
        set_up_mesh_from_profile(profile, true, 0, build_script);
}

// 0x00420110
bool MeshInstanceModule::swap_texture(uint32_t mesh, const char* src, const char* dest)
{
    return mesh_instance_ && model_replace_texture(mesh_instance_, mesh, src, dest);
}

// 0x004202A0
bool MeshInstanceModule::conceal_mesh(uint32_t name, bool hide, bool send)
{
    if (send) {
        online::HideGeomMessage sent;
        sent.hide = hide;
        sent.name = name;
        sent.object_id = static_cast<uint8_t>(entity_->ident_);
        std::array<uint8_t, online::kHideGeomSize> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_hide_geom(writer, sent);
        send_to_server(entity_, online::kMsgHideGeom, sizeof(data), data.data());
    }
    if (mesh_instance_)
        model_hide_geom(mesh_instance_, name, hide);
    return true;
}

// 0x00420670
void MeshInstanceModule::apply_brightness(render::ProbeRay* feeler, bool lerp, float rate)
{
    if (!feeler->is_hit_triangle_collision())
        return;
    void* lights = model_lights(mesh_instance_);
    const double value = feeler->get_hit_vertex_value();
    if (!lights) {
        set_global_brightness(static_cast<float>(value));
        return;
    }
    const double doubled = value + value;
    brightness_ = static_cast<float>(doubled);
    if (lerp)
        lights_lerp_brightness(lights, static_cast<float>(doubled * 0.5), rate * kLerpRateScale);
    else
        lights_set_brightness(lights, static_cast<float>(doubled * 0.5));
}

// 0x00420700
void MeshInstanceModule::change_tint(uint32_t color)
{
    if (!sector_model_)
        return;
    void** geom = model_geom(mesh_instance_, 0);
    raw::as<render::Geom>(geom)->change_tint(color);
}

// 0x00420730
void MeshInstanceModule::rotate_display(float duration, float start, float end, int power, bool hold, bool x, bool y,
                                        bool z)
{
    const float period = duration * kFramesToMs;
    pivot_ = {0.0f, 30.0f, 0.0f, 1.0f};
    auto now = static_cast<uint32_t>(frame_clock::game_clock_time());
    uint8_t axes = 0;
    const std::array<bool, 3> flags = {{x, y, z}};
    for (int axis = 0; axis < 3; ++axis) {
        if (!flags[axis])
            continue;
        twist_rate_[axis].append_one(period, now, start, end - start, power, hold, false);
        axes |= static_cast<uint8_t>(1 << axis);
    }
    online::SpinModelMessage sent;
    void* manager = net_session_slot();
    void* player = find_player_slot_by_obj(entity_->ident_);
    if (!player || !is_own_player(player))
        return;
    net::NetMessageInfo message;
    net::Guest* app = client_app(manager, player_skater_index(player));
    sent.period = get_truncated(period);
    sent.start = static_cast<int16_t>(get_truncated(start));
    sent.range = static_cast<int16_t>(get_truncated(static_cast<double>(end) - start));
    sent.power = power;
    sent.object_id = static_cast<uint8_t>(entity_->ident_);
    sent.hold = hold;
    sent.axes = axes;
    std::array<uint8_t, online::kSpinModelSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_spin_model(writer, sent);
    message.content_ = data.data();
    message.byte_count_ = sizeof(data);
    message.ident_ = online::kMsgSpinModel;
    app->send_to_host(&message);
}

// 0x004208D0
void MeshInstanceModule::cancel_rotate_display()
{
    for (ModelSpinAxis& axis : twist_rate_) {
        for (ModelSpin& spin : axis.spins_)
            spin.flag_bits_ &= ~(kModelSpinOn | kModelSpinHold);
        axis.active_ = false;
    }
    auto object = static_cast<uint8_t>(entity_->ident_);
    send_to_server(entity_, online::kMsgCancelSpins, 1, &object);
}

} // namespace objects

namespace frame_clock {

// 0x00420250
int game_clock_time_since(int start) { return game_clock_elapsed() + (objects::g_clock_offset - start); }

} // namespace frame_clock
