// The object and camera animation movies, 0x005871B0-0x005880DF. See anim_movies.h.

#include "engine/objects/anim_movies.h"

#include <cmath>
#include <cstdint>
#include <cstring>

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
#include "engine/core/code_address.h"
#include "frontend/front_end.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"

namespace objects {

float g_anim_movies_one{1.0f};
float g_anim_movies_seconds_per_frame{0.016666668f};
float g_float_zero;

namespace {

extern void** const kObjectAnimMovieVtable;
extern void** const kCameraAnimMovieVtable;
constexpr uint32_t kNoAsset = 0xEE721991;   // a timer, not an asset
constexpr uint32_t kFrames = 0x019176C5;    // its length in frames
constexpr uint32_t kLooping = 0x5EA0E211;
constexpr uint32_t kBackwards = 0xF8CFD515;

using gfx::AnimTimeline;

// The base movie's code (VideoEntry, node_objects.h) and the base's invoke_method_by_name (0x004A0CF0).
VideoEntry* current_base(void* movie) { return static_cast<VideoEntry*>(movie); }
void base_construct(void* movie) { current_base(movie)->make_new(); }
void base_destroy_body(void* movie) { current_base(movie)->destroy_body(); }
void base_load(void* movie, script::Struct* params) { current_base(movie)->configure_from_data(params); }
void set_held(void* movie, int value) { current_base(movie)->set_flag_1b(static_cast<uint8_t>(value)); }
// The movie's name (movie_manager.h).
uint32_t get_name_of(void* movie) { return movie_key_from_params(movie); }
// The asset manager's asset by name (null if none).
void* find_asset(uint32_t name) { return assets::g_asset_manager->get_asset_data(name, 0); }
// The movie's own Start (vtable slot 3).
void start_movie(Movie* movie) { reinterpret_cast<bool(__thiscall*)(Movie*)>(movie->get_vtable()[3])(movie); }

// The timeline's code: constructor / destructor body, complete, PlayAnim (slot 3), Update (slot 2).
void construct_timeline(AnimTimeline* t) { t->make_new(); }
void destroy_timeline(AnimTimeline* t) { t->destroy_body(); }
bool is_accomplished(AnimTimeline* t) { return t->is_accomplished(); }
void play_anim(AnimTimeline* t, uint32_t anim, float start, float end, int loop_type, float blend, float speed)
{
    reinterpret_cast<void(__thiscall*)(AnimTimeline*, uint32_t, float, float, int, float, float)>(t->vtable_[3])(
        t, anim, start, end, loop_type, blend, speed);
}
void update_timeline(AnimTimeline* t) { reinterpret_cast<void(__thiscall*)(AnimTimeline*)>(t->vtable_[2])(t); }

// The animation asset's code: started (0x00482F10); told a step from one time to another (0x00482F40).
void start_asset(void* anim) { static_cast<gfx::JointClipFrames*>(anim)->arm_events(); }
void step_asset(void* anim, float from, float to, gfx::ViewCam* camera, bool first)
{
    static_cast<gfx::JointClipFrames*>(anim)->fire_events(from, to, camera, first);
}

float seconds_per_frame() { return g_anim_movies_seconds_per_frame; } // 1/60

constexpr uint32_t kFixedTarget = 0xC588EEBC; // a camera target that is a point (the target offset), not an object

template <typename T>
T& prop(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// 0x00463AA0 (cdecl, cutscene.h): a rotation matrix from a quaternion (negated in place on the way) with `origin` as
// its last row.
void quat_to_matrix(float* quat, float* origin, float* matrix) { objects::quat_to_matrix(quat, origin, matrix); }

void set_identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// out = a x b (x, y and z; each a difference of products, rounded once).
void cross_into(float* out, const float* a, const float* b)
{
    double x = static_cast<double>(a[1]) * b[2] - static_cast<double>(a[2]) * b[1];
    double y = static_cast<double>(a[2]) * b[0] - static_cast<double>(a[0]) * b[2];
    double z = static_cast<double>(a[0]) * b[1] - static_cast<double>(a[1]) * b[0];
    out[0] = static_cast<float>(x);
    out[1] = static_cast<float>(y);
    out[2] = static_cast<float>(z);
}

// Play: the asset's length (its first word) from 0, or backwards from a frame before its end to 0.
void play_on(AnimTimeline* t, void* anim, int loop_type, bool backwards)
{
    float length = *static_cast<float*>(anim);
    if (backwards)
        play_anim(t, 0, static_cast<float>(static_cast<double>(length) - seconds_per_frame()), 0.0f, loop_type, 0.0f, 1.0f);
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
        bool backwards = params->has_bare_flag(kBackwards);
        int loop_type = params->has_bare_flag(kLooping) ? 1 : 0;
        movie->perform(anim, loop_type, backwards);
    } else {
        int frames = 0;
        params->fetch_int_value(kFrames, &frames, 1);
        movie->anim_ = nullptr;
        play_anim(&movie->timeline_, 0, 0.0f, static_cast<float>(frames * static_cast<double>(seconds_per_frame())), 0, 0.0f,
                 1.0f);
    }
    set_held(movie, 1);
    return true;
}

} // namespace

// --- the object animation movie -------------------------------------------------------------------------------

// 0x005871B0
ObjectAnimMovie* ObjectAnimMovie::make_new()
{
    base_construct(this);
    base_.list_link_.vtable = kObjectAnimMovieVtable;
    construct_timeline(&timeline_);
    anim_ = nullptr;
    return this;
}

// 0x00587210
void ObjectAnimMovie::destroy_body()
{
    base_.list_link_.vtable = kObjectAnimMovieVtable;
    destroy_timeline(&timeline_);
    base_destroy_body(this);
}

// 0x00587370
ObjectAnimMovie* ObjectAnimMovie::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x005875D0
bool ObjectAnimMovie::read_in(script::Struct* params)
{
    base_load(this, params);
    if (!load_anim(this, params))
        return false;
    start_movie(&base_);
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
    if (base_.unknown1a_)
        return true;
    if (base_.unknown18_)
        return false;
    return is_accomplished(&timeline_);
}
int ObjectAnimMovie::is_held_and_complete() { return base_.unknown18_ && is_accomplished(&timeline_) ? 1 : 0; }

// --- the camera animation movie -------------------------------------------------------------------------------

// 0x005873F0
CameraAnimMovie* CameraAnimMovie::make_new()
{
    base_construct(this);
    base_.list_link_.vtable = kCameraAnimMovieVtable;
    construct_timeline(&timeline_);
    anim_ = nullptr;
    has_goal_ = false;
    aim_point_ = 0;
    reinterpret_cast<uint32_t&>(field_of_view_) = 0;
    return this;
}

// 0x005872C0
void CameraAnimMovie::destroy_body()
{
    base_.list_link_.vtable = kCameraAnimMovieVtable;
    destroy_timeline(&timeline_);
    base_destroy_body(this);
}

// 0x00587450
CameraAnimMovie* CameraAnimMovie::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00587F70
bool CameraAnimMovie::read_in(script::Struct* params)
{
    base_load(this, params);
    float fov;
    if (params->fetch_float(0x17E7321E, &fov, 0)) {
        field_of_view_ = fov;
        base_.viewer_->change_h_fov(fov);
    }
    if (!load_anim(this, params))
        return false;
    load_target(params);
    start_movie(&base_);
    place_camera();
    if (anim_)
        step_asset(anim_, timeline_.ongoing_, timeline_.ongoing_, base_.viewer_, true);
    return true;
}

// 0x00587480
void CameraAnimMovie::load_target(script::Struct* params)
{
    has_goal_ = params->read_hash_value(0x531E4D28, &aim_point_, 0);
    if (!has_goal_)
        return;
    goal_shift_[0] = goal_shift_[1] = goal_shift_[2] = 0.0f;
    goal_shift_[3] = 1.0f;
    params->fetch_vec(0x906642BF, reinterpret_cast<script::VectorValue*>(goal_shift_), 0);
    float zero = g_float_zero;
    // (not equal, or unordered: a NaN counts as set)
    target_offset_set_ = !(goal_shift_[0] == zero) || !(goal_shift_[1] == zero) || !(goal_shift_[2] == zero);
    look_offset_[0] = look_offset_[1] = look_offset_[2] = 0.0f;
    look_offset_[3] = 1.0f;
    look_offset_set_ = params->fetch_vec(0xA00D3D6E, reinterpret_cast<script::VectorValue*>(look_offset_), 0);
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
    if (base_.unknown1a_)
        return true;
    if (base_.unknown18_)
        return false;
    return is_accomplished(&timeline_);
}
int CameraAnimMovie::is_held_and_complete() { return base_.unknown18_ && is_accomplished(&timeline_) ? 1 : 0; }

// 0x00587590
bool CameraAnimMovie::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
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
    return reinterpret_cast<bool(__thiscall*)(void*, uint32_t, script::Struct*, script::Script*)>(
        code_address(&render::ViewRegion::query_point))(this, name, params, script);
}

