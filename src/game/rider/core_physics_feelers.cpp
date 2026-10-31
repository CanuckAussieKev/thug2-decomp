// The skater's collision feelers: the component's own feeler check (what was hit: skatable, vert, a wall), the
// side collisions that push the skater off walls, the rail and ground probes, and its script member functions.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/maths.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/shadow_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/park_editing/park_editor.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kWallNonSkatableAngle = 0x3EEDE4D3; // Wall_Non_Skatable_Angle (degrees)
constexpr uint32_t kRight = 0x4B358AEB;                // Right
constexpr uint32_t kLedge = 0x315D9ED4;                // Ledge (a member function)
constexpr uint32_t kObjMoveToNode = 0x8819DD8B;        // Obj_MoveToNode (a member function)
constexpr uint32_t kMotion = 0x0A015E17;               // motion (a component)
constexpr uint32_t kShadow = 0x8A897DD2;               // Shadow (a component)
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;  // SkaterFlipAndRotate (a component)
constexpr uint32_t kNoReset = 0xD607E2E6;              // NoReset
constexpr uint32_t kOrient = 0x90A91232;               // orient
constexpr uint32_t kNose = 0x7F03932C;                 // nose
constexpr uint32_t kTail = 0x83C84BA2;                 // tail
constexpr uint32_t kLeft = 0x85981897;                 // left
constexpr uint32_t kAxisX = 0x7323E97C;                // x
constexpr uint32_t kAxisZ = 0x9D2D8850;                // z
constexpr uint32_t kOff = 0xD443A2BC;                  // Off
constexpr uint32_t kSideCollideHeight = 0xB45B39E2;    // Skater_side_collide_height
constexpr uint32_t kLip = 0xA549B57B;                  // Lip
constexpr uint32_t kLipHorizOffset = 0xB92CBE3B;       // SkateInAble_LipHorizOffset
constexpr uint32_t kLipDownOffset = 0xEFDFE781;        // SkateInAble_LipDownOffset
constexpr uint32_t kLipExtraHorizOffset = 0x108613CB;  // SkateInAble_LipExtraCheckHorizOffset
constexpr uint32_t kLipExtraDownOffset = 0xF9DC446D;   // SkateInAble_LipExtraCheckDownOffset
constexpr uint32_t kSidewaysShift = 0xC78EBE56;          // SkateInAble_HorizOffset
constexpr uint32_t kDownOffset = 0xFCA3378C;           // SkateInAble_DownOffset
constexpr uint32_t kCount = 0x7A26BB9D;                // count
constexpr uint32_t kAmmoMeterChange = 0x0C8E5500;      // ammo_meter_change (a script)
constexpr uint32_t kDir = 0x455485EF;                  // dir
constexpr uint32_t kUpLeft = 0xB7231A95;               // UpLeft
constexpr uint32_t kUpRight = 0xA50950C5;              // UpRight
constexpr uint32_t kDownLeft = 0xD8847EFA;             // DownLeft
constexpr uint32_t kDownRight = 0x786B8B68;            // DownRight
constexpr uint32_t kDown = 0xE3006FC4;                 // Down

// The feeler's hit (the feeler at +0xA8: its CollisionHit at +0xCC).
constexpr uint32_t kHitObjectChecksum = 0x100;
constexpr uint32_t kHitUnknown38 = 0x104;
constexpr uint32_t kHitFaceFlag40 = 0x115;
constexpr uint32_t kHitDistance = 0x11C;

// Face flags (the feeler's hit).
constexpr uint16_t kFaceSkatable = 0x1;
constexpr uint16_t kFaceNotSkatable = 0x2;
constexpr uint16_t kFaceWall = 0x4;
constexpr uint16_t kFaceVert = 0x8;

