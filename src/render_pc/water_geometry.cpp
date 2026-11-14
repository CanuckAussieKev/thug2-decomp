// NOT ORIGINAL - the water's moving surface at load (water_geometry.h).
#include "render_pc/water_geometry.h"

#include <chrono>
#include <cstdint>
#include <ratio>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/water_surfaces.h"
#include "engine/gfx/water_tess.h"
#include "formats/scene_format.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "render_pc/mesh_smoothing.h"
#include "render_pc/water_materials.h"

namespace render::water_geometry {

namespace {

std::unordered_set<const IndexBufferData*>& split_meshes()
{
    static std::unordered_set<const IndexBufferData*> kept;
    return kept;
}

struct Totals {
    uint32_t meshes = 0;      // split
    uint32_t looked_at = 0;   // water materials' meshes
    uint32_t not_flat = 0;
    uint32_t widened = 0;     // split coarser than asked (too many vertices)
    uint32_t triangles_before = 0;
    uint32_t triangles_after = 0;
    uint32_t vertices_added = 0;
    double milliseconds = 0.0;
};
Totals& totals()
{
    static Totals made;
    return made;
}

bool log_meshes()
{
    static const bool kWanted = build_info::fetch_flag("debug", "rt_water_geometry_log", false);
    return kWanted;
}

} // namespace

bool on() { return water::geometry_wanted(); }

std::vector<uint16_t> split_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic)
{
    if (!on() || body == nullptr || material == nullptr || dynamic || body->has_vertex_bytes || !body->scaled.empty() ||
        fvf == 0 || core::at(body->index_counts, 0) < 3 || !water_materials::is_water(material)) {
        return {};
    }
    tess::VertexLayout layout;
    if (!mesh_smoothing::layout_of(fvf, *body, &layout) || !scene_format::vertices_fit(*body, body->stride)) {
        return {};
    }
    Totals& sum = totals();
    ++sum.looked_at;
    const auto started = std::chrono::steady_clock::now();
    std::vector<uint16_t>& strip = core::at(body->indices, 0);
    const water_tess::Options options;
    const water_tess::Result result =
        water_tess::subdivide(body->vertices, body->vertex_count, layout,
                              std::span<const uint16_t>(strip.data(), core::at(body->index_counts, 0)), options);
    sum.milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (log_meshes()) {
        get_log("rt_water_geometry: material %08X mesh of %u vertices, %u triangles: %s%s", material->checksum(),
                unsigned{body->vertex_count}, result.triangles, result.changed ? "split" : "left - ", result.why);
    }
    if (!result.changed) {
        sum.not_flat += std::string_view(result.why) == "not flat" ? 1U : 0U;
        return {};
    }
    ++sum.meshes;
    sum.widened += result.spacing > options.spacing ? 1U : 0U;
    sum.triangles_before += result.triangles;
    sum.triangles_after += result.made;
    sum.vertices_added += result.vertex_count - body->vertex_count;
    // (the new vertices replace the body's: the first level of detail's strip too; the others keep the original
    // vertices, still first)
    body->vertices = result.vertices;
    body->vertex_count = static_cast<uint16_t>(result.vertex_count);
    std::vector<uint16_t> original(strip.begin(), strip.begin() + core::at(body->index_counts, 0));
    strip = result.strip;
    core::at(body->index_counts, 0) = static_cast<uint16_t>(result.strip.size());
    return original;
}

void remember(const IndexBufferData* drawn)
{
    if (drawn != nullptr) {
        split_meshes().insert(drawn);
    }
}

void copy(const IndexBufferData* from, const IndexBufferData* to)
{
    if (to != nullptr && split(from)) {
        split_meshes().insert(to);
    }
}

void forget(const IndexBufferData* drawn)
{
    if (!split_meshes().empty()) {
        split_meshes().erase(drawn);
    }
}

bool split(const IndexBufferData* drawn) { return drawn != nullptr && split_meshes().contains(drawn); }

void before_draw(const IndexBufferData* first_level, int level)
{
    if (water::wanted()) {
        water::set_current_waves(level == 0 && split(first_level));
    }
}

void after_draw()
{
    if (water::wanted()) {
        water::set_current_waves(false);
    }
}

void log_totals(const char* scene)
{
    Totals& sum = totals();
    if (sum.looked_at != 0) {
        get_log("rt_water_geometry: %s - %u of %u water meshes split (%u not flat, %u coarser than asked): triangles %u -> "
                "%u, +%u vertices; %.1f ms",
                scene != nullptr ? scene : "(scene)", sum.meshes, sum.looked_at, sum.not_flat, sum.widened,
                sum.triangles_before, sum.triangles_after, sum.vertices_added, sum.milliseconds);
    }
    sum = Totals{};
}

} // namespace render::water_geometry
