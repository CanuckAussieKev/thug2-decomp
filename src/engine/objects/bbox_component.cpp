// The bounding box component (GameObjects::CbBoxComponent): Obj_ObjectInRadius, Obj_ObjectInRadiusOfNode and
// Obj_ObjectInRect. From the disassembly of 0x0041A810-0x0041B274. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "engine/objects/bbox_component.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/entity_hub.h"

namespace objects {

namespace {

constexpr uint32_t kObjObjectInRect = 0x6520B902;         // Obj_ObjectInRect
constexpr uint32_t kObjObjectInRadius = 0x67660FE5;       // Obj_ObjectInRadius
constexpr uint32_t kObjObjectInRadiusOfNode = 0xDCE760A1; // Obj_ObjectInRadiusOfNode
constexpr uint32_t kOffset = 0xA6F5352F;                  // offset
constexpr uint32_t kAngle = 0xFF7EBAF6;                   // angle
constexpr uint32_t kHeight = 0x0AB21AF0;                  // height
constexpr uint32_t kDist = 0x7E832F08;                    // dist
constexpr uint32_t kWidth = 0x73E5BAD0;                   // width
constexpr uint32_t kRadius = 0xC48391A5;                  // radius
constexpr uint32_t kTypeParam = 0x7321A8D6;               // type
constexpr uint32_t kNode = 0x7A8017BA;                    // node
// type's values and the object types they stand for
constexpr uint32_t kSkater = 0x5B8AB877;  // skater
constexpr uint32_t kPed = 0x061A741E;     // ped
constexpr uint32_t kGameObj = 0x3B5737A6; // gameobj
constexpr uint32_t kCar = 0x88C21962;     // car
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;
constexpr uint32_t kObjectTypeGameObj = 0xBC0D91D8;
constexpr uint32_t kObjectTypeCar = 0x5C0312EB;

constexpr float kTwelveInchScale = 12.0f;          // 0x0064546C
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kHalf = 0.5f;                      // 0x006453BC

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// A manager's object list.
memory::ListNode* manager_object_list(ObjectHubBase* manager) { return manager->fetch_object_set(); }

// Whether `object` is nearer than the square root of `reach_squared` to `point` (or to the position of `self`'s object).
// The squares summed z, y, then x on the FPU stack, compared unrounded.
bool is_near(const GameEntity* object, const GameEntity* self, float reach_squared, const maths::Vec4* point)
{
    float x = self->at_point_.x;
    float y = self->at_point_.y;
    float z = self->at_point_.z;
    if (point) {
        x = point->x;
        y = point->y;
        z = point->z;
    }
    double dx = static_cast<double>(object->at_point_.x) - x;
    double dy = static_cast<double>(object->at_point_.y) - y;
    double dz = static_cast<double>(object->at_point_.z) - z;
    return dz * dz + dy * dy + dx * dx < reach_squared;
}

} // namespace

// 0x0041ADC0
ObjectModule* __cdecl CbBoxComponent::fabricate() { return new CbBoxComponent; }

// 0x0041AAD0
CbBoxComponent::CbBoxComponent()
{
    kind_tag_ = kType;
    box_max_ = {10.0f, 10.0f, 10.0f, 1.0f};
    box_min_ = {-10.0f, -10.0f, -10.0f, 1.0f};
    shift_ = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x0041A810 (0x0041A880 the deleting one)
CbBoxComponent::~CbBoxComponent() {}

// 0x0041A950
void CbBoxComponent::init_rect(Rect2* rect, script::Struct* params)
{
    rect->computed_ = false;
    rect->angle_ = 0.0f;
    rect->radius_computed_ = false;
    rect->shift_ = shift_;
    rect->separation_ = 0.0f;
    rect->span_ = 0.0f;
    rect->level_y_ = 0.0f;
    maths::Vec4 offset;
    offset.w = 0.0f; // (x, y and z uninitialised in the original until found)
    if (params->fetch_vec(kOffset, reinterpret_cast<script::VectorValue*>(&offset), 0)) {
        offset.feet_as_inches();
        rect->shift_.x = offset.x + rect->shift_.x;
        rect->shift_.y = offset.y + rect->shift_.y;
        rect->shift_.z = offset.z + rect->shift_.z;
        rect->shift_.w = offset.w + rect->shift_.w;
    }
    if (params->fetch_float(kAngle, &rect->angle_, 0))
        rect->angle_ = rect->angle_ * kRadiansPerDegree;
    if (params->fetch_float(kHeight, &rect->level_y_, 0))
        rect->level_y_ = rect->level_y_ * kTwelveInchScale;
    if (params->fetch_float(kDist, &rect->separation_, 0))
        rect->separation_ = rect->separation_ * kTwelveInchScale;
    if (params->fetch_float(kWidth, &rect->span_, 0))
        rect->span_ = rect->span_ * kTwelveInchScale;
}

// 0x0041AB20
void CbBoxComponent::compute_rect(Rect2* rect)
{
    if (rect->computed_)
        return;
    rect->basis_frame_ = entity_->basis_frame_;
    if (rect->angle_ != 0.0f) {
        maths::Mat4 rotation;
        matrix_multiply(&rect->basis_frame_, &maths::make_yaw_matrix(rotation, rect->angle_), &rect->basis_frame_);
        rect->basis_frame_ = rect->basis_frame_; // (the original copies it onto itself)
    }
    rect->basis_frame_.upend();
    rect->upper_limit_.w = 1.0f;
    rect->upper_limit_.x = rect->span_ * kHalf;
    rect->upper_limit_.y = rect->level_y_ * kHalf;
    rect->upper_limit_.z = rect->separation_;
    rect->lower_limit_.x = -rect->upper_limit_.x;
    rect->lower_limit_.y = -rect->upper_limit_.y;
    rect->lower_limit_.w = -rect->upper_limit_.w;
    rect->lower_limit_.z = 0.0f; // (the original first stores -upper_limit_.z there)
    rect->upper_limit_.x = rect->upper_limit_.x + rect->shift_.x;
    rect->upper_limit_.y = rect->shift_.y + rect->upper_limit_.y;
    rect->upper_limit_.z = rect->shift_.z + rect->upper_limit_.z;
    rect->upper_limit_.w = rect->shift_.w + rect->upper_limit_.w;
    rect->lower_limit_.x = rect->lower_limit_.x + rect->shift_.x;
    rect->lower_limit_.y = rect->shift_.y + rect->lower_limit_.y;
    rect->lower_limit_.z = rect->shift_.z + rect->lower_limit_.z;
    rect->lower_limit_.w = rect->shift_.w + rect->lower_limit_.w;
    rect->computed_ = true;
}

// 0x0041A8B0
double rect_radius_sqr(CbBoxComponent::Rect2* rect)
{
    if (rect->radius_computed_)
        return rect->reach_squared_;
    float size = std::fabs(rect->separation_);
    if (std::fabs(rect->level_y_) > size)
        size = std::fabs(rect->level_y_);
    if (std::fabs(rect->span_) > size)
        size = std::fabs(rect->span_);
    float offset = std::fabs(rect->shift_.z);
    if (std::fabs(rect->shift_.y) > offset)
        offset = std::fabs(rect->shift_.y);
    if (std::fabs(rect->shift_.x) > offset)
        offset = std::fabs(rect->shift_.x);
    rect->radius_computed_ = true;
    double sum = static_cast<double>(size) + offset;
    double square = sum * sum;
    rect->reach_squared_ = static_cast<float>(square);
    return square;
}

// 0x0041AC40
bool CbBoxComponent::is_in_rect(GameEntity* object, Rect2* rect)
{
    float offset_x = rect->shift_.x;
    float offset_y = rect->shift_.y;
    float offset_z = rect->shift_.z;
    compute_rect(rect);
    // From the box's middle (rounded where the original stores: x, the middle's z, the sum).
    const maths::Vec4& self = entity_->at_point_;
    float dx = static_cast<float>((static_cast<double>(offset_x) + object->at_point_.x) - self.x);
    double dy = (static_cast<double>(offset_y) + object->at_point_.y) - self.y;
    float middle_z = static_cast<float>(static_cast<double>(offset_z) + object->at_point_.z);
    double dz = static_cast<double>(middle_z) - self.z;
    float distance_sqr = static_cast<float>(dz * dz + dy * dy + static_cast<double>(dx) * dx);
    if (rect_radius_sqr(rect) < distance_sqr)
        return false;
    // The middle of its box in the box's frame.
    maths::Vec4 point = object->at_point_;
    auto* query = static_cast<CbBoxComponent*>(object->fetch_module(kType));
    point.y = static_cast<float>((static_cast<double>(query->box_max_.y) - query->box_min_.y) * kHalf + point.y);
    point.x = point.x - self.x;
    point.y = point.y - self.y;
    point.z = point.z - self.z;
    point.w = point.w - self.w;
    maths::Vec4 local;
    point = rect->basis_frame_.xform(local, point);
    return maths::point_in_box(&point, &rect->upper_limit_, &rect->lower_limit_);
}

// 0x0041AE20
bool CbBoxComponent::any_skater_near(Rect2* rect, float reach_squared, const maths::Vec4* point)
{
    int count = modules::Skate::sole()->count_riders();
    for (int i = 0; i < count; ++i) {
        GameEntity* skater = modules::Skate::sole()->get_rider_at(i);
        if (skater == entity_)
            continue;
        if (rect) {
            if (is_in_rect(skater, rect))
                return true;
        } else if (is_near(skater, entity_, reach_squared, point)) {
            return true;
        }
    }
    return false;
}

// 0x0041AF10
bool CbBoxComponent::any_object_near(script::Struct* params, script::Script* script, float reach_squared, Rect2* rect,
                                     uint32_t type, const maths::Vec4* point)
{
    (void)params;
    (void)script;
    uint32_t object_type;
    switch (type) {
    case kSkater:
        return any_skater_near(rect, reach_squared, point);
    case kPed:
        object_type = kObjectTypePed;
        break;
    case kGameObj:
        object_type = kObjectTypeGameObj;
        break;
    case kCar:
        object_type = kObjectTypeCar;
        break;
    default:
        return false;
    }
    ObjectSearch search;
    for (auto* object = static_cast<GameEntity*>(
             search.first_item_of_type(manager_object_list(entity_->park_owner_), object_type));
         object; object = static_cast<GameEntity*>(search.next_item_of_type())) {
        if (object == entity_)
            continue;
        if (rect) {
            if (is_in_rect(object, rect))
                return true;
        } else if (is_near(object, entity_, reach_squared, point)) {
            return true;
        }
    }
    return false;
}

// 0x0041B0A0
bool CbBoxComponent::query_objects(script::Struct* params, script::Script* script, Rect2* rect,
                                   const maths::Vec4* point)
{
    float reach_squared = 0.0f;
    if (!rect) {
        if (!params->fetch_float(kRadius, &reach_squared, 0))
            return false;
        double radius = static_cast<double>(reach_squared) * kTwelveInchScale;
        reach_squared = static_cast<float>(radius * radius);
    }
    uint32_t type;
    if (params->read_hash_value(kTypeParam, &type, 0))
        return any_object_near(params, script, reach_squared, rect, type, point);
    script::Array* types = nullptr;
    if (params->fetch_list(kTypeParam, &types, 1)) {
        for (uint32_t i = 0; i < types->extent_; ++i) {
            if (any_object_near(params, script, reach_squared, rect, types->fetch_part(i), point))
                return true;
        }
    }
    return false;
}

// 0x0041B1A0
int CbBoxComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    Rect2 rect;
    maths::Vec4 position;
    switch (name) {
    case kObjObjectInRect:
        init_rect(&rect, params);
        return query_objects(params, script, &rect, nullptr) ? 1 : 0;
    case kObjObjectInRadius:
        return query_objects(params, script, nullptr, nullptr) ? 1 : 0;
    case kObjObjectInRadiusOfNode: {
        uint32_t node;
        if (!params->read_hash_value(kNode, &node, 0))
            return 0;
        level_nodes::fetch_place(node, &position);
        return query_objects(params, script, nullptr, &position) ? 1 : 0;
    }
    default:
        return 2;
    }
}

} // namespace objects
