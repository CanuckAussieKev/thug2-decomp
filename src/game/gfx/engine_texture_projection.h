#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Obj {
class CCompositeObject;
}
namespace Script {
class CStruct;
}

// The engine's texture projection (Nx::CTextureProjection; the PC's CPcTextureProjection - pc_texture_projection.h -
// extends it): a texture projected from an object, along a direction from a point offset from it, through an
// orthographic or perspective matrix onto the sectors whose collision it reaches - set up from a structure, followed
// each frame (the texture projection component's).
namespace Nx {

// CTextureProjection::m_flags
enum : uint32_t {
    kProjectionFlag1 = 0x01,       // the structure's 0xAF06447B
    kProjectionOrthographic = 0x02, // a size given (0x083FDB95)
    kProjectionPerspective = 0x04,  // a field of view given (0x17E7321E; the aspect 0x0F7C3CAA)
};

// The projection (0xC8 bytes; vtable 0x00649C14 - PC 0x0064A764: [0] destroy, [1] projected onto the sectors). It's a
// node of its component's list too (Lst::Node<CTextureProjection>, its own data).
class CTextureProjection {
public:
    // 0x0049FE20: construct - its own list node, no texture, the identity, unit direction w, nothing else set.
    // Returns this.
    CTextureProjection* Construct();
    // 0x004A04A0: the destructor's body (the node unlinked: vtable 0x00646600).
    void DestroyBody();
    // 0x0049FE10 (virtual [1], the engine's stub: "STUB: plat_update..."): the platform's projection.
    void PlatProject(void** sectors, void** collision);

    // 0x004A04C0: set up from a structure: the direction (0xB1DE1E91, normalised), the range (0xDF61A4BD), a size
    // (orthographic) or a field of view and aspect (perspective), 0xDC46BA66, the offset from the object (0xA6F5352F),
    // an angle in degrees (0xFF7EBAF6), the texture (from the particle dictionary), the blend mode (0xDD6BB3D5,
    // "blend"), the fineness (0x12602598), flag 1 (0xAF06447B), two distances (0xE979B3D3 - 100000 -, 0x7FCAEAB1 -
    // 15000); then the matrix made.
    void InitFromStructure(Script::CStruct* params);
    // 0x0049FEF0: the projection matrix - a look-at from the position along the world direction (up y), times an
    // orthographic (size x size) or perspective one (0.1 to the range); unchanged if it's neither.
    void UpdateMatrix();
    // 0x0049FFE0: moved with `object` (its position plus the offset through its display matrix, the direction through
    // it); with one viewport, nothing more when further than 0x7FCAEAB1's distance from the camera or out of its view
    // (a sphere half the range ahead); else the matrix made and the texture projected (slot 1) onto the sectors whose
    // collision - in the grids the line from it along the range reaches - touches that line's box grown by 2.5 sizes.
    void Update(Obj::CCompositeObject* object);

    void** vtable;
    Mem::ListNode* m_nodeData;       // +0x04: itself
    int m_nodePriority;              // +0x08
    Mem::ListNode* m_next;           // +0x0C
    Mem::ListNode* m_prev;           // +0x10
    uint32_t m_name;                 // +0x14: the structure's 0x1EE8B7B9 (the component sets it)
    uint8_t* mp_texture;             // +0x18: an engine CTexture
    uint32_t m_blendMode;            // +0x1C
    Mth::Matrix m_projection;        // +0x20
    float m_size;                    // +0x60
    float m_fov;                     // +0x64
    float m_aspect;                  // +0x68
    uint32_t m_fineness;             // +0x6C
    Mth::Vector m_direction;         // +0x70: the object's (normalised)
    Mth::Vector m_worldDirection;    // +0x80
    Mth::Vector m_position;          // +0x90: in the world
    float m_range;                   // +0xA0
    uint32_t m_unknownA4;            // +0xA4: the structure's 0xDC46BA66
    Mth::Vector m_offset;            // +0xA8: from the object, in its space
    float m_angle;                   // +0xB8: radians
    float m_unknownBC;               // +0xBC: 0xE979B3D3 (100000)
    float m_viewDistance;            // +0xC0: 0x7FCAEAB1 (15000)
    uint32_t m_flags;                // +0xC4
};
static_assert(offsetof(CTextureProjection, m_projection) == 0x20 && offsetof(CTextureProjection, m_fineness) == 0x6C &&
                  offsetof(CTextureProjection, m_position) == 0x90 && offsetof(CTextureProjection, m_offset) == 0xA8 &&
                  offsetof(CTextureProjection, m_flags) == 0xC4 && sizeof(CTextureProjection) == 0xC8,
              "layout must match the original");

} // namespace Nx
