#pragma once

// NOT ORIGINAL - ray tracing's contract (docs/design/vulkan.md "Ray tracing", tasks R1-R5): what the renderer records
// each frame (R1: render_pc fills it as it walks the scene - the D3D9 path too, it costs little) and what a backend that
// traces rays is given (R2: acceleration structures, R3: the material table, R4 / R5: the effects). API-neutral: no
// Vulkan here. Direct3D 9 has no RayTracing (ray_tracing() is null); Vulkan's (gpu_vulkan) is installed when the device
// has VK_KHR_acceleration_structure + VK_KHR_ray_query and [Graphics] ray_tracing=1.
//
// The work streams meet here: change this file only by adding (a member at a struct's end, a virtual with a default
// body), so the others' branches still build.

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/gpu_handles.h"

namespace render::rt {

using MeshId = uint32_t;     // stable while the mesh lives; 0: none
using MaterialId = uint32_t; // an entry of the material table (R3); 0: none (shaded as untextured white)

// A mesh's triangles where the GPU already has them (the buffers render_pc draws from - nothing copied): positions are
// three floats at `position_offset` in each vertex of `vertex_stride` bytes.
struct Geometry {
    GpuVertexBuffer* vertices = nullptr;
    uint32_t vertex_offset = 0; // bytes into the buffer of the first vertex
    uint32_t vertex_stride = 0;
    uint32_t vertex_count = 0;
    uint32_t position_offset = 0;
    GpuIndexBuffer* indices = nullptr; // null: not indexed
    uint32_t index_offset = 0;         // indices (not bytes) into the buffer of the first one
    uint32_t index_count = 0;
    bool indices_32bit = false;
    bool strip = false;   // a triangle strip (the backend makes a list of it), else a list
    bool dynamic = false; // rewritten by the CPU (skinned, deformed): mesh_changed() when it is
    // (added by R1) The buffers' sizes in bytes as far as the renderer knows them (0: not known) - at least this many:
    // a share of a larger buffer gives the end of its own part - and the indices' copy in memory where the renderer
    // keeps one (16- or 32-bit as indices_32bit, from index_offset; null: none), valid while the mesh is registered.
    uint32_t vertex_buffer_bytes = 0;
    uint32_t index_buffer_bytes = 0;
    const void* cpu_indices = nullptr;
    // (added by R5's reflections) Where a hit shader finds a vertex's other attributes, in bytes from the vertex's start:
    // its first texture coordinates (two floats) and its diffuse colour (Direct3D's A8R8G8B8 - 128 is 1 for the
    // game's passes); a format of kAttributeNone: the mesh has none.
    uint32_t uv_offset = 0;
    uint32_t uv_format = 0; // kAttributeNone or kUvFloat2
    uint32_t colour_offset = 0;
    uint32_t colour_format = 0; // kAttributeNone or kColourA8R8G8B8
};
inline constexpr uint32_t kAttributeNone = 0;
inline constexpr uint32_t kUvFloat2 = 1;
inline constexpr uint32_t kColourA8R8G8B8 = 1;

enum InstanceFlag : uint32_t {
    kOpaque = 1U << 0,      // no alpha test or blend: any hit ends a shadow ray
    kAlphaTested = 1U << 1, // the material's alpha decides a hit (R3)
    kCastsShadow = 1U << 2,
    kSkinned = 1U << 3,
    kWorld = 1U << 4,       // the level's own geometry (sectors)
    kObject = 1U << 5,      // a moving object, ped, vehicle, the skater
    kTransparent = 1U << 6, // blended: seen through, casts no shadow by default
};

// One draw of a mesh this frame.
struct Instance {
    MeshId mesh = 0;
    MaterialId material = 0;
    uint32_t flags = 0;
    std::array<std::array<float, 4>, 3> object_to_world{}; // rows: x' = row0 . (x, y, z, 1)
};

struct Light {
    enum class Kind : uint8_t { kAmbient, kDirectional, kPoint };
    Kind kind = Kind::kAmbient;
    std::array<float, 3> colour{};
    std::array<float, 3> direction{}; // directional: the way the light travels (normalised)
    std::array<float, 3> position{};  // point
    float radius = 0.0F;              // point
};

struct Camera {
    uint32_t viewport = 0;
    std::array<std::array<float, 4>, 4> view{};       // D3D's row-vector layout, as GpuTransform's matrices
    std::array<std::array<float, 4>, 4> projection{};
    std::array<float, 3> position{};
};

inline constexpr uint32_t kNoEnvironmentPass = 0xFFFFFFFFU; // (Material::environment_pass: none)

// A surface as a hit shader reads it (R3): its first pass's texture and how it is used.
struct Material {
    GpuTexture* texture = nullptr; // null: untextured
    uint32_t uv_set = 0;
    uint32_t blend = 0; // the pass's blend mode, as the game's material passes number them
    float alpha_ref = 0.0F;
    std::array<float, 4> colour{1.0F, 1.0F, 1.0F, 1.0F};
    uint32_t flags = 0; // InstanceFlag's kAlphaTested / kTransparent where they come from the material
    // (R3) how the texture is sampled, as the pass's sampler states: address modes as gpu_value::kAddress* (1 wrap,
    // 2 mirror, 3 clamp, 4 border - transparent black, 5 mirror once: read as mirror), the filter as gpu_value::kFilter*
    // (none / point: nearest, linear / anisotropic: linear). alpha_ref above is 0..1 (Direct3D's ALPHAREF / 255), passed
    // when the texel's alpha (times colour's) is at least it, as the alpha test's GREATEREQUAL.
    uint32_t address_u = 1;
    uint32_t address_v = 1;
    uint32_t filter = 2;
    // (added by R5's reflections) How much the surface mirrors its surroundings (0 - 1; 0: not at all): the game marks
    // a shiny surface with an environment-mapped pass, whose strength (the pass's colour and fixed alpha) is this; and
    // which pass that is (its index; kNoEnvironmentPass: none).
    float reflectivity = 0.0F;
    uint32_t environment_pass = kNoEnvironmentPass;
};

// What was drawn this frame (R1): filled by render_pc between begin_frame_record and end_frame_record.
struct FrameRecord {
    uint64_t frame = 0;
    std::vector<Instance> instances;
    std::vector<Light> lights;
    std::vector<Camera> cameras;
};

// One ray traced through the scene the backend last built (R2: the automation channel's rtprobe - a check against the
// game's own collision): from `origin` along `direction` (any length) to `max_distance`. `traced` false: nothing to
// trace against (no backend, or no frame built yet).
struct ProbeRequest {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{};
    float max_distance = 0.0F;
};
struct ProbeResult {
    bool traced = false;
    bool hit = false;
    float distance = 0.0F;   // along the direction normalised
    uint32_t instance = 0;   // its index in the frame record's instances
    MaterialId material = 0; // the instance's
    MeshId mesh = 0;         // the instance's
    uint32_t primitive = 0;  // the triangle in the mesh (a strip's: counted along it)
};

// A backend that traces rays (R2 on): meshes registered as they are made and released, materials likewise, then each
// frame's record - from which it builds the scene it traces (the TLAS) before the frame is drawn.
class RayTracing {
public:
    virtual ~RayTracing() = default;
    [[nodiscard]] virtual bool available() const = 0;
    virtual void add_mesh(MeshId mesh, const Geometry& geometry) = 0;
    virtual void remove_mesh(MeshId mesh) = 0;
    virtual void mesh_changed(MeshId mesh) = 0; // a dynamic mesh's vertices rewritten
    virtual void set_material(MaterialId material, const Material& surface) { (void)material, (void)surface; }
    virtual void remove_material(MaterialId material) { (void)material; }
    virtual void begin_frame(const FrameRecord& record) = 0;
    // (R2) The ray through the last frame's scene, traced now (slow: the frame's work is finished first) - for tools,
    // not for frames.
    virtual ProbeResult probe(const ProbeRequest& ray)
    {
        (void)ray;
        return {};
    }
    // (added by R1) A registered mesh's triangles now somewhere else (another of its buffers - a skinned mesh's
    // vertex buffers take turns -, buffers made again after a device reset, an edit): the same id, new geometry, and
    // its contents changed as mesh_changed says.
    virtual void update_mesh(MeshId mesh, const Geometry& geometry)
    {
        remove_mesh(mesh);
        add_mesh(mesh, geometry);
    }
    // (added by R1, for checks) Whether mesh_drawn is wanted: render_pc then reports every draw of a registered mesh in
    // the world's and the objects' passes (a world transform read back per draw: a backend leaves it false).
    [[nodiscard]] virtual bool wants_draws() const { return false; }
    virtual void mesh_drawn(MeshId mesh, const std::array<std::array<float, 4>, 3>& object_to_world)
    {
        (void)mesh, (void)object_to_world;
    }
};

// The installed backend (null: none - Direct3D 9, or no ray tracing on this device / off in the ini).
RayTracing* ray_tracing();
void set_ray_tracing(RayTracing* backend);

// The frame being recorded (render_pc writes it; ray_tracing()->begin_frame reads it).
FrameRecord& frame_record();

// Ids: a new mesh / material id each call (never 0, never reused while the game runs).
MeshId next_mesh_id();
MaterialId next_material_id();

// --- R4: screen-space effects over a viewport's opaque world -------------------------------------------------------

// A viewport's opaque world drawn: render_pc's draw_viewport calls opaque_view_drawn after the world's opaque meshes,
// the objects' opaque passes, the texture splats and projections, the imposters and the sky - before the transparent
// passes, the particles, the weather, the 2D. The colour and depth targets bound then hold what is drawn so far.
struct ViewDrawn {
    Camera camera;                      // the world's camera (not the sky's, which sits at the origin)
    std::array<uint32_t, 4> rect{};     // the viewport in the target's pixels: x, y, width, height
    std::array<float, 2> depth_range{}; // the viewport's min / max z (0, 1)
};

// What listens (a backend's screen-space effect: the Vulkan Gpu's ray-traced sun shadows). Called on the thread that
// draws the frame, between the Gpu's draws.
class ViewListener {
public:
    virtual ~ViewListener() = default;
    virtual void opaque_drawn(const ViewDrawn& view) = 0;
};
void add_view_listener(ViewListener* listener);
void remove_view_listener(ViewListener* listener);
// Each listener told (render_pc; nothing happens when none listens).
void opaque_view_drawn(const ViewDrawn& view);
// Whether any listens (render_pc skips filling a ViewDrawn when none does).
bool view_listened();

} // namespace render::rt
