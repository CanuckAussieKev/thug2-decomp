// Following a path of linked nodes (0x00469B00-0x0046AA9F). See path_follower.h.
//
// The original keeps many intermediate values on the x87 stack between instructions; where it does, they're kept
// in double here (and rounded to float where it stores them), in the original's order of operations.

#include "engine/objects/path_follower.h"
#include "platform/d3dx.h"

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/motion.h"
#include "engine/objects/entity.h"
#include "engine/script/nodes.h"
#include "engine/maths/random.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

constexpr uint32_t kPathComponent = 0x0A015E17; // the owner's component that picks links and hears of nodes

ObjectModule* path_component(GameEntity* owner) { return owner->fetch_module(kPathComponent); }
// The component's choice: a random link (true) or the first (not rewritten yet).
bool picks_random_link(ObjectModule* component)
{
    return core::down_cast<KinematicsModule>(component)->picks_random_link();
}
// The component told that `node` has been passed (not rewritten yet).
void node_passed(ObjectModule* component, uint32_t node)
{
    core::down_cast<KinematicsModule>(component)->arrive_at_node(node);
}
// x, y and z scaled to `length` (0x0042BBF0, not rewritten yet).
void set_length(maths::Vec4& v, float length) { v.set_length(length); }
// D3DXMatrixMultiply (linked library code).
void d3dx_multiply(maths::Mat4& out, const maths::Mat4& a, const maths::Mat4& b)
{
    d3dx::matrix_multiply(&out, &a, &b);
}

// The length of a - b, unrounded: (z, y, then x) squared and summed.
double span_to(const maths::Vec4& a, const maths::Vec4& b)
{
    const double x = static_cast<double>(a.x) - b.x;
    const double y = static_cast<double>(a.y) - b.y;
    const double z = static_cast<double>(a.z) - b.z;
    return std::sqrt(z * z + y * y + x * x);
}
// A vector's length, unrounded (0x004023C0's maths: x, y, then z).
double len_of(const maths::Vec4& v)
{
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
}
// 0x00402530's maths, unrounded (z, y, then x).
double get_dot(const maths::Vec4& a, const maths::Vec4& b)
{
    return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x;
}

} // namespace

