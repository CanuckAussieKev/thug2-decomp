// GameObjects::GameEntity - the object made of components (skaters, peds, cars, level objects) - and the base of its
// components, GameObjects::ObjectModule (entity.h). From the disassembly of 0x0045D720-0x0045EE20 (methods __thiscall,
// RET n = 4 * arguments; the member functions scripts call are entries of the table at 0x00683268, filled in by
// 0x006425D0, all `bool (GameEntity::*)(CStruct* params, CScript* script)`).

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/maths/mat4.h"
#include "engine/maths/trig.h"
#include "engine/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/mem.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec4.h"

namespace objects {

using script::Array;
using script::MemberFunctionEntry;
using script::Script;
using script::Struct;

namespace {
constexpr float kDefaultLength = 3.0F;
constexpr float kDefaultHeight = 3.0F;
constexpr float kCompositeAcosA0{1.5707288F};
constexpr float kCompositeAcosA1{0.2121144F};
constexpr float kCompositeAcosA2{0.074261F};
constexpr float kCompositeAcosA3{-0.0187293F};
constexpr float kCompositeFeetToInches{12.0F};

// Names (checksums).
constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kPermanent = 0x23627FD7;      // permanent
constexpr uint32_t kComponent = 0xB6015EA8;      // component
constexpr uint32_t kPos = 0x7F261953;            // pos
constexpr uint32_t kLength = 0xFE82614D;         // length
constexpr uint32_t kHeight = 0x0AB21AF0;         // height
constexpr uint32_t kDistance = 0xE36D657E;       // distance
constexpr uint32_t kShift = 0x5AF4C4BA;          // shift
constexpr uint32_t kSide = 0xDC7EE44A;           // side
constexpr uint32_t kDown = 0xE3006FC4;           // down
constexpr uint32_t kPosition = 0xB9D31B0A;       // position
constexpr uint32_t kVel = 0x0C4C809E;            // vel
constexpr uint32_t kSpeed = 0xF0D90109;          // speed
constexpr uint32_t kX = 0x7323E97C;              // x
constexpr uint32_t kY = 0x0424D9EA;              // y
constexpr uint32_t kZ = 0x9D2D8850;              // z
constexpr uint32_t kDistanceResult = 0x976B04A3; // (name unknown): obj_get_distance_to_object's result
constexpr uint32_t kTypeResult = 0x24495505;     // (name unknown): obj_get_type's result
constexpr uint32_t kDist = 0x7E832F08;           // dist
constexpr uint32_t kDotProd = 0x7C6A7D7C;        // dotprod
constexpr uint32_t kAngleResult = 0xC97F3AA9;    // (name unknown): Obj_GetOrientationToObject's angle
constexpr uint32_t kDir = 0x455485EF;            // dir
constexpr uint32_t kQuat = 0x748CA262;           // quat
constexpr uint32_t kNearestType = 0xBAF9B341;    // (name unknown): the object type 0x0045EF30 looks for

// The constants the original reads (0x006461F4: inches to feet; 0x00645D74).
constexpr float kInchesToFeet = 1.0F / 12.0F;
constexpr float kSpeedTolerance = 0.1F;
constexpr float kDegreesPerRadian = 57.2957763671875F;    // 0x00645468
constexpr float kQuarterTurnDeg = 90.0F;                  // 0x00645EB0
constexpr float kRadiansPerDegree = 0.01745329238474369F; // 0x00645464
constexpr float kNearlyVertical = 0.99F;                  // 0x00645450
constexpr float kFarAway = 5322672.0F;                    // (0x4AA26F60)

// The composite object manager (0x006F0FB8, compositemgr.cpp): its lists of the components of each type, and the
// component makers.
void enrol_module(ObjectModule* component) { object_hub_slot()->attach_module_of_kind(component); }
void unregister_component(ObjectModule* component) { object_hub_slot()->detach_module_of_kind(component); }
ObjectModule* spawn_module(uint32_t type) { return object_hub_slot()->spawn_module(type); }
// The position (pos=) / orientation in a structure, if it has them (level_nodes, nodes.h).
void get_position_from_structure(Struct* params, maths::Vec4* pos) { level_nodes::fetch_place(params, pos); }
void get_orientation_from_structure(Struct* params, maths::Mat4* matrix) { level_nodes::fetch_facing(params, matrix); }

// The node named `name` (level_nodes).
Struct* fetch_vertex(uint32_t name, int assert) { return level_nodes::fetch_vertex(name, assert != 0); }
// Quaternions (x, y, z, w): to a rotation matrix (0x0042BC40, on the quaternion), from one (0x0042E1F0, constructing
// the quaternion).
void quat_to_matrix(const std::array<float, 4>& quat, maths::Mat4* matrix)
{
    maths::quat_to_matrix({.x = core::at(quat, 0), .y = core::at(quat, 1), .z = core::at(quat, 2), .w = core::at(quat, 3)}, matrix);
}
void matrix_to_quat(std::array<float, 4>& quat, const maths::Mat4* matrix)
{
    const maths::Vec4 q = maths::quat_from_matrix(*matrix);
    quat = {{q.x, q.y, q.z, q.w}};
}
// A manager's object list.
memory::ListNode* manager_object_list(ObjectHubBase* manager) { return manager->fetch_object_set(); }
// maths::fast_a_cos (0x00403EF0) as its callers here see it: the x87 result is used before it is rounded to a float.
double fast_a_cos_unrounded(float value)
{
    const double a0 = kCompositeAcosA0;
    const double a1 = kCompositeAcosA1;
    const double a2 = kCompositeAcosA2;
    const double a3 = kCompositeAcosA3;
    const double pi = maths::kPi;
    if (value < 0.0F) {
        const double x = -static_cast<double>(value);
        const double root = std::sqrt(1.0 - x);
        return pi - (((((((x * a3) + a2) * x) - a1) * x) + a0) * root);
    }
    const double x = value;
    const double root = std::sqrt(1.0 - x);
    return ((((((x * a3) + a2) * x) - a1) * x) + a0) * root;
}
// a x b in double precision, each component rounded once; w = 0.
maths::Vec4 perpendicular(const maths::Vec4& a, const maths::Vec4& b)
{
    return {.x = static_cast<float>((static_cast<double>(a.y) * b.z) - (static_cast<double>(a.z) * b.y)),
            .y = static_cast<float>((static_cast<double>(a.z) * b.x) - (static_cast<double>(a.x) * b.z)),
            .z = static_cast<float>((static_cast<double>(a.x) * b.y) - (static_cast<double>(a.y) * b.x)),
            .w = 0.0F};
}

// |v| as the original's x87 code works it out: (x*x + y*y) + z*z in double precision, not rounded to a float.
double len_of(const maths::Vec4& v)
{
    const auto x = v.x;
    const auto y = v.y;
    const auto z = v.z;
    const double x2 = static_cast<double>(x) * x;
    const double y2 = static_cast<double>(y) * y;
    const double z2 = static_cast<double>(z) * z;
    return std::sqrt(x2 + y2 + z2);
}

// pos += row * scale, as the original rounds it: the x and y products stay in double precision, the z and w ones
// are rounded to floats first.
void add_scaled(maths::Vec4& pos, const maths::Vec4& row, float scale)
{
    const double x = static_cast<double>(scale) * row.x;
    const double y = static_cast<double>(scale) * row.y;
    const float z = scale * row.z;
    const float w = scale * row.w;
    pos.x = static_cast<float>(x + pos.x);
    pos.y = static_cast<float>(y + pos.y);
    pos.z = z + pos.z;
    pos.w = w + pos.w;
}

// Debug output of an object's details: compiled out of this build (the text is still read).
inline void print_object_details(const char* /*text*/) {}

// The collision feeler (render::ProbeRay): constructed by 0x00417760, its line tested by 0x004172F0; what it hit is
// held by a smart pointer, let go of by its destructor.
class CollisionFeeler {
public:
    CollisionFeeler() = default;
    CollisionFeeler(const CollisionFeeler&) = delete;
    CollisionFeeler& operator=(const CollisionFeeler&) = delete;
    ~CollisionFeeler() = default;
    maths::Vec4& kickoff() { return ray_.line().start; }
    maths::Vec4& get_end() { return ray_.line().end; }
    bool get_hit_shape(bool a, bool b) { return ray_.get_hit_shape(a, b) != 0; }

private:
    render::ProbeRay ray_;
};

float feet_as_inches() { return kCompositeFeetToInches; } // 12

} // namespace

// --- Entity: a member function table entry -------------------------------------------------------------------

// 0x0045D850
bool Entity::invoke_method(const MemberFunctionEntry* entry, Struct* params, Script* script)
{
    if (entry->method_code_) {
        // (this_adjustment_ is 0 in every table: the entry's code casts the object to its own class)
        return entry->method_code_(this, entry->callsign_, params, script) != 0;
    }
    return invoke_method_by_name(entry->callsign_, params, script);
}

// --- ObjectModule -------------------------------------------------------------------------------------------

void ObjectModule::operator delete(void* memory) { memory::Hub::dealloc(memory); }

// 0x0045D740 (0x0045D780 the deleting one)
ObjectModule::~ObjectModule()
{
    if (flag_bits_ & kComponentFlagScriptWaiting) {
        script::component_destroyed_all_scripts(this);
    }
}

// 0x0051B970
void ObjectModule::tick_frame() { hold_off(true); }

// 0x0045D760
void ObjectModule::hold_off(bool suspend)
{
    if (suspend) {
        flag_bits_ |= kComponentFlagSuspended;
    } else {
        flag_bits_ &= ~kComponentFlagSuspended;
    }
}

// 0x0045D720 (a jump to slot 12 with the entry's name in place of the entry)
int ObjectModule::invoke_method(const MemberFunctionEntry* entry, Struct* params, Script* script)
{
    return invoke_method_by_name(entry->callsign_, params, script);
}

// --- GameEntity: life -----------------------------------------------------------------------------------

// 0x0045E510
GameEntity::GameEntity()
{
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    at_point_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(basis_frame_.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(basis_frame_.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(basis_frame_.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(basis_frame_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(display_matrix_.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(display_matrix_.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(display_matrix_.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(display_matrix_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    object_flags() |= kObjectFlagHasPosition;
}

// 0x0045D890
GameEntity::~GameEntity()
{
    composite_flags_ |= kCompositeFlagDestroying;
    while (attached_modules_) {
        gsl::owner<ObjectModule*> component = memory::owned_element(attached_modules_); // (the list's)
        attached_modules_ = component->following();
        unregister_component(component);
        delete component;
    }
}

// --- GameEntity: the frame ------------------------------------------------------------------------------

// 0x0045D920
void GameEntity::tick_frame()
{
    if (composite_flags_ & kCompositeFlagPaused) {
        return;
    }
    if (!(composite_flags_ & kCompositeFlagSuspended)) {
        if (own_script()) {
            own_script()->tick_frame();
        }
        if (object_flags() & kObjectFlagDead) {
            return;
        }
        own_update();
    }
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        if (component->flag_bits() & kComponentFlagSuspended) {
            continue;
        }
        component->tick_frame();
        if (object_flags() & kObjectFlagDead) {
            break;
        }
    }
    at_point_.w = 1.0F;
    composite_flags_ &= ~kCompositeFlagTeleported;
}

// 0x0045D990
void GameEntity::conceal(bool hide)
{
    if (hide == ((composite_flags_ & kCompositeFlagHidden) != 0)) {
        return;
    }
    if (hide) {
        composite_flags_ |= kCompositeFlagHidden;
    } else {
        composite_flags_ &= ~kCompositeFlagHidden;
    }
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        component->conceal(hide);
    }
}

// 0x0045D9E0
void GameEntity::hold_off(bool suspend)
{
    if (suspend == ((composite_flags_ & kCompositeFlagSuspended) != 0)) {
        return;
    }
    if (suspend) {
        composite_flags_ |= kCompositeFlagSuspended;
    } else {
        composite_flags_ &= ~kCompositeFlagSuspended;
    }
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        component->hold_off(suspend);
    }
}

// 0x0045E4C0
void GameEntity::mark_jumped_ahead(bool teleport_components)
{
    composite_flags_ |= kCompositeFlagTeleported;
    if (teleport_components) {
        for (ObjectModule* component = attached_modules_; component; component = component->following()) {
            component->jump_to();
        }
    }
}

// 0x0045E500
bool GameEntity::should_update_scripts() { return !(composite_flags_ & kCompositeFlagPaused); }

// 0x0045D7C0
void GameEntity::flag_as_dead()
{
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        component->object_marked_dead();
    }
    Entity::flag_as_dead();
}

// --- GameEntity: components -----------------------------------------------------------------------------

// 0x0045DA30 (the component's +0x10 isn't cleared: it is expected to be null already)
void GameEntity::attach_module(ObjectModule* component)
{
    if (!attached_modules_) {
        attached_modules_ = component;
    } else {
        ObjectModule* last = attached_modules_;
        while (last->following()) {
            last = last->following();
        }
        last->set_following(component);
    }
    component->set_entity(this);
    enrol_module(component);
}

// 0x0045DA90
void GameEntity::build_modules_from_list(Array* components, Struct* extra)
{
    const int count = static_cast<int>(components->extent());
    for (int i = 0; i < count; ++i) {
        create_component_from_structure(components->struct_at(static_cast<uint32_t>(i)), extra);
    }
}

// 0x0045EE20. With no component= the type looked up is `params`' own address [quirk]; a type the manager can't
// make gives a null component, added all the same [bug: crash]; so does a failed allocation of the merged copy.
void GameEntity::create_component_from_structure(Struct* params, Struct* extra)
{
    uint32_t type = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(kComponent, &type, 1);
    ObjectModule* component = spawn_module(type);
    attach_module(component);
    if (!extra) {
        component->configure_from_data(params);
        return;
    }
    gsl::owner<Struct*> merged = script::new_struct();
    merged->merge_structure(params);
    merged->merge_structure(extra);
    component->configure_from_data(merged);
    if (merged) {
        merged->get_clear();
        script::delete_struct(merged);
    }
}

// 0x0045DAD0
void GameEntity::configure_from_data(Struct* params)
{
    uint32_t id = 0;
    if (params->read_hash_value(kName, &id, 0)) {
        change_serial(id);
    }
    get_position_from_structure(params, &at_point_);
    get_orientation_from_structure(params, &basis_frame_);
    if (params->has_bare_flag(kPermanent)) {
        object_flags() |= kObjectFlagLocked;
    }
}

// 0x0045DB30
void GameEntity::seal()
{
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        component->seal();
    }
    composite_flags_ |= kCompositeFlagFinalized;
    composite_flags_ |= kCompositeFlagFinalized2;
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        component->jump_to();
    }
}

// 0x0045DB90
ObjectModule* GameEntity::fetch_module(uint32_t type)
{
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        if (component->kind_tag() == type) {
            return component;
        }
    }
    return nullptr;
}

// 0x0045E450
bool GameEntity::invoke_method_by_name(uint32_t name, Struct* params, Script* script)
{
    for (ObjectModule* component = attached_modules_; component; component = component->following()) {
        const int result = component->invoke_method_by_name(name, params, script);
        if (result == 0) {
            return false;
        }
        if (result == 1) {
            return true;
        }
    }
    return Entity::invoke_method_by_name(name, params, script);
}

// --- GameEntity: the member functions scripts call ------------------------------------------------------

// 0x0045D7F0: the unnamed string (the details' title) is read for the debug output.
bool GameEntity::obj_print_details(Struct* params, Script* /*script*/)
{
    const char* text = ""; // (the original's default was the parameters' own address, read as text)
    params->fetch_text(kUnnamed, &text, 0);
    print_object_details(text);
    return true;
}

// 0x0045D810: a component from the parameters (CreateComponentFromStructure).
bool GameEntity::obj_create_component(Struct* params, Script* /*script*/)
{
    create_component_from_structure(params, nullptr);
    return true;
}

// 0x0045D830: position= (if given) as the position.
bool GameEntity::obj_set_position(Struct* params, Script* /*script*/)
{
    params->fetch_vec(kPosition, &at_point_, 0);
    return true;
}

// 0x0045DBC0: pos = the position, in the calling script's parameters.
bool GameEntity::obj_get_position(Struct* /*params*/, Script* script)
{
    script->args()->put_vec3(checksums::Name{kPos}, {.x = at_point_.x, .y = at_point_.y, .z = at_point_.z});
    return true;
}

// 0x0045DBF0: whether it has a component of the type name= names (required).
bool GameEntity::obj_component_exists(Struct* params, Script* /*script*/)
{
    uint32_t type = 0;
    params->read_hash_value(kName, &type, 1);
    return fetch_module(type) != nullptr;
}

// 0x0045DC40: an extra update now.
bool GameEntity::obj_force_update(Struct* /*params*/, Script* /*script*/)
{
    tick_frame();
    return true;
}

// 0x0045DC50: vel = the velocity.
bool GameEntity::obj_get_velocity(Struct* /*params*/, Script* script)
{
    script->args()->put_vec3(checksums::Name{kVel}, {.x = vel_.x, .y = vel_.y, .z = vel_.z});
    return true;
}

// 0x0045DC80: speed = the velocity's length.
bool GameEntity::get_speed(Struct* /*params*/, Script* script)
{
    script->args()->push_float(checksums::Name{kSpeed}, static_cast<float>(len_of({.x = vel_.x, .y = vel_.y, .z = vel_.z})));
    return true;
}

// 0x0045DCD0 / 0x0045DD20 / 0x0045DD70
bool GameEntity::obj_hide(Struct* /*params*/, Script* /*script*/)
{
    conceal(true);
    return true;
}
bool GameEntity::obj_unhide(Struct* /*params*/, Script* /*script*/)
{
    conceal(false);
    return true;
}
bool GameEntity::obj_is_hidden(Struct* /*params*/, Script* /*script*/) const
{
    return (composite_flags_ & kCompositeFlagHidden) != 0;
}

// 0x0045DD80: moves along its own axes by x=, y= and z= (each 0 if missing), all four components of each row.
bool GameEntity::obj_move_local(Struct* params, Script* /*script*/)
{
    float distance = 0.0F;
    params->fetch_float(kX, &distance, 0);
    add_scaled(at_point_, core::at(basis_frame_.row, 0), distance);
    distance = 0.0F;
    params->fetch_float(kY, &distance, 0);
    add_scaled(at_point_, core::at(basis_frame_.row, 1), distance);
    distance = 0.0F;
    params->fetch_float(kZ, &distance, 0);
    add_scaled(at_point_, core::at(basis_frame_.row, 2), distance);
    return true;
}

// 0x0045DED0 / 0x0045DF20
bool GameEntity::obj_suspend(Struct* /*params*/, Script* /*script*/)
{
    hold_off(true);
    return true;
}
bool GameEntity::obj_unsuspend(Struct* /*params*/, Script* /*script*/)
{
    hold_off(false);
    return true;
}

// 0x0045E0B0: the distance in feet to the object the unnamed checksum names (required; none: 0).
bool GameEntity::obj_get_distance_to_object(Struct* params, Script* script) const
{
    uint32_t name = 0;
    params->read_hash_value(kUnnamed, &name, 1);
    auto* other = core::down_cast<GameEntity>(find_object(name));
    float distance = 0.0F;
    if (other) {
        const double dx = static_cast<double>(at_point_.x) - other->at_point_.x;
        const double dy = static_cast<double>(at_point_.y) - other->at_point_.y;
        const double dz = static_cast<double>(at_point_.z) - other->at_point_.z;
        distance = static_cast<float>(std::sqrt((dz * dz) + (dy * dy) + (dx * dx)) * kInchesToFeet);
    }
    script->args()->push_float(checksums::Name{kDistanceResult}, distance);
    return true;
}

// 0x0045E260: whether the velocity points backwards (against its matrix's at row).
bool GameEntity::obj_moving_backward(Struct* /*params*/, Script* /*script*/)
{
    const maths::Vec4& at = core::at(basis_frame_.row, 2);
    double along = (static_cast<double>(vel_.z) * at.z) + (static_cast<double>(vel_.y) * at.y);
    along += static_cast<double>(vel_.x) * at.x;
    return along < 0.0;
}

// 0x0045E2A0: whether the speed is within 0.1 of the unnamed float (0 if missing).
bool GameEntity::obj_speed_near(Struct* params, Script* /*script*/)
{
    float speed = 0.0F;
    params->fetch_float(kUnnamed, &speed, 0);
    return std::fabs(len_of({.x = vel_.x, .y = vel_.y, .z = vel_.z}) - speed) < kSpeedTolerance;
}

// 0x0045E310 / 0x0045E380: whether the speed is above / below the unnamed float (0 if missing).
bool GameEntity::obj_speed_above(Struct* params, Script* /*script*/)
{
    float speed = 0.0F;
    params->fetch_float(kUnnamed, &speed, 0);
    return len_of({.x = vel_.x, .y = vel_.y, .z = vel_.z}) > speed;
}
bool GameEntity::obj_speed_below(Struct* params, Script* /*script*/)
{
    float speed = 0.0F;
    params->fetch_float(kUnnamed, &speed, 0);
    return len_of({.x = vel_.x, .y = vel_.y, .z = vel_.z}) < speed;
}

// 0x0045E3F0: its type, in the calling script's parameters.
bool GameEntity::obj_get_type(Struct* /*params*/, Script* script)
{
    script->args()->put_name_key(checksums::Name{kTypeResult}, kind_tag());
    return true;
}

// 0x0045E410: whether it has a component of the type the unnamed checksum names. With none, the type looked for
// is `params`' own address [quirk].
bool GameEntity::obj_has_component(Struct* params, Script* /*script*/)
{
    uint32_t type = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(kUnnamed, &type, 1);
    return fetch_module(type) != nullptr;
}

// 0x0045E9E0
bool GameEntity::obj_get_collision(Struct* params, Script* /*script*/)
{
    CollisionFeeler feeler;
    float length = kDefaultLength;
    params->fetch_float(kLength, &length, 0);
    length *= feet_as_inches();
    float height = kDefaultHeight;
    params->fetch_float(kHeight, &height, 0);
    height *= feet_as_inches();
    float distance = 0.0F;
    params->fetch_float(kDistance, &distance, 0);
    distance *= feet_as_inches();
    float shift = 0.0F;
    params->fetch_float(kShift, &shift, 0);
    shift *= feet_as_inches();
    const maths::Vec4& right = core::at(basis_frame_.row, 0);
    const maths::Vec4& up = core::at(basis_frame_.row, 1);
    const maths::Vec4& at = core::at(basis_frame_.row, 2);
    // The start: pos + up * height + at * distance + right * shift (x87: some sums kept at 53 bits, as below).
    const float offset_cell_x = shift * right.x;
    const float shift_y = shift * right.y;
    const float offset_cell_z = shift * right.z;
    const double shift_w = static_cast<double>(shift) * right.w;
    const float at_x = distance * at.x;
    const float at_y = distance * at.y;
    const double at_z = static_cast<double>(distance) * at.z;
    const double at_w = static_cast<double>(distance) * at.w;
    const float up_x = height * up.x;
    const float up_y = height * up.y;
    const double up_z = static_cast<double>(height) * up.z;
    const double up_w = static_cast<double>(height) * up.w;
    auto w1 = static_cast<float>(up_w + at_point_.w);
    const double z1 = up_z + at_point_.z;
    float y1 = up_y + at_point_.y;
    float x1 = up_x + at_point_.x;
    w1 = static_cast<float>(w1 + at_w);
    auto z2 = static_cast<float>(z1 + at_z);
    y1 = y1 + at_y;
    x1 = x1 + at_x;
    const maths::Vec4 start = {.x = x1 + offset_cell_x, .y = y1 + shift_y, .z = z2 + offset_cell_z, .w = static_cast<float>(shift_w + w1)};
    feeler.kickoff() = start;
    maths::Vec4& end = feeler.get_end();
    end = start;
    // The end: `length` along the right (side), down (down) or ahead.
    if (params->has_bare_flag(kSide)) {
        for (int i = 0; i < 4; ++i) {
            end[i] = static_cast<float>((static_cast<double>(length) * right[i]) + end[i]);
        }
    } else if (params->has_bare_flag(kDown)) {
        for (int i = 0; i < 4; ++i) {
            end[i] = static_cast<float>(end[i] - (static_cast<double>(length) * up[i]));
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            end[i] = static_cast<float>((static_cast<double>(length) * at[i]) + end[i]);
        }
    }
    return feeler.get_hit_shape(true, false);
}

// 0x0045E9B0 / 0x0045E9D0
bool GameEntity::obj_pause(Struct* /*params*/, Script* /*script*/)
{
    composite_flags_ |= kCompositeFlagPaused;
    return true;
}
bool GameEntity::obj_unpause(Struct* /*params*/, Script* /*script*/)
{
    composite_flags_ &= ~kCompositeFlagPaused;
    return true;
}

// 0x0045DF70: x, y, z = its display matrix's at row, made unit length (left as it is if its length is 0).
bool GameEntity::obj_get_orientation(Struct* /*params*/, Script* script)
{
    float x = core::at(display_matrix_.row, 2).x;
    float y = core::at(display_matrix_.row, 2).y;
    float z = core::at(display_matrix_.row, 2).z;
    const double length =
        std::sqrt((static_cast<double>(z) * z) + (static_cast<double>(y) * y) + (static_cast<double>(x) * x));
    if (length > 0.0) {
        const double scale = 1.0 / length;
        x = static_cast<float>(x * scale);
        y = static_cast<float>(y * scale);
        z = static_cast<float>(z * scale);
    }
    script->args()->push_float(checksums::Name{kX}, x);
    script->args()->push_float(checksums::Name{kY}, y);
    script->args()->push_float(checksums::Name{kZ}, z);
    return true;
}

// 0x0045E020: dist = the distance to the node the unnamed checksum names (none: 0).
bool GameEntity::obj_get_dist_to_node(Struct* params, Script* script) const
{
    uint32_t name = 0;
    params->read_hash_value(kUnnamed, &name, 0);
    maths::Vec4 node_pos;
    get_position_from_structure(fetch_vertex(name, 1), &node_pos);
    const double dx = static_cast<double>(at_point_.x) - node_pos.x;
    const double dy = static_cast<double>(at_point_.y) - node_pos.y;
    const double dz = static_cast<double>(at_point_.z) - node_pos.z;
    script->args()->push_float(checksums::Name{kDist},
                               static_cast<float>(std::sqrt((dz * dz) + (dy * dy) + (dx * dx))));
    return true;
}

// 0x0045E140: dotprod = how much the object the unnamed checksum names (required) lies along its right axis (the
// cosine: both made unit length), and the angle that makes in degrees, less 90; none: both 0.
bool GameEntity::obj_get_orientation_to_object(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kUnnamed, &name, 1);
    auto* other = core::down_cast<GameEntity>(find_object(name));
    float dot = 0.0F;
    float angle = 0.0F;
    if (other) {
        maths::Vec4 right = {.x = core::at(basis_frame_.row, 0).x,
                             .y = core::at(basis_frame_.row, 0).y,
                             .z = core::at(basis_frame_.row, 0).z,
                             .w = 0.0F};
        right.make_unit();
        maths::Vec4 to_other = {.x = other->at_point_.x - at_point_.x,
                                .y = other->at_point_.y - at_point_.y,
                                .z = other->at_point_.z - at_point_.z,
                                .w = 0.0F};
        to_other.make_unit();
        const double sum = (static_cast<double>(to_other.z) * right.z) + (static_cast<double>(to_other.y) * right.y);
        dot = static_cast<float>(sum + (static_cast<double>(to_other.x) * right.x));
        angle = static_cast<float>((fast_a_cos_unrounded(dot) * kDegreesPerRadian) - kQuarterTurnDeg);
    }
    script->args()->push_float(checksums::Name{kDotProd}, dot);
    script->args()->push_float(checksums::Name{kAngleResult}, angle);
    return true;
}

// 0x0045E650: a new orientation (also as the display matrix): dir= (looking along it, the up axis kept as near
// world up as it can be: world x when dir is within 0.99 of vertical), else quat= (x, y, z; w from them), else
// the angles x=, y=, z= in degrees (each 0 if missing).
bool GameEntity::obj_set_orientation(Struct* params, Script* /*script*/)
{
    script::VectorValue value = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 1.0F};
    if (params->fetch_vec(kDir, &value, 0)) {
        maths::Mat4 m;
        core::at(m.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        core::at(m.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
        core::at(m.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        if (std::fabs(value.y) > kNearlyVertical) {
            core::at(m.row, 1) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        }
        core::at(m.row, 2) = {.x = value.x, .y = value.y, .z = value.z, .w = 0.0F};
        core::at(m.row, 2).make_unit();
        core::at(m.row, 0) = perpendicular(core::at(m.row, 1), core::at(m.row, 2));
        core::at(m.row, 0).make_unit();
        core::at(m.row, 1) = perpendicular(core::at(m.row, 2), core::at(m.row, 0));
        core::at(m.row, 1).make_unit();
        basis_frame_ = m;
        display_matrix_ = m;
        return true;
    }
    if (params->fetch_vec(kQuat, &value, 0)) {
        double sum = (static_cast<double>(value.z) * value.z) + (static_cast<double>(value.y) * value.y);
        sum += static_cast<double>(value.x) * value.x;
        const std::array<float, 4> quat = {{value.x, value.y, value.z, static_cast<float>(std::sqrt(1.0 - sum))}};
        quat_to_matrix(quat, &basis_frame_);
        display_matrix_ = basis_frame_;
        return true;
    }
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    params->fetch_float(kX, &x, 0);
    params->fetch_float(kY, &y, 0);
    params->fetch_float(kZ, &z, 0);
    x *= kRadiansPerDegree;
    y *= kRadiansPerDegree;
    z *= kRadiansPerDegree;
    maths::Mat4 m;
    m.set_from_euler_angles(x, y, z);
    basis_frame_ = m;
    display_matrix_ = m;
    return true;
}

// 0x0045E940: quat = its orientation as a quaternion's x, y, z (negated when w < 0, so that w >= 0).
bool GameEntity::obj_get_quat(Struct* /*params*/, Script* script)
{
    std::array<float, 4> quat{};
    matrix_to_quat(quat, &basis_frame_);
    if (core::at(quat, 3) < 0.0F) {
        core::at(quat, 0) = -core::at(quat, 0);
        core::at(quat, 1) = -core::at(quat, 1);
        core::at(quat, 2) = -core::at(quat, 2);
    }
    script->args()->put_vec3(checksums::Name{kQuat}, {.x = core::at(quat, 0), .y = core::at(quat, 1), .z = core::at(quat, 2)});
    return true;
}

// 0x0045ED30: the nearest other object of `type` in its manager's list (the first one met when none is nearer
// than 5322672), or null.
GameEntity* GameEntity::find_nearest_object_of_type(uint32_t type)
{
    ObjectSearch search;
    auto* object = core::down_cast<GameEntity>(search.first_item_of_type(manager_object_list(park_owner()), type));
    if (object == this) {
        object = core::down_cast<GameEntity>(search.next_item_of_type());
    }
    if (!object) {
        return nullptr;
    }
    GameEntity* nearest = object;
    float nearest_distance = kFarAway;
    for (; object; object = core::down_cast<GameEntity>(search.next_item_of_type())) {
        const double dx = static_cast<double>(object->at_point_.x) - at_point_.x;
        const double dy = static_cast<double>(object->at_point_.y) - at_point_.y;
        const double dz = static_cast<double>(object->at_point_.z) - at_point_.z;
        const double distance = std::sqrt((dz * dz) + (dy * dy) + (dx * dx));
        if (object != this && distance < nearest_distance) {
            nearest_distance = static_cast<float>(distance);
            nearest = object;
        }
    }
    return nearest;
}

// 0x0045EF30: whether the nearest object of type 0xBAF9B341 lies more than the unnamed angle (degrees, required)
// off its at axis, measured about its up axis. None: false.
bool GameEntity::obj_nearest_outside_angle(Struct* params, Script* /*script*/)
{
    const GameEntity* other = find_nearest_object_of_type(kNearestType);
    if (!other) {
        return false;
    }
    float angle = 0.0F; // (uninitialised in the original: a missing angle compares with stack garbage)
    params->fetch_float(kUnnamed, &angle, 1);
    angle *= kRadiansPerDegree;
    const maths::Vec4 offset = {.x = other->at_point_.x - at_point_.x, .y = other->at_point_.y - at_point_.y,
                                .z = other->at_point_.z - at_point_.z, .w = other->at_point_.w - at_point_.w};
    return std::fabs(maths::get_angle_in_plane(basis_frame_, offset, 2, 1)) > angle;
}

} // namespace objects
