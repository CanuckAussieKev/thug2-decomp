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
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

extern void** const kVtable;
constexpr uint32_t kObjectHookManager = 0x27443708;
constexpr uint32_t kAllowSkitching = 0xC38CCE3B; // Obj_AllowSkitching
const char* const kOff = "off"; // "off"

// The hook manager's node list (its vtable +0x34), or null.
ClassNodeList* hook_nodes(ObjectModule* hooks)
{
    return reinterpret_cast<ClassNodeList*(__thiscall*)(ObjectModule*)>(hooks->vtable_[13])(hooks);
}

// The nodes' transform: the object's orientation (the w column cleared) at its position.
void place_nodes(ClassNodeList* nodes, GameEntity* object)
{
    maths::Mat4 transform;
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

// 0x0051B970
void ComponentWithoutUpdate::tick_frame() { virtual_suspend(true); }

// 0x0051BA10
ObjectModule* __cdecl TowRideHandler::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(TowRideHandler));
    if (!memory)
        return nullptr;
    auto* skitch = static_cast<TowRideHandler*>(memory);
    skitch->ObjectModule::make_new();
    skitch->vtable_ = kVtable;
    skitch->kind_tag_ = kType;
    skitch->allowed_ = true;
    return skitch;
}

// 0x0051B960
void TowRideHandler::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x0051B980
TowRideHandler* TowRideHandler::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

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
    float nearest_distance = std::bit_cast<float>(0x56B5E621u); // (far enough)
    maths::Vec4 nearest_point = {0.0f, 0.0f, 0.0f, 1.0f};
    int index = 0;
    for (ClassNodeList::TableRow* node = nodes->entries_; node; node = node->next, ++index) {
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
        double dx = static_cast<double>(point.x) - target->x;
        double dy = static_cast<double>(point.y) - target->y;
        double dz = static_cast<double>(point.z) - target->z;
        double distance = (dz * dz + dy * dy) + dx * dx;
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
    for (ClassNodeList::TableRow* node = nodes->entries_; node; node = node->next, ++i) {
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

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::TowRideHandler::teardown,
         &objects::ComponentWithoutUpdate::tick_frame,
         &objects::TowRideHandler::configure_from_data,
         stubs::do_nothing,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::TowRideHandler::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace Obj
