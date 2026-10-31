// GameObjects::GameEntity - the object made of components (skaters, peds, cars, level objects) - and the base of its
// components, GameObjects::ObjectModule (entity.h). From the disassembly of 0x0045D720-0x0045EE20 (methods __thiscall,
// RET n = 4 * arguments; the member functions scripts call are entries of the table at 0x00683268, filled in by
// 0x006425D0, all `bool (GameEntity::*)(CStruct* params, CScript* script)`).

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/entity_hub.h"

namespace objects {

float g_pi{3.1415927f};

using script::Array;
using script::MemberFunctionEntry;
using script::Script;
using script::Struct;

float g_composite_acos_a0{1.5707288f};
float g_composite_acos_a1{0.2121144f};
float g_composite_acos_a2{0.074261f};
float g_composite_acos_a3{-0.0187293f};
float g_composite_feet_to_inches{12.0f};

namespace {

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
constexpr float kInchesToFeet = 1.0f / 12.0f;
constexpr float kSpeedTolerance = 0.1f;
constexpr float kDegreesPerRadian = 57.2957763671875f;    // 0x00645468
constexpr float kQuarterTurnDeg = 90.0f;                  // 0x00645EB0
constexpr float kRadiansPerDegree = 0.01745329238474369f; // 0x00645464
constexpr float kNearlyVertical = 0.99f;                  // 0x00645450
constexpr float kFarAway = 5322672.0f;                    // (0x4AA26F60)

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
void quat_to_matrix(const float* quat, maths::Mat4* matrix)
{
    reinterpret_cast<const maths::QuatRotation*>(quat)->fetch_mat(matrix);
}
void matrix_to_quat(float* quat, const maths::Mat4* matrix)
{
    reinterpret_cast<maths::QuatRotation*>(quat)->from_matrix(*matrix);
}
// A manager's object list.
memory::ListNode* manager_object_list(ObjectHubBase* manager) { return manager->fetch_object_set(); }
// maths::fast_a_cos (0x00403EF0) as its callers here see it: the x87 result is used before it is rounded to a float.
double fast_a_cos_unrounded(float value)
{
    const double a0 = g_composite_acos_a0;
    const double a1 = g_composite_acos_a1;
    const double a2 = g_composite_acos_a2;
    const double a3 = g_composite_acos_a3;
    const double pi = g_pi;
    if (value < 0.0f) {
        double x = -static_cast<double>(value);
        double root = std::sqrt(1.0 - x);
        return pi - (((x * a3 + a2) * x - a1) * x + a0) * root;
    }
    double x = value;
    double root = std::sqrt(1.0 - x);
    return (((x * a3 + a2) * x - a1) * x + a0) * root;
}
// a x b in double precision, each component rounded once; w = 0.
maths::Vec4 perpendicular(const maths::Vec4& a, const maths::Vec4& b)
{
    return {static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y),
            static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z),
            static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x), 0.0f};
}

// |v| as the original's x87 code works it out: (x*x + y*y) + z*z in double precision, not rounded to a float.
double len_of(float x, float y, float z)
{
    double x2 = static_cast<double>(x) * x;
    double y2 = static_cast<double>(y) * y;
    double z2 = static_cast<double>(z) * z;
    return std::sqrt(x2 + y2 + z2);
}

// pos += row * scale, as the original rounds it: the x and y products stay in double precision, the z and w ones
// are rounded to floats first.
void add_scaled(maths::Vec4& pos, const maths::Vec4& row, float scale)
{
    double x = static_cast<double>(scale) * row.x;
    double y = static_cast<double>(scale) * row.y;
    float z = scale * row.z;
    float w = scale * row.w;
    pos.x = static_cast<float>(x + pos.x);
    pos.y = static_cast<float>(y + pos.y);
    pos.z = z + pos.z;
    pos.w = w + pos.w;
}

// Debug output of an object's details: compiled out of this build (the text is still read).
inline void print_object_details(const char* /*text*/) {}

