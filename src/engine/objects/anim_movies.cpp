// The object and camera animation movies, 0x005871B0-0x005880DF. See anim_movies.h.

#include "engine/objects/anim_movies.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/assets/resource_registry.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/node_objects.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/objects/compositemgr.h"
#include "platform/code_address.h"
#include "frontend/front_end.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

float g_anim_movies_one{1.0f};
float g_anim_movies_seconds_per_frame{0.016666668f};
float g_float_zero;

namespace {

constexpr uint32_t kNoAsset = 0xEE721991; // a timer, not an asset
constexpr uint32_t kFrames = 0x019176C5;  // its length in frames
constexpr uint32_t kLooping = 0x5EA0E211;
constexpr uint32_t kBackwards = 0xF8CFD515;

using gfx::AnimTimeline;

// The base movie's code (VideoEntry, node_objects.h).
void base_load(VideoEntry* movie, script::Struct* params) { movie->configure_from_data(params); }
void set_held(VideoEntry* movie, int value) { movie->set_flag_1b(static_cast<uint8_t>(value)); }
// The movie's name (movie_manager.h).
uint32_t get_name_of(void* movie) { return movie_key_from_params(movie); }
// The asset manager's asset by name (null if none).
void* find_asset(uint32_t name) { return assets::g_asset_manager->get_asset_data(name, 0); }
// The movie's own Start (the original's slot 3).
void start_movie(VideoEntry* movie) { movie->kickoff(); }

// The timeline's code: constructor / destructor body, complete, PlayAnim (slot 3), Update (slot 2).
void construct_timeline(AnimTimeline* t) { new (t) AnimTimeline; }
void destroy_timeline(AnimTimeline* t) { t->~AnimTimeline(); }
bool is_accomplished(AnimTimeline* t) { return t->is_accomplished(); }
void play_anim(AnimTimeline* t, uint32_t anim, float start, float end, int loop_type, float blend, float speed)
{
    t->play_anim(anim, start, end, loop_type, blend, speed);
}
void update_timeline(AnimTimeline* t) { t->tick_frame(); }

// The animation asset's code: started (0x00482F10); told a step from one time to another (0x00482F40).
void start_asset(void* anim) { static_cast<gfx::JointClipFrames*>(anim)->arm_events(); }
void step_asset(void* anim, float from, float to, gfx::ViewCam* camera, bool first)
{
    static_cast<gfx::JointClipFrames*>(anim)->fire_events(from, to, camera, first);
}

float seconds_per_frame() { return g_anim_movies_seconds_per_frame; } // 1/60

constexpr uint32_t kFixedTarget = 0xC588EEBC; // a camera target that is a point (the target offset), not an object


void set_identity(std::span<float, 16> m)
{
    for (size_t i = 0; i < 16; ++i)
        core::at(m, i) = (i % 5 == 0) ? 1.0f : 0.0f;
}

// Row `row` of a matrix as its first three floats (x, y, z).
std::span<float, 3> axis_of(std::span<float, 16> m, size_t row) { return std::span<float, 3>(m.subspan(row * 4, 3)); }

// out = a x b (x, y and z; each a difference of products, rounded once).
void cross_into(std::span<float, 3> out, std::span<const float, 3> a, std::span<const float, 3> b)
{
    const double x = static_cast<double>(core::at(a, 1)) * core::at(b, 2) - static_cast<double>(core::at(a, 2)) * core::at(b, 1);
    const double y = static_cast<double>(core::at(a, 2)) * core::at(b, 0) - static_cast<double>(core::at(a, 0)) * core::at(b, 2);
    const double z = static_cast<double>(core::at(a, 0)) * core::at(b, 1) - static_cast<double>(core::at(a, 1)) * core::at(b, 0);
    core::at(out, 0) = static_cast<float>(x);
    core::at(out, 1) = static_cast<float>(y);
    core::at(out, 2) = static_cast<float>(z);
}

// Play: the asset's length (its first word) from 0, or backwards from a frame before its end to 0.
void play_on(AnimTimeline* t, void* anim, int loop_type, bool backwards)
{
    const float length = static_cast<gfx::JointClipFrames*>(anim)->duration_;
    if (backwards)
        play_anim(t, 0, static_cast<float>(static_cast<double>(length) - seconds_per_frame()), 0.0f, loop_type, 0.0f,
                  1.0f);
    else
        play_anim(t, 0, 0.0f, length, loop_type, 0.0f, 1.0f);
}

// Load's shared part: the asset named like the movie played (false: there's none), or a timer of `frames`.
template <typename Movie>
bool load_anim(Movie* movie, script::Struct* params)
{
    if (!params->has_bare_flag(kNoAsset)) {
        void* anim = find_asset(get_name_of(movie));
        if (!anim)
            return false;
        const bool backwards = params->has_bare_flag(kBackwards);
        const int loop_type = params->has_bare_flag(kLooping) ? 1 : 0;
        movie->perform(anim, loop_type, backwards);
    } else {
        int frames = 0;
        params->fetch_int_value(kFrames, &frames, 1);
        movie->anim_ = nullptr;
        play_anim(&movie->timeline_, 0, 0.0f, static_cast<float>(frames * static_cast<double>(seconds_per_frame())), 0,
                  0.0f, 1.0f);
    }
    set_held(movie, 1);
    return true;
}

} // namespace

