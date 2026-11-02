// The PedLogic component (GameObjects::PedestrianBrainModule): see ped_logic.h. Its path - the waypoints, the choice of the node
// after next, each waypoint's actions - and the skating peds' tricks (the states a node's SkateAction starts, the
// jumps' parameters); from the disassembly of 0x004ED0E0-0x004F2780 (0x004EDEC0, 0x004F1E80 and 0x004F2770 aren't
// Ghidra functions). The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include <bit>
#include <cmath>
#include <cstdint>
#include <array>

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
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace objects {

namespace {

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kAnimBlender = 0x7149B753;

constexpr uint32_t kSkateAction = 0x963DC198;                   // SkateAction
constexpr uint32_t kStopAction = 0xE41199AF;                    // (a SkateAction: the end of a grind's way off)
constexpr uint32_t kVertGrab = 0x7D9D0008;                      // Vert_Grab
constexpr uint32_t kVertLip = 0x554DBD64;                       // Vert_Lip
constexpr uint32_t kVertGrind = 0xE6DFAEC7;                     // Vert_Grind
constexpr uint32_t kVertJump = 0xD5B4F014;                      // Vert_Jump
constexpr uint32_t kVertFlip = 0xE8F91257;                      // Vert_Flip
constexpr uint32_t kVertOther = 0xDA0723DA;                     // (a vert SkateAction; its name isn't known)
constexpr uint32_t kGrind = 0x255ED86F;                         // Grind
constexpr uint32_t kJump = 0x584CF9E9;                          // Jump
constexpr uint32_t kManual = 0xEF24413B;                        // Manual
constexpr uint32_t kManualDown = 0x9403A0ED;                    // Manual_Down
constexpr uint32_t kGrabTrick = 0x12797A1B;                     // Grab_Trick
constexpr uint32_t kFlipTrick = 0xBA1D1E94;                     // Flip_Trick
constexpr uint32_t kStop = 0x46A9E949;                          // stop
constexpr uint32_t kRollOff = 0x6D3144BF;                       // Roll_off
constexpr uint32_t kGrindOff = 0xC870BCE5;                      // (a SkateAction: off the grind)
constexpr uint32_t kClass = 0x12B4E660, kWaypoint = 0x4C23A77E; // Class: Waypoint
constexpr uint32_t kTypeName = 0x7321A8D6, kPedAi = 0xCBA10FFA; // Type: PedAI
constexpr uint32_t kPriority = 0x9D5923D8, kLow = 0x6D77875E, kNormal = 0xDE7A971B; // Priority: Low / Normal
constexpr uint32_t kMinInnerPathAngle = 0xFE65D822;                                 // ped_min_inner_path_angle
constexpr uint32_t kPedType = 0x17EA37FB, kSkate = 0x54166ACD;                      // PedType: Skate
constexpr uint32_t kHitDeadEnd = 0xA80F965E;     // ped_walker_hit_dead_end (an aux script)
constexpr uint32_t kChangePaceFlag = 0x74E4BBA5; // (a node flag: its speed changed)
constexpr uint32_t kAdjustSpeedWeight = 0x922C5B59;
constexpr uint32_t kAdjustSpeedPercent = 0xA04D16EC; // AdjustSpeedPercent
constexpr uint32_t kLaunchNodeScript = 0x55D98ED7;   // launch_script
constexpr uint32_t kRunScriptWeight = 0x86C9CCCE;    // RunScriptWeight
constexpr uint32_t kNodeScriptKey = 0x64B4CD9D;      // RunScriptName
constexpr uint32_t kSurfaceKindKey = 0x54CF8532;     // TerrainType
constexpr uint32_t kTable = 0x46196541;              // SkaterPed_StateTable
constexpr uint32_t kStateGrind = 0x57CD5C63, kStateGrindOff = 0x6DB75D2A, kStateJumpVert = 0x5DC4A15F,
                   kStateJump = 0xCCAF702F, kStateLip = 0x4E7AA8C2, kStateManual = 0xE6E09E94,
                   kStateManualDown = 0xEF03188F, kStateGrab = 0x423331C0, kStateStop = 0xD24A608F,
                   kStateRollOff = 0x5F69700C, kStateFlip = 0xD757239F, kStateBailGrind = 0x423431E1;
constexpr uint32_t kIsVert = 0x01615618;                                   // is_vert
constexpr uint32_t kIsJumping = 0x65B5788D;                                // is_jumping
constexpr uint32_t kJumpToNextNode = 0xB02567AE;                           // JumpToNextNode
constexpr uint32_t kJumpHeight = 0x838DA447;                               // JumpHeight
constexpr uint32_t kLeapGravity = 0xE224A5D3;                              // ped_skater_jump_gravity
constexpr uint32_t kJumpSpeedGlobal = 0xF1FF758A;                          // ped_skater_jump_speed
constexpr uint32_t kJumpSpeed = 0x1C4C5690;                                // jumpSpeed
constexpr uint32_t kLeapDuration = 0x66CED87B;                             // jump_time
constexpr uint32_t kHeading = 0xFD4BC03E;                                  // heading
constexpr uint32_t kHeightSlop = 0x23F14B13;                               // ped_skater_jump_to_next_node_height_slop
constexpr uint32_t kRandomSpin = 0xB4077854;                               // (a flag: a random spin, in half turns)
constexpr uint32_t kMinSpinTime = 0x8B381AB1;                              // ped_skater_min_180_spin_time
constexpr uint32_t kSpinAngle = 0x96FB50D9;                                // SpinAngle
constexpr uint32_t kSpinDirection = 0xEF0B71A0;                            // SpinDirection
constexpr uint32_t kBs = 0x20E1C4A3, kFs = 0x448D01A7, kRand = 0xE7390A8B; // BS / FS / Rand
constexpr uint32_t kSpineTransfer = 0x04AA0CFF;                            // SpineTransfer
constexpr uint32_t kShouldFlip = 0x8868E76A;                               // should_flip
constexpr uint32_t kSpineSlop = 0x95E91D23;                                // ped_skater_spine_rotation_slop
constexpr uint32_t kVertTimeSlop = 0xC1DFC97B;                             // ped_skater_vert_rotation_time_slop
constexpr uint32_t kLandHeight = 0xA1810C27;                               // land_height
constexpr uint32_t kHeight = 0x0AB21AF0, kGravity = 0xA5E2DA58;            // height / gravity
constexpr uint32_t kJumpSpeedScale = 0x2B5BEDBA; // (a global float: the jump's speed along the ground)

constexpr float kFullTurnDegrees = 360.0f;        // 0x00645EA8
constexpr float kInverseFullTurn = 1.0f / 360.0f; // 0x0064B798
constexpr float kAlwaysAbove = 0.99f;             // 0x00645450
constexpr float kThousand = 1000.0f;              // 0x006453C0
constexpr float kHundred = 100.0f;                // 0x006463D4
constexpr float kMinusTwo = -2.0f;                // 0x00646970
constexpr float kHalf = 0.5f;                     // 0x006453BC
constexpr float kSpinRound = 0.01f;               // 0x0064544C

KinematicsModule* get_motion(GameEntity* object)
{
    return core::down_cast<KinematicsModule>(object->fetch_module(kMotion));
}


// _ftol2: a 64-bit truncation whose low half is used (the integer indefinite, 2^63 - a NaN, what doesn't fit - gives
// 0).
int truncate(double value)
{
    if (!(std::fabs(value) < 9.2e18))
        return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

} // namespace

// 0x004ED080
bool PedestrianBrainModule::is_vert_node(uint32_t node)
{
    // The action is read into the argument's own slot; a missing node is read through a null (original).
    uint32_t action = node;
    if (!level_nodes::fetch_vertex(node, true)->read_hash_value(kSkateAction, &action, 0))
        return false;
    return action == kVertGrab || action == kVertLip || action == kVertGrind || action == kVertOther ||
           action == kVertFlip || action == kVertJump;
}

// 0x004ED0E0
bool PedestrianBrainModule::get_chance(script::Struct* node, uint32_t weight_name)
{
    // (The weight is read over the `weight_name` argument: missing - it's asserted - it stays the name's bits.)
    auto weight = std::bit_cast<float>(weight_name);
    node->fetch_float(weight_name, &weight, 1);
    if (weight > kAlwaysAbove)
        return true;
    const int roll = maths::rnd(1000);
    return static_cast<double>(weight) * kThousand > roll;
}

// 0x004ED8A0
void PedestrianBrainModule::choose_node_after()
{
    bool from_stop = false;
    uint32_t action = 0;
    if (level_nodes::fetch_vertex(list_link_, true)->read_hash_value(kSkateAction, &action, 0) && action == kStopAction)
        from_stop = true;
    const int count = static_cast<int>(level_nodes::get_link_count(next_node_));
    uint32_t chosen = 0;
    if (count == 0) {
        chosen = list_link_;
    } else if (count == 1) {
        chosen = level_nodes::fetch_link(next_node_, 0);
        if (chosen == list_link_) {
            // A dead end, unless the node is a vert grab: back the way it came.
            uint32_t next_action = 0;
            if (level_nodes::fetch_vertex(next_node_, true)->read_hash_value(kSkateAction, &next_action, 0) &&
                next_action == kVertGrab)
                chosen = next_node_;
        }
    } else {
        const maths::Vec4 incoming = {next_node_pos_.x - node_pos_.x, next_node_pos_.y - node_pos_.y,
                                      next_node_pos_.z - node_pos_.z, next_node_pos_.w - node_pos_.w};
        std::array<int, 20> lows{};
        std::array<int, 20> normals{};
        for (int i = 0; i < count; ++i) {
            core::at(normals, i) = i;
            core::at(lows, i) = i;
        }
        int low_count = 0, normal_count = 0;
        const int min_angle = script::fetch_int_value(kMinInnerPathAngle);
        float best_angle = 0.0f;
        int best = 0;
        for (int i = 0; i < count; ++i) {
            const uint32_t link = level_nodes::fetch_link(next_node_, i);
            if (link == list_link_)
                continue;
            script::Struct* linked = level_nodes::fetch_vertex(link, true);
            uint32_t link_class = 0, link_type = 0;
            linked->read_hash_value(kClass, &link_class, 0);
            linked->read_hash_value(kTypeName, &link_type, 0);
            if (link_class != kWaypoint || link_type != kPedAi)
                continue;
            uint32_t link_action = 0;
            if (!from_stop && linked->read_hash_value(kSkateAction, &link_action, 0) && link_action == kStopAction)
                continue;
            maths::Vec4 link_pos;
            level_nodes::fetch_place(link, &link_pos);
            const maths::Vec4 outgoing = {next_node_pos_.x - link_pos.x, next_node_pos_.y - link_pos.y,
                                          next_node_pos_.z - link_pos.z, next_node_pos_.w - link_pos.w};
            double angle = maths::read_turn_angle(incoming, outgoing);
            if (angle > kFullTurnDegrees)
                angle -= static_cast<double>(truncate(angle * kInverseFullTurn)) * kFullTurnDegrees;
            if (angle > best_angle) {
                best_angle = static_cast<float>(angle);
                best = i;
            }
            if (!(angle >= min_angle))
                continue;
            uint32_t priority = 0;
            linked->read_hash_value(kPriority, &priority, 1);
            if (priority == kLow)
                core::at(lows, low_count++) = i;
            else if (priority == kNormal)
                core::at(normals, normal_count++) = i;
        }
        if (normal_count == 0 && low_count == 0) {
            chosen = level_nodes::fetch_link(next_node_, best);
        } else {
            bool low = normal_count == 0;
            if (!low) {
                low = low_count != 0;
                if (low) {
                    const float probability = script::fetch_float("ped_low_priority_waypoint_probability");
                    const int roll = maths::rnd(100);
                    low = !(static_cast<double>(probability) * kHundred < roll);
                }
            }
            if (low)
                chosen = level_nodes::fetch_link(next_node_, core::at(lows, maths::rnd(low_count)));
            else
                chosen = level_nodes::fetch_link(next_node_, core::at(normals, maths::rnd(normal_count)));
        }
    }
    if (chosen) {
        node_after_ = chosen;
        level_nodes::fetch_place(chosen, &node_after_pos_);
    }
}

// 0x004EEFA0
void PedestrianBrainModule::run_waypoint_actions(script::Struct* node)
{
    // (Both values are read over the `node` argument's slot - missing, both are asserted, it keeps what it held: the
    // pointer's bits, or the percent read before.)
    auto slot = static_cast<uint32_t>(cell::address(node));
    if (node->has_bare_flag(kChangePaceFlag) && get_chance(node, kAdjustSpeedWeight)) {
        node->fetch_float(kAdjustSpeedPercent, raw::as<float>(&slot), 1);
        auto percent = std::bit_cast<float>(slot);
        KinematicsModule* motion = get_motion(entity_);
        motion->path_max_speed_ = percent * motion->path_max_speed_;
    }
    if (node->has_bare_flag(kLaunchNodeScript) && get_chance(node, kRunScriptWeight)) {
        node->read_hash_value(kNodeScriptKey, &slot, 1);
        script::Script* script = script::spawn_script(slot, nullptr, 0, nullptr, 0, 0, false, false, false, false);
        script->entity_.assign_value(entity_);
    }
}

// 0x004EFD50
script::Struct* PedestrianBrainModule::leap_node_params(script::Struct* params, bool to_node)
{
    flag_bits_ &= ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    vert_rotation_ = 0.0f;
    vert_rotation_target_ = 0.0f;
    twist_rate_ = 0.0f;
    gsl::owner<script::Struct*> out = get_state_params(params);
    // (The height is read over the `params` argument: missing - it's asserted - it stays the pointer's bits.)
    float height = 0.0f; // (missing: 0 - the original read an argument's stack slot, an address)
    params->fetch_float(kJumpHeight, &height, 1);
    const float gravity = script::fetch_float(kLeapGravity);
    float jump_time = 0.0f;
    int spin = 0;
    if (!to_node && !params->has_bare_flag(kJumpToNextNode)) {
        // Straight up: the take-off speed for the height, and the time up and down.
        out->push_float(kJumpSpeed, static_cast<float>(std::sqrt(static_cast<double>(gravity) * height * kMinusTwo)));
        const double up = std::sqrt(static_cast<double>(gravity) * height * kMinusTwo);
        jump_time = static_cast<float>(up + up);
    } else {
        // To the next node: up to its height (and a slop above it) and across to it.
        const GameEntity* object = entity_;
        const float dw = next_node_pos_.w - object->at_point_.w;
        const float dz = next_node_pos_.z - object->at_point_.z;
        const float dx = next_node_pos_.x - object->at_point_.x;
        maths::Vec4 across = {dx, 0.0f, dz, dw};
        if (static_cast<double>(height) + object->at_point_.y < next_node_pos_.y) {
            const double slop = script::fetch_float(kHeightSlop);
            height = static_cast<float>(slop + (static_cast<double>(next_node_pos_.y) - object->at_point_.y));
        }
        const double inverse_gravity = 1.0 / gravity;
        const double up = std::sqrt(static_cast<double>(height) * inverse_gravity * kMinusTwo);
        auto up_speed = static_cast<float>(height / up - up * gravity * kHalf);
        auto across_sqr =
            static_cast<float>((static_cast<double>(across.y) * across.y + static_cast<double>(across.z) * across.z) +
                               static_cast<double>(across.x) * across.x);
        const double down = std::sqrt(
            std::fabs(((static_cast<double>(height) + entity_->at_point_.y) - next_node_pos_.y) * inverse_gravity) *
            2.0);
        jump_time = static_cast<float>(down + up);
        const maths::Vec4& direction = across.make_unit();
        const double speed_across = std::sqrt(static_cast<double>(across_sqr)) / jump_time;
        auto ax = static_cast<float>(speed_across * direction.x), ay = static_cast<float>(speed_across * direction.y),
             az = static_cast<float>(speed_across * direction.z);
        const double aw = speed_across * direction.w;
        const auto up_x = static_cast<float>(static_cast<double>(up_speed) * 0.0f);
        const float up_z = up_x;
        const double vx = static_cast<double>(up_x) + ax;
        const double vy = static_cast<double>(up_speed) + ay;
        const double vz = static_cast<double>(up_z) + az;
        auto vel_x = static_cast<float>(vx), vel_y = static_cast<float>(vy), vel_z = static_cast<float>(vz);
        maths::Vec4 heading = {vel_x, vel_y, vel_z, static_cast<float>(up_speed + aw)};
        heading.make_unit();
        out->put_vec3(kHeading, script::VectorValue{heading.x, heading.y, heading.z, heading.w});
        out->push_float(kJumpSpeed, static_cast<float>(std::sqrt(
                                        (static_cast<double>(vel_x) * vel_x + static_cast<double>(vel_z) * vel_z) +
                                        static_cast<double>(vel_y) * vel_y)));
        out->push_float(kLeapDuration, jump_time);
    }
    // The spin.
    if (params->has_bare_flag(kRandomSpin)) {
        int half_turns = truncate(jump_time / static_cast<double>(script::fetch_float(kMinSpinTime)));
        if (half_turns > 4)
            half_turns = 4;
        spin = maths::rnd(half_turns + 1) * 180;
    } else {
        float angle = 0.0f;
        if (params->fetch_float(kSpinAngle, &angle, 0))
            spin = truncate(static_cast<double>(angle) + kSpinRound);
    }
    spin = spin < 0 ? -spin : spin;
    if (spin > 0) {
        // (Read over a slot that held the distance across, squared: missing - it's asserted - that stays.)
        uint32_t direction = 0;
        params->read_hash_value(kSpinDirection, &direction, 1);
        const bool flag =
            core::down_cast<AnimBlenderComponent>(entity_->fetch_module(kAnimBlender))->all_channels_flag29();
        if (direction == kBs) {
            if (flag)
                spin = -spin;
        } else if (direction == kFs) {
            if (!flag)
                spin = -spin;
        } else if (direction == kRand && maths::rnd(2) == 1) {
            spin = -spin;
        }
        spin_angle_ = spin;
        flag_bits_ |= kPedFlagSpinning;
    }
    // A spine transfer turns it round over the top.
    const bool spine = params->has_bare_flag(kSpineTransfer);
    bool turn = false;
    if (spin % 360 == 0) {
        if (spine)
            turn = true;
        else
            out->put_name_key(0u, kShouldFlip);
    } else if (spine) {
        out->put_name_key(0u, kShouldFlip);
        turn = true;
    }
    if (turn) {
        flag_bits_ |= kPedFlagVertRotating;
        const maths::Vec4 up = {0.0f, 1.0f, 0.0f, 1.0f};
        auto angle = static_cast<float>(maths::read_turn_angle(core::at(saved_matrix_.row, 2), up));
        vert_rotation_target_ = static_cast<float>(static_cast<double>(script::fetch_float(kSpineSlop)) + angle);
    }
    if (flag_bits_ & (kPedFlagSpinning | kPedFlagVertRotating)) {
        jump_matrix_ = saved_matrix_;
        spin_elapsed_ = 0.0f;
        spin_time_ = static_cast<float>(static_cast<double>(script::fetch_float(kVertTimeSlop)) * jump_time);
    }
    if (params->has_bare_flag(kJumpToNextNode))
        out->push_float(kLandHeight, next_node_pos_.y);
    return out;
}

// 0x004EFBC0
void PedestrianBrainModule::lip_trick_update()
{
    maths::Mat4 matrix = entity_->display_matrix_;
    core::at(matrix.row, 2) = {0.0f, 1.0f, 0.0f, 1.0f};
    const maths::Vec4& up = up_dir_.make_unit();
    // Across: up x (0, 1, 0), normalised (x and z kept as floats too).
    auto zero = static_cast<float>(static_cast<double>(up.y) * 0.0f);
    const float up_x = up.x;
    const float up_z = up.z;
    double sx = static_cast<double>(zero) - up_z;
    double sy = static_cast<double>(up_z) * 0.0f - static_cast<double>(up_x) * 0.0f;
    double sz = static_cast<double>(up_x) - zero;
    auto side_x = static_cast<float>(sx), side_z = static_cast<float>(sz);
    const double length = std::sqrt((sz * sz + sy * sy) + sx * sx);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        sx *= inverse;
        side_x = static_cast<float>(sx);
        sy *= inverse;
        sz *= inverse;
        side_z = static_cast<float>(sz);
    }
    core::at(matrix.row, 0) = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz), 0.0f};
    // Up: (0, 1, 0) x across, normalised.
    auto zero_y = static_cast<float>(sy * 0.0f);
    double a = static_cast<double>(side_z) - zero_y;
    double b = static_cast<double>(side_x) * 0.0f - static_cast<double>(side_z) * 0.0f;
    const double c = static_cast<double>(zero_y) - side_x;
    auto up_row_z = static_cast<float>(c);
    const double length2 = std::sqrt((c * up_row_z + b * b) + a * a);
    if (length2 > 0.0) {
        const double inverse = 1.0 / length2;
        a *= inverse;
        b *= inverse;
        up_row_z = static_cast<float>(up_row_z * inverse);
    }
    core::at(matrix.row, 1) = {static_cast<float>(a), static_cast<float>(b), up_row_z, 0.0f};
    entity_->display_matrix_ = matrix;
}

