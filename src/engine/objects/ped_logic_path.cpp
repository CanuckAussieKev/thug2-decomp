// The PedLogic component (GameObjects::CPedestrianBrainModule): see ped_logic.h. Its path - the waypoints, the choice of the node
// after next, each waypoint's actions - and the skating peds' tricks (the states a node's SkateAction starts, the
// jumps' parameters); from the disassembly of 0x004ED0E0-0x004F2780 (0x004EDEC0, 0x004F1E80 and 0x004F2770 aren't
// Ghidra functions). The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include <bit>
#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/motion.h"
#include "engine/objects/path_manager.h"
#include "engine/objects/ped_logic.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"

namespace GameObjects {

namespace {

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kAnimBlender = 0x7149B753;

constexpr uint32_t kSkateAction = 0x963DC198;       // SkateAction
constexpr uint32_t kStopAction = 0xE41199AF;        // (a SkateAction: the end of a grind's way off)
constexpr uint32_t kVertGrab = 0x7D9D0008;          // Vert_Grab
constexpr uint32_t kVertLip = 0x554DBD64;           // Vert_Lip
constexpr uint32_t kVertGrind = 0xE6DFAEC7;         // Vert_Grind
constexpr uint32_t kVertJump = 0xD5B4F014;          // Vert_Jump
constexpr uint32_t kVertFlip = 0xE8F91257;          // Vert_Flip
constexpr uint32_t kVertOther = 0xDA0723DA;         // (a vert SkateAction; its name isn't known)
constexpr uint32_t kGrind = 0x255ED86F;             // Grind
constexpr uint32_t kJump = 0x584CF9E9;              // Jump
constexpr uint32_t kManual = 0xEF24413B;            // Manual
constexpr uint32_t kManualDown = 0x9403A0ED;        // Manual_Down
constexpr uint32_t kGrabTrick = 0x12797A1B;         // Grab_Trick
constexpr uint32_t kFlipTrick = 0xBA1D1E94;         // Flip_Trick
constexpr uint32_t kStop = 0x46A9E949;              // stop
constexpr uint32_t kRollOff = 0x6D3144BF;           // Roll_off
constexpr uint32_t kGrindOff = 0xC870BCE5;          // (a SkateAction: off the grind)
constexpr uint32_t kClass = 0x12B4E660, kWaypoint = 0x4C23A77E; // Class: Waypoint
constexpr uint32_t kTypeName = 0x7321A8D6, kPedAI = 0xCBA10FFA; // Type: PedAI
constexpr uint32_t kPriority = 0x9D5923D8, kLow = 0x6D77875E, kNormal = 0xDE7A971B; // Priority: Low / Normal
constexpr uint32_t kMinInnerPathAngle = 0xFE65D822; // ped_min_inner_path_angle
constexpr uint32_t kPedType = 0x17EA37FB, kSkate = 0x54166ACD; // PedType: Skate
constexpr uint32_t kHitDeadEnd = 0xA80F965E;        // ped_walker_hit_dead_end (an aux script)
constexpr uint32_t kChangePaceFlag = 0x74E4BBA5;       // (a node flag: its speed changed)
constexpr uint32_t kAdjustSpeedWeight = 0x922C5B59;
constexpr uint32_t kAdjustSpeedPercent = 0xA04D16EC; // AdjustSpeedPercent
constexpr uint32_t kLaunchNodeScript = 0x55D98ED7;         // LaunchScript
constexpr uint32_t kRunScriptWeight = 0x86C9CCCE;   // RunScriptWeight
constexpr uint32_t kNodeScriptKey = 0x64B4CD9D;     // RunScriptName
constexpr uint32_t kSurfaceKindKey = 0x54CF8532;       // TerrainType
constexpr uint32_t kTable = 0x46196541;             // SkaterPed_StateTable
constexpr uint32_t kStateGrind = 0x57CD5C63, kStateGrindOff = 0x6DB75D2A, kStateJumpVert = 0x5DC4A15F,
                   kStateJump = 0xCCAF702F, kStateLip = 0x4E7AA8C2, kStateManual = 0xE6E09E94,
                   kStateManualDown = 0xEF03188F, kStateGrab = 0x423331C0, kStateStop = 0xD24A608F,
                   kStateRollOff = 0x5F69700C, kStateFlip = 0xD757239F, kStateBailGrind = 0x423431E1;
constexpr uint32_t kIsVert = 0x01615618;            // is_vert
constexpr uint32_t kIsJumping = 0x65B5788D;         // is_jumping
constexpr uint32_t kJumpToNextNode = 0xB02567AE;    // JumpToNextNode
constexpr uint32_t kJumpHeight = 0x838DA447;        // JumpHeight
constexpr uint32_t kLeapGravity = 0xE224A5D3;       // ped_skater_jump_gravity
constexpr uint32_t kJumpSpeedGlobal = 0xF1FF758A;   // ped_skater_jump_speed
constexpr uint32_t kJumpSpeed = 0x1C4C5690;         // jumpSpeed
constexpr uint32_t kLeapDuration = 0x66CED87B;          // jumpTime
constexpr uint32_t kHeading = 0xFD4BC03E;           // heading
constexpr uint32_t kHeightSlop = 0x23F14B13;        // ped_skater_jump_to_next_node_height_slop
constexpr uint32_t kRandomSpin = 0xB4077854;        // (a flag: a random spin, in half turns)
constexpr uint32_t kMinSpinTime = 0x8B381AB1;       // ped_skater_min_180_spin_time
constexpr uint32_t kSpinAngle = 0x96FB50D9;         // SpinAngle
constexpr uint32_t kSpinDirection = 0xEF0B71A0;     // SpinDirection
constexpr uint32_t kBS = 0x20E1C4A3, kFS = 0x448D01A7, kRand = 0xE7390A8B; // BS / FS / Rand
constexpr uint32_t kSpineTransfer = 0x04AA0CFF;     // SpineTransfer
constexpr uint32_t kShouldFlip = 0x8868E76A;        // should_flip
constexpr uint32_t kSpineSlop = 0x95E91D23;         // ped_skater_spine_rotation_slop
constexpr uint32_t kVertTimeSlop = 0xC1DFC97B;      // ped_skater_vert_rotation_time_slop
constexpr uint32_t kLandHeight = 0xA1810C27;        // land_height
constexpr uint32_t kHeight = 0x0AB21AF0, kGravity = 0xA5E2DA58; // height / gravity
constexpr uint32_t kJumpSpeedScale = 0x2B5BEDBA;    // (a global float: the jump's speed along the ground)

constexpr float kFullTurnDegrees = 360.0f;         // 0x00645EA8
constexpr float kInverseFullTurn = 1.0f / 360.0f;  // 0x0064B798
constexpr float kAlwaysAbove = 0.99f;               // 0x00645450
constexpr float kThousand = 1000.0f;                // 0x006453C0
constexpr float kHundred = 100.0f;                  // 0x006463D4
constexpr float kMinusTwo = -2.0f;                  // 0x00646970
constexpr float kHalf = 0.5f;                       // 0x006453BC
constexpr float kSpinRound = 0.01f;                 // 0x0064544C

CKinematicsModule* Motion(CGameEntity* object) { return static_cast<CKinematicsModule*>(object->FetchModule(kMotion)); }

void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// _ftol2: a 64-bit truncation whose low half is used (the integer indefinite, 2^63 - a NaN, what doesn't fit - gives
// 0).
int Truncate(double value)
{
    if (!(std::fabs(value) < 9.2e18))
        return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

} // namespace

// 0x004ED080
bool CPedestrianBrainModule::IsVertNode(uint32_t node)
{
    // The action is read into the argument's own slot; a missing node is read through a null (original).
    uint32_t action = node;
    if (!LevelNodes::FetchVertex(node, true)->ReadHashValue(kSkateAction, &action, 0))
        return false;
    return action == kVertGrab || action == kVertLip || action == kVertGrind || action == kVertOther ||
           action == kVertFlip || action == kVertJump;
}

// 0x004ED0E0
bool CPedestrianBrainModule::Chance(Scripts::CStruct* node, uint32_t weightName)
{
    // (The weight is read over the `weightName` argument: missing - it's asserted - it stays the name's bits.)
    float weight = std::bit_cast<float>(weightName);
    node->FetchFloat(weightName, &weight, 1);
    if (weight > kAlwaysAbove)
        return true;
    int roll = Maths::Rnd(1000);
    return static_cast<double>(weight) * kThousand > roll;
}

// 0x004ED8A0
void CPedestrianBrainModule::ChooseNodeAfter()
{
    bool fromStop = false;
    uint32_t action;
    if (LevelNodes::FetchVertex(m_listLink, true)->ReadHashValue(kSkateAction, &action, 0) && action == kStopAction)
        fromStop = true;
    int count = static_cast<int>(LevelNodes::GetLinkCount(m_nextNode));
    uint32_t chosen;
    if (count == 0) {
        chosen = m_listLink;
    } else if (count == 1) {
        chosen = LevelNodes::FetchLink(m_nextNode, 0);
        if (chosen == m_listLink) {
            // A dead end, unless the node is a vert grab: back the way it came.
            uint32_t nextAction;
            if (LevelNodes::FetchVertex(m_nextNode, true)->ReadHashValue(kSkateAction, &nextAction, 0) && nextAction == kVertGrab)
                chosen = m_nextNode;
        }
    } else {
        Maths::Vec4 incoming = {m_nextNodePos.x - m_nodePos.x, m_nextNodePos.y - m_nodePos.y,
                                m_nextNodePos.z - m_nodePos.z, m_nextNodePos.w - m_nodePos.w};
        int lows[20], normals[20];
        for (int i = 0; i < count; ++i) {
            normals[i] = i;
            lows[i] = i;
        }
        int lowCount = 0, normalCount = 0;
        int minAngle = Scripts::FetchIntValue(kMinInnerPathAngle);
        float bestAngle = 0.0f;
        int best = 0;
        for (int i = 0; i < count; ++i) {
            uint32_t link = LevelNodes::FetchLink(m_nextNode, i);
            if (link == m_listLink)
                continue;
            Scripts::CStruct* linked = LevelNodes::FetchVertex(link, true);
            uint32_t linkClass = 0, linkType = 0;
            linked->ReadHashValue(kClass, &linkClass, 0);
            linked->ReadHashValue(kTypeName, &linkType, 0);
            if (linkClass != kWaypoint || linkType != kPedAI)
                continue;
            uint32_t linkAction;
            if (!fromStop && linked->ReadHashValue(kSkateAction, &linkAction, 0) && linkAction == kStopAction)
                continue;
            Maths::Vec4 linkPos;
            LevelNodes::FetchPlace(link, &linkPos);
            Maths::Vec4 outgoing = {m_nextNodePos.x - linkPos.x, m_nextNodePos.y - linkPos.y,
                                    m_nextNodePos.z - linkPos.z, m_nextNodePos.w - linkPos.w};
            double angle = Maths::ReadTurnAngle(incoming, outgoing);
            if (angle > kFullTurnDegrees)
                angle -= static_cast<double>(Truncate(angle * kInverseFullTurn)) * kFullTurnDegrees;
            if (angle > bestAngle) {
                bestAngle = static_cast<float>(angle);
                best = i;
            }
            if (!(angle >= minAngle))
                continue;
            uint32_t priority;
            linked->ReadHashValue(kPriority, &priority, 1);
            if (priority == kLow)
                lows[lowCount++] = i;
            else if (priority == kNormal)
                normals[normalCount++] = i;
        }
        if (normalCount == 0 && lowCount == 0) {
            chosen = LevelNodes::FetchLink(m_nextNode, best);
        } else {
            bool low = normalCount == 0;
            if (!low) {
                low = lowCount != 0;
                if (low) {
                    float probability = Scripts::FetchFloat("ped_low_priority_waypoint_probability");
                    int roll = Maths::Rnd(100);
                    low = !(static_cast<double>(probability) * kHundred < roll);
                }
            }
            if (low)
                chosen = LevelNodes::FetchLink(m_nextNode, lows[Maths::Rnd(lowCount)]);
            else
                chosen = LevelNodes::FetchLink(m_nextNode, normals[Maths::Rnd(normalCount)]);
        }
    }
    if (chosen) {
        m_nodeAfter = chosen;
        LevelNodes::FetchPlace(chosen, &m_nodeAfterPos);
    }
}

// 0x004EEFA0
void CPedestrianBrainModule::RunWaypointActions(Scripts::CStruct* node)
{
    // (Both values are read over the `node` argument's slot - missing, both are asserted, it keeps what it held: the
    // pointer's bits, or the percent read before.)
    uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    if (node->HasBareFlag(kChangePaceFlag) && Chance(node, kAdjustSpeedWeight)) {
        node->FetchFloat(kAdjustSpeedPercent, reinterpret_cast<float*>(&slot), 1);
        float percent = std::bit_cast<float>(slot);
        CKinematicsModule* motion = Motion(m_entity);
        motion->m_pathMaxSpeed = percent * motion->m_pathMaxSpeed;
    }
    if (node->HasBareFlag(kLaunchNodeScript) && Chance(node, kRunScriptWeight)) {
        node->ReadHashValue(kNodeScriptKey, &slot, 1);
        Scripts::CScript* script = Scripts::SpawnScript(slot, nullptr, 0, nullptr, 0, 0, false, false, false, false);
        script->m_entity.AssignValue(m_entity);
    }
}

// 0x004EFD50
Scripts::CStruct* CPedestrianBrainModule::LeapNodeParams(Scripts::CStruct* params, bool toNode)
{
    m_flagBits &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    m_vertRotation = 0.0f;
    m_vertRotationTarget = 0.0f;
    m_twistRate = 0.0f;
    Scripts::CStruct* out = StateParams(params);
    // (The height is read over the `params` argument: missing - it's asserted - it stays the pointer's bits.)
    float height = std::bit_cast<float>(reinterpret_cast<uintptr_t>(params));
    params->FetchFloat(kJumpHeight, &height, 1);
    float gravity = Scripts::FetchFloat(kLeapGravity);
    float jumpTime;
    int spin = 0;
    if (!toNode && !params->HasBareFlag(kJumpToNextNode)) {
        // Straight up: the take-off speed for the height, and the time up and down.
        out->PushFloat(kJumpSpeed, static_cast<float>(std::sqrt(static_cast<double>(gravity) * height * kMinusTwo)));
        double up = std::sqrt(static_cast<double>(gravity) * height * kMinusTwo);
        jumpTime = static_cast<float>(up + up);
    } else {
        // To the next node: up to its height (and a slop above it) and across to it.
        CGameEntity* object = m_entity;
        float dw = m_nextNodePos.w - object->m_atPoint.w;
        float dz = m_nextNodePos.z - object->m_atPoint.z;
        float dx = m_nextNodePos.x - object->m_atPoint.x;
        Maths::Vec4 across = {dx, 0.0f, dz, dw};
        if (static_cast<double>(height) + object->m_atPoint.y < m_nextNodePos.y) {
            double slop = Scripts::FetchFloat(kHeightSlop);
            height = static_cast<float>(slop + (static_cast<double>(m_nextNodePos.y) - object->m_atPoint.y));
        }
        double inverseGravity = 1.0 / gravity;
        double up = std::sqrt(static_cast<double>(height) * inverseGravity * kMinusTwo);
        float upSpeed = static_cast<float>(height / up - up * gravity * kHalf);
        float acrossSqr = static_cast<float>((static_cast<double>(across.y) * across.y +
                                              static_cast<double>(across.z) * across.z) +
                                             static_cast<double>(across.x) * across.x);
        double down = std::sqrt(std::fabs(((static_cast<double>(height) + m_entity->m_atPoint.y) - m_nextNodePos.y) *
                                          inverseGravity) * 2.0);
        jumpTime = static_cast<float>(down + up);
        const Maths::Vec4& direction = across.MakeUnit();
        double speedAcross = std::sqrt(static_cast<double>(acrossSqr)) / jumpTime;
        float ax = static_cast<float>(speedAcross * direction.x), ay = static_cast<float>(speedAcross * direction.y),
              az = static_cast<float>(speedAcross * direction.z);
        double aw = speedAcross * direction.w;
        float upX = static_cast<float>(static_cast<double>(upSpeed) * 0.0f), upZ = upX;
        double vx = static_cast<double>(upX) + ax, vy = static_cast<double>(upSpeed) + ay,
               vz = static_cast<double>(upZ) + az;
        float velX = static_cast<float>(vx), velY = static_cast<float>(vy), velZ = static_cast<float>(vz);
        Maths::Vec4 heading = {velX, velY, velZ, static_cast<float>(upSpeed + aw)};
        heading.MakeUnit();
        out->PutVec3(kHeading, Scripts::VectorValue{heading.x, heading.y, heading.z, heading.w});
        out->PushFloat(kJumpSpeed, static_cast<float>(std::sqrt((static_cast<double>(velX) * velX +
                                                                static_cast<double>(velZ) * velZ) +
                                                               static_cast<double>(velY) * velY)));
        out->PushFloat(kLeapDuration, jumpTime);
    }
    // The spin.
    if (params->HasBareFlag(kRandomSpin)) {
        int halfTurns = Truncate(jumpTime / static_cast<double>(Scripts::FetchFloat(kMinSpinTime)));
        if (halfTurns > 4)
            halfTurns = 4;
        spin = Maths::Rnd(halfTurns + 1) * 180;
    } else {
        float angle;
        if (params->FetchFloat(kSpinAngle, &angle, 0))
            spin = Truncate(static_cast<double>(angle) + kSpinRound);
    }
    spin = spin < 0 ? -spin : spin;
    if (spin > 0) {
        // (Read over a slot that held the distance across, squared: missing - it's asserted - that stays.)
        uint32_t direction = 0;
        params->ReadHashValue(kSpinDirection, &direction, 1);
        bool flag = static_cast<CAnimBlenderComponent*>(m_entity->FetchModule(kAnimBlender))->AllChannelsFlag29();
        if (direction == kBS) {
            if (flag)
                spin = -spin;
        } else if (direction == kFS) {
            if (!flag)
                spin = -spin;
        } else if (direction == kRand && Maths::Rnd(2) == 1) {
            spin = -spin;
        }
        m_spinAngle = spin;
        m_flagBits |= PED_FLAG_SPINNING;
    }
    // A spine transfer turns it round over the top.
    bool spine = params->HasBareFlag(kSpineTransfer);
    bool turn = false;
    if (spin % 360 == 0) {
        if (spine)
            turn = true;
        else
            out->PutNameKey(0u, kShouldFlip);
    } else if (spine) {
        out->PutNameKey(0u, kShouldFlip);
        turn = true;
    }
    if (turn) {
        m_flagBits |= PED_FLAG_VERT_ROTATING;
        const Maths::Vec4 up = {0.0f, 1.0f, 0.0f, 1.0f};
        float angle = static_cast<float>(Maths::ReadTurnAngle(m_savedMatrix.row[2], up));
        m_vertRotationTarget = static_cast<float>(static_cast<double>(Scripts::FetchFloat(kSpineSlop)) + angle);
    }
    if (m_flagBits & (PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING)) {
        m_jumpMatrix = m_savedMatrix;
        m_spinElapsed = 0.0f;
        m_spinTime = static_cast<float>(static_cast<double>(Scripts::FetchFloat(kVertTimeSlop)) * jumpTime);
    }
    if (params->HasBareFlag(kJumpToNextNode))
        out->PushFloat(kLandHeight, m_nextNodePos.y);
    return out;
}

// 0x004EFBC0
void CPedestrianBrainModule::LipTrickUpdate()
{
    Maths::Mat4 matrix = m_entity->m_displayMatrix;
    matrix.row[2] = {0.0f, 1.0f, 0.0f, 1.0f};
    const Maths::Vec4& up = m_upDir.MakeUnit();
    // Across: up x (0, 1, 0), normalised (x and z kept as floats too).
    float zero = static_cast<float>(static_cast<double>(up.y) * 0.0f);
    float upX = up.x, upZ = up.z;
    double sx = static_cast<double>(zero) - upZ;
    double sy = static_cast<double>(upZ) * 0.0f - static_cast<double>(upX) * 0.0f;
    double sz = static_cast<double>(upX) - zero;
    float sideX = static_cast<float>(sx), sideZ = static_cast<float>(sz);
    double length = std::sqrt((sz * sz + sy * sy) + sx * sx);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        sx *= inverse;
        sideX = static_cast<float>(sx);
        sy *= inverse;
        sz *= inverse;
        sideZ = static_cast<float>(sz);
    }
    matrix.row[0] = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz), 0.0f};
    // Up: (0, 1, 0) x across, normalised.
    float zeroY = static_cast<float>(sy * 0.0f);
    double a = static_cast<double>(sideZ) - zeroY;
    double b = static_cast<double>(sideX) * 0.0f - static_cast<double>(sideZ) * 0.0f;
    double c = static_cast<double>(zeroY) - sideX;
    float upRowZ = static_cast<float>(c);
    double length2 = std::sqrt((c * upRowZ + b * b) + a * a);
    if (length2 > 0.0) {
        double inverse = 1.0 / length2;
        a *= inverse;
        b *= inverse;
        upRowZ = static_cast<float>(upRowZ * inverse);
    }
    matrix.row[1] = {static_cast<float>(a), static_cast<float>(b), upRowZ, 0.0f};
    m_entity->m_displayMatrix = matrix;
}