// The collision feeler (Nx's ray, 0xB0 bytes; its code isn't in this file): constructed by 0x00417760, its line
// from +0x00 to +0x10, tested by 0x004172F0; what it hit is held by a smart pointer at +0x98, its only destructor.
class CollisionFeeler {
public:
    CollisionFeeler() { reinterpret_cast<render::ProbeRay*>(bytes_)->make_new(); }
    ~CollisionFeeler() { reinterpret_cast<objects::SharedRef<objects::SharedBase>*>(bytes_ + 0x98)->~SharedRef(); }
    CollisionFeeler(const CollisionFeeler&) = delete;
    CollisionFeeler& operator=(const CollisionFeeler&) = delete;
    maths::Vec4& kickoff() { return *reinterpret_cast<maths::Vec4*>(bytes_); }
    maths::Vec4& get_end() { return *reinterpret_cast<maths::Vec4*>(bytes_ + 0x10); }
    bool get_hit_shape(bool a, bool b) { return reinterpret_cast<render::ProbeRay*>(bytes_)->get_hit_shape(a, b) != 0; }

private:
    alignas(16) uint8_t bytes_[0xB0];
};

float feet_as_inches() { return g_composite_feet_to_inches; } // 12

} // namespace

// --- Entity: a member function table entry -------------------------------------------------------------------

// 0x0045D850
bool Entity::invoke_method(MemberFunctionEntry* entry, Struct* params, Script* script)
{
    if (entry->method_code_) {
        void* self = reinterpret_cast<uint8_t*>(this) + entry->this_adjustment_;
        return reinterpret_cast<bool(__thiscall*)(void*, Struct*, Script*)>(entry->method_code_)(self, params, script);
    }
    return invoke_method_by_name(entry->callsign_, params, script);
}

// --- ObjectModule -------------------------------------------------------------------------------------------

void ObjectModule::operator delete(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

// 0x0045D740 (0x0045D780 the deleting one)
ObjectModule::~ObjectModule()
{
    if (flag_bits_ & kComponentFlagScriptWaiting)
        script::component_destroyed_all_scripts(this);
}

// 0x0051B970
void ObjectModule::tick_frame() { hold_off(true); }

// 0x0045D760
void ObjectModule::hold_off(bool suspend)
{
    if (suspend)
        flag_bits_ |= kComponentFlagSuspended;
    else
        flag_bits_ &= ~kComponentFlagSuspended;
}

// 0x0045D720 (a jump to slot 12 with the entry's name in place of the entry)
int ObjectModule::invoke_method(MemberFunctionEntry* entry, Struct* params, Script* script)
{
    return invoke_method_by_name(entry->callsign_, params, script);
}

// --- GameEntity: life -----------------------------------------------------------------------------------

// 0x0045E510
GameEntity::GameEntity()
{
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    at_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
    basis_frame_.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    basis_frame_.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    basis_frame_.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    basis_frame_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    display_matrix_.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    display_matrix_.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    display_matrix_.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    display_matrix_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    object_flags_ |= kObjectFlagHasPosition;
}

// 0x0045D890
GameEntity::~GameEntity()
{
    composite_flags_ |= kCompositeFlagDestroying;
    while (attached_modules_) {
        ObjectModule* component = attached_modules_;
        attached_modules_ = component->following_;
        unregister_component(component);
        delete component;
    }
}

// --- GameEntity: the frame ------------------------------------------------------------------------------

// 0x0045D920
void GameEntity::tick_frame()
{
    if (composite_flags_ & kCompositeFlagPaused)
        return;
    if (!(composite_flags_ & kCompositeFlagSuspended)) {
        if (own_script_)
            own_script_->tick_frame();
        if (object_flags_ & kObjectFlagDead)
            return;
        own_update();
    }
    for (ObjectModule* component = attached_modules_; component; component = component->following_) {
        if (component->flag_bits_ & kComponentFlagSuspended)
            continue;
        component->tick_frame();
        if (object_flags_ & kObjectFlagDead)
            break;
    }
    at_point_.w = 1.0f;
    composite_flags_ &= ~kCompositeFlagTeleported;
}

// 0x0045D990
void GameEntity::conceal(bool hide)
{
    if (hide == ((composite_flags_ & kCompositeFlagHidden) != 0))
        return;
    if (hide)
        composite_flags_ |= kCompositeFlagHidden;
    else
        composite_flags_ &= ~kCompositeFlagHidden;
    for (ObjectModule* component = attached_modules_; component; component = component->following_)
        component->conceal(hide);
}

// 0x0045D9E0
void GameEntity::hold_off(bool suspend)
{
    if (suspend == ((composite_flags_ & kCompositeFlagSuspended) != 0))
        return;
    if (suspend)
        composite_flags_ |= kCompositeFlagSuspended;
    else
        composite_flags_ &= ~kCompositeFlagSuspended;
    for (ObjectModule* component = attached_modules_; component; component = component->following_)
        component->hold_off(suspend);
}

// 0x0045E4C0
void GameEntity::mark_jumped_ahead(bool teleport_components)
{
    composite_flags_ |= kCompositeFlagTeleported;
    if (teleport_components) {
        for (ObjectModule* component = attached_modules_; component; component = component->following_)
            component->jump_to();
    }
}

// 0x0045E500
bool GameEntity::should_update_scripts() { return !(composite_flags_ & kCompositeFlagPaused); }

// 0x0045D7C0
void GameEntity::flag_as_dead()
{
    for (ObjectModule* component = attached_modules_; component; component = component->following_)
        component->object_marked_dead();
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
        while (last->following_)
            last = last->following_;
        last->following_ = component;
    }
    component->entity_ = this;
    enrol_module(component);
}

