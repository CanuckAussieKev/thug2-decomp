#pragma once

#include <cstdint>

#include "frontend/screen_element.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/smtptr.h"

namespace objects {
class GameEntity;
}
namespace script {
class Struct;
}

// The front end's 3D model screen element (CreateScreenElement type = Element3d): up to 25 composite objects with
// a model each (model=, models=, CloneModel=, skeletonName=, or level geometry by Sector= / Sectors=), placed in the
// 3D world in front of a viewport's camera so that they appear at the element's place on the 640x480 layout
// (or following an object / a node: ParentParams), turned by angleX/Y/Z (spinning by AngVelX/Y/Z), optionally
// pointed at a node or object, hovering, and scaled with their distance. They are ordinary world objects: drawn by
// the 3D pass with the scene behind the menus (the network menu's backdrop panel UI_bg.mdl is one).
// Vtable 0x00649FF4 (UiWidget's, with 0 the destructor, 1 change_traits, 9 blend_towards, 10 tick_frame).
namespace frontend {

class ModelWidget final : public UiWidget {
public:
    // A model's object and where it sits in the element (w = 1).
    struct ShapeMesh {
        objects::SharedRef<objects::GameEntity> object;
        maths::Vec4 offset;
    };
    enum { kMaxModels = 25 };
    // point_at_
    enum { kPointAtNothing = 0, kPointAtNode = 1, kPointAtObject = 2 };

    // 0x004A6060: no models, CameraZ -400, the Y X Z angle order, the identity
    // orientation, scale 1, ScaleWithDistance's factor 0.002 up to 3; type Element3d (0x82C59D78).
    ModelWidget();
    // 0x004A6240 (0x004A6D00 the deleting one): the models' objects die (clear_models), the pointers go, then
    // UiWidget's.
    ~ModelWidget() override;

    // 0x004A6630 (vtable slot 1): UiWidget's properties, then the models and everything listed above.
    void change_traits(script::Struct* params) override;
    // 0x004A6BA0 (vtable slot 9): UiWidget's morph; scale_to_screen_dims fits the models again.
    void blend_towards(script::Struct* params) override;
    // 0x004A6D30 (vtable slot 10): one frame - places the models.
    void tick_frame() override;

    // 0x004A5E30: every model's object hidden and killed (vtable +0x18); offsets back to (0, 0, 0, 1); none left.
    void clear_models();
    // 0x004A5E80: each model drawn only in view_slot_ (the model's viewport mask, 0x00494970).
    void apply_viewport();
    // 0x004A6540: a new composite object for the next slot, set up by the scripts 0xDF835908 / 0xFA03586F with
    // `params` around its Finalize, at `offset`.
    void attach_model(script::Struct* params, maths::Vec4 offset);
    // 0x004A62B0 (scale_to_screen_dims): the scale (fit_scale_) and centre that fit the models' bounding box to
    // the element's size at its CameraZ.
    void scale_to_screen_dims();

    ShapeMesh models_[kMaxModels]; // +0x178
    int model_total_;              // +0x434
    float camera_z_;               // +0x438 (CameraZ: the distance in front of the camera, negative)
    float angle_[3];               // +0x43C angleX/Y/Z (radians, kept within one turn)
    float angular_velocity_[3];    // +0x448 AngVelX/Y/Z (per frame)
    maths::Mat4 basis_frame_;      // +0x454 where the element is: the camera's orientation and the place in
                                   // front of it (row 3 at +0x484)
    maths::Vec4 target_position_;  // +0x494 NodeToPointTo's node position
    int point_at_;                 // +0x4A4 kPointAt*
    int euler_sequence_;           // +0x4A8 AngleOrderXYZ 0, kXzy 1, YXZ 2 (default), YZX 3, ZXY 4, kZyx 5
    uint32_t target_object_;       // +0x4AC ObjectToPointTo
    float bob_height_;             // +0x4B0 HoverParams Amp
    int bob_period_;               // +0x4B4 ms (1000 / freq)
    maths::Vec4 pivot_;            // +0x4B8 PivotPoint (w = 1)
    maths::Vec4 anchor_shift_;     // +0x4C8 ParentParams' vector
    maths::Vec4 centre_;           // +0x4D8 scale_to_screen_dims: minus the bounding box's centre (w = 0)
    float fit_scale_;              // +0x4E8 scale_to_screen_dims' scale (1)
    bool size_follows_range_;      // +0x4EC ScaleWithDistance
    float distance_scale_;         // +0x4F0 the scale per unit of distance (0.002; checksum 0x571F3F3A)
    float max_scale_;              // +0x4F4 MaxScale (3)
    int view_slot_;                // +0x4F8 active_viewport
    uint32_t anchor_object_;       // +0x4FC ParentParams name= (0x5B8AB877 if it gives that integer)
    int parent_integer_;           // +0x500 ParentParams' integer 0x5B8AB877
    uint32_t attach_to_name_;      // +0x504 ParentParams' node (0x7A8017BA)
    float lean_angle_;             // +0x508 tilt (degrees, when pointing at something)
    bool is_dead_;                 // +0x50C its parent object went: element3d_died ran, nothing more
};

// 0x004A6BD0 (cdecl): `m` turned about the world's axes by the three angles in `order` (the euler_sequence_ values;
// others: nothing).
void apply_angles(maths::Mat4* m, float x, float y, float z, int order);

// 0x004A5EE0: `out` = where a point at (x, y) on the
// 640x480 layout is, `z` in front of `viewport`'s camera (`centre` added in the camera's frame, w + 1): the
// camera's orientation, translated there (the identity with no camera). The horizontal is scaled by the screen's
// aspect / (4/3), both by 0.9 on back buffers wider than 640. `local` (ours, may be null) gets the point in the
// camera's frame (x right, y up, z backward).
void screen_to_world(const maths::Vec4* centre, maths::Mat4* out, int viewport, float x, float y, float z,
                     maths::Vec4* local = nullptr);

// The widescreen front end's rule for Element3d (thug2-decomp.ini [features] widescreen_menus; not in the
// original), given the element's model corners in its own frame (scaled, before the camera's orientation), its
// place in the camera's frame and the tangent of half the camera's horizontal angle: the factor its horizontal
// scale is multiplied by, 1 when unchanged. See model_widget.cpp.
float widescreen_stretch(const maths::Vec4* corners, int count, const maths::Vec4& place, double tan_half_angle);

} // namespace frontend