// --- the states a node's SkateAction starts ---

// 0x004F1A90
void CPedestrianBrainModule::StartGrind(Scripts::CStruct* node)
{
    m_flagBits = (m_flagBits & ~(PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING)) | PED_FLAG_GRINDING;
    uint32_t terrain;
    if (node->ReadHashValue(kSurfaceKindKey, &terrain, 0))
        SetSoundTerrain(SurfaceKind::GetTerrainIndex(terrain));
    Scripts::CStruct* params = StateParams(node);
    ChangePhase(kStateGrind, kTable, nullptr); // (the parameters aren't passed)
    DeleteParams(params);
}

// 0x004F1B10
void CPedestrianBrainModule::EndGrind(Scripts::CStruct*)
{
    if (m_flagBits & PED_FLAG_GRINDING) {
        m_flagBits &= ~PED_FLAG_GRINDING;
        ChangePhase(kStateGrindOff, kTable, nullptr);
    }
}

// 0x004F1B40
void CPedestrianBrainModule::Jump(Scripts::CStruct* node, bool vert)
{
    m_flagBits &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    Scripts::CStruct* params;
    uint32_t state;
    if (vert) {
        m_flagBits &= ~PED_FLAG_MOVING;
        params = LeapNodeParams(node, vert);
        state = kStateJumpVert;
    } else {
        if (node->HasBareFlag(kJumpToNextNode)) {
            m_flagBits &= ~PED_FLAG_MOVING;
            params = LeapNodeParams(node, false);
            m_flagBits |= PED_FLAG_JUMPING_TO_NODE;
        } else {
            params = StateParams(node);
        }
        state = kStateJump;
    }
    ChangePhase(state, kTable, params);
    DeleteParams(params);
}

