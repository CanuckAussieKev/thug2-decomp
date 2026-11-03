// Script commands (CFunctions) for nodes and composite objects, 0x00449280-0x00449F80 (see object_cfuncs.h).

#include "engine/objects/object_cfuncs.h"
#include "platform/d3dx.h"

#include <cstdint>
#include <new>

#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/gfx/weather.h"
#include "engine/objects/particle_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/gfx/particle_sets.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec2.h"
#include <bit>

namespace commands {

namespace {
constexpr uint32_t kDefaultId = 8;

constexpr uint32_t kNode = 0x7A8017BA;         // "node"
constexpr uint32_t kParams = 0x7031F10C;       // "params"
constexpr uint32_t kSize = 0x083FDB95;         // "size"
constexpr uint32_t kCoimB0847d54 = 0xB0847D54; // (name unknown) InitCOIM's second integer
constexpr uint32_t kCoimBb9a31b5 = 0xBB9A31B5; // (name unknown) InitCOIM's first integer
constexpr uint32_t kCoim03d6dE47 = 0x03D6DE47; // (name unknown) InitCOIM's float (squared)
constexpr uint32_t kCoimNewSize = 0x7C554E79;  // "newsize"
constexpr uint32_t kComponents = 0x11B70A02;   // "Components"
constexpr uint32_t kPreferredId = 0x392649D7;  // "PreferredId"
constexpr uint32_t kUniqueId = 0xA2B033FD;     // "UniqueID"
constexpr uint32_t kObject = 0x57525413;       // "object"
constexpr uint32_t kScreenX = 0x3828EDE4;      // "screen_x"
constexpr uint32_t kScreenY = 0x4F2FDD72;      // "screen_y"
constexpr uint32_t kComponent = 0xB6015EA8;    // "component"
constexpr uint32_t kTarget = 0xB990D003;       // "target"
constexpr uint32_t kLocalOffset = 0xA18717AA;  // "local_offset"
constexpr uint32_t kWorldOffset = 0x36CB486C;  // "world_offset"
constexpr uint32_t kGroupId = 0x87FA53ED;      // "groupID"
constexpr uint32_t kParticle = 0xA9DB601E;     // "particle" (the component type)
constexpr uint32_t kNodeName = 0x9F92BA78;     // "nodename"
constexpr uint32_t kId = 0x40C698AF;           // "id"
constexpr uint32_t kRelativeTo = 0xC03F9839;   // "RelativeTo"
constexpr uint32_t kBone = 0xCAB94088;         // "bone"
constexpr uint32_t kOffset = 0xA6F5352F;       // "offset"
constexpr uint32_t kAngles = 0x9D2D0915;       // "Angles"
constexpr uint32_t kDegrees = 0x30EDA37B;      // "degrees"
constexpr uint32_t kSkeleton = 0x222756D5;     // "skeleton" (the component type)
constexpr uint32_t kEx = 0xF8728BEC;           // "ex"
constexpr uint32_t kScr = 0xA6D2D890;          // "scr"
constexpr uint32_t kGroup = 0x923FBB3A;        // "Group"
constexpr uint32_t kDefaultGroup = 0x1CA1FF20; // "default"
constexpr uint32_t kException = 0x80367192;    // "Exception"

objects::GameEntity* entity_for_id(uint32_t id)
{
    return core::down_cast<objects::GameEntity>(objects::object_hub_slot()->look_up_entity(id));
}

// The camera of viewport `index` (0x004A06D0), and its projection of a world position to the screen (0x004A1090,
// vtable slot 1 of the camera: x, y and depth out; returns a scale, unused here).
render::ViewRegion* viewport_camera(int index) { return render::fetch_current_view(index); }
// (the depth is written as an integer into the float, as the original)
float project_to_screen(render::ViewRegion* camera, const maths::Vec4* position, maths::Vec2* at, float* depth)
{
    int whole = 0;
    const float result = camera->project_to_screen(position, at, &whole);
    *depth = std::bit_cast<float>(whole);
    return result;
}

// The renderer's particle manager (0x006FA7C0) and its "destroy the particle systems of this group" (0x0049ADE0).

void destroy_particle_group(void* manager, uint32_t group)
{
    static_cast<render::FlexibleParticleManager*>(manager)->destroy_group(group);
}

// A particle component's group id: its particle system's (its parameters').
uint32_t particle_group(objects::ObjectModule* component)
{
    return core::down_cast<objects::EffectsEmitterModule>(component)->effect_handle()->args().group_id();
}

// The rotation of `angles` (radians) as a matrix (0x0041A730: the identity times RotationX, RotationY, RotationZ).
void angles_to_matrix(maths::Mat4* matrix, const script::VectorValue* angles)
{
    matrix->init_from_angles(*angles);
}

// D3DX's matrix product (linked library code; stdcall): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// A skeleton component's skeleton (0x005480F0: its +0x18) and that skeleton's matrix of `bone` (0x004A36E0; false -
// `out` untouched - if it has no such bone).
void* skeleton_of(objects::ObjectModule* component)
{
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    return core::down_cast<objects::BoneRigModule>(component)->bone_rig();
}
bool read_joint_matrix(void* skeleton, uint32_t bone, maths::Mat4* out)
{
    return static_cast<gfx::Rig*>(skeleton)->read_joint_matrix(bone, out);
}

void flag_as_dead(objects::Entity* object) { object->flag_as_dead(); }

} // namespace

// 0x00449280: AppendNodeParams node=<name> params={...}: the parameters added to the node's own structure.
bool cfunc_append_node_params(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kNode, &name, 1);
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    script::Struct* extra = nullptr;
    params->read_sub_struct(kParams, &extra, 1);
    node->append_structure_if_any(extra);
    return true;
}