// 0x00588090
void CameraAnimMovie::tick_frame()
{
    float from = timeline_.ongoing_;
    update_timeline(&timeline_);
    base_.viewer_->remember_prev_pos();
    if (anim_)
        step_asset(anim_, from, timeline_.ongoing_, base_.viewer_, false);
    place_camera();
}

// 0x00587A50
void CameraAnimMovie::place_camera()
{
    float* pos = reinterpret_cast<float*>(base_.viewer_->fetch_location());
    float* m = reinterpret_cast<float*>(base_.viewer_->fetch_mat());
    // A skater (of the two) whose 0x40 flag is up: the base's 0x00468F20(1).
    uint8_t** skaters = reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(modules::g_front_end_instance) + 0x20);
    for (int i = 0; i < 2; ++i) {
        if (prop<uint8_t>(prop<uint8_t*>(skaters[i], 0x18), 0x20) & 0x40)
            current_base(this)->check_done(true);
    }
    if (anim_) {
        // The camera from the asset's frame: its position and its rotation.
        float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float at[4];
        bool flag = false;
        static_cast<gfx::JointClipFrames*>(anim_)->get_float_key_pose(quat, at, &flag, timeline_.ongoing_, 0);
        for (int i = 0; i < 4; ++i)
            pos[i] = at[i];
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        quat_to_matrix(quat, origin, m);
    } else {
        // Where it is; the field of view (0: the script global's).
        pos[0] = pos[1] = pos[2] = 0.0f;
        pos[3] = 1.0f;
        set_identity(m);
        float fov = field_of_view_;
        if (fov == g_float_zero)
            fov = script::fetch_float(0x99529205);
        base_.viewer_->change_h_fov(fov);
    }
    if (!has_goal_)
        return;
    float dir[4];
    if (aim_point_ != kFixedTarget) {
        // Aimed at an object (none: as it was): from its position (moved by the offsets in its frame).
        auto* object = reinterpret_cast<uint8_t*>(reinterpret_cast<ObjectHubBase*>(g_composite_registry)->look_up_entity(aim_point_));
        if (!object)
            return;
        float target[4] = {prop<float>(object, 0x4C), prop<float>(object, 0x50), prop<float>(object, 0x54), 1.0f};
        float frame[16];
        auto moved = [&](float* offset) {
            *reinterpret_cast<maths::Mat4*>(frame) = *reinterpret_cast<const maths::Mat4*>(object + 0x6C);
            frame[12] = target[0];
            frame[13] = target[1];
            frame[14] = target[2];
            frame[15] = 1.0f;
            reinterpret_cast<maths::Mat4*>(frame)->offset_local(*reinterpret_cast<const maths::Vec4*>(offset));
        };
        if (target_offset_set_) {
            moved(goal_shift_);
            for (int i = 0; i < 4; ++i)
                target[i] = frame[12 + i];
        }
        if (look_offset_set_) {
            for (int i = 0; i < 4; ++i)
                pos[i] = target[i];
            moved(look_offset_);
            for (int i = 0; i < 4; ++i)
                pos[i] = frame[12 + i];
        }
        for (int i = 0; i < 4; ++i)
            dir[i] = pos[i] - target[i];
        reinterpret_cast<maths::Vec4*>(dir)->make_unit();
        set_identity(m);
        for (int i = 0; i < 4; ++i)
            m[8 + i] = dir[i];
        cross_into(m, m + 4, m + 8); // the right axis: up x at
        m[3] = 0.0f;
        reinterpret_cast<maths::Vec4*>(m)->make_unit();
    } else {
        // A fixed point (the target offset), seen from the look offset.
        for (int i = 0; i < 4; ++i)
            pos[i] = look_offset_[i];
        // (x and y stay unrounded; z is stored and squared as its unrounded value times the stored one)
        double x = static_cast<double>(pos[0]) - goal_shift_[0];
        double y = static_cast<double>(pos[1]) - goal_shift_[1];
        double z_exact = static_cast<double>(pos[2]) - goal_shift_[2];
        float z = static_cast<float>(z_exact);
        double length = std::sqrt(z_exact * z + y * y + x * x);
        if (length > g_float_zero) {
            double inverse = g_anim_movies_one / length;
            x *= inverse;
            y *= inverse;
            z = static_cast<float>(z * inverse);
        }
        set_identity(m);
        m[8] = static_cast<float>(x);
        m[10] = z;
        m[9] = static_cast<float>(y);
        m[11] = 0.0f;
        cross_into(m, m + 4, m + 8);
        m[3] = 0.0f;
        double a = m[0], b = m[1], c = m[2];
        double size = std::sqrt(a * a + b * b + c * c);
        if (size > g_float_zero) {
            double inverse = g_anim_movies_one / size;
            m[0] = static_cast<float>(inverse * m[0]);
            m[1] = static_cast<float>(inverse * m[1]);
            m[2] = static_cast<float>(m[2] * inverse);
        }
    }
    // The up axis: at x right.
    cross_into(m + 4, m + 8, m);
    m[7] = 0.0f;
}

