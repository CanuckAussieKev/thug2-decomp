// Cars made in code and the King of the Hill crown, 0x005780E0-0x00578670. See game_objects.h.

#include "engine/objects/game_objects.h"

#include <cstdint>

#include "engine/core/manager.h"
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
#include "engine/objects/entity_hub.h"
#include "engine/objects/sound_component.h"
#include "engine/objects/stream_component.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/path_follower.h"
#include "engine/core/owner.h"

namespace objects {

namespace {

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
memory::owner<ObjectModule*> new_component()
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
void update_model(ObjectModule* model) { static_cast<MeshInstanceModule*>(model)->jump_to(); }
script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

} // namespace

// 0x0057B250
void add_base_components(GameEntity* object)
{
    object->attach_module(new_component<DormancyModule, 0x4C>()); // suspend
    object->attach_module(new_component<CbBoxComponent, 0x4C>()); // (type 0xB87C8E43)
}

// 0x0057B2F0
void add_motion_components(GameEntity* object)
{
    object->attach_module(new_component<KinematicsModule, 0x24C>());        // motion
    object->attach_module(new_component<ObjectProximityComponent, 0x30>()); // objectproximity
    object->attach_module(new_component<AudioEmitterModule, 0xC0>());       // sound
    object->attach_module(new_component<AudioStreamModule, 0x2C>());        // stream
    object->attach_module(new_component<PinnedObjectModule, 0x90>());       // lockobj
}

// 0x0057B420
void add_node_components(GameEntity* object, script::Struct* node)
{
    ObjectModule* motion = object->fetch_module(kMotion);
    if (!motion)
        add_motion_components(object); // (sic: not looked up again - the motion stays null below)
    static_cast<KinematicsModule*>(motion)->face_node(node);
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
    auto* ped = new GameEntity;
    add_base_components(ped);
    add_motion_components(ped);
    ped->change_species(kObjectTypePed);
    object_hub_slot()->enrol_subject(ped);
    level_nodes::fetch_place(params, &ped->at_point_);
    add_node_components(ped, params);
    script::launch_script(kPedAddComponents, params, ped, false, nullptr);
    ped->seal();
    script::launch_script(kPedInitModel, params, ped, false, nullptr);
    auto* motion = static_cast<KinematicsModule*>(ped->fetch_module(kMotion));
    motion->path_max_speed_ = 44.0f;
    motion->path_acceleration_ = 48.0f;
    motion->path_deceleration_ = 12.0f;
    motion->path_turn_distance_ = 24.0f;
    motion->init_path_follower(ped);
    auto* path = static_cast<PathFollower*>(reinterpret_cast<MotionPathMembers*>(motion)->get_path_follower());
    path->corner_distance_ = 24.0f;
    motion->motion_flags2_ |= 0x400;
    update_model(ped->fetch_module(kModel));
    script::launch_script(kPedDisableBones, nullptr, ped, false, nullptr);
}

// 0x005780E0
void set_up_automobile(GameEntity* car, script::Struct* params)
{
    add_motion_components(car);
    add_node_components(car, params);
    auto* motion = static_cast<KinematicsModule*>(car->fetch_module(kMotion));
    motion->path_max_speed_ = 528.0f;
    motion->path_acceleration_ = 216.0f;
    motion->path_deceleration_ = 384.0f;
    motion->ground_height_only_ = false;
    motion->init_path_follower(car);
    auto* extra = static_cast<PathFollower*>(reinterpret_cast<MotionPathMembers*>(motion)->get_path_follower());
    extra->corner_distance_ = 240.0f;
    script::launch_script(kCarAddComponents, params, car, false, nullptr);
    car->seal();
    update_model(car->fetch_module(kModel));
}

// 0x00578170
void spawn_automobile(script::Struct* params)
{
    auto* car = new GameEntity;
    add_base_components(car);
    car->change_species(kObjectTypeCar);
    car->attach_module(new_component<AutomobileDynamicsModule, 0x58>());
    level_nodes::fetch_place(params, &car->at_point_);
    object_hub_slot()->enrol_subject(car);
    set_up_automobile(car, params);
}

// 0x00578240
CrownObject::CrownObject()
{
    add_base_components(this);
    kind_tag_ = kObjectTypeCrown;
    change_serial(kCrownObject);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    worn_ = false;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
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
    void* skeleton = static_cast<objects::BoneRigModule*>(skater_->fetch_module(kSkeleton))->bone_rig_;
    static_cast<gfx::Rig*>(skeleton)->read_joint_matrix(kBoneHead, &basis_frame_);
    maths::Mat4 turn;
    maths::get_multiply(basis_frame_, maths::make_pitch_matrix(turn, 90.0f), basis_frame_);
    const maths::Vec4 raise = {0.0f, 0.0f, -18.0f, 0.0f};
    basis_frame_.offset_local(raise);
    const maths::Vec4 offset = basis_frame_.row[3];
    basis_frame_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::get_multiply(basis_frame_, basis_frame_, skater_->basis_frame_);
    maths::Vec4 moved;
    at_point_ = skater_->basis_frame_.xform(moved, offset); // (the matrix times the offset, w too)
    const maths::Vec4& at = skater_->at_point_;
    at_point_.x = static_cast<float>(static_cast<double>(at.x) + at_point_.x);
    at_point_.y = static_cast<float>(static_cast<double>(at.y) + at_point_.y);
    at_point_.z = static_cast<float>(static_cast<double>(at.z) + at_point_.z);
    at_point_.w = static_cast<float>(static_cast<double>(at.w) + at_point_.w);
}

// 0x00578490
void CrownObject::init_from_node(EntityHub* manager, script::Struct* node)
{
    manager->enrol_subject(this);
    add_motion_components(this);
    script::Struct* model = spawn_param_table();
    model->put_name_key(kComponent, kModel);
    create_component_from_structure(model, nullptr);
    delete_structure(model);
    add_node_components(this, node);
    change_serial(kCrownObject);
    auto* suspend = static_cast<DormancyModule*>(fetch_module(kSuspend));
    suspend->lod_distances_[0] = 1000000.0f;
    suspend->lod_distances_[1] = 1000001.0f;
    suspend->lod_distances_[2] = 1000002.0f;
    suspend->lod_distances_[3] = 1000003.0f;
    text::Chars file;
    {
        // The model's path (0x004879D0 returns a String: made in `storage`).
        alignas(text::Chars) uint8_t storage[sizeof(text::Chars)];
        auto* path = gfx::make_model_path(reinterpret_cast<text::Chars*>(storage), "crown", ".mdl");
        file = *path;
        path->~Chars();
    }
    // The model component's model (+0x208): loaded from the file (0x004935E0), then 0x00493C60(1).
    void* model_data = static_cast<objects::MeshInstanceModule*>(fetch_module(kModel))->mesh_instance_;
    static_cast<render::RenderModel*>(model_data)->add_file_geom(file.c_str(), 0, true, 0, false, false, true, false);
    model_data = static_cast<objects::MeshInstanceModule*>(fetch_module(kModel))->mesh_instance_;
    static_cast<render::RenderModel*>(model_data)->change_live(1);
    level_nodes::fetch_place(node, &at_point_);
    seal();
    update_model(fetch_module(kModel));
}

} // namespace objects
