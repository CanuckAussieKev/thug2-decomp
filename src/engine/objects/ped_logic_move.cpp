// The PedLogic component (GameObjects::PedestrianBrainModule): see ped_logic.h. Its movement - the steering (round other peds,
// towards the waypoints, back to the path), facing where it goes, the ground under it, the skaters' spins - and the
// move updates (generic, skater, bail, go home, chase, blocker); from the disassembly of 0x004ED5E0-0x004F2F08
// (0x004EF090, 0x004EF930, 0x004F0950 and 0x004F2AD0 aren't Ghidra functions). The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double; FSIN / FCOS results
// are used at their full precision where the original goes on computing with them.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "engine/memory/arena_alloc.h"
#include "engine/objects/smtptr.h"
#include "game/rider/rider.h"
#include "base/maths/maths.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "base/maths/trig.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/path_manager.h"
#include "engine/objects/ped_logic.h"
#include "engine/objects/walk_lerp.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/rider/looping_sound.h"
#include "engine/sys/stopwatch.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"

namespace objects {

namespace {

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;         // OBJECT_TYPE_PED
constexpr uint32_t kMinBias = 0x4ABA9798;               // ped_min_bias
constexpr uint32_t kBiasDecay = 0x98CD0C8C;             // (the bias's decay per frame)
constexpr uint32_t kAvoidPedRange = 0x7A78F471;         // ped_avoid_ped_range
constexpr uint32_t kPedDodgeBias = 0xD1E6177B;          // ped_avoid_ped_bias
constexpr uint32_t kMaxYDistance = 0x21B13FFC;          // ped_max_y_distance_to_ignore
constexpr uint32_t kHeadOnRange = 0xD2E22CEC;           // ped_head_on_range
constexpr uint32_t kMaxDistanceToPath = 0xA876AA2C;     // ped_max_distance_to_path
constexpr uint32_t kMinDistanceToPath = 0x81085734;     // ped_min_distance_to_path
constexpr uint32_t kTargetNodeBias = 0x57342672;        // ped_target_node_bias
constexpr uint32_t kDeadEndDistance = 0x65B46F32;       // ped_walker_min_square_distance_to_dead_end
constexpr uint32_t kSkaterFadeDistance = 0x354BEDA1;    // ped_skater_fade_target_bias_max_distance_square
constexpr uint32_t kFadeDistance = 0x5B21AB0E;          // ped_fade_target_bias_max_distance_square
constexpr uint32_t kMinDistanceToSkater = 0xBC2E678A;   // ped_skater_min_square_distance_to_skater
constexpr uint32_t kMinDistanceToWaypoint = 0xFB5D106F; // ped_skater_min_square_distance_to_waypoint
constexpr uint32_t kCrouchDistance = 0x52EC432F;        // ped_skater_min_square_distance_to_crouch_for_jump
constexpr uint32_t kStateJumpCrouch = 0xB32163E0;       // STATE_jumpcrouch
constexpr uint32_t kStateChase = 0x1B91406A, kStateChasePause = 0x0CC94B7C, kStateChaseCatch = 0x1E38A2F5;
constexpr uint32_t kSkaterPedStateTable = 0x46196541;
constexpr uint32_t kSkaterMoveUpdate = 0xA631E964, kGenericMoveUpdate = 0x5B4E8427, kNoMoveUpdate = 0x520A0B40,
                   kGoHomeUpdate = 0x4DF2CC6E, kChaseUpdate = 0x30A12962, kBlockerUpdate = 0x3884D585,
                   kLipTrickUpdate = 0xC51B9B62, kBailUpdate = 0xF229C933;
constexpr uint32_t kPedInitializeMovement = 0x1B7DE0FB; // ped_initialize_movement
constexpr uint32_t kEndReturnHome = 0x44EB4098;         // Ped_Utils_EndReturnHome
constexpr uint32_t kEndPlayerChase = 0xFF21C020;        // Ped_Utils_EndPlayerChase
constexpr uint32_t kMaxHomeDist = 0x4116F7FF;           // MaxHomeDist (a tag)
constexpr uint32_t kChaser = 0x96919C9C;                // (the chased skater's id, to STATE_chasecatch)
constexpr uint32_t kVelDir = 0xF68EA304;                // VelDir
constexpr uint32_t kBlockerBumpPlayer = 0xF75EC2CF, kBlockerStand = 0xEFF1B1AA, kBlockerShuffle = 0x3B5A86D7,
                   kBlockerRun = 0x9D45C77A;                                   // the blocker's events
constexpr uint32_t kDir = 0x455485EF, kLeft = 0x85981897, kRight = 0x4B358AEB; // dir: left / Right

constexpr float kQuarterTurn = 1.5707963705062866F;      // 0x00645484
constexpr float kThreeQuarterTurns = 4.71238899230957F;  // 0x0064B76C
constexpr float kFullTurn = 6.2831854820251465F;         // 0x0064545C
constexpr float kInverseFullTurn = 0.15915493667125702F; // 0x0064A044
constexpr float kHalfTurn = 3.1415927410125732F;         // 0x0064B730
constexpr float kDegrees = 360.0F;                       // 0x00645EA8
constexpr float kInverseDegrees = 1.0F / 360.0F;         // 0x0064B798
constexpr float kHalfDegrees = 180.0F;                   // 0x0064B278
constexpr float kToRadians = 0.01745329238474369F;       // 0x00645464
constexpr float kFramesPerSecond = 60.0F;                // 0x00645EA0
constexpr float kMaxCos = 0.9998999834060669F;           // 0x0064B7F4
constexpr float kMinCos = -0.9998999834060669F;          // 0x0064B7EC
constexpr float kTurnPerSecond = 0.05235987901687622F;   // 0x0064B7F0 (3 degrees)
constexpr float kTurnTime = 0.083F;                      // 0x0064B7E8
constexpr double kAhead = 0.866;                         // 0x0064B7F8 (a double: about cos 30 degrees)
constexpr float kReach = 48.0F;                          // 0x006461CC
constexpr float kProbeLift = 8.0F;                       // 0x006461D4
constexpr float kNear = 1000.0F;                         // 0x006453C0
constexpr float kCatchDistanceSqr = 3047.039794921875F;  // 0x0067EFEC
constexpr float kChaseOutOfRangeSqr = 360000.0F;         // 0x0067EFF4
constexpr float kChaseSpeed = 400.0F;
constexpr float kFastSkaterSqr = 50625.0F;          // 0x0067EFF8
constexpr float kLookAhead = 600.0F;                // 0x0064B81C
constexpr float kNoSpeed = 0.001F;                  // 0x006461D8
constexpr double kStandDistance = 3.6;              // 0x0064B810
constexpr float kRunRatio = 225.0F;                 // 0x0067EFF0
constexpr float kRunDistance = 96.0F;               // 0x0064B808
constexpr float kMinBlockerSpeed = 140.0F;          // 0x0064B804
constexpr float kMaxBlockerSpeed = 1500.0F;         // 0x0064B800
constexpr double kBlockerTurn = 1.5707963705062866; // 0x0064B828 / 0x0064B820 (+ / -)
constexpr float kUpBlendRate = 6.0F;                // 0x00646284

KinematicsModule* get_motion(GameEntity* object)
{
    return core::down_cast<KinematicsModule>(object->fetch_module(kMotion));
}

GameEntity* local_skater() { return static_cast<GameEntity*>(modules::Skate::sole()->get_home_rider()); }

// D3DXMatrixMultiply (linked library code).
void d3dx_multiply(maths::Mat4& out, const maths::Mat4& a, const maths::Mat4& b)
{
    maths::matrix_multiply(&out, &a, &b);
}

// _ftol2: a 64-bit truncation whose low half is used (the integer indefinite, 2^63 - a NaN, what doesn't fit - gives
// 0).
int truncate(double value)
{
    if (!(std::fabs(value) < maths::kExactDoubleLimit)) {
        return 0;
    }
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}


// The collision feeler (0xB0 bytes): what it hit is held by a smart pointer at +0x98, let go when it goes.
class Feeler {
public:
    Feeler() = default;
    Feeler(const maths::Vec4* start, const maths::Vec4* end) : ray_(start, end) {}
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    ~Feeler() = default;
    render::ProbeRay* feeler() { return &ray_; }

private:
    render::ProbeRay ray_;
};

// The cross product's rows of a facing matrix: across = up x at (`a` kept unrounded), up again = at x across.
void rebuild_across_and_up(maths::Mat4& matrix)
{
    const maths::Vec4& up = core::at(matrix.row, 1);
    const maths::Vec4& at = core::at(matrix.row, 2);
    const double cross_x = (static_cast<double>(at.z) * up.y) - (static_cast<double>(at.y) * up.z);
    auto cross_y = static_cast<float>((static_cast<double>(at.x) * up.z) - (static_cast<double>(at.z) * up.x));
    auto cross_z = static_cast<float>((static_cast<double>(at.y) * up.x) - (static_cast<double>(at.x) * up.y));
    const double up_x = (static_cast<double>(cross_z) * at.y) - (static_cast<double>(cross_y) * at.z);
    const double up_y = (static_cast<double>(at.z) * cross_x) - (static_cast<double>(cross_z) * at.x);
    auto up_z = static_cast<float>((static_cast<double>(cross_y) * at.x) - (static_cast<double>(at.y) * cross_x));
    core::at(matrix.row,
             0) = {.x = static_cast<float>(cross_x), .y = cross_y, .z = cross_z, .w = core::at(matrix.row, 0).w};
    core::at(matrix.row, 1) = {
        .x = static_cast<float>(up_x), .y = static_cast<float>(up_y), .z = up_z, .w = core::at(matrix.row, 1).w};
}

} // namespace

// --- steering ---

// 0x004ECFF0
void PedestrianBrainModule::decay_bias()
{
    const float decay = script::fetch_float(kBiasDecay);
    const double threshold = script::fetch_float(kMinBias);
    for (float& bias : bias_) {
        // The product is compared unrounded (x87); a NaN is kept.
        const double value = static_cast<double>(decay) * bias;
        bias = static_cast<float>(value);
        if (value < threshold) {
            bias = 0.0F;
        }
    }
}

// 0x004ED5E0
void PedestrianBrainModule::add_avoidance(Avoidance avoidance)
{
    const auto angle = avoidance.angle;
    const auto strength = avoidance.strength;
    const auto distance = avoidance.distance;
    const auto range = avoidance.range;
    const double weight = (static_cast<double>(range) - distance) / range * strength;
    for (int i = 0; i < 4; ++i) {
        const double offset = (static_cast<double>(i) * kQuarterTurn) - angle;
        const double size = std::fabs(offset);
        if (!(size < kQuarterTurn) && !(size > kThreeQuarterTurns || std::isnan(size))) {
            continue;
        }
        const double value = (std::cos(offset) * weight) + core::at(bias_, i);
        core::at(bias_, i) = static_cast<float>(value);
        if (value > 1.0) {
            core::at(bias_, i) = 1.0F;
        } else if (value < -1.0) {
            core::at(bias_, i) = -1.0F;
        }
    }
}

// 0x004ED680
void PedestrianBrainModule::add_steer(Steer steer)
{
    const auto angle = steer.angle;
    const auto weight = steer.weight;
    for (int i = 0; i < 4; ++i) {
        const double offset = (static_cast<double>(i) * kQuarterTurn) - angle;
        const double size = std::fabs(offset);
        if (!(size < kQuarterTurn) && !(size > kThreeQuarterTurns || std::isnan(size))) {
            continue;
        }
        const double value = (std::cos(offset) * weight) + core::at(avoid_, i);
        core::at(avoid_, i) = static_cast<float>(value);
        if (value > 1.0) {
            core::at(avoid_, i) = 1.0F;
        } else if (value < -1.0) {
            core::at(avoid_, i) = -1.0F;
        }
    }
}

// 0x004ED710
float PedestrianBrainModule::steer_sideways()
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        auto quarter = static_cast<float>(static_cast<double>(i) * kQuarterTurn);
        double weight = static_cast<double>(core::at(bias_, i)) + core::at(avoid_, i);
        weight = std::min(weight, 1.0);
        sum = sum - (std::sin(static_cast<double>(quarter)) * weight);
        if (sum >= 1.0) {
            return 1.0F;
        }
        if (sum <= -1.0) {
            return -1.0F;
        }
    }
    auto result = static_cast<float>(sum);
    if (script::fetch_float(kMinBias) > std::fabs(result)) {
        return 0.0F;
    }
    return result;
}

