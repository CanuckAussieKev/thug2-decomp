// NOT ORIGINAL - (round TRAIN) texture coordinates that place nothing made world-aligned at load (texture_placing.h).
#include "render_pc/texture_placing.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/facade_relief.h"
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/texture_mapping.h"
#include "formats/scene_format.h"
#include "platform/run_log.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/mesh_smoothing.h"

namespace render::texture_placing {

namespace {

constexpr uint32_t kPassTransparent = 0x40; // a material's first pass: drawn with the transparent meshes
constexpr uint8_t kCutOutAlphaRef = 8;      // an alpha reference above this: a cut-out (foliage, fences)

struct Totals {
    uint32_t meshes = 0;
    uint32_t triangles = 0;
    uint32_t placing_nothing = 0;
    uint32_t shown = 0;
    uint32_t vertices = 0;
    uint32_t copies = 0;
    float largest = 0.0F;
    float densest = 0.0F;
};

Totals& totals()
{
    static Totals sum;
    return sum;
}

} // namespace

std::vector<uint16_t> place_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic,
                                 std::vector<relief::VertexSource>* sources)
{
    if (body == nullptr || material == nullptr || dynamic || body->has_vertex_bytes || !body->scaled.empty() || fvf == 0 ||
        core::at(body->index_counts, 0) < 3) {
        return {};
    }
    if ((core::at(material->pass_flags(), 0) & kPassTransparent) != 0U || material->alpha_ref() > kCutOutAlphaRef) {
        return {};
    }
    const TextureImage* texture = core::at(material->textures(), 0);
    if (texture == nullptr) {
        return {};
    }
    tess::VertexLayout layout;
    if (!mesh_smoothing::layout_of(fvf, *body, &layout) || layout.uv_offset < 0 ||
        !scene_format::vertices_fit(*body, body->stride)) {
        return {};
    }
    std::vector<uint16_t>& strip = core::at(body->indices, 0);
    const std::size_t drawn = std::min<std::size_t>(strip.size(), core::at(body->index_counts, 0));
    texture_mapping::Result result = texture_mapping::world_aligned(
        body->vertices, body->vertex_count, layout, std::span<const uint16_t>(strip.data(), drawn),
        std::max<uint32_t>(texture->width, texture->height));
    Totals& sum = totals();
    sum.triangles += result.triangles;
    sum.placing_nothing += result.placing_nothing;
    sum.shown += result.shown;
    if (!result.changed) {
        return {};
    }
    ++sum.meshes;
    sum.vertices += result.vertices;
    sum.copies += static_cast<uint32_t>(result.sources.size());
    sum.largest = std::max(sum.largest, result.largest);
    sum.densest = std::max(sum.densest, result.density);
    // (the original vertices first, the copies after them: the other levels of detail keep drawing the originals)
    const std::size_t original_bytes = std::size_t{body->vertex_count} * layout.stride;
    std::copy_n(result.vertex_bytes.begin(), static_cast<std::ptrdiff_t>(original_bytes), body->vertices.begin());
    body->vertices.insert(body->vertices.begin() + static_cast<std::ptrdiff_t>(original_bytes),
                          result.vertex_bytes.begin() + static_cast<std::ptrdiff_t>(original_bytes),
                          result.vertex_bytes.end());
    body->vertex_count = static_cast<uint16_t>(result.vertex_count);
    if (result.strip.empty()) {
        return {};
    }
    if (sources != nullptr) {
        for (const uint16_t from : result.sources) {
            sources->push_back({.from = {from, 0, 0}, .weight = {1.0F, 0.0F, 0.0F}, .count = 1});
        }
    }
    std::vector<uint16_t> original(strip.begin(), strip.begin() + static_cast<std::ptrdiff_t>(drawn));
    strip = std::move(result.strip);
    core::at(body->index_counts, 0) = static_cast<uint16_t>(strip.size());
    return original;
}

void log_totals(const char* scene)
{
    Totals& sum = totals();
    if (sum.placing_nothing != 0) {
        get_log("texture placing: %s - %u of %u opaque textured triangles mapped along one direction only (their texture a "
                "fan of stripes), %u of them lit by the art (kept); %u vertices of %u meshes given world-aligned coordinates "
                "(%u of them copies), at most %.4f a unit, the largest %.1f",
                scene != nullptr ? scene : "(scene)", sum.placing_nothing, sum.triangles, sum.shown, sum.vertices, sum.meshes,
                sum.copies, static_cast<double>(sum.densest), static_cast<double>(sum.largest));
    }
    sum = Totals{};
}

} // namespace render::texture_placing
