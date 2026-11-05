// The rigid body component: its life, shapes, set-up and script member functions (engine/objects/rigidbody_component.h).

#include "engine/objects/rigidbody_component.h"

#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <gsl/pointers>
#include <array>
#include <gsl/assert>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/maths/quat_rotation.h"
#include "base/maths/random.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/script/fwd.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/sys/startup_init.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/objects/objects_shared.h"
#include "base/core/checksums.h"
#include "base/maths/vec4.h"

namespace objects {
namespace {
constexpr float kGravity = 386.3999938964844F;    // inches a second squared
constexpr int kBoxContacts = 8;                   // contact points of a box, plane and cylinder body
constexpr int kPlaneContacts = 5;
constexpr int kCylinderContacts = 6;
constexpr float kBoxWidth = 32.0F; // a default box body's size, in inches
constexpr float kBoxDepth = 16.0F;
constexpr float kBoxHeight = 42.0F;
constexpr float kCylinderRadius = 12.0F;
constexpr float kCylinderHeight = 60.0F;
constexpr int kCylinderEdges = 6;
constexpr float kPusherHeight = 77.0F;
constexpr int kLastContact = 7;
} // namespace

std::array<maths::Vec4, kContactCount>& BodyDynamicsModule::world_contact_points() { return objects_shared().rigid_body_world_contact_points(); }
std::array<maths::Vec4, kContactCount>& BodyDynamicsModule::world_sphere_centers() { return objects_shared().rigid_body_world_sphere_centers(); }
std::array<maths::Vec4, 2>& BodyDynamicsModule::world_spring_points() { return objects_shared().rigid_body_world_spring_points(); }
std::span<BodyDynamicsModule::Contact> BodyDynamicsModule::get_contacts() { return objects_shared().rigid_body_contacts(); }

namespace {
constexpr uint32_t kRadius = 0xC48391A5; // Radius
constexpr uint32_t kRigidBodySounds = 0xB9738EBA; // RigidBodySounds
constexpr uint32_t kCollideSound = 0xBF8E0ACE; // CollideSound
constexpr uint32_t kBounceSound = 0x1FB2F60C; // BounceSound
constexpr uint32_t kCollideMuteDelay = 0x3818EE47; // CollideMuteDelay
constexpr uint32_t kGlobalCollideMuteDelay = 0xAD67A34D; // GlobalCollideMuteDelay
constexpr uint32_t kBounceVelocityCallbackThreshold = 0x4511CA8D; // BounceVelocityCallbackThreshold
constexpr uint32_t kBounceVelocityFullSpeed = 0x8FC6519F; // BounceVelocityFullSpeed
constexpr uint32_t kRigidbodyState = 0x61FBB62C; // rigidbody_state
constexpr uint32_t kAsleep = 0x4484B712; // Asleep
constexpr uint32_t kDisabled = 0x7B01911C; // Disabled
constexpr uint32_t kAwake = 0x99A34896; // Awake
constexpr uint32_t kVel = 0x0C4C809E; // Vel
constexpr uint32_t kRotVel = 0xFB1A83B2; // RotVel
constexpr uint32_t kCenterOfMass = 0x737612C6; // center_of_mass
constexpr uint32_t kConstAcc = 0x6FAE5C9A; // const_acc
constexpr uint32_t kCoeffRestitution = 0x60B4860D; // coeff_restitution
constexpr uint32_t kCoeffFriction = 0xF9D75930; // coeff_friction
constexpr uint32_t kSpringConst = 0xF1ECB30F; // spring_const
constexpr uint32_t kLinearDamping = 0xDE76C3A7; // text unknown (the linear damping)
constexpr uint32_t kAngularDamping = 0x9DA897CF; // text unknown (the angular damping)
constexpr uint32_t kLinearVelocitySleepPoint = 0xC11E4DD8; // linear_velocity_sleep_point
constexpr uint32_t kAngularVelocitySleepPoint = 0xA457ECE3; // angular_velocity_sleep_point
constexpr uint32_t kSkaterCollisionImpulseFactor = 0x3390D1E5; // skater_collision_impulse_factor
constexpr uint32_t kSkaterCollisionRotationFactor = 0x5A18F61F; // skater_collision_rotation_factor
constexpr uint32_t kSkaterCollisionRadius = 0x85F112C1; // skater_collision_radius
constexpr uint32_t kSkaterCollisionApplicationRadius = 0x8AB2DB68; // skater_collision_application_radius
constexpr uint32_t kSkaterCollisionAssent = 0xE9156E6D; // skater_collision_assent
constexpr uint32_t kPusherQuietTime = 0x1F4D0CA5; // text unknown (the seconds the pushers are not felt after a push)
constexpr uint32_t kCollideScript = 0x58CDC0F0; // CollideScript
constexpr uint32_t kBounceScript = 0x2D075F90; // BounceScript
constexpr uint32_t kSettleScript = 0x571917FA; // SettleScript
constexpr uint32_t kStuckScript = 0xCCB84047; // stuckscript
constexpr uint32_t kCallbackParams = 0x0DD85BDA; // CallbackParams
constexpr uint32_t kSoundType = 0x94956E48; // SoundType
constexpr uint32_t kDieUponSettling = 0x5E51924F; // DieUponSettling
constexpr uint32_t kConstraints = 0xB77BE94A; // constraints
constexpr uint32_t kContacts = 0xCCBFEA8C; // Contacts
constexpr uint32_t kSpheres = 0x5B6A8D18; // text unknown (the list of spheres)
constexpr uint32_t kBox = 0xF756B7C5; // Box
constexpr uint32_t kPyramid = 0x1CB1A2B6; // Pyramid
constexpr uint32_t kCylinder = 0x64FBA415; // Cylinder
constexpr uint32_t kTriangle = 0x20689278; // Triangle
constexpr uint32_t kHeight = 0x0AB21AF0; // Height
constexpr uint32_t kDepth = 0x055CE396; // Depth
constexpr uint32_t kTopRadius = 0x937E3B29; // top_radius
constexpr uint32_t kBottomRadius = 0xE2AC8C3A; // bottom_radius
constexpr uint32_t kEdges = 0x4055F24A; // Edges
constexpr uint32_t kDimensions = 0x1D82745A; // Dimensions
constexpr uint32_t kDirectedFriction = 0xC35CAC0D; // directed_friction
constexpr uint32_t kNone = 0x806FFF30; // None
constexpr uint32_t kAngles = 0x9D2D0915; // Angles
constexpr uint32_t kModelOffset = 0x851211B0; // model_offset
constexpr uint32_t kMassOverMoment = 0x967CD3AE; // mass_over_moment
constexpr uint32_t kDisablePlayerCollision = 0xA92B9954; // disable_player_collision
constexpr uint32_t kPos = 0x7F261953; // Pos
constexpr uint32_t kCenterOfMassPos = 0x77808D3C; // CenterOfMassPos

constexpr float kHalf = 0.5F;                             // 0x006453BC
constexpr float kRadiansPerDegree = 0.01745329238474369F; // 0x00645464
constexpr double kAssent = 0.4363323251406352;            // 0x006463F0 (25 degrees)
constexpr float kPyramidBase = -0.6000000238418579F;      // 0x006463E8
constexpr float kPyramidTip = 1.600000023841858F;         // 0x006463E4
constexpr float kAlmostTwoPi = 6.2829999923706055F;       // 0x006463EC
constexpr float kBigShape = 30.0F;                        // 0x00646240
constexpr float kShapeScale = 0.03333333507180214F;       // 0x00646430

// The quaternion's matrix.
void quat_to_matrix(const maths::QuatRotation* quat, maths::Mat4* matrix) { quat->fetch_mat(matrix); }

// A structure copied into a new one (from the script pools), and one deleted.
gsl::owner<script::Struct*> copy_of(script::Struct* source)
{
    gsl::owner<script::Struct*> copy = script::spawn_param_bag();
    if (copy) {
        new (copy) script::Struct;
    }
    Expects(copy != nullptr);
    copy->merge_structure(source); // (out of memory: through null, as the original)
    return copy;
}
void erase_one(script::Struct* structure)
{
    structure->get_clear();
    script::delete_struct(structure);
}

// An array of `size` bytes from the default heap in place of `old` (freed).
template <typename T>
T* reallocate(T* old, uint32_t size)
{
    if (old) {
        memory::Hub::dealloc(old);
    }
    return static_cast<T*>(memory::alloc(size));
}

// A script vector's coordinates as a point (w 0).
maths::Vec4 point_of(const script::Vector* vector)
{
    return {.x = vector->x(), .y = vector->y(), .z = vector->z(), .w = 0.0F};
}

} // namespace

// 0x0042EF00
gsl::owner<ObjectModule*> BodyDynamicsModule::fabricate() { return new BodyDynamicsModule; }

// 0x0042E3F0
BodyDynamicsModule::BodyDynamicsModule()
    : skater_assent_cos_(static_cast<float>(std::cos(kAssent))),
      skater_assent_sin_(static_cast<float>(std::sin(kAssent)))
{
    // (capsule_ is constructed first, as a member)
    orientation_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    set_kind_tag(kType);
    steady_accel_ = {.x = 0.0F, .y = -kGravity, .z = 0.0F, .w = 1.0F};
    model_offset_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}

// 0x0042EF60 (0x00430DF0 the deleting one)
BodyDynamicsModule::~BodyDynamicsModule()
{
    for (void* array : {static_cast<void*>(contacts_), static_cast<void*>(steer_friction_),
                        static_cast<void*>(spheres_), static_cast<void*>(springs_)}) {
        if (array) {
            memory::Hub::dealloc(array);
        }
    }
    for (script::Struct* structure : {callback_params_ptr_, bounce_sfx_, collide_sound_ptr_}) {
        if (structure) {
            erase_one(structure);
        }
    }
}

// 0x0042D4B0
void BodyDynamicsModule::set_contacts(script::Array* points)
{
    contact_count_ = static_cast<uint16_t>(points->extent());
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    for (uint32_t i = 0; i < contact_count_; ++i) {
        core::at(contact_run(), i) = point_of(points->vector_at(i));
    }
}

// 0x0042D560
void BodyDynamicsModule::set_box(const maths::Vec4* top, const maths::Vec4* bottom)
{
    contact_count_ = kBoxContacts;
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    int written = 0;
    for (const bool right : {false, true}) {
        for (const bool upper : {false, true}) {
            const maths::Vec4& half = upper ? *top : *bottom;
            for (const bool front : {false, true}) {
                core::at(contact_run(), written++) = {.x = half.x * (right ? 1.0F : -1.0F),
                                                      .y = upper ? top->y : -bottom->y,
                                                      .z = half.z * (front ? 1.0F : -1.0F),
                                                      .w = 1.0F};
            }
        }
    }
}

// 0x0042D690
void BodyDynamicsModule::set_pyramid(PyramidSize pyramid_size)
{
    const auto half_height = pyramid_size.half_height_;
    const auto half_depth = pyramid_size.half_depth_;
    contact_count_ = kPlaneContacts;
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    const float base = half_height * kPyramidBase;
    core::at(contact_run(), 0) = {.x = half_depth, .y = base, .z = half_depth, .w = 1.0F};
    core::at(contact_run(), 1) = {.x = -half_depth, .y = base, .z = half_depth, .w = 1.0F};
    core::at(contact_run(), 2) = {.x = -half_depth, .y = base, .z = -half_depth, .w = 1.0F};
    core::at(contact_run(), 3) = {.x = half_depth, .y = base, .z = -half_depth, .w = 1.0F};
    core::at(contact_run(), 4) = {.x = 0.0F, .y = half_height * kPyramidTip, .z = 0.0F, .w = 1.0F};
}

// 0x0042D790 (the angle's cosine taken unrounded, its sine from the angle rounded to a float, as the original)
void BodyDynamicsModule::set_cylinder(CylinderShape cylinder)
{
    const auto top_radius = cylinder.top_radius_;
    const auto bottom_radius = cylinder.bottom_radius_;
    const auto half_height = cylinder.half_height_;
    const auto edges = cylinder.edges;
    contact_count_ = static_cast<uint16_t>(edges * 2);
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    int written = 0;
    for (const bool upper : {false, true}) {
        const float radius = upper ? top_radius : bottom_radius;
        for (int i = edges; i != 0;) {
            --i;
            const double angle = static_cast<double>(i) * kAlmostTwoPi / static_cast<double>(edges);
            auto rounded = static_cast<float>(angle);
            core::at(contact_run(),
                     written++) = {.x = static_cast<float>(std::cos(angle) * radius),
                                   .y = upper ? half_height : -half_height,
                                   .z = static_cast<float>(std::sin(static_cast<double>(rounded)) * radius),
                                   .w = 1.0F};
        }
    }
}

// 0x0042D8A0
void BodyDynamicsModule::set_triangle(const maths::Vec4* dimensions)
{
    contact_count_ = kCylinderContacts;
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    const maths::Vec4& dims = *dimensions;
    int written = 0;
    for (const bool back : {false, true}) {
        const float z = (back ? -1.0F : 1.0F) * dims.z;
        core::at(contact_run(), written++) = {.x = dims.x, .y = -dims.y, .z = z, .w = 1.0F};
        core::at(contact_run(), written++) = {.x = -dims.x, .y = -dims.y, .z = z, .w = 1.0F};
        core::at(contact_run(), written++) = {.x = 0.0F, .y = dims.y, .z = z, .w = 1.0F};
    }
}

// 0x0042ED60
void BodyDynamicsModule::set_spheres(script::Array* spheres)
{
    sphere_count_ = static_cast<uint16_t>(spheres->extent());
    spheres_ = reallocate(spheres_, sphere_count_ * sizeof(Sphere));
    for (uint32_t i = 0; i < sphere_count_; ++i) {
        script::Struct* settings = spheres->struct_at(i);
        settings->fetch_vec(0U, &core::at(sphere_run(), i).center, 1);
        settings->fetch_float(kRadius, &core::at(sphere_run(), i).radius, 1); // radius
    }
}

// 0x0042EB10
void BodyDynamicsModule::set_sounds(uint32_t type)
{
    script::Struct* sounds = nullptr;
    script::read_sub_struct(kRigidBodySounds)->read_sub_struct(type, &sounds, 0); // RigidBodySounds
    // (no such type: through null, as the original)
    script::Struct* sound = nullptr;
    if (sounds->has_entry_named(kCollideSound)) { // CollideSound
        sounds->read_sub_struct(kCollideSound, &sound, 1);
        if (collide_sound_ptr_) {
            erase_one(collide_sound_ptr_);
        }
        collide_sound_ptr_ = copy_of(sound);
    }
    if (!sounds->has_entry_named(kBounceSound)) { // BounceSound
        return;
    }
    sounds->read_sub_struct(kBounceSound, &sound, 1);
    if (bounce_sfx_) {
        erase_one(bounce_sfx_);
    }
    bounce_sfx_ = copy_of(sound);
    if (sounds->has_entry_named(kCollideMuteDelay)) { // CollideMuteDelay
        // (read into the argument slot - the original's then held the copy's allocation; it's there, so the read
        // replaces it)
        int delay = 0;
        sounds->fetch_int_value(kCollideMuteDelay, &delay, 0);
        impact_quiet_ms_ = delay;
    }
    if (sounds->has_entry_named(kGlobalCollideMuteDelay)) { // GlobalCollideMuteDelay
        int delay = 0;                         // (uninitialised in the original: only read when it's given)
        sounds->fetch_int_value(kGlobalCollideMuteDelay, &delay, 0);
        global_collide_mute_delay_ = delay;
    }
    sounds->fetch_float(kBounceVelocityCallbackThreshold, &bounce_callback_threshold_, 0); // BounceVelocityCallbackThreshold
    sounds->fetch_float(kBounceVelocityFullSpeed, &bounce_full_speed_, 0);         // BounceVelocityFullSpeed
}

// The shape's size: its farthest point, or sphere's edge, from the centre of mass.
float BodyDynamicsModule::shape_extent() const
{
    float extent = 0.0F;
    for (int i = contact_count_; i != 0;) {
        const maths::Vec4& point = core::at(contact_run(), --i);
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        auto length = static_cast<float>(std::sqrt(((z * z) + (x * x)) + (y * y)));
        if (!(extent > length)) {
            extent = length;
        }
    }
    for (int i = sphere_count_; i != 0;) {
        const Sphere& sphere = core::at(sphere_run(), --i);
        const double x = sphere.center.x;
        const double y = sphere.center.y;
        const double z = sphere.center.z;
        auto length = static_cast<float>(std::sqrt(((z * z) + (x * x)) + (y * y)) + sphere.radius);
        if (!(extent > length)) {
            extent = length;
        }
    }
    return extent;
}

// Angles: the shape's points and spheres turned.
void BodyDynamicsModule::turn_shape(script::Struct* params) const
{
    maths::Vec4 angles;
    level_nodes::read_turn_angles(params, &angles);
    maths::Mat4 turn;
    turn.init_from_angles(angles);
    auto element = [&turn](int row_index, int column) {
        return static_cast<double>(core::at(turn.row, row_index)[column]);
    };
    for (int i = contact_count_; i != 0;) {
        maths::Vec4& point = core::at(contact_run(), --i);
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        point.z = static_cast<float>(((element(0, 2) * x) + (element(1, 2) * y)) + (element(2, 2) * z));
        point.x = static_cast<float>(((element(0, 0) * x) + (element(1, 0) * y)) + (element(2, 0) * z));
        point.y = static_cast<float>(((element(0, 1) * x) + (element(1, 1) * y)) + (element(2, 1) * z));
    }
    for (int i = sphere_count_; i != 0;) {
        maths::Vec4& point = core::at(sphere_run(), --i).center;
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        point.z = static_cast<float>(((element(1, 2) * y) + (element(2, 2) * z)) + (element(0, 2) * x));
        point.x = static_cast<float>(((element(1, 0) * y) + (element(2, 0) * z)) + (element(0, 0) * x));
        point.y = static_cast<float>(((element(1, 1) * y) + (element(2, 1) * z)) + (element(0, 1) * x));
    }
}

// The shape: the contacts or spheres given, else Box (the default), Pyramid, Cylinder or Triangle.
void BodyDynamicsModule::configure_shape(script::Struct* params)
{
    // The shape: the contacts or spheres given, else Box (the default), Pyramid, Cylinder or Triangle.
    int shape = (contact_count_ != 0U) ? 4 : 0;
    if (params->has_entry_named(kContacts)) { // contacts
        script::Array* contacts = nullptr;
        params->fetch_list(kContacts, &contacts, 0);
        set_contacts(contacts);
        shape = 4;
    }
    if (params->has_entry_named(kSpheres)) { // (the spheres)
        script::Array* spheres = nullptr;
        params->fetch_list(kSpheres, &spheres, 0);
        set_spheres(spheres);
        shape = 4;
    }
    if (params->has_bare_flag(kBox)) { // Box
        shape = 0;
    } else if (params->has_bare_flag(kPyramid)) { // Pyramid
        shape = 1;
    } else if (params->has_bare_flag(kCylinder)) { // Cylinder
        shape = 2;
    } else if (params->has_bare_flag(kTriangle)) { // Triangle
        shape = 3;
    }
    switch (shape) {
    case 0: {
        maths::Vec4 top = {.x = kBoxWidth, .y = kBoxWidth, .z = kBoxDepth, .w = 1.0F};
        params->fetch_vec("dimensions", &top, 0);
        maths::Vec4 bottom = top;
        params->fetch_vec("top_dimensions", &top, 0);
        params->fetch_vec("bottom_dimensions", &bottom, 0);
        top = {.x = top.x * kHalf, .y = top.y * kHalf, .z = top.z * kHalf, .w = top.w * kHalf};
        bottom = {.x = bottom.x * kHalf, .y = bottom.y * kHalf, .z = bottom.z * kHalf, .w = bottom.w * kHalf};
        set_box(&top, &bottom);
        break;
    }
    case 1: {
        float height = kBoxHeight;
        float depth = kBoxDepth;
        params->fetch_float(kHeight, &height, 0); // height
        params->fetch_float(kDepth, &depth, 0);   // depth
        height = height * kHalf;
        depth = depth * kHalf;
        set_pyramid({.half_height_ = height, .half_depth_ = depth});
        break;
    }
    case 2: {
        float radius = kCylinderRadius;
        float height = kCylinderHeight;
        int edges = kCylinderEdges;
        params->fetch_float(kRadius, &radius, 0); // radius
        float bottom_radius = radius;
        params->fetch_float(kTopRadius, &radius, 0);           // top_radius
        params->fetch_float(kBottomRadius, &bottom_radius, 0); // bottom_radius
        params->fetch_float(kHeight, &height, 0);              // height
        params->fetch_int_value(kEdges, &edges, 0);            // edges
        height = height * kHalf;
        set_cylinder({.top_radius_ = radius, .bottom_radius_ = bottom_radius, .half_height_ = height, .edges = edges});
        break;
    }
    case 3: {
        maths::Vec4 dimensions = {.x = kBoxWidth, .y = kBoxWidth, .z = kBoxDepth, .w = 1.0F};
        params->fetch_vec(kDimensions, &dimensions, 0); // Dimensions
        dimensions = {
            .x = dimensions.x * kHalf, .y = dimensions.y * kHalf, .z = dimensions.z * kHalf, .w = dimensions.w * kHalf};
        set_triangle(&dimensions);
        break;
    }
    default:
        break;
    }
}

// directed_friction: one structure per contact (its direction, or none).
void BodyDynamicsModule::configure_directed_friction(script::Struct* params)
{
    if (params->has_entry_named(kDirectedFriction)) { // directed_friction: one structure per contact
        script::Array* friction = nullptr;
        params->fetch_list(kDirectedFriction, &friction, 0);
        steer_friction_ = reallocate(steer_friction_, contact_count_ * sizeof(DirectedFriction));
        const std::span<DirectedFriction> frictions(steer_friction_, contact_count_);
        for (int i = contact_count_; i != 0;) {
            --i;
            script::Struct* settings = friction->struct_at(static_cast<uint32_t>(i));
            DirectedFriction& directed = core::at(frictions, i);
            if (settings->has_entry_named(kNone)) { // none
                directed.active = false;
            } else {
                directed.active = true;
                settings->fetch_vec(0U, &directed.direction, 1);
            }
        }
    }
}

// 0x0042F050
void BodyDynamicsModule::configure_from_data(script::Struct* params)
{
    cur_phase_ = BodyDynamicsModule::State::kAsleep;
    uint32_t state = 0;
    if (params->read_hash_value(kRigidbodyState, &state, 0)) { // rigidbody_state
        if (state == kAsleep) {                                // asleep
            cur_phase_ = BodyDynamicsModule::State::kAsleep;
        } else if (state == kDisabled) { // disabled
            cur_phase_ = BodyDynamicsModule::State::kDisabled;
        } else if (state == kAwake) { // awake
            cur_phase_ = BodyDynamicsModule::State::kAwake;
        }
    }
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    rot_vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    for (int row_index = 0; row_index < 4; ++row_index) {
        core::at(basis_frame_.row, row_index) = {.x = row_index == 0 ? 1.0F : 0.0F,
                                                 .y = row_index == 1 ? 1.0F : 0.0F,
                                                 .z = row_index == 2 ? 1.0F : 0.0F,
                                                 .w = row_index == 3 ? 1.0F : 0.0F};
    }
    at_point_ = entity()->at_point();
    maths::QuatRotation orientation{};
    orientation.from_matrix(entity()->basis_frame());
    orientation_ = orientation;
    orientation_.make_unit(1.0F);
    quat_to_matrix(&orientation_, &basis_frame_);

    auto vector = [params](uint32_t name, maths::Vec4* out) {
        return params->fetch_vec(name, out, 0);
    };
    vector(kVel, &vel_);                                 // vel
    vector(kRotVel, &rot_vel_);                             // rotvel
    vector(kCenterOfMass, &balance_point_);                       // center_of_mass
    if (!params->fetch_float(kConstAcc, &steady_accel_.y, 0)) {   // const_acc: a number is the y
        vector(kConstAcc, &steady_accel_);
    }
    params->fetch_float(kCoeffRestitution, &restitution_, 0);  // coeff_restitution
    params->fetch_float(kCoeffFriction, &drag_factor_, 0);  // coeff_friction
    params->fetch_float(kSpringConst, &spring_const_, 0); // spring_const
    params->fetch_float(kLinearDamping, &unknown38_, 0);
    params->fetch_float(kAngularDamping, &unknown3c_, 0);
    if (params->has_entry_named(kLinearVelocitySleepPoint)) { // linear_velocity_sleep_point
        params->fetch_float(kLinearVelocitySleepPoint, &linear_sleep_squared_, 0);
        linear_sleep_squared_ = linear_sleep_squared_ * linear_sleep_squared_;
    }
    if (params->has_entry_named(kAngularVelocitySleepPoint)) { // angular_velocity_sleep_point
        params->fetch_float(kAngularVelocitySleepPoint, &angular_sleep_squared_, 0);
        angular_sleep_squared_ = angular_sleep_squared_ * angular_sleep_squared_;
    }
    params->fetch_float(kSkaterCollisionImpulseFactor, &skater_impulse_factor_, 0);     // skater_collision_impulse_factor
    params->fetch_float(kSkaterCollisionRotationFactor, &skater_rotation_factor_, 0);    // skater_collision_rotation_factor
    params->fetch_float(kSkaterCollisionRadius, &skater_radius_, 0);             // skater_collision_radius
    params->fetch_float(kSkaterCollisionApplicationRadius, &skater_application_radius_, 0); // skater_collision_application_radius
    if (params->has_entry_named(kSkaterCollisionAssent)) {                       // skater_collision_assent (degrees)
        float degrees = 0.0F;
        params->fetch_float(kSkaterCollisionAssent, &degrees, 0);
        const double radians = static_cast<double>(degrees) * kRadiansPerDegree;
        skater_assent_cos_ = static_cast<float>(std::cos(radians));
        skater_assent_sin_ = static_cast<float>(std::sin(radians));
    }
    params->fetch_float(kPusherQuietTime, &push_cooldown_time_, 0);
    params->read_hash_value(kCollideScript, &collide_script_, 0); // CollideScript
    params->read_hash_value(kBounceScript, &bounce_script_, 0);  // BounceScript
    params->read_hash_value(kSettleScript, &settle_script_, 0);  // SettleScript
    params->read_hash_value(kStuckScript, &script_d0_, 0);
    if (params->has_entry_named(kCallbackParams)) { // CallbackParams (the old copy is kept - leaked)
        script::Struct* callback = nullptr;
        params->read_sub_struct(kCallbackParams, &callback, 1);
        callback_params_ptr_ = copy_of(callback);
    }
    if (params->has_entry_named(kSoundType)) { // SoundType
        uint32_t type = 0;
        params->read_hash_value(kSoundType, &type, 1);
        set_sounds(type);
    }
    if (params->has_bare_flag(kDieUponSettling)) { // DieUponSettling
        flag_bits_ |= kFlagDieUponSettling;
    } else {
        flag_bits_ &= ~kFlagDieUponSettling;
    }

    if (params->has_entry_named(kConstraints)) { // springs
        script::Array* springs = nullptr;
        params->fetch_list(kConstraints, &springs, 1);
        spring_count_ = static_cast<uint16_t>(springs->extent());
        springs_ = reallocate(springs_, spring_count_ * sizeof(Spring));
        // (each built by the vector constructor iterator 0x00401020 with 0x005902E0 - which does nothing)
        for (int i = 0; std::cmp_less(i, spring_count_); ++i) {
            core::at(spring_run(), i).point = point_of(springs->vector_at(static_cast<uint32_t>(i)));
            core::at(spring_run(), i).active = true;
        }
    }

    configure_shape(params);

    configure_directed_friction(params);

    if (params->has_entry_named(kAngles)) { // Angles: the shape turned
        turn_shape(params);
    }

    // The centre of mass: given, else the average of the points.
    maths::Vec4 center = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (params->has_entry_named(kCenterOfMass) && params->has_entry_named(kCenterOfMass)) {
        vector(kCenterOfMass, &center);
    }
    if (center.x == 0.0F && center.y == 0.0F && center.z == 0.0F) {
        for (int i = contact_count_; i != 0;) {
            const maths::Vec4& point = core::at(contact_run(), --i);
            center = {
                .x = center.x + point.x, .y = center.y + point.y, .z = center.z + point.z, .w = center.w + point.w};
        }
        for (int i = sphere_count_; i != 0;) {
            const maths::Vec4& point = core::at(sphere_run(), --i).center;
            center = {
                .x = center.x + point.x, .y = center.y + point.y, .z = center.z + point.z, .w = center.w + point.w};
        }
        const double share = 1.0 / static_cast<double>(contact_count_ + sphere_count_);
        center = {.x = static_cast<float>(center.x * share), .y = static_cast<float>(center.y * share),
                  .z = static_cast<float>(center.z * share), .w = static_cast<float>(center.w * share)};
    }
    maths::Vec4 model_offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    vector(kModelOffset, &model_offset); // model_offset
    model_offset_ = {.x = model_offset.x - center.x, .y = model_offset.y - center.y, .z = model_offset.z - center.z,
                     .w = model_offset.w - center.w};
    // The shape about the centre of mass (w too).
    auto recentre = [&center](maths::Vec4& point) {
        point = {.x = point.x - center.x, .y = point.y - center.y, .z = point.z - center.z, .w = point.w - center.w};
    };
    for (int i = contact_count_; i != 0;) {
        recentre(core::at(contact_run(), --i));
    }
    for (int i = sphere_count_; i != 0;) {
        recentre(core::at(sphere_run(), --i).center);
    }
    for (int i = spring_count_; i != 0;) {
        recentre(core::at(spring_run(), --i).point);
    }

    // mass_over_moment, else the default scaled down by the size of a big shape.
    if (!params->fetch_float(kMassOverMoment, &mass_over_moment_, 0)) {
        const float extent = shape_extent();
        if (extent > kBigShape) {
            const double scale = static_cast<double>(extent) * kShapeScale;
            mass_over_moment_ = static_cast<float>(mass_over_moment_ / (scale * scale));
        }
    }
    if (params->has_bare_flag(kDisablePlayerCollision)) { // disable_player_collision
        flag_bits_ |= kFlagNoPlayerCollision;
    }
    objects_shared().set_rigid_body_pusher_height(kPusherHeight);

    // The bounding radius (squared lengths compared unrounded).
    radius_ = 0.0F;
    for (int i = contact_count_; i != 0;) {
        const maths::Vec4& point = core::at(contact_run(), --i);
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        const double length_squared = ((z * z) + (x * x)) + (y * y);
        if (length_squared > radius_) {
            radius_ = static_cast<float>(length_squared);
        }
    }
    for (int i = sphere_count_; i != 0;) {
        const Sphere& sphere = core::at(sphere_run(), --i);
        const double x = sphere.center.x;
        const double y = sphere.center.y;
        const double z = sphere.center.z;
        const double reach = std::sqrt(((z * z) + (x * x)) + (y * y)) + sphere.radius;
        if (reach * reach > radius_) {
            radius_ = static_cast<float>(reach * reach);
        }
    }
    push_cooldown_ = 0.0F;
    radius_ = static_cast<float>(std::sqrt(static_cast<double>(radius_)) + 1.0F);

    // The position is the centre of mass: moved by the (turned) model offset.
    auto entry = [this](int row, int column) { return static_cast<double>(core::at(basis_frame_.row, row)[column]); };
    const double ox = model_offset_.x;
    const double oy = model_offset_.y;
    const double oz = model_offset_.z;
    const double tx = ((entry(2, 0) * oz) + (entry(1, 0) * oy)) + (ox * entry(0, 0));
    const double ty = ((entry(2, 1) * oz) + (entry(1, 1) * oy)) + (entry(0, 1) * ox);
    auto tz = static_cast<float>(((entry(2, 2) * oz) + (entry(1, 2) * oy)) + (entry(0, 2) * ox));
    const float tw = model_offset_.w;
    at_point_.x = static_cast<float>(at_point_.x - tx);
    at_point_.y = static_cast<float>(at_point_.y - ty);
    at_point_.z = at_point_.z - tz;
    at_point_.w = at_point_.w - tw;
}

// Awake (the inlined end of the member functions): unless it is already, where the object is now kept.
void BodyDynamicsModule::wake()
{
    if (cur_phase_ == BodyDynamicsModule::State::kAwake) {
        return;
    }
    wake_pos_ = entity()->at_point();
    cur_phase_ = BodyDynamicsModule::State::kAwake;
}

// 0x0042DCA0
bool BodyDynamicsModule::rigid_body_wake(script::Struct* /*params*/, script::Script* /*script*/)
{
    wake();
    return true;
}

// 0x0042DCF0
bool BodyDynamicsModule::rigid_body_sleep(script::Struct* /*params*/, script::Script* /*script*/)
{
    sleep();
    return true;
}

// 0x0042DD00
bool BodyDynamicsModule::rigidbody_kick(script::Struct* params, script::Script* /*script*/)
{
    script::VectorValue v{};
    if (params->fetch_vec(kVel, &v, 0)) { // vel
        vel_ = {.x = v.x + vel_.x, .y = v.y + vel_.y, .z = v.z + vel_.z, .w = v.w + vel_.w};
        entity()->vel() = vel_;
    }
    if (params->fetch_vec(kRotVel, &v, 0)) { // rotvel
        rot_vel_ = {.x = v.x + rot_vel_.x, .y = v.y + rot_vel_.y, .z = v.z + rot_vel_.z, .w = v.w + rot_vel_.w};
    }
    if (params->fetch_vec(kPos, &v, 0)) { // pos
        at_point_ = {.x = v.x + at_point_.x, .y = v.y + at_point_.y, .z = v.z + at_point_.z, .w = v.w + at_point_.w};
        entity()->at_point() = at_point_;
    }
    wake();
    return true;
}

// 0x0042DEB0
bool BodyDynamicsModule::rigid_body_set_velocities(script::Struct* params, script::Script* /*script*/)
{
    script::VectorValue v{};
    if (params->fetch_vec(kVel, &v, 0)) { // vel
        vel_ = {.x = v.x, .y = v.y, .z = v.z, .w = v.w};
        entity()->vel() = vel_;
    }
    if (params->fetch_vec(kRotVel, &v, 0)) { // rotvel
        rot_vel_ = {.x = v.x, .y = v.y, .z = v.z, .w = v.w};
    }
    wake();
    return true;
}

// 0x0042EE30
bool BodyDynamicsModule::rigid_body_enable(script::Struct* /*params*/, script::Script* /*script*/)
{
    cur_phase_ = BodyDynamicsModule::State::kAwake;
    at_point_ = entity()->at_point();
    maths::QuatRotation orientation{};
    orientation.from_matrix(entity()->basis_frame());
    orientation_ = orientation;
    orientation_.make_unit(1.0F);
    quat_to_matrix(&orientation_, &basis_frame_);
    vel_ = entity()->vel();
    return true;
}

// 0x0042DFB0
bool BodyDynamicsModule::rigid_body_disable_player_collision(script::Struct* /*params*/, script::Script* /*script*/)
{
    flag_bits_ |= kFlagNoPlayerCollision;
    return true;
}

// 0x0042DFC0
bool BodyDynamicsModule::rigid_body_enable_player_collision(script::Struct* /*params*/, script::Script* /*script*/)
{
    flag_bits_ &= ~kFlagNoPlayerCollision;
    return true;
}

// 0x0042DFD0
bool BodyDynamicsModule::get_center_of_mass_pos(script::Struct* /*params*/, script::Script* script)
{
    script->args()->put_vec3(checksums::Name{kCenterOfMassPos}, {.x = at_point_.x, .y = at_point_.y, .z = at_point_.z, .w = at_point_.w}); // CenterOfMassPos
    return true;
}

// 0x0042E020
bool BodyDynamicsModule::rigidbody_match_velocity_to(script::Struct* params, script::Script* /*script*/)
{
    const uint32_t id = params->step_to_entry(nullptr)->value.word();
    auto* other = core::down_cast<GameEntity>(objects::EntityHub::look_up_entity(id));
    const bool adjust = params->has_bare_flag(0x8ACE8A0F); // ApplyRandomAdjustment
    vel_ = other->vel();
    if (adjust) {
        const double factor = maths::plus_or_minus_exact(0.4000000059604645F) + 1.0F;
        vel_ = {.x = static_cast<float>(factor * vel_.x), .y = static_cast<float>(factor * vel_.y),
                .z = static_cast<float>(factor * vel_.z), .w = static_cast<float>(factor * vel_.w)};
    }
    entity()->vel() = vel_;
    if (cur_phase_ == BodyDynamicsModule::State::kAsleep && std::fabs(vel_.y) < 1.0F) {
        vel_.y = 1.0F;
    }
    if (auto* body = core::down_cast<BodyDynamicsModule>(other->fetch_module(kType))) {
        rot_vel_ = body->rot_vel_;
        if (adjust) {
            const double factor = maths::plus_or_minus_exact(0.4000000059604645F);
            rot_vel_ = {.x = static_cast<float>(factor * rot_vel_.x), .y = static_cast<float>(factor * rot_vel_.y),
                        .z = static_cast<float>(factor * rot_vel_.z), .w = static_cast<float>(factor * rot_vel_.w)};
        }
    }
    wake();
    return true;
}

// 0x0042E380
void add_rigid_body_pusher(GameEntity* object)
{
    auto& pushers = objects_shared().rigid_body_pushers();
    for (int i = kLastContact; i >= 0; --i) {
        if (core::at(pushers, i).get_pull()) {
            continue;
        }
        core::at(pushers, i).assign_value(object);
        core::at(objects_shared().rigid_body_pusher_reach(), i) =
            0.0F; // (its extra reach: set_rigid_body_pusher_reach)
        return;
    }
}

} // namespace objects

