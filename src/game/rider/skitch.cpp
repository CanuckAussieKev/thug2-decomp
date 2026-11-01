// The skitch component: skitching allowed or not, and a car's hook points. See skitch.h.

#include "game/rider/skitch.h"

#include <bit>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/node_objects.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr uint32_t kObjectHookManager = 0x27443708;
constexpr uint32_t kAllowSkitching = 0xC38CCE3B; // Obj_AllowSkitching
const char* const kOff = "off";                  // "off"

// The hook manager's node list (the original's slot 13), or null.
ClassNodeList* hook_nodes(ObjectModule* hooks) { return static_cast<HookPointRegistryModule*>(hooks)->get_hooks(); }

// The nodes' transform: the object's orientation (the w column cleared) at its position.
void place_nodes(ClassNodeList* nodes, GameEntity* object)
{
    maths::Mat4 transform{};
    transform = object->basis_frame_;
    transform.row[3].x = object->at_point_.x;
    transform.row[3].z = object->at_point_.z;
    transform.row[0].w = 0.0f;
    transform.row[1].w = 0.0f;
    transform.row[2].w = 0.0f;
    transform.row[3].y = object->at_point_.y;
    transform.row[3].w = 1.0f;
    nodes->set_transform(&transform);
}

} // namespace

// 0x0051BA10
memory::owner<ObjectModule*> TowRideHandler::fabricate() { return new TowRideHandler; }

// (the constructor: inlined in fabricate in the original)
TowRideHandler::TowRideHandler() { kind_tag_ = kType; }

// 0x0051B960 (0x0051B980 the deleting one)
TowRideHandler::~TowRideHandler() = default;

// 0x0051B9B0
void TowRideHandler::configure_from_data(script::Struct*) { hooks_ = entity_->fetch_module(kObjectHookManager); }

// 0x0051B9D0
int TowRideHandler::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    if (name != kAllowSkitching)
        return 2;
    allowed_ = !params->has_bare_flag(kOff);
    return 1;
}

// 0x0051BA70
int TowRideHandler::get_nearest_hook_point(maths::Vec4* out, const maths::Vec4* target)
{
    int nearest = -1;
    GameEntity* object = entity_;
    if (!hooks_ || !object)
        return nearest;
    ClassNodeList* nodes = hook_nodes(hooks_);
    if (!nodes)
        return nearest;
    place_nodes(nodes, object);
    auto nearest_distance = std::bit_cast<float>(0x56B5E621u); // (far enough)
    maths::Vec4 nearest_point = {0.0f, 0.0f, 0.0f, 1.0f};
    int index = 0;
    for (const ClassNodeList::TableRow* node = nodes->entries_; node; node = node->next, ++index) {
        maths::Vec4 point;
        nodes->xform(&point, &node->pos);
        const maths::Vec4& pos = object->at_point_;
        point.x -= pos.x;
        point.y -= pos.y;
        point.z -= pos.z;
        point.w -= pos.w;
        point.flatten_onto_plane(object->basis_frame_.row[1]);
        point.x += pos.x;
        point.y += pos.y;
        point.z += pos.z;
        point.w += pos.w;
        const double dx = static_cast<double>(point.x) - target->x;
        const double dy = static_cast<double>(point.y) - target->y;
        const double dz = static_cast<double>(point.z) - target->z;
        const double distance = (dz * dz + dy * dy) + dx * dx;
        if (distance < nearest_distance) {
            nearest_distance = static_cast<float>(distance);
            nearest_point = point;
            nearest = index;
        }
    }
    *out = nearest_point;
    return nearest;
}

// 0x0051BC70
bool TowRideHandler::get_hook_point(maths::Vec4* out, int index)
{
    GameEntity* object = entity_;
    if (!hooks_ || !object)
        return false;
    ClassNodeList* nodes = hook_nodes(hooks_);
    if (!nodes)
        return false;
    place_nodes(nodes, object);
    int i = 0;
    for (const ClassNodeList::TableRow* node = nodes->entries_; node; node = node->next, ++i) {
        if (i != index)
            continue;
        maths::Vec4 point;
        nodes->xform(&point, &node->pos);
        point -= object->at_point_;
        point.flatten_onto_plane(object->basis_frame_.row[1]);
        point += object->at_point_;
        *out = point;
        return true;
    }
    return true;
}

} // namespace objects
