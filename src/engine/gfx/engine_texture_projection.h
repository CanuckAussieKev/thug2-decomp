#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace objects {
class GameEntity;
}
namespace script {
class Struct;
}

// The engine's texture projection (render::TextureProjection; the PC's PcTextureProjection - pc_texture_projection.h -
// extends it): a texture projected from an object, along a direction from a point offset from it, through an
// orthographic or perspective matrix onto the sectors whose collision it reaches - set up from a structure, followed
// each frame (the texture projection component's).
namespace render {

// TextureProjection::flag_bits_
enum : uint32_t {
    kProjectionFlag1 = 0x01,        // the structure's 0xAF06447B
    kProjectionOrthographic = 0x02, // a size given (0x083FDB95)
    kProjectionPerspective = 0x04,  // a field of view given (0x17E7321E; the aspect 0x0F7C3CAA)
};

// The projection (0xC8 bytes; vtable 0x00649C14 - PC 0x0064A764: [0] destroy, [1] projected onto the sectors). It's a
// node of its component's list too (lists::Elem<TextureProjection>, data = the projection: the original's projection
// was its own node, the node's table word its vtable; here the node follows the vptr, in the original's place).
class TextureProjection : public memory::HeapObject {
public:
    // 0x0049FE20: its own list node, no texture, the identity, unit direction w, nothing else set.
    TextureProjection();
    // 0x004A04A0: the node unlinked.
    virtual ~TextureProjection();
    TextureProjection(const TextureProjection&) = delete;
    TextureProjection& operator=(const TextureProjection&) = delete;
    // 0x0049FE10 (slot 1, the engine's stub: "STUB: plat_update..."): the platform's projection onto the sectors (a
    // null-terminated array) and their collision objects.
    virtual void plat_project(void** sectors, void** collision);

    // 0x004A04C0: set up from a structure: the direction (0xB1DE1E91, normalised), the range (0xDF61A4BD), a size
    // (orthographic) or a field of view and aspect (perspective), 0xDC46BA66, the offset from the object (0xA6F5352F),
    // an angle in degrees (0xFF7EBAF6), the texture (from the particle dictionary), the blend mode (0xDD6BB3D5,
    // "blend"), the fineness (0x12602598), flag 1 (0xAF06447B), two distances (0xE979B3D3 - 100000 -, 0x7FCAEAB1 -
    // 15000); then the matrix made.
    void configure_from_data(script::Struct* params);
    // 0x0049FEF0: the projection matrix - a look-at from the position along the world direction (up y), times an
    // orthographic (size x size) or perspective one (0.1 to the range); unchanged if it's neither.
    void update_matrix();
    // 0x0049FFE0: moved with `object` (its position plus the offset through its display matrix, the direction through
    // it); with one viewport, nothing more when further than 0x7FCAEAB1's distance from the camera or out of its view
    // (a sphere half the range ahead); else the matrix made and the texture projected (slot 1) onto the sectors whose
    // collision - in the grids the line from it along the range reaches - touches that line's box grown by 2.5 sizes.
    void tick_frame(objects::GameEntity* object);

    memory::ListNode list_link_;  // +0x04 in its component's list (data = this)
    uint32_t label_;              // +0x14: the structure's 0x1EE8B7B9 (the component sets it)
    uint8_t* bitmap_;             // +0x18: an engine BitmapTexture
    uint32_t blend_mode_;         // +0x1C
    maths::Mat4 projection_;      // +0x20
    float extent_;                // +0x60
    float field_of_view_;         // +0x64
    float aspect_;                // +0x68
    uint32_t fineness_;           // +0x6C
    maths::Vec4 direction_;       // +0x70: the object's (normalised)
    maths::Vec4 world_direction_; // +0x80
    maths::Vec4 anchor_point_;    // +0x90: in the world
    float range_;                 // +0xA0
    uint32_t unknowna4_;          // +0xA4: the structure's 0xDC46BA66
    maths::Vec4 shift_;           // +0xA8: from the object, in its space
    float angle_;                 // +0xB8: radians
    float unknownbc_;             // +0xBC: 0xE979B3D3 (100000)
    float view_distance_;         // +0xC0: 0x7FCAEAB1 (15000)
    uint32_t flag_bits_;          // +0xC4
};
static_assert(offsetof(TextureProjection, list_link_) == 0x04 && offsetof(TextureProjection, projection_) == 0x20 &&
                  offsetof(TextureProjection, fineness_) == 0x6C &&
                  offsetof(TextureProjection, anchor_point_) == 0x90 && offsetof(TextureProjection, shift_) == 0xA8 &&
                  offsetof(TextureProjection, flag_bits_) == 0xC4 && sizeof(TextureProjection) == 0xC8,
              "layout must match the original");

} // namespace render
