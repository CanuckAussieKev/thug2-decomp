#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/core/mem.h"
#include "engine/lists/key_map.h"
#include "engine/lists/object_set.h"
#include <gsl/pointers>

namespace gfx {
class ViewCam;
}

// The engine's shadows (Nx): what an object's shadow component makes (obj/shadow_component.h) - detailed (type 1: the
// model drawn into a 256 x 256 texture from a camera along the light, projected onto the ground), simple (type 2: a
// shadow model - "Ped_Shadow" - laid on the ground) or a volume (type 3: the model's silhouette edges, shared by every
// shadow of the same model - 0x00701418 - and extruded along the light).
//
// Each kind overrides the virtual functions it needs; the base's do nothing (the original's table at 0x00649E78 has
// shared stubs there).
namespace render {

class RenderModel;
class BitmapTexture;
class SubMesh;
struct TriMeshSource;

// The base (8 bytes, vtable 0x00649E78).
class ShadowBase : public memory::HeapObject {
public:
    // 0x004A16C0: the shadow's kind (1 detailed, 2 simple, 3 volume).
    explicit ShadowBase(int kind) : kind_tag_(kind) {}
    // 0x004A16D0 / 0x004A18B0 (the scalar deleting destructor)
    virtual ~ShadowBase() = default;
    ShadowBase(const ShadowBase&) = delete;
    ShadowBase& operator=(const ShadowBase&) = delete;

    // 0x004A18A0 (virtual [1] of the base, the detailed shadow and the volume): the shadow placed at `position` on
    // the ground (normal by value) - nothing for the base.
    virtual void tick_frame(const maths::Vec4* /*position*/, const maths::Mat4* /*matrix*/, maths::Vec4 /*normal*/) {}
    // [2]: the shadow's position (the object's, or above it).
    virtual void change_place(maths::Vec4 /*position*/) {}
    // [3]: the light's direction.
    virtual void set_light_direction(const maths::Vec4* /*direction*/) {}
    // [4]: the object's matrix.
    virtual void change_mat(const maths::Mat4* /*matrix*/) {}
    // [5] / [6]: hidden / shown.
    virtual void hide() {}
    virtual void show() {}
    // [7]: the model it's the shadow of, and the model's asset data.
    virtual void change_mesh(RenderModel* /*model*/, const SubMesh* /*asset_data*/) {}

    int kind_tag_; // +0x04 1 detailed, 2 simple, 3 volume
};

// The detailed shadow (0x24 bytes, vtable 0x00649E98).
class FullShadow final : public ShadowBase {
public:
    FullShadow(const FullShadow&) = delete;
    FullShadow& operator=(const FullShadow&) = delete;
    // 0x004A19C0: a 256 x 256 render target (depth 16) the model's shadow caster draws into; the light's direction
    // (normalised) and the camera's distance; the camera made (at the origin, looking along the light) and the model
    // told it casts a shadow.
    FullShadow(RenderModel* model, maths::Vec4 direction, float distance);
    // 0x004A16E0 / 0x004A18E0: the caster removed, the render target and the camera deleted.
    ~FullShadow() override;
    // 0x004A1910 (virtual [2]): the camera put `distance` back along the light from `position`, the caster aimed.
    void change_place(maths::Vec4 position) override;
    // 0x004A1BA0 (virtual [3]): the light's direction (normalised) and the camera turned to look along it (its
    // position kept); the caster aimed.
    void set_light_direction(const maths::Vec4* direction) override;

