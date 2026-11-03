// Cars made in code and the King of the Hill crown, 0x005780E0-0x00578670. See game_objects.h.

#include "engine/objects/game_objects.h"

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/text_store.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/bbox_component.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/model_component.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/object_proximity.h"
#include "engine/objects/sound_component.h"
#include "engine/objects/stream_component.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/path_follower.h"
#include <gsl/pointers>
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {
constexpr std::size_t kDormancyModuleBytes = 0x4C;   // the sizes the components are allocated with
constexpr std::size_t kKinematicsModuleBytes = 0x24C;
constexpr std::size_t kObjectProximityBytes = 0x30;
constexpr std::size_t kAudioEmitterBytes = 0xC0;
constexpr std::size_t kAudioStreamBytes = 0x2C;
constexpr std::size_t kPinnedObjectBytes = 0x90;
constexpr std::size_t kAutomobileDynamicsBytes = 0x58;
constexpr float kPedMaxSpeed = 44.0f;                // a ped's path-following tunings (inches per second, inches)
constexpr float kPedAcceleration = 48.0f;
constexpr float kPedDeceleration = 12.0f;
constexpr float kPedStopDistance = 24.0f;
constexpr float kPedCornerDistance = 24.0f;
constexpr uint32_t kMotionFlag2PedPath = 0x400;
constexpr float kVehicleMaxSpeed = 528.0f;           // a vehicle's
constexpr float kVehicleAcceleration = 216.0f;
constexpr float kVehicleDeceleration = 384.0f;
constexpr float kVehicleCornerDistance = 240.0f;
constexpr float kPitchQuarterTurn = 90.0f;
constexpr float kLodDistanceNear = 1000000.0f;       // lod distances so far that no level of detail switches
constexpr float kLodDistanceNear1 = 1000001.0f;
constexpr float kLodDistanceNear2 = 1000002.0f;
constexpr float kLodDistanceNear3 = 1000003.0f;

// Names.
constexpr uint32_t kMotion = 0x0A015E17;           // motion (a component)
constexpr uint32_t kModel = 0x286A8D26;            // model
constexpr uint32_t kSkeleton = 0x222756D5;         // skeleton
constexpr uint32_t kSuspend = 0xCE0CA665;          // suspend
constexpr uint32_t kComponent = 0xB6015EA8;        // component
constexpr uint32_t kBoneHead = 0xDDEC28AF;         // bone_head
constexpr uint32_t kCrownObject = 0xC015BC23;      // crown_object (its id)
constexpr uint32_t kCarAddComponents = 0xF1753591; // car_add_components (a script)
constexpr uint32_t kObjectTypeCar = 0x5C0312EB;    // OBJECT_TYPE_CAR
constexpr uint32_t kObjectTypeCrown = 0xD2156C50;

constexpr uint32_t kTriggerScript = 0x2CA8A299;    // TriggerScript
constexpr uint32_t kParams = 0x7031F10C;           // params
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;    // OBJECT_TYPE_PED
constexpr uint32_t kPedAddComponents = 0xF3EEC45E; // ped_add_components (a script)
constexpr uint32_t kPedInitModel = 0x955A2767;     // ped_init_model (a script)
constexpr uint32_t kPedDisableBones = 0x48CA2D26;  // ped_disable_bones (a script)

// A new component of `Size` bytes (the original's allocation) from T's constructor (null if there's no memory).
template <typename T, unsigned int Size>
gsl::owner<ObjectModule*> new_component()
{
    static_assert(sizeof(T) == Size, "the original allocates this many bytes");
    return new T;
}
// A component's configure_from_data (vtable +0x08).
void init_component_from_structure(ObjectModule* component, script::Struct* params)
{
    component->configure_from_data(params);
}
// The model component's update after the object is finished (0x00420480).
void update_model(ObjectModule* model) { core::down_cast<MeshInstanceModule>(model)->jump_to(); }

} // namespace