// 0x0045DA90
void GameEntity::build_modules_from_list(Array* components, Struct* extra)
{
    uint32_t* elements = components->find_array_value();
    int count = static_cast<int>(components->extent_);
    for (int i = 0; i < count; ++i)
        create_component_from_structure(reinterpret_cast<Struct*>(elements[i]), extra);
}

// 0x0045EE20. With no component= the type looked up is `params`' own address [quirk]; a type the manager can't
// make gives a null component, added all the same [bug: crash]; so does a failed allocation of the merged copy.
void GameEntity::create_component_from_structure(Struct* params, Struct* extra)
{
    uint32_t type = reinterpret_cast<uint32_t>(params);
    params->read_hash_value(kComponent, &type, 1);
    ObjectModule* component = spawn_module(type);
    attach_module(component);
    if (!extra) {
        component->configure_from_data(params);
        return;
    }
    Struct* memory = script::spawn_param_bag();
    Struct* merged = memory ? memory->prepare_all() : nullptr;
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
    if (params->read_hash_value(kName, &id, 0))
        change_serial(id);
    get_position_from_structure(params, &at_point_);
    get_orientation_from_structure(params, &basis_frame_);
    if (params->has_bare_flag(kPermanent))
        object_flags_ |= kObjectFlagLocked;
}

// 0x0045DB30
void GameEntity::seal()
{
    for (ObjectModule* component = attached_modules_; component; component = component->following_)
        component->seal();
    composite_flags_ |= kCompositeFlagFinalized;
    composite_flags_ |= kCompositeFlagFinalized2;
    for (ObjectModule* component = attached_modules_; component; component = component->following_)
        component->jump_to();
}

// 0x0045DB90
ObjectModule* GameEntity::fetch_module(uint32_t type)
{
    for (ObjectModule* component = attached_modules_; component; component = component->following_) {
        if (component->kind_tag_ == type)
            return component;
    }
    return nullptr;
}

// 0x0045E450
bool GameEntity::invoke_method_by_name(uint32_t name, Struct* params, Script* script)
{
    for (ObjectModule* component = attached_modules_; component; component = component->following_) {
        int result = component->invoke_method_by_name(name, params, script);
        if (result == 0)
            return false;
        if (result == 1)
            return true;
    }
    return Entity::invoke_method_by_name(name, params, script);
}

// --- GameEntity: the member functions scripts call ------------------------------------------------------

// 0x0045D7F0: the unnamed string (the details' title) is read for the debug output.
bool GameEntity::obj_print_details(Struct* params, Script* /*script*/)
{
    const char* text = reinterpret_cast<const char*>(params);
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
    params->fetch_vec(kPosition, reinterpret_cast<script::VectorValue*>(&at_point_), 0);
    return true;
}

