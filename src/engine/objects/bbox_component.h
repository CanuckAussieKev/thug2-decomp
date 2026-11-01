#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The bounding box component (type 0xB87C8E43 "bbox", vtable 0x00645DC4, 0x4C bytes; made by 0x0041ADC0; every
// composite object gets one, add_base_components): the member functions Obj_ObjectInRadius,
// Obj_ObjectInRadiusOfNode and Obj_ObjectInRect - whether a skater / ped / car / game object (other than the object
// itself) is within a distance of the object (or of a node), or inside a box turned with the object. Its own box (the
// other objects' boxes give the height of their middle) is set only by the constructor: +-10 about the origin.
//
// Vtable: 0 the scalar deleting destructor, 12 invoke_method_by_name; the rest the base's / shared (1 0x0051B970, 2 / 8
// / 10 0x005BAB90, 3 / 4 / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 11 0x0045D720).
class CbBoxComponent final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0xB87C8E43;

    // The box of Obj_ObjectInRect (a local of invoke_method_by_name; no constructor or vtable).
    struct Rect2 {
        maths::Mat4 basis_frame_; // +0x00 the object's orientation (turned by m_angle about Y), then inverted
        float level_y_;           // +0x40 height (inches)
        float separation_;        // +0x44 dist (inches): forwards
        float span_;              // +0x48 width (inches)
        float angle_;             // +0x4C angle (radians)
        maths::Vec4 upper_limit_; // +0x50 the box in the object's frame: (width / 2, height / 2, dist, 1) + shift_
        maths::Vec4 lower_limit_; // +0x60 (-width / 2, -height / 2, 0, -1) + shift_
        maths::Vec4 shift_;       // +0x70 the component's offset + offset (inches)
        uint8_t unknown80[4];
        bool computed_;        // +0x84 basis_frame_, upper_limit_ and lower_limit_ done (compute_rect)
        bool radius_computed_; // +0x85 reach_squared_ done (rect_radius_sqr)
        uint8_t pad86[2];
        float reach_squared_; // +0x88
    };

    // 0x0041ADC0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> __cdecl fabricate();
    // 0x0041AAD0: its type; the box (10, 10, 10, 1) - (-10, -10, -10, 1), the
    // offset (0, 0, 0, 1).
    CbBoxComponent();
    // 0x0041A810: the base's body.
    ~CbBoxComponent() override;
    // 0x0041B1A0 (vtable slot 12): Obj_ObjectInRect (0x6520B902; a Rect from `params`), Obj_ObjectInRadius
    // (0x67660FE5), Obj_ObjectInRadiusOfNode (0xDCE760A1: about the position of `node` - none: 0): 1 / 0 for whether
    // an object is there (query_objects); anything else 2 (not its own).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x0041A950: `rect` from `params`: offset (feet, added to the component's offset), angle (degrees), height,
    // dist, width (feet; 0 when not given); not computed.
    void init_rect(Rect2* rect, script::Struct* params);
    // 0x0041AB20: once: the matrix (the object's orientation, turned by the angle about Y first when it isn't 0),
    // inverted; the box's corners.
    void compute_rect(Rect2* rect);
    // 0x0041AC40: whether `object` is inside `rect`: within the box's bounding distance of the box's middle, then
    // the middle of its box (its position + half its box's height) brought into the box's frame (point_in_box).
    bool is_in_rect(GameEntity* object, Rect2* rect);
    // 0x0041AE20: whether a skater other than the object is inside `rect`, or (without one) nearer than the square
    // root of `reach_squared` to `point` (none: the object's position).
    bool any_skater_near(Rect2* rect, float reach_squared, const maths::Vec4* point);
    // 0x0041AF10: the same for the objects of `type`: skater (0x5B8AB877, any_skater_near), ped (0x061A741E), gameobj
    // (0x3B5737A6) or car (0x88C21962) - the objects of their object types in the object's manager. Anything else:
    // false. (`params` and `script` aren't used.)
    bool any_object_near(script::Struct* params, script::Script* script, float reach_squared, Rect2* rect,
                         uint32_t type, const maths::Vec4* point);
    // 0x0041B0A0: the query of the three members: without a rect, radius (feet; none: false); type - a checksum or an
    // array of them (any one there: true; neither: false).
    bool query_objects(script::Struct* params, script::Script* script, Rect2* rect, const maths::Vec4* point);

    maths::Vec4 box_max_; // +0x18
    maths::Vec4 box_min_; // +0x28
    maths::Vec4 shift_;   // +0x38
    uint8_t unknown48[4];
};
static_assert(offsetof(CbBoxComponent, box_max_) == 0x18 && offsetof(CbBoxComponent, box_min_) == 0x28 &&
                  offsetof(CbBoxComponent, shift_) == 0x38 && sizeof(CbBoxComponent) == 0x4C,
              "layout must match the original");
static_assert(offsetof(CbBoxComponent::Rect2, level_y_) == 0x40 && offsetof(CbBoxComponent::Rect2, angle_) == 0x4C &&
                  offsetof(CbBoxComponent::Rect2, upper_limit_) == 0x50 &&
                  offsetof(CbBoxComponent::Rect2, shift_) == 0x70 &&
                  offsetof(CbBoxComponent::Rect2, computed_) == 0x84 &&
                  offsetof(CbBoxComponent::Rect2, reach_squared_) == 0x88 && sizeof(CbBoxComponent::Rect2) == 0x8C,
              "layout must match the original");

// 0x0041A8B0 (cdecl): the square of a distance that holds the whole box: (the largest of height, dist and width + the
// largest of the offset's x, y and z, all as magnitudes) squared - computed once (kept as a float; the first call
// returns it unrounded, as the original does on the FPU stack).
double rect_radius_sqr(CbBoxComponent::Rect2* rect);

} // namespace objects
