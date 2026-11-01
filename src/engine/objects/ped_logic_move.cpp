// The PedLogic component (GameObjects::PedestrianBrainModule): see ped_logic.h. Its movement - the steering (round other peds,
// towards the waypoints, back to the path), facing where it goes, the ground under it, the skaters' spins - and the
// move updates (generic, skater, bail, go home, chase, blocker); from the disassembly of 0x004ED5E0-0x004F2F08
// (0x004EF090, 0x004EF930, 0x004F0950 and 0x004F2AD0 aren't Ghidra functions). The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double; FSIN / FCOS results
// are used at their full precision where the original goes on computing with them.

#include <cmath>
#include <cstdint>

#include "game/rider/rider.h"
#include "engine/gfx/d3dx.h"
#include "engine/core/manager.h"
#include "engine/maths/maths.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/maths/trig.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/path_manager.h"
#include "engine/objects/ped_logic.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/rider/looping_sound.h"
#include "platform/stopwatch.h"
#include "engine/objects/entity_hub.h"

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

constexpr float kQuarterTurn = 1.5707963705062866f;      // 0x00645484
constexpr float kThreeQuarterTurns = 4.71238899230957f;  // 0x0064B76C
constexpr float kFullTurn = 6.2831854820251465f;         // 0x0064545C
constexpr float kInverseFullTurn = 0.15915493667125702f; // 0x0064A044
constexpr float kHalfTurn = 3.1415927410125732f;         // 0x0064B730
constexpr float kDegrees = 360.0f;                       // 0x00645EA8
constexpr float kInverseDegrees = 1.0f / 360.0f;         // 0x0064B798
constexpr float kHalfDegrees = 180.0f;                   // 0x0064B278
constexpr float kToRadians = 0.01745329238474369f;       // 0x00645464
constexpr float kFramesPerSecond = 60.0f;                // 0x00645EA0
constexpr float kMaxCos = 0.9998999834060669f;           // 0x0064B7F4
constexpr float kMinCos = -0.9998999834060669f;          // 0x0064B7EC
constexpr float kTurnPerSecond = 0.05235987901687622f;   // 0x0064B7F0 (3 degrees)
constexpr float kTurnTime = 0.083f;                      // 0x0064B7E8
constexpr double kAhead = 0.866;                         // 0x0064B7F8 (a double: about cos 30 degrees)
constexpr float kReach = 48.0f;                          // 0x006461CC
constexpr float kProbeLift = 8.0f;                       // 0x006461D4
constexpr float kNear = 1000.0f;                         // 0x006453C0
constexpr float kCatchDistanceSqr = 3047.039794921875f;  // 0x0067EFEC
constexpr float kChaseOutOfRangeSqr = 360000.0f;         // 0x0067EFF4
constexpr float kChaseSpeed = 400.0f;
constexpr float kFastSkaterSqr = 50625.0f;          // 0x0067EFF8
constexpr float kLookAhead = 600.0f;                // 0x0064B81C
constexpr float kNoSpeed = 0.001f;                  // 0x006461D8
constexpr double kStandDistance = 3.6;              // 0x0064B810
constexpr float kRunRatio = 225.0f;                 // 0x0067EFF0
constexpr float kRunDistance = 96.0f;               // 0x0064B808
constexpr float kMinBlockerSpeed = 140.0f;          // 0x0064B804
constexpr float kMaxBlockerSpeed = 1500.0f;         // 0x0064B800
constexpr double kBlockerTurn = 1.5707963705062866; // 0x0064B828 / 0x0064B820 (+ / -)
constexpr float kUpBlendRate = 6.0f;                // 0x00646284

KinematicsModule* get_motion(GameEntity* object)
{
    return static_cast<KinematicsModule*>(object->fetch_module(kMotion));
}
MotionPathMembers* motion_path(KinematicsModule* motion) { return reinterpret_cast<MotionPathMembers*>(motion); }

GameEntity* local_skater() { return static_cast<GameEntity*>(modules::Skate::sole()->get_home_rider()); }

// D3DXMatrixMultiply (linked library code).
void d3dx_multiply(maths::Mat4& out, const maths::Mat4& a, const maths::Mat4& b)
{
    d3dx::matrix_multiply(&out, &a, &b);
}

// _ftol2: a 64-bit truncation whose low half is used (the integer indefinite, 2^63 - a NaN, what doesn't fit - gives
// 0).
int truncate(double value)
{
    if (!(std::fabs(value) < 9.2e18))
        return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

script::Struct* new_params()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? new (memory) script::Struct : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// The collision feeler (0xB0 bytes): what it hit is held by a smart pointer at +0x98, let go when it goes.
class Feeler {
public:
    Feeler() = default;
    Feeler(const maths::Vec4* start, const maths::Vec4* end) : ray_(start, end) {}
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    render::ProbeRay* feeler() { return &ray_; }

private:
    render::ProbeRay ray_;
};

// The cross product's rows of a facing matrix: across = up x at (`a` kept unrounded), up again = at x across.
void rebuild_across_and_up(maths::Mat4& m)
{
    const maths::Vec4& up = m.row[1];
    const maths::Vec4& at = m.row[2];
    double a = static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z;
    auto b = static_cast<float>(static_cast<double>(at.x) * up.z - static_cast<double>(at.z) * up.x);
    auto c = static_cast<float>(static_cast<double>(at.y) * up.x - static_cast<double>(at.x) * up.y);
    double up_x = static_cast<double>(c) * at.y - static_cast<double>(b) * at.z;
    double up_y = static_cast<double>(at.z) * a - static_cast<double>(c) * at.x;
    auto up_z = static_cast<float>(static_cast<double>(b) * at.x - static_cast<double>(at.y) * a);
    m.row[0] = {static_cast<float>(a), b, c, m.row[0].w};
    m.row[1] = {static_cast<float>(up_x), static_cast<float>(up_y), up_z, m.row[1].w};
}

} // namespace

// --- steering ---

// 0x004ECFF0
void PedestrianBrainModule::decay_bias()
{
    float decay = script::fetch_float(kBiasDecay);
    double threshold = script::fetch_float(kMinBias);
    for (float& bias : bias_) {
        // The product is compared unrounded (x87); a NaN is kept.
        double value = static_cast<double>(decay) * bias;
        bias = static_cast<float>(value);
        if (value < threshold)
            bias = 0.0f;
    }
}

// 0x004ED5E0
void PedestrianBrainModule::add_avoidance(float angle, float strength, float distance, float range)
{
    double weight = (static_cast<double>(range) - distance) / range * strength;
    for (int i = 0; i < 4; ++i) {
        double offset = static_cast<double>(i) * kQuarterTurn - angle;
        double size = std::fabs(offset);
        if (!(size < kQuarterTurn) && !(size > kThreeQuarterTurns || std::isnan(size)))
            continue;
        double value = std::cos(offset) * weight + bias_[i];
        bias_[i] = static_cast<float>(value);
        if (value > 1.0)
            bias_[i] = 1.0f;
        else if (value < -1.0)
            bias_[i] = -1.0f;
    }
}