// 0x004492E0: InitCOIM size=<bytes> ...: the COIM heap set up (GameObjects::InitCoim) with the three integers and the
// float squared, truncated.
bool cfunc_init_coim(script::Struct* params, script::Script*)
{
    float factor = 0.0f;
    int size = 0, b = 0, a = 0;
    params->fetch_int_value(kSize, &size, 1);
    params->fetch_int_value(kCoimB0847d54, &b, 1);
    params->fetch_int_value(kCoimBb9a31b5, &a, 1);
    params->fetch_float(kCoim03d6dE47, &factor, 1);
    // (x87: the square unrounded, _ftol's truncation)
    const int squared = static_cast<int>(static_cast<int64_t>(static_cast<double>(factor) * factor));
    objects::init_coim(a, static_cast<uint32_t>(size), b, squared);
    return true;
}

// 0x00449380: ResizeCOIMHeap newsize=<bytes>.
bool cfunc_resize_coim_heap(script::Struct* params, script::Script*)
{
    int size = 0;
    params->fetch_int_value(kCoimNewSize, &size, 1);
    objects::resize_coim_heap(static_cast<uint32_t>(size));
    return true;
}

// 0x00449540: CreateCompositeObject Components=[...] [params={...}]: a composite object made from them (finalized).
bool cfunc_create_composite_object(script::Struct* params, script::Script*)
{
    script::Array* components = nullptr;
    params->fetch_list(kComponents, &components, 1);
    script::Struct* node_data = nullptr;
    params->read_sub_struct(kParams, &node_data, 0);
    objects::object_hub_slot()->spawn_entity_from_node(components, node_data, true);
    return true;
}

// 0x004495A0: GetUniqueCompositeObjectID [PreferredId=<id> (8)]: UniqueID = the first id from there no object has.
bool cfunc_get_unique_composite_object_id(script::Struct* params, script::Script* script)
{
    uint32_t id = kDefaultId;
    params->read_hash_value(kPreferredId, &id, 0);
    while (entity_for_id(id))
        ++id;
    script->args()->put_name_key(checksums::Name{kUniqueId}, id);
    return true;
}