// The other fields used here.
constexpr uint32_t kLastRail = 0x22C;          // the rail node last ground
constexpr uint32_t kRailManager = 0x230;
constexpr uint32_t kRailLeftTime = 0x234;
constexpr uint32_t kRailWait = 0x23C;          // (ms) before the same rail can be ground again
constexpr uint32_t kOnLedge = 0x248;
constexpr uint32_t kNearestSide = 0x2D4;
constexpr uint32_t kSideNormalSum = 0x2AC;     // the side feelers' hit normals added up
constexpr uint32_t kSideNearest = 0x2BC;       // the nearest side hit (0..1 along the feeler)
constexpr uint32_t kSideDistance = 0x2C0;      // how far the side feelers reach (0: off)
constexpr uint32_t kSidePush = 0x2C4;
constexpr uint32_t kSideTurn = 0x2C8;
constexpr uint32_t kSideTimes = 0x328;         // the clock when each side last hit (4)
constexpr uint32_t kProjectileCount = 0x2F8;   // (the ammo)

constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kNearGroundReach = 16.0f;          // 0x00645D68
constexpr float kNearGroundHeight = 20.0f;         // 0x006486AC
constexpr float kRailEndDistance = 6.0f;           // 0x00646284
// The natas exit's turns about Y (radians; the code's immediates).
constexpr float kEighthTurn = 0.785398185f;        // 0x3F490FDB
constexpr float kQuarterTurn = 1.57079637f;        // 0x3FC90FDB
constexpr float kThreeEighthsTurn = 2.3561945f;    // 0x4016CBE4
constexpr float kHalfTurn = 3.14159274f;           // 0x40490FDB

objects::GameEntity* skater_object(void* component) { return static_cast<objects::GameEntity*>(object_of(component)); }

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

// 0x0057CB60 (GameObjects::GrindRailHub::get_absolute_pos): a rail node's position.
maths::Vec4 rail_node_position(void* manager, const void* node)
{
    maths::Vec4 position;
    static_cast<objects::GrindRailHub*>(manager)->get_absolute_pos(&position, static_cast<const objects::GrindPoint*>(node));
    return position;
}

} // namespace

// 0x005003A0
bool SkaterPhysicsModule::probe_touches_zone(uint32_t ignore, uint32_t require)
{
    auto* feeler = reinterpret_cast<render::ProbeRay*>(&get_at<uint8_t>(this, kFeeler));
    feeler->change_strand(&vector_at(this, kProbeOrigin), &vector_at(this, kProbeTip));
    feeler->set_excluded(static_cast<uint16_t>(ignore), static_cast<uint16_t>(require));
    uint8_t hit = static_cast<uint8_t>(feeler->get_hit_shape(true, false));
    if (!hit)
        return false;
    if (get_at<uint32_t>(this, kHitObjectChecksum) && get_at<uint8_t>(this, kHitFaceFlag40))
        get_at<uint32_t>(this, kHitUnknown38) = 0;
    uint16_t flags = get_at<uint16_t>(this, kHitFlags);
    float normal_y = vector_at(this, kHitNormal).y;
    get_at<uint8_t>(this, kHitWall) = 0;
    get_at<uint8_t>(this, kHitVert) = (flags & kFaceVert) ? 1 : 0;
    bool skatable;
    if (flags & kFaceSkatable) {
        skatable = true;
    } else if (flags & kFaceNotSkatable) {
        skatable = false;
    } else if (flags & kFaceVert) {
        skatable = true;
    } else if (flags & kFaceWall) {
        get_at<uint8_t>(this, kHitSkatable) = 0;
        get_at<uint8_t>(this, kHitWall) = 1;
        return true;
    } else {
        // Neither: skatable unless steeper than Wall_Non_Skatable_Angle (its sine above the normal's y).
        double limit = x87_sin(static_cast<double>(script::fetch_float(kWallNonSkatableAngle)) * kRadiansPerDegree);
        skatable = !(limit > normal_y);
    }
    get_at<uint8_t>(this, kHitSkatable) = skatable ? 1 : 0;
    return true;
}

