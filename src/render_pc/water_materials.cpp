// NOT ORIGINAL - which of the game's materials are water (water_materials.h).
#include "render_pc/water_materials.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/water_surfaces.h"
#include "engine/memory/arena_alloc.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "render_pc/bitmap_texture.h"

namespace render::water_materials {

namespace {

constexpr uint32_t kPassUvWibble = 0x1;      // a pass's flags: its texture matrix from its UV wibble (pc_material.cpp)
constexpr uint32_t kPassTextureAnim = 0x800; // ... its texture from the material's texture animation
constexpr uint32_t kPassTransparent = 0x40;  // a material's first pass: drawn with the transparent meshes
constexpr uint32_t kPassCount = 4;
constexpr uint32_t kBlendDiffuse = 0;          // the pass drawn as it is
constexpr uint32_t kBlendVertexTexAlpha = 0xB; // (kBlendModeVertexTexAlpha: blended by the texel's alpha x the vertex's)

bool moving(const MaterialRef* material)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    bool moves = material->texture_anim() != nullptr;
    for (uint32_t pass = 0; pass < passes; ++pass) {
        moves = moves || (core::at(material->pass_flags(), pass) & (kPassUvWibble | kPassTextureAnim)) != 0U ||
                core::at(material->uv_wibble(), pass) != nullptr;
    }
    return moves;
}

// The first pass's blend shows the surface itself (or over what is behind it by alpha), not a light or a shadow.
bool drawn_as_surface(const MaterialRef* material)
{
    const uint32_t mode = core::at(material->blend_modes(), 0) & static_cast<uint32_t>(kBlendModeMask);
    return mode == kBlendDiffuse || mode == static_cast<uint32_t>(kBlendModeAlpha) ||
           mode == static_cast<uint32_t>(kBlendModeFixBlend) || mode == kBlendVertexTexAlpha;
}

// [debug] rt_water_materials=1: each moving material's passes, once (the survey of which surfaces count).
void log_material(const MaterialRef* material, bool water)
{
    static const bool kWanted = build_info::fetch_flag("debug", "rt_water_materials", false);
    if (!kWanted) {
        return;
    }
    static std::unordered_set<uint64_t> logged; // (by checksum and name: a level loaded again isn't logged again)
    constexpr uint32_t kChecksumShift = 32U;
    if (!logged.insert((uint64_t{material->checksum()} << kChecksumShift) | material->name_hash()).second) {
        return;
    }
    std::string text = text::format("water: %s material %08X (name %08X): sort %u, two-sided %u, alpha ref %u, texture anim %s",
                                    water ? "water" : "moving, not water", material->checksum(), material->name_hash(),
                                    unsigned{material->sort_flag()}, unsigned{material->two_sided()},
                                    unsigned{material->alpha_ref()}, material->texture_anim() != nullptr ? "yes" : "no");
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        const TextureImage* texture = core::at(material->textures(), pass);
        const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
        text += text::format(" | pass %u flags %04X blend %X colour %.2f %.2f %.2f %.2f tex %08X %ux%u%s%s", pass,
                             core::at(material->pass_flags(), pass), core::at(material->blend_modes(), pass),
                             static_cast<double>(core::at(colour, 0)), static_cast<double>(core::at(colour, 1)),
                             static_cast<double>(core::at(colour, 2)), static_cast<double>(core::at(colour, 3)),
                             texture != nullptr ? texture->checksum : 0U, texture != nullptr ? unsigned{texture->width} : 0U,
                             texture != nullptr ? unsigned{texture->height} : 0U,
                             core::at(material->uv_wibble(), pass) != nullptr ? " wibble" : "",
                             pass == 0 && (core::at(material->pass_flags(), 0) & kPassTransparent) != 0U ? " transparent" : "");
    }
    get_log("%s", text.c_str());
}

// The level's main scene (the first world scene that isn't a sky): its checksum (0: none).
uint32_t main_scene()
{
    constexpr int kMaxSceneNodes = 256; // (a longer list is taken as broken)
    const memory::ListNode* node = engine_parts().scene_list().next();
    for (int guard = 0; node != nullptr && !node->is_head() && guard < kMaxSceneNodes; ++guard) {
        const auto* scene = static_cast<const WorldScene*>(node->data());
        if (scene != nullptr && !scene->is_sky()) {
            return scene->checksum();
        }
        node = node->next();
    }
    return 0;
}

// Each level's water (by its scene's name): the open sea's swell, the harbours', the canals' - and how clear it is.
struct LevelEntry {
    const char* name = nullptr;
    water::LevelWater water;
};
constexpr std::array kLevels = {
    LevelEntry{.name = "AU", .water = {.waves = 1.0F, .swell_length = 900.0F, .visibility = 900.0F, .heading = 205.0F}},
    LevelEntry{.name = "BA", .water = {.waves = 0.4F, .swell_length = 520.0F, .visibility = 600.0F, .heading = 120.0F}},
    LevelEntry{.name = "BE", .water = {.waves = 0.15F, .swell_length = 300.0F, .visibility = 220.0F, .heading = 60.0F}},
    LevelEntry{.name = "NO", .water = {.waves = 0.22F, .swell_length = 350.0F, .visibility = 180.0F, .heading = 30.0F}},
    LevelEntry{.name = "SE", .water = {.waves = 0.5F, .swell_length = 600.0F, .visibility = 450.0F, .heading = 330.0F}},
};

// The level's water told when the level changes (render_pc's draws: the main scene looked up once per water material).
void note_level()
{
    static uint32_t told = 1; // (never a checksum's 0: the first call tells)
    const uint32_t level = main_scene();
    if (level == told) {
        return;
    }
    told = level;
    water::LevelWater water;
    for (const LevelEntry& entry : kLevels) {
        if (checksums::hash_from_text(entry.name) == level) {
            water = entry.water;
        }
    }
    water.level = level;
    water::set_level_water(water);
}

} // namespace

bool is_water(const MaterialRef* material)
{
    if (material == nullptr || material->pass_count() == 0) {
        return false;
    }
    const TextureImage* first = core::at(material->textures(), 0);
    return first != nullptr && first->texture != nullptr && moving(material) && drawn_as_surface(material);
}

void note_material(const MaterialRef* material)
{
    if (!water::wanted()) {
        return;
    }
    const bool water = is_water(material);
    if (material != nullptr && moving(material)) {
        static std::unordered_set<const MaterialRef*> counted;
        if (water && counted.insert(material).second) {
            water::note_water_material();
        }
        log_material(material, water);
    }
    if (water) {
        note_level();
    }
    water::set_current(water ? core::at(material->textures(), 0)->texture : nullptr, material != nullptr ? material->checksum() : 0U);
}

} // namespace render::water_materials
