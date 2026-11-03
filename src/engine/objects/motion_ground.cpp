// The motion component (GameObjects::KinematicsModule, motion.h): jumping, sticking to the ground, and moving to points
// (Obj_MoveTo*, Obj_MoveForward / Left, obj_skip_to_restart).

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_tests.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_internal.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/maths/maths.h"

namespace objects {

using namespace motion_detail;

namespace {
constexpr double kStepDistance = 2.0;
constexpr float kHalf = 0.5f;
constexpr float kTwo = 2.0f;

constexpr uint32_t kDistAbove = 0xA8A5A3C7;    // DistAbove
constexpr uint32_t kDistBelow = 0x182D87A7;    // DistBelow
constexpr uint32_t kTime = 0x906B67BA;         // time
constexpr uint32_t kAcceleration = 0x85894A84; // Acceleration
constexpr uint32_t kOrient = 0x90A91232;       // orient
constexpr uint32_t kDist = 0x7E832F08;         // dist
constexpr uint32_t kLinkNum = 0xE85997D0;      // LinkNum
constexpr uint32_t kRandomLink = 0x90A1C52A;   // randomlink
constexpr uint32_t kModel = 0x286A8D26;        // the model component

// Miles per hour to inches per second (63360 inches a mile, 1 / 3600 hours a second: 0x006461E4, 0x006461E0).
constexpr float kInchesPerMile = get_bits(0x47778000);
constexpr float kHoursPerSecond = get_bits(0x3991A2B4);

// A vertex of a collision object in the world.
maths::Vec4 vertex_position3(render::HitMesh* object, int vertex) { return object->read_vertex_point(vertex); }

} // namespace

// 0x00425CB0
bool get_ground_hit(std::array<maths::Vec4, 3>& triangle, const maths::Vec4* end, const maths::Vec4* start, maths::Vec4* out)
{
    const maths::Vec4 along = {.x = static_cast<float>(static_cast<double>(end->x) - start->x),
                               .y = static_cast<float>(static_cast<double>(end->y) - start->y),
                               .z = static_cast<float>(static_cast<double>(end->z) - start->z),
                               .w = static_cast<float>(static_cast<double>(end->w) - start->w)};
    float t = 0.0f;
    if (render::ray_hits_triangle(*start, along, std::get<0>(triangle), std::get<1>(triangle), std::get<2>(triangle), &t)) {
        // (x through the unrounded product, the others rounded first)
        const double x = static_cast<double>(along.x) * t;
        auto y = static_cast<float>(static_cast<double>(along.y) * t);
        auto z = static_cast<float>(static_cast<double>(along.z) * t);
        auto w = static_cast<float>(static_cast<double>(along.w) * t);
        out->z = static_cast<float>(static_cast<double>(z) + start->z);
        out->w = static_cast<float>(static_cast<double>(w) + start->w);
        out->x = static_cast<float>(x + start->x);
        out->y = static_cast<float>(static_cast<double>(y) + start->y);
        return true;
    }
    render::ProbeRay feeler;
    if (!feeler.get_collision_along(start, end, false))
        return false;
    // (the face hit becomes the triangle tried first next time)
    render::HitMesh* object = feeler.hit().object;
    const render::TriMeshSource* geometry = object->geometry();
    const int face = static_cast<int>(feeler.hit().face);
    for (int corner = 0; corner < 3; ++corner) {
        const auto vertex = static_cast<int>(geometry->face_vertex(face, corner));
        core::at(triangle, corner) = vertex_position3(object, vertex);
    }
    *out = feeler.contact_pos();
    return true;
}

// 0x00424870
void KinematicsModule::update_jump()
{
    const maths::Vec4& pos = entity()->at_point();
    float x = pos.x;
    float z = pos.z;
    float w = pos.w;
    const double fall = static_cast<double>(leap_pull_) * step_seconds_;
    const double rise = static_cast<double>(jump_speed_) * step_seconds_ + fall * step_seconds_ * 0.5;
    if (jump_has_heading_) {
        // (along the heading at the start's speed: x and z through rounded products, w through an unrounded one)
        const float frame = step_seconds_;
        const double speed = jump_start_speed_;
        const double along_w = speed * leap_direction_.w * frame;
        auto along_z = static_cast<float>(speed * leap_direction_.z);
        auto along_x = static_cast<float>(speed * leap_direction_.x);
        x = static_cast<float>(static_cast<double>(along_x) * frame + x);
        z = static_cast<float>(static_cast<double>(z) + static_cast<double>(along_z) * frame);
        w = static_cast<float>(static_cast<double>(w) + along_w);
    }
    auto y = static_cast<float>(static_cast<double>(pos.y) + rise);
    jump_speed_ = static_cast<float>(fall + jump_speed_);
    if (rise < 0.0) {
        // (coming down: landed at land_height, or on whatever is below)
        if (jump_has_land_height_) {
            if (y < leap_touchdown_y_) {
                motion_flags_ &= ~kMotionJumping;
                y = leap_touchdown_y_;
            }
        } else {
            render::ProbeRay feeler;
            auto above = static_cast<float>(static_cast<double>(w) + 1.0f);
            feeler.line().start = {.x = x, .y = static_cast<float>(static_cast<double>(y) + jump_col_above_), .z = z, .w = above};
            feeler.line().end = {.x = x, .y = static_cast<float>(static_cast<double>(y) + -jump_col_below_), .z = z, .w = above};
            if (feeler.get_hit_shape(true, false))
                motion_flags_ &= ~kMotionJumping;
        }
    }
    jump_pos_ = {.x = x, .y = y, .z = z, .w = w};
}

// 0x00425F40
bool KinematicsModule::stick_to_ground()
{
    if (ground_above_ == 0.0f && ground_below_ == 0.0f)
        return false;
    maths::Mat4& matrix = entity()->basis_frame();
    maths::Vec4& pos = entity()->at_point();
    if (ground_height_only_) {
        // The right axis from up and at, then the ground under it: the height to make up (Update eases it in).
        const maths::Vec4& up = core::at(matrix.row, 1);
        const maths::Vec4& at = core::at(matrix.row, 2);
        auto right_x = static_cast<float>(static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z);
        auto right_y = static_cast<float>(static_cast<double>(up.z) * at.x - static_cast<double>(at.z) * up.x);
        auto right_z = static_cast<float>(static_cast<double>(at.y) * up.x - static_cast<double>(up.y) * at.x);
        core::at(matrix.row, 0) = {.x = right_x, .y = right_y, .z = right_z, .w = 0.0f};
        core::at(matrix.row, 0).make_unit();
        auto above = static_cast<float>(std::fabs(static_cast<double>(ground_above_)) * maths::kInchesPerFoot);
        const float below = -static_cast<float>(std::fabs(static_cast<double>(ground_below_)) * 12.0f);
        auto w = static_cast<float>(static_cast<double>(pos.w) + 1.0f);
        const maths::Vec4 start = {.x = pos.x, .y = static_cast<float>(static_cast<double>(above) + pos.y), .z = pos.z, .w = w};
        const maths::Vec4 end = {.x = pos.x, .y = static_cast<float>(static_cast<double>(below) + pos.y), .z = pos.z, .w = w};
        maths::Vec4 hit;
        if (!get_ground_hit(ground_triangle_, &end, &start, &hit))
            return false;
        ground_offset_ = static_cast<float>(static_cast<double>(hit.y) - pos.y);
        return true;
    }

    // Three feelers - behind on the right, behind on the left, ahead in the middle (72 back or ahead, 30 to the
    // side): the ground's points give the height and the orientation.
    const render::ProbeRay feeler; // (made and never used)
    maths::Vec4& right = core::at(matrix.row, 0);
    maths::Vec4& up = core::at(matrix.row, 1);
    maths::Vec4& at = core::at(matrix.row, 2);
    up = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    {
        auto z = static_cast<float>(static_cast<double>(up.x) * at.y - static_cast<double>(up.y) * at.x);
        auto y = static_cast<float>(static_cast<double>(up.z) * at.x - static_cast<double>(up.x) * at.z);
        auto x = static_cast<float>(static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z);
        right = {.x = x, .y = y, .z = z, .w = 0.0f};
        right.make_unit();
    }
    const std::array<float, 4> ahead = {{
        static_cast<float>(static_cast<double>(at.x) * 72.0f), static_cast<float>(static_cast<double>(at.y) * 72.0f),
        static_cast<float>(static_cast<double>(at.z) * 72.0f), static_cast<float>(static_cast<double>(at.w) * 72.0f)}};
    const std::array<float, 4> side = {{static_cast<float>(static_cast<double>(right.x) * 30.0f),
                           static_cast<float>(static_cast<double>(right.y) * 30.0f),
                           static_cast<float>(static_cast<double>(right.z) * 30.0f),
                           static_cast<float>(static_cast<double>(right.w) * 30.0f)}};
    auto move = [&pos](const std::array<float, 4>& by, double scale) {
        auto& p = pos;
        for (int i = 0; i < 4; ++i)
            p[i] = static_cast<float>(static_cast<double>(p[i]) + core::at(by, i) * scale);
    };
    // (a feeler from DistAbove over the position to DistBelow under it, feet; the products unrounded)
    auto line = [&](maths::Vec4* start, maths::Vec4* end) {
        const float x = pos.x;
        const float y = pos.y;
        const float z = pos.z;
        const float w = pos.w;
        *start = {.x = x, .y = static_cast<float>(static_cast<double>(ground_above_) * maths::kInchesPerFoot + y), .z = z, .w = w};
        *end = {.x = x, .y = static_cast<float>(static_cast<double>(y) - static_cast<double>(ground_below_) * maths::kInchesPerFoot), .z = z, .w = w};
    };
    maths::Vec4 start;
    maths::Vec4 end;
    maths::Vec4 back_right;
    maths::Vec4 back_left;
    maths::Vec4 front;
    move(ahead, -1.0);
    move(side, 1.0);
    line(&start, &end);
    const bool hit_right = get_ground_hit(core::at(ground_triangles_, 0), &end, &start, &back_right);
    move(side, -kStepDistance);
    line(&start, &end);
    bool hit_left = false;
    if (get_ground_hit(core::at(ground_triangles_, 1), &end, &start, &back_left)) {
        hit_left = true;
        if (hit_right) {
            right = {.x = static_cast<float>(static_cast<double>(back_right.x) - back_left.x),
                     .y = static_cast<float>(static_cast<double>(back_right.y) - back_left.y),
                     .z = static_cast<float>(static_cast<double>(back_right.z) - back_left.z),
                     .w = static_cast<float>(static_cast<double>(back_right.w) - back_left.w)};
            right.make_unit();
        }
    }
    move(ahead, kStepDistance);
    move(side, 1.0);
    line(&start, &end);
    move(ahead, -1.0); // (back where it was)
    if (!get_ground_hit(core::at(ground_triangles_, 2), &end, &start, &front) || !hit_left || !hit_right)
        return false;
    // The height: half way between the back's middle and the front; at: towards the front from the back's
    // middle, half way from the old at; up: at x right.
    pos.y = static_cast<float>(static_cast<double>(back_left.y) + back_right.y);
    pos.y = static_cast<float>(static_cast<double>(pos.y) * kHalf);
    pos.y = static_cast<float>(static_cast<double>(front.y) + pos.y);
    pos.y = static_cast<float>(static_cast<double>(pos.y) * kHalf);
    maths::Vec4 back = back_right + back_left;
    back /= kTwo;
    back -= front;
    back *= -1.0f;
    back.make_unit();
    at += back;
    at /= kTwo;
    up = maths::cross_vec(at, right);
    up.make_unit();
    return true;
}

// 0x004269F0
bool KinematicsModule::move_to(script::Struct* params, script::Script*, maths::Vec4 target, uint32_t node)
{
    move_target_ = target;
    const maths::Vec4& pos = entity()->at_point();
    const double dx = static_cast<double>(move_target_.x) - pos.x;
    const double dy = static_cast<double>(move_target_.y) - pos.y;
    const double dz = static_cast<double>(move_target_.z) - pos.z;
    move_acceleration_ = 0.0f;
    move_distance_ = static_cast<float>(std::sqrt((dz * dz + dy * dy) + dx * dx));
    move_start_ = pos;
    move_done_ = 0.0f;
    move_arrive_node_ = 0;
    motion_flags_ = (motion_flags_ & ~kMotionFollowingPath) | kMotionMoving;
    float time = target.x; // (the original reads `time` into the target's x argument)
    if (params->fetch_float(kTime, &time, 0)) {
        move_speed_ = static_cast<float>(static_cast<double>(move_distance_) / time);
        return false;
    }
    float speed = 0.0f;
    if (!params->fetch_float(kSpeed, &speed, 0)) {
        // (no speed or time: there at once)
        entity()->at_point() = move_target_;
        motion_flags_ &= ~kMotionMoving;
        if (node && params->has_bare_flag(kOrient))
            face_node(level_nodes::fetch_vertex(node, true));
        return true;
    }
    int units = 2; // (miles per hour)
    if (params->has_bare_flag(kMph))
        units = 2;
    else if (params->has_bare_flag(kFps))
        units = 1;
    else if (params->has_bare_flag(kIps))
        units = 0;
    params->fetch_float(kAcceleration, &move_acceleration_, 0);
    double inches = speed; // (kept unrounded)
    if (units == 2) {
        inches = inches * kInchesPerMile * kHoursPerSecond;
        move_acceleration_ =
            static_cast<float>(static_cast<double>(move_acceleration_) * kInchesPerMile * kHoursPerSecond);
    } else if (units == 1) {
        inches = inches * maths::kInchesPerFoot;
        move_acceleration_ = static_cast<float>(static_cast<double>(move_acceleration_) * maths::kInchesPerFoot);
    }
    if (move_acceleration_ == 0.0f) {
        move_speed_ = static_cast<float>(inches);
        return false;
    }
    move_max_speed_ = static_cast<float>(inches);
    move_speed_ = 0.0f;
    return false;
}

// 0x00426C10
void KinematicsModule::glide_to_node(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 0);
    if (!name)
        return;
    maths::Vec4 pos;
    level_nodes::fetch_place(name, &pos);
    if (move_to(params, script, pos, name))
        arrive_at_node(name);
    else
        move_arrive_node_ = name;
}