// 0x00500020
bool SkaterPhysicsModule::can_grind_rail(const void* rail, const void*, bool any_time)
{
    struct RailNode {
        uint8_t unknown[0x40];
        const RailNode* previous; // +0x40
        const RailNode* next;     // +0x44
    };
    const auto* node = static_cast<const RailNode*>(rail);
    uint8_t* flags = flags_of(this);
    const auto* last = get_at<const RailNode*>(this, kLastRail);
    auto same_rail = [&] { return last == node || last == node->previous || last == node->next; };
    // In the air after a grind: not the same rail (or its neighbours) again.
    if (get_at<int>(flags, kState) == 1 && flags[0xA8] && flags[0xD0] && same_rail())
        return false;

    bool new_rail = false;
    if (!last) {
        new_rail = true;
    } else if (!same_rail()) {
        // Another rail. In a created park, only if its ends are all more than 6 from the last one's (pieces' rails
        // meet end to end).
        uint8_t* park = reinterpret_cast<uint8_t*>(park_editing::g_park_editor); // (the park editor)
        if (get_at<uint32_t>(park, 0x40) == kOff || !last->previous || !node->previous) {
            new_rail = true;
        } else {
            // (Each pair in the original's order: this rail's end first, then the last one's.)
            auto between = [this](const void* from, const void* to) {
                maths::Vec4 a = rail_node_position(get_at<void*>(this, kRailManager), from);
                maths::Vec4 b = rail_node_position(get_at<void*>(this, kRailManager), to);
                return b - a;
            };
            maths::Vec4 d1 = between(node, get_at<const RailNode*>(this, kLastRail));
            maths::Vec4 d2 = between(node->previous, get_at<const RailNode*>(this, kLastRail));
            const RailNode* last_previous = get_at<const RailNode*>(this, kLastRail)->previous;
            maths::Vec4 d3 = between(node, last_previous);
            last_previous = get_at<const RailNode*>(this, kLastRail)->previous;
            maths::Vec4 d4 = between(node->previous, last_previous);
            new_rail = std::fabs(length_unrounded(d1)) > kRailEndDistance && std::fabs(length_unrounded(d2)) > kRailEndDistance &&
                      std::fabs(length_unrounded(d3)) > kRailEndDistance && std::fabs(length_unrounded(d4)) > kRailEndDistance;
        }
    }
    if (new_rail)
        set_timed_flag(this, 0x50, 1);

    // The same rail: only after the wait since leaving it.
    if (!flags_of(this)[0x50] && get_since(get_at<uint32_t>(this, kRailLeftTime)) <= get_at<uint32_t>(this, kRailWait))
        return false;
    if (any_time)
        return true;
    flags = flags_of(this);
    if (get_at<int>(flags, kState) == 4)
        return false;
    // (On a vert wall, only while moving up.)
    if (flags[0x38] && !(vector_at(object_of(this), kVelocity).y > 0.0f))
        return false;
    return true;
}

// 0x005012B0
int SkaterPhysicsModule::near_ground(script::Struct* params, script::Script*)
{
    bool right = params->find_component(kRight) != nullptr;
    void* object = object_of(this);
    const maths::Vec4& side = vector_at(object, kMatrix);
    const maths::Vec4& position = vector_at(object, kPosition);
    double x = static_cast<double>(side.x) * kNearGroundReach; // (left unrounded)
    float y = side.y * kNearGroundReach, z = side.z * kNearGroundReach, w = side.w * kNearGroundReach;
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    maths::Vec4& end = vector_at(this, kProbeTip);
    // A feeler 20 up and down, a little to the side (Right: the other way).
    float origin_coord_x, origin_coord_y;
    if (right) {
        origin_coord_x = static_cast<float>(position.x - x);
        origin_coord_y = position.y - y;
        start.z = position.z - z;
        start.w = position.w - w;
    } else {
        origin_coord_x = static_cast<float>(x + position.x);
        origin_coord_y = y + position.y;
        start.z = z + position.z;
        start.w = w + position.w;
    }
    start.x = origin_coord_x;
    start.y = origin_coord_y;
    start.y = start.y + kNearGroundHeight;
    end = start;
    end.y = end.y - kNearGroundHeight;
    return probe_touches_zone(0x10, 0) ? 1 : 0;
}