// 0x004ED680
void PedestrianBrainModule::add_steer(float angle, float weight)
{
    for (int i = 0; i < 4; ++i) {
        double offset = static_cast<double>(i) * kQuarterTurn - angle;
        double size = std::fabs(offset);
        if (!(size < kQuarterTurn) && !(size > kThreeQuarterTurns || std::isnan(size)))
            continue;
        double value = std::cos(offset) * weight + avoid_[i];
        avoid_[i] = static_cast<float>(value);
        if (value > 1.0)
            avoid_[i] = 1.0f;
        else if (value < -1.0)
            avoid_[i] = -1.0f;
    }
}

// 0x004ED710
float PedestrianBrainModule::steer_sideways()
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        auto quarter = static_cast<float>(static_cast<double>(i) * kQuarterTurn);
        double weight = static_cast<double>(bias_[i]) + avoid_[i];
        if (weight > 1.0)
            weight = 1.0;
        sum = sum - std::sin(static_cast<double>(quarter)) * weight;
        if (sum >= 1.0)
            return 1.0f;
        if (sum <= -1.0)
            return -1.0f;
    }
    auto result = static_cast<float>(sum);
    if (script::fetch_float(kMinBias) > std::fabs(result))
        return 0.0f;
    return result;
}

// 0x004ED7D0
float PedestrianBrainModule::steer_forward()
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        auto quarter = static_cast<float>(static_cast<double>(i) * kQuarterTurn);
        double weight = static_cast<double>(bias_[i]) + avoid_[i];
        if (weight > 1.0)
            weight = 1.0;
        else if (weight < -1.0)
            weight = -1.0;
        sum = std::cos(static_cast<double>(quarter)) * weight + sum;
        if (sum >= 1.0)
            return 1.0f;
        if (sum <= -1.0)
            return -1.0f;
    }
    auto result = static_cast<float>(sum);
    if (script::fetch_float(kMinBias) > std::fabs(result))
        return 0.0f;
    return result;
}