// 0x00426CB0
void KinematicsModule::move_to_position(script::Struct* params, script::Script* script, bool absolute)
{
    maths::Vec4 pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    params->fetch_vec(0u, &pos, 0);
    if (params->has_bare_flag(kMaxCoords))
        pos.negate_x_rotate_x90();
    if (!absolute) {
        const maths::Vec4& at = entity()->at_point();
        pos = {.x = pos.x + at.x, .y = pos.y + at.y, .z = pos.z + at.z, .w = pos.w + at.w};
    }
    move_to(params, script, pos, 0);
}

// 0x00426D80
void KinematicsModule::move_to_link(script::Struct* params, script::Script* script)
{
    int link = 0;
    const int links = static_cast<int>(level_nodes::get_link_count(list_link_));
    if (!links)
        return;
    if (params->fetch_int_value(kLinkNum, &link, 0))
        link -= 1;
    else if (links > 1 && params->has_bare_flag(kRandomLink))
        link = maths::rnd(links);
    script::Struct* node = level_nodes::fetch_neighbour_node(list_link_, link);
    const uint32_t name = level_nodes::get_node_name(node);
    maths::Vec4 pos;
    level_nodes::fetch_place(node, &pos);
    if (move_to(params, script, pos, 0))
        arrive_at_node(name);
    else
        move_arrive_node_ = name;
}

