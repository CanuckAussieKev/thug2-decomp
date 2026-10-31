#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/lists/key_map.h"

namespace gfx {
class ViewCam;
}

// The engine's shadows (Nx): what an object's shadow component makes (obj/shadow_component.h) - detailed (type 1: the
// model drawn into a 256 x 256 texture from a camera along the light, projected onto the ground), simple (type 2: a
// shadow model - "Ped_Shadow" - laid on the ground) or a volume (type 3: the model's silhouette edges, shared by every
// shadow of the same model - 0x00701418 - and extruded along the light).
//
// The vtable: [0] the scalar deleting destructor; [1] tick_frame(position, matrix, ground normal by value); [2]
// change_place(by value); [3] SetLightDirection; [4] change_mat; [5] Hide; [6] Show; [7] change_mesh(model, asset data).
namespace render {

class RenderModel;

// The base (8 bytes, vtable 0x00649E78).
class ShadowBase {
public:
    // 0x004A16C0: type 0. Returns this.
    ShadowBase* make_new();
    // 0x004A16D0: the destructor's body.
    void destroy_body();
    // 0x004A18B0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    ShadowBase* teardown(unsigned int free_flags);
    // 0x004A18A0 (virtual [1] of the base, the detailed shadow and the volume): nothing.
    void tick_frame(const maths::Vec4* position, const maths::Mat4* matrix, maths::Vec4 normal);

    void** vtable_;
    int kind_tag_; // +0x04 1 detailed, 2 simple, 3 volume
};

// The detailed shadow (0x24 bytes, vtable 0x00649E98).
class FullShadow : public ShadowBase {
public:
    // 0x004A19C0: a 256 x 256 render target (depth 16) the model's shadow caster draws into; the light's direction
    // (normalised) and the camera's distance; the camera made (at the origin, looking along the light) and the model
    // told it casts a shadow. Returns this.
    FullShadow* make_new(RenderModel* model, maths::Vec4 direction, float distance);
    // 0x004A16E0: the caster removed, the render target and the camera deleted; the base's body.
    void destroy_body();
    // 0x004A18E0 (virtual [0])
    FullShadow* teardown(unsigned int free_flags);
    // 0x004A1910 (virtual [2]): the camera put `distance` back along the light from `position`, the caster aimed.
    void change_place(maths::Vec4 position);
    // 0x004A1BA0 (virtual [3]): the light's direction (normalised) and the camera turned to look along it (its
    // position kept); the caster aimed.
    void set_light_direction(const maths::Vec4* direction);

    gfx::ViewCam* view_camera_; // +0x08
    uint8_t* bitmap_;           // +0x0C the render target
    maths::Vec4 direction_;     // +0x10 the light's
    float reach_;               // +0x20
};
static_assert(offsetof(FullShadow, reach_) == 0x20 && sizeof(FullShadow) == 0x24, "layout must match the original");

// The simple shadow (0x14 bytes, vtable 0x00649EB8).
class BlobShadow : public ShadowBase {
public:
    // 0x004A1750: no model yet, scale and offset 1. Returns this.
    BlobShadow* make_new();
    // 0x004A1780: the model deleted; the base's body.
    void destroy_body();
    // 0x004A1990 (virtual [0])
    BlobShadow* teardown(unsigned int free_flags);
    // 0x004A17E0: a new model (the old one deleted) loaded from "<name>.mdl".
    void change_mesh(const char* name);
    // 0x004A1CD0 (virtual [1]): the model drawn lying on the ground: its rows (the normal x the matrix's third row,
    // the normal, the matrix's third row) scaled, at `position` raised along the normal by the offset.
    void tick_frame(const maths::Vec4* position, const maths::Mat4* matrix, maths::Vec4 normal);
    // 0x004A1880 / 0x004A1890 (virtual [5] / [6]): the model made inactive / active.
    void conceal();
    void get_show();

    RenderModel* mesh_instance_; // +0x08
    float size_factor_;          // +0x0C
    float shift_;                // +0x10
};
static_assert(sizeof(BlobShadow) == 0x14, "layout must match the original");

// A model's silhouette edges (0x10 bytes; in the table at 0x00701418 by the model's asset data, shared).
struct ShadowEdges {
    uint8_t* edges; // 0x50 bytes each
    int count;      // +0x04
    int last_mesh_; // +0x08 the last mesh read
    int users;      // +0x0C
};

// The shadow volume (0x68 bytes, vtable 0x00649ED8).
class ShadowVolume : public ShadowBase {
public:
    // 0x004A1F80: as the detailed shadow's camera (no texture); no model or edges yet. Returns this.
    ShadowVolume* make_new(RenderModel* model, maths::Vec4 direction, float distance);
    // 0x004A2120: the camera deleted; with edges (and some), taken out of the volumes' list and released.
    void destroy_body();
    // 0x004A22C0 (virtual [0])
    ShadowVolume* teardown(unsigned int free_flags);
    // 0x004A1E70 (virtual [2])
    void change_place(maths::Vec4 position);
    // 0x004A21A0 (virtual [3])
    void set_light_direction(const maths::Vec4* direction);
    // 0x004A1EE0 (virtual [4]): a copy of the object's matrix (a jump to maths::Mat4::operator=).
    maths::Mat4* change_mat(const maths::Mat4* matrix);
    // 0x004A22F0 (virtual [7]): the edges of `asset_data`'s meshes (by the asset data's +0x8C - the key: none, the one
    // kept) found or built, and this volume put in the volumes' list.
    void change_mesh(RenderModel* model, uint8_t* asset_data);

    gfx::ViewCam* view_camera_; // +0x08
    maths::Mat4 basis_frame_;   // +0x0C
    uint32_t model_key_;        // +0x4C the asset data's meshes
    maths::Vec4 direction_;     // +0x50
    ShadowEdges* edges_;        // +0x60
    float reach_;               // +0x64
};
static_assert(offsetof(ShadowVolume, model_key_) == 0x4C && offsetof(ShadowVolume, edges_) == 0x60 &&
                  sizeof(ShadowVolume) == 0x68,
              "layout must match the original");

// 0x004A1E40: a volume taken out of the volumes drawn (0x007013D8, 16 slots).
void __cdecl remove_shadow_volume(ShadowVolume* volume);
// 0x004A1EF0: the edges kept under `key` (null: none).
ShadowEdges* __cdecl find_shadow_edges(uint32_t key);
// 0x004A1F30: one user fewer of the edges under `key`; with none left they're freed and taken out of the table.
void __cdecl release_shadow_edges(uint32_t key);

extern ShadowVolume* g_shadow_volumes[16];
extern lists::KeyMap g_shadow_edge_table; // 0x00701418: the silhouette edges by model

} // namespace render