// --- the states a node's SkateAction starts ---

// 0x004F1A90
void PedestrianBrainModule::start_grind(script::Struct* node)
{
    flag_bits_ = (flag_bits_ & ~(kPedFlagSpinning | kPedFlagVertRotating)) | kPedFlagGrinding;
    uint32_t terrain = 0;
    if (node->read_hash_value(kSurfaceKindKey, &terrain, 0))
        set_sound_terrain(surface_kind::get_terrain_index(terrain));
    gsl::owner<script::Struct*> params = get_state_params(node);
    change_phase(kStateGrind, kTable, nullptr); // (the parameters aren't passed)
    script::free_struct(params);
}

// 0x004F1B10
void PedestrianBrainModule::end_grind(script::Struct*)
{
    if (flag_bits_ & kPedFlagGrinding) {
        flag_bits_ &= ~kPedFlagGrinding;
        change_phase(kStateGrindOff, kTable, nullptr);
    }
}

// 0x004F1B40
void PedestrianBrainModule::get_jump(script::Struct* node, bool vert)
{
    flag_bits_ &= ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    script::Struct* params = nullptr;
    uint32_t state = 0;
    if (vert) {
        flag_bits_ &= ~kPedFlagMoving;
        params = leap_node_params(node, vert);
        state = kStateJumpVert;
    } else {
        if (node->has_bare_flag(kJumpToNextNode)) {
            flag_bits_ &= ~kPedFlagMoving;
            params = leap_node_params(node, false);
            flag_bits_ |= kPedFlagJumpingToNode;
        } else {
            params = get_state_params(node);
        }
        state = kStateJump;
    }
    change_phase(state, kTable, params);
    script::free_struct(params);
}

