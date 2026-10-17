#pragma once

#include <cstdint>

#include "game/front/screen_element.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/smtptr.h"

namespace Obj {
class CCompositeObject;
}
namespace Script {
class CStruct;
}

// The front end's 3D model screen element (CreateScreenElement type = Element3d): up to 25 composite objects with
// a model each (model=, models=, CloneModel=, skeletonName=, or level geometry by Sector= / Sectors=), placed in the
// 3D world in front of a viewport's camera so that they appear at the element's place on the 640x480 layout
// (or following an object / a node: ParentParams), turned by angleX/Y/Z (spinning by AngVelX/Y/Z), optionally
// pointed at a node or object, hovering, and scaled with their distance. They are ordinary world objects: drawn by
// the 3D pass with the scene behind the menus (the network menu's backdrop panel UI_bg.mdl is one).
// Vtable 0x00649FF4 (CScreenElement's, with 0 the destructor, 1 SetProperties, 9 SetMorph, 10 Update).
namespace Front {

class CElement3d : public CScreenElement {
public:
    // A model's object and where it sits in the element (w = 1).
    struct Model {
        Obj::CSmtPtr<Obj::CCompositeObject> object;
        Mth::Vector offset;
    };
    static_assert(sizeof(Model) == 0x1C, "the original's slot");
    enum { kMaxModels = 25 };
    // m_pointAt
    enum { kPointAtNothing = 0, kPointAtNode = 1, kPointAtObject = 2 };

    // 0x004A6060: the constructor (returns this): no models, CameraZ -400, the Y X Z angle order, the identity
    // orientation, scale 1, ScaleWithDistance's factor 0.002 up to 3; type Element3d (0x82C59D78).
    CElement3d* Construct();
    // 0x004A6240: the destructor body: the models' objects die (ClearModels), the pointers go, then
    // CScreenElement's.
    void DestroyBody();
    // 0x004A6D00 (vtable slot 0): the scalar deleting destructor.
    CElement3d* Destroy(uint8_t flags);

    // 0x004A6630 (vtable slot 1): CScreenElement's properties, then the models and everything listed above.
    void SetProperties(Script::CStruct* params);
    // 0x004A6BA0 (vtable slot 9): CScreenElement's morph; scale_to_screen_dims fits the models again.
    void SetMorph(Script::CStruct* params);
    // 0x004A6D30 (vtable slot 10): one frame - places the models.
    void Update();

    // 0x004A5E30: every model's object hidden and killed (vtable +0x18); offsets back to (0, 0, 0, 1); none left.
    void ClearModels();
    // 0x004A5E80: each model drawn only in m_viewport (the model's viewport mask, 0x00494970).
    void ApplyViewport();
    // 0x004A6540: a new composite object for the next slot, set up by the scripts 0xDF835908 / 0xFA03586F with
    // `params` around its Finalize, at `offset`.
    void AddModel(Script::CStruct* params, Mth::Vector offset);
    // 0x004A62B0 (scale_to_screen_dims): the scale (m_fitScale) and centre that fit the models' bounding box to
    // the element's size at its CameraZ.
    void ScaleToScreenDims();

    Model m_models[kMaxModels];    // +0x178
    int m_numModels;               // +0x434
    float m_cameraZ;               // +0x438 (CameraZ: the distance in front of the camera, negative)
    float m_angle[3];              // +0x43C angleX/Y/Z (radians, kept within one turn)
    float m_angularVelocity[3];    // +0x448 AngVelX/Y/Z (per frame)
    Mth::Matrix m_matrix;          // +0x454 where the element is: the camera's orientation and the place in
                                   // front of it (row 3 at +0x484)
    Mth::Vector m_targetPosition;  // +0x494 NodeToPointTo's node position
    int m_pointAt;                 // +0x4A4 kPointAt*
    int m_angleOrder;              // +0x4A8 AngleOrderXYZ 0, XZY 1, YXZ 2 (default), YZX 3, ZXY 4, ZYX 5
    uint32_t m_targetObject;       // +0x4AC ObjectToPointTo
    float m_hoverAmplitude;        // +0x4B0 HoverParams Amp
    int m_hoverPeriod;             // +0x4B4 ms (1000 / freq)
    Mth::Vector m_pivot;           // +0x4B8 PivotPoint (w = 1)
    Mth::Vector m_parentOffset;    // +0x4C8 ParentParams' vector
    Mth::Vector m_centre;          // +0x4D8 scale_to_screen_dims: minus the bounding box's centre (w = 0)
    float m_fitScale;              // +0x4E8 scale_to_screen_dims' scale (1)
    bool m_scaleWithDistance;      // +0x4EC ScaleWithDistance
    uint8_t m_pad4ED[3];
    float m_distanceScale;         // +0x4F0 the scale per unit of distance (0.002; checksum 0x571F3F3A)
    float m_maxScale;              // +0x4F4 MaxScale (3)
    int m_viewport;                // +0x4F8 active_viewport
    uint32_t m_parentObject;       // +0x4FC ParentParams name= (0x5B8AB877 if it gives that integer)
    int m_parentInteger;           // +0x500 ParentParams' integer 0x5B8AB877
    uint32_t m_parentNode;         // +0x504 ParentParams' node (0x7A8017BA)
    float m_tilt;                  // +0x508 tilt (degrees, when pointing at something)
    bool m_dead;                   // +0x50C its parent object went: element3d_died ran, nothing more
    uint8_t m_pad50D[3];
};

// 0x004A6BD0 (cdecl): `m` turned about the world's axes by the three angles in `order` (the m_angleOrder values;
// others: nothing).
void ApplyAngles(Mth::Matrix* m, float x, float y, float z, int order);

// 0x004A5EE0 (EAX = centre, ESI = out, then cdecl viewport, x, y, z): `out` = where a point at (x, y) on the
// 640x480 layout is, `z` in front of `viewport`'s camera (`centre` added in the camera's frame, w + 1): the
// camera's orientation, translated there (the identity with no camera). The horizontal is scaled by the screen's
// aspect / (4/3), both by 0.9 on back buffers wider than 640. `local` (ours, may be null) gets the point in the
// camera's frame (x right, y up, z backward).
void ScreenToWorld(const Mth::Vector* centre, Mth::Matrix* out, int viewport, float x, float y, float z,
                   Mth::Vector* local = nullptr);
// The register-argument entry point for ScreenToWorld (tests call it).
void ScreenToWorldStub();

// The widescreen front end's rule for Element3d (thug2-decomp.ini [features] widescreen_menus; not in the
// original), given the element's model corners in its own frame (scaled, before the camera's orientation), its
// place in the camera's frame and the tangent of half the camera's horizontal angle: the factor its horizontal
// scale is multiplied by, 1 when unchanged. See element3d.cpp.
float WidescreenStretch(const Mth::Vector* corners, int count, const Mth::Vector& place, double tanHalfAngle);

} // namespace Front