// 0x00449610: GetObjScreenCoords object=<id>: screen_x / screen_y = where the object's position is on the screen
// through the first viewport's camera. False if there is no such object.
bool cfunc_get_obj_screen_coords(script::Struct* params, script::Script* script)
{
    render::ViewRegion* camera = viewport_camera(0);
    uint32_t id = 0;
    params->read_hash_value(kObject, &id, 1);
    const objects::GameEntity* object = entity_for_id(id);
    if (!object)
        return false;
    maths::Vec2 at{.x = 0.0f, .y = 0.0f};
    float depth = 0.0f;
    project_to_screen(camera, &object->at_point(), &at, &depth);
    const float x = at.x;
    const float y = at.y;
    script->args()->push_float(checksums::Name{kScreenX}, x);
    script->args()->push_float(checksums::Name{kScreenY}, y);
    return true;
}

// 0x004496B0: RunScriptOnComponentType component=<type> target=<member function> [params={...}]: the member
// function called on every component of that type.
bool cfunc_run_script_on_component_type(script::Struct* params, script::Script* script)
{
    uint32_t type = 0, target = 0;
    params->read_hash_value(kComponent, &type, 1);
    params->read_hash_value(kTarget, &target, 1);
    script::Struct* target_params = nullptr;
    params->read_sub_struct(kParams, &target_params, 0);
    for (objects::ObjectModule* component = objects::object_hub_slot()->find_first_module_of_kind(type); component;
         component = component->next_of_type())
        component->invoke_method_by_name(target, target_params, script);
    return true;
}

// 0x00449740: TransformLocalToWorld local_offset=<vector> object=<id>: world_offset = the offset through the
// object's display matrix. (No check that the object exists; missing arguments leave the original's values
// undefined - here zero.)
bool cfunc_transform_local_to_world(script::Struct* params, script::Script* script)
{
    script::VectorValue offset = {};
    params->fetch_vec(kLocalOffset, &offset, 0);
    uint32_t id = 0;
    params->read_hash_value(kObject, &id, 0);
    const objects::GameEntity* object = entity_for_id(id);
    const maths::Vec4 local = {.x = offset.x, .y = offset.y, .z = offset.z, .w = offset.w};
    maths::Vec4 world;
    object->display_matrix().xform(world, local);
    script->args()->put_vec3(checksums::Name{kWorldOffset}, {.x = world.x, .y = world.y, .z = world.z});
    return true;
}

// 0x004497D0: DestroyParticlesByGroupID groupID=<id>: the renderer's particle systems of that group destroyed (if
// there is a particle manager), and every live object with a particle component of that group marked dead.
bool cfunc_destroy_particles_by_group_id(script::Struct* params, script::Script*)
{
    uint32_t group = 0;
    params->read_hash_value(kGroupId, &group, 1);
    if (void* manager = render::flexible_particle_manager())
        destroy_particle_group(manager, group);
    for (objects::ObjectModule* component = objects::object_hub_slot()->find_first_module_of_kind(kParticle); component;
         component = component->next_of_type()) {
        objects::GameEntity* object = component->entity();
        if (!(object->object_flags() & objects::kObjectFlagDead) && particle_group(component) == group)
            flag_as_dead(object);
    }
    return true;
}

// 0x00449850: fetch_vertex <name>: node = the node's structure. False if there is none.
bool cfunc_get_node(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 1);
    script::Struct* node = level_nodes::fetch_vertex(name, false);
    if (!node)
        return false;
    script->args()->put_sub_struct(kNode, node);
    return true;
}

// 0x00449890: GetNodeName: nodename = the id of the object the script runs on. False with no object or id 0.
bool cfunc_get_node_name(script::Struct*, script::Script* script)
{
    const objects::Entity* object = script->entity().get_pull();
    if (!object || !object->ident())
        return false;
    script->args()->put_name_key(checksums::Name{kNodeName}, object->ident());
    return true;
}

// 0x004498C0: GetTriggerNodeName: nodename = the node the script was spawned for. False if none.
bool cfunc_get_trigger_node_name(script::Struct*, script::Script* script)
{
    if (!script->list_link())
        return false;
    script->args()->put_name_key(checksums::Name{kNodeName}, static_cast<uint32_t>(script->list_link()));
    return true;
}