// 0x0057B250
void add_base_components(GameEntity* object)
{
    object->attach_module(new_component<DormancyModule, kDormancyModuleBytes>()); // suspend
    object->attach_module(new_component<CbBoxComponent, kCbBoxComponentBytes>()); // (type 0xB87C8E43)
}

// 0x0057B2F0
void add_motion_components(GameEntity* object)
{
    object->attach_module(new_component<KinematicsModule, kKinematicsModuleBytes>());        // motion
    object->attach_module(new_component<ObjectProximityComponent, kObjectProximityBytes>()); // objectproximity
    object->attach_module(new_component<AudioEmitterModule, kAudioEmitterBytes>());       // sound
    object->attach_module(new_component<AudioStreamModule, kAudioStreamBytes>());        // stream
    object->attach_module(new_component<PinnedObjectModule, kPinnedObjectBytes>());       // lockobj
}

// 0x0057B420
void add_node_components(GameEntity* object, script::Struct* node)
{
    ObjectModule* motion = object->fetch_module(kMotion);
    if (!motion)
        add_motion_components(object); // (sic: not looked up again - the motion stays null below)
    core::down_cast<KinematicsModule>(motion)->face_node(node);
    if (const uint32_t name = level_nodes::get_node_name(node))
        object->change_serial(name);
    uint32_t script = 0;
    if (node->read_hash_value(kTriggerScript, &script, 0)) {
        script::Struct* params = nullptr;
        node->read_sub_struct(kParams, &params, 0);
        object->replace_script(script, params);
    }
    init_component_from_structure(motion, node);
    init_component_from_structure(object->fetch_module(kSuspend), node);
}

// 0x0057B4E0
void spawn_pedestrian(script::Struct* params)
{
    gsl::owner<objects::GameEntity*> ped{memory::make_owned<GameEntity>()};
    add_base_components(ped);
    add_motion_components(ped);
    ped->change_species(kObjectTypePed);
    object_hub_slot()->enrol_subject(ped);
    level_nodes::fetch_place(params, &ped->at_point());
    add_node_components(ped, params);
    script::launch_script(kPedAddComponents, params, ped, false, nullptr);
    ped->seal();
    script::launch_script(kPedInitModel, params, ped, false, nullptr);
    auto* motion = core::down_cast<KinematicsModule>(ped->fetch_module(kMotion));
    motion->set_path_max_speed(kPedMaxSpeed);
    motion->assign_path_acceleration(kPedAcceleration);
    motion->assign_path_deceleration(kPedDeceleration);
    motion->set_path_stop_distance(kPedStopDistance);
    motion->init_path_follower(ped);
    auto* path = motion->get_path_follower();
    path->set_corner_distance(kPedCornerDistance);
    motion->motion_flags2() |= kMotionFlag2PedPath;
    update_model(ped->fetch_module(kModel));
    script::launch_script(kPedDisableBones, nullptr, ped, false, nullptr);
}

// 0x005780E0
void set_up_automobile(GameEntity* car, script::Struct* params)
{
    add_motion_components(car);
    add_node_components(car, params);
    auto* motion = core::down_cast<KinematicsModule>(car->fetch_module(kMotion));
    motion->set_path_max_speed(kVehicleMaxSpeed);
    motion->assign_path_acceleration(kVehicleAcceleration);
    motion->assign_path_deceleration(kVehicleDeceleration);
    motion->set_ground_height_only(false);
    motion->init_path_follower(car);
    auto* extra = motion->get_path_follower();
    extra->set_corner_distance(kVehicleCornerDistance);
    script::launch_script(kCarAddComponents, params, car, false, nullptr);
    car->seal();
    update_model(car->fetch_module(kModel));
}