// 0x00500F10
int SkaterPhysicsModule::skate_in_able(script::Struct* params, script::Script*)
{
    void* object = object_of(this);
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    maths::Vec4& end = vector_at(this, kProbeTip);
    // (each line's x - and across the skater its y - from an unrounded product, as the original's FPU code has them)
    if (params->has_bare_flag(kLip)) {
        // Down from just behind the position (back along up), then from a little further.
        float back = tuning_float_lookup(kLipHorizOffset, object);
        float down = tuning_float_lookup(kLipDownOffset, object);
        end = vector_at(object_of(this), kPosition);
        end.y = end.y - down;
        const maths::Vec4& up = vector_at(object_of(this), kUp);
        start.x = static_cast<float>(end.x - static_cast<double>(back) * up.x);
        start.y = end.y - back * up.y;
        start.z = end.z - back * up.z;
        start.w = end.w - back * up.w;
        if (!probe_touches_zone(0x10, 0)) {
            back = tuning_float_lookup(kLipExtraHorizOffset, object);
            down = tuning_float_lookup(kLipExtraDownOffset, object);
            const maths::Vec4& position = vector_at(object_of(this), kPosition);
            const maths::Vec4& up2 = vector_at(object_of(this), kUp);
            start.x = static_cast<float>(position.x - static_cast<double>(back) * up2.x);
            start.y = position.y - back * up2.y;
            start.z = position.z - back * up2.z;
            start.w = position.w - back * up2.w;
            end = start;
            end.y = end.y - down;
            if (!probe_touches_zone(0x10, 0))
                return 0;
        }
        return get_at<uint8_t>(this, kHitVert) ? 1 : 0;
    }
    // Across the skater: from `across` to one side (lowered) to as far to the other.
    float across = tuning_float_lookup(kSidewaysShift, object);
    const maths::Vec4& side = vector_at(object_of(this), kMatrix);
    const maths::Vec4& position = vector_at(object_of(this), kPosition);
    maths::Vec4 near_side;
    near_side.x = static_cast<float>(static_cast<double>(across) * side.x + position.x);
    near_side.y = static_cast<float>(static_cast<double>(across) * side.y + position.y);
    near_side.z = across * side.z + position.z;
    near_side.w = across * side.w + position.w;
    float down = tuning_float_lookup(kDownOffset, object);
    near_side.y = near_side.y - down;
    const maths::Vec4& side2 = vector_at(object_of(this), kMatrix);
    const double twice = static_cast<double>(across) + across;
    maths::Vec4 far_side;
    far_side.x = static_cast<float>(near_side.x - twice * side2.x);
    far_side.y = near_side.y - static_cast<float>(twice * side2.y);
    far_side.z = near_side.z - static_cast<float>(twice * side2.z);
    far_side.w = near_side.w - static_cast<float>(twice * side2.w);
    if (params->has_bare_flag(kLeft)) {
        start = near_side;
        end = far_side;
    } else {
        start = far_side;
        end = near_side;
    }
    if (!probe_touches_zone(0x10, 0))
        return 0;
    return get_at<uint8_t>(this, kHitVert) ? 1 : 0;
}

// 0x005013E0
int SkaterPhysicsModule::set_ammo(script::Struct* params, script::Script*)
{
    int& ammo = get_at<int>(this, kProjectileCount);
    int before = ammo;
    params->fetch_int_value(uint32_t{0}, &ammo, 0); // (the unnamed integer)
    int change = ammo - before;
    script::Struct* changed = script::spawn_param_bag();
    changed = changed ? changed->prepare_all() : nullptr;
    changed->put_int(kCount, change); // (on a null structure if out of memory, as the original)
    script::end_child_scripts_of_entity_by_name(skater_object(this), kAmmoMeterChange, nullptr);
    skater_object(this)->spawn_script(kAmmoMeterChange, changed, 0, false);
    if (changed) {
        changed->get_clear();
        script::delete_struct(changed);
    }
    return 1;
}