// 0x004ED7D0
float PedestrianBrainModule::steer_forward()
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        auto quarter = static_cast<float>(static_cast<double>(i) * kQuarterTurn);
        double weight = static_cast<double>(core::at(bias_, i)) + core::at(avoid_, i);
        if (weight > 1.0) {
            weight = 1.0;
        } else if (weight < -1.0) {
            weight = -1.0;
        }
        sum = (std::cos(static_cast<double>(quarter)) * weight) + sum;
        if (sum >= 1.0) {
            return 1.0F;
        }
        if (sum <= -1.0) {
            return -1.0F;
        }
    }
    auto result = static_cast<float>(sum);
    if (script::fetch_float(kMinBias) > std::fabs(result)) {
        return 0.0F;
    }
    return result;
}

// 0x004EE650
void PedestrianBrainModule::face_along(const maths::Vec4* direction)
{
    maths::Mat4 matrix;
    core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 2) = {.x = direction->x, .y = 0.0F, .z = direction->z, .w = 1.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(matrix.row, 2).make_unit();
    core::at(matrix.row, 0).w = 0.0F;
    core::at(matrix.row, 1).w = 0.0F;
    rebuild_across_and_up(matrix);
    entity()->display_matrix() = matrix;
    entity()->basis_frame() = matrix;
    saved_matrix_ = matrix;
}

// 0x004EE7D0
bool PedestrianBrainModule::avoid_peds()
{
    bool avoided = false;
    if (!peds_) {
        return false;
    }
    const std::span<const SharedRef<Entity>> slots = peds_->slots();
    const maths::Mat4 matrix = entity()->display_matrix();
    const maths::Vec4& at = core::at(matrix.row, 2);
    const float range = script::fetch_float(kAvoidPedRange);
    const float bias = script::fetch_float(kPedDodgeBias);
    const KinematicsModule* motion = get_motion(entity());
    const int count = peds_->extent();
    if (count <= 0) {
        return false;
    }
    for (int k = 0; k < count; ++k) {
        const SharedRef<Entity>* slot = &core::at(slots, k);
        auto* other = core::down_cast<GameEntity>(slot->get_pull());
        if (!other || other == entity()) {
            continue;
        }
        const double max_y = script::fetch_float(kMaxYDistance);
        const GameEntity* me = entity();
        if (std::fabs(static_cast<double>(me->at_point().y) - other->at_point().y) > max_y) {
            continue;
        }
        const maths::Vec4& other_at = core::at(other->display_matrix().row, 2);
        const double dot = ((static_cast<double>(at.y) * other_at.y) + (static_cast<double>(at.z) * other_at.z)) +
                           (static_cast<double>(at.x) * other_at.x);
        auto heading_dot = static_cast<float>(dot);
        if (dot <= 0.0) {
            // Heading the other way: only one ahead.
            const double dx = static_cast<double>(other->at_point().x) - me->at_point().x;
            const double dy = static_cast<double>(other->at_point().y) - me->at_point().y;
            const double dz = static_cast<double>(other->at_point().z) - me->at_point().z;
            const double ahead = ((static_cast<double>(at.z) * dz) + (static_cast<double>(at.y) * dy)) +
                                 (static_cast<double>(at.x) * dx);
            if (ahead <= 0.0) {
                continue;
            }
        }
        const KinematicsModule* other_motion = get_motion(other);
        if (heading_dot > 0.0F && other_motion->path_max_speed() >= motion->path_max_speed()) {
            continue;
        }
        const double dx = static_cast<double>(entity()->at_point().x) - other->at_point().x;
        const double dy = static_cast<double>(entity()->at_point().y) - other->at_point().y;
        const double dz = static_cast<double>(entity()->at_point().z) - other->at_point().z;
        const double distance = std::sqrt(((dz * dz) + (dy * dy)) + (dx * dx));
        auto distance_f = static_cast<float>(distance);
        if (!(distance <= range)) {
            continue;
        }
        const double other_length = std::sqrt(
            ((static_cast<double>(other_at.x) * other_at.x) + (static_cast<double>(other_at.y) * other_at.y)) +
            (static_cast<double>(other_at.z) * other_at.z));
        const double my_length = std::sqrt(((static_cast<double>(at.x) * at.x) + (static_cast<double>(at.y) * at.y)) +
                                           (static_cast<double>(at.z) * at.z));
        const float cosine = maths::confine(static_cast<float>(heading_dot / (other_length * my_length)), -1.0F, 1.0F);
        bool above = false;
        float angle = maths::acos_f({.value = cosine, .limit = kFullTurn}, &above);
        if (above) {
            angle = static_cast<float>(
                angle - (static_cast<double>(truncate(static_cast<double>(angle) * kInverseFullTurn)) * kFullTurn));
        }
        auto off_head_on = static_cast<float>(std::fabs(static_cast<double>(angle) - kHalfTurn));
        if (script::fetch_float(kHeadOnRange) > off_head_on) {
            // Head on: to the side the other is on.
            angle = kQuarterTurn;
            if (maths::cross_vec(at, other_at).y < 0.0F) {
                angle = -kQuarterTurn;
            }
        }
        avoided = true;
        add_avoidance({.angle = angle, .strength = bias, .distance = distance_f, .range = range});
    }
    return avoided;
}

