// The skater's rotate component (GameObjects::RiderTurning): see rotate_component.h.

#include "game/rider/rotate_component.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterRotate = 0x52D61A0D, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kRotate = 0xC876ADF3;
constexpr uint32_t kDuration = 0x79A07F3F, kSeconds = 0xD029F619, kSecond = 0x49E0EE96, kFrames = 0x019176C5,
                   kFrame = 0x4A07C332, kX = 0x7323E97C, kY = 0x0424D9EA, kZ = 0x9D2D8850, kNode = 0x7A8017BA;

constexpr float kRadiansPerDegree = 0.01745329238474369f; // 0x00645464
constexpr float kDegreesPerRadian = 57.2957763671875f;    // 0x00645468
constexpr float kSecondsPerMs = 0.001f;                   // 0x006461D8
constexpr float kSecondsPerFrame = 0.0166666675f;         // 0x006463B8

maths::Mat4& core_matrix(uint8_t* core_physics) { return *reinterpret_cast<maths::Mat4*>(core_physics + 0x18); }
// The core physics' matrix set from its object's.
void core_matrix_from_object(uint8_t* core_physics)
{
    core_matrix(core_physics) = reinterpret_cast<ObjectModule*>(core_physics)->entity_->basis_frame_;
}
// m = rotation * m (D3DXMatrixMultiply, linked library code).
void turn_by(maths::Mat4& m, const maths::Mat4& rotation) { d3dx::matrix_multiply(&m, &rotation, &m); }

// A turn set up from `degrees` over `duration` seconds.
void start_turn(RiderTurning::Turn& turn, double degrees, float duration)
{
    turn.duration = duration;
    turn.turned = 0.0f;
    turn.active = true;
    turn.rate = static_cast<float>(degrees / duration * kRadiansPerDegree);
    turn.remaining = static_cast<float>(std::fabs(degrees * kRadiansPerDegree));
}

} // namespace

// 0x00517D40
ObjectModule* __cdecl RiderTurning::fabricate() { return new RiderTurning; }

// (the constructor: inlined in fabricate in the original)
RiderTurning::RiderTurning()
{
    kind_tag_ = kSkaterRotate;
    core_physics_ = nullptr;
}

// 0x00517CC0 (0x00517CF0 the deleting one)
RiderTurning::~RiderTurning() {}

// 0x00517CD0
void RiderTurning::configure_from_data(script::Struct* /*params*/)
{
    for (int axis = 2; axis >= 0; --axis)
        turns_[axis].active = false;
}

// 0x00517D20
void RiderTurning::seal() { core_physics_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterCorePhysics)); }

// 0x00517DC0
void RiderTurning::tick_frame()
{
    for (int axis = 0; axis < 3; ++axis) {
        Turn& turn = turns_[axis];
        if (!turn.active)
            continue;
        double exact = frame_clock::get_slomo_frame_seconds() * turn.rate;
        float step = static_cast<float>(exact);
        float turned = static_cast<float>(std::fabs(exact) + turn.turned);
        turn.turned = turned;
        if (!(turned < turn.remaining)) {
            // The last step: cut so the turn ends exactly.
            turn.active = false;
            double sign = step < 0.0f ? -1.0 : 1.0;
            step = static_cast<float>(step - (static_cast<double>(turned) - turn.remaining) * sign);
        }
        maths::Mat4 rotation;
        maths::build_axis_angle_matrix(rotation, axis, step);
        turn_by(entity_->basis_frame_, rotation);
        turn_by(core_matrix(core_physics_), rotation);
    }
    if (!turns_[0].active && !turns_[1].active && !turns_[2].active)
        hold_off(true);
}

// 0x00517EA0
int RiderTurning::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name != kRotate)
        return 2;
    float duration;
    if (params->fetch_float(kDuration, &duration, 0)) {
        // Over time: the duration in seconds (seconds / second), frames (frames / frame) or ms.
        if (!params->has_bare_flag(kSeconds) && !params->has_bare_flag(kSecond)) {
            if (params->has_bare_flag(kFrames) || params->has_bare_flag(kFrame))
                duration = static_cast<float>(static_cast<double>(duration) * kSecondsPerFrame);
            else
                duration = static_cast<float>(static_cast<double>(duration) * kSecondsPerMs);
        }
        if (params->fetch_float(kX, &turns_[0].remaining, 0))
            start_turn(turns_[0], turns_[0].remaining, duration);
        if (params->fetch_float(kY, &turns_[1].remaining, 0))
            start_turn(turns_[1], turns_[1].remaining, duration);
        if (params->fetch_float(kZ, &turns_[2].remaining, 0))
            start_turn(turns_[2], turns_[2].remaining, duration);
        // To face a node: about y by the angle to it in the skater's plane.
        uint32_t node = 0;
        if (params->read_hash_value(kNode, &node, 0)) {
            maths::Vec4 pos;
            level_nodes::fetch_place(level_nodes::fetch_vertex(node, true), &pos);
            const maths::Vec4& at = entity_->at_point_;
            maths::Vec4 to_node = {pos.x - at.x, pos.y - at.y, pos.z - at.z, pos.w - at.w};
            double degrees = static_cast<double>(maths::get_angle_in_plane(entity_->basis_frame_, to_node, 2, 1)) *
                             kDegreesPerRadian;
            start_turn(turns_[1], degrees, duration);
        }
        hold_off(false);
        return 1;
    }
    // At once.
    float degrees;
    if (params->fetch_float(kX, &degrees, 0)) {
        maths::Mat4 rotation;
        maths::make_pitch_matrix(rotation, static_cast<float>(static_cast<double>(degrees) * kRadiansPerDegree));
        turn_by(entity_->basis_frame_, rotation);
    } else if (params->fetch_float(kY, &degrees, 0)) {
        entity_->basis_frame_.yaw_locally(static_cast<float>(static_cast<double>(degrees) * kRadiansPerDegree));
    } else if (params->fetch_float(kZ, &degrees, 0)) {
        entity_->basis_frame_.roll_in_own_frame(static_cast<float>(static_cast<double>(degrees) * kRadiansPerDegree));
    } else {
        // Turned round: the at and right rows reversed (their w kept), and the core physics' flag at +0x34C flipped.
        maths::Vec4& at_row = entity_->basis_frame_.row[2];
        at_row.x = -at_row.x;
        at_row.y = -at_row.y;
        at_row.z = -at_row.z;
        maths::Vec4& right = entity_->basis_frame_.row[0];
        right.x = -right.x;
        right.y = -right.y;
        right.z = -right.z;
        core_matrix_from_object(core_physics_);
        core_physics_[0x34C] = core_physics_[0x34C] ? 0 : 1;
        return 1;
    }
    core_matrix_from_object(core_physics_);
    return 1;
}

} // namespace objects