// 0x00578170
void spawn_automobile(script::Struct* params)
{
    gsl::owner<objects::GameEntity*> car{memory::make_owned<GameEntity>()};
    add_base_components(car);
    car->change_species(kObjectTypeCar);
    car->attach_module(new_component<AutomobileDynamicsModule, kAutomobileDynamicsBytes>());
    level_nodes::fetch_place(params, &car->at_point());
    object_hub_slot()->enrol_subject(car);
    set_up_automobile(car, params);
}

// 0x00578240
CrownObject::CrownObject()
{
    add_base_components(this);
    set_kind_tag(kObjectTypeCrown);
    change_serial(kCrownObject);
}

// 0x005782B0
void CrownObject::put_on(GameEntity* skater)
{
    skater_ = skater;
    worn_ = true;
}

// 0x005782D0
void CrownObject::take_off() { worn_ = false; }

// 0x005782E0
bool CrownObject::is_worn() { return worn_; }

// 0x005782F0
CrownObject::~CrownObject() { worn_ = false; }

// 0x00578310
void CrownObject::own_update()
{
    if (!worn_)
        return;
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    void* skeleton = core::down_cast<objects::BoneRigModule>(skater_->fetch_module(kSkeleton))->bone_rig();
    static_cast<gfx::Rig*>(skeleton)->read_joint_matrix(kBoneHead, &basis_frame());
    maths::Mat4 turn;
    maths::get_multiply(basis_frame(), maths::make_pitch_matrix(turn, kPitchQuarterTurn), basis_frame());
    const maths::Vec4 raise = {.x = 0.0f, .y = 0.0f, .z = -18.0f, .w = 0.0f};
    basis_frame().offset_local(raise);
    const maths::Vec4 offset = core::at(basis_frame().row, 3);
    core::at(basis_frame().row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::get_multiply(basis_frame(), basis_frame(), skater_->basis_frame());
    maths::Vec4 moved;
    at_point() = skater_->basis_frame().xform(moved, offset); // (the matrix times the offset, w too)
    const maths::Vec4& at = skater_->at_point();
    at_point().x = static_cast<float>(static_cast<double>(at.x) + at_point().x);
    at_point().y = static_cast<float>(static_cast<double>(at.y) + at_point().y);
    at_point().z = static_cast<float>(static_cast<double>(at.z) + at_point().z);
    at_point().w = static_cast<float>(static_cast<double>(at.w) + at_point().w);
}

// 0x00578490
void CrownObject::init_from_node(EntityHub* manager, script::Struct* node)
{
    manager->enrol_subject(this);
    add_motion_components(this);
    script::StructPtr model = script::make_struct();
    model->put_name_key(checksums::Name{kComponent}, kModel);
    create_component_from_structure(model.get(), nullptr);
    model.reset();
    add_node_components(this, node);
    change_serial(kCrownObject);
    auto* suspend = core::down_cast<DormancyModule>(fetch_module(kSuspend));
    core::at(suspend->lod_distances(), 0) = kLodDistanceNear;
    core::at(suspend->lod_distances(), 1) = kLodDistanceNear1;
    core::at(suspend->lod_distances(), 2) = kLodDistanceNear2;
    core::at(suspend->lod_distances(), 3) = kLodDistanceNear3;
    text::Chars file;
    file = gfx::make_model_path("crown", ".mdl"); // (0x004879D0 returns a String: a temporary)
    // The model component's model (+0x208): loaded from the file (0x004935E0), then 0x00493C60(1).
    void* model_data = core::down_cast<objects::MeshInstanceModule>(fetch_module(kModel))->mesh_instance().get();
    static_cast<render::RenderModel*>(model_data)->add_file_geom(file.c_str(), 0, true, 0, false, false, true, false);
    model_data = core::down_cast<objects::MeshInstanceModule>(fetch_module(kModel))->mesh_instance().get();
    static_cast<render::RenderModel*>(model_data)->change_live(1);
    level_nodes::fetch_place(node, &at_point());
    seal();
    update_model(fetch_module(kModel));
}

} // namespace objects