// 0x004EEAC0
void PedestrianBrainModule::keep_to_path()
{
    const maths::Vec4& from = node_pos_;
    const float ax = from.x;
    const float az = from.z;
    const float aw = from.w;
    const float bx = next_node_pos_.x;
    const float bz = next_node_pos_.z;
    const float bw = next_node_pos_.w;
    const maths::Vec4& at = entity()->at_point();
    const float px = at.x;
    const float pz = at.z;
    const float pw = at.w;
    const float max_distance = script::fetch_float(kMaxDistanceToPath);
    auto dx = static_cast<float>(static_cast<double>(bx) - ax);
    const double dz = static_cast<double>(bz) - az;
    auto dw = static_cast<float>(static_cast<double>(bw) - aw);
    const double ex = static_cast<double>(px) - ax;
    const double ez = static_cast<double>(pz) - az;
    auto cross_x = static_cast<float>((0.0F * ez) - (dz * 0.0F));
    auto cross_y = static_cast<float>((dz * ex) - (ez * dx));
    auto cross_z = static_cast<float>((static_cast<double>(dx) * 0.0F) - (ex * 0.0F));
    const double length_sqr = (static_cast<double>(dx) * dx) + (dz * dz);
    // The closest point of the path's line (its start for a point path).
    maths::Vec4 closest = from;
    if (length_sqr != 0.0) {
        const double fraction = ((ex * dx) + (ez * dz)) / length_sqr;
        const double along_x = dx * fraction;
        auto along_y = static_cast<float>(fraction * 0.0F);
        auto along_z = static_cast<float>(dz * fraction);
        auto along_w = static_cast<float>(dw * fraction);
        closest = {.x = static_cast<float>(along_x + from.x), .y = static_cast<float>(static_cast<double>(along_y) + from.y),
                   .z = static_cast<float>(static_cast<double>(along_z) + from.z),
                   .w = static_cast<float>(static_cast<double>(along_w) + from.w)};
    }
    const maths::Vec4 to_path = {.x = static_cast<float>(static_cast<double>(closest.x) - px),
                                 .y = 0.0F,
                                 .z = static_cast<float>(static_cast<double>(closest.z) - pz),
                                 .w = static_cast<float>(static_cast<double>(closest.w) - pw)};
    const maths::Mat4 matrix = entity()->display_matrix();
    const double exact = maths::read_turn_angle(core::at(matrix.row, 2), to_path);
    auto angle = static_cast<float>(exact);
    if (exact > kDegrees) {
        angle = static_cast<float>(
            angle - (static_cast<double>(truncate(static_cast<double>(angle) * kInverseDegrees)) * kDegrees));
    }
    if ((static_cast<double>(core::at(matrix.row, 2).z) * to_path.x) -
            (static_cast<double>(core::at(matrix.row, 2).x) * to_path.z) >
        0.0) {
        angle = static_cast<float>(static_cast<double>(kDegrees) - angle);
    }
    const double sx = static_cast<double>(ax) - bx;
    const double sz = static_cast<double>(az) - bz;
    const double segment = std::sqrt((sx * sx) + (sz * sz));
    float off = 0.0F;
    if (segment == 0.0) {
        off = 0.0F;
    } else {
        off = static_cast<float>(
            std::sqrt(((static_cast<double>(cross_x) * cross_x) + (static_cast<double>(cross_z) * cross_z)) +
                      (static_cast<double>(cross_y) * cross_y)) /
            segment);
    }
    float weight = 0.0F;
    if (off > max_distance) {
        weight = 1.0F;
    } else {
        weight = static_cast<float>(1.0 - ((static_cast<double>(max_distance) - off) / max_distance));
    }
    if (script::fetch_float(kMinDistanceToPath) < off) {
        add_steer({.angle = static_cast<float>(static_cast<double>(angle) * kToRadians), .weight = weight});
    }
}

// 0x004EEDC0
void PedestrianBrainModule::move_steered()
{
    const KinematicsModule* motion = get_motion(entity());
    const float side = steer_sideways();
    const double forward = steer_forward();
    push_position();
    const double step = static_cast<double>(step_seconds_) * motion->path_speed();
    auto side_step = static_cast<float>(side * step);
    auto forward_step = static_cast<float>(step * forward);
    const maths::Mat4 matrix = entity()->display_matrix();
    const double fx = static_cast<double>(core::at(matrix.row, 2).x) * forward_step;
    const double fy = static_cast<double>(core::at(matrix.row, 2).y) * forward_step;
    auto fz = static_cast<float>(static_cast<double>(core::at(matrix.row, 2).z) * forward_step);
    auto fw = static_cast<float>(static_cast<double>(core::at(matrix.row, 2).w) * forward_step);
    const double sx = static_cast<double>(core::at(matrix.row, 0).x) * side_step;
    const double sy = static_cast<double>(core::at(matrix.row, 0).y) * side_step;
    auto sz = static_cast<float>(static_cast<double>(core::at(matrix.row, 0).z) * side_step);
    auto sw = static_cast<float>(static_cast<double>(core::at(matrix.row, 0).w) * side_step);
    maths::Vec4& pos = entity()->at_point();
    pos.x = static_cast<float>(sx + pos.x);
    pos.y = static_cast<float>(sy + pos.y);
    pos.z = static_cast<float>(static_cast<double>(sz) + pos.z);
    pos.w = static_cast<float>(static_cast<double>(sw) + pos.w);
    pos.x = static_cast<float>(fx + pos.x);
    pos.y = static_cast<float>(fy + pos.y);
    pos.z = static_cast<float>(static_cast<double>(fz) + pos.z);
    pos.w = static_cast<float>(static_cast<double>(fw) + pos.w);
}

// 0x004EEEC0
void PedestrianBrainModule::steer_towards(maths::Vec4 target, float weight)
{
    const maths::Mat4 matrix = entity()->display_matrix();
    const maths::Vec4& at = entity()->at_point();
    const maths::Vec4 away = {.x = static_cast<float>(static_cast<double>(at.x) - target.x),
                              .y = 0.0F,
                              .z = static_cast<float>(static_cast<double>(at.z) - target.z),
                              .w = static_cast<float>(static_cast<double>(at.w) - target.w)};
    double angle = maths::read_turn_angle(core::at(matrix.row, 2), away) + kHalfDegrees;
    if (angle > kDegrees) {
        angle -= static_cast<double>(truncate(angle * kInverseDegrees)) * kDegrees;
    }
    if ((static_cast<double>(core::at(matrix.row, 2).z) * away.x) -
            (static_cast<double>(core::at(matrix.row, 2).x) * away.z) >
        0.0) {
        angle = static_cast<double>(kDegrees) - angle;
    }
    add_steer({.angle = static_cast<float>(angle * kToRadians), .weight = weight});
}

