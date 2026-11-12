#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_memory.h"

// NOT ORIGINAL - ray tracing's material table on Vulkan (vulkan_rt_materials.cpp; docs/design/vulkan.md "Ray tracing",
// R3): every surface the renderer registers (rt::Material, engine/gfx/rt_scene.h) as a record a shader reads, and the
// textures they use as one bindless array, so a ray's hit can be shaded whatever it hit. Made only when ray tracing is
// enabled (Context::ray_tracing_enabled); without it nothing here exists and nothing else changes.
//
// The descriptor set (layout(): bind it at any set number - the GLSL below reads THUG2_MATERIAL_SET, 1 by default):
//   binding 0  readonly buffer, scalar layout: MaterialRecord records[] - row n is MaterialId n (ids are never reused,
//              so the rows grow with the session: 40 bytes each); row 0 and rows never set are untextured white
//   binding 1  sampler samplers[kMaterialSamplerCount] - address u x address v (wrap, mirror, clamp, border transparent
//              black) x filter (nearest, linear), written once per set; sampled at an explicit level (a ray has no
//              derivatives), so no anisotropy. (Not immutable samplers: the AMD driver here read every index of an
//              immutable array, non-uniformly indexed, as its first sampler.)
//   binding 2  texture2D textures[] - partially bound, update-after-bind, a variable count (texture_slots()); slot 0 is
//              the backend's 1x1 opaque white texture
// Separate images and samplers rather than combined ones: a material picks its sampler independently of its texture
// (the game's passes set the addressing per pass, not per texture), with one slot per texture and no slot per pair;
// and combined image samplers would count against the device's sampler limits too.
//
// Textures: a texture gets a slot the first time a material uses it, kept until the texture is released; then the slot
// is set back to the white texture at once, every material using it reads slot 0, and the slot is handed out again
// only once the submissions that may have read it have finished. A texture the game draws into (a render target, a
// depth texture) is shown as white: its layout changes within the frame, and a slot must stay sampleable throughout.
// The sampled view is the raster draws' (the X formats' alpha reads 1), so a texel reads as it does there.
//
// The records: set() / remove() change a copy kept here (any thread; the table's own lock); make_visible() - once a
// frame, with the backend's lock held, before the work that reads the table is recorded - writes the new texture slots
// and copies the changed rows into the device's buffer in the frame's uploads (the reads of the frames before waited for
// first, every later read after). When the rows outgrow the buffer, a buffer twice the size and a new descriptor set are
// made (the old ones destroyed once their submissions have finished): fetch descriptor_set() after make_visible().
namespace render::vulkan {

struct Backend;
struct Texture;

// The samplers of binding 1, by index: sampler_index(address u, address v, linear).
inline constexpr uint32_t kMaterialAddressModes = 4; // wrap, mirror, clamp, border
inline constexpr uint32_t kMaterialSamplerCount = kMaterialAddressModes * kMaterialAddressModes * 2;
inline constexpr uint32_t kMaterialRecordsBinding = 0;
inline constexpr uint32_t kMaterialSamplersBinding = 1;
inline constexpr uint32_t kMaterialTexturesBinding = 2;

// A row of binding 0 (scalar layout: 40 bytes). The GLSL's `Material` has the same members in the same order.
struct MaterialRecord {
    uint32_t texture = 0; // slot in textures[] (0: white)
    uint32_t sampler = 0; // index in samplers[]
    uint32_t uv_set = 0;
    uint32_t blend = 0;
    float alpha_ref = 0.0F; // 0..1
    uint32_t flags = 0;     // rt::InstanceFlag's kAlphaTested / kTransparent
    std::array<float, 4> colour{1.0F, 1.0F, 1.0F, 1.0F};
};
inline constexpr std::size_t kMaterialRecordBytes = 40;
static_assert(sizeof(MaterialRecord) == kMaterialRecordBytes);

// The sampler index for a material's address modes (gpu_value::kAddress*) and filter (gpu_value::kFilter*).
uint32_t material_sampler_index(const rt::Material& material);
// A record as the table writes it for `material` with its texture in `slot`.
MaterialRecord material_record(const rt::Material& material, uint32_t slot);

// The GLSL a shader puts right after its `#version 460` (and its own #extension lines; `#define THUG2_MATERIAL_SET n`
// first to bind the table at set n, 1 by default). It enables GL_EXT_nonuniform_qualifier and
// GL_EXT_scalar_block_layout, declares the three bindings and:
//   struct Material { uint texture_slot; uint sampler_index; uint uv_set; uint blend; float alpha_ref; uint flags;
//                     vec4 colour; };
//   const uint MATERIAL_ALPHA_TESTED = 2u, MATERIAL_TRANSPARENT = 64u;   (rt::InstanceFlag's values)
//   Material material(uint id)                       - the record (id: rt::MaterialId; 0: untextured white)
//   vec4 material_texel(uint id, vec2 uv)            - its texture at level 0 through its sampler (non-uniform indexing)
//   vec4 material_texel_lod(uint id, vec2 uv, float lod)
//   vec4 material_sample(uint id, vec2 uv)           - the texel times the material's colour
//   bool material_alpha_passes(uint id, float alpha) - the alpha test: true unless MATERIAL_ALPHA_TESTED and the
//                                                      8-bit alpha is below the 8-bit reference (GREATEREQUAL)
//   bool material_alpha_passes_at(uint id, vec2 uv)  - the same for material_sample(id, uv).a
// The UVs are the caller's: the material says which set (uv_set) its texture uses.
std::string_view material_glsl();

class MaterialTable {
public:
    static constexpr uint32_t kDefaultTextureSlots = 1U << 16U;
    static constexpr uint32_t kDefaultFirstRows = 1U << 10U;
    struct Options {
        uint32_t texture_slots = 0;                // the textures[] count (0: kDefaultTextureSlots, within the limit)
        uint32_t first_rows = kDefaultFirstRows;   // the records' first buffer
        uint32_t first_slot = 1;                   // where new slots are handed out from (the checks: near the top)
    };
    // The most textures[] may hold, whatever the device allows (some allow billions: the table keeps a word or two per slot).
    static constexpr uint32_t kMostTextureSlots = 1U << 20U;
    // Sampled images left for the other sets of a pipeline that binds the table (its per-stage limits are shared).
    static constexpr uint32_t kReservedSampledImages = 64;