// 0x004F1BF0
void PedestrianBrainModule::get_lip_trick(script::Struct* node)
{
    flag_bits_ &= ~(kPedFlagMoving | kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    lip_trick_update();
    gsl::owner<script::Struct*> params = get_state_params(node);
    change_phase(kStateLip, kTable, params);
    script::free_struct(params);
}

// 0x004F1C40
void PedestrianBrainModule::get_manual(script::Struct* node)
{
    flag_bits_ &= ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    gsl::owner<script::Struct*> params = get_state_params(node);
    change_phase(kStateManual, kTable, params);
    script::free_struct(params);
}

// 0x004F1C90
void PedestrianBrainModule::manual_down(script::Struct* node)
{
    flag_bits_ &= ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    gsl::owner<script::Struct*> params = get_state_params(node);
    change_phase(kStateManualDown, kTable, params);
    script::free_struct(params);
}

// 0x004F1CE0
void PedestrianBrainModule::grab_trick(script::Struct* node, bool vert)
{
    script::Struct* params = nullptr;
    if (vert) {
        flag_bits_ &= ~kPedFlagMoving;
        params = leap_node_params(node, vert);
        params->put_name_key(0u, kIsVert);
    } else {
        params = get_state_params(node);
        const bool jumping = (get_motion(entity_)->motion_flags_ & 0x100) != 0;
        if (!jumping) {
            const float speed = script::fetch_float(kJumpSpeedGlobal);
            const double gravity = script::fetch_float(kLeapGravity);
            const double time = std::sqrt(static_cast<double>(speed) * speed / (gravity * gravity));
            params->put_int(kLeapDuration, truncate(time + time));
        }
        params->put_int(kIsJumping, jumping ? 1 : 0);
    }
    change_phase(kStateGrab, kTable, params);
    script::free_struct(params);
}

// 0x004F1DC0
void PedestrianBrainModule::halt(script::Struct* node)
{
    flag_bits_ = (flag_bits_ & ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating)) | kPedFlagStopping;
    gsl::owner<script::Struct*> params = get_state_params(node);
    change_phase(kStateStop, kTable, params);
    script::free_struct(params);
}