// --- the object animation movie -------------------------------------------------------------------------------

// 0x005871B0
ObjectAnimMovie::ObjectAnimMovie() { construct_timeline(&timeline_); }

// 0x00587210
ObjectAnimMovie::~ObjectAnimMovie() { destroy_timeline(&timeline_); }

// 0x005875D0
bool ObjectAnimMovie::read_in(script::Struct* params)
{
    base_load(this, params);
    if (!load_anim(this, params))
        return false;
    start_movie(this);
    return true;
}

// 0x005873A0
void ObjectAnimMovie::perform(void* anim, int loop_type, bool backwards)
{
    anim_ = anim;
    play_on(&timeline_, anim, loop_type, backwards);
}

// 0x00587270
bool ObjectAnimMovie::kickoff()
{
    if (anim_)
        start_asset(anim_);
    return true;
}

// 0x00587280 / 0x005872A0
bool ObjectAnimMovie::is_finished()
{
    if (is_finished_)
        return true;
    if (flag18_)
        return false;
    return is_accomplished(&timeline_);
}
bool ObjectAnimMovie::is_held_and_complete() { return flag18_ && is_accomplished(&timeline_); }

// --- the camera animation movie -------------------------------------------------------------------------------

// 0x005873F0
CameraAnimMovie::CameraAnimMovie()
{
    construct_timeline(&timeline_);
}

// 0x005872C0
CameraAnimMovie::~CameraAnimMovie() { destroy_timeline(&timeline_); }

// 0x00587F70
bool CameraAnimMovie::read_in(script::Struct* params)
{
    base_load(this, params);
    float fov = 0.0f;
    if (params->fetch_float(0x17E7321E, &fov, 0)) {
        field_of_view_ = fov;
        viewer_->change_h_fov(fov);
    }
    if (!load_anim(this, params))
        return false;
    load_target(params);
    start_movie(this);
    place_camera();
    if (anim_)
        step_asset(anim_, timeline_.ongoing_, timeline_.ongoing_, viewer_, true);
    return true;
}

// 0x00587480
void CameraAnimMovie::load_target(script::Struct* params)
{
    has_goal_ = params->read_hash_value(0x531E4D28, &aim_point_, 0);
    if (!has_goal_)
        return;
    goal_shift_.x = goal_shift_.y = goal_shift_.z = 0.0f;
    goal_shift_.w = 1.0f;
    params->fetch_vec(0x906642BF, &goal_shift_, 0);
    const float zero = g_float_zero;
    // (not equal, or unordered: a NaN counts as set)
    target_offset_set_ = !(goal_shift_.x == zero) || !(goal_shift_.y == zero) || !(goal_shift_.z == zero);
    look_offset_.x = look_offset_.y = look_offset_.z = 0.0f;
    look_offset_.w = 1.0f;
    look_offset_set_ = params->fetch_vec(0xA00D3D6E, &look_offset_, 0);
}

// 0x00587540
void CameraAnimMovie::perform(void* anim, int loop_type, bool backwards)
{
    anim_ = anim;
    play_on(&timeline_, anim, loop_type, backwards);
}

// 0x00587320
bool CameraAnimMovie::kickoff()
{
    if (anim_)
        start_asset(anim_);
    return true;
}

// 0x00587330 / 0x00587350
bool CameraAnimMovie::is_finished()
{
    if (is_finished_)
        return true;
    if (flag18_)
        return false;
    return is_accomplished(&timeline_);
}
bool CameraAnimMovie::is_held_and_complete() { return flag18_ && is_accomplished(&timeline_); }