// 0x004F1BF0
void CPedestrianBrainModule::LipTrick(Scripts::CStruct* node)
{
    m_flagBits &= ~(PED_FLAG_MOVING | PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    LipTrickUpdate();
    Scripts::CStruct* params = StateParams(node);
    ChangePhase(kStateLip, kTable, params);
    DeleteParams(params);
}

// 0x004F1C40
void CPedestrianBrainModule::Manual(Scripts::CStruct* node)
{
    m_flagBits &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    Scripts::CStruct* params = StateParams(node);
    ChangePhase(kStateManual, kTable, params);
    DeleteParams(params);
}

// 0x004F1C90
void CPedestrianBrainModule::ManualDown(Scripts::CStruct* node)
{
    m_flagBits &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    Scripts::CStruct* params = StateParams(node);
    ChangePhase(kStateManualDown, kTable, params);
    DeleteParams(params);
}

// 0x004F1CE0
void CPedestrianBrainModule::GrabTrick(Scripts::CStruct* node, bool vert)
{
    Scripts::CStruct* params;
    if (vert) {
        m_flagBits &= ~PED_FLAG_MOVING;
        params = LeapNodeParams(node, vert);
        params->PutNameKey(0u, kIsVert);
    } else {
        params = StateParams(node);
        bool jumping = (Motion(m_entity)->m_motionFlags & 0x100) != 0;
        if (!jumping) {
            float speed = Scripts::FetchFloat(kJumpSpeedGlobal);
            double gravity = Scripts::FetchFloat(kLeapGravity);
            double time = std::sqrt(static_cast<double>(speed) * speed / (gravity * gravity));
            params->PutInt(kLeapDuration, Truncate(time + time));
        }
        params->PutInt(kIsJumping, jumping ? 1 : 0);
    }
    ChangePhase(kStateGrab, kTable, params);
    DeleteParams(params);
}

// 0x004F1DC0
void CPedestrianBrainModule::Halt(Scripts::CStruct* node)
{
    m_flagBits = (m_flagBits & ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING)) | PED_FLAG_STOPPING;
    Scripts::CStruct* params = StateParams(node);
    ChangePhase(kStateStop, kTable, params);
    DeleteParams(params);
}