// 0x004F1F30
void PedestrianBrainModule::roll_off(script::Struct*) { change_phase(kStateRollOff, kTable, nullptr); }

// 0x004F1F50
void PedestrianBrainModule::flip_trick(script::Struct* node, bool vert)
{
    flag_bits_ &= ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    const KinematicsModule* motion = get_motion(entity_);
    gsl::owner<script::Struct*> params = get_state_params(node);
    script::Struct* jump = leap_node_params(node, vert);
    params->append_structure_if_any(jump);
    script::free_struct(jump);
    if (vert) {
        flag_bits_ &= ~kPedFlagMoving;
        params->put_name_key(0u, kIsVert);
    }
    params->put_int(kIsJumping, (motion->motion_flags_ & 0x100) ? 1 : 0);
    if (node->has_bare_flag(kJumpToNextNode))
        flag_bits_ = (flag_bits_ & ~kPedFlagMoving) | kPedFlagJumpingToNode;
    change_phase(kStateFlip, kTable, params);
    script::free_struct(params);
}

// 0x004F1E80
bool PedestrianBrainModule::bail_from_grind()
{
    if (!(flag_bits_ & kPedFlagGrinding))
        return false;
    flag_bits_ = (flag_bits_ & ~kPedFlagGrinding) | kPedFlagGrindBailing;
    const int count = static_cast<int>(level_nodes::get_link_count(next_node_));
    if (count > 1) {
        // Off the grind the way its stop link goes.
        for (int i = 0; i < count; ++i) {
            const uint32_t link = level_nodes::fetch_link(next_node_, i);
            uint32_t action = 0;
            if (level_nodes::fetch_vertex(link, true)->read_hash_value(kSkateAction, &action, 0) &&
                action == kStopAction) {
                node_after_ = link;
                level_nodes::fetch_place(link, &node_after_pos_);
                break;
            }
        }
    }
    change_phase(kStateBailGrind, kTable, nullptr);
    return true;
}