// 0x0045DBC0: pos = the position, in the calling script's parameters.
bool GameEntity::obj_get_position(Struct* /*params*/, Script* script)
{
    script->args_->put_vec3(kPos, at_point_.x, at_point_.y, at_point_.z);
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
    script->args_->put_vec3(kVel, vel_.x, vel_.y, vel_.z);
    return true;
}

// 0x0045DC80: speed = the velocity's length.
bool GameEntity::get_speed(Struct* /*params*/, Script* script)
{
    script->args_->push_float(kSpeed, static_cast<float>(len_of(vel_.x, vel_.y, vel_.z)));
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
bool GameEntity::obj_is_hidden(Struct* /*params*/, Script* /*script*/)
{
    return (composite_flags_ & kCompositeFlagHidden) != 0;
}

// 0x0045DD80: moves along its own axes by x=, y= and z= (each 0 if missing), all four components of each row.
bool GameEntity::obj_move_local(Struct* params, Script* /*script*/)
{
    float distance = 0.0f;
    params->fetch_float(kX, &distance, 0);
    add_scaled(at_point_, basis_frame_.row[0], distance);
    distance = 0.0f;
    params->fetch_float(kY, &distance, 0);
    add_scaled(at_point_, basis_frame_.row[1], distance);
    distance = 0.0f;
    params->fetch_float(kZ, &distance, 0);
    add_scaled(at_point_, basis_frame_.row[2], distance);
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
bool GameEntity::obj_get_distance_to_object(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kUnnamed, &name, 1);
    auto* other = static_cast<GameEntity*>(find_object(name));
    float distance = 0.0f;
    if (other) {
        double dx = static_cast<double>(at_point_.x) - other->at_point_.x;
        double dy = static_cast<double>(at_point_.y) - other->at_point_.y;
        double dz = static_cast<double>(at_point_.z) - other->at_point_.z;
        distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx) * kInchesToFeet);
    }
    script->args_->push_float(kDistanceResult, distance);
    return true;
}

// 0x0045E260: whether the velocity points backwards (against its matrix's at row).
bool GameEntity::obj_moving_backward(Struct* /*params*/, Script* /*script*/)
{
    const maths::Vec4& at = basis_frame_.row[2];
    double along = static_cast<double>(vel_.z) * at.z + static_cast<double>(vel_.y) * at.y;
    along += static_cast<double>(vel_.x) * at.x;
    return along < 0.0;
}

// 0x0045E2A0: whether the speed is within 0.1 of the unnamed float (0 if missing).
bool GameEntity::obj_speed_near(Struct* params, Script* /*script*/)
{
    float speed = 0.0f;
    params->fetch_float(kUnnamed, &speed, 0);
    return std::fabs(len_of(vel_.x, vel_.y, vel_.z) - speed) < kSpeedTolerance;
}

// 0x0045E310 / 0x0045E380: whether the speed is above / below the unnamed float (0 if missing).
bool GameEntity::obj_speed_above(Struct* params, Script* /*script*/)
{
    float speed = 0.0f;
    params->fetch_float(kUnnamed, &speed, 0);
    return len_of(vel_.x, vel_.y, vel_.z) > speed;
}
bool GameEntity::obj_speed_below(Struct* params, Script* /*script*/)
{
    float speed = 0.0f;
    params->fetch_float(kUnnamed, &speed, 0);
    return len_of(vel_.x, vel_.y, vel_.z) < speed;
}

// 0x0045E3F0: its type, in the calling script's parameters.
bool GameEntity::obj_get_type(Struct* /*params*/, Script* script)
{
    script->args_->put_name_key(kTypeResult, kind_tag_);
    return true;
}

// 0x0045E410: whether it has a component of the type the unnamed checksum names. With none, the type looked for
// is `params`' own address [quirk].
bool GameEntity::obj_has_component(Struct* params, Script* /*script*/)
{
    uint32_t type = reinterpret_cast<uint32_t>(params);
    params->read_hash_value(kUnnamed, &type, 1);
    return fetch_module(type) != nullptr;
}