// 0x00587590
bool CameraAnimMovie::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name == 0xA8D5A188) {
        load_target(params);
        return true;
    }
    if (name == 0xD7241A6C) {
        has_goal_ = false;
        aim_point_ = 0;
        return true;
    }
    // (the base class's invoke_method_by_name: a stub shared by many vtables, 0x004A0CF0 - "not handled")
    return false;
}

// 0x00588090
void CameraAnimMovie::tick_frame()
{
    const float from = timeline_.ongoing_;
    update_timeline(&timeline_);
    viewer_->remember_prev_pos();
    if (anim_)
        step_asset(anim_, from, timeline_.ongoing_, viewer_, false);
    place_camera();
}

// 0x00587A50
void CameraAnimMovie::place_camera()
{
    const std::span<float, 4> pos(viewer_->fetch_location()->data(), 4);
    maths::Mat4* mat = viewer_->fetch_mat();
    const std::span<float, 16> m(mat->data(), 16);
    // A skater (of the two) whose 0x40 flag is up: the base's 0x00468F20(1).
    for (const auto& handler : modules::g_front_end_instance->pad_listeners_) {
        if (handler->control_data_->makes_ & 0x40)
            check_done(true);
    }
    if (anim_) {
        // The camera from the asset's frame: its position and its rotation.
        std::array<float, 4> quat = {{0.0f, 0.0f, 0.0f, 1.0f}};
        std::array<float, 4> at{};
        bool flag = false;
        static_cast<gfx::JointClipFrames*>(anim_)->get_float_key_pose(quat, at, &flag, timeline_.ongoing_, 0);
        for (int i = 0; i < 4; ++i)
            core::at(pos, i) = core::at(at, i);
        std::array<float, 4> origin = {{0.0f, 0.0f, 0.0f, 1.0f}};
        objects::quat_to_matrix(quat, origin, m);
    } else {
        // Where it is; the field of view (0: the script global's).
        core::at(pos, 0) = core::at(pos, 1) = core::at(pos, 2) = 0.0f;
        core::at(pos, 3) = 1.0f;
        set_identity(m);
        float fov = field_of_view_;
        if (fov == g_float_zero)
            fov = script::fetch_float(0x99529205);
        viewer_->change_h_fov(fov);
    }
    if (!has_goal_)
        return;
    maths::Vec4 dir{};
    if (aim_point_ != kFixedTarget) {
        // Aimed at an object (none: as it was): from its position (moved by the offsets in its frame).
        auto* object = core::down_cast<GameEntity>(
            static_cast<ObjectHubBase*>(g_composite_registry.get())->look_up_entity(aim_point_));
        if (!object)
            return;
        std::array<float, 4> target = {{object->at_point_.x, object->at_point_.y, object->at_point_.z, 1.0f}};
        maths::Mat4 frame{};
        auto moved = [&](const maths::Vec4& offset) {
            frame = object->basis_frame_;
            frame[3] = {.x = core::at(target, 0), .y = core::at(target, 1), .z = core::at(target, 2), .w = 1.0f};
            frame.offset_local(offset);
        };
        if (target_offset_set_) {
            moved(goal_shift_);
            for (int i = 0; i < 4; ++i)
                core::at(target, i) = frame[3][i];
        }
        if (look_offset_set_) {
            for (int i = 0; i < 4; ++i)
                core::at(pos, i) = core::at(target, i);
            moved(look_offset_);
            for (int i = 0; i < 4; ++i)
                core::at(pos, i) = frame[3][i];
        }
        for (int i = 0; i < 4; ++i)
            dir[i] = core::at(pos, i) - core::at(target, i);
        dir.make_unit();
        set_identity(m);
        for (int i = 0; i < 4; ++i)
            core::at(m, 8 + i) = dir[i];
        cross_into(axis_of(m, 0), axis_of(m, 1), axis_of(m, 2)); // the right axis: up x at
        core::at(m, 3) = 0.0f;
        (*mat)[0].make_unit();
    } else {
        // A fixed point (the target offset), seen from the look offset.
        for (int i = 0; i < 4; ++i)
            core::at(pos, i) = look_offset_[i];
        // (x and y stay unrounded; z is stored and squared as its unrounded value times the stored one)
        double x = static_cast<double>(core::at(pos, 0)) - goal_shift_.x;
        double y = static_cast<double>(core::at(pos, 1)) - goal_shift_.y;
        const double z_exact = static_cast<double>(core::at(pos, 2)) - goal_shift_.z;
        auto z = static_cast<float>(z_exact);
        const double length = std::sqrt(z_exact * z + y * y + x * x);
        if (length > g_float_zero) {
            const double inverse = g_anim_movies_one / length;
            x *= inverse;
            y *= inverse;
            z = static_cast<float>(z * inverse);
        }
        set_identity(m);
        core::at(m, 8) = static_cast<float>(x);
        core::at(m, 10) = z;
        core::at(m, 9) = static_cast<float>(y);
        core::at(m, 11) = 0.0f;
        cross_into(axis_of(m, 0), axis_of(m, 1), axis_of(m, 2));
        core::at(m, 3) = 0.0f;
        const double a = core::at(m, 0);
        const double b = core::at(m, 1);
        const double c = core::at(m, 2);
        const double size = std::sqrt(a * a + b * b + c * c);
        if (size > g_float_zero) {
            const double inverse = g_anim_movies_one / size;
            core::at(m, 0) = static_cast<float>(inverse * core::at(m, 0));
            core::at(m, 1) = static_cast<float>(inverse * core::at(m, 1));
            core::at(m, 2) = static_cast<float>(core::at(m, 2) * inverse);
        }
    }
    // The up axis: at x right.
    cross_into(axis_of(m, 1), axis_of(m, 2), axis_of(m, 0));
    core::at(m, 7) = 0.0f;
}