// 0x004F1F30
void CPedestrianBrainModule::RollOff(Scripts::CStruct*) { ChangePhase(kStateRollOff, kTable, nullptr); }

// 0x004F1F50
void CPedestrianBrainModule::FlipTrick(Scripts::CStruct* node, bool vert)
{
    m_flagBits &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    CKinematicsModule* motion = Motion(m_entity);
    Scripts::CStruct* params = StateParams(node);
    Scripts::CStruct* jump = LeapNodeParams(node, vert);
    params->AppendStructureIfAny(jump);
    DeleteParams(jump);
    if (vert) {
        m_flagBits &= ~PED_FLAG_MOVING;
        params->PutNameKey(0u, kIsVert);
    }
    params->PutInt(kIsJumping, (motion->m_motionFlags & 0x100) ? 1 : 0);
    if (node->HasBareFlag(kJumpToNextNode))
        m_flagBits = (m_flagBits & ~PED_FLAG_MOVING) | PED_FLAG_JUMPING_TO_NODE;
    ChangePhase(kStateFlip, kTable, params);
    DeleteParams(params);
}

// 0x004F1E80
bool CPedestrianBrainModule::BailFromGrind()
{
    if (!(m_flagBits & PED_FLAG_GRINDING))
        return false;
    m_flagBits = (m_flagBits & ~PED_FLAG_GRINDING) | PED_FLAG_GRIND_BAILING;
    int count = static_cast<int>(LevelNodes::GetLinkCount(m_nextNode));
    if (count > 1) {
        // Off the grind the way its stop link goes.
        for (int i = 0; i < count; ++i) {
            uint32_t link = LevelNodes::FetchLink(m_nextNode, i);
            uint32_t action;
            if (LevelNodes::FetchVertex(link, true)->ReadHashValue(kSkateAction, &action, 0) && action == kStopAction) {
                m_nodeAfter = link;
                LevelNodes::FetchPlace(link, &m_nodeAfterPos);
                break;
            }
        }
    }
    ChangePhase(kStateBailGrind, kTable, nullptr);
    return true;
}