    MaterialTable() = default;
    MaterialTable(const MaterialTable&) = delete;
    MaterialTable& operator=(const MaterialTable&) = delete;
    MaterialTable(MaterialTable&&) = delete;
    MaterialTable& operator=(MaterialTable&&) = delete;
    ~MaterialTable() = default;

    // The textures[] count the device allows a set (its update-after-bind limits, kReservedSampledImages left over), at
    // most kMostTextureSlots.
    static uint32_t device_slot_limit(VkPhysicalDevice device);

    // The samplers, the layout, the first buffer and set (the backend's draw objects made; its lock held). False (and
    // nothing kept) when ray tracing isn't enabled or a step fails.
    bool init(Backend& state, const Options& options);
    bool init(Backend& state) { return init(state, Options{}); }
    // Everything destroyed (the device's work finished first: Frames::shutdown).
    void shutdown(Backend& state);
    [[nodiscard]] bool ready() const { return layout_ != VK_NULL_HANDLE; }

    // Any thread.
    void set(rt::MaterialId id, const rt::Material& material);
    void remove(rt::MaterialId id);

    // The backend's lock held. A texture released: its slot set back to white, its materials to slot 0.
    void texture_released(Backend& state, Texture* texture);
    // The frame's changes recorded (the uploads command buffer); false when the memory ran out.
    bool make_visible(Backend& state);

    [[nodiscard]] VkDescriptorSetLayout layout() const { return layout_; }
    [[nodiscard]] VkDescriptorSet descriptor_set() const { return current_.set; }

    // For the checks.
    [[nodiscard]] uint32_t texture_slots() const { return slot_count_; }
    [[nodiscard]] uint32_t slots_in_use() const;
    [[nodiscard]] uint32_t slot_of(GpuTexture* texture) const; // 0: none
    [[nodiscard]] uint32_t rows() const { return current_.rows; }
    [[nodiscard]] MaterialRecord record(rt::MaterialId id) const;

private:
    // A buffer of records and the set that names it.
    struct Generation {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        DeviceBuffer buffer;
        uint32_t rows = 0;
    };
    struct RetiredGeneration {
        Generation generation;
        uint64_t serial = 0;
    };
    struct RetiredSlot {
        uint32_t slot = 0;
        uint64_t serial = 0;
    };

    uint32_t slot_for(Texture* texture); // (the table's lock held)
    void mark_dirty(uint32_t row);
    bool make_generation(Backend& state, uint32_t rows, Generation* made);
    void destroy_generation(Backend& state, Generation* generation);
    void write_slots(Backend& state, VkDescriptorSet set, const std::vector<uint32_t>& slots);
    bool upload_dirty_rows(Backend& state);

    mutable std::mutex mutex_;
    VkDevice device_ = nullptr;
    std::array<VkSampler, kMaterialSamplerCount> samplers_{};
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    uint32_t slot_count_ = 0;
    uint32_t first_slot_ = 1;
    Generation current_;
    std::vector<RetiredGeneration> retired_generations_;

    std::vector<MaterialRecord> records_;           // by MaterialId
    std::vector<Texture*> record_textures_;         // by MaterialId: the texture its slot came from
    std::vector<uint32_t> dirty_;                   // rows changed since the last make_visible
    std::vector<uint8_t> dirty_mark_;               // by row
    std::unordered_map<Texture*, uint32_t> slots_;  // the textures with a slot
    std::vector<Texture*> slot_textures_;           // by slot (null: free or white)
    std::vector<std::vector<rt::MaterialId>> slot_users_; // by slot: the materials set with it (some since changed)
    std::vector<uint32_t> free_slots_;
    std::vector<RetiredSlot> retired_slots_;
    uint32_t next_slot_ = 1;
    std::vector<uint32_t> slots_to_write_;          // slots whose descriptor changed since the last make_visible
    bool full_reported_ = false;
};

} // namespace render::vulkan