// 0x005014E0 (some turns by Mat4::RotateY, the others by a rotation matrix and D3DXMatrixMultiply, as the original
// has them)
int SkaterPhysicsModule::check_natas_exit_direction(script::Struct* params, script::Script*)
{
    uint32_t dir = 0; // (left as it was on the stack without one, in the original)
    params->read_hash_value(kDir, &dir, 1);
    maths::Mat4 m;
    m = get_at<maths::Mat4>(static_cast<objects::Rider*>(skater_object(this))->get_skater_camera(), kMatrix);
    switch (dir) {
    case kDownRight:
        m.rotate_y(kEighthTurn);
        break;
    case kRight:
        m.rotate_y(kQuarterTurn);
        break;
    case kLeft:
        m.rotate_y(-kQuarterTurn);
        break;
    case kUpRight:
        m.rotate_y(kThreeEighthsTurn);
        break;
    case kDown:
        m.rotate_y(0.0f);
        break;
    default: { // UpLeft, DownLeft; Up and anything else half a turn
        float turn = dir == kUpLeft ? -kThreeEighthsTurn : dir == kDownLeft ? -kEighthTurn : kHalfTurn;
        maths::Mat4 rotation;
        matrix_multiply(&m, &m, &maths::make_yaw_matrix(rotation, turn));
        break;
    }
    }
    get_at<maths::Mat4>(object_of(this), kMatrix) = m;
    return 1;
}