// 0x004F2340
void PedestrianBrainModule::do_skate_action(script::Struct* node)
{
    if (flag_bits_ & (kPedFlagBailing | kPedFlagGrindBailing))
        return;
    uint32_t action = 0;
    if (!node->read_hash_value(kSkateAction, &action, 0))
        return;
    switch (action) {
    case kManualDown:
        manual_down(node);
        break;
    case kVertLip:
        get_lip_trick(node);
        break;
    case kGrabTrick:
        grab_trick(node, false);
        break;
    case kGrind:
    case kVertGrind:
        start_grind(node);
        break;
    case kStop:
        halt(node);
        break;
    case kJump:
        get_jump(node, false);
        break;
    case kRollOff:
        roll_off(node);
        break;
    case kVertGrab:
        grab_trick(node, true);
        break;
    case kFlipTrick:
        flip_trick(node, false);
        break;
    case kGrindOff:
        end_grind(node);
        break;
    case kVertJump:
        get_jump(node, true);
        break;
    case kVertFlip:
        flip_trick(node, true);
        break;
    case kManual:
        get_manual(node);
        break;
    default:
        break;
    }
}

// 0x004F2570
void PedestrianBrainModule::reach_waypoint()
{
    const uint32_t reached = next_node_;
    const uint32_t left = list_link_;
    flag_bits_ &= ~(kPedFlagVertLast | kPedFlagVertAfter);
    if (flag_bits_ & kPedFlagVertNext)
        flag_bits_ |= kPedFlagVertLast;
    next_node_ = node_after_;
    list_link_ = reached;
    flag_bits_ &= ~kPedFlagJumpingToNode;
    node_pos_ = next_node_pos_;
    next_node_pos_ = node_after_pos_;
    if (kind_ == 0 && left == next_node_)
        activate_aux_script(kHitDeadEnd, nullptr);
    flag_bits_ &= ~kPedFlagVertNext;
    script::Struct* next = level_nodes::fetch_vertex(next_node_, true);
    flag_bits_ &= ~kPedFlagVertLip;
    uint32_t ped_type = 0;
    if (next->read_hash_value(kPedType, &ped_type, 0) && ped_type == kSkate) {
        // (Read over a slot holding a stale value: SkateAction is asserted.)
        uint32_t action = 0;
        next->read_hash_value(kSkateAction, &action, 1);
        if (is_vert_node(next_node_))
            flag_bits_ |= kPedFlagVertNext;
        if (action == kVertGrab || action == kJump) {
            uint32_t reached_action = 0;
            if (level_nodes::fetch_vertex(list_link_, true)->read_hash_value(kSkateAction, &reached_action, 1) &&
                reached_action != kGrind && !(flag_bits_ & (kPedFlagGrinding | kPedFlagVertLast)))
                flag_bits_ |= kPedFlagCrouch;
        }
        if (action == kVertLip)
            flag_bits_ |= kPedFlagVertLip;
    }
    choose_node_after();
    if (is_vert_node(node_after_))
        flag_bits_ |= kPedFlagVertAfter;
    script::Struct* left_node = level_nodes::fetch_vertex(left, true);
    script::Struct* reached_node = level_nodes::fetch_vertex(reached, true);
    run_waypoint_actions(reached_node);
    uint32_t left_action = 0;
    if (!left_node->read_hash_value(kSkateAction, &left_action, 0) || left_action != kVertGrab) {
        uint32_t reached_type = 0;
        if (reached_node->read_hash_value(kPedType, &reached_type, 0) && reached_type == kSkate)
            do_skate_action(reached_node);
    }
    turn_frames_ = 0;
    turn_frame_ = 0;
}