// 0x004F2780
bool PedestrianBrainModule::turn_towards_node_after()
{
    const maths::Vec4& at = entity()->at_point();
    const double dz = static_cast<double>(at.z) - next_node_pos_.z;
    const double dx = static_cast<double>(at.x) - next_node_pos_.x;
    auto distance_sqr = static_cast<float>((dx * dx) + (dz * dz));
    bool turning = false;
    float bias = script::fetch_float(kTargetNodeBias);
    for (float& avoid : avoid_) {
        avoid = 0.0F;
    }
    if (node_after_ == list_link_) {
        // A dead end: reached when near enough.
        const double dead_end = script::fetch_float(kDeadEndDistance);
        if (static_cast<double>(distance_sqr) <= dead_end) {
            reach_waypoint();
        }
    } else {
        const int fade_distance = script::fetch_int_value(kind_ == 2 ? kSkaterFadeDistance : kFadeDistance);
        if (turn_frames_ != 0 && turn_frame_ >= turn_frames_) {
            reach_waypoint();
        } else if ((node_after_ != 0U) && static_cast<double>(fade_distance) > distance_sqr) {
            // Near the next node: turning towards the one after over a number of frames by the corner's angle.
            if (turn_frames_ == 0) {
                const KinematicsModule* motion = get_motion(entity());
                const maths::Vec4 from = {.x = node_pos_.x, .y = 0.0F, .z = node_pos_.z, .w = node_pos_.w};
                const maths::Vec4 next = {
                    .x = next_node_pos_.x, .y = 0.0F, .z = next_node_pos_.z, .w = next_node_pos_.w};
                const maths::Vec4 after = {
                    .x = node_after_pos_.x, .y = 0.0F, .z = node_after_pos_.z, .w = node_after_pos_.w};
                const maths::Vec4 in = next - from;
                const maths::Vec4 out = after - next;
                const double corner = maths::read_turn_angle(in, out) * kToRadians;
                const double reach =
                    std::sqrt(std::sqrt((std::cos(corner) + 1.0) * (static_cast<double>(distance_sqr) + distance_sqr)));
                turn_frames_ = truncate(reach / motion->path_max_speed() * kFramesPerSecond);
                if (turn_frames_ <= 0) {
                    turn_frames_ = 1;
                }
            }
            const int frames = turn_frames_;
            turning = true;
            const double fraction =
                frames == 0 ? 0.0 : (1.0 - (static_cast<double>(frames - turn_frame_) / frames)) * 0.5F;
            ++turn_frame_;
            auto to_after = static_cast<float>(fraction * bias);
            steer_towards(node_after_pos_, to_after);
            bias = static_cast<float>(static_cast<double>(bias) - to_after);
        }
    }
    steer_towards(next_node_pos_, bias);
    return turning;
}