// 0x0046A1D0
PathFollower::PathFollower(GameEntity* owner) : owner_(owner)
{
    core::at(tick_.row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(tick_.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(tick_.row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(tick_.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    segment_origin_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    unknown84_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    end_point_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x00469B00
void PathFollower::read_node_position()
{
    level_nodes::fetch_place(level_nodes::fetch_vertex(list_link_, true), &end_point_);
    if (level_nodes::get_link_count(list_link_) == 0)
        flag_bits_ |= kFlagLastNode;
    else
        flag_bits_ &= ~kFlagLastNode;
}

// 0x0046A120
bool PathFollower::get_next_node()
{
    const uint32_t links = level_nodes::get_link_count(list_link_);
    if (links == 0)
        return false;
    last_node_ = list_link_;
    if (picks_random_link(path_component(owner_)))
        list_link_ = level_nodes::fetch_link(list_link_, maths::rnd(static_cast<int>(links)));
    else
        list_link_ = level_nodes::fetch_link(list_link_, 0);
    segment_origin_ = end_point_;
    read_node_position();
    return true;
}

// 0x00469F60
void PathFollower::refresh_placement()
{
    at_point_ = segment_origin_;
    const double dx = static_cast<double>(end_point_.x) - segment_origin_.x;
    const double dy = static_cast<double>(end_point_.y) - segment_origin_.y;
    const float dz = end_point_.z - segment_origin_.z;
    const float dw = end_point_.w - segment_origin_.w;
    auto t = static_cast<float>(static_cast<double>(along_) / byte_count_);
    auto sx = static_cast<float>(dx * t);
    const double sy = dy * t;
    const double sz = static_cast<double>(dz) * t;
    auto sw = static_cast<float>(static_cast<double>(dw) * t);
    at_point_.x = static_cast<float>(static_cast<double>(sx) + at_point_.x);
    at_point_.y = static_cast<float>(sy + at_point_.y);
    at_point_.z = static_cast<float>(sz + at_point_.z);
    at_point_.w = static_cast<float>(static_cast<double>(sw) + at_point_.w);
}

// 0x0046A000
bool PathFollower::check_corner()
{
    if (corner_distance_ != 0.0f) {
        if (level_nodes::get_link_count(list_link_) == 0)
            return false;
        maths::Vec4 next;
        level_nodes::fetch_place(level_nodes::fetch_neighbour_node(list_link_, 0), &next);
        const double between = span_to(next, end_point_);
        if (between < corner_distance_) {
            const double cut = between * 0.5;
            cut_ = static_cast<float>(cut);
            auto left = static_cast<float>(span_to(end_point_, at_point_));
            if (cut > left)
                cut_ = left;
            flag_bits_ |= kFlagCorner;
            return true;
        }
    }
    flag_bits_ &= ~kFlagCorner;
    cut_ = corner_distance_;
    return true;
}

// 0x0046A250
bool PathFollower::start_segment()
{
    const double along = span_to(at_point_, segment_origin_);
    along_ = static_cast<float>(along);
    byte_count_ = static_cast<float>(span_to(segment_origin_, end_point_));
    along_ = static_cast<float>(along + carry_);
    carry_ = 0.0f;
    maths::Vec4& heading = core::at(tick_.row, 2);
    heading = {.x = end_point_.x - segment_origin_.x, .y = end_point_.y - segment_origin_.y, .z = end_point_.z - segment_origin_.z,
               .w = end_point_.w - segment_origin_.w};
    heading.make_unit();
    check_corner();
    return true;
}

// 0x00469B80
int PathFollower::compute_arc()
{
    // the corner point: as far along the next segment from the node as the position is short of it
    const double dx = static_cast<double>(at_point_.x) - segment_origin_.x;
    const double dy = static_cast<double>(at_point_.y) - segment_origin_.y;
    const double dz = static_cast<double>(at_point_.z) - segment_origin_.z;
    bend_point_ = segment_origin_;
    auto short_of = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    const double ex = static_cast<double>(end_point_.x) - segment_origin_.x;
    const double ey = static_cast<double>(end_point_.y) - segment_origin_.y;
    const float ez = end_point_.z - segment_origin_.z;
    const float ew = end_point_.w - segment_origin_.w;
    const double segment = std::sqrt(ex * ex + static_cast<double>(ez) * ez + ey * ey);
    const double scale = short_of / segment;
    auto scalef = static_cast<float>(scale);
    auto cx = static_cast<float>(ex * scale);
    const double cy = ey * scalef;
    const double cz = static_cast<double>(ez) * scalef;
    auto cw = static_cast<float>(static_cast<double>(ew) * scalef);
    bend_point_.x = static_cast<float>(static_cast<double>(cx) + bend_point_.x);
    bend_point_.y = static_cast<float>(cy + bend_point_.y);
    bend_point_.z = static_cast<float>(cz + bend_point_.z);
    bend_point_.w = static_cast<float>(static_cast<double>(cw) + bend_point_.w);
    if (flag_bits_ & kFlagCorner)
        return 0;
    if (short_of == 0.0f)
        return 2;

    // the arc's centre: on the bisector, through the middle of position - corner
    auto mx = static_cast<float>((static_cast<double>(bend_point_.x) - at_point_.x) * 0.5);
    const double my = (static_cast<double>(bend_point_.y) - at_point_.y) * 0.5;
    const double mz = static_cast<double>(bend_point_.z - at_point_.z) * 0.5;
    auto mw = static_cast<float>(static_cast<double>(bend_point_.w - at_point_.w) * 0.5);
    auto half_chord = static_cast<float>(std::sqrt(static_cast<double>(mx) * mx + mz * mz + my * my));
    auto mid_x = static_cast<float>(static_cast<double>(mx) + at_point_.x);
    auto mid_y = static_cast<float>(my + at_point_.y);
    const double mid_z = mz + at_point_.z;
    const double mid_w = static_cast<double>(mw) + at_point_.w;
    auto qx = static_cast<float>(static_cast<double>(mid_x) - segment_origin_.x);
    auto qy = static_cast<float>(static_cast<double>(mid_y) - segment_origin_.y);
    auto qz = static_cast<float>(mid_z - segment_origin_.z);
    const double qw = mid_w - segment_origin_.w;
    const double bisector =
        std::sqrt(static_cast<double>(qz) * qz + static_cast<double>(qy) * qy + static_cast<double>(qx) * qx);
    auto bisectorf = static_cast<float>(bisector);
    const bool tight = 6.0 * bisector < half_chord;
    if (tight || bisector == 0.0)
        return 0;
    const double radius = static_cast<double>(half_chord) / bisector * short_of;
    radius_ = static_cast<float>(radius);
    if (radius == 0.0)
        return 2;
    center_.w = static_cast<float>(qw);
    auto to_center = static_cast<float>(static_cast<double>(short_of) * short_of / bisectorf);
    center_.x = qx;
    center_.y = qy;
    center_.z = qz;
    set_length(center_, to_center);
    center_ += segment_origin_;

    // the frame: from the centre to the position, its axis, and the heading
    maths::Vec4& out = core::at(tick_.row, 0);
    maths::Vec4& axis = core::at(tick_.row, 1);
    maths::Vec4& heading = core::at(tick_.row, 2);
    out = center_ - at_point_;
    heading = center_ - bend_point_;
    if (len_of(out) == 0.0 || len_of(heading) == 0.0)
        return 2;
    out.make_unit();
    heading.make_unit();
    const double angle = std::acos(get_dot(out, heading));
    arc_angle_ = static_cast<float>(angle);
    arc_length_ = static_cast<float>(angle * radius_);
    axis = maths::cross_vec(out, heading);
    if (len_of(axis) == 0.0)
        return 2;
    axis.make_unit();
    heading = maths::cross_vec(out, axis);
    if (len_of(heading) == 0.0)
        return 2;
    heading.make_unit();
    along_ = carry_;
    carry_ = 0.0f;
    return 1;
}

// 0x0046A320
void PathFollower::arc_step(float distance)
{
    const double step = static_cast<double>(distance) / arc_length_ * arc_angle_;
    step_ = static_cast<float>(step);
    maths::Mat4 turn;
    d3dx_multiply(tick_, maths::make_yaw_matrix(turn, static_cast<float>(step)), tick_);
    if (core::at(tick_.row, 1).y > 0.0f)
        step_ = step_ * -1.0f;
    const double out = -static_cast<double>(radius_);
    const maths::Vec4& from_center = core::at(tick_.row, 0);
    at_point_.z = static_cast<float>(out * from_center.z);
    at_point_.w = static_cast<float>(out * from_center.w);
    at_point_.x = static_cast<float>(out * from_center.x);
    at_point_.y = static_cast<float>(out * from_center.y);
    at_point_.x = at_point_.x + center_.x;
    at_point_.y = center_.y + at_point_.y;
    at_point_.z = center_.z + at_point_.z;
    at_point_.w = center_.w + at_point_.w;
}

// 0x0046A550
void PathFollower::start_corner()
{
    switch (compute_arc()) {
    case 0:
        // no arc: blend into the next segment from here
        center_ = at_point_;
        along_ = carry_;
        cur_phase_ = kStateBlend;
        byte_count_ = static_cast<float>(span_to(at_point_, segment_origin_));
        break;
    case 1:
        cur_phase_ = kStateArc;
        break;
    case 2: {
        const uint32_t passed = last_node_;
        node_passed(path_component(owner_), passed);
        along_ = byte_count_ - along_;
        flag_bits_ &= ~kFlagTellNode;
        start_segment();
        refresh_placement();
        cur_phase_ = kStateStraight;
        break;
    }
    default:
        break;
    }
}

// 0x0046A3F0
void PathFollower::kickoff(uint32_t node, const maths::Vec4* position)
{
    cur_phase_ = kStateStart;
    flag_bits_ = 0;
    carry_ = 0.0f;
    along_ = 0.0f;
    list_link_ = node;
    segment_origin_ = *position;
    at_point_ = segment_origin_;
    read_node_position();
    while (span_to(at_point_, end_point_) < 1.0) {
        if (!get_next_node())
            return;
    }
    maths::Vec4& heading = core::at(tick_.row, 2);
    heading = {.x = end_point_.x - at_point_.x, .y = end_point_.y - at_point_.y, .z = end_point_.z - at_point_.z,
               .w = end_point_.w - at_point_.w};
    heading.make_unit();
}

// 0x0046A640
bool PathFollower::tick_frame(float distance, const maths::Vec4* direction)
{
    if (cur_phase_ == kStateDone)
        return true;
    if (direction) {
        const double along = get_dot(core::at(tick_.row, 2), *direction);
        distance = along == 0.0 ? 1.0f : static_cast<float>(along * distance);
    }
    switch (cur_phase_) {
    case kStateStart:
        start_segment();
        step_ = 0.0f;
        cur_phase_ = kStateStraight;
        [[fallthrough]];
    case kStateStraight: {
        const double along = static_cast<double>(distance) + along_;
        along_ = static_cast<float>(along);
        if (flag_bits_ & kFlagLastNode) {
            if (!(along >= byte_count_)) { // (unordered too, as the original's comparisons below)
                refresh_placement();
                return false;
            }
            last_node_ = list_link_;
            segment_origin_ = end_point_;
            at_point_ = segment_origin_;
            cur_phase_ = kStateDone;
            return true;
        }
        if (!(along >= static_cast<double>(byte_count_) - cut_)) {
            refresh_placement();
            return false;
        }
        if (cut_ == 0.0f) {
            at_point_ = end_point_;
            node_passed(path_component(owner_), list_link_);
            if (get_next_node()) {
                cur_phase_ = kStateStart;
                return false;
            }
            return true; // (the state isn't set to done)
        }
        refresh_placement();
        get_next_node();
        flag_bits_ |= kFlagTellNode;
        start_corner();
        return false;
    }
    case kStateArc: {
        const double along = static_cast<double>(distance) + along_;
        along_ = static_cast<float>(along);
        if ((flag_bits_ & kFlagTellNode) && along >= static_cast<double>(arc_length_) * 0.5) {
            const uint32_t passed = last_node_;
            flag_bits_ &= ~kFlagTellNode;
            node_passed(path_component(owner_), passed);
        }
        if (!(along_ > arc_length_)) {
            arc_step(distance);
            return false;
        }
        const double over = static_cast<double>(along_) - arc_length_;
        carry_ = static_cast<float>(over);
        arc_step(static_cast<float>(static_cast<double>(distance) - over));
        carry_ = 0.0f;
        start_segment();
        refresh_placement();
        step_ = 0.0f;
        cur_phase_ = kStateStraight;
        return false;
    }
    case kStateBlend: {
        const double along = static_cast<double>(distance) + along_;
        along_ = static_cast<float>(along);
        auto t = static_cast<float>(along / byte_count_);
        maths::Vec4 offset;
        if (along < byte_count_) {
            at_point_ = center_;
            offset = segment_origin_ - center_;
            offset *= t;
        } else {
            at_point_ = segment_origin_;
            offset = bend_point_ - segment_origin_;
            offset *= static_cast<float>(static_cast<double>(t) - 1.0);
        }
        at_point_ += offset;
        const maths::Vec4 into = segment_origin_ - center_;
        const maths::Vec4 next = bend_point_ - segment_origin_;
        float blend = t * 0.5f;
        if (blend >= 0.5f && (flag_bits_ & kFlagTellNode)) {
            const uint32_t passed = last_node_;
            flag_bits_ &= ~kFlagTellNode;
            node_passed(path_component(owner_), passed);
        }
        if (blend >= 1.0f) {
            blend = 1.0f;
            carry_ = 0.0f;
            start_segment();
            cur_phase_ = kStateStraight;
        }
        maths::Vec4& heading = core::at(tick_.row, 2);
        heading = into * static_cast<float>(1.0 - blend);
        heading += next * blend;
        heading.make_unit();
        return false;
    }
    default:
        return false;
    }
}

// 0x0046A1B0
void PathFollower::get_heading(maths::Vec4* out) { *out = core::at(tick_.row, 2); }

} // namespace objects
