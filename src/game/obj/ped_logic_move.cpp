// The PedLogic component (GameObjects::CPedestrianBrainModule): see ped_logic.h. Its movement - the steering (round other peds,
// towards the waypoints, back to the path), facing where it goes, the ground under it, the skaters' spins - and the
// move updates (generic, skater, bail, go home, chase, blocker); from the disassembly of 0x004ED5E0-0x004F2F08
// (0x004EF090, 0x004EF930, 0x004F0950 and 0x004F2AD0 aren't Ghidra functions). The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double; FSIN / FCOS results
// are used at their full precision where the original goes on computing with them.

#include <cmath>
#include <cstdint>

#include "game/skater/rider.h"
#include "game/gfx/d3dx.h"
#include "game/core/manager.h"
#include "game/maths/maths.h"
#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/maths/x87_math.h"
#include "game/engine3d/coll_obj.h"
#include "game/engine3d/coll_tri.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/entity_hub.h"
#include "game/obj/path_manager.h"
#include "game/obj/ped_logic.h"
#include "game/obj/walk_hang.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/skater/looping_sound.h"
#include "game/sys/stopwatch.h"
#include "game/obj/entity_hub.h"

namespace GameObjects {

namespace {

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;     // OBJECT_TYPE_PED
constexpr uint32_t kMinBias = 0x4ABA9798;           // ped_min_bias
constexpr uint32_t kBiasDecay = 0x98CD0C8C;         // (the bias's decay per frame)
constexpr uint32_t kAvoidPedRange = 0x7A78F471;     // ped_avoid_ped_range
constexpr uint32_t kPedDodgeBias = 0xD1E6177B;      // ped_avoid_ped_bias
constexpr uint32_t kMaxYDistance = 0x21B13FFC;      // ped_max_y_distance_to_ignore
constexpr uint32_t kHeadOnRange = 0xD2E22CEC;       // ped_head_on_range
constexpr uint32_t kMaxDistanceToPath = 0xA876AA2C; // ped_max_distance_to_path
constexpr uint32_t kMinDistanceToPath = 0x81085734; // ped_min_distance_to_path
constexpr uint32_t kTargetNodeBias = 0x57342672;    // ped_target_node_bias
constexpr uint32_t kDeadEndDistance = 0x65B46F32;   // ped_walker_min_square_distance_to_dead_end
constexpr uint32_t kSkaterFadeDistance = 0x354BEDA1; // ped_skater_fade_target_bias_max_distance_square
constexpr uint32_t kFadeDistance = 0x5B21AB0E;      // ped_fade_target_bias_max_distance_square
constexpr uint32_t kMinDistanceToSkater = 0xBC2E678A; // ped_skater_min_square_distance_to_skater
constexpr uint32_t kMinDistanceToWaypoint = 0xFB5D106F; // ped_skater_min_square_distance_to_waypoint
constexpr uint32_t kCrouchDistance = 0x52EC432F;    // ped_skater_min_square_distance_to_crouch_for_jump
constexpr uint32_t kStateJumpCrouch = 0xB32163E0;   // STATE_jumpcrouch
constexpr uint32_t kStateChase = 0x1B91406A, kStateChasePause = 0x0CC94B7C, kStateChaseCatch = 0x1E38A2F5;
constexpr uint32_t kSkaterPedStateTable = 0x46196541;
constexpr uint32_t kSkaterMoveUpdate = 0xA631E964, kGenericMoveUpdate = 0x5B4E8427, kNoMoveUpdate = 0x520A0B40,
                   kGoHomeUpdate = 0x4DF2CC6E, kChaseUpdate = 0x30A12962, kBlockerUpdate = 0x3884D585,
                   kLipTrickUpdate = 0xC51B9B62, kBailUpdate = 0xF229C933;
constexpr uint32_t kPedInitializeMovement = 0x1B7DE0FB; // ped_initialize_movement
constexpr uint32_t kEndReturnHome = 0x44EB4098;     // Ped_Utils_EndReturnHome
constexpr uint32_t kEndPlayerChase = 0xFF21C020;    // Ped_Utils_EndPlayerChase
constexpr uint32_t kMaxHomeDist = 0x4116F7FF;       // MaxHomeDist (a tag)
constexpr uint32_t kChaser = 0x96919C9C;            // (the chased skater's id, to STATE_chasecatch)
constexpr uint32_t kVelDir = 0xF68EA304;            // VelDir
constexpr uint32_t kBlockerBumpPlayer = 0xF75EC2CF, kBlockerStand = 0xEFF1B1AA, kBlockerShuffle = 0x3B5A86D7,
                   kBlockerRun = 0x9D45C77A;        // the blocker's events
constexpr uint32_t kDir = 0x455485EF, kLeft = 0x85981897, kRight = 0x4B358AEB; // dir: left / Right

constexpr float kQuarterTurn = 1.5707963705062866f; // 0x00645484
constexpr float kThreeQuarterTurns = 4.71238899230957f; // 0x0064B76C
constexpr float kFullTurn = 6.2831854820251465f;    // 0x0064545C
constexpr float kInverseFullTurn = 0.15915493667125702f; // 0x0064A044
constexpr float kHalfTurn = 3.1415927410125732f;    // 0x0064B730
constexpr float kDegrees = 360.0f;                  // 0x00645EA8
constexpr float kInverseDegrees = 1.0f / 360.0f;    // 0x0064B798
constexpr float kHalfDegrees = 180.0f;              // 0x0064B278
constexpr float kToRadians = 0.01745329238474369f;  // 0x00645464
constexpr float kFramesPerSecond = 60.0f;           // 0x00645EA0
constexpr float kMaxCos = 0.9998999834060669f;      // 0x0064B7F4
constexpr float kMinCos = -0.9998999834060669f;     // 0x0064B7EC
constexpr float kTurnPerSecond = 0.05235987901687622f; // 0x0064B7F0 (3 degrees)
constexpr float kTurnTime = 0.083f;                 // 0x0064B7E8
constexpr double kAhead = 0.866;                    // 0x0064B7F8 (a double: about cos 30 degrees)
constexpr float kReach = 48.0f;                     // 0x006461CC
constexpr float kProbeLift = 8.0f;                  // 0x006461D4
constexpr float kNear = 1000.0f;                    // 0x006453C0
constexpr float kCatchDistanceSqr = 3047.039794921875f; // 0x0067EFEC
constexpr float kChaseOutOfRangeSqr = 360000.0f;    // 0x0067EFF4
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

CKinematicsModule* Motion(CGameEntity* object) { return static_cast<CKinematicsModule*>(object->FetchModule(kMotion)); }
CMotionPathMembers* MotionPath(CKinematicsModule* motion) { return reinterpret_cast<CMotionPathMembers*>(motion); }

CGameEntity* LocalSkater()
{
    return static_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
}

// D3DXMatrixMultiply (linked library code).
void D3dxMultiply(Maths::Mat4& out, const Maths::Mat4& a, const Maths::Mat4& b)
{
    D3dx::MatrixMultiply(&out, &a, &b);
}

// _ftol2: a 64-bit truncation whose low half is used (the integer indefinite, 2^63 - a NaN, what doesn't fit - gives
// 0).
int Truncate(double value)
{
    if (!(std::fabs(value) < 9.2e18))
        return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// FCOS then FMUL / FADD on the 64-bit cosine (the product / sum rounded to 53 bits).
double CosTimes(double angle, double factor)
{
    double result;
    __asm {
        fld angle
        fcos
        fmul factor
        fstp result
    }
    return result;
}
double CosPlus(double angle, double addend)
{
    double result;
    __asm {
        fld angle
        fcos
        fadd addend
        fstp result
    }
    return result;
}

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// The collision feeler (0xB0 bytes): what it hit is held by a smart pointer at +0x98, let go when it goes.
class Feeler {
public:
    Feeler() { reinterpret_cast<Engine3D::CProbeRay*>(m_bytes)->MakeNew(); }
    Feeler(const Maths::Vec4* start, const Maths::Vec4* end) { reinterpret_cast<Engine3D::CProbeRay*>(m_bytes)->MakeNew(start, end); }
    ~Feeler() { feeler()->m_entity.~CSharedRef(); }
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    Engine3D::CProbeRay* feeler() { return reinterpret_cast<Engine3D::CProbeRay*>(m_bytes); }

private:
    alignas(16) uint8_t m_bytes[0xB0];
};

// The cross product's rows of a facing matrix: across = up x at (`a` kept unrounded), up again = at x across.
void RebuildAcrossAndUp(Maths::Mat4& m)
{
    const Maths::Vec4& up = m.row[1];
    const Maths::Vec4& at = m.row[2];
    double a = static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z;
    float b = static_cast<float>(static_cast<double>(at.x) * up.z - static_cast<double>(at.z) * up.x);
    float c = static_cast<float>(static_cast<double>(at.y) * up.x - static_cast<double>(at.x) * up.y);
    double upX = static_cast<double>(c) * at.y - static_cast<double>(b) * at.z;
    double upY = static_cast<double>(at.z) * a - static_cast<double>(c) * at.x;
    float upZ = static_cast<float>(static_cast<double>(b) * at.x - static_cast<double>(at.y) * a);
    m.row[0] = {static_cast<float>(a), b, c, m.row[0].w};
    m.row[1] = {static_cast<float>(upX), static_cast<float>(upY), upZ, m.row[1].w};
}

} // namespace

// --- steering ---

// 0x004ECFF0
void CPedestrianBrainModule::DecayBias()
{
    float decay = Scripts::FetchFloat(kBiasDecay);
    double threshold = Scripts::FetchFloat(kMinBias);
    for (float& bias : m_bias) {
        // The product is compared unrounded (x87); a NaN is kept.
        double value = static_cast<double>(decay) * bias;
        bias = static_cast<float>(value);
        if (value < threshold)
            bias = 0.0f;
    }
}

// 0x004ED5E0
void CPedestrianBrainModule::AddAvoidance(float angle, float strength, float distance, float range)
{
    double weight = (static_cast<double>(range) - distance) / range * strength;
    for (int i = 0; i < 4; ++i) {
        double offset = static_cast<double>(i) * kQuarterTurn - angle;
        double size = std::fabs(offset);
        if (!(size < kQuarterTurn) && !(size > kThreeQuarterTurns || std::isnan(size)))
            continue;
        double value = CosTimes(offset, weight) + m_bias[i];
        m_bias[i] = static_cast<float>(value);
        if (value > 1.0)
            m_bias[i] = 1.0f;
        else if (value < -1.0)
            m_bias[i] = -1.0f;
    }
}

// 0x004ED680
void CPedestrianBrainModule::AddSteer(float angle, float weight)
{
    for (int i = 0; i < 4; ++i) {
        double offset = static_cast<double>(i) * kQuarterTurn - angle;
        double size = std::fabs(offset);
        if (!(size < kQuarterTurn) && !(size > kThreeQuarterTurns || std::isnan(size)))
            continue;
        double value = CosTimes(offset, weight) + m_avoid[i];
        m_avoid[i] = static_cast<float>(value);
        if (value > 1.0)
            m_avoid[i] = 1.0f;
        else if (value < -1.0)
            m_avoid[i] = -1.0f;
    }
}

// 0x004ED710
float CPedestrianBrainModule::SteerSideways()
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        float quarter = static_cast<float>(static_cast<double>(i) * kQuarterTurn);
        double weight = static_cast<double>(m_bias[i]) + m_avoid[i];
        if (weight > 1.0)
            weight = 1.0;
        sum = sum - X87::SinTimes(quarter, weight);
        if (sum >= 1.0)
            return 1.0f;
        if (sum <= -1.0)
            return -1.0f;
    }
    float result = static_cast<float>(sum);
    if (Scripts::FetchFloat(kMinBias) > std::fabs(result))
        return 0.0f;
    return result;
}