// 0x005876A0 (and the thunk 0x00587F60)
void ObjectAnimMovie::tick_frame()
{
    const float from = timeline_.ongoing_;
    update_timeline(&timeline_);
    if (anim_)
        step_asset(anim_, from, timeline_.ongoing_, viewer_, false);
    // Every object the asset moves: its frame's rotation and position.
    auto* frames = static_cast<gfx::JointClipFrames*>(anim_);
    const int count = frames->bone_count_;
    auto* quat_memory = static_cast<float*>(memory::alloc(count * 16));
    const auto bones = static_cast<size_t>(count);
    const std::span<float> quats(quat_memory, bones * 4);
    for (size_t i = 0; i < bones; ++i) {
        const std::span<float, 4> quat = gfx::quad_at(quats, i);
        core::at(quat, 0) = core::at(quat, 1) = core::at(quat, 2) = 0.0f;
        core::at(quat, 3) = 1.0f;
    }
    auto* position_memory = static_cast<float*>(memory::alloc(frames->bone_count_ * 16));
    const std::span<float> positions(position_memory, bones * 4);
    auto* flags = static_cast<bool*>(memory::alloc(frames->bone_count_));
    static_cast<gfx::JointClipFrames*>(anim_)->get_float_key_pose(quats, positions, flags, timeline_.ongoing_, 0);
    for (int i = 0; i < frames->bone_count_; ++i) {
        const uint32_t name = static_cast<gfx::JointClipFrames*>(anim_)->fetch_joint_name(i);
        auto* object = core::down_cast<GameEntity>(objects::find_object(name));
        if (!object) {
            debug::printf("object animation:  couldn't find object %s\n", script::find_checksum_name_or_unknown(name));
            continue;
        }
        std::array<float, 4> origin = {{0.0f, 0.0f, 0.0f, 1.0f}};
        std::array<float, 16> matrix{};
        objects::quat_to_matrix(gfx::quad_at(quats, static_cast<size_t>(i)), origin, matrix);
        std::memcpy(&object->basis_frame_, matrix.data(), sizeof(matrix));    // its matrix
        std::memcpy(&object->display_matrix_, matrix.data(), sizeof(matrix)); // and its display matrix
        std::memcpy(&object->at_point_, gfx::quad_at(positions, static_cast<size_t>(i)).data(), 16); // its position
        if (script::fetch_int_value(0x2F510F45)) {
            debug::printf("Object %s @ time %f:  ", script::find_checksum_name_or_unknown(name),
                          static_cast<double>(timeline_.ongoing_));
            object->at_point_.echo_out(); // (printed)
            object->basis_frame_.echo_out();
        }
        // The object's model told (the Update of its 0x286A8D26 component).
        ObjectModule* component = object->fetch_module(0x286A8D26);
        if (component)
            component->tick_frame();
    }
    memory::Hub::get_pull()->dealloc(quat_memory);
    memory::Hub::get_pull()->dealloc(position_memory);
    memory::Hub::get_pull()->dealloc(flags);
}

} // namespace objects