// 0x004EF350
void PedestrianBrainModule::face_movement(maths::Vec4 old_pos)
{
    maths::Vec4 pos = entity()->at_point();
    maths::Mat4 matrix;
    if (kind_ == 0) {
        // A walker turns its orientation towards where it moved, at most so much a frame.
        core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
        core::at(matrix.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
        core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        maths::Vec4 heading = core::at(entity()->display_matrix().row, 2);
        heading.make_unit();
        maths::Vec4 moved = {.x = static_cast<float>(static_cast<double>(pos.x) - old_pos.x),
                             .y = 0.0F,
                             .z = static_cast<float>(static_cast<double>(pos.z) - old_pos.z),
                             .w = 1.0F};
        moved.make_unit();
        auto dot = static_cast<float>(
            ((static_cast<double>(moved.z) * heading.z) + (static_cast<double>(moved.y) * heading.y)) +
            (static_cast<double>(moved.x) * heading.x));
        if (dot > kMaxCos) {
            dot = kMaxCos;
        } else if (dot < kMinCos) {
            dot = kMinCos;
        }
        bool unused = false;
        const float turn = std::fabs(maths::acos_f({.value = dot, .limit = 0.0F}, &unused));
        if (!(static_cast<double>(step_seconds_) * kTurnPerSecond >= turn)) {
            static const float kMaxTurn = 0.69813174F; // (40 degrees)
            float turn_time = kTurnTime;
            if (turn > kMaxTurn) {
                turn_time = static_cast<float>(static_cast<double>(turn) / kMaxTurn * kTurnTime);
            }
            core::at(matrix.row, 2) = *walk::get_lerp(&pos, &heading, &moved, turn_time);
            core::at(matrix.row, 2).make_unit();
        } else {
            core::at(matrix.row, 2) = moved;
        }
        matrix.square_up_axes(2);
        entity()->basis_frame() = matrix;
    } else {
        // A skater faces where it moved (or along its path, to a lip).
        matrix = entity()->display_matrix();
        float old_w = old_pos.w;
        maths::Vec4 to = pos;
        if ((flag_bits_ & kPedFlagVertLip) != 0U) {
            old_pos = {.x = node_pos_.x, .y = node_pos_.y, .z = node_pos_.z, .w = old_pos.w};
            old_w = node_pos_.w;
            to = next_node_pos_;
        }
        maths::Vec4 heading = {.x = static_cast<float>(static_cast<double>(to.x) - old_pos.x),
                               .y = static_cast<float>(static_cast<double>(to.y) - old_pos.y),
                               .z = static_cast<float>(static_cast<double>(to.z) - old_pos.z),
                               .w = static_cast<float>(static_cast<double>(to.w) - old_w)};
        core::at(matrix.row, 2) = heading.make_unit();
        core::at(matrix.row, 0).w = 0.0F;
        core::at(matrix.row, 1).w = 0.0F;
        rebuild_across_and_up(matrix);
    }
    entity()->display_matrix() = matrix;
    saved_matrix_ = matrix;
}

// 0x004EF740
bool PedestrianBrainModule::probe_ground(const maths::Vec4* start, const maths::Vec4* end, maths::Vec4* point,
                                         GroundTriangle* triangle)
{
    Feeler feeler;
    render::ProbeRay* feeler_ray = feeler.feeler();
    if (feeler_ray->get_collision_along(start, end, false) == 0) {
        return false;
    }
    const render::HitMesh* object = feeler_ray->hit().object;
    const render::TriMeshSource* geometry = object->geometry();
    const uint32_t face = feeler_ray->hit().face;
    for (int i = 0; i < 3; ++i) {
        const uint32_t vertex = geometry->face_vertex(static_cast<int>(face), i);
        core::at(triangle->corners, i) = object->read_vertex_point(static_cast<int>(static_cast<uint16_t>(vertex)));
    }
    *point = feeler_ray->contact_pos();
    set_sound_terrain(feeler_ray->hit().terrain);
    return true;
}

// 0x004EF930
void PedestrianBrainModule::stick_to_ground_ped()
{
    KinematicsModule* motion = get_motion(entity());
    // The orientation's across row again (up x at).
    maths::Mat4& basis = entity()->basis_frame();
    const double ax = (static_cast<double>(core::at(basis.row, 1).y) * core::at(basis.row, 2).z) -
                      (static_cast<double>(core::at(basis.row, 1).z) * core::at(basis.row, 2).y);
    const double ay = (static_cast<double>(core::at(basis.row, 1).z) * core::at(basis.row, 2).x) -
                      (static_cast<double>(core::at(basis.row, 1).x) * core::at(basis.row, 2).z);
    const double az = (static_cast<double>(core::at(basis.row, 1).x) * core::at(basis.row, 2).y) -
                      (static_cast<double>(core::at(basis.row, 1).y) * core::at(basis.row, 2).x);
    core::at(basis.row, 0).w = 0.0F;
    core::at(basis.row, 0).z = static_cast<float>(az);
    core::at(basis.row, 0).x = static_cast<float>(ax);
    core::at(basis.row, 0).y = static_cast<float>(ay);
    core::at(basis.row, 0).make_unit();
    // A probe from above to below along the display matrix's up.
    const maths::Mat4 matrix = entity()->display_matrix();
    const maths::Vec4& up = core::at(matrix.row, 1);
    const maths::Vec4& pos = entity()->at_point();
    const float above = stick_above_;
    auto ux = static_cast<float>(static_cast<double>(up.x) * above);
    auto uy = static_cast<float>(static_cast<double>(up.y) * above);
    auto uz = static_cast<float>(static_cast<double>(up.z) * above);
    const double uw = static_cast<double>(above) * up.w;
    const maths::Vec4 start = {.x = static_cast<float>(static_cast<double>(ux) + pos.x),
                               .y = static_cast<float>(static_cast<double>(uy) + pos.y),
                               .z = static_cast<float>(static_cast<double>(uz) + pos.z), .w = static_cast<float>(uw + pos.w)};
    const float below = stick_below_;
    const float nz = -up.z;
    const float nw = -up.w;
    auto dx = static_cast<float>(-static_cast<double>(up.x) * below);
    auto dy = static_cast<float>(-static_cast<double>(up.y) * below);
    const double dz = static_cast<double>(nz) * below;
    auto dw = static_cast<float>(static_cast<double>(nw) * below);
    const maths::Vec4 end = {.x = static_cast<float>(static_cast<double>(dx) + pos.x),
                             .y = static_cast<float>(static_cast<double>(dy) + pos.y), .z = static_cast<float>(dz + pos.z),
                             .w = static_cast<float>(static_cast<double>(dw) + pos.w)};
    maths::Vec4 point;
    GroundTriangle last{motion->ground_triangle()}; // (the motion's last ground triangle, tried first and kept)
    const bool found = probe_ground(&start, &end, &point, &last);
    motion->set_ground_triangle(last.corners);
    if (found) {
        entity()->at_point() = point;
    }
}

// 0x004EF090
void PedestrianBrainModule::align_to_ground()
{
    const KinematicsModule* motion = get_motion(entity());
    if (!motion) {
        return;
    }
    GroundTriangle triangle{};
    get_assign(triangle, GroundTriangle{motion->ground_triangle()});
    const auto& corners = triangle.corners;
    const double e1x = static_cast<double>(core::at(corners, 1).x) - core::at(corners, 0).x;
    const double e1y = static_cast<double>(core::at(corners, 1).y) - core::at(corners, 0).y;
    const double e1z = static_cast<double>(core::at(corners, 1).z) - core::at(corners, 0).z;
    auto e2x = static_cast<float>(static_cast<double>(core::at(corners, 2).x) - core::at(corners, 0).x);
    auto e2y = static_cast<float>(static_cast<double>(core::at(corners, 2).y) - core::at(corners, 0).y);
    const double e2z = static_cast<double>(core::at(corners, 2).z) - core::at(corners, 0).z;
    maths::Vec4 normal = {.x = static_cast<float>((e2z * e1y) - (static_cast<double>(e2y) * e1z)),
                          .y = static_cast<float>((e1z * e2x) - (e2z * e1x)),
                          .z = static_cast<float>((static_cast<double>(e2y) * e1x) - (e1y * e2x)),
                          .w = 0.0F};
    maths::Vec4 up = normal.make_unit();
    if (up.y < 0.0F) {
        up = {.x = -up.x, .y = -up.y, .z = -up.z, .w = -up.w};
    }
    if (up != up_target_) {
        up_target_ = up;
        up_dir_ = up_shown_;
        up_blend_ = 1.0F;
    }
    if (!(up_blend_ == 0.0F)) {
        if (up_dir_ == up_target_) {
            up_blend_ = 0.0F;
        } else {
            const double blend = up_blend_ - (static_cast<double>(step_seconds_) * kUpBlendRate);
            auto blend_f = static_cast<float>(blend);
            up_blend_ = blend_f;
            if (blend > 0.0 || std::isnan(blend)) {
                maths::Vec4 between;
                up_shown_ = *walk::get_lerp(&between, &up_target_, &up_dir_, blend_f);
                up_shown_.make_unit();
            } else {
                up_blend_ = 0.0F;
                up_shown_ = up_target_;
            }
        }
    }
    maths::Mat4 matrix = entity()->display_matrix();
    if (core::at(matrix.row, 1) != up_shown_) {
        core::at(matrix.row, 1) = up_shown_;
        matrix.square_up_axes(1);
        entity()->display_matrix() = matrix;
        saved_matrix_ = matrix;
    }
}

// 0x004F01C0
void PedestrianBrainModule::spin_update()
{
    auto elapsed = static_cast<float>(static_cast<double>(step_seconds_) + spin_elapsed_);
    spin_elapsed_ = elapsed;
    double fraction = static_cast<double>(step_seconds_) / spin_time_;
    fraction = std::min(fraction, 1.0);
    auto turn = static_cast<float>(static_cast<double>(kHalfDegrees) - vert_rotation_target_);
    const uint32_t flags = flag_bits_;
    if ((flags & kPedFlagSpinning) != 0U) {
        twist_rate_ = static_cast<float>((static_cast<double>(spin_angle_) * fraction) + twist_rate_);
    }
    if ((flags & kPedFlagVertRotating) != 0U) {
        vert_rotation_ = static_cast<float>((static_cast<double>(turn) * fraction) + vert_rotation_);
    }
    if (elapsed >= spin_time_ || fraction >= 1.0) {
        if ((flags & kPedFlagSpinning) != 0U) {
            twist_rate_ = static_cast<float>(spin_angle_);
        }
        if ((flags & kPedFlagVertRotating) != 0U) {
            vert_rotation_ = turn;
        }
        flag_bits_ = flags & ~(kPedFlagSpinning | kPedFlagVertRotating);
    }
    if (((flag_bits_ & kPedFlagVertRotating) != 0U) && vert_rotation_ > turn) {
        vert_rotation_ = turn;
    }
    maths::Mat4 matrix = jump_matrix_;
    const maths::Vec4 rotation = {.x = static_cast<float>(static_cast<double>(vert_rotation_) * kToRadians),
                                  .y = static_cast<float>(static_cast<double>(twist_rate_) * kToRadians),
                                  .z = 0.0F,
                                  .w = 1.0F};
    matrix.turn_in_own_frame(rotation);
    entity()->display_matrix() = matrix;
    saved_matrix_ = matrix;
    up_target_ = core::at(matrix.row, 1);
    up_dir_ = up_target_;
    up_shown_ = up_dir_;
}

// 0x004F03C0
int PedestrianBrainModule::probe_ahead(maths::Vec4 direction, float range, maths::Vec4* out)
{
    auto range_sqr = static_cast<float>(static_cast<double>(range) * range);
    const maths::Mat4 matrix = entity()->display_matrix();
    maths::Vec4 dir = direction;
    dir.flatten_onto_plane(core::at(matrix.row, 1));
    const double length = std::sqrt(((static_cast<double>(dir.z) * dir.z) + (static_cast<double>(dir.y) * dir.y)) +
                                    (static_cast<double>(dir.x) * dir.x));
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        dir.x = static_cast<float>(dir.x * inverse);
        dir.y = static_cast<float>(dir.y * inverse);
        dir.z = static_cast<float>(dir.z * inverse);
    }
    // A ped close ahead?
    ObjectSearch search;
    ObjectHubBase* manager = entity()->park_owner();
    memory::ListNode* list = manager->fetch_object_set();
    for (auto* other = core::down_cast<GameEntity>(search.first_item_of_type(list, kObjectTypePed)); other;
         other = core::down_cast<GameEntity>(search.next_item_of_type())) {
        const GameEntity* me = entity();
        if (other == me) {
            continue;
        }
        auto dz = static_cast<float>(static_cast<double>(other->at_point().z) - me->at_point().z);
        auto dy = static_cast<float>(static_cast<double>(other->at_point().y) - me->at_point().y);
        const double dx = static_cast<double>(other->at_point().x) - me->at_point().x;
        const double distance_sqr = ((dx * dx) + (static_cast<double>(dy) * dy)) + (static_cast<double>(dz) * dz);
        if (!(distance_sqr < range_sqr)) {
            continue;
        }
        const double distance = std::sqrt(distance_sqr);
        auto distance_f = static_cast<float>(distance);
        double nx = dx;
        double nz = dz;
        float ny = dy;
        if (distance > 0.0) {
            const double inverse = 1.0 / distance_f;
            nx = dx * inverse;
            ny = static_cast<float>(static_cast<double>(dy) * inverse);
            nz = inverse * dz;
        }
        const double dot = ((static_cast<double>(dir.z) * nz) + (static_cast<double>(ny) * dir.y)) + (nx * dir.x);
        if (dot > kAhead) {
            if (out) {
                *out = other->at_point();
            }
            return 3;
        }
    }
    // The world: a line from 48 up to 8 up `range` ahead.
    dir.x = static_cast<float>(static_cast<double>(dir.x) * range);
    dir.y = static_cast<float>(static_cast<double>(dir.y) * range);
    dir.z = static_cast<float>(static_cast<double>(dir.z) * range);
    dir.w = static_cast<float>(static_cast<double>(dir.w) * range);
    const maths::Vec4& at = entity()->at_point();
    maths::Vec4 start = at;
    start.y = static_cast<float>(static_cast<double>(start.y) + kReach);
    auto top_y = static_cast<float>(static_cast<double>(dir.y) + at.y);
    const maths::Vec4 end = {.x = static_cast<float>(static_cast<double>(dir.x) + at.x),
                             .y = static_cast<float>(static_cast<double>(top_y) + kProbeLift),
                             .z = static_cast<float>(static_cast<double>(dir.z) + at.z),
                             .w = static_cast<float>(static_cast<double>(dir.w) + at.w)};
    Feeler feeler(&start, &end);
    render::ProbeRay* feeler_ray = feeler.feeler();
    feeler_ray->set_excluded({.ignore = render::kDefaultIgnoredFaceFlags, .require = render::kNoFaceFlags});
    if (feeler_ray->get_hit_shape(true, false) == 0) {
        return 0;
    }
    if (out) {
        *out = feeler_ray->contact_pos();
    }
    return 1;
}