// 0x004F2340
void CPedestrianBrainModule::DoSkateAction(Scripts::CStruct* node)
{
    if (m_flagBits & (PED_FLAG_BAILING | PED_FLAG_GRIND_BAILING))
        return;
    uint32_t action;
    if (!node->ReadHashValue(kSkateAction, &action, 0))
        return;
    switch (action) {
    case kManualDown: ManualDown(node); break;
    case kVertLip: LipTrick(node); break;
    case kGrabTrick: GrabTrick(node, false); break;
    case kGrind:
    case kVertGrind: StartGrind(node); break;
    case kStop: Halt(node); break;
    case kJump: Jump(node, false); break;
    case kRollOff: RollOff(node); break;
    case kVertGrab: GrabTrick(node, true); break;
    case kFlipTrick: FlipTrick(node, false); break;
    case kGrindOff: EndGrind(node); break;
    case kVertJump: Jump(node, true); break;
    case kVertFlip: FlipTrick(node, true); break;
    case kManual: Manual(node); break;
    default: break;
    }
}

// 0x004F2570
void CPedestrianBrainModule::ReachWaypoint()
{
    uint32_t reached = m_nextNode, left = m_listLink;
    m_flagBits &= ~(PED_FLAG_VERT_LAST | PED_FLAG_VERT_AFTER);
    if (m_flagBits & PED_FLAG_VERT_NEXT)
        m_flagBits |= PED_FLAG_VERT_LAST;
    m_nextNode = m_nodeAfter;
    m_listLink = reached;
    m_flagBits &= ~PED_FLAG_JUMPING_TO_NODE;
    m_nodePos = m_nextNodePos;
    m_nextNodePos = m_nodeAfterPos;
    if (m_kind == 0 && left == m_nextNode)
        ActivateAuxScript(kHitDeadEnd, nullptr);
    m_flagBits &= ~PED_FLAG_VERT_NEXT;
    Scripts::CStruct* next = LevelNodes::FetchVertex(m_nextNode, true);
    m_flagBits &= ~PED_FLAG_VERT_LIP;
    uint32_t pedType;
    if (next->ReadHashValue(kPedType, &pedType, 0) && pedType == kSkate) {
        // (Read over a slot holding a stale value: SkateAction is asserted.)
        uint32_t action = 0;
        next->ReadHashValue(kSkateAction, &action, 1);
        if (IsVertNode(m_nextNode))
            m_flagBits |= PED_FLAG_VERT_NEXT;
        if (action == kVertGrab || action == kJump) {
            uint32_t reachedAction;
            if (LevelNodes::FetchVertex(m_listLink, true)->ReadHashValue(kSkateAction, &reachedAction, 1) &&
                reachedAction != kGrind && !(m_flagBits & (PED_FLAG_GRINDING | PED_FLAG_VERT_LAST)))
                m_flagBits |= PED_FLAG_CROUCH;
        }
        if (action == kVertLip)
            m_flagBits |= PED_FLAG_VERT_LIP;
    }
    ChooseNodeAfter();
    if (IsVertNode(m_nodeAfter))
        m_flagBits |= PED_FLAG_VERT_AFTER;
    Scripts::CStruct* leftNode = LevelNodes::FetchVertex(left, true);
    Scripts::CStruct* reachedNode = LevelNodes::FetchVertex(reached, true);
    RunWaypointActions(reachedNode);
    uint32_t leftAction;
    if (!leftNode->ReadHashValue(kSkateAction, &leftAction, 0) || leftAction != kVertGrab) {
        uint32_t reachedType;
        if (reachedNode->ReadHashValue(kPedType, &reachedType, 0) && reachedType == kSkate)
            DoSkateAction(reachedNode);
    }
    m_turnFrames = 0;
    m_turnFrame = 0;
}

