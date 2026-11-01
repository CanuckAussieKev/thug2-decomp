// The rigid body component: its life, shapes, set-up and script member functions (engine/objects/rigidbody_component.h).

#include "engine/objects/rigidbody_component.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/random.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/render/coll_cache.h"
#include "engine/sys/startup_init.h"

namespace objects {

float g_rigid_body_pusher_reach[8];
RigidBodyCacheStorage g_rigid_body_cache;
BodyDynamicsModule::Contact g_rigid_body_contacts[24];
maths::Vec4 g_rigid_body_world_contact_points[24];
maths::Vec4 g_rigid_body_world_sphere_centers[24];
maths::Vec4 g_rigid_body_world_spring_points[2];
SharedRef<GameEntity> g_rigid_body_pushers[8];

maths::Vec4* BodyDynamicsModule::world_contact_points() { return g_rigid_body_world_contact_points; }
maths::Vec4* BodyDynamicsModule::world_sphere_centers() { return g_rigid_body_world_sphere_centers; }
maths::Vec4* BodyDynamicsModule::world_spring_points() { return g_rigid_body_world_spring_points; }
BodyDynamicsModule::Contact* BodyDynamicsModule::get_contacts() { return g_rigid_body_contacts; }
float g_rigid_body_pusher_height;

namespace {

constexpr float kHalf = 0.5f;                             // 0x006453BC
constexpr float kRadiansPerDegree = 0.01745329238474369f; // 0x00645464
constexpr double kAssent = 0.4363323251406352;            // 0x006463F0 (25 degrees)
constexpr float kPyramidBase = -0.6000000238418579f;      // 0x006463E8
constexpr float kPyramidTip = 1.600000023841858f;         // 0x006463E4
constexpr float kAlmostTwoPi = 6.2829999923706055f;       // 0x006463EC
constexpr float kBigShape = 30.0f;                        // 0x00646240
constexpr float kShapeScale = 0.03333333507180214f;       // 0x00646430

// The x87's own FCOS / FSIN (the original's; the C runtime's round differently), the result stored as a float.
float x87_cos(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
float x87_sin(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
// cos(angle) * value / sin(angle) * value, the cosine or sine kept at the x87's precision.
float x87_cos_times(double angle, float value)
{
    float result;
    __asm {
        fld angle
        fcos
        fmul value
        fstp result
    }
    return result;
}
float x87_sin_times(double angle, float value)
{
    float result;
    __asm {
        fld angle
        fsin
        fmul value
        fstp result
    }
    return result;
}

// The quaternion's matrix.
void quat_to_matrix(const maths::QuatRotation* quat, maths::Mat4* matrix) { quat->fetch_mat(matrix); }

// A structure copied into a new one (from the script pools), and one deleted.
script::Struct* copy_of(script::Struct* source)
{
    script::Struct* copy = script::spawn_param_bag();
    if (copy)
        copy->prepare_all();
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
    if (old)
        memory::Hub::get_pull()->dealloc(old);
    return static_cast<T*>(memory::alloc(size));
}

// A script vector's coordinates (x at +4).
const float* vector_of(uint32_t element) { return reinterpret_cast<const float*>(element) + 1; }

} // namespace

// 0x0042EF00
memory::owner<ObjectModule*> __cdecl BodyDynamicsModule::fabricate() { return new BodyDynamicsModule; }

// 0x0042E3F0
BodyDynamicsModule::BodyDynamicsModule()
{
    // (capsule_ is constructed first, as a member)
    flag_bits_ = 0;
    orientation_ = {0.0f, 0.0f, 0.0f, 1.0f};
    kind_tag_ = kType;
    steady_accel_ = {0.0f, -386.3999938964844f, 0.0f, 1.0f};
    skater_impulse_factor_ = 1.0f;
    skater_rotation_factor_ = 1.0f;
    mass_over_moment_ = 0.003000000026077032f;
    restitution_ = 0.4000000059604645f;
    drag_factor_ = 0.5f;
    spring_const_ = 10.0f;
    linear_sleep_squared_ = 100.0f;
    angular_sleep_squared_ = 0.1600000113248825f; // (0x3E23D70B)
    skater_radius_ = 48.0f;
    skater_application_radius_ = 12.0f;
    push_cooldown_time_ = 0.0833333358168602f;
    skater_assent_cos_ = x87_cos(kAssent);
    skater_assent_sin_ = x87_sin(kAssent);
    model_offset_ = {0.0f, 0.0f, 0.0f, 1.0f};
    script_d0_ = 0;
    settle_script_ = 0;
    bounce_script_ = 0;
    collide_script_ = 0;
    impact_quiet_ms_ = 1000;
    global_collide_mute_delay_ = 100;
    bounce_callback_threshold_ = 20.0f;
    bounce_full_speed_ = 300.0f;
    die_timer_ = -1.0f;
}

// 0x0042EF60 (0x00430DF0 the deleting one)
BodyDynamicsModule::~BodyDynamicsModule()
{
    for (void* array : {static_cast<void*>(contacts_), static_cast<void*>(steer_friction_),
                        static_cast<void*>(spheres_), static_cast<void*>(springs_)}) {
        if (array)
            memory::Hub::get_pull()->dealloc(array);
    }
    for (script::Struct* structure : {callback_params_ptr_, bounce_sfx_, collide_sound_ptr_}) {
        if (structure)
            erase_one(structure);
    }
}

// 0x0042D4B0
void BodyDynamicsModule::set_contacts(script::Array* points)
{
    contact_count_ = static_cast<uint16_t>(points->extent_);
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    const uint32_t* elements = points->find_array_value();
    for (uint32_t i = 0; i < contact_count_; ++i) {
        const float* v = vector_of(elements[i]);
        contacts_[i] = {v[0], v[1], v[2], 0.0f};
    }
}

// 0x0042D560
void BodyDynamicsModule::set_box(const maths::Vec4* top, const maths::Vec4* bottom)
{
    contact_count_ = 8;
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    int n = 0;
    for (bool right : {false, true}) {
        for (bool upper : {false, true}) {
            const maths::Vec4& half = upper ? *top : *bottom;
            for (bool front : {false, true}) {
                contacts_[n++] = {half.x * (right ? 1.0f : -1.0f), upper ? top->y : -bottom->y,
                                  half.z * (front ? 1.0f : -1.0f), 1.0f};
            }
        }
    }
}

// 0x0042D690
void BodyDynamicsModule::set_pyramid(float half_height, float half_depth)
{
    contact_count_ = 5;
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    const float base = half_height * kPyramidBase;
    contacts_[0] = {half_depth, base, half_depth, 1.0f};
    contacts_[1] = {-half_depth, base, half_depth, 1.0f};
    contacts_[2] = {-half_depth, base, -half_depth, 1.0f};
    contacts_[3] = {half_depth, base, -half_depth, 1.0f};
    contacts_[4] = {0.0f, half_height * kPyramidTip, 0.0f, 1.0f};
}

// 0x0042D790 (the angle's cosine taken unrounded, its sine from the angle rounded to a float, as the original)
void BodyDynamicsModule::set_cylinder(float top_radius, float bottom_radius, float half_height, int edges)
{
    contact_count_ = static_cast<uint16_t>(edges * 2);
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    int n = 0;
    for (bool upper : {false, true}) {
        const float radius = upper ? top_radius : bottom_radius;
        for (int i = edges; i != 0;) {
            --i;
            double angle = static_cast<double>(i) * kAlmostTwoPi / static_cast<double>(edges);
            float rounded = static_cast<float>(angle);
            contacts_[n++] = {x87_cos_times(angle, radius), upper ? half_height : -half_height,
                              x87_sin_times(rounded, radius), 1.0f};
        }
    }
}

// 0x0042D8A0
void BodyDynamicsModule::set_triangle(const maths::Vec4* dimensions)
{
    contact_count_ = 6;
    contacts_ = reallocate(contacts_, contact_count_ * sizeof(maths::Vec4));
    const maths::Vec4& d = *dimensions;
    int n = 0;
    for (bool back : {false, true}) {
        float z = (back ? -1.0f : 1.0f) * d.z;
        contacts_[n++] = {d.x, -d.y, z, 1.0f};
        contacts_[n++] = {-d.x, -d.y, z, 1.0f};
        contacts_[n++] = {0.0f, d.y, z, 1.0f};
    }
}

// 0x0042ED60
void BodyDynamicsModule::set_spheres(script::Array* spheres)
{
    sphere_count_ = static_cast<uint16_t>(spheres->extent_);
    spheres_ = reallocate(spheres_, sphere_count_ * sizeof(Sphere));
    const uint32_t* elements = spheres->find_array_value();
    for (uint32_t i = 0; i < sphere_count_; ++i) {
        auto* settings = reinterpret_cast<script::Struct*>(elements[i]);
        settings->fetch_vec(0u, reinterpret_cast<script::VectorValue*>(&spheres_[i].center), 1);
        settings->fetch_float(0xC48391A5, &spheres_[i].radius, 1); // radius
    }
}

// 0x0042EB10
void BodyDynamicsModule::set_sounds(uint32_t type)
{
    script::Struct* sounds = nullptr;
    script::read_sub_struct(0xB9738EBA)->read_sub_struct(type, &sounds, 0); // RigidBodySounds
    // (no such type: through null, as the original)
    script::Struct* sound;
    if (sounds->has_entry_named(0xBF8E0ACE)) { // CollideSound
        sounds->read_sub_struct(0xBF8E0ACE, &sound, 1);
        if (collide_sound_ptr_)
            erase_one(collide_sound_ptr_);
        collide_sound_ptr_ = copy_of(sound);
    }
    if (!sounds->has_entry_named(0x1FB2F60C)) // BounceSound
        return;
    sounds->read_sub_struct(0x1FB2F60C, &sound, 1);
    if (bounce_sfx_)
        erase_one(bounce_sfx_);
    bounce_sfx_ = copy_of(sound);
    if (sounds->has_entry_named(0x3818EE47)) { // CollideMuteDelay
        // (read into the argument slot, which then holds the copy's allocation)
        int delay = static_cast<int>(reinterpret_cast<uint32_t>(bounce_sfx_));
        sounds->fetch_int_value(0x3818EE47, &delay, 0);
        impact_quiet_ms_ = delay;
    }
    if (sounds->has_entry_named(0xAD67A34D)) { // GlobalCollideMuteDelay
        int delay = 0;                         // (uninitialised in the original: only read when it's given)
        sounds->fetch_int_value(0xAD67A34D, &delay, 0);
        global_collide_mute_delay_ = delay;
    }
    sounds->fetch_float(0x4511CA8D, &bounce_callback_threshold_, 0); // BounceVelocityCallbackThreshold
    sounds->fetch_float(0x8FC6519F, &bounce_full_speed_, 0);         // BounceVelocityFullSpeed
}

// 0x0042F050
void BodyDynamicsModule::configure_from_data(script::Struct* params)
{
    cur_phase_ = kStateAsleep;
    uint32_t state;
    if (params->read_hash_value(0x61FBB62C, &state, 0)) { // rigidbody_state
        if (state == 0x4484B712)                          // asleep
            cur_phase_ = kStateAsleep;
        else if (state == 0x7B01911C) // disabled
            cur_phase_ = kStateDisabled;
        else if (state == 0x99A34896) // awake
            cur_phase_ = kStateAwake;
    }
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    rot_vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int r = 0; r < 4; ++r)
        basis_frame_.row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
    at_point_ = entity_->at_point_;
    maths::QuatRotation orientation;
    orientation.from_matrix(entity_->basis_frame_);
    orientation_ = orientation;
    orientation_.make_unit(1.0f);
    quat_to_matrix(&orientation_, &basis_frame_);

    auto vector = [params](uint32_t name, maths::Vec4* out) {
        return params->fetch_vec(name, reinterpret_cast<script::VectorValue*>(out), 0);
    };
    vector(0x0C4C809E, &vel_);                                 // vel
    vector(0xFB1A83B2, &rot_vel_);                             // rotvel
    vector(0x737612C6, &balance_point_);                       // center_of_mass
    if (!params->fetch_float(0x6FAE5C9A, &steady_accel_.y, 0)) // const_acc: a number is the y
        vector(0x6FAE5C9A, &steady_accel_);
    params->fetch_float(0x60B4860D, &restitution_, 0);  // coeff_restitution
    params->fetch_float(0xF9D75930, &drag_factor_, 0);  // coeff_friction
    params->fetch_float(0xF1ECB30F, &spring_const_, 0); // spring_const
    params->fetch_float(0xDE76C3A7, &unknown38_, 0);
    params->fetch_float(0x9DA897CF, &unknown3c_, 0);
    if (params->has_entry_named(0xC11E4DD8)) { // linear_velocity_sleep_point
        params->fetch_float(0xC11E4DD8, &linear_sleep_squared_, 0);
        linear_sleep_squared_ = linear_sleep_squared_ * linear_sleep_squared_;
    }
    if (params->has_entry_named(0xA457ECE3)) { // angular_velocity_sleep_point
        params->fetch_float(0xA457ECE3, &angular_sleep_squared_, 0);
        angular_sleep_squared_ = angular_sleep_squared_ * angular_sleep_squared_;
    }
    params->fetch_float(0x3390D1E5, &skater_impulse_factor_, 0);     // skater_collision_impulse_factor
    params->fetch_float(0x5A18F61F, &skater_rotation_factor_, 0);    // skater_collision_rotation_factor
    params->fetch_float(0x85F112C1, &skater_radius_, 0);             // skater_collision_radius
    params->fetch_float(0x8AB2DB68, &skater_application_radius_, 0); // skater_collision_application_radius
    if (params->has_entry_named(0xE9156E6D)) {                       // skater_collision_assent (degrees)
        float degrees;
        params->fetch_float(0xE9156E6D, &degrees, 0);
        double radians = static_cast<double>(degrees) * kRadiansPerDegree;
        skater_assent_cos_ = x87_cos(radians);
        skater_assent_sin_ = x87_sin(radians);
    }
    params->fetch_float(0x1F4D0CA5, &push_cooldown_time_, 0);
    params->read_hash_value(0x58CDC0F0, &collide_script_, 0); // CollideScript
    params->read_hash_value(0x2D075F90, &bounce_script_, 0);  // BounceScript
    params->read_hash_value(0x571917FA, &settle_script_, 0);  // SettleScript
    params->read_hash_value(0xCCB84047, &script_d0_, 0);
    if (params->has_entry_named(0x0DD85BDA)) { // CallbackParams (the old copy is kept - leaked)
        script::Struct* callback;
        params->read_sub_struct(0x0DD85BDA, &callback, 1);
        callback_params_ptr_ = copy_of(callback);
    }
    if (params->has_entry_named(0x94956E48)) { // SoundType
        uint32_t type;
        params->read_hash_value(0x94956E48, &type, 1);
        set_sounds(type);
    }
    if (params->has_bare_flag(0x5E51924F)) // DieUponSettling
        flag_bits_ |= kFlagDieUponSettling;
    else
        flag_bits_ &= ~kFlagDieUponSettling;

    if (params->has_entry_named(0xB77BE94A)) { // springs
        script::Array* springs;
        params->fetch_list(0xB77BE94A, &springs, 1);
        spring_count_ = static_cast<uint16_t>(springs->extent_);
        springs_ = reallocate(springs_, spring_count_ * sizeof(Spring));
        // (each built by the vector constructor iterator 0x00401020 with 0x005902E0 - which does nothing)
        const uint32_t* elements = springs->find_array_value();
        for (int i = 0; i < spring_count_; ++i) {
            const float* v = vector_of(elements[i]);
            springs_[i].point = {v[0], v[1], v[2], 0.0f};
            springs_[i].active = true;
        }
    }

    // The shape: the contacts or spheres given, else Box (the default), Pyramid, Cylinder or Triangle.
    int shape = contact_count_ ? 4 : 0;
    if (params->has_entry_named(0xCCBFEA8C)) { // contacts
        script::Array* contacts;
        params->fetch_list(0xCCBFEA8C, &contacts, 0);
        set_contacts(contacts);
        shape = 4;
    }
    if (params->has_entry_named(0x5B6A8D18)) { // (the spheres)
        script::Array* spheres;
        params->fetch_list(0x5B6A8D18, &spheres, 0);
        set_spheres(spheres);
        shape = 4;
    }
    if (params->has_bare_flag(0xF756B7C5)) // Box
        shape = 0;
    else if (params->has_bare_flag(0x1CB1A2B6)) // Pyramid
        shape = 1;
    else if (params->has_bare_flag(0x64FBA415)) // Cylinder
        shape = 2;
    else if (params->has_bare_flag(0x20689278)) // Triangle
        shape = 3;
    switch (shape) {
    case 0: {
        maths::Vec4 top = {32.0f, 32.0f, 16.0f, 1.0f};
        params->fetch_vec("dimensions", reinterpret_cast<script::VectorValue*>(&top), 0);
        maths::Vec4 bottom = top;
        params->fetch_vec("top_dimensions", reinterpret_cast<script::VectorValue*>(&top), 0);
        params->fetch_vec("bottom_dimensions", reinterpret_cast<script::VectorValue*>(&bottom), 0);
        top = {top.x * kHalf, top.y * kHalf, top.z * kHalf, top.w * kHalf};
        bottom = {bottom.x * kHalf, bottom.y * kHalf, bottom.z * kHalf, bottom.w * kHalf};
        set_box(&top, &bottom);
        break;
    }
    case 1: {
        float height = 42.0f, depth = 32.0f;
        params->fetch_float(0x0AB21AF0, &height, 0); // height
        params->fetch_float(0x055CE396, &depth, 0);  // depth
        height = height * kHalf;
        depth = depth * kHalf;
        set_pyramid(height, depth);
        break;
    }
    case 2: {
        float radius = 12.0f, height = 60.0f;
        int edges = 6;
        params->fetch_float(0xC48391A5, &radius, 0); // radius
        float bottom_radius = radius;
        params->fetch_float(0x937E3B29, &radius, 0);        // top_radius
        params->fetch_float(0xE2AC8C3A, &bottom_radius, 0); // bottom_radius
        params->fetch_float(0x0AB21AF0, &height, 0);        // height
        params->fetch_int_value(0x4055F24A, &edges, 0);     // edges
        height = height * kHalf;
        set_cylinder(radius, bottom_radius, height, edges);
        break;
    }
    case 3: {
        maths::Vec4 dimensions = {32.0f, 32.0f, 16.0f, 1.0f};
        params->fetch_vec(0x1D82745A, reinterpret_cast<script::VectorValue*>(&dimensions), 0); // Dimensions
        dimensions = {dimensions.x * kHalf, dimensions.y * kHalf, dimensions.z * kHalf, dimensions.w * kHalf};
        set_triangle(&dimensions);
        break;
    }
    default:
        break;
    }

    if (params->has_entry_named(0xC35CAC0D)) { // directed_friction: one structure per contact
        script::Array* friction;
        params->fetch_list(0xC35CAC0D, &friction, 0);
        steer_friction_ = reallocate(steer_friction_, contact_count_ * sizeof(DirectedFriction));
        const uint32_t* elements = friction->find_array_value();
        for (int i = contact_count_; i != 0;) {
            --i;
            auto* settings = reinterpret_cast<script::Struct*>(elements[i]);
            if (settings->has_entry_named(0x806FFF30)) { // none
                steer_friction_[i].active = false;
            } else {
                steer_friction_[i].active = true;
                settings->fetch_vec(0u, reinterpret_cast<script::VectorValue*>(&steer_friction_[i].direction), 1);
            }
        }
    }

    if (params->has_entry_named(0x9D2D0915)) { // Angles: the shape turned
        maths::Vec4 angles;
        level_nodes::read_turn_angles(params, &angles);
        maths::Mat4 turn;
        turn.init_from_angles(angles);
        auto m = [&turn](int r, int c) { return static_cast<double>(reinterpret_cast<const float*>(&turn.row[r])[c]); };
        for (int i = contact_count_; i != 0;) {
            maths::Vec4& p = contacts_[--i];
            double x = p.x, y = p.y, z = p.z;
            p.z = static_cast<float>((m(0, 2) * x + m(1, 2) * y) + m(2, 2) * z);
            p.x = static_cast<float>((m(0, 0) * x + m(1, 0) * y) + m(2, 0) * z);
            p.y = static_cast<float>((m(0, 1) * x + m(1, 1) * y) + m(2, 1) * z);
        }
        for (int i = sphere_count_; i != 0;) {
            maths::Vec4& p = spheres_[--i].center;
            double x = p.x, y = p.y, z = p.z;
            p.z = static_cast<float>((m(1, 2) * y + m(2, 2) * z) + m(0, 2) * x);
            p.x = static_cast<float>((m(1, 0) * y + m(2, 0) * z) + m(0, 0) * x);
            p.y = static_cast<float>((m(1, 1) * y + m(2, 1) * z) + m(0, 1) * x);
        }
    }

    // The centre of mass: given, else the average of the points.
    maths::Vec4 center = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->has_entry_named(0x737612C6) && params->has_entry_named(0x737612C6))
        vector(0x737612C6, &center);
    if (center.x == 0.0f && center.y == 0.0f && center.z == 0.0f) {
        for (int i = contact_count_; i != 0;) {
            const maths::Vec4& p = contacts_[--i];
            center = {center.x + p.x, center.y + p.y, center.z + p.z, center.w + p.w};
        }
        for (int i = sphere_count_; i != 0;) {
            const maths::Vec4& p = spheres_[--i].center;
            center = {center.x + p.x, center.y + p.y, center.z + p.z, center.w + p.w};
        }
        double share = 1.0 / static_cast<double>(contact_count_ + sphere_count_);
        center = {static_cast<float>(center.x * share), static_cast<float>(center.y * share),
                  static_cast<float>(center.z * share), static_cast<float>(center.w * share)};
    }
    maths::Vec4 model_offset = {0.0f, 0.0f, 0.0f, 1.0f};
    vector(0x851211B0, &model_offset); // model_offset
    model_offset_ = {model_offset.x - center.x, model_offset.y - center.y, model_offset.z - center.z,
                     model_offset.w - center.w};
    // The shape about the centre of mass (w too).
    auto recentre = [&center](maths::Vec4& p) { p = {p.x - center.x, p.y - center.y, p.z - center.z, p.w - center.w}; };
    for (int i = contact_count_; i != 0;)
        recentre(contacts_[--i]);
    for (int i = sphere_count_; i != 0;)
        recentre(spheres_[--i].center);
    for (int i = spring_count_; i != 0;)
        recentre(springs_[--i].point);