// --- the move updates ---

// 0x004F2A10
void PedestrianBrainModule::generic_move_update()
{
    if ((flag_bits_ & kPedFlagMoving) == 0U) {
        return;
    }
    KinematicsModule* motion = get_motion(entity());
    const bool avoided = avoid_peds();
    if (!turn_towards_node_after() && !avoided) {
        keep_to_path();
    }
    const maths::Vec4 old = entity()->at_point();
    motion->update_path_speed();
    move_steered();
    if (old != entity()->at_point()) {
        face_movement(old);
    }
    decay_bias();
    motion->stick_to_ground();
}

// (Part of 0x004F2AD0) Reaching the next waypoint (in the air, or for a vert node, in 3D); near enough, the
// crouch before a jump.
void PedestrianBrainModule::approach_waypoint(float step, bool airborne)
{
    // Reaching the next waypoint (in the air, or for a vert node, in 3D).
    const maths::Vec4& at = entity()->at_point();
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    if (((flag_bits_ & kPedFlagVertNext) != 0U) || airborne) {
        dx = static_cast<double>(at.x) - next_node_pos_.x;
        dy = static_cast<float>(static_cast<double>(at.y) - next_node_pos_.y);
        dz = static_cast<float>(static_cast<double>(at.z) - next_node_pos_.z);
    } else {
        dx = static_cast<double>(at.x) - next_node_pos_.x;
        dy = 0.0F;
        dz = static_cast<float>(static_cast<double>(at.z) - next_node_pos_.z);
    }
    auto distance_sqr = static_cast<float>(((dx * dx) + (dz * dz)) + (dy * dy));
    const int min_distance = script::fetch_int_value(kMinDistanceToWaypoint);
    if (((flag_bits_ & kPedFlagJumpingToNode) == 0U) &&
        (static_cast<double>(step) * step > distance_sqr || static_cast<double>(min_distance) > distance_sqr)) {
        reach_waypoint();
    }
    if ((flag_bits_ & kPedFlagCrouch) != 0U) {
        if (static_cast<double>(script::fetch_int_value(kCrouchDistance)) > distance_sqr) {
            flag_bits_ &= ~kPedFlagCrouch;
            change_phase(kStateJumpCrouch, kSkaterPedStateTable, nullptr);
        }
    }
}

// (Part of 0x004F2AD0) The move this frame: steered along the path, or straight at the next node; kept to the
// ground unless grinding. The sound state it asks for: 1 in the air, else -1 (none).
int PedestrianBrainModule::move_skater_ped(float step, bool airborne)
{
    int sound = -1;
    const maths::Vec4 old = entity()->at_point();
    bool faced = false;
    if (kind_ == 2 &&
        ((flag_bits_ & (kPedFlagGrinding | kPedFlagVertNext | kPedFlagVertLast | kPedFlagGrindBailing)) == 0U) &&
        ((flag_bits_ & kPedFlagJumpingToNode) == 0U)) {
        if (!turn_towards_node_after()) {
            keep_to_path();
        }
        move_steered();
        if (airborne) {
            entity()->display_matrix() = saved_matrix_;
        }
    } else {
        // Straight at the next node.
        maths::Vec4 to = {.x = next_node_pos_.x - old.x,
                          .y = next_node_pos_.y - old.y,
                          .z = next_node_pos_.z - old.z,
                          .w = next_node_pos_.w - old.w};
        const maths::Vec4& dir = to.make_unit();
        auto z = static_cast<float>(static_cast<double>(step) * dir.z);
        auto w = static_cast<float>(static_cast<double>(step) * dir.w);
        const maths::Vec4 moved = {.x = static_cast<float>((static_cast<double>(step) * dir.x) + old.x),
                                   .y = static_cast<float>((static_cast<double>(step) * dir.y) + old.y),
                                   .z = static_cast<float>(static_cast<double>(z) + old.z),
                                   .w = static_cast<float>(static_cast<double>(w) + old.w)};
        keep_to_path();
        move_steered();
        entity()->at_point() = moved;
        face_movement(old);
        faced = true;
    }
    if ((flag_bits_ & (kPedFlagGrinding | kPedFlagGrindBailing)) == 0U) {
        if (airborne) {
            sound = 1;
        } else {
            stick_to_ground_ped();
            if (!faced) {
                face_movement(old);
            }
            align_to_ground();
        }
    } else if (airborne) {
        sound = 1;
    }
    return sound;
}

// 0x004F2AD0
void PedestrianBrainModule::skater_move_update()
{
    KinematicsModule* motion = get_motion(entity());
    motion->update_path_speed();
    auto step = static_cast<float>(static_cast<double>(step_seconds_) * motion->path_speed());
    const bool airborne = (motion->motion_flags() & 0x100) != 0;
    if ((flag_bits_ & kPedFlagGrinding) != 0U) {
        if (const GameEntity* skater = local_skater()) {
            const maths::Vec4& at = entity()->at_point();
            const double dx = static_cast<double>(at.x) - skater->at_point().x;
            const double dy = static_cast<double>(at.y) - skater->at_point().y;
            const double dz = static_cast<double>(at.z) - skater->at_point().z;
            auto distance_sqr = static_cast<float>(((dz * dz) + (dy * dy)) + (dx * dx));
            if (script::fetch_float(kMinDistanceToSkater) > distance_sqr) {
                bail_from_grind();
                return;
            }
        }
    }
    if (((flag_bits_ & kPedFlagMoving) != 0U) || airborne) {
        approach_waypoint(step, airborne);
    }
    int sound = -1;
    if (((flag_bits_ & kPedFlagMoving) != 0U) && step != 0.0F) {
        sound = move_skater_ped(step, airborne);
    } else {
        push_position();
        entity()->display_matrix() = saved_matrix_;
        if ((flag_bits_ & (kPedFlagSpinning | kPedFlagVertRotating)) != 0U) {
            spin_update();
        }
        if (airborne) {
            sound = 1;
        }
    }
    if (sound < 0) {
        if ((flag_bits_ & kPedFlagGrinding) != 0U) {
            sound = 4;
        } else if ((flag_bits_ & kPedFlagMoving) != 0U) {
            sound = 0;
        } else if ((flag_bits_ & kPedFlagSoundState3) != 0U) {
            sound = 3;
        }
    }
    if (sound >= 0) {
        set_sound_state(sound);
    }
    if ((flag_bits_ & kPedFlagMoving) != 0U) {
        speed_ratio_ = motion->path_max_speed() == 0.0F
                           ? 0.0F
                           : static_cast<float>(static_cast<double>(motion->path_speed()) / motion->path_max_speed());
    } else {
        speed_ratio_ = 0.0F;
    }
    if (auto* looping = core::down_cast<RiderRollingAudio>(entity()->fetch_module(kSkaterLoopingSound))) {
        looping->set_no_rail_sound(static_cast<uint8_t>((flag_bits_ & (kPedFlagBailing | kPedFlagGrindBailing)) != 0));
        looping->set_pace_ratio(speed_ratio_);
    }
}