// --- script member functions ---

// 0x004F0740
bool CPedestrianBrainModule::Ped_InitPath(Scripts::CStruct*, Scripts::CScript*)
{
    m_nextNode = LevelNodes::FetchLink(LevelNodes::FetchVertex(m_listLink, true), 0);
    LevelNodes::FetchPlace(m_nextNode, &m_nextNodePos);
    ChooseNodeAfter();
    m_flagBits |= PED_FLAG_MOVING;
    m_peds = CPathManager::Sole()->AddToExtraGroup(m_entity, m_nextNode);
    Motion(m_entity)->m_motionFlags |= 0x10;
    // Facing along the way to the first waypoint.
    float dx = m_nextNodePos.x - m_nodePos.x, dy = m_nextNodePos.y - m_nodePos.y, dz = m_nextNodePos.z - m_nodePos.z,
          dw = m_nextNodePos.w - m_nodePos.w;
    double length = std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    Maths::Mat4 matrix = m_entity->m_displayMatrix;
    matrix.row[2] = {dx, dy, dz, dw};
    matrix.SquareUpAxes(2);
    m_entity->m_displayMatrix = matrix;
    m_savedMatrix = matrix;
    for (Maths::Vec4& position : m_pastFrames)
        position = m_entity->m_atPoint;
    return true;
}

