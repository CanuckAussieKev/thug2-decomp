#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/screen_effects.h"

#include "base/maths/shape_tests.h"
#include <gsl/pointers>
#include <array>
#include "platform/d3d_forward.h"

namespace scene_format {
class MemoryStream;
class FileStream;
} // namespace scene_format

// The PC scene and sector (Nx engine), and the PC screen effects object's setters.
namespace render {

struct SceneRef;

// What the scene's PostLoad packs the sectors' vertices into (one vertex buffer).
struct VertexArena {
    IDirect3DVertexBuffer9* buffer; // +0x00
    uint32_t used; // +0x04
    uint32_t size; // +0x08: bytes
    void* data;    // +0x0C: while locked
};

constexpr std::size_t kPcSectorUnknown20Offset = 0x20;
constexpr std::size_t kPcSectorBytes = 0x34;

// The PC sector (0x34 bytes, the original's vtable 0x0064A718; the engine's WorldCell for its first 0x1C bytes). Its
// table's slots 4-10 (the shared stubs, fetch_empty_bounds and - folded - HitMovingMesh::fetch_facing, the floats at
// +0x20) were never called.
class PcSector final : public WorldCell {
public:
    // 0x004C6550
    PcSector() = default;
    // 0x004C65B0 (0x004C6580 the deleting destructor): a jump to the engine's.
    ~PcSector() override = default;
    // 0x004C6530 (slot 1): with `on`, its geometry given to the engine's 0x00490DC0.
    void plat_set_shatter(bool on) override;
    // 0x004C65C0 (slot 3): a new sector (the arguments unused). Null if no memory.
    gsl::owner<PcSector*> plat_clone(int instance, void* scene) override;
    // 0x004C6650: loaded from a scene in memory (scene_format.h; the stream moved past it): its checksum, flags,
    // box, billboard and meshes (each made, its vertices loaded by 0x004D5CC0, its index lists read and dropped).
    // False (the stream failed; the meshes made so far kept) if a record is damaged.
    bool read_from_ram_block(scene_format::MemoryStream& in);
    // 0x004C6B10: the same from a file (the vertices by 0x004D6100, given `unknown` unless a billboard).
    bool ingest_from_file(scene_format::FileStream& in, VertexArena* arena);
    // 0x004C64E0 (slot 9, never called): an empty box (a static one, made the first time).
    static const maths::BBox* fetch_empty_bounds();

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t flags() const { return flags_; }
    void set_flags(uint32_t value) { flags_ = value; }

private:
    uint32_t flags_{0};                    // +0x1C: 0x800000 billboards, 0x200000 the meshes' flag 0x200
    std::array<float, 3> unknown20{{0.0F, 0.0F, 0.0F}}; // +0x20 (zero)
    float unknown2c_{1.0F};                             // +0x2C (1)
    std::array<uint8_t, 4> unknown30_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcSector, unknown20) == kPcSectorUnknown20Offset &&
                          sizeof(PcSector) == kPcSectorBytes,
                      "layout must match the original");
    }
};

constexpr std::size_t kPcSceneMeshesOffset = 0x280;
constexpr std::size_t kPcSceneBytes = 0x284;

// The PC scene (0x284 bytes, vtable 0x0064A6B8; the engine's WorldScene - constructor 0x0049D390, destructor body
// 0x0049DD10 - for its first 0x280 bytes: +0xD0 the sectors' hash table).
struct PcScene final : public WorldScene {
    // 0x004C6130: the engine's constructor (`bits` its sector table's).
    explicit PcScene(int bits) : WorldScene(bits) {}
    // 0x004C6180 (0x004C6150 the scalar deleting destructor): a jump to the engine's.
    ~PcScene() override = default;
    // 0x004C6270 (vtable slot 1): after loading: with `arena`, a write-only vertex buffer of its size made and locked
    // and the sectors' first vertex buffers moved into it; the sectors' meshes counted, the mesh list allocated, the
    // meshes added to it and sorted.
    void post_load(void* arena) override;
    // (Slots 3 and 4: a shared "true", folded with SaveCard::always_true.)
    bool load_textures(const char* /*name*/) override { return true; }
    bool load_collision(const char* /*name*/) override { return true; }
    // (Slot 6: a shared "false".)
    bool unload_add_scene() override { return false; }
    // 0x004C6190 (slot 7): a new sector. Null if no memory.
    gsl::owner<WorldCell*> plat_create_sector() override;
    // 0x004C6200: the sectors gone through (nothing done with them - stripped).
    void unload_sectors();

    // The fields other code reads and sets.
    [[nodiscard]] SceneRef* meshes() const { return meshes_; }
    void set_meshes(SceneRef* value) { meshes_ = value; }

private:
    SceneRef* meshes_{}; // +0x280: its mesh list
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcScene, meshes_) == kPcSceneMeshesOffset && sizeof(PcScene) == kPcSceneBytes,
                      "layout must match the original");
    }
};

// The PC screen effects (8 bytes, vtable 0x0064A6E4; the engine's 0x0049E2F0 - destructor body 0x0049E300): its
// setters write the renderer's settings (screen_fx.cpp: the bloom).
class PcScreenEffects final : public ScreenEffects {
public:
    // 0x004C6480: the engine's.
    PcScreenEffects() = default;
    // 0x004C64A0 (0x004C64B0 the scalar deleting destructor): the engine's.
    ~PcScreenEffects() override = default;
    // 0x004C63C0 (vtable slot 2): the bloom on / off (0x00705720).
    void set_bloom(int on) override;
    // 0x004C63E0 (slot 3): the bloom's cut-off colour (0x0067E98C).
    void set_bloom_cut_off(uint32_t colour) override;
    // 0x004C63F0 (slot 4): the bloom passes' alphas (0x0067E97C, 0x0067E980).
    void set_bloom_alphas(int first, int second) override;
    // 0x004C6410 (slot 5): 0x0067E990.
    void set_bloom_colour(uint32_t colour) override;
    // 0x004C6420 (slot 6): 0x00705724 on / off.
    void set_dof_on(int on) override;
    // (Slots 7 and 9: a shared function ignoring its argument.)
    void set_dof_colour_b(uint32_t /*colour*/) override {}
    void set_dof_value_b(float /*value*/) override {}
    // 0x004C6460 (slot 8): 0x0067E994.
    void set_dof_colour_a(uint32_t colour) override;
    // 0x004C6470 (slot 10): 0x0067E984 (the float's bits).
    void set_dof_value_a(float value) override;
    // 0x004C6440 (slot 11): 0x0067E988.
    void set_dof_value_c(float value) override;
    // 0x004C6450 (slot 12): 0x00705728.
    void set_dof_value_d(float value) override;
};

} // namespace render