// 0x004F1890
void PedestrianBrainModule::bail_move_update()
{
    KinematicsModule* motion = get_motion(entity());
    motion->update_path_speed();
    auto step = static_cast<float>(static_cast<double>(step_seconds_) * motion->path_speed());
    if ((flag_bits_ & (kPedFlagBailing | kPedFlagGrindBailing)) == 0U) {
        // The bail's over: back to skating.
        update_type_ = kSkaterMoveUpdate;
        script::launch_script(kPedInitializeMovement, nullptr, entity(), false, nullptr);
        if (update_type_ == kGenericMoveUpdate || update_type_ == kSkaterMoveUpdate) {
            flag_bits_ |= kPedFlagMoving;
        }
        return;
    }
    if ((flag_bits_ & kPedFlagMoving) != 0U) {
        const maths::Vec4& at = entity()->at_point();
        auto dx = static_cast<float>(static_cast<double>(at.x) - next_node_pos_.x);
        const double dz = static_cast<double>(at.z) - next_node_pos_.z;
        auto distance_sqr = static_cast<float>((dz * dz) + (static_cast<double>(dx) * dx));
        const int min_distance = script::fetch_int_value(kMinDistanceToWaypoint);
        if (((flag_bits_ & kPedFlagJumpingToNode) == 0U) &&
            (static_cast<double>(step) * step > distance_sqr || static_cast<double>(min_distance) > distance_sqr)) {
            flag_bits_ &= ~kPedFlagMoving;
        }
    }
    if (((flag_bits_ & kPedFlagMoving) != 0U) && !(step == 0.0F)) {
        const maths::Vec4 old = entity()->at_point();
        maths::Vec4 to = {.x = next_node_pos_.x - old.x, .y = next_node_pos_.y - old.y, .z = next_node_pos_.z - old.z,
                          .w = next_node_pos_.w - old.w};
        const maths::Vec4& dir = to.make_unit();
        auto z = static_cast<float>(static_cast<double>(step) * dir.z);
        auto w = static_cast<float>(static_cast<double>(step) * dir.w);
        const maths::Vec4 moved = {.x = static_cast<float>((static_cast<double>(step) * dir.x) + old.x),
                                   .y = static_cast<float>((static_cast<double>(step) * dir.y) + old.y),
                                   .z = static_cast<float>(static_cast<double>(z) + old.z),
                                   .w = static_cast<float>(static_cast<double>(w) + old.w)};
        keep_to_path();
        move_steered();
        entity()->at_point() = moved;
    }
    entity()->display_matrix() = saved_matrix_;
}

// 0x004F1780
void PedestrianBrainModule::go_home_update()
{
    maths::Vec4 home = {.x = node_pos_.x - entity()->at_point().x, .y = node_pos_.y - entity()->at_point().y,
                        .z = node_pos_.z - entity()->at_point().z, .w = node_pos_.w - entity()->at_point().w};
    const double distance_sqr = ((static_cast<double>(home.z) * home.z) + (static_cast<double>(home.y) * home.y)) +
                                (static_cast<double>(home.x) * home.x);
    if (distance_sqr <= kNear) {
        script::launch_script(kEndReturnHome, nullptr, entity(), false, nullptr);
        return;
    }
    home.make_unit();
    KinematicsModule* motion = get_motion(entity());
    push_position();
    maths::Vec4& pos = entity()->at_point();
    const maths::Vec4 old = pos;
    const double step = static_cast<double>(step_seconds_) * motion->path_max_speed();
    auto step_f = static_cast<float>(step);
    pos.w = 1.0F;
    pos.x = static_cast<float>((step * home.x) + old.x);
    pos.z = static_cast<float>((static_cast<double>(step_f) * home.z) + old.z);
    face_movement(old);
    motion->stick_to_ground();
}

// 0x004F1480
void PedestrianBrainModule::chase_update()
{
    const GameEntity* skater = local_skater();
    if (!skater) {
        return;
    }
    KinematicsModule* motion = get_motion(entity());
    auto dz = static_cast<float>(static_cast<double>(skater->at_point().z) - entity()->at_point().z);
    maths::Vec4 toward_player = {
        .x = static_cast<float>(static_cast<double>(skater->at_point().x) - entity()->at_point().x),
        .y = 0.0F,
        .z = dz,
        .w = 0.0F};
    const double distance_sqr =
        (static_cast<double>(toward_player.x) * toward_player.x) + (static_cast<double>(dz) * dz);
    if (distance_sqr <= kCatchDistanceSqr) {
        // Caught.
        face_along(&toward_player);
        script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kChaser}, skater->ident());
        change_phase(kStateChaseCatch, 0, params.get());
        params.reset();
        return;
    }
    if (distance_sqr > kChaseOutOfRangeSqr) {
        script::launch_script(kEndPlayerChase, nullptr, entity(), false, nullptr);
        return;
    }
    int max_home = 0;
    if (entity()->read_int_tag(kMaxHomeDist, &max_home) && max_home > 0) {
        const maths::Vec4 from_home = node_pos_ - entity()->at_point();
        const double length_sqr =
            ((static_cast<double>(from_home.x) * from_home.x) + (static_cast<double>(from_home.y) * from_home.y)) +
            (static_cast<double>(from_home.z) * from_home.z);
        if (static_cast<double>(max_home * max_home) <= length_sqr) {
            script::launch_script(kEndPlayerChase, nullptr, entity(), false, nullptr);
            return;
        }
    }
    if (probe_ahead({.x = toward_player.x, .y = 0.0F, .z = dz, .w = 0.0F}, kReach, nullptr) != 0) {
        if (cur_phase_ != kStateChasePause) {
            change_phase(kStateChasePause, 0, nullptr);
        }
        return;
    }
    if (cur_phase_ != kStateChase) {
        change_phase(kStateChase, 0, nullptr);
    }
    motion->set_path_max_speed(kChaseSpeed);
    toward_player.make_unit();
    push_position();
    maths::Vec4& pos = entity()->at_point();
    const maths::Vec4 old = pos;
    const double step = static_cast<double>(step_seconds_) * motion->path_max_speed();
    auto step_f = static_cast<float>(step);
    pos.w = 1.0F;
    pos.x = static_cast<float>((step * toward_player.x) + old.x);
    pos.z = static_cast<float>((static_cast<double>(step_f) * toward_player.z) + old.z);
    face_movement(old);
    motion->stick_to_ground();
}