// 0x004F2770
bool CPedestrianBrainModule::Ped_HitWaypoint(Scripts::CStruct*, Scripts::CScript*)
{
    ReachWaypoint();
    return true;
}

// 0x004EDEC0
bool CPedestrianBrainModule::Ped_GetJumpToNextNode(Scripts::CStruct* params, Scripts::CScript* script)
{
    float height;
    params->FetchFloat(kHeight, &height, 1);
    // (The gravity is read over the `params` argument: missing - it's asserted - it stays the pointer's bits.)
    float gravity = std::bit_cast<float>(reinterpret_cast<uintptr_t>(params));
    params->FetchFloat(kGravity, &gravity, 1);
    const Maths::Vec4& at = m_entity->m_atPoint;
    float dx = m_nextNodePos.x - at.x, dy = m_nextNodePos.y - at.y, dz = m_nextNodePos.z - at.z,
          dw = m_nextNodePos.w - at.w;
    double rise = std::sqrt(static_cast<double>(height) / gravity * kMinusTwo);
    float time = static_cast<float>(rise + rise);
    float upSpeed = static_cast<float>(std::sqrt(static_cast<double>(gravity) * height * kMinusTwo));
    float distance = static_cast<float>(std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) +
                                                  static_cast<double>(dx) * dx));
    double across = Scripts::FetchFloat(kJumpSpeedScale) * (static_cast<double>(distance) * kHalf);
    if (distance > 0.0f) {
        double inverse = 1.0 / distance;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    double speedAcross = across / time;
    float ax = static_cast<float>(speedAcross * dx), ay = static_cast<float>(speedAcross * dy),
          az = static_cast<float>(speedAcross * dz), aw = static_cast<float>(speedAcross * dw);
    float upX = static_cast<float>(static_cast<double>(upSpeed) * 0.0f);
    double sx = static_cast<double>(upX) + ax;
    double syExact = static_cast<double>(upSpeed) + ay, szExact = static_cast<double>(upX) + az;
    float sy = static_cast<float>(syExact), sz = static_cast<float>(szExact);
    // (the speed from the sums as they are; the heading from the stored y and z)
    float speed = static_cast<float>(std::sqrt((szExact * szExact + syExact * syExact) + sx * sx));
    float sw = static_cast<float>(static_cast<double>(upSpeed) + aw);
    double length = std::sqrt((static_cast<double>(sz) * sz + static_cast<double>(sy) * sy) + sx * sx);
    double hx = sx, hy = sy;
    float hz = sz;
    if (length > 0.0) {
        double inverse = 1.0 / length;
        hx = sx * inverse;
        hy = sy * inverse;
        hz = static_cast<float>(sz * inverse);
    }
    script->m_args->PutVec3(kHeading, Scripts::VectorValue{static_cast<float>(hx), static_cast<float>(hy), hz, sw});
    script->m_args->PushFloat(kJumpSpeed, speed);
    script->m_args->PushFloat(kLeapDuration, time);
    return true;
}

} // namespace Obj