// --- script member functions ---

// 0x004F0740
bool PedestrianBrainModule::ped_init_path(script::Struct*, script::Script*)
{
    next_node_ = level_nodes::fetch_link(level_nodes::fetch_vertex(list_link_, true), 0);
    level_nodes::fetch_place(next_node_, &next_node_pos_);
    choose_node_after();
    flag_bits_ |= kPedFlagMoving;
    peds_ = PathManager::sole()->add_to_extra_group(entity_, next_node_);
    get_motion(entity_)->motion_flags_ |= 0x10;
    // Facing along the way to the first waypoint.
    float dx = next_node_pos_.x - node_pos_.x, dy = next_node_pos_.y - node_pos_.y, dz = next_node_pos_.z - node_pos_.z,
          dw = next_node_pos_.w - node_pos_.w;
    const double length =
        std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    maths::Mat4 matrix = entity_->display_matrix_;
    core::at(matrix.row, 2) = {dx, dy, dz, dw};
    matrix.square_up_axes(2);
    entity_->display_matrix_ = matrix;
    saved_matrix_ = matrix;
    for (maths::Vec4& position : past_frames_)
        position = entity_->at_point_;
    return true;
}

// 0x004F2770
bool PedestrianBrainModule::ped_hit_waypoint(script::Struct*, script::Script*)
{
    reach_waypoint();
    return true;
}