namespace objects {

// 0x00640900 (a static initialiser): the rigid bodies' collision cache (registered among the caches, empty).
// (never destroyed: it stays registered to the end)
void construct_rigid_body_cache() { objects_shared().rigid_body_cache().emplace(); }

// NOT ORIGINAL
void BodyDynamicsModule::check_layout()
{
    // Each field at the original's offset (32-bit builds; the size counts on the heap it is made on).
    struct FieldAt {
        std::size_t now;
        std::size_t original;
    };
    constexpr std::array kLayout{
        FieldAt{.now = offsetof(BodyDynamicsModule, mass_over_moment_), .original = 0x18},
        FieldAt{.now = offsetof(BodyDynamicsModule, steady_accel_), .original = 0x1C},
        FieldAt{.now = offsetof(BodyDynamicsModule, restitution_), .original = 0x2C},
        FieldAt{.now = offsetof(BodyDynamicsModule, drag_factor_), .original = 0x30},
        FieldAt{.now = offsetof(BodyDynamicsModule, spring_const_), .original = 0x34},
        FieldAt{.now = offsetof(BodyDynamicsModule, unknown38_), .original = 0x38},
        FieldAt{.now = offsetof(BodyDynamicsModule, unknown3c_), .original = 0x3C},
        FieldAt{.now = offsetof(BodyDynamicsModule, skater_radius_), .original = 0x40},
        FieldAt{.now = offsetof(BodyDynamicsModule, skater_application_radius_), .original = 0x44},
        FieldAt{.now = offsetof(BodyDynamicsModule, skater_assent_cos_), .original = 0x48},
        FieldAt{.now = offsetof(BodyDynamicsModule, skater_assent_sin_), .original = 0x4C},
        FieldAt{.now = offsetof(BodyDynamicsModule, skater_impulse_factor_), .original = 0x50},
        FieldAt{.now = offsetof(BodyDynamicsModule, skater_rotation_factor_), .original = 0x54},
        FieldAt{.now = offsetof(BodyDynamicsModule, linear_sleep_squared_), .original = 0x58},
        FieldAt{.now = offsetof(BodyDynamicsModule, angular_sleep_squared_), .original = 0x5C},
        FieldAt{.now = offsetof(BodyDynamicsModule, contacts_), .original = 0x60},
        FieldAt{.now = offsetof(BodyDynamicsModule, capsule_), .original = 0x64},
        FieldAt{.now = offsetof(BodyDynamicsModule, steer_friction_), .original = 0x88},
        FieldAt{.now = offsetof(BodyDynamicsModule, contact_count_), .original = 0x8C},
        FieldAt{.now = offsetof(BodyDynamicsModule, pad8e_), .original = 0x8E},
        FieldAt{.now = offsetof(BodyDynamicsModule, spheres_), .original = 0x90},
        FieldAt{.now = offsetof(BodyDynamicsModule, sphere_count_), .original = 0x94},
        FieldAt{.now = offsetof(BodyDynamicsModule, springs_), .original = 0x98},
        FieldAt{.now = offsetof(BodyDynamicsModule, spring_count_), .original = 0x9C},
        FieldAt{.now = offsetof(BodyDynamicsModule, balance_point_), .original = 0xA0},
        FieldAt{.now = offsetof(BodyDynamicsModule, model_offset_), .original = 0xB0},
        FieldAt{.now = offsetof(BodyDynamicsModule, push_cooldown_time_), .original = 0xC0},
        FieldAt{.now = offsetof(BodyDynamicsModule, collide_script_), .original = 0xC4},
        FieldAt{.now = offsetof(BodyDynamicsModule, bounce_script_), .original = 0xC8},
        FieldAt{.now = offsetof(BodyDynamicsModule, settle_script_), .original = 0xCC},
        FieldAt{.now = offsetof(BodyDynamicsModule, script_d0_), .original = 0xD0},
        FieldAt{.now = offsetof(BodyDynamicsModule, callback_params_ptr_), .original = 0xD4},
        FieldAt{.now = offsetof(BodyDynamicsModule, collide_sound_ptr_), .original = 0xD8},
        FieldAt{.now = offsetof(BodyDynamicsModule, bounce_sfx_), .original = 0xDC},
        FieldAt{.now = offsetof(BodyDynamicsModule, impact_quiet_ms_), .original = 0xE0},
        FieldAt{.now = offsetof(BodyDynamicsModule, global_collide_mute_delay_), .original = 0xE4},
        FieldAt{.now = offsetof(BodyDynamicsModule, bounce_callback_threshold_), .original = 0xE8},
        FieldAt{.now = offsetof(BodyDynamicsModule, bounce_full_speed_), .original = 0xEC},
        FieldAt{.now = offsetof(BodyDynamicsModule, unknownf0_), .original = 0xF0},
        FieldAt{.now = offsetof(BodyDynamicsModule, flag_bits_), .original = 0xF4},
        FieldAt{.now = offsetof(BodyDynamicsModule, wake_pos_), .original = 0xF8},
        FieldAt{.now = offsetof(BodyDynamicsModule, die_timer_), .original = 0x108},
        FieldAt{.now = offsetof(BodyDynamicsModule, cur_phase_), .original = 0x10C},
        FieldAt{.now = offsetof(BodyDynamicsModule, unknown10d_), .original = 0x10D},
        FieldAt{.now = offsetof(BodyDynamicsModule, at_point_), .original = 0x110},
        FieldAt{.now = offsetof(BodyDynamicsModule, vel_), .original = 0x120},
        FieldAt{.now = offsetof(BodyDynamicsModule, orientation_), .original = 0x130},
        FieldAt{.now = offsetof(BodyDynamicsModule, rot_vel_), .original = 0x140},
        FieldAt{.now = offsetof(BodyDynamicsModule, basis_frame_), .original = 0x150},
        FieldAt{.now = offsetof(BodyDynamicsModule, contacts_hit_), .original = 0x190},
        FieldAt{.now = offsetof(BodyDynamicsModule, unknown191_), .original = 0x191},
        FieldAt{.now = offsetof(BodyDynamicsModule, push_cooldown_), .original = 0x194},
        FieldAt{.now = offsetof(BodyDynamicsModule, radius_), .original = 0x198},
        FieldAt{.now = offsetof(BodyDynamicsModule, audio_module_), .original = 0x19C},
        FieldAt{.now = sizeof(BodyDynamicsModule), .original = 0x1A0},
    };
    static_assert(sizeof(void*) != 4 || std::ranges::all_of(kLayout, [](const FieldAt& field) {
                      return field.now == field.original;
                  }),
                  "layout must match the original");
}

} // namespace objects