// 0x00449A10: RunScriptOnObject id=<object> <script> [params={...}]: UniqueID is taken out of the calling script's
// parameters; the object (found through the tracker) runs the script now as an interrupt of its own script (which,
// if it has no object yet, is given this one). False if there is no such object.
bool cfunc_run_script_on_object(script::Struct* params, script::Script* script)
{
    uint32_t id = 0, name = 0;
    params->read_hash_value(kId, &id, 1);
    params->read_hash_value(0u, &name, 1);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    script->args()->erase_module(kUniqueId);
    objects::Entity* object = objects::find_object(id);
    if (!object)
        return false;
    script::Script* own = object->own_script();
    if (own && !own->entity().get_pull())
        own->entity().assign_value(object);
    object->run_sub_script(name, script_params);
    return true;
}

// 0x00449AC0: Orient object=<id> [RelativeTo=<id>] [bone=<name>] [offset=<vector>] [Angles=<vector>] [degrees]:
// the object's matrix made from the angles (radians unless `degrees`), its position the offset; then, with a bone,
// times that bone's matrix in the relative object's skeleton (no RelativeTo: a crash, as in the original), and
// with RelativeTo, times that object's display matrix at its position. The result becomes the object's position,
// matrix (position row reset) and display matrix. (No check that the object exists.)
bool cfunc_orient(script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    params->read_hash_value(kObject, &id, 1);
    objects::GameEntity* object = entity_for_id(id);
    objects::GameEntity* relative_to = nullptr;
    uint32_t relative_id = 0;
    if (params->read_hash_value(kRelativeTo, &relative_id, 0))
        relative_to = entity_for_id(relative_id);
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 0);
    script::VectorValue offset{};
    if (!params->fetch_vec(kOffset, &offset, 0))
        offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    script::VectorValue angles{};
    if (!params->fetch_vec(kAngles, &angles, 0))
        angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (params->has_bare_flag(kDegrees)) {
        const float degrees = kDegreeToRadianRatio; // pi / 180
        angles.x *= degrees;
        angles.y *= degrees;
        angles.z *= degrees;
        angles.w *= degrees;
    }

    maths::Mat4& matrix = object->basis_frame();
    angles_to_matrix(&matrix, &angles);
    core::at(matrix.row, 3) = {.x = offset.x, .y = offset.y, .z = offset.z, .w = offset.w};
    if (bone) {
        maths::Mat4 joint_mat;
        read_joint_matrix(skeleton_of(relative_to->fetch_module(kSkeleton)), bone, &joint_mat);
        matrix_multiply(&matrix, &matrix, &joint_mat);
    }
    if (relative_to) {
        maths::Mat4 relative;
        relative = relative_to->display_matrix();
        core::at(relative.row, 3) = relative_to->at_point();
        matrix_multiply(&matrix, &matrix, &relative);
    }
    object->at_point() = core::at(matrix.row, 3);
    core::at(matrix.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    object->display_matrix() = matrix;
    return true;
}

// 0x00449F80: SetEventHandlerOnObject object=<id> ex=<event> scr=<script> [Group=<name> (default)] [Exception]
// [params={...}]: the event handler set on the object's script (made - on the script pool - if it has none). (No
// check that the object exists.)
bool cfunc_set_event_handler_on_object(script::Struct* params, script::Script*)
{
    uint32_t event_type = 0, handler = 0, group = kDefaultGroup;
    params->read_hash_value(kEx, &event_type, 1);
    params->read_hash_value(kScr, &handler, 1);
    params->read_hash_value(kGroup, &group, 0);
    const bool exception = params->has_bare_flag(kException);
    script::Struct* handler_params = nullptr;
    params->read_sub_struct(kParams, &handler_params, 0);
    uint32_t id = 0;
    params->read_hash_value(kObject, &id, 1);
    objects::GameEntity* object = entity_for_id(id);
    if (!object->own_script()) {
        object->set_own_script(script::Script::create());
    }
    object->own_script()->set_event_handler(event_type, handler, group, exception, handler_params);
    return true;
}

} // namespace commands