// 0x005876A0 (and the thunk 0x00587F60)
void ObjectAnimMovie::tick_frame()
{
    float from = timeline_.ongoing_;
    update_timeline(&timeline_);
    if (anim_)
        step_asset(anim_, from, timeline_.ongoing_, base_.viewer_, false);
    // Every object the asset moves: its frame's rotation and position.
    int count = prop<int>(anim_, 8);
    auto* quats = static_cast<float*>(memory::alloc(count * 16));
    if (quats) {
        for (int i = 0; i < count; ++i) {
            quats[4 * i] = quats[4 * i + 1] = quats[4 * i + 2] = 0.0f;
            quats[4 * i + 3] = 1.0f;
        }
    }
    auto* positions = static_cast<float*>(memory::alloc(prop<int>(anim_, 8) * 16));
    auto* flags = static_cast<bool*>(memory::alloc(prop<int>(anim_, 8)));
    static_cast<gfx::JointClipFrames*>(anim_)->get_float_key_pose(quats, positions, flags, timeline_.ongoing_, 0);
    for (int i = 0; i < prop<int>(anim_, 8); ++i) {
        uint32_t name = static_cast<gfx::JointClipFrames*>(anim_)->fetch_joint_name(i);
        uint8_t* object = reinterpret_cast<uint8_t*>(objects::find_object(name));
        if (!object) {
            debug::printf("object animation:  couldn't find object %s\n", script::find_checksum_name_or_unknown(name));
            continue;
        }
        float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float matrix[16];
        quat_to_matrix(quats + 4 * i, origin, matrix);
        std::memcpy(object + 0x6C, matrix, sizeof(matrix)); // its matrix
        std::memcpy(object + 0xC0, matrix, sizeof(matrix)); // and its display matrix
        std::memcpy(object + 0x4C, positions + 4 * i, 16);  // its position
        if (script::fetch_int_value(0x2F510F45)) {
            debug::printf("Object %s @ time %f:  ", script::find_checksum_name_or_unknown(name),
                        static_cast<double>(timeline_.ongoing_));
            reinterpret_cast<const maths::Vec4*>(object + 0x4C)->echo_out(); // (printed)
            reinterpret_cast<const maths::Mat4*>(object + 0x6C)->echo_out();
        }
        // The object's model told (slot 1 of its 0x286A8D26 component).
        void* component = reinterpret_cast<GameEntity*>(object)->fetch_module(0x286A8D26);
        if (component)
            reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(component))[1])(component);
    }
    memory::Hub::get_pull()->dealloc(quats);
    memory::Hub::get_pull()->dealloc(positions);
    memory::Hub::get_pull()->dealloc(flags);
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_object_anim_movie_vtable_slots[] = {&objects::ObjectAnimMovie::teardown,
         &objects::ObjectAnimMovie::read_in,
         &objects::ObjectAnimMovie::tick_frame,
         &objects::ObjectAnimMovie::kickoff,
         &render::ViewRegion::query_point,
         &objects::ObjectAnimMovie::is_finished,
         &objects::ObjectAnimMovie::is_held_and_complete,
         stubs::return_false,
         stubs::do_nothing,
         stubs::return_true,
         stubs::return_false,
         stubs::do_nothing,
         stubs::ignore_argument,
         stubs::do_nothing};
void** const kObjectAnimMovieVtable = reinterpret_cast<void**>(g_object_anim_movie_vtable_slots);
static VtableSlot g_camera_anim_movie_vtable_slots[] = {&objects::CameraAnimMovie::teardown,
         &objects::CameraAnimMovie::read_in,
         &objects::CameraAnimMovie::tick_frame,
         &objects::CameraAnimMovie::kickoff,
         &objects::CameraAnimMovie::invoke_method_by_name,
         &objects::CameraAnimMovie::is_finished,
         &objects::CameraAnimMovie::is_held_and_complete,
         stubs::return_true,
         stubs::do_nothing,
         stubs::return_true,
         stubs::return_true,
         stubs::do_nothing,
         stubs::ignore_argument,
         stubs::do_nothing};
void** const kCameraAnimMovieVtable = reinterpret_cast<void**>(g_camera_anim_movie_vtable_slots);

} // namespace
} // namespace Obj