// The body of Obj_MoveForward / obj_move_left: `dist` along the matrix row (x, y, z through rounded products, w
// through an unrounded one).
void KinematicsModule::move_along(script::Struct* params, script::Script* script, const maths::Vec4& direction)
{
    float distance = 0.0f;
    params->fetch_float(kDist, &distance, 0);
    const maths::Vec4& pos = entity()->at_point();
    const double w = static_cast<double>(distance) * direction.w;
    auto z = static_cast<float>(static_cast<double>(distance) * direction.z);
    auto y = static_cast<float>(static_cast<double>(distance) * direction.y);
    auto x = static_cast<float>(static_cast<double>(distance) * direction.x);
    const maths::Vec4 target = {.x = static_cast<float>(static_cast<double>(x) + pos.x),
                                .y = static_cast<float>(static_cast<double>(y) + pos.y),
                                .z = static_cast<float>(static_cast<double>(z) + pos.z), .w = static_cast<float>(w + pos.w)};
    move_to(params, script, target, 0);
}

// --- script member functions ---

// 0x004268C0
bool KinematicsModule::obj_stick_to_ground(script::Struct* params, script::Script*)
{
    params->fetch_float(kDistAbove, &ground_above_, 0);
    params->fetch_float(kDistBelow, &ground_below_, 0);
    if (params->has_bare_flag(kOff)) {
        motion_flags2_ &= ~kMotion2StickToGround;
        return true;
    }
    motion_flags2_ |= kMotion2StickToGround;
    if (ground_above_ == 0.0f && ground_below_ == 0.0f)
        return false;
    stick_to_ground();
    return true;
}