// 0x004F0DD0
void PedestrianBrainModule::blocker_update()
{
    const GameEntity* skater = local_skater();
    if (!skater) {
        return;
    }
    KinematicsModule* motion = get_motion(entity());
    auto sz = static_cast<float>(static_cast<double>(skater->at_point().z) - entity()->at_point().z);
    const maths::Vec4 toward_player = {
        .x = static_cast<float>(static_cast<double>(skater->at_point().x) - entity()->at_point().x),
        .y = 0.0F,
        .z = sz,
        .w = 0.0F};
    auto path_z = static_cast<float>(static_cast<double>(next_node_pos_.z) - node_pos_.z);
    auto path_x = static_cast<float>(static_cast<double>(next_node_pos_.x) - node_pos_.x);
    // Across the path, on the skater's side.
    const double side = (static_cast<double>(path_x) * sz) - (static_cast<double>(path_z) * toward_player.x);
    const double turn = side < 0.0 ? kBlockerTurn : -kBlockerTurn;
    auto sine = static_cast<float>(std::sin(turn));
    auto cosine = static_cast<float>(std::cos(turn));
    const double across_x = (std::cos(turn) * path_x) + (static_cast<double>(path_z) * sine);
    const double across_z = -(static_cast<double>(path_x) * sine) + (static_cast<double>(cosine) * path_z);
    maths::Vec4 across = {.x = static_cast<float>(across_x), .y = 0.0F, .z = static_cast<float>(across_z), .w = 0.0F};
    across.make_unit();
    if ((static_cast<double>(toward_player.x) * toward_player.x) + (static_cast<double>(sz) * sz) <=
        kCatchDistanceSqr) {
        // Bumped.
        face_along(&across);
        script::StructPtr params = script::make_struct();
        params->put_vec3(checksums::Name{kVelDir}, script::VectorValue{.x = across.x, .y = across.y, .z = across.z, .w = across.w});
        entity()->post_own_event(kBlockerBumpPlayer, params.get());
        params.reset();
        return;
    }
    // Where the skater's going crosses the path.
    const maths::Vec4& vel = skater->vel();
    const bool not_approaching = !(((static_cast<double>(across.z) * vel.z) + (static_cast<double>(across.y) * vel.y)) +
                                       (static_cast<double>(across.x) * vel.x) >
                                   0.0);
    const double speed_sqr = ((static_cast<double>(vel.x) * vel.x) + (static_cast<double>(vel.y) * vel.y)) +
                             (static_cast<double>(vel.z) * vel.z);
    const maths::Vec4& at = skater->at_point();
    maths::Vec4 end;
    maths::Vec4 on_path;
    float along_path = 0.0F;
    float along_skater = 0.0F;
    if (!(speed_sqr < kFastSkaterSqr) && not_approaching) {
        auto ex = static_cast<float>(static_cast<double>(vel.x) * kLookAhead);
        auto ey = static_cast<float>(static_cast<double>(vel.y) * kLookAhead);
        auto ez = static_cast<float>(static_cast<double>(vel.z) * kLookAhead);
        const double ew = static_cast<double>(vel.w) * kLookAhead;
        end = {.x = static_cast<float>(static_cast<double>(ex) + at.x), .y = static_cast<float>(static_cast<double>(ey) + at.y),
               .z = static_cast<float>(static_cast<double>(ez) + at.z), .w = static_cast<float>(ew + at.w)};
    } else {
        auto ex = static_cast<float>(static_cast<double>(across.x) * -kLookAhead);
        auto ey = static_cast<float>(static_cast<double>(across.y) * -kLookAhead);
        auto ez = static_cast<float>(static_cast<double>(across.z) * -kLookAhead);
        const double ew = static_cast<double>(across.w) * -kLookAhead;
        end = {.x = static_cast<float>(static_cast<double>(ex) + at.x), .y = static_cast<float>(static_cast<double>(ey) + at.y),
               .z = static_cast<float>(static_cast<double>(ez) + at.z), .w = static_cast<float>(ew + at.w)};
    }
    const maths::Seg path(next_node_pos_, node_pos_);
    const maths::Seg skater_line(at, end);
    maths::closest_points_between_lines(path, skater_line, on_path, end, along_path, along_skater, true);
    double speed = std::sqrt(((static_cast<double>(vel.x) * vel.x) + (static_cast<double>(vel.y) * vel.y)) +
                             (static_cast<double>(vel.z) * vel.z));
    auto gz = static_cast<float>(static_cast<double>(on_path.z) - at.z);
    auto gy = static_cast<float>(static_cast<double>(on_path.y) - at.y);
    auto gx = static_cast<float>(static_cast<double>(on_path.x) - at.x);
    if (speed == 0.0) {
        speed = kNoSpeed;
    }
    const double skater_time =
        std::sqrt(((static_cast<double>(gx) * gx) + (static_cast<double>(gy) * gy)) + (static_cast<double>(gz) * gz)) /
        speed;
    const maths::Vec4& me = entity()->at_point();
    const double tx = static_cast<double>(on_path.x) - me.x;
    const double ty = static_cast<double>(on_path.y) - me.y;
    const double tz = static_cast<double>(on_path.z) - me.z;
    const double tw = static_cast<double>(on_path.w) - me.w;
    maths::Vec4 to_point = {.x = static_cast<float>(tx), .y = static_cast<float>(ty), .z = static_cast<float>(tz),
                            .w = static_cast<float>(tw)};
    const double distance = std::sqrt(((tx * tx) + (ty * ty)) + (tz * tz));
    auto distance_f = static_cast<float>(distance);
    if (distance <= kStandDistance) {
        face_along(&toward_player);
        entity()->post_own_event(kBlockerStand, nullptr);
        return;
    }
    auto needed = static_cast<float>(static_cast<double>(distance_f) / skater_time);
    to_point.make_unit();
    if (!(needed > kRunRatio) && !(distance_f > kRunDistance)) {
        // Near enough: a shuffle to the side the point is on.
        face_along(&across);
        const script::StructPtr params = script::make_struct();
        const maths::Vec4 cross = maths::cross_vec(to_point, across);
        params->put_name_key(checksums::Name{kDir}, cross.y < 0.0F ? kLeft : kRight);
        entity()->post_own_event(kBlockerShuffle, params.get());
    } else {
        face_along(&to_point);
        entity()->post_own_event(kBlockerRun, nullptr);
    }
    float speed_needed = needed > kMinBlockerSpeed ? needed : kMinBlockerSpeed;
    motion->set_path_max_speed(speed_needed);
    if (!(speed_needed < kMaxBlockerSpeed)) {
        speed_needed = kMaxBlockerSpeed;
    }
    motion->set_path_max_speed(speed_needed);
    push_position();
    const double step = static_cast<double>(step_seconds_) * motion->path_max_speed();
    maths::Vec4& pos = entity()->at_point();
    pos.w = 1.0F;
    pos.x = static_cast<float>((static_cast<double>(to_point.x) * step) + pos.x);
    pos.z = static_cast<float>((static_cast<double>(to_point.z) * step) + pos.z);
}

// 0x004F2190
void PedestrianBrainModule::tick_frame()
{
    step_seconds_ = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    switch (update_type_) {
    case kGenericMoveUpdate:
        generic_move_update();
        break;
    case kGoHomeUpdate:
        go_home_update();
        break;
    case kChaseUpdate:
        chase_update();
        break;
    case kBlockerUpdate:
        blocker_update();
        break;
    case kNoMoveUpdate:
        no_move_update();
        break;
    case kSkaterMoveUpdate:
        skater_move_update();
        break;
    case kLipTrickUpdate:
        lip_trick_update();
        break;
    case kBailUpdate:
        bail_move_update();
        break;
    default:
        break;
    }
    if (aux_script_) {
        aux_script_->tick_frame();
    }
}

// --- script member functions ---

// 0x004F0950
bool PedestrianBrainModule::ped_turn_to_current_waypoint_orientation(script::Struct* params, script::Script* /*script*/)
{
    KinematicsModule* motion = get_motion(entity());
    maths::Mat4 matrix;
    core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 angles = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    level_nodes::read_turn_angles(list_link_, &angles);
    maths::Mat4 rotation;
    d3dx_multiply(matrix, matrix, maths::make_pitch_matrix(rotation, angles.x));
    d3dx_multiply(matrix, matrix, maths::make_yaw_matrix(rotation, angles.y));
    d3dx_multiply(matrix, matrix, maths::make_roll_matrix(rotation, angles.z));
    for (int i = 0; i < 4; ++i) {
        core::at(matrix.row, 2)[i] = static_cast<float>(static_cast<double>(core::at(matrix.row, 2)[i]) * kReach);
    }
    const maths::Vec4& at = entity()->at_point();
    const maths::Vec4 target = {.x = static_cast<float>(static_cast<double>(core::at(matrix.row, 2).x) + at.x),
                                .y = static_cast<float>(static_cast<double>(core::at(matrix.row, 2).y) + at.y),
                                .z = static_cast<float>(static_cast<double>(core::at(matrix.row, 2).z) + at.z),
                                .w = static_cast<float>(static_cast<double>(core::at(matrix.row, 2).w) + at.w)};
    motion->get_look_at(params, &target);
    return true;
}

} // namespace objects