// 0x004EDEC0
bool PedestrianBrainModule::ped_get_jump_to_next_node(script::Struct* params, script::Script* script)
{
    float height = 0.0f;
    params->fetch_float(kHeight, &height, 1);
    // (The gravity is read over the `params` argument: missing - it's asserted - it stays the pointer's bits.)
    float gravity = 0.0f; // (missing: 0 - the original read an argument's stack slot, an address)
    params->fetch_float(kGravity, &gravity, 1);
    const maths::Vec4& at = entity_->at_point_;
    float dx = next_node_pos_.x - at.x, dy = next_node_pos_.y - at.y, dz = next_node_pos_.z - at.z,
          dw = next_node_pos_.w - at.w;
    const double rise = std::sqrt(static_cast<double>(height) / gravity * kMinusTwo);
    auto time = static_cast<float>(rise + rise);
    auto up_speed = static_cast<float>(std::sqrt(static_cast<double>(gravity) * height * kMinusTwo));
    auto distance = static_cast<float>(
        std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx));
    const double across = script::fetch_float(kJumpSpeedScale) * (static_cast<double>(distance) * kHalf);
    if (distance > 0.0f) {
        const double inverse = 1.0 / distance;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    const double speed_across = across / time;
    auto ax = static_cast<float>(speed_across * dx), ay = static_cast<float>(speed_across * dy),
         az = static_cast<float>(speed_across * dz), aw = static_cast<float>(speed_across * dw);
    auto up_x = static_cast<float>(static_cast<double>(up_speed) * 0.0f);
    const double sx = static_cast<double>(up_x) + ax;
    const double sy_exact = static_cast<double>(up_speed) + ay;
    const double sz_exact = static_cast<double>(up_x) + az;
    auto sy = static_cast<float>(sy_exact), sz = static_cast<float>(sz_exact);
    // (the speed from the sums as they are; the heading from the stored y and z)
    auto speed = static_cast<float>(std::sqrt((sz_exact * sz_exact + sy_exact * sy_exact) + sx * sx));
    auto sw = static_cast<float>(static_cast<double>(up_speed) + aw);
    const double length = std::sqrt((static_cast<double>(sz) * sz + static_cast<double>(sy) * sy) + sx * sx);
    double hx = sx, hy = sy;
    float hz = sz;
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        hx = sx * inverse;
        hy = sy * inverse;
        hz = static_cast<float>(sz * inverse);
    }
    script->args_->put_vec3(kHeading, script::VectorValue{static_cast<float>(hx), static_cast<float>(hy), hz, sw});
    script->args_->push_float(kJumpSpeed, speed);
    script->args_->push_float(kLeapDuration, time);
    return true;
}

} // namespace objects