    // mass_over_moment, else the default scaled down by the size of a big shape.
    if (!params->fetch_float(0x967CD3AE, &mass_over_moment_, 0)) {
        float extent = 0.0f;
        for (int i = contact_count_; i != 0;) {
            const maths::Vec4& p = contacts_[--i];
            double x = p.x, y = p.y, z = p.z;
            float length = static_cast<float>(std::sqrt((z * z + x * x) + y * y));
            if (!(extent > length))
                extent = length;
        }
        for (int i = sphere_count_; i != 0;) {
            const Sphere& s = spheres_[--i];
            double x = s.center.x, y = s.center.y, z = s.center.z;
            float length = static_cast<float>(std::sqrt((z * z + x * x) + y * y) + s.radius);
            if (!(extent > length))
                extent = length;
        }
        if (extent > kBigShape) {
            double scale = static_cast<double>(extent) * kShapeScale;
            mass_over_moment_ = static_cast<float>(mass_over_moment_ / (scale * scale));
        }
    }
    if (params->has_bare_flag(0xA92B9954)) // disable_player_collision
        flag_bits_ |= kFlagNoPlayerCollision;
    g_rigid_body_pusher_height = 77.0f;

    // The bounding radius (squared lengths compared unrounded).
    radius_ = 0.0f;
    for (int i = contact_count_; i != 0;) {
        const maths::Vec4& p = contacts_[--i];
        double x = p.x, y = p.y, z = p.z;
        double length_squared = (z * z + x * x) + y * y;
        if (length_squared > radius_)
            radius_ = static_cast<float>(length_squared);
    }
    for (int i = sphere_count_; i != 0;) {
        const Sphere& s = spheres_[--i];
        double x = s.center.x, y = s.center.y, z = s.center.z;
        double reach = std::sqrt((z * z + x * x) + y * y) + s.radius;
        if (reach * reach > radius_)
            radius_ = static_cast<float>(reach * reach);
    }
    push_cooldown_ = 0.0f;
    radius_ = static_cast<float>(std::sqrt(static_cast<double>(radius_)) + 1.0f);

