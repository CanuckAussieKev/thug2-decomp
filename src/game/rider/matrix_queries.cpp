// The skater's matrix queries component (GameObjects::RiderFrameLookups): see matrix_queries.h.

#include "game/rider/matrix_queries.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>

#include "engine/maths/maths.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/core_physics.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterMatrixQueries = 0xBF518E27, kSkaterCorePhysics = 0x5BD63B29;

// Script member functions
constexpr uint32_t kGetHeading = 0x8E7833BE, kAbsolutePitchGreaterThan = 0x5E269B2B, kGetSlope = 0x97201739,
                   kPitchGreaterThan = 0xA0551543, kYawingLeft = 0xA745C080, kYawingRight = 0xC8C4D2F4,
                   kRollGreaterThan = 0xD3313E92, kYawBetween = 0xB992F3CC;
// Their results
constexpr uint32_t kHeading = 0xFD4BC03E, kCosine = 0x2315EF17, kSine = 0x26910CC0, kSlope = 0xA733BA7A,
                   kChangeInSlope = 0x21AFFF16;

constexpr float kDegreesPerRadian = 57.2957763671875F;    // 0x00645468
constexpr float kRadiansPerDegree = 0.01745329238474369F; // 0x00645464
constexpr float kFullCircle = 360.0F;                     // 0x00645EA8

// The core physics' vector at +0x78 (the plane PitchGreaterThan and RollGreaterThan measure against).
const maths::Vec4& core_vector78(skater::SkaterPhysicsModule* core_physics) { return core_physics->old_normal(); }

// The unnamed float parameter (0 without one; asserted).
float angle_parameter(script::Struct* params)
{
    float angle = 0.0F;
    params->fetch_float(uint32_t{0}, &angle, 1);
    return angle;
}
// The cosine of `degrees`, as the original's x87.
double cos_degrees(float degrees) { return std::cos(static_cast<double>(degrees) * kRadiansPerDegree); }

} // namespace

// 0x00514430
gsl::owner<ObjectModule*> RiderFrameLookups::fabricate() { return new RiderFrameLookups; }

// (the constructor: inlined in fabricate in the original)
RiderFrameLookups::RiderFrameLookups() { set_kind_tag(kSkaterMatrixQueries); }

// 0x00513FD0 (0x00513FE0 the deleting one)
RiderFrameLookups::~RiderFrameLookups() = default;

// 0x00514010
void RiderFrameLookups::seal()
{
    core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
}

// 0x00514030
void RiderFrameLookups::tick_frame() { basis_frame_ = core_physics_->display_matrix(); }

// 0x00514040
int RiderFrameLookups::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    const maths::Vec4& right = core::at(basis_frame_.row, 0);
    const maths::Vec4& up = core::at(basis_frame_.row, 1);
    const maths::Vec4& at = core::at(basis_frame_.row, 2);
    switch (name) {
    case kGetHeading: {
        const maths::Vec4& facing = core::at(entity()->basis_frame().row, 2);
        auto heading = static_cast<float>(static_cast<double>(maths::fast_a_cos(facing.x)) * kDegreesPerRadian);
        if (facing.z < 0.0F)
            heading = kFullCircle - heading;
        script->args()->push_float(checksums::Name{kHeading}, heading);
        script->args()->push_float(checksums::Name{kCosine}, core::at(entity()->basis_frame().row, 2).x);
        script->args()->push_float(checksums::Name{kSine}, core::at(entity()->basis_frame().row, 2).z);
        return 1;
    }
    case kAbsolutePitchGreaterThan: {
        const float angle = angle_parameter(params);
        return cos_degrees(angle) > up.y ? 1 : 0;
    }
    case kGetSlope: {
        // The at row's angle from its own flattened copy (negative when it points down).
        const maths::Vec4& facing = core::at(entity()->basis_frame().row, 2);
        maths::Vec4 flat = facing;
        flat.y = 0.0F;
        auto slope = static_cast<float>(maths::read_turn_angle(facing, flat));
        if (core::at(entity()->basis_frame().row, 2).y < 0.0F)
            slope = -slope;
        script->args()->push_float(checksums::Name{kSlope}, slope);
        script->args()->push_float(checksums::Name{kChangeInSlope}, slope - prev_gradient_);
        prev_gradient_ = slope;
        return 1;
    }
    case kPitchGreaterThan: {
        const float angle = angle_parameter(params);
        const maths::Vec4& v = core_vector78(core_physics_);
        const double dot =
            (static_cast<double>(up.z) * v.z) + (static_cast<double>(up.y) * v.y) + (static_cast<double>(v.x) * up.x);
        return cos_degrees(angle) > dot ? 1 : 0;
    }
    case kYawingLeft: {
        maths::Vec4 v = entity()->vel();
        v.align_to_plane(up);
        return (static_cast<double>(v.z) * at.x) - (static_cast<double>(v.x) * at.z) > 0.0 ? 1 : 0;
    }
    case kYawingRight: {
        maths::Vec4 v = entity()->vel();
        v.align_to_plane(up);
        return maths::cross_vec(v, at).y < 0.0F ? 1 : 0;
    }
    case kRollGreaterThan: {
        const float angle = angle_parameter(params);
        maths::Vec4 v = right;
        v.align_to_plane(core_vector78(core_physics_));
        return maths::is_angle_beyond(v, right, angle) ? 1 : 0;
    }
    case kYawBetween: {
        script::Pair range;
        params->fetch_pair(uint32_t{0}, &range, true);
        maths::Vec4 v = entity()->vel();
        v.align_to_plane(up);
        if (!maths::is_angle_beyond(v, at, range.x()))
            return 0;
        return maths::is_angle_beyond(v, at, range.y()) ? 0 : 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