// 0x00501600
int SkaterPhysicsModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name == kLedge)
        return get_at<uint8_t>(this, kOnLedge) ? 1 : 0;
    if (name != kObjMoveToNode) {
        // Not original - THAW's bike member functions (SkaterOnBike, IsSkaterOnBike...: game/rider/thaw_bmx.h)
        // while [features] thaw_bmx is on.
        // Original behaviour: any other name isn't this component's (2).
        if (thaw_bmx::is_switched_on())
            return thaw_bmx::invoke_method_by_name(this, name, params, script);
        return 2;
    }
    // Obj_MoveToNode: the motion component moves the skater; then everything that follows the position is put there.
    objects::GameEntity* object = skater_object(this);
    objects::ObjectModule* motion = object->fetch_module(kMotion);
    bool moved = motion->invoke_method_by_name(kObjMoveToNode, params, script) != 0;
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    auto* shadow = static_cast<objects::GroundShadowModule*>(skater_object(this)->fetch_module(kShadow));
    shadow->change_place(&vector_at(object_of(this), kPosition));
    shadow->set_normal(&vector_at(object_of(this), kUp));
    vector_at(this, kSafePosition) = vector_at(object_of(this), kPosition);
    vector_at(object_of(this), kOldPosition) = vector_at(object_of(this), kPosition);
    skater_object(this)->mark_jumped_ahead(true);
    if (get_at<uint8_t>(object_of(this), 0xBC)) {
        // (the skater's camera follows at once)
        void* camera = static_cast<objects::Rider*>(object_of(this))->get_skater_camera();
        if (camera)
            static_cast<objects::GameEntity*>(camera)->tick_frame();
    }
    if (!params->has_bare_flag(kNoReset)) {
        get_at<uint8_t>(this, 0x1C5) = 0;
        vector_at(object_of(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        physics::change_phase(this, 0);
        set_timed_flag(this, 0x28, get_at<uint8_t>(object_of(this), 0x110) == 0);
        static_cast<objects::RiderSpinAndFlip*>(skater_object(this)->fetch_module(kSkaterFlipAndRotate))
            ->apply_flip_to_anims();
    } else if (params->has_bare_flag(kOrient)) {
        // The horizontal speed kept, along the new facing.
        maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
        float upward = velocity.y;
        velocity.y = 0.0f;
        void* o = object_of(this);
        float speed = static_cast<float>(length_unrounded(vector_at(o, kVelocity)));
        vector_at(o, kVelocity) = vector_at(o, kForward) * speed;
        vector_at(object_of(this), kVelocity).y = upward;
    }
    return moved ? 1 : 0;
}

// 0x005009C0
void SkaterPhysicsModule::find_nearest_wall_side()
{
    render::ProbeRay feeler;
    feeler.make_new();
    get_at<uint32_t>(this, kNearestSide) = 0;
    feeler.line_.start = vector_at(object_of(this), kPosition);
    float nearest = 999.0f;
    // Each end and side of the board: 12 along the forward or side row.
    static const struct {
        uint32_t side;
        uint32_t axis;
        float distance;
    } kSides[4] = {{kNose, kAxisZ, 12.0f}, {kTail, kAxisZ, -12.0f}, {kLeft, kAxisX, 12.0f}, {kRight, kAxisX, -12.0f}};
    for (const auto& side : kSides) {
        const maths::Vec4& row = vector_at(object_of(this), side.axis == kAxisX ? kMatrix : kForward);
        const maths::Vec4& start = feeler.line_.start;
        double distance = side.distance;
        maths::Vec4& end = feeler.line_.end;
        end.x = static_cast<float>(distance * row.x + start.x);
        end.y = static_cast<float>(distance * row.y + start.y);
        end.z = static_cast<float>(distance * row.z + start.z);
        end.w = static_cast<float>(distance * row.w + start.w);
        if (feeler.get_hit_shape(true, false) && feeler.reach_ < nearest) {
            nearest = feeler.reach_;
            get_at<uint32_t>(this, kNearestSide) = side.side;
        }
    }
}

// 0x00501C90
void SkaterPhysicsModule::check_side_feeler(const maths::Vec4* start, const maths::Vec4* end, uint32_t side)
{
    // (The original's loop over the other three sides checks the opposite side's time each time round.)
    for (uint32_t other = 0; other < 4; ++other) {
        if (other != side && get_since(get_at<uint32_t>(this, kSideTimes + (side ^ 1) * 4)) > 500)
            return;
    }
    vector_at(this, kProbeOrigin) = *start;
    vector_at(this, kProbeTip) = *end;
    if (!probe_touches_zone(0x10, 0))
        return;
    maths::Vec4& sum = vector_at(this, kSideNormalSum);
    const maths::Vec4& normal = vector_at(this, kHitNormal);
    sum.x = normal.x + sum.x;
    sum.y = normal.y + sum.y;
    sum.z = normal.z + sum.z;
    sum.w = normal.w + sum.w;
    if (get_at<float>(this, kHitDistance) < get_at<float>(this, kSideNearest))
        get_at<float>(this, kSideNearest) = get_at<float>(this, kHitDistance);
    get_at<int>(this, kSideTimes + side * 4) = get_now();
}

// 0x005055B0
void SkaterPhysicsModule::check_side_feelers()
{
    if (get_at<float>(this, kSideDistance) == 0.0f || get_at<float>(this, kSidePush) == 0.0f)
        return;
    vector_at(this, kSideNormalSum) = {0.0f, 0.0f, 0.0f, 1.0f};
    get_at<float>(this, kSideNearest) = 1.0f;
    // The feelers start at the side collision height up the skater.
    void* object = object_of(this);
    double height = script::fetch_float(kSideCollideHeight);
    const maths::Vec4& up = vector_at(object, kUp);
    const maths::Vec4& position = vector_at(object_of(this), kPosition);
    maths::Vec4 base;
    base.x = static_cast<float>(height * up.x + position.x);
    base.y = static_cast<float>(height * up.y) + position.y;
    base.z = static_cast<float>(height * up.z) + position.z;
    base.w = static_cast<float>(height * up.w) + position.w;
    maths::Vec4 start = base;
    // Sides 0 and 1: along the side row and back; 2 and 3: along the forward row and back.
    for (uint32_t side = 0; side < 4; ++side) {
        const maths::Vec4& row = vector_at(object_of(this), side < 2 ? kMatrix : kForward);
        double reach = get_at<float>(this, kSideDistance);
        float x = static_cast<float>(reach * row.x), y = static_cast<float>(reach * row.y),
              z = static_cast<float>(reach * row.z);
        double w = reach * row.w; // (left unrounded)
        maths::Vec4 end;
        if (side % 2 == 0) {
            end.x = x + base.x;
            end.y = y + base.y;
            end.z = z + base.z;
            end.w = static_cast<float>(w + base.w);
        } else {
            end.x = base.x - x;
            end.y = base.y - y;
            end.z = base.z - z;
            end.w = static_cast<float>(base.w - w);
        }
        check_side_feeler(&start, &end, side);
    }
    if (get_at<uint32_t>(this, kSideNearest) == 0x3F800000u) // (nothing hit: still 1)
        return;
    // Pushed off by the walls hit, the nearer the harder; turned along a wall it faces into.
    maths::Vec4& push = vector_at(this, kSideNormalSum);
    set_length(push, static_cast<float>((1.0 - get_at<float>(this, kSideNearest)) * get_at<float>(this, kSidePush)));
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    velocity.x = push.x + velocity.x;
    velocity.y = push.y + velocity.y;
    velocity.z = push.z + velocity.z;
    velocity.w = push.w + velocity.w;
    vector_at(object_of(this), kVelocity).align_to_plane(vector_at(this, kFloorUpVector));
    object = object_of(this);
    const maths::Vec4& forward = vector_at(object, kForward);
    const maths::Vec4& normal = vector_at(this, kHitNormal);
    double into = static_cast<double>(forward.z) * normal.z + static_cast<double>(forward.y) * normal.y +
                  static_cast<double>(forward.x) * normal.x;
    if (!(into < 0.0))
        return;
    maths::Vec4 along = forward;
    along.align_to_plane(normal);
    float angle = static_cast<float>(maths::get_turn_around_axis(vector_at(object_of(this), kForward), along,
                                                        vector_at(object_of(this), kUp)));
    double turn = static_cast<double>(maths::get_sign(angle)) * get_at<float>(this, kSideTurn) * get_at<float>(this, kFrameLength);
    float step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn))
        step = angle;
    get_at<maths::Mat4>(object_of(this), kMatrix).yaw_locally(step);
    get_at<maths::Mat4>(this, kDisplayMatrix).yaw_locally(step);
}