// 0x004ED7D0
float CPedestrianBrainModule::SteerForward()
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        float quarter = static_cast<float>(static_cast<double>(i) * kQuarterTurn);
        double weight = static_cast<double>(m_bias[i]) + m_avoid[i];
        if (weight > 1.0)
            weight = 1.0;
        else if (weight < -1.0)
            weight = -1.0;
        sum = CosTimes(quarter, weight) + sum;
        if (sum >= 1.0)
            return 1.0f;
        if (sum <= -1.0)
            return -1.0f;
    }
    float result = static_cast<float>(sum);
    if (Scripts::FetchFloat(kMinBias) > std::fabs(result))
        return 0.0f;
    return result;
}

// 0x004EE650
void CPedestrianBrainModule::FaceAlong(const Maths::Vec4* direction)
{
    Maths::Mat4 m;
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {direction->x, 0.0f, direction->z, 1.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[2].MakeUnit();
    m.row[0].w = 0.0f;
    m.row[1].w = 0.0f;
    RebuildAcrossAndUp(m);
    m_entity->m_displayMatrix = m;
    m_entity->m_basisFrame = m;
    m_savedMatrix = m;
}

// 0x004EE7D0
bool CPedestrianBrainModule::AvoidPeds()
{
    bool avoided = false;
    if (!mp_peds)
        return false;
    CSharedRef<CEntity>* slot = mp_peds->m_slots;
    Maths::Mat4 m = m_entity->m_displayMatrix;
    const Maths::Vec4& at = m.row[2];
    float range = Scripts::FetchFloat(kAvoidPedRange);
    float bias = Scripts::FetchFloat(kPedDodgeBias);
    CKinematicsModule* motion = Motion(m_entity);
    int count = mp_peds->m_extent;
    if (count <= 0)
        return false;
    for (; count; --count, ++slot) {
        auto* other = reinterpret_cast<CGameEntity*>(slot->m_ownerObject);
        if (!other || other == m_entity)
            continue;
        double maxY = Scripts::FetchFloat(kMaxYDistance);
        CGameEntity* me = m_entity;
        if (std::fabs(static_cast<double>(me->m_atPoint.y) - other->m_atPoint.y) > maxY)
            continue;
        const Maths::Vec4& otherAt = other->m_displayMatrix.row[2];
        double dot = (static_cast<double>(at.y) * otherAt.y + static_cast<double>(at.z) * otherAt.z) +
                     static_cast<double>(at.x) * otherAt.x;
        float headingDot = static_cast<float>(dot);
        if (dot <= 0.0) {
            // Heading the other way: only one ahead.
            double dx = static_cast<double>(other->m_atPoint.x) - me->m_atPoint.x;
            double dy = static_cast<double>(other->m_atPoint.y) - me->m_atPoint.y;
            double dz = static_cast<double>(other->m_atPoint.z) - me->m_atPoint.z;
            double ahead = (static_cast<double>(at.z) * dz + static_cast<double>(at.y) * dy) + static_cast<double>(at.x) * dx;
            if (ahead <= 0.0)
                continue;
        }
        CKinematicsModule* otherMotion = Motion(other);
        if (headingDot > 0.0f && otherMotion->m_pathMaxSpeed >= motion->m_pathMaxSpeed)
            continue;
        double dx = static_cast<double>(m_entity->m_atPoint.x) - other->m_atPoint.x;
        double dy = static_cast<double>(m_entity->m_atPoint.y) - other->m_atPoint.y;
        double dz = static_cast<double>(m_entity->m_atPoint.z) - other->m_atPoint.z;
        double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        float distanceF = static_cast<float>(distance);
        if (!(distance <= range))
            continue;
        double otherLength = std::sqrt((static_cast<double>(otherAt.x) * otherAt.x + static_cast<double>(otherAt.y) * otherAt.y) +
                                       static_cast<double>(otherAt.z) * otherAt.z);
        double myLength = std::sqrt((static_cast<double>(at.x) * at.x + static_cast<double>(at.y) * at.y) +
                                    static_cast<double>(at.z) * at.z);
        float cosine = Maths::Confine(static_cast<float>(headingDot / (otherLength * myLength)), -1.0f, 1.0f);
        bool above;
        float angle = X87::AcosF(cosine, kFullTurn, &above);
        if (above)
            angle = static_cast<float>(angle - static_cast<double>(Truncate(static_cast<double>(angle) * kInverseFullTurn)) * kFullTurn);
        float offHeadOn = static_cast<float>(std::fabs(static_cast<double>(angle) - kHalfTurn));
        if (Scripts::FetchFloat(kHeadOnRange) > offHeadOn) {
            // Head on: to the side the other is on.
            angle = kQuarterTurn;
            if (Maths::CrossVec(at, otherAt).y < 0.0f)
                angle = -kQuarterTurn;
        }
        avoided = true;
        AddAvoidance(angle, bias, distanceF, range);
    }
    return avoided;
}

// 0x004EEAC0
void CPedestrianBrainModule::KeepToPath()
{
    const Maths::Vec4& from = m_nodePos;
    float ax = from.x, az = from.z, aw = from.w;
    float bx = m_nextNodePos.x, bz = m_nextNodePos.z, bw = m_nextNodePos.w;
    const Maths::Vec4& at = m_entity->m_atPoint;
    float px = at.x, pz = at.z, pw = at.w;
    float maxDistance = Scripts::FetchFloat(kMaxDistanceToPath);
    float dx = static_cast<float>(static_cast<double>(bx) - ax);
    double dz = static_cast<double>(bz) - az;
    float dw = static_cast<float>(static_cast<double>(bw) - aw);
    double ex = static_cast<double>(px) - ax, ez = static_cast<double>(pz) - az;
    float crossX = static_cast<float>(0.0f * ez - dz * 0.0f);
    float crossY = static_cast<float>(dz * ex - ez * dx);
    float crossZ = static_cast<float>(static_cast<double>(dx) * 0.0f - ex * 0.0f);
    double lengthSqr = static_cast<double>(dx) * dx + dz * dz;
    // The closest point of the path's line (its start for a point path).
    Maths::Vec4 closest = from;
    if (lengthSqr != 0.0) {
        double t = (ex * dx + ez * dz) / lengthSqr;
        double alongX = dx * t;
        float alongY = static_cast<float>(t * 0.0f), alongZ = static_cast<float>(dz * t), alongW = static_cast<float>(dw * t);
        closest = {static_cast<float>(alongX + from.x), static_cast<float>(static_cast<double>(alongY) + from.y),
                   static_cast<float>(static_cast<double>(alongZ) + from.z), static_cast<float>(static_cast<double>(alongW) + from.w)};
    }
    Maths::Vec4 toPath = {static_cast<float>(static_cast<double>(closest.x) - px), 0.0f,
                          static_cast<float>(static_cast<double>(closest.z) - pz),
                          static_cast<float>(static_cast<double>(closest.w) - pw)};
    Maths::Mat4 m = m_entity->m_displayMatrix;
    double exact = Maths::ReadTurnAngle(m.row[2], toPath);
    float angle = static_cast<float>(exact);
    if (exact > kDegrees)
        angle = static_cast<float>(angle - static_cast<double>(Truncate(static_cast<double>(angle) * kInverseDegrees)) * kDegrees);
    if (static_cast<double>(m.row[2].z) * toPath.x - static_cast<double>(m.row[2].x) * toPath.z > 0.0)
        angle = static_cast<float>(static_cast<double>(kDegrees) - angle);
    double sx = static_cast<double>(ax) - bx, sz = static_cast<double>(az) - bz;
    double segment = std::sqrt(sx * sx + sz * sz);
    float off;
    if (segment == 0.0)
        off = 0.0f;
    else
        off = static_cast<float>(std::sqrt((static_cast<double>(crossX) * crossX + static_cast<double>(crossZ) * crossZ) +
                                           static_cast<double>(crossY) * crossY) / segment);
    float weight;
    if (off > maxDistance)
        weight = 1.0f;
    else
        weight = static_cast<float>(1.0 - (static_cast<double>(maxDistance) - off) / maxDistance);
    if (Scripts::FetchFloat(kMinDistanceToPath) < off)
        AddSteer(static_cast<float>(static_cast<double>(angle) * kToRadians), weight);
}

// 0x004EEDC0
void CPedestrianBrainModule::MoveSteered()
{
    CKinematicsModule* motion = Motion(m_entity);
    float side = SteerSideways();
    double forward = SteerForward();
    PushPosition();
    double step = static_cast<double>(m_frameTime) * motion->m_pathSpeed;
    float sideStep = static_cast<float>(side * step);
    float forwardStep = static_cast<float>(step * forward);
    Maths::Mat4 m = m_entity->m_displayMatrix;
    double fx = static_cast<double>(m.row[2].x) * forwardStep, fy = static_cast<double>(m.row[2].y) * forwardStep;
    float fz = static_cast<float>(static_cast<double>(m.row[2].z) * forwardStep);
    float fw = static_cast<float>(static_cast<double>(m.row[2].w) * forwardStep);
    double sx = static_cast<double>(m.row[0].x) * sideStep, sy = static_cast<double>(m.row[0].y) * sideStep;
    float sz = static_cast<float>(static_cast<double>(m.row[0].z) * sideStep);
    float sw = static_cast<float>(static_cast<double>(m.row[0].w) * sideStep);
    Maths::Vec4& pos = m_entity->m_atPoint;
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
void CPedestrianBrainModule::SteerTowards(Maths::Vec4 target, float weight)
{
    Maths::Mat4 m = m_entity->m_displayMatrix;
    const Maths::Vec4& at = m_entity->m_atPoint;
    Maths::Vec4 away = {static_cast<float>(static_cast<double>(at.x) - target.x), 0.0f,
                        static_cast<float>(static_cast<double>(at.z) - target.z),
                        static_cast<float>(static_cast<double>(at.w) - target.w)};
    double angle = Maths::ReadTurnAngle(m.row[2], away) + kHalfDegrees;
    if (angle > kDegrees)
        angle -= static_cast<double>(Truncate(angle * kInverseDegrees)) * kDegrees;
    if (static_cast<double>(m.row[2].z) * away.x - static_cast<double>(m.row[2].x) * away.z > 0.0)
        angle = static_cast<double>(kDegrees) - angle;
    AddSteer(static_cast<float>(angle * kToRadians), weight);
}

// 0x004F2780
bool CPedestrianBrainModule::TurnTowardsNodeAfter()
{
    const Maths::Vec4& at = m_entity->m_atPoint;
    double dz = static_cast<double>(at.z) - m_nextNodePos.z;
    double dx = static_cast<double>(at.x) - m_nextNodePos.x;
    float distanceSqr = static_cast<float>(dx * dx + dz * dz);
    bool turning = false;
    float bias = Scripts::FetchFloat(kTargetNodeBias);
    for (float& avoid : m_avoid)
        avoid = 0.0f;
    if (m_nodeAfter == m_listLink) {
        // A dead end: reached when near enough.
        double deadEnd = Scripts::FetchFloat(kDeadEndDistance);
        if (static_cast<double>(distanceSqr) <= deadEnd)
            ReachWaypoint();
    } else {
        int fadeDistance = Scripts::FetchIntValue(m_kind == 2 ? kSkaterFadeDistance : kFadeDistance);
        if (m_turnFrames != 0 && m_turnFrame >= m_turnFrames) {
            ReachWaypoint();
        } else if (m_nodeAfter && static_cast<double>(fadeDistance) > distanceSqr) {
            // Near the next node: turning towards the one after over a number of frames by the corner's angle.
            if (m_turnFrames == 0) {
                CKinematicsModule* motion = Motion(m_entity);
                Maths::Vec4 from = {m_nodePos.x, 0.0f, m_nodePos.z, m_nodePos.w};
                Maths::Vec4 next = {m_nextNodePos.x, 0.0f, m_nextNodePos.z, m_nextNodePos.w};
                Maths::Vec4 after = {m_nodeAfterPos.x, 0.0f, m_nodeAfterPos.z, m_nodeAfterPos.w};
                Maths::Vec4 in = next - from;
                Maths::Vec4 out = after - next;
                double corner = Maths::ReadTurnAngle(in, out) * kToRadians;
                double reach = std::sqrt(std::sqrt(CosPlus(corner, 1.0) * (static_cast<double>(distanceSqr) + distanceSqr)));
                m_turnFrames = Truncate(reach / motion->m_pathMaxSpeed * kFramesPerSecond);
                if (m_turnFrames <= 0)
                    m_turnFrames = 1;
            }
            int frames = m_turnFrames;
            turning = true;
            double fraction = frames == 0 ? 0.0
                                          : (1.0 - static_cast<double>(frames - m_turnFrame) / frames) * 0.5f;
            ++m_turnFrame;
            float toAfter = static_cast<float>(fraction * bias);
            SteerTowards(m_nodeAfterPos, toAfter);
            bias = static_cast<float>(static_cast<double>(bias) - toAfter);
        }
    }
    SteerTowards(m_nextNodePos, bias);
    return turning;
}

// 0x004EF350
void CPedestrianBrainModule::FaceMovement(Maths::Vec4 oldPos)
{
    Maths::Vec4 pos = m_entity->m_atPoint;
    Maths::Mat4 m;
    if (m_kind == 0) {
        // A walker turns its orientation towards where it moved, at most so much a frame.
        m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
        m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
        m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        Maths::Vec4 heading = m_entity->m_displayMatrix.row[2];
        heading.MakeUnit();
        Maths::Vec4 moved = {static_cast<float>(static_cast<double>(pos.x) - oldPos.x), 0.0f,
                             static_cast<float>(static_cast<double>(pos.z) - oldPos.z), 1.0f};
        moved.MakeUnit();
        float dot = static_cast<float>((static_cast<double>(moved.z) * heading.z + static_cast<double>(moved.y) * heading.y) +
                                       static_cast<double>(moved.x) * heading.x);
        if (dot > kMaxCos)
            dot = kMaxCos;
        else if (dot < kMinCos)
            dot = kMinCos;
        bool unused;
        float turn = std::fabs(X87::AcosF(dot, 0.0f, &unused));
        if (!(static_cast<double>(m_frameTime) * kTurnPerSecond >= turn)) {
            static float maxTurn = 0.69813174f; // (40 degrees)
            float t = kTurnTime;
            if (turn > maxTurn)
                t = static_cast<float>(static_cast<double>(turn) / maxTurn * kTurnTime);
            m.row[2] = *Walk::Lerp(&pos, &heading, &moved, t);
            m.row[2].MakeUnit();
        } else {
            m.row[2] = moved;
        }
        m.SquareUpAxes(2);
        m_entity->m_basisFrame = m;
    } else {
        // A skater faces where it moved (or along its path, to a lip).
        m = m_entity->m_displayMatrix;
        float oldW = oldPos.w;
        Maths::Vec4 to = pos;
        if (m_flagBits & PED_FLAG_VERT_LIP) {
            oldPos = {m_nodePos.x, m_nodePos.y, m_nodePos.z, oldPos.w};
            oldW = m_nodePos.w;
            to = m_nextNodePos;
        }
        Maths::Vec4 heading = {static_cast<float>(static_cast<double>(to.x) - oldPos.x),
                               static_cast<float>(static_cast<double>(to.y) - oldPos.y),
                               static_cast<float>(static_cast<double>(to.z) - oldPos.z),
                               static_cast<float>(static_cast<double>(to.w) - oldW)};
        m.row[2] = heading.MakeUnit();
        m.row[0].w = 0.0f;
        m.row[1].w = 0.0f;
        RebuildAcrossAndUp(m);
    }
    m_entity->m_displayMatrix = m;
    m_savedMatrix = m;
}

// 0x004EF740
bool CPedestrianBrainModule::ProbeGround(const Maths::Vec4* start, const Maths::Vec4* end, Maths::Vec4* point,
                                     GroundTriangle* triangle)
{
    Feeler feeler;
    Engine3D::CProbeRay* f = feeler.feeler();
    if (!f->GetCollisionAlong(start, end, false))
        return false;
    Engine3D::CHitMesh* object = f->m_hit.object;
    Engine3D::CTriMeshSource* geometry = object->m_geometry;
    uint32_t face = f->m_hit.face;
    for (int i = 0; i < 3; ++i) {
        uint32_t vertex = geometry->m_smallFaces ? geometry->m_faces[face * 8 + 4 + i]
                                                 : *reinterpret_cast<uint16_t*>(geometry->m_faces + face * 10 + 4 + 2 * i);
        // (slot 9, ReadVertexPoint: the Vector returned through a pointer the caller passes)
        Maths::Vec4 corner;
        triangle->corners[i] = *reinterpret_cast<Maths::Vec4*(__thiscall*)(Engine3D::CHitMesh*, Maths::Vec4*, int)>(
            object->m_vtable[9])(object, &corner, static_cast<int>(static_cast<uint16_t>(vertex)));
    }
    *point = f->m_contactPos;
    SetSoundTerrain(f->m_hit.terrain);
    return true;
}

// 0x004EF930
void CPedestrianBrainModule::StickToGroundPed()
{
    CKinematicsModule* motion = Motion(m_entity);
    // The orientation's across row again (up x at).
    Maths::Mat4& o = m_entity->m_basisFrame;
    double ax = static_cast<double>(o.row[1].y) * o.row[2].z - static_cast<double>(o.row[1].z) * o.row[2].y;
    double ay = static_cast<double>(o.row[1].z) * o.row[2].x - static_cast<double>(o.row[1].x) * o.row[2].z;
    double az = static_cast<double>(o.row[1].x) * o.row[2].y - static_cast<double>(o.row[1].y) * o.row[2].x;
    o.row[0].w = 0.0f;
    o.row[0].z = static_cast<float>(az);
    o.row[0].x = static_cast<float>(ax);
    o.row[0].y = static_cast<float>(ay);
    o.row[0].MakeUnit();
    // A probe from above to below along the display matrix's up.
    Maths::Mat4 m = m_entity->m_displayMatrix;
    const Maths::Vec4& up = m.row[1];
    const Maths::Vec4& pos = m_entity->m_atPoint;
    float above = m_stickAbove;
    float ux = static_cast<float>(static_cast<double>(up.x) * above), uy = static_cast<float>(static_cast<double>(up.y) * above),
          uz = static_cast<float>(static_cast<double>(up.z) * above);
    double uw = static_cast<double>(above) * up.w;
    Maths::Vec4 start = {static_cast<float>(static_cast<double>(ux) + pos.x), static_cast<float>(static_cast<double>(uy) + pos.y),
                         static_cast<float>(static_cast<double>(uz) + pos.z), static_cast<float>(uw + pos.w)};
    float below = m_stickBelow;
    float nz = -up.z, nw = -up.w;
    float dx = static_cast<float>(-static_cast<double>(up.x) * below), dy = static_cast<float>(-static_cast<double>(up.y) * below);
    double dz = static_cast<double>(nz) * below;
    float dw = static_cast<float>(static_cast<double>(nw) * below);
    Maths::Vec4 end = {static_cast<float>(static_cast<double>(dx) + pos.x), static_cast<float>(static_cast<double>(dy) + pos.y),
                       static_cast<float>(dz + pos.z), static_cast<float>(static_cast<double>(dw) + pos.w)};
    Maths::Vec4 point;
    if (ProbeGround(&start, &end, &point, reinterpret_cast<GroundTriangle*>(&motion->m_groundTriangle[0])))
        m_entity->m_atPoint = point;
}

// 0x004EF090
void CPedestrianBrainModule::AlignToGround()
{
    CKinematicsModule* motion = Motion(m_entity);
    if (!motion)
        return;
    GroundTriangle t;
    t.Assign(*reinterpret_cast<GroundTriangle*>(&motion->m_groundTriangle[0]));
    const Maths::Vec4* c = t.corners;
    double e1x = static_cast<double>(c[1].x) - c[0].x, e1y = static_cast<double>(c[1].y) - c[0].y,
           e1z = static_cast<double>(c[1].z) - c[0].z;
    float e2x = static_cast<float>(static_cast<double>(c[2].x) - c[0].x);
    float e2y = static_cast<float>(static_cast<double>(c[2].y) - c[0].y);
    double e2z = static_cast<double>(c[2].z) - c[0].z;
    Maths::Vec4 normal = {static_cast<float>(e2z * e1y - static_cast<double>(e2y) * e1z),
                          static_cast<float>(e1z * e2x - e2z * e1x),
                          static_cast<float>(static_cast<double>(e2y) * e1x - e1y * e2x), 0.0f};
    Maths::Vec4 up = normal.MakeUnit();
    if (up.y < 0.0f)
        up = {-up.x, -up.y, -up.z, -up.w};
    if (up != m_upTarget) {
        m_upTarget = up;
        m_upDir = m_upShown;
        m_upBlend = 1.0f;
    }
    if (!(m_upBlend == 0.0f)) {
        if (m_upDir == m_upTarget) {
            m_upBlend = 0.0f;
        } else {
            double blend = m_upBlend - static_cast<double>(m_frameTime) * kUpBlendRate;
            float blendF = static_cast<float>(blend);
            m_upBlend = blendF;
            if (blend > 0.0 || std::isnan(blend)) {
                Maths::Vec4 between;
                m_upShown = *Walk::Lerp(&between, &m_upTarget, &m_upDir, blendF);
                m_upShown.MakeUnit();
            } else {
                m_upBlend = 0.0f;
                m_upShown = m_upTarget;
            }
        }
    }
    Maths::Mat4 m = m_entity->m_displayMatrix;
    if (m.row[1] != m_upShown) {
        m.row[1] = m_upShown;
        m.SquareUpAxes(1);
        m_entity->m_displayMatrix = m;
        m_savedMatrix = m;
    }
}

// 0x004F01C0
void CPedestrianBrainModule::SpinUpdate()
{
    float elapsed = static_cast<float>(static_cast<double>(m_frameTime) + m_spinElapsed);
    m_spinElapsed = elapsed;
    double fraction = static_cast<double>(m_frameTime) / m_spinTime;
    if (fraction > 1.0)
        fraction = 1.0;
    float turn = static_cast<float>(static_cast<double>(kHalfDegrees) - m_vertRotationTarget);
    uint32_t flags = m_flagBits;
    if (flags & PED_FLAG_SPINNING)
        m_twistRate = static_cast<float>(static_cast<double>(m_spinAngle) * fraction + m_twistRate);
    if (flags & PED_FLAG_VERT_ROTATING)
        m_vertRotation = static_cast<float>(static_cast<double>(turn) * fraction + m_vertRotation);
    if (elapsed >= m_spinTime || fraction >= 1.0) {
        if (flags & PED_FLAG_SPINNING)
            m_twistRate = static_cast<float>(m_spinAngle);
        if (flags & PED_FLAG_VERT_ROTATING)
            m_vertRotation = turn;
        m_flagBits = flags & ~(PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    }
    if ((m_flagBits & PED_FLAG_VERT_ROTATING) && m_vertRotation > turn)
        m_vertRotation = turn;
    Maths::Mat4 m = m_jumpMatrix;
    Maths::Vec4 rotation = {static_cast<float>(static_cast<double>(m_vertRotation) * kToRadians),
                            static_cast<float>(static_cast<double>(m_twistRate) * kToRadians), 0.0f, 1.0f};
    m.TurnInOwnFrame(rotation);
    m_entity->m_displayMatrix = m;
    m_savedMatrix = m;
    m_upTarget = m.row[1];
    m_upDir = m_upTarget;
    m_upShown = m_upDir;
}

// 0x004F03C0
int CPedestrianBrainModule::ProbeAhead(Maths::Vec4 direction, float range, Maths::Vec4* out)
{
    float rangeSqr = static_cast<float>(static_cast<double>(range) * range);
    Maths::Mat4 m = m_entity->m_displayMatrix;
    Maths::Vec4 dir = direction;
    dir.FlattenOntoPlane(m.row[1]);
    double length = std::sqrt((static_cast<double>(dir.z) * dir.z + static_cast<double>(dir.y) * dir.y) +
                              static_cast<double>(dir.x) * dir.x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        dir.x = static_cast<float>(dir.x * inverse);
        dir.y = static_cast<float>(dir.y * inverse);
        dir.z = static_cast<float>(dir.z * inverse);
    }
    // A ped close ahead?
    CObjectSearch search;
    search.m_vtable = g_objectSearchVtable;
    search.m_listLink = nullptr;
    CObjectHubBase* manager = m_entity->m_parkOwner;
    auto* list = reinterpret_cast<MemSys::ListNode*(__thiscall*)(CObjectHubBase*)>(manager->m_vtable[6])(manager);
    for (auto* other = static_cast<CGameEntity*>(search.FirstItemOfType(list, kObjectTypePed)); other;
         other = static_cast<CGameEntity*>(search.NextItemOfType())) {
        CGameEntity* me = m_entity;
        if (other == me)
            continue;
        float dz = static_cast<float>(static_cast<double>(other->m_atPoint.z) - me->m_atPoint.z);
        float dy = static_cast<float>(static_cast<double>(other->m_atPoint.y) - me->m_atPoint.y);
        double dx = static_cast<double>(other->m_atPoint.x) - me->m_atPoint.x;
        double distanceSqr = (dx * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz;
        if (!(distanceSqr < rangeSqr))
            continue;
        double distance = std::sqrt(distanceSqr);
        float distanceF = static_cast<float>(distance);
        double nx = dx, nz = dz;
        float ny = dy;
        if (distance > 0.0) {
            double inverse = 1.0 / distanceF;
            nx = dx * inverse;
            ny = static_cast<float>(static_cast<double>(dy) * inverse);
            nz = inverse * dz;
        }
        double dot = (static_cast<double>(dir.z) * nz + static_cast<double>(ny) * dir.y) + nx * dir.x;
        if (dot > kAhead) {
            if (out)
                *out = other->m_atPoint;
            return 3;
        }
    }
    // The world: a line from 48 up to 8 up `range` ahead.
    dir.x = static_cast<float>(static_cast<double>(dir.x) * range);
    dir.y = static_cast<float>(static_cast<double>(dir.y) * range);
    dir.z = static_cast<float>(static_cast<double>(dir.z) * range);
    dir.w = static_cast<float>(static_cast<double>(dir.w) * range);
    const Maths::Vec4& at = m_entity->m_atPoint;
    Maths::Vec4 start = at;
    start.y = static_cast<float>(static_cast<double>(start.y) + kReach);
    float topY = static_cast<float>(static_cast<double>(dir.y) + at.y);
    Maths::Vec4 end = {static_cast<float>(static_cast<double>(dir.x) + at.x),
                       static_cast<float>(static_cast<double>(topY) + kProbeLift),
                       static_cast<float>(static_cast<double>(dir.z) + at.z),
                       static_cast<float>(static_cast<double>(dir.w) + at.w)};
    Feeler feeler(&start, &end);
    Engine3D::CProbeRay* f = feeler.feeler();
    f->SetExcluded(0x10, 0);
    if (!f->GetHitShape(true, false))
        return 0;
    if (out)
        *out = f->m_contactPos;
    return 1;
}

// --- the move updates ---

// 0x004F2A10
void CPedestrianBrainModule::GenericMoveUpdate()
{
    if (!(m_flagBits & PED_FLAG_MOVING))
        return;
    CKinematicsModule* motion = Motion(m_entity);
    bool avoided = AvoidPeds();
    if (!TurnTowardsNodeAfter() && !avoided)
        KeepToPath();
    Maths::Vec4 old = m_entity->m_atPoint;
    MotionPath(motion)->UpdatePathSpeed();
    MoveSteered();
    if (old != m_entity->m_atPoint)
        FaceMovement(old);
    DecayBias();
    motion->StickToGround();
}

// 0x004F2AD0
void CPedestrianBrainModule::SkaterMoveUpdate()
{
    CKinematicsModule* motion = Motion(m_entity);
    MotionPath(motion)->UpdatePathSpeed();
    float step = static_cast<float>(static_cast<double>(m_frameTime) * motion->m_pathSpeed);
    bool airborne = (motion->m_motionFlags & 0x100) != 0;
    if (m_flagBits & PED_FLAG_GRINDING) {
        if (CGameEntity* skater = LocalSkater()) {
            const Maths::Vec4& at = m_entity->m_atPoint;
            double dx = static_cast<double>(at.x) - skater->m_atPoint.x, dy = static_cast<double>(at.y) - skater->m_atPoint.y,
                   dz = static_cast<double>(at.z) - skater->m_atPoint.z;
            float distanceSqr = static_cast<float>((dz * dz + dy * dy) + dx * dx);
            if (Scripts::FetchFloat(kMinDistanceToSkater) > distanceSqr) {
                BailFromGrind();
                return;
            }
        }
    }
    if ((m_flagBits & PED_FLAG_MOVING) || airborne) {
        // Reaching the next waypoint (in the air, or for a vert node, in 3D).
        const Maths::Vec4& at = m_entity->m_atPoint;
        double dx, dy, dz;
        if ((m_flagBits & PED_FLAG_VERT_NEXT) || airborne) {
            dx = static_cast<double>(at.x) - m_nextNodePos.x;
            dy = static_cast<float>(static_cast<double>(at.y) - m_nextNodePos.y);
            dz = static_cast<float>(static_cast<double>(at.z) - m_nextNodePos.z);
        } else {
            dx = static_cast<double>(at.x) - m_nextNodePos.x;
            dy = 0.0f;
            dz = static_cast<float>(static_cast<double>(at.z) - m_nextNodePos.z);
        }
        float distanceSqr = static_cast<float>((dx * dx + dz * dz) + dy * dy);
        int minDistance = Scripts::FetchIntValue(kMinDistanceToWaypoint);
        if (!(m_flagBits & PED_FLAG_JUMPING_TO_NODE) &&
            (static_cast<double>(step) * step > distanceSqr || static_cast<double>(minDistance) > distanceSqr))
            ReachWaypoint();
        if (m_flagBits & PED_FLAG_CROUCH) {
            if (static_cast<double>(Scripts::FetchIntValue(kCrouchDistance)) > distanceSqr) {
                m_flagBits &= ~PED_FLAG_CROUCH;
                ChangePhase(kStateJumpCrouch, kSkaterPedStateTable, nullptr);
            }
        }
    }
    int sound = -1;
    if ((m_flagBits & PED_FLAG_MOVING) && step != 0.0f) {
        Maths::Vec4 old = m_entity->m_atPoint;
        bool faced = false;
        if (m_kind == 2 && !(m_flagBits & (PED_FLAG_GRINDING | PED_FLAG_VERT_NEXT | PED_FLAG_VERT_LAST | PED_FLAG_GRIND_BAILING)) &&
            !(m_flagBits & PED_FLAG_JUMPING_TO_NODE)) {
            if (!TurnTowardsNodeAfter())
                KeepToPath();
            MoveSteered();
            if (airborne)
                m_entity->m_displayMatrix = m_savedMatrix;
        } else {
            // Straight at the next node.
            Maths::Vec4 to = {m_nextNodePos.x - old.x, m_nextNodePos.y - old.y, m_nextNodePos.z - old.z,
                              m_nextNodePos.w - old.w};
            const Maths::Vec4& dir = to.MakeUnit();
            float z = static_cast<float>(static_cast<double>(step) * dir.z), w = static_cast<float>(static_cast<double>(step) * dir.w);
            Maths::Vec4 moved = {static_cast<float>(static_cast<double>(step) * dir.x + old.x),
                                 static_cast<float>(static_cast<double>(step) * dir.y + old.y),
                                 static_cast<float>(static_cast<double>(z) + old.z), static_cast<float>(static_cast<double>(w) + old.w)};
            KeepToPath();
            MoveSteered();
            m_entity->m_atPoint = moved;
            FaceMovement(old);
            faced = true;
        }
        if (!(m_flagBits & (PED_FLAG_GRINDING | PED_FLAG_GRIND_BAILING))) {
            if (airborne) {
                sound = 1;
            } else {
                StickToGroundPed();
                if (!faced)
                    FaceMovement(old);
                AlignToGround();
            }
        } else if (airborne) {
            sound = 1;
        }
    } else {
        PushPosition();
        m_entity->m_displayMatrix = m_savedMatrix;
        if (m_flagBits & (PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING))
            SpinUpdate();
        if (airborne)
            sound = 1;
    }
    if (sound < 0) {
        if (m_flagBits & PED_FLAG_GRINDING)
            sound = 4;
        else if (m_flagBits & PED_FLAG_MOVING)
            sound = 0;
        else if (m_flagBits & PED_FLAG_SOUND_STATE_3)
            sound = 3;
    }
    if (sound >= 0)
        SetSoundState(sound);
    if (m_flagBits & PED_FLAG_MOVING)
        m_speedRatio = motion->m_pathMaxSpeed == 0.0f ? 0.0f : static_cast<float>(static_cast<double>(motion->m_pathSpeed) / motion->m_pathMaxSpeed);
    else
        m_speedRatio = 0.0f;
    if (auto* looping = static_cast<CRiderRollingAudio*>(m_entity->FetchModule(kSkaterLoopingSound))) {
        looping->m_noRailSound = (m_flagBits & (PED_FLAG_BAILING | PED_FLAG_GRIND_BAILING)) != 0;
        looping->SetPaceRatio(m_speedRatio);
    }
}

// 0x004F1890
void CPedestrianBrainModule::BailMoveUpdate()
{
    CKinematicsModule* motion = Motion(m_entity);
    MotionPath(motion)->UpdatePathSpeed();
    float step = static_cast<float>(static_cast<double>(m_frameTime) * motion->m_pathSpeed);
    if (!(m_flagBits & (PED_FLAG_BAILING | PED_FLAG_GRIND_BAILING))) {
        // The bail's over: back to skating.
        m_updateType = kSkaterMoveUpdate;
        Scripts::LaunchScript(kPedInitializeMovement, nullptr, m_entity, false, nullptr);
        if (m_updateType == kGenericMoveUpdate || m_updateType == kSkaterMoveUpdate)
            m_flagBits |= PED_FLAG_MOVING;
        return;
    }
    if (m_flagBits & PED_FLAG_MOVING) {
        const Maths::Vec4& at = m_entity->m_atPoint;
        float dx = static_cast<float>(static_cast<double>(at.x) - m_nextNodePos.x);
        double dz = static_cast<double>(at.z) - m_nextNodePos.z;
        float distanceSqr = static_cast<float>(dz * dz + static_cast<double>(dx) * dx);
        int minDistance = Scripts::FetchIntValue(kMinDistanceToWaypoint);
        if (!(m_flagBits & PED_FLAG_JUMPING_TO_NODE) &&
            (static_cast<double>(step) * step > distanceSqr || static_cast<double>(minDistance) > distanceSqr))
            m_flagBits &= ~PED_FLAG_MOVING;
    }
    if ((m_flagBits & PED_FLAG_MOVING) && !(step == 0.0f)) {
        Maths::Vec4 old = m_entity->m_atPoint;
        Maths::Vec4 to = {m_nextNodePos.x - old.x, m_nextNodePos.y - old.y, m_nextNodePos.z - old.z, m_nextNodePos.w - old.w};
        const Maths::Vec4& dir = to.MakeUnit();
        float z = static_cast<float>(static_cast<double>(step) * dir.z), w = static_cast<float>(static_cast<double>(step) * dir.w);
        Maths::Vec4 moved = {static_cast<float>(static_cast<double>(step) * dir.x + old.x),
                             static_cast<float>(static_cast<double>(step) * dir.y + old.y),
                             static_cast<float>(static_cast<double>(z) + old.z), static_cast<float>(static_cast<double>(w) + old.w)};
        KeepToPath();
        MoveSteered();
        m_entity->m_atPoint = moved;
    }
    m_entity->m_displayMatrix = m_savedMatrix;
}

// 0x004F1780
void CPedestrianBrainModule::GoHomeUpdate()
{
    Maths::Vec4 home = {m_nodePos.x - m_entity->m_atPoint.x, m_nodePos.y - m_entity->m_atPoint.y, m_nodePos.z - m_entity->m_atPoint.z,
                        m_nodePos.w - m_entity->m_atPoint.w};
    double distanceSqr = (static_cast<double>(home.z) * home.z + static_cast<double>(home.y) * home.y) +
                         static_cast<double>(home.x) * home.x;
    if (distanceSqr <= kNear) {
        Scripts::LaunchScript(kEndReturnHome, nullptr, m_entity, false, nullptr);
        return;
    }
    home.MakeUnit();
    CKinematicsModule* motion = Motion(m_entity);
    PushPosition();
    Maths::Vec4& pos = m_entity->m_atPoint;
    Maths::Vec4 old = pos;
    double step = static_cast<double>(m_frameTime) * motion->m_pathMaxSpeed;
    float stepF = static_cast<float>(step);
    pos.w = 1.0f;
    pos.x = static_cast<float>(step * home.x + old.x);
    pos.z = static_cast<float>(static_cast<double>(stepF) * home.z + old.z);
    FaceMovement(old);
    motion->StickToGround();
}

// 0x004F1480
void CPedestrianBrainModule::ChaseUpdate()
{
    CGameEntity* skater = LocalSkater();
    if (!skater)
        return;
    CKinematicsModule* motion = Motion(m_entity);
    float dz = static_cast<float>(static_cast<double>(skater->m_atPoint.z) - m_entity->m_atPoint.z);
    Maths::Vec4 towardPlayer = {static_cast<float>(static_cast<double>(skater->m_atPoint.x) - m_entity->m_atPoint.x), 0.0f, dz, 0.0f};
    double distanceSqr = static_cast<double>(towardPlayer.x) * towardPlayer.x + static_cast<double>(dz) * dz;
    if (distanceSqr <= kCatchDistanceSqr) {
        // Caught.
        FaceAlong(&towardPlayer);
        Scripts::CStruct* params = NewParams();
        params->PutNameKey(kChaser, skater->m_ident);
        ChangePhase(kStateChaseCatch, 0, params);
        DeleteParams(params);
        return;
    }
    if (distanceSqr > kChaseOutOfRangeSqr) {
        Scripts::LaunchScript(kEndPlayerChase, nullptr, m_entity, false, nullptr);
        return;
    }
    int maxHome = 0;
    if (m_entity->ReadIntTag(kMaxHomeDist, &maxHome) && maxHome > 0) {
        Maths::Vec4 fromHome = m_nodePos - m_entity->m_atPoint;
        double lengthSqr = (static_cast<double>(fromHome.x) * fromHome.x + static_cast<double>(fromHome.y) * fromHome.y) +
                           static_cast<double>(fromHome.z) * fromHome.z;
        if (static_cast<double>(maxHome * maxHome) <= lengthSqr) {
            Scripts::LaunchScript(kEndPlayerChase, nullptr, m_entity, false, nullptr);
            return;
        }
    }
    if (ProbeAhead({towardPlayer.x, 0.0f, dz, 0.0f}, kReach, nullptr)) {
        if (m_curPhase != kStateChasePause)
            ChangePhase(kStateChasePause, 0, nullptr);
        return;
    }
    if (m_curPhase != kStateChase)
        ChangePhase(kStateChase, 0, nullptr);
    motion->m_pathMaxSpeed = kChaseSpeed;
    towardPlayer.MakeUnit();
    PushPosition();
    Maths::Vec4& pos = m_entity->m_atPoint;
    Maths::Vec4 old = pos;
    double step = static_cast<double>(m_frameTime) * motion->m_pathMaxSpeed;
    float stepF = static_cast<float>(step);
    pos.w = 1.0f;
    pos.x = static_cast<float>(step * towardPlayer.x + old.x);
    pos.z = static_cast<float>(static_cast<double>(stepF) * towardPlayer.z + old.z);
    FaceMovement(old);
    motion->StickToGround();
}

// 0x004F0DD0
void CPedestrianBrainModule::BlockerUpdate()
{
    CGameEntity* skater = LocalSkater();
    if (!skater)
        return;
    CKinematicsModule* motion = Motion(m_entity);
    float sz = static_cast<float>(static_cast<double>(skater->m_atPoint.z) - m_entity->m_atPoint.z);
    Maths::Vec4 towardPlayer = {static_cast<float>(static_cast<double>(skater->m_atPoint.x) - m_entity->m_atPoint.x), 0.0f, sz, 0.0f};
    float pathZ = static_cast<float>(static_cast<double>(m_nextNodePos.z) - m_nodePos.z);
    float pathX = static_cast<float>(static_cast<double>(m_nextNodePos.x) - m_nodePos.x);
    // Across the path, on the skater's side.
    double side = static_cast<double>(pathX) * sz - static_cast<double>(pathZ) * towardPlayer.x;
    double turn = side < 0.0 ? kBlockerTurn : -kBlockerTurn;
    float sine = X87::SinF(turn);
    float cosine = X87::CosF(turn);
    double acrossX = CosTimes(turn, pathX) + static_cast<double>(pathZ) * sine;
    double acrossZ = -(static_cast<double>(pathX) * sine) + static_cast<double>(cosine) * pathZ;
    Maths::Vec4 across = {static_cast<float>(acrossX), 0.0f, static_cast<float>(acrossZ), 0.0f};
    across.MakeUnit();
    if (static_cast<double>(towardPlayer.x) * towardPlayer.x + static_cast<double>(sz) * sz <= kCatchDistanceSqr) {
        // Bumped.
        FaceAlong(&across);
        Scripts::CStruct* params = NewParams();
        params->PutVec3(kVelDir, Scripts::VectorValue{across.x, across.y, across.z, across.w});
        m_entity->PostOwnEvent(kBlockerBumpPlayer, params);
        DeleteParams(params);
        return;
    }
    // Where the skater's going crosses the path.
    const Maths::Vec4& vel = skater->m_vel;
    bool notApproaching = !((static_cast<double>(across.z) * vel.z + static_cast<double>(across.y) * vel.y) +
                                static_cast<double>(across.x) * vel.x > 0.0);
    double speedSqr = (static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) + static_cast<double>(vel.z) * vel.z;
    const Maths::Vec4& at = skater->m_atPoint;
    Maths::Vec4 end;
    Maths::Vec4 onPath;
    float s, t;
    if (!(speedSqr < kFastSkaterSqr) && notApproaching) {
        float ex = static_cast<float>(static_cast<double>(vel.x) * kLookAhead), ey = static_cast<float>(static_cast<double>(vel.y) * kLookAhead),
              ez = static_cast<float>(static_cast<double>(vel.z) * kLookAhead);
        double ew = static_cast<double>(vel.w) * kLookAhead;
        end = {static_cast<float>(static_cast<double>(ex) + at.x), static_cast<float>(static_cast<double>(ey) + at.y),
               static_cast<float>(static_cast<double>(ez) + at.z), static_cast<float>(ew + at.w)};
    } else {
        float ex = static_cast<float>(static_cast<double>(across.x) * -kLookAhead), ey = static_cast<float>(static_cast<double>(across.y) * -kLookAhead),
              ez = static_cast<float>(static_cast<double>(across.z) * -kLookAhead);
        double ew = static_cast<double>(across.w) * -kLookAhead;
        end = {static_cast<float>(static_cast<double>(ex) + at.x), static_cast<float>(static_cast<double>(ey) + at.y),
               static_cast<float>(static_cast<double>(ez) + at.z), static_cast<float>(ew + at.w)};
    }
    Maths::Seg path(m_nextNodePos, m_nodePos);
    Maths::Seg skaterLine(at, end);
    Maths::ClosestPointsBetweenLines(path, skaterLine, onPath, end, s, t, true);
    double speed = std::sqrt((static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                             static_cast<double>(vel.z) * vel.z);
    float gz = static_cast<float>(static_cast<double>(onPath.z) - at.z);
    float gy = static_cast<float>(static_cast<double>(onPath.y) - at.y);
    float gx = static_cast<float>(static_cast<double>(onPath.x) - at.x);
    if (speed == 0.0)
        speed = kNoSpeed;
    double skaterTime = std::sqrt((static_cast<double>(gx) * gx + static_cast<double>(gy) * gy) + static_cast<double>(gz) * gz) / speed;
    const Maths::Vec4& me = m_entity->m_atPoint;
    double tx = static_cast<double>(onPath.x) - me.x, ty = static_cast<double>(onPath.y) - me.y,
           tz = static_cast<double>(onPath.z) - me.z, tw = static_cast<double>(onPath.w) - me.w;
    Maths::Vec4 toPoint = {static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz), static_cast<float>(tw)};
    double distance = std::sqrt((tx * tx + ty * ty) + tz * tz);
    float distanceF = static_cast<float>(distance);
    if (distance <= kStandDistance) {
        FaceAlong(&towardPlayer);
        m_entity->PostOwnEvent(kBlockerStand, nullptr);
        return;
    }
    float needed = static_cast<float>(static_cast<double>(distanceF) / skaterTime);
    toPoint.MakeUnit();
    if (!(needed > kRunRatio) && !(distanceF > kRunDistance)) {
        // Near enough: a shuffle to the side the point is on.
        FaceAlong(&across);
        Scripts::CStruct* params = NewParams();
        Maths::Vec4 cross = Maths::CrossVec(toPoint, across);
        params->PutNameKey(kDir, cross.y < 0.0f ? kLeft : kRight);
        m_entity->PostOwnEvent(kBlockerShuffle, params);
        DeleteParams(params);
    } else {
        FaceAlong(&toPoint);
        m_entity->PostOwnEvent(kBlockerRun, nullptr);
    }
    float speedNeeded = needed > kMinBlockerSpeed ? needed : kMinBlockerSpeed;
    motion->m_pathMaxSpeed = speedNeeded;
    if (!(speedNeeded < kMaxBlockerSpeed))
        speedNeeded = kMaxBlockerSpeed;
    motion->m_pathMaxSpeed = speedNeeded;
    PushPosition();
    double step = static_cast<double>(m_frameTime) * motion->m_pathMaxSpeed;
    Maths::Vec4& pos = m_entity->m_atPoint;
    pos.w = 1.0f;
    pos.x = static_cast<float>(static_cast<double>(toPoint.x) * step + pos.x);
    pos.z = static_cast<float>(static_cast<double>(toPoint.z) * step + pos.z);
}

// 0x004F2190
void CPedestrianBrainModule::TickFrame()
{
    m_frameTime = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    switch (m_updateType) {
    case kGenericMoveUpdate: VirtualGenericMoveUpdate(); break;
    case kGoHomeUpdate: GoHomeUpdate(); break;
    case kChaseUpdate: ChaseUpdate(); break;
    case kBlockerUpdate: BlockerUpdate(); break;
    case kNoMoveUpdate: VirtualNoMoveUpdate(); break;
    case kSkaterMoveUpdate: VirtualSkaterMoveUpdate(); break;
    case kLipTrickUpdate: LipTrickUpdate(); break;
    case kBailUpdate: BailMoveUpdate(); break;
    default: break;
    }
    if (mp_auxScript)
        mp_auxScript->TickFrame();
}

// --- script member functions ---

// 0x004F0950
bool CPedestrianBrainModule::Ped_TurnToCurrentWaypointOrientation(Scripts::CStruct* params, Scripts::CScript*)
{
    CKinematicsModule* motion = Motion(m_entity);
    Maths::Mat4 m;
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Vec4 angles = {0.0f, 0.0f, 0.0f, 1.0f};
    LevelNodes::ReadTurnAngles(m_listLink, &angles);
    Maths::Mat4 rotation;
    D3dxMultiply(m, m, Maths::MakePitchMatrix(rotation, angles.x));
    D3dxMultiply(m, m, Maths::MakeYawMatrix(rotation, angles.y));
    D3dxMultiply(m, m, Maths::MakeRollMatrix(rotation, angles.z));
    for (int i = 0; i < 4; ++i)
        (&m.row[2].x)[i] = static_cast<float>(static_cast<double>((&m.row[2].x)[i]) * kReach);
    const Maths::Vec4& at = m_entity->m_atPoint;
    Maths::Vec4 target = {static_cast<float>(static_cast<double>(m.row[2].x) + at.x),
                          static_cast<float>(static_cast<double>(m.row[2].y) + at.y),
                          static_cast<float>(static_cast<double>(m.row[2].z) + at.z),
                          static_cast<float>(static_cast<double>(m.row[2].w) + at.w)};
    motion->LookAt(params, &target);
    return true;
}

} // namespace Obj