    // The position is the centre of mass: moved by the (turned) model offset.
    auto r = [this](int row, int c) {
        return static_cast<double>(reinterpret_cast<const float*>(&basis_frame_.row[row])[c]);
    };
    const double ox = model_offset_.x, oy = model_offset_.y, oz = model_offset_.z;
    double tx = (r(2, 0) * oz + r(1, 0) * oy) + ox * r(0, 0);
    double ty = (r(2, 1) * oz + r(1, 1) * oy) + r(0, 1) * ox;
    float tz = static_cast<float>((r(2, 2) * oz + r(1, 2) * oy) + r(0, 2) * ox);
    float tw = model_offset_.w;
    at_point_.x = static_cast<float>(at_point_.x - tx);
    at_point_.y = static_cast<float>(at_point_.y - ty);
    at_point_.z = at_point_.z - tz;
    at_point_.w = at_point_.w - tw;
}

// Awake (the inlined end of the member functions): unless it is already, where the object is now kept.
void BodyDynamicsModule::wake()
{
    if (cur_phase_ == kStateAwake)
        return;
    wake_pos_ = entity_->at_point_;
    cur_phase_ = kStateAwake;
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
    script::VectorValue v;
    if (params->fetch_vec(0x0C4C809E, &v, 0)) { // vel
        vel_ = {v.x + vel_.x, v.y + vel_.y, v.z + vel_.z, v.w + vel_.w};
        entity_->vel_ = vel_;
    }
    if (params->fetch_vec(0xFB1A83B2, &v, 0)) // rotvel
        rot_vel_ = {v.x + rot_vel_.x, v.y + rot_vel_.y, v.z + rot_vel_.z, v.w + rot_vel_.w};
    if (params->fetch_vec(0x7F261953, &v, 0)) { // pos
        at_point_ = {v.x + at_point_.x, v.y + at_point_.y, v.z + at_point_.z, v.w + at_point_.w};
        entity_->at_point_ = at_point_;
    }
    wake();
    return true;
}

// 0x0042DEB0
bool BodyDynamicsModule::rigid_body_set_velocities(script::Struct* params, script::Script* /*script*/)
{
    script::VectorValue v;
    if (params->fetch_vec(0x0C4C809E, &v, 0)) { // vel
        vel_ = {v.x, v.y, v.z, v.w};
        entity_->vel_ = vel_;
    }
    if (params->fetch_vec(0xFB1A83B2, &v, 0)) // rotvel
        rot_vel_ = {v.x, v.y, v.z, v.w};
    wake();
    return true;
}

// 0x0042EE30
bool BodyDynamicsModule::rigid_body_enable(script::Struct* /*params*/, script::Script* /*script*/)
{
    cur_phase_ = kStateAwake;
    at_point_ = entity_->at_point_;
    maths::QuatRotation orientation;
    orientation.from_matrix(entity_->basis_frame_);
    orientation_ = orientation;
    orientation_.make_unit(1.0f);
    quat_to_matrix(&orientation_, &basis_frame_);
    vel_ = entity_->vel_;
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
    script->args_->put_vec3(0x77808D3C, {at_point_.x, at_point_.y, at_point_.z, at_point_.w}); // CenterOfMassPos
    return true;
}

// 0x0042E020
bool BodyDynamicsModule::rigidbody_match_velocity_to(script::Struct* params, script::Script* /*script*/)
{
    uint32_t id = params->step_to_entry(nullptr)->checksum;
    auto* other = reinterpret_cast<GameEntity*>(object_hub_slot()->look_up_entity(id));
    bool adjust = params->has_bare_flag(0x8ACE8A0F); // ApplyRandomAdjustment
    vel_ = other->vel_;
    if (adjust) {
        double factor = maths::plus_or_minus_exact(0.4000000059604645f) + 1.0f;
        vel_ = {static_cast<float>(factor * vel_.x), static_cast<float>(factor * vel_.y),
                static_cast<float>(factor * vel_.z), static_cast<float>(factor * vel_.w)};
    }
    entity_->vel_ = vel_;
    if (cur_phase_ == kStateAsleep && std::fabs(vel_.y) < 1.0f)
        vel_.y = 1.0f;
    if (auto* body = static_cast<BodyDynamicsModule*>(other->fetch_module(kType))) {
        rot_vel_ = body->rot_vel_;
        if (adjust) {
            double factor = maths::plus_or_minus_exact(0.4000000059604645f);
            rot_vel_ = {static_cast<float>(factor * rot_vel_.x), static_cast<float>(factor * rot_vel_.y),
                        static_cast<float>(factor * rot_vel_.z), static_cast<float>(factor * rot_vel_.w)};
        }
    }
    wake();
    return true;
}

// 0x0042E380
void __cdecl add_rigid_body_pusher(GameEntity* object)
{
    auto* pushers = g_rigid_body_pushers;
    for (int i = 7; i >= 0; --i) {
        if (pushers[i].owner_object_)
            continue;
        pushers[i].assign_value(object);
        g_rigid_body_pusher_reach[i] = 0.0f; // (its extra reach: set_rigid_body_pusher_reach)
        return;
    }
}

} // namespace objects

namespace objects {

// 0x00640900 (a static initialiser): the rigid bodies' collision cache (registered among the caches, empty).
// (never destroyed: it stays registered to the end)
void construct_rigid_body_cache() { new (&g_rigid_body_cache) render::HitMeshCache; }

} // namespace objects