// 0x005054A0
bool SkaterPhysicsModule::apply_model_rotations()
{
    void* model = skater_object(this)->fetch_module(0x286A8D26); // (Model)
    if (!get_at<uint8_t>(model, 0xC8) && !get_at<uint8_t>(model, 0x15C) && !get_at<uint8_t>(model, 0x1F0))
        return false;
    void* object = object_of(this);
    maths::Vec4 axis = vector_at(object, kUp);
    static_cast<objects::MeshInstanceModule*>(model)->get_draw_matrix(&get_at<maths::Mat4>(object, kMatrix));
    auto* y_rotation = &get_at<objects::ModelSpinAxis>(model, 0xCC);
    float angle = y_rotation->bearing();
    if (angle != 0.0f) {
        // The velocity turned with the model about the up axis.
        maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
        alignas(16) uint8_t rotation[0x10]; // (a quaternion, it seems)
        auto* quat = reinterpret_cast<maths::QuatRotation*>(rotation);
        quat->init_from_axis_angle(axis, angle);
        velocity = *quat->spin_by(&axis, velocity);
    }
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    get_at<objects::ModelSpinAxis>(model, 0x38).get_clear();
    y_rotation->get_clear();
    get_at<objects::ModelSpinAxis>(model, 0x160).get_clear();
    return true;
}

} // namespace Skater
