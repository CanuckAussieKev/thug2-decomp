#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Gfx {
class Camera;
}

// The engine's shadows (Nx): what an object's shadow component makes (obj/shadow_component.h) - detailed (type 1: the
// model drawn into a 256 x 256 texture from a camera along the light, projected onto the ground), simple (type 2: a
// shadow model - "Ped_Shadow" - laid on the ground) or a volume (type 3: the model's silhouette edges, shared by every
// shadow of the same model - 0x00701418 - and extruded along the light).
//
// The vtable: [0] the scalar deleting destructor; [1] Update(position, matrix, ground normal by value); [2]
// SetPosition(by value); [3] SetLightDirection; [4] SetMatrix; [5] Hide; [6] Show; [7] SetModel(model, asset data).
namespace Nx {

class CModel;

// The base (8 bytes, vtable 0x00649E78).
class CShadow {
public:
    // 0x004A16C0: type 0. Returns this.
    CShadow* Construct();
    // 0x004A16D0: the destructor's body.
    void DestroyBody();
    // 0x004A18B0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CShadow* Destroy(unsigned int freeFlags);
    // 0x004A18A0 (virtual [1] of the base, the detailed shadow and the volume): nothing.
    void Update(const Mth::Vector* position, const Mth::Matrix* matrix, Mth::Vector normal);

    void** m_vtable;
    int m_type; // +0x04 1 detailed, 2 simple, 3 volume
};

// The detailed shadow (0x24 bytes, vtable 0x00649E98).
class CDetailedShadow : public CShadow {
public:
    // 0x004A19C0: a 256 x 256 render target (depth 16) the model's shadow caster draws into; the light's direction
    // (normalised) and the camera's distance; the camera made (at the origin, looking along the light) and the model
    // told it casts a shadow. Returns this.
    CDetailedShadow* Construct(CModel* model, Mth::Vector direction, float distance);
    // 0x004A16E0: the caster removed, the render target and the camera deleted; the base's body.
    void DestroyBody();
    // 0x004A18E0 (virtual [0])
    CDetailedShadow* Destroy(unsigned int freeFlags);
    // 0x004A1910 (virtual [2]): the camera put `distance` back along the light from `position`, the caster aimed.
    void SetPosition(Mth::Vector position);
    // 0x004A1BA0 (virtual [3]): the light's direction (normalised) and the camera turned to look along it (its
    // position kept); the caster aimed.
    void SetLightDirection(const Mth::Vector* direction);

    Gfx::Camera* mp_camera;  // +0x08
    uint8_t* mp_texture;     // +0x0C the render target
    Mth::Vector m_direction; // +0x10 the light's
    float m_distance;        // +0x20
};
static_assert(offsetof(CDetailedShadow, m_distance) == 0x20 && sizeof(CDetailedShadow) == 0x24,
              "layout must match the original");

// The simple shadow (0x14 bytes, vtable 0x00649EB8).
class CSimpleShadow : public CShadow {
public:
    // 0x004A1750: no model yet, scale and offset 1. Returns this.
    CSimpleShadow* Construct();
    // 0x004A1780: the model deleted; the base's body.
    void DestroyBody();
    // 0x004A1990 (virtual [0])
    CSimpleShadow* Destroy(unsigned int freeFlags);
    // 0x004A17E0: a new model (the old one deleted) loaded from "<name>.mdl".
    void SetModel(const char* name);
    // 0x004A1CD0 (virtual [1]): the model drawn lying on the ground: its rows (the normal x the matrix's third row,
    // the normal, the matrix's third row) scaled, at `position` raised along the normal by the offset.
    void Update(const Mth::Vector* position, const Mth::Matrix* matrix, Mth::Vector normal);
    // 0x004A1880 / 0x004A1890 (virtual [5] / [6]): the model made inactive / active.
    void Hide();
    void Show();

    CModel* mp_model; // +0x08
    float m_scale;    // +0x0C
    float m_offset;   // +0x10
};
static_assert(sizeof(CSimpleShadow) == 0x14, "layout must match the original");

// A model's silhouette edges (0x10 bytes; in the table at 0x00701418 by the model's asset data, shared).
struct ShadowEdges {
    uint8_t* edges; // 0x50 bytes each
    int count;      // +0x04
    int lastMesh;   // +0x08 the last mesh read
    int users;      // +0x0C
};

// The shadow volume (0x68 bytes, vtable 0x00649ED8).
class CShadowVolume : public CShadow {
public:
    // 0x004A1F80: as the detailed shadow's camera (no texture); no model or edges yet. Returns this.
    CShadowVolume* Construct(CModel* model, Mth::Vector direction, float distance);
    // 0x004A2120: the camera deleted; with edges (and some), taken out of the volumes' list and released.
    void DestroyBody();
    // 0x004A22C0 (virtual [0])
    CShadowVolume* Destroy(unsigned int freeFlags);
    // 0x004A1E70 (virtual [2])
    void SetPosition(Mth::Vector position);
    // 0x004A21A0 (virtual [3])
    void SetLightDirection(const Mth::Vector* direction);
    // 0x004A1EE0 (virtual [4]): a copy of the object's matrix (a jump to Mth::Matrix::operator=).
    Mth::Matrix* SetMatrix(const Mth::Matrix* matrix);
    // 0x004A22F0 (virtual [7]): the edges of `assetData`'s meshes (by the asset data's +0x8C - the key: none, the one
    // kept) found or built, and this volume put in the volumes' list.
    void SetModel(CModel* model, uint8_t* assetData);

    Gfx::Camera* mp_camera;  // +0x08
    Mth::Matrix m_matrix;    // +0x0C
    uint32_t m_modelKey;     // +0x4C the asset data's meshes
    Mth::Vector m_direction; // +0x50
    ShadowEdges* mp_edges;   // +0x60
    float m_distance;        // +0x64
};
static_assert(offsetof(CShadowVolume, m_modelKey) == 0x4C && offsetof(CShadowVolume, mp_edges) == 0x60 &&
                  sizeof(CShadowVolume) == 0x68,
              "layout must match the original");

// 0x004A1E40: a volume taken out of the volumes drawn (0x007013D8, 16 slots).
void __cdecl RemoveShadowVolume(CShadowVolume* volume);
// 0x004A1EF0: the edges kept under `key` (null: none).
ShadowEdges* __cdecl FindShadowEdges(uint32_t key);
// 0x004A1F30: one user fewer of the edges under `key`; with none left they're freed and taken out of the table.
void __cdecl ReleaseShadowEdges(uint32_t key);

} // namespace Nx