// 0x0045E9E0
bool GameEntity::obj_get_collision(Struct* params, Script* /*script*/)
{
    CollisionFeeler feeler;
    float length = 3.0f;
    params->fetch_float(kLength, &length, 0);
    length *= feet_as_inches();
    float height = 3.0f;
    params->fetch_float(kHeight, &height, 0);
    height *= feet_as_inches();
    float distance = 0.0f;
    params->fetch_float(kDistance, &distance, 0);
    distance *= feet_as_inches();
    float shift = 0.0f;
    params->fetch_float(kShift, &shift, 0);
    shift *= feet_as_inches();
    const maths::Vec4& right = basis_frame_.row[0];
    const maths::Vec4& up = basis_frame_.row[1];
    const maths::Vec4& at = basis_frame_.row[2];
    // The start: pos + up * height + at * distance + right * shift (x87: some sums kept at 53 bits, as below).
    float offset_cell_x = shift * right.x, shift_y = shift * right.y, offset_cell_z = shift * right.z;
    double shift_w = static_cast<double>(shift) * right.w;
    float at_x = distance * at.x, at_y = distance * at.y;
    double at_z = static_cast<double>(distance) * at.z, at_w = static_cast<double>(distance) * at.w;
    float up_x = height * up.x, up_y = height * up.y;
    double up_z = static_cast<double>(height) * up.z, up_w = static_cast<double>(height) * up.w;
    float w1 = static_cast<float>(up_w + at_point_.w);
    double z1 = up_z + at_point_.z;
    float y1 = up_y + at_point_.y;
    float x1 = up_x + at_point_.x;
    w1 = static_cast<float>(w1 + at_w);
    float z2 = static_cast<float>(z1 + at_z);
    y1 = y1 + at_y;
    x1 = x1 + at_x;
    maths::Vec4 start = {x1 + offset_cell_x, y1 + shift_y, z2 + offset_cell_z, static_cast<float>(shift_w + w1)};
    feeler.kickoff() = start;
    maths::Vec4& end = feeler.get_end();
    end = start;
    // The end: `length` along the right (side), down (down) or ahead.
    if (params->has_bare_flag(kSide)) {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>(static_cast<double>(length) * (&right.x)[i] + (&end.x)[i]);
    } else if (params->has_bare_flag(kDown)) {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>((&end.x)[i] - static_cast<double>(length) * (&up.x)[i]);
    } else {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>(static_cast<double>(length) * (&at.x)[i] + (&end.x)[i]);
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
    float x = display_matrix_.row[2].x;
    float y = display_matrix_.row[2].y;
    float z = display_matrix_.row[2].z;
    double length = std::sqrt(static_cast<double>(z) * z + static_cast<double>(y) * y + static_cast<double>(x) * x);
    if (length > 0.0) {
        double scale = 1.0 / length;
        x = static_cast<float>(x * scale);
        y = static_cast<float>(y * scale);
        z = static_cast<float>(z * scale);
    }
    script->args_->push_float(kX, x);
    script->args_->push_float(kY, y);
    script->args_->push_float(kZ, z);
    return true;
}

// 0x0045E020: dist = the distance to the node the unnamed checksum names (none: 0).
bool GameEntity::obj_get_dist_to_node(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kUnnamed, &name, 0);
    maths::Vec4 node_pos;
    get_position_from_structure(fetch_vertex(name, 1), &node_pos);
    double dx = static_cast<double>(at_point_.x) - node_pos.x;
    double dy = static_cast<double>(at_point_.y) - node_pos.y;
    double dz = static_cast<double>(at_point_.z) - node_pos.z;
    script->args_->push_float(kDist, static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx)));
    return true;
}