// 0x004EE650
void PedestrianBrainModule::face_along(const maths::Vec4* direction)
{
    maths::Mat4 m;
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {direction->x, 0.0f, direction->z, 1.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[2].make_unit();
    m.row[0].w = 0.0f;
    m.row[1].w = 0.0f;
    rebuild_across_and_up(m);
    entity_->display_matrix_ = m;
    entity_->basis_frame_ = m;
    saved_matrix_ = m;
}

// 0x004EE7D0
bool PedestrianBrainModule::avoid_peds()
{
    bool avoided = false;
    if (!peds_)
        return false;
    SharedRef<Entity>* slot = peds_->slots_;
    maths::Mat4 m = entity_->display_matrix_;
    const maths::Vec4& at = m.row[2];
    float range = script::fetch_float(kAvoidPedRange);
    float bias = script::fetch_float(kPedDodgeBias);
    KinematicsModule* motion = get_motion(entity_);
    int count = peds_->extent_;
    if (count <= 0)
        return false;
    for (; count; --count, ++slot) {
        auto* other = reinterpret_cast<GameEntity*>(slot->owner_object_);
        if (!other || other == entity_)
            continue;
        double max_y = script::fetch_float(kMaxYDistance);
        GameEntity* me = entity_;
        if (std::fabs(static_cast<double>(me->at_point_.y) - other->at_point_.y) > max_y)
            continue;
        const maths::Vec4& other_at = other->display_matrix_.row[2];
        double dot = (static_cast<double>(at.y) * other_at.y + static_cast<double>(at.z) * other_at.z) +
                     static_cast<double>(at.x) * other_at.x;
        auto heading_dot = static_cast<float>(dot);
        if (dot <= 0.0) {
            // Heading the other way: only one ahead.
            double dx = static_cast<double>(other->at_point_.x) - me->at_point_.x;
            double dy = static_cast<double>(other->at_point_.y) - me->at_point_.y;
            double dz = static_cast<double>(other->at_point_.z) - me->at_point_.z;
            double ahead =
                (static_cast<double>(at.z) * dz + static_cast<double>(at.y) * dy) + static_cast<double>(at.x) * dx;
            if (ahead <= 0.0)
                continue;
        }
        KinematicsModule* other_motion = get_motion(other);
        if (heading_dot > 0.0f && other_motion->path_max_speed_ >= motion->path_max_speed_)
            continue;
        double dx = static_cast<double>(entity_->at_point_.x) - other->at_point_.x;
        double dy = static_cast<double>(entity_->at_point_.y) - other->at_point_.y;
        double dz = static_cast<double>(entity_->at_point_.z) - other->at_point_.z;
        double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        auto distance_f = static_cast<float>(distance);
        if (!(distance <= range))
            continue;
        double other_length =
            std::sqrt((static_cast<double>(other_at.x) * other_at.x + static_cast<double>(other_at.y) * other_at.y) +
                      static_cast<double>(other_at.z) * other_at.z);
        double my_length = std::sqrt((static_cast<double>(at.x) * at.x + static_cast<double>(at.y) * at.y) +
                                     static_cast<double>(at.z) * at.z);
        float cosine = maths::confine(static_cast<float>(heading_dot / (other_length * my_length)), -1.0f, 1.0f);
        bool above;
        float angle = maths::acos_f(cosine, kFullTurn, &above);
        if (above)
            angle = static_cast<float>(
                angle - static_cast<double>(truncate(static_cast<double>(angle) * kInverseFullTurn)) * kFullTurn);
        auto off_head_on = static_cast<float>(std::fabs(static_cast<double>(angle) - kHalfTurn));
        if (script::fetch_float(kHeadOnRange) > off_head_on) {
            // Head on: to the side the other is on.
            angle = kQuarterTurn;
            if (maths::cross_vec(at, other_at).y < 0.0f)
                angle = -kQuarterTurn;
        }
        avoided = true;
        add_avoidance(angle, bias, distance_f, range);
    }
    return avoided;
}

// 0x004EEAC0
void PedestrianBrainModule::keep_to_path()
{
    const maths::Vec4& from = node_pos_;
    float ax = from.x, az = from.z, aw = from.w;
    float bx = next_node_pos_.x, bz = next_node_pos_.z, bw = next_node_pos_.w;
    const maths::Vec4& at = entity_->at_point_;
    float px = at.x, pz = at.z, pw = at.w;
    float max_distance = script::fetch_float(kMaxDistanceToPath);
    auto dx = static_cast<float>(static_cast<double>(bx) - ax);
    double dz = static_cast<double>(bz) - az;
    auto dw = static_cast<float>(static_cast<double>(bw) - aw);
    double ex = static_cast<double>(px) - ax, ez = static_cast<double>(pz) - az;
    auto cross_x = static_cast<float>(0.0f * ez - dz * 0.0f);
    auto cross_y = static_cast<float>(dz * ex - ez * dx);
    auto cross_z = static_cast<float>(static_cast<double>(dx) * 0.0f - ex * 0.0f);
    double length_sqr = static_cast<double>(dx) * dx + dz * dz;
    // The closest point of the path's line (its start for a point path).
    maths::Vec4 closest = from;
    if (length_sqr != 0.0) {
        double t = (ex * dx + ez * dz) / length_sqr;
        double along_x = dx * t;
        auto along_y = static_cast<float>(t * 0.0f), along_z = static_cast<float>(dz * t),
             along_w = static_cast<float>(dw * t);
        closest = {static_cast<float>(along_x + from.x), static_cast<float>(static_cast<double>(along_y) + from.y),
                   static_cast<float>(static_cast<double>(along_z) + from.z),
                   static_cast<float>(static_cast<double>(along_w) + from.w)};
    }
    maths::Vec4 to_path = {static_cast<float>(static_cast<double>(closest.x) - px), 0.0f,
                           static_cast<float>(static_cast<double>(closest.z) - pz),
                           static_cast<float>(static_cast<double>(closest.w) - pw)};
    maths::Mat4 m = entity_->display_matrix_;
    double exact = maths::read_turn_angle(m.row[2], to_path);
    auto angle = static_cast<float>(exact);
    if (exact > kDegrees)
        angle = static_cast<float>(angle - static_cast<double>(truncate(static_cast<double>(angle) * kInverseDegrees)) *
                                               kDegrees);
    if (static_cast<double>(m.row[2].z) * to_path.x - static_cast<double>(m.row[2].x) * to_path.z > 0.0)
        angle = static_cast<float>(static_cast<double>(kDegrees) - angle);
    double sx = static_cast<double>(ax) - bx, sz = static_cast<double>(az) - bz;
    double segment = std::sqrt(sx * sx + sz * sz);
    float off;
    if (segment == 0.0)
        off = 0.0f;
    else
        off = static_cast<float>(
            std::sqrt((static_cast<double>(cross_x) * cross_x + static_cast<double>(cross_z) * cross_z) +
                      static_cast<double>(cross_y) * cross_y) /
            segment);
    float weight;
    if (off > max_distance)
        weight = 1.0f;
    else
        weight = static_cast<float>(1.0 - (static_cast<double>(max_distance) - off) / max_distance);
    if (script::fetch_float(kMinDistanceToPath) < off)
        add_steer(static_cast<float>(static_cast<double>(angle) * kToRadians), weight);
}

// 0x004EEDC0
void PedestrianBrainModule::move_steered()
{
    KinematicsModule* motion = get_motion(entity_);
    float side = steer_sideways();
    double forward = steer_forward();
    push_position();
    double step = static_cast<double>(step_seconds_) * motion->path_speed_;
    auto side_step = static_cast<float>(side * step);
    auto forward_step = static_cast<float>(step * forward);
    maths::Mat4 m = entity_->display_matrix_;
    double fx = static_cast<double>(m.row[2].x) * forward_step, fy = static_cast<double>(m.row[2].y) * forward_step;
    auto fz = static_cast<float>(static_cast<double>(m.row[2].z) * forward_step);
    auto fw = static_cast<float>(static_cast<double>(m.row[2].w) * forward_step);
    double sx = static_cast<double>(m.row[0].x) * side_step, sy = static_cast<double>(m.row[0].y) * side_step;
    auto sz = static_cast<float>(static_cast<double>(m.row[0].z) * side_step);
    auto sw = static_cast<float>(static_cast<double>(m.row[0].w) * side_step);
    maths::Vec4& pos = entity_->at_point_;
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
    maths::Mat4 m = entity_->display_matrix_;
    const maths::Vec4& at = entity_->at_point_;
    maths::Vec4 away = {static_cast<float>(static_cast<double>(at.x) - target.x), 0.0f,
                        static_cast<float>(static_cast<double>(at.z) - target.z),
                        static_cast<float>(static_cast<double>(at.w) - target.w)};
    double angle = maths::read_turn_angle(m.row[2], away) + kHalfDegrees;
    if (angle > kDegrees)
        angle -= static_cast<double>(truncate(angle * kInverseDegrees)) * kDegrees;
    if (static_cast<double>(m.row[2].z) * away.x - static_cast<double>(m.row[2].x) * away.z > 0.0)
        angle = static_cast<double>(kDegrees) - angle;
    add_steer(static_cast<float>(angle * kToRadians), weight);
}

// 0x004F2780
bool PedestrianBrainModule::turn_towards_node_after()
{
    const maths::Vec4& at = entity_->at_point_;
    double dz = static_cast<double>(at.z) - next_node_pos_.z;
    double dx = static_cast<double>(at.x) - next_node_pos_.x;
    auto distance_sqr = static_cast<float>(dx * dx + dz * dz);
    bool turning = false;
    float bias = script::fetch_float(kTargetNodeBias);
    for (float& avoid : avoid_)
        avoid = 0.0f;
    if (node_after_ == list_link_) {
        // A dead end: reached when near enough.
        double dead_end = script::fetch_float(kDeadEndDistance);
        if (static_cast<double>(distance_sqr) <= dead_end)
            reach_waypoint();
    } else {
        int fade_distance = script::fetch_int_value(kind_ == 2 ? kSkaterFadeDistance : kFadeDistance);
        if (turn_frames_ != 0 && turn_frame_ >= turn_frames_) {
            reach_waypoint();
        } else if (node_after_ && static_cast<double>(fade_distance) > distance_sqr) {
            // Near the next node: turning towards the one after over a number of frames by the corner's angle.
            if (turn_frames_ == 0) {
                KinematicsModule* motion = get_motion(entity_);
                maths::Vec4 from = {node_pos_.x, 0.0f, node_pos_.z, node_pos_.w};
                maths::Vec4 next = {next_node_pos_.x, 0.0f, next_node_pos_.z, next_node_pos_.w};
                maths::Vec4 after = {node_after_pos_.x, 0.0f, node_after_pos_.z, node_after_pos_.w};
                maths::Vec4 in = next - from;
                maths::Vec4 out = after - next;
                double corner = maths::read_turn_angle(in, out) * kToRadians;
                double reach =
                    std::sqrt(std::sqrt((std::cos(corner) + 1.0) * (static_cast<double>(distance_sqr) + distance_sqr)));
                turn_frames_ = truncate(reach / motion->path_max_speed_ * kFramesPerSecond);
                if (turn_frames_ <= 0)
                    turn_frames_ = 1;
            }
            int frames = turn_frames_;
            turning = true;
            double fraction = frames == 0 ? 0.0 : (1.0 - static_cast<double>(frames - turn_frame_) / frames) * 0.5f;
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
    maths::Vec4 pos = entity_->at_point_;
    maths::Mat4 m;
    if (kind_ == 0) {
        // A walker turns its orientation towards where it moved, at most so much a frame.
        m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
        m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
        m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        maths::Vec4 heading = entity_->display_matrix_.row[2];
        heading.make_unit();
        maths::Vec4 moved = {static_cast<float>(static_cast<double>(pos.x) - old_pos.x), 0.0f,
                             static_cast<float>(static_cast<double>(pos.z) - old_pos.z), 1.0f};
        moved.make_unit();
        auto dot =
            static_cast<float>((static_cast<double>(moved.z) * heading.z + static_cast<double>(moved.y) * heading.y) +
                               static_cast<double>(moved.x) * heading.x);
        if (dot > kMaxCos)
            dot = kMaxCos;
        else if (dot < kMinCos)
            dot = kMinCos;
        bool unused;
        float turn = std::fabs(maths::acos_f(dot, 0.0f, &unused));
        if (!(static_cast<double>(step_seconds_) * kTurnPerSecond >= turn)) {
            static float g_max_turn = 0.69813174f; // (40 degrees)
            float t = kTurnTime;
            if (turn > g_max_turn)
                t = static_cast<float>(static_cast<double>(turn) / g_max_turn * kTurnTime);
            m.row[2] = *walk::get_lerp(&pos, &heading, &moved, t);
            m.row[2].make_unit();
        } else {
            m.row[2] = moved;
        }
        m.square_up_axes(2);
        entity_->basis_frame_ = m;
    } else {
        // A skater faces where it moved (or along its path, to a lip).
        m = entity_->display_matrix_;
        float old_w = old_pos.w;
        maths::Vec4 to = pos;
        if (flag_bits_ & kPedFlagVertLip) {
            old_pos = {node_pos_.x, node_pos_.y, node_pos_.z, old_pos.w};
            old_w = node_pos_.w;
            to = next_node_pos_;
        }
        maths::Vec4 heading = {static_cast<float>(static_cast<double>(to.x) - old_pos.x),
                               static_cast<float>(static_cast<double>(to.y) - old_pos.y),
                               static_cast<float>(static_cast<double>(to.z) - old_pos.z),
                               static_cast<float>(static_cast<double>(to.w) - old_w)};
        m.row[2] = heading.make_unit();
        m.row[0].w = 0.0f;
        m.row[1].w = 0.0f;
        rebuild_across_and_up(m);
    }
    entity_->display_matrix_ = m;
    saved_matrix_ = m;
}

// 0x004EF740
bool PedestrianBrainModule::probe_ground(const maths::Vec4* start, const maths::Vec4* end, maths::Vec4* point,
                                         GroundTriangle* triangle)
{
    Feeler feeler;
    render::ProbeRay* f = feeler.feeler();
    if (!f->get_collision_along(start, end, false))
        return false;
    render::HitMesh* object = f->hit_.object;
    render::TriMeshSource* geometry = object->geometry_;
    uint32_t face = f->hit_.face;
    for (int i = 0; i < 3; ++i) {
        uint32_t vertex = geometry->small_faces_
                              ? geometry->faces_[face * 8 + 4 + i]
                              : *reinterpret_cast<uint16_t*>(geometry->faces_ + face * 10 + 4 + 2 * i);
        triangle->corners[i] = object->read_vertex_point(static_cast<int>(static_cast<uint16_t>(vertex)));
    }
    *point = f->contact_pos_;
    set_sound_terrain(f->hit_.terrain);
    return true;
}

// 0x004EF930
void PedestrianBrainModule::stick_to_ground_ped()
{
    KinematicsModule* motion = get_motion(entity_);
    // The orientation's across row again (up x at).
    maths::Mat4& o = entity_->basis_frame_;
    double ax = static_cast<double>(o.row[1].y) * o.row[2].z - static_cast<double>(o.row[1].z) * o.row[2].y;
    double ay = static_cast<double>(o.row[1].z) * o.row[2].x - static_cast<double>(o.row[1].x) * o.row[2].z;
    double az = static_cast<double>(o.row[1].x) * o.row[2].y - static_cast<double>(o.row[1].y) * o.row[2].x;
    o.row[0].w = 0.0f;
    o.row[0].z = static_cast<float>(az);
    o.row[0].x = static_cast<float>(ax);
    o.row[0].y = static_cast<float>(ay);
    o.row[0].make_unit();
    // A probe from above to below along the display matrix's up.
    maths::Mat4 m = entity_->display_matrix_;
    const maths::Vec4& up = m.row[1];
    const maths::Vec4& pos = entity_->at_point_;
    float above = stick_above_;
    auto ux = static_cast<float>(static_cast<double>(up.x) * above),
         uy = static_cast<float>(static_cast<double>(up.y) * above),
         uz = static_cast<float>(static_cast<double>(up.z) * above);
    double uw = static_cast<double>(above) * up.w;
    maths::Vec4 start = {static_cast<float>(static_cast<double>(ux) + pos.x),
                         static_cast<float>(static_cast<double>(uy) + pos.y),
                         static_cast<float>(static_cast<double>(uz) + pos.z), static_cast<float>(uw + pos.w)};
    float below = stick_below_;
    float nz = -up.z, nw = -up.w;
    auto dx = static_cast<float>(-static_cast<double>(up.x) * below),
         dy = static_cast<float>(-static_cast<double>(up.y) * below);
    double dz = static_cast<double>(nz) * below;
    auto dw = static_cast<float>(static_cast<double>(nw) * below);
    maths::Vec4 end = {static_cast<float>(static_cast<double>(dx) + pos.x),
                       static_cast<float>(static_cast<double>(dy) + pos.y), static_cast<float>(dz + pos.z),
                       static_cast<float>(static_cast<double>(dw) + pos.w)};
    maths::Vec4 point;
    if (probe_ground(&start, &end, &point, reinterpret_cast<GroundTriangle*>(&motion->ground_triangle_[0])))
        entity_->at_point_ = point;
}

// 0x004EF090
void PedestrianBrainModule::align_to_ground()
{
    KinematicsModule* motion = get_motion(entity_);
    if (!motion)
        return;
    GroundTriangle t;
    t.get_assign(*reinterpret_cast<GroundTriangle*>(&motion->ground_triangle_[0]));
    const maths::Vec4* c = t.corners;
    double e1x = static_cast<double>(c[1].x) - c[0].x, e1y = static_cast<double>(c[1].y) - c[0].y,
           e1z = static_cast<double>(c[1].z) - c[0].z;
    auto e2x = static_cast<float>(static_cast<double>(c[2].x) - c[0].x);
    auto e2y = static_cast<float>(static_cast<double>(c[2].y) - c[0].y);
    double e2z = static_cast<double>(c[2].z) - c[0].z;
    maths::Vec4 normal = {static_cast<float>(e2z * e1y - static_cast<double>(e2y) * e1z),
                          static_cast<float>(e1z * e2x - e2z * e1x),
                          static_cast<float>(static_cast<double>(e2y) * e1x - e1y * e2x), 0.0f};
    maths::Vec4 up = normal.make_unit();
    if (up.y < 0.0f)
        up = {-up.x, -up.y, -up.z, -up.w};
    if (up != up_target_) {
        up_target_ = up;
        up_dir_ = up_shown_;
        up_blend_ = 1.0f;
    }
    if (!(up_blend_ == 0.0f)) {
        if (up_dir_ == up_target_) {
            up_blend_ = 0.0f;
        } else {
            double blend = up_blend_ - static_cast<double>(step_seconds_) * kUpBlendRate;
            auto blend_f = static_cast<float>(blend);
            up_blend_ = blend_f;
            if (blend > 0.0 || std::isnan(blend)) {
                maths::Vec4 between;
                up_shown_ = *walk::get_lerp(&between, &up_target_, &up_dir_, blend_f);
                up_shown_.make_unit();
            } else {
                up_blend_ = 0.0f;
                up_shown_ = up_target_;
            }
        }
    }
    maths::Mat4 m = entity_->display_matrix_;
    if (m.row[1] != up_shown_) {
        m.row[1] = up_shown_;
        m.square_up_axes(1);
        entity_->display_matrix_ = m;
        saved_matrix_ = m;
    }
}

// 0x004F01C0
void PedestrianBrainModule::spin_update()
{
    auto elapsed = static_cast<float>(static_cast<double>(step_seconds_) + spin_elapsed_);
    spin_elapsed_ = elapsed;
    double fraction = static_cast<double>(step_seconds_) / spin_time_;
    if (fraction > 1.0)
        fraction = 1.0;
    auto turn = static_cast<float>(static_cast<double>(kHalfDegrees) - vert_rotation_target_);
    uint32_t flags = flag_bits_;
    if (flags & kPedFlagSpinning)
        twist_rate_ = static_cast<float>(static_cast<double>(spin_angle_) * fraction + twist_rate_);
    if (flags & kPedFlagVertRotating)
        vert_rotation_ = static_cast<float>(static_cast<double>(turn) * fraction + vert_rotation_);
    if (elapsed >= spin_time_ || fraction >= 1.0) {
        if (flags & kPedFlagSpinning)
            twist_rate_ = static_cast<float>(spin_angle_);
        if (flags & kPedFlagVertRotating)
            vert_rotation_ = turn;
        flag_bits_ = flags & ~(kPedFlagSpinning | kPedFlagVertRotating);
    }
    if ((flag_bits_ & kPedFlagVertRotating) && vert_rotation_ > turn)
        vert_rotation_ = turn;
    maths::Mat4 m = jump_matrix_;
    maths::Vec4 rotation = {static_cast<float>(static_cast<double>(vert_rotation_) * kToRadians),
                            static_cast<float>(static_cast<double>(twist_rate_) * kToRadians), 0.0f, 1.0f};
    m.turn_in_own_frame(rotation);
    entity_->display_matrix_ = m;
    saved_matrix_ = m;
    up_target_ = m.row[1];
    up_dir_ = up_target_;
    up_shown_ = up_dir_;
}

// 0x004F03C0
int PedestrianBrainModule::probe_ahead(maths::Vec4 direction, float range, maths::Vec4* out)
{
    auto range_sqr = static_cast<float>(static_cast<double>(range) * range);
    maths::Mat4 m = entity_->display_matrix_;
    maths::Vec4 dir = direction;
    dir.flatten_onto_plane(m.row[1]);
    double length = std::sqrt((static_cast<double>(dir.z) * dir.z + static_cast<double>(dir.y) * dir.y) +
                              static_cast<double>(dir.x) * dir.x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        dir.x = static_cast<float>(dir.x * inverse);
        dir.y = static_cast<float>(dir.y * inverse);
        dir.z = static_cast<float>(dir.z * inverse);
    }
    // A ped close ahead?
    ObjectSearch search;
    ObjectHubBase* manager = entity_->park_owner_;
    memory::ListNode* list = manager->fetch_object_set();
    for (auto* other = static_cast<GameEntity*>(search.first_item_of_type(list, kObjectTypePed)); other;
         other = static_cast<GameEntity*>(search.next_item_of_type())) {
        GameEntity* me = entity_;
        if (other == me)
            continue;
        auto dz = static_cast<float>(static_cast<double>(other->at_point_.z) - me->at_point_.z);
        auto dy = static_cast<float>(static_cast<double>(other->at_point_.y) - me->at_point_.y);
        double dx = static_cast<double>(other->at_point_.x) - me->at_point_.x;
        double distance_sqr = (dx * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz;
        if (!(distance_sqr < range_sqr))
            continue;
        double distance = std::sqrt(distance_sqr);
        auto distance_f = static_cast<float>(distance);
        double nx = dx, nz = dz;
        float ny = dy;
        if (distance > 0.0) {
            double inverse = 1.0 / distance_f;
            nx = dx * inverse;
            ny = static_cast<float>(static_cast<double>(dy) * inverse);
            nz = inverse * dz;
        }
        double dot = (static_cast<double>(dir.z) * nz + static_cast<double>(ny) * dir.y) + nx * dir.x;
        if (dot > kAhead) {
            if (out)
                *out = other->at_point_;
            return 3;
        }
    }
    // The world: a line from 48 up to 8 up `range` ahead.
    dir.x = static_cast<float>(static_cast<double>(dir.x) * range);
    dir.y = static_cast<float>(static_cast<double>(dir.y) * range);
    dir.z = static_cast<float>(static_cast<double>(dir.z) * range);
    dir.w = static_cast<float>(static_cast<double>(dir.w) * range);
    const maths::Vec4& at = entity_->at_point_;
    maths::Vec4 start = at;
    start.y = static_cast<float>(static_cast<double>(start.y) + kReach);
    auto top_y = static_cast<float>(static_cast<double>(dir.y) + at.y);
    maths::Vec4 end = {static_cast<float>(static_cast<double>(dir.x) + at.x),
                       static_cast<float>(static_cast<double>(top_y) + kProbeLift),
                       static_cast<float>(static_cast<double>(dir.z) + at.z),
                       static_cast<float>(static_cast<double>(dir.w) + at.w)};
    Feeler feeler(&start, &end);
    render::ProbeRay* f = feeler.feeler();
    f->set_excluded(0x10, 0);
    if (!f->get_hit_shape(true, false))
        return 0;
    if (out)
        *out = f->contact_pos_;
    return 1;
}

// --- the move updates ---

// 0x004F2A10
void PedestrianBrainModule::generic_move_update()
{
    if (!(flag_bits_ & kPedFlagMoving))
        return;
    KinematicsModule* motion = get_motion(entity_);
    bool avoided = avoid_peds();
    if (!turn_towards_node_after() && !avoided)
        keep_to_path();
    maths::Vec4 old = entity_->at_point_;
    motion_path(motion)->update_path_speed();
    move_steered();
    if (old != entity_->at_point_)
        face_movement(old);
    decay_bias();
    motion->stick_to_ground();
}

// 0x004F2AD0
void PedestrianBrainModule::skater_move_update()
{
    KinematicsModule* motion = get_motion(entity_);
    motion_path(motion)->update_path_speed();
    auto step = static_cast<float>(static_cast<double>(step_seconds_) * motion->path_speed_);
    bool airborne = (motion->motion_flags_ & 0x100) != 0;
    if (flag_bits_ & kPedFlagGrinding) {
        if (GameEntity* skater = local_skater()) {
            const maths::Vec4& at = entity_->at_point_;
            double dx = static_cast<double>(at.x) - skater->at_point_.x,
                   dy = static_cast<double>(at.y) - skater->at_point_.y,
                   dz = static_cast<double>(at.z) - skater->at_point_.z;
            auto distance_sqr = static_cast<float>((dz * dz + dy * dy) + dx * dx);
            if (script::fetch_float(kMinDistanceToSkater) > distance_sqr) {
                bail_from_grind();
                return;
            }
        }
    }
    if ((flag_bits_ & kPedFlagMoving) || airborne) {
        // Reaching the next waypoint (in the air, or for a vert node, in 3D).
        const maths::Vec4& at = entity_->at_point_;
        double dx, dy, dz;
        if ((flag_bits_ & kPedFlagVertNext) || airborne) {
            dx = static_cast<double>(at.x) - next_node_pos_.x;
            dy = static_cast<float>(static_cast<double>(at.y) - next_node_pos_.y);
            dz = static_cast<float>(static_cast<double>(at.z) - next_node_pos_.z);
        } else {
            dx = static_cast<double>(at.x) - next_node_pos_.x;
            dy = 0.0f;
            dz = static_cast<float>(static_cast<double>(at.z) - next_node_pos_.z);
        }
        auto distance_sqr = static_cast<float>((dx * dx + dz * dz) + dy * dy);
        int min_distance = script::fetch_int_value(kMinDistanceToWaypoint);
        if (!(flag_bits_ & kPedFlagJumpingToNode) &&
            (static_cast<double>(step) * step > distance_sqr || static_cast<double>(min_distance) > distance_sqr))
            reach_waypoint();
        if (flag_bits_ & kPedFlagCrouch) {
            if (static_cast<double>(script::fetch_int_value(kCrouchDistance)) > distance_sqr) {
                flag_bits_ &= ~kPedFlagCrouch;
                change_phase(kStateJumpCrouch, kSkaterPedStateTable, nullptr);
            }
        }
    }
    int sound = -1;
    if ((flag_bits_ & kPedFlagMoving) && step != 0.0f) {
        maths::Vec4 old = entity_->at_point_;
        bool faced = false;
        if (kind_ == 2 &&
            !(flag_bits_ & (kPedFlagGrinding | kPedFlagVertNext | kPedFlagVertLast | kPedFlagGrindBailing)) &&
            !(flag_bits_ & kPedFlagJumpingToNode)) {
            if (!turn_towards_node_after())
                keep_to_path();
            move_steered();
            if (airborne)
                entity_->display_matrix_ = saved_matrix_;
        } else {
            // Straight at the next node.
            maths::Vec4 to = {next_node_pos_.x - old.x, next_node_pos_.y - old.y, next_node_pos_.z - old.z,
                              next_node_pos_.w - old.w};
            const maths::Vec4& dir = to.make_unit();
            auto z = static_cast<float>(static_cast<double>(step) * dir.z),
                 w = static_cast<float>(static_cast<double>(step) * dir.w);
            maths::Vec4 moved = {static_cast<float>(static_cast<double>(step) * dir.x + old.x),
                                 static_cast<float>(static_cast<double>(step) * dir.y + old.y),
                                 static_cast<float>(static_cast<double>(z) + old.z),
                                 static_cast<float>(static_cast<double>(w) + old.w)};
            keep_to_path();
            move_steered();
            entity_->at_point_ = moved;
            face_movement(old);
            faced = true;
        }
        if (!(flag_bits_ & (kPedFlagGrinding | kPedFlagGrindBailing))) {
            if (airborne) {
                sound = 1;
            } else {
                stick_to_ground_ped();
                if (!faced)
                    face_movement(old);
                align_to_ground();
            }
        } else if (airborne) {
            sound = 1;
        }
    } else {
        push_position();
        entity_->display_matrix_ = saved_matrix_;
        if (flag_bits_ & (kPedFlagSpinning | kPedFlagVertRotating))
            spin_update();
        if (airborne)
            sound = 1;
    }
    if (sound < 0) {
        if (flag_bits_ & kPedFlagGrinding)
            sound = 4;
        else if (flag_bits_ & kPedFlagMoving)
            sound = 0;
        else if (flag_bits_ & kPedFlagSoundState3)
            sound = 3;
    }
    if (sound >= 0)
        set_sound_state(sound);
    if (flag_bits_ & kPedFlagMoving)
        speed_ratio_ = motion->path_max_speed_ == 0.0f
                           ? 0.0f
                           : static_cast<float>(static_cast<double>(motion->path_speed_) / motion->path_max_speed_);
    else
        speed_ratio_ = 0.0f;
    if (auto* looping = static_cast<RiderRollingAudio*>(entity_->fetch_module(kSkaterLoopingSound))) {
        looping->no_rail_sound_ = (flag_bits_ & (kPedFlagBailing | kPedFlagGrindBailing)) != 0;
        looping->set_pace_ratio(speed_ratio_);
    }
}

// 0x004F1890
void PedestrianBrainModule::bail_move_update()
{
    KinematicsModule* motion = get_motion(entity_);
    motion_path(motion)->update_path_speed();
    auto step = static_cast<float>(static_cast<double>(step_seconds_) * motion->path_speed_);
    if (!(flag_bits_ & (kPedFlagBailing | kPedFlagGrindBailing))) {
        // The bail's over: back to skating.
        update_type_ = kSkaterMoveUpdate;
        script::launch_script(kPedInitializeMovement, nullptr, entity_, false, nullptr);
        if (update_type_ == kGenericMoveUpdate || update_type_ == kSkaterMoveUpdate)
            flag_bits_ |= kPedFlagMoving;
        return;
    }
    if (flag_bits_ & kPedFlagMoving) {
        const maths::Vec4& at = entity_->at_point_;
        auto dx = static_cast<float>(static_cast<double>(at.x) - next_node_pos_.x);
        double dz = static_cast<double>(at.z) - next_node_pos_.z;
        auto distance_sqr = static_cast<float>(dz * dz + static_cast<double>(dx) * dx);
        int min_distance = script::fetch_int_value(kMinDistanceToWaypoint);
        if (!(flag_bits_ & kPedFlagJumpingToNode) &&
            (static_cast<double>(step) * step > distance_sqr || static_cast<double>(min_distance) > distance_sqr))
            flag_bits_ &= ~kPedFlagMoving;
    }
    if ((flag_bits_ & kPedFlagMoving) && !(step == 0.0f)) {
        maths::Vec4 old = entity_->at_point_;
        maths::Vec4 to = {next_node_pos_.x - old.x, next_node_pos_.y - old.y, next_node_pos_.z - old.z,
                          next_node_pos_.w - old.w};
        const maths::Vec4& dir = to.make_unit();
        auto z = static_cast<float>(static_cast<double>(step) * dir.z),
             w = static_cast<float>(static_cast<double>(step) * dir.w);
        maths::Vec4 moved = {static_cast<float>(static_cast<double>(step) * dir.x + old.x),
                             static_cast<float>(static_cast<double>(step) * dir.y + old.y),
                             static_cast<float>(static_cast<double>(z) + old.z),
                             static_cast<float>(static_cast<double>(w) + old.w)};
        keep_to_path();
        move_steered();
        entity_->at_point_ = moved;
    }
    entity_->display_matrix_ = saved_matrix_;
}

// 0x004F1780
void PedestrianBrainModule::go_home_update()
{
    maths::Vec4 home = {node_pos_.x - entity_->at_point_.x, node_pos_.y - entity_->at_point_.y,
                        node_pos_.z - entity_->at_point_.z, node_pos_.w - entity_->at_point_.w};
    double distance_sqr = (static_cast<double>(home.z) * home.z + static_cast<double>(home.y) * home.y) +
                          static_cast<double>(home.x) * home.x;
    if (distance_sqr <= kNear) {
        script::launch_script(kEndReturnHome, nullptr, entity_, false, nullptr);
        return;
    }
    home.make_unit();
    KinematicsModule* motion = get_motion(entity_);
    push_position();
    maths::Vec4& pos = entity_->at_point_;
    maths::Vec4 old = pos;
    double step = static_cast<double>(step_seconds_) * motion->path_max_speed_;
    auto step_f = static_cast<float>(step);
    pos.w = 1.0f;
    pos.x = static_cast<float>(step * home.x + old.x);
    pos.z = static_cast<float>(static_cast<double>(step_f) * home.z + old.z);
    face_movement(old);
    motion->stick_to_ground();
}

// 0x004F1480
void PedestrianBrainModule::chase_update()
{
    GameEntity* skater = local_skater();
    if (!skater)
        return;
    KinematicsModule* motion = get_motion(entity_);
    auto dz = static_cast<float>(static_cast<double>(skater->at_point_.z) - entity_->at_point_.z);
    maths::Vec4 toward_player = {static_cast<float>(static_cast<double>(skater->at_point_.x) - entity_->at_point_.x),
                                 0.0f, dz, 0.0f};
    double distance_sqr = static_cast<double>(toward_player.x) * toward_player.x + static_cast<double>(dz) * dz;
    if (distance_sqr <= kCatchDistanceSqr) {
        // Caught.
        face_along(&toward_player);
        script::Struct* params = new_params();
        params->put_name_key(kChaser, skater->ident_);
        change_phase(kStateChaseCatch, 0, params);
        delete_params(params);
        return;
    }
    if (distance_sqr > kChaseOutOfRangeSqr) {
        script::launch_script(kEndPlayerChase, nullptr, entity_, false, nullptr);
        return;
    }
    int max_home = 0;
    if (entity_->read_int_tag(kMaxHomeDist, &max_home) && max_home > 0) {
        maths::Vec4 from_home = node_pos_ - entity_->at_point_;
        double length_sqr =
            (static_cast<double>(from_home.x) * from_home.x + static_cast<double>(from_home.y) * from_home.y) +
            static_cast<double>(from_home.z) * from_home.z;
        if (static_cast<double>(max_home * max_home) <= length_sqr) {
            script::launch_script(kEndPlayerChase, nullptr, entity_, false, nullptr);
            return;
        }
    }
    if (probe_ahead({toward_player.x, 0.0f, dz, 0.0f}, kReach, nullptr)) {
        if (cur_phase_ != kStateChasePause)
            change_phase(kStateChasePause, 0, nullptr);
        return;
    }
    if (cur_phase_ != kStateChase)
        change_phase(kStateChase, 0, nullptr);
    motion->path_max_speed_ = kChaseSpeed;
    toward_player.make_unit();
    push_position();
    maths::Vec4& pos = entity_->at_point_;
    maths::Vec4 old = pos;
    double step = static_cast<double>(step_seconds_) * motion->path_max_speed_;
    auto step_f = static_cast<float>(step);
    pos.w = 1.0f;
    pos.x = static_cast<float>(step * toward_player.x + old.x);
    pos.z = static_cast<float>(static_cast<double>(step_f) * toward_player.z + old.z);
    face_movement(old);
    motion->stick_to_ground();
}

// 0x004F0DD0
void PedestrianBrainModule::blocker_update()
{
    GameEntity* skater = local_skater();
    if (!skater)
        return;
    KinematicsModule* motion = get_motion(entity_);
    auto sz = static_cast<float>(static_cast<double>(skater->at_point_.z) - entity_->at_point_.z);
    maths::Vec4 toward_player = {static_cast<float>(static_cast<double>(skater->at_point_.x) - entity_->at_point_.x),
                                 0.0f, sz, 0.0f};
    auto path_z = static_cast<float>(static_cast<double>(next_node_pos_.z) - node_pos_.z);
    auto path_x = static_cast<float>(static_cast<double>(next_node_pos_.x) - node_pos_.x);
    // Across the path, on the skater's side.
    double side = static_cast<double>(path_x) * sz - static_cast<double>(path_z) * toward_player.x;
    double turn = side < 0.0 ? kBlockerTurn : -kBlockerTurn;
    auto sine = static_cast<float>(std::sin(turn));
    auto cosine = static_cast<float>(std::cos(turn));
    double across_x = std::cos(turn) * path_x + static_cast<double>(path_z) * sine;
    double across_z = -(static_cast<double>(path_x) * sine) + static_cast<double>(cosine) * path_z;
    maths::Vec4 across = {static_cast<float>(across_x), 0.0f, static_cast<float>(across_z), 0.0f};
    across.make_unit();
    if (static_cast<double>(toward_player.x) * toward_player.x + static_cast<double>(sz) * sz <= kCatchDistanceSqr) {
        // Bumped.
        face_along(&across);
        script::Struct* params = new_params();
        params->put_vec3(kVelDir, script::VectorValue{across.x, across.y, across.z, across.w});
        entity_->post_own_event(kBlockerBumpPlayer, params);
        delete_params(params);
        return;
    }
    // Where the skater's going crosses the path.
    const maths::Vec4& vel = skater->vel_;
    bool not_approaching = !((static_cast<double>(across.z) * vel.z + static_cast<double>(across.y) * vel.y) +
                                 static_cast<double>(across.x) * vel.x >
                             0.0);
    double speed_sqr =
        (static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) + static_cast<double>(vel.z) * vel.z;
    const maths::Vec4& at = skater->at_point_;
    maths::Vec4 end;
    maths::Vec4 on_path;
    float s, t;
    if (!(speed_sqr < kFastSkaterSqr) && not_approaching) {
        auto ex = static_cast<float>(static_cast<double>(vel.x) * kLookAhead),
             ey = static_cast<float>(static_cast<double>(vel.y) * kLookAhead),
             ez = static_cast<float>(static_cast<double>(vel.z) * kLookAhead);
        double ew = static_cast<double>(vel.w) * kLookAhead;
        end = {static_cast<float>(static_cast<double>(ex) + at.x), static_cast<float>(static_cast<double>(ey) + at.y),
               static_cast<float>(static_cast<double>(ez) + at.z), static_cast<float>(ew + at.w)};
    } else {
        auto ex = static_cast<float>(static_cast<double>(across.x) * -kLookAhead),
             ey = static_cast<float>(static_cast<double>(across.y) * -kLookAhead),
             ez = static_cast<float>(static_cast<double>(across.z) * -kLookAhead);
        double ew = static_cast<double>(across.w) * -kLookAhead;
        end = {static_cast<float>(static_cast<double>(ex) + at.x), static_cast<float>(static_cast<double>(ey) + at.y),
               static_cast<float>(static_cast<double>(ez) + at.z), static_cast<float>(ew + at.w)};
    }
    maths::Seg path(next_node_pos_, node_pos_);
    maths::Seg skater_line(at, end);
    maths::closest_points_between_lines(path, skater_line, on_path, end, s, t, true);
    double speed = std::sqrt((static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                             static_cast<double>(vel.z) * vel.z);
    auto gz = static_cast<float>(static_cast<double>(on_path.z) - at.z);
    auto gy = static_cast<float>(static_cast<double>(on_path.y) - at.y);
    auto gx = static_cast<float>(static_cast<double>(on_path.x) - at.x);
    if (speed == 0.0)
        speed = kNoSpeed;
    double skater_time =
        std::sqrt((static_cast<double>(gx) * gx + static_cast<double>(gy) * gy) + static_cast<double>(gz) * gz) / speed;
    const maths::Vec4& me = entity_->at_point_;
    double tx = static_cast<double>(on_path.x) - me.x, ty = static_cast<double>(on_path.y) - me.y,
           tz = static_cast<double>(on_path.z) - me.z, tw = static_cast<double>(on_path.w) - me.w;
    maths::Vec4 to_point = {static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz),
                            static_cast<float>(tw)};
    double distance = std::sqrt((tx * tx + ty * ty) + tz * tz);
    auto distance_f = static_cast<float>(distance);
    if (distance <= kStandDistance) {
        face_along(&toward_player);
        entity_->post_own_event(kBlockerStand, nullptr);
        return;
    }
    auto needed = static_cast<float>(static_cast<double>(distance_f) / skater_time);
    to_point.make_unit();
    if (!(needed > kRunRatio) && !(distance_f > kRunDistance)) {
        // Near enough: a shuffle to the side the point is on.
        face_along(&across);
        script::Struct* params = new_params();
        maths::Vec4 cross = maths::cross_vec(to_point, across);
        params->put_name_key(kDir, cross.y < 0.0f ? kLeft : kRight);
        entity_->post_own_event(kBlockerShuffle, params);
        delete_params(params);
    } else {
        face_along(&to_point);
        entity_->post_own_event(kBlockerRun, nullptr);
    }
    float speed_needed = needed > kMinBlockerSpeed ? needed : kMinBlockerSpeed;
    motion->path_max_speed_ = speed_needed;
    if (!(speed_needed < kMaxBlockerSpeed))
        speed_needed = kMaxBlockerSpeed;
    motion->path_max_speed_ = speed_needed;
    push_position();
    double step = static_cast<double>(step_seconds_) * motion->path_max_speed_;
    maths::Vec4& pos = entity_->at_point_;
    pos.w = 1.0f;
    pos.x = static_cast<float>(static_cast<double>(to_point.x) * step + pos.x);
    pos.z = static_cast<float>(static_cast<double>(to_point.z) * step + pos.z);
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
    if (aux_script_)
        aux_script_->tick_frame();
}

// --- script member functions ---

// 0x004F0950
bool PedestrianBrainModule::ped_turn_to_current_waypoint_orientation(script::Struct* params, script::Script*)
{
    KinematicsModule* motion = get_motion(entity_);
    maths::Mat4 m;
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Vec4 angles = {0.0f, 0.0f, 0.0f, 1.0f};
    level_nodes::read_turn_angles(list_link_, &angles);
    maths::Mat4 rotation;
    d3dx_multiply(m, m, maths::make_pitch_matrix(rotation, angles.x));
    d3dx_multiply(m, m, maths::make_yaw_matrix(rotation, angles.y));
    d3dx_multiply(m, m, maths::make_roll_matrix(rotation, angles.z));
    for (int i = 0; i < 4; ++i)
        (&m.row[2].x)[i] = static_cast<float>(static_cast<double>((&m.row[2].x)[i]) * kReach);
    const maths::Vec4& at = entity_->at_point_;
    maths::Vec4 target = {static_cast<float>(static_cast<double>(m.row[2].x) + at.x),
                          static_cast<float>(static_cast<double>(m.row[2].y) + at.y),
                          static_cast<float>(static_cast<double>(m.row[2].z) + at.z),
                          static_cast<float>(static_cast<double>(m.row[2].w) + at.w)};
    motion->get_look_at(params, &target);
    return true;
}

} // namespace objects