    std::unique_ptr<gfx::ViewCam> view_camera_; // +0x08
    BitmapTexture* bitmap_;                     // +0x0C the render target
    maths::Vec4 direction_{};                   // +0x10 the light's
    float reach_;                               // +0x20
};
static_assert(offsetof(FullShadow, reach_) == 0x20 && sizeof(FullShadow) == 0x24, "the original's size");

// The simple shadow (0x14 bytes, vtable 0x00649EB8).
class BlobShadow final : public ShadowBase {
public:
    BlobShadow(const BlobShadow&) = delete;
    BlobShadow& operator=(const BlobShadow&) = delete;
    // 0x004A1750: no model yet, scale and offset 1.
    BlobShadow();
    // 0x004A1780 / 0x004A1990: the model deleted.
    ~BlobShadow() override;
    // 0x004A17E0: a new model (the old one deleted) loaded from "<name>.mdl".
    void load_mesh(const char* name);
    // 0x004A1CD0 (virtual [1]): the model drawn lying on the ground: its rows (the normal x the matrix's third row,
    // the normal, the matrix's third row) scaled, at `position` raised along the normal by the offset.
    void tick_frame(const maths::Vec4* position, const maths::Mat4* matrix, maths::Vec4 normal) override;
    // 0x004A1880 / 0x004A1890 (virtual [5] / [6]): the model made inactive / active.
    void hide() override;
    void show() override;

    gsl::owner<RenderModel*> mesh_instance_{nullptr}; // +0x08
    float size_factor_{1.0f};             // +0x0C
    float shift_{1.0f};                   // +0x10
};
static_assert(sizeof(BlobShadow) == 0x14, "the original's size");

// An edge of a model's silhouette: its two ends as each of the two faces sharing it sees them - [0] / [2] the first face's
// ends, [1] / [3] the second's (set when it's found) - each with the face's normal.
struct EdgeCorner {
    uint16_t vertex;
    uint16_t pad; // (stack garbage in the original's records: zeroed here)
    maths::Vec4 normal;
};
struct ShadowSilhouetteEdge {
    std::array<EdgeCorner, 4> corners;
};

// A model's silhouette edges (in the table at 0x00701418 by the model's asset data, shared).
struct ShadowEdges {
    ShadowSilhouetteEdge* edges;
    int count;      // +0x04
    int last_mesh_; // +0x08 the last mesh read
    int users;      // +0x0C
    const TriMeshSource* key; // the model's collision meshes they were made from (the table's key)
};

// The shadow volume (0x68 bytes, vtable 0x00649ED8).
class ShadowVolume final : public ShadowBase {
public:
    ShadowVolume(const ShadowVolume&) = delete;
    ShadowVolume& operator=(const ShadowVolume&) = delete;
    // 0x004A1F80: as the detailed shadow's camera (no texture); no model or edges yet.
    ShadowVolume(RenderModel* model, maths::Vec4 direction, float distance);
    // 0x004A2120 / 0x004A22C0: the camera deleted; with edges (and some), taken out of the volumes' list and released.
    ~ShadowVolume() override;
    // 0x004A1E70 (virtual [2])
    void change_place(maths::Vec4 position) override;
    // 0x004A21A0 (virtual [3])
    void set_light_direction(const maths::Vec4* direction) override;
    // 0x004A1EE0 (virtual [4]): a copy of the object's matrix (a jump to maths::Mat4::operator=).
    void change_mat(const maths::Mat4* matrix) override;
    // 0x004A22F0 (virtual [7]): the edges of `asset_data`'s meshes (by the asset data's +0x8C - the key: none, the one
    // kept) found or built, and this volume put in the volumes' list.
    void change_mesh(RenderModel* model, const SubMesh* asset_data) override;

    std::unique_ptr<gfx::ViewCam> view_camera_; // +0x08
    maths::Mat4 basis_frame_{};                 // +0x0C
    const TriMeshSource* model_key_;            // +0x4C the asset data's collision meshes
    maths::Vec4 direction_{};                   // +0x50
    ShadowEdges* edges_;                        // +0x60
    float reach_;                               // +0x64
};
static_assert(offsetof(ShadowVolume, model_key_) == 0x4C && offsetof(ShadowVolume, edges_) == 0x60 &&
                  sizeof(ShadowVolume) == 0x68,
              "the original's size");

// 0x004A1E40: a volume taken out of the volumes drawn (0x007013D8, 16 slots).
void remove_shadow_volume(ShadowVolume* volume);
// 0x004A1EF0: the edges kept under `key` (null: none).
ShadowEdges* find_shadow_edges(const TriMeshSource* key);
// 0x004A1F30: one user fewer of the edges under `key`; with none left they're freed and taken out of the table.
void release_shadow_edges(const TriMeshSource* key);

} // namespace render