// 0x00426960
bool KinematicsModule::obj_skip_to_restart(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    maths::Vec4 pos;
    level_nodes::fetch_place(node, &pos);
    entity()->at_point() = pos;
    face_node(node);
    // (its model brought up to date: a crash without a model component)
    ObjectModule* model = entity()->fetch_module(kModel);
    model->tick_frame();
    return true;
}

// 0x004272D0
bool KinematicsModule::obj_move_to_pos_stored(script::Struct* params, script::Script* script)
{
    move_to(params, script, stored_pos_, 0);
    return true;
}

// 0x00427320
bool KinematicsModule::obj_move_forward(script::Struct* params, script::Script* script)
{
    move_along(params, script, core::at(entity()->basis_frame().row, 2));
    return true;
}

// 0x004273D0
bool KinematicsModule::obj_move_left(script::Struct* params, script::Script* script)
{
    move_along(params, script, core::at(entity()->basis_frame().row, 0));
    return true;
}

// 0x00427470
bool KinematicsModule::obj_move_to_pos(script::Struct* params, script::Script* script)
{
    move_to_position(params, script, true);
    return true;
}

// 0x00427490
bool KinematicsModule::obj_move_to_rel_pos(script::Struct* params, script::Script* script)
{
    move_to_position(params, script, false);
    return true;
}

// 0x004274B0
bool KinematicsModule::obj_move_to_link(script::Struct* params, script::Script* script)
{
    move_to_link(params, script);
    return true;
}

} // namespace objects