// 0x0045E140: dotprod = how much the object the unnamed checksum names (required) lies along its right axis (the
// cosine: both made unit length), and the angle that makes in degrees, less 90; none: both 0.
bool GameEntity::obj_get_orientation_to_object(Struct* params, Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(kUnnamed, &name, 1);
    auto* other = static_cast<GameEntity*>(find_object(name));
    float dot = 0.0f;
    float angle = 0.0f;
    if (other) {
        maths::Vec4 right = {basis_frame_.row[0].x, basis_frame_.row[0].y, basis_frame_.row[0].z, 0.0f};
        right.make_unit();
        maths::Vec4 to_other = {other->at_point_.x - at_point_.x, other->at_point_.y - at_point_.y,
                                other->at_point_.z - at_point_.z, 0.0f};
        to_other.make_unit();
        double sum = static_cast<double>(to_other.z) * right.z + static_cast<double>(to_other.y) * right.y;
        dot = static_cast<float>(sum + static_cast<double>(to_other.x) * right.x);
        angle = static_cast<float>(fast_a_cos_unrounded(dot) * kDegreesPerRadian - kQuarterTurnDeg);
    }
    script->args_->push_float(kDotProd, dot);
    script->args_->push_float(kAngleResult, angle);
    return true;
}

// 0x0045E650: a new orientation (also as the display matrix): dir= (looking along it, the up axis kept as near
// world up as it can be: world x when dir is within 0.99 of vertical), else quat= (x, y, z; w from them), else
// the angles x=, y=, z= in degrees (each 0 if missing).
bool GameEntity::obj_set_orientation(Struct* params, Script* /*script*/)
{
    script::VectorValue value = {0.0f, 0.0f, 1.0f, 1.0f};
    if (params->fetch_vec(kDir, &value, 0)) {
        maths::Mat4 m;
        m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
        m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (std::fabs(value.y) > kNearlyVertical)
            m.row[1] = {1.0f, 0.0f, 0.0f, 1.0f};
        m.row[2] = {value.x, value.y, value.z, 0.0f};
        m.row[2].make_unit();
        m.row[0] = perpendicular(m.row[1], m.row[2]);
        m.row[0].make_unit();
        m.row[1] = perpendicular(m.row[2], m.row[0]);
        m.row[1].make_unit();
        basis_frame_ = m;
        display_matrix_ = m;
        return true;
    }
    if (params->fetch_vec(kQuat, &value, 0)) {
        double sum = static_cast<double>(value.z) * value.z + static_cast<double>(value.y) * value.y;
        sum += static_cast<double>(value.x) * value.x;
        float quat[4] = {value.x, value.y, value.z, static_cast<float>(std::sqrt(1.0 - sum))};
        quat_to_matrix(quat, &basis_frame_);
        display_matrix_ = basis_frame_;
        return true;
    }
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
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
    float quat[4];
    matrix_to_quat(quat, &basis_frame_);
    if (quat[3] < 0.0f) {
        quat[0] = -quat[0];
        quat[1] = -quat[1];
        quat[2] = -quat[2];
    }
    script->args_->put_vec3(kQuat, quat[0], quat[1], quat[2]);
    return true;
}

// 0x0045ED30: the nearest other object of `type` in its manager's list (the first one met when none is nearer
// than 5322672), or null.
GameEntity* GameEntity::find_nearest_object_of_type(uint32_t type)
{
    ObjectSearch search;
    auto* object = static_cast<GameEntity*>(search.first_item_of_type(manager_object_list(park_owner_), type));
    if (object == this)
        object = static_cast<GameEntity*>(search.next_item_of_type());
    if (!object)
        return nullptr;
    GameEntity* nearest = object;
    float nearest_distance = kFarAway;
    for (; object; object = static_cast<GameEntity*>(search.next_item_of_type())) {
        double dx = static_cast<double>(object->at_point_.x) - at_point_.x;
        double dy = static_cast<double>(object->at_point_.y) - at_point_.y;
        double dz = static_cast<double>(object->at_point_.z) - at_point_.z;
        double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
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
    GameEntity* other = find_nearest_object_of_type(kNearestType);
    if (!other)
        return false;
    float angle = 0.0f; // (uninitialised in the original: a missing angle compares with stack garbage)
    params->fetch_float(kUnnamed, &angle, 1);
    angle *= kRadiansPerDegree;
    maths::Vec4 offset = {other->at_point_.x - at_point_.x, other->at_point_.y - at_point_.y,
                          other->at_point_.z - at_point_.z, other->at_point_.w - at_point_.w};
    return std::fabs(maths::get_angle_in_plane(basis_frame_, offset, 2, 1)) > angle;
}

} // namespace objects
