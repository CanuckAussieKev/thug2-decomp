// NOT ORIGINAL - building relief at load (building_relief.h).
#include "render_pc/building_relief.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ratio>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "engine/file/file_load.h"
#include "engine/gfx/facade_relief.h"
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/surface_material.h"
#include "formats/relief_format.h"
#include "formats/scene_format.h"
#include "formats/surface_map_format.h"
#include "platform/run_log.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/mesh_smoothing.h"
#include "render_pc/surface_map_load.h"

namespace render::building_relief {

namespace {

constexpr const char* kSuffix = ".relief";
constexpr uint32_t kLargestFile = 16U * 1024U * 1024U;
constexpr uint32_t kPassTransparent = 0x40; // a material's first pass: drawn with the transparent meshes
constexpr uint8_t kCutOutAlphaRef = 8;      // an alpha reference above this: a cut-out (foliage, fences)
constexpr float kMostStrength = 2.0F;

struct Settings {
    bool on = false;
    relief::Options options;
};

const Settings& settings()
{
    static const Settings kSettings = [] {
        // (the settings model's running values: off without the Vulkan renderer or the files - settings_model.cpp)
        const config::Resolution& in_effect = config::running();
        Settings read;
        read.on = in_effect.on("building_relief");
        read.options.strength =
            std::clamp(in_effect.number("building_relief_strength", 1.0F), 0.0F, kMostStrength);
        if (read.on) {
            get_log("building_relief: on - strength %.2f, at most %.1f units in, %.1f out",
                    static_cast<double>(read.options.strength), static_cast<double>(read.options.most_recess),
                    static_cast<double>(read.options.most_raise));
        }
        return read;
    }();
    return kSettings;
}

// The textures' structures (attached as their dictionaries load; forgotten when they are destroyed).
std::unordered_map<const TextureImage*, relief::Structure>& structures()
{
    static std::unordered_map<const TextureImage*, relief::Structure> kept;
    return kept;
}

struct Totals {
    uint32_t meshes = 0;
    uint32_t looked_at = 0;
    uint32_t walls = 0;
    uint32_t relieved = 0;
    uint32_t pieces = 0;
    uint32_t sides = 0;
    uint32_t vertices_added = 0;
    uint32_t indices_before = 0;
    uint32_t indices_after = 0;
    uint32_t windows_only = 0;
    uint32_t too_big = 0;
    float deepest = 0.0F;
    float highest = 0.0F;
    double milliseconds = 0.0;
};
Totals& totals()
{
    static Totals made;
    return made;
}

} // namespace

bool on() { return settings().on; }

ReliefPack::ReliefPack(const char* name)
{
    if (!on() || name == nullptr) {
        return;
    }
    name_ = std::string(name) + kSuffix;
    if (!file::is_present(name_.c_str())) {
        return;
    }
    file::Hdl handle = file::unseal(name_.c_str(), "rb");
    if (handle == nullptr) {
        return;
    }
    std::vector<uint8_t> bytes;
    const uint32_t size = file::get_file_size(handle);
    if (size > 0 && size <= kLargestFile) {
        bytes.resize(size);
        if (file::get_take_in(bytes.data(), static_cast<int>(size), 1, handle) != static_cast<int>(size)) {
            bytes.clear();
        }
    }
    file::shut(handle);
    if (!relief_format::read_file(bytes, &file_)) {
        get_log("building_relief: %s isn't a building-relief file (or is damaged) - not used", name_.c_str());
        file_ = relief_format::File{};
        return;
    }
    if (active()) {
        originals_ = original_hashes(name, &set_);
    }
}

void ReliefPack::attach(const TextureImage* texture, uint32_t checksum, std::span<const uint8_t> first_level)
{
    if (!active() || texture == nullptr) {
        return;
    }
    bool listed = false;
    for (const relief_format::Entry& entry : file_.entries) {
        listed = listed || entry.checksum == checksum;
    }
    if (!listed) {
        return;
    }
    // (a texture set's texture: known by its original's texels)
    const auto original = originals_.find(checksum);
    const uint64_t hash =
        original != originals_.end() ? original->second : surface_map_format::content_hash(first_level);
    const relief_format::Entry* entry = relief_format::find(file_, checksum, hash);
    if (entry == nullptr) {
        return;
    }
    relief::Structure structure;
    structure.repeat_u = entry->repeat_u;
    structure.repeat_v = entry->repeat_v;
    for (uint32_t i = 0; i < entry->count; ++i) {
        const relief_format::Rect& rect = core::at(file_.rects, entry->first + i);
        structure.rects.push_back({.u0 = rect.u0,
                                   .v0 = rect.v0,
                                   .u1 = rect.u1,
                                   .v1 = rect.v1,
                                   .height = rect.height,
                                   .window = rect.kind == relief_format::Kind::kWindow});
    }
    structures().insert_or_assign(texture, std::move(structure));
    ++attached_;
}

void ReliefPack::report() const
{
    if (!active()) {
        return;
    }
    get_log("building_relief: %s - %u of %zu facades attached%s", name_.c_str(), attached_, file_.entries.size(),
            set_.empty() ? "" : (" (" + set_ + "'s textures, matched by their originals)").c_str());
}

void forget(const TextureImage* texture)
{
    if (!structures().empty()) {
        structures().erase(texture);
    }
}

std::vector<uint16_t> relieve_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic)
{
    if (!on() || structures().empty() || body == nullptr || material == nullptr || dynamic || body->has_vertex_bytes ||
        !body->scaled.empty() || fvf == 0 || core::at(body->index_counts, 0) < 3) {
        return {};
    }
    if ((core::at(material->pass_flags(), 0) & kPassTransparent) != 0U || material->alpha_ref() > kCutOutAlphaRef) {
        return {};
    }
    const auto found = structures().find(core::at(material->textures(), 0));
    if (found == structures().end()) {
        return {};
    }
    tess::VertexLayout layout;
    if (!mesh_smoothing::layout_of(fvf, *body, &layout) || layout.uv_offset < 0 ||
        !scene_format::vertices_fit(*body, body->stride)) {
        return {};
    }
    Totals& sum = totals();
    ++sum.looked_at;
    const auto started = std::chrono::steady_clock::now();
    std::vector<uint16_t>& strip = core::at(body->indices, 0);
    const relief::Result result =
        relief::build(body->vertices, body->vertex_count, layout,
                      std::span<const uint16_t>(strip.data(), core::at(body->index_counts, 0)), found->second,
                      settings().options);
    sum.milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    sum.walls += result.walls;
    sum.too_big += result.too_big ? 1U : 0U;
    if (!result.changed) {
        return {};
    }
    ++sum.meshes;
    sum.relieved += result.relieved;
    sum.pieces += result.pieces;
    sum.sides += result.sides;
    sum.vertices_added += result.vertex_count - body->vertex_count;
    sum.indices_before += core::at(body->index_counts, 0);
    sum.indices_after += static_cast<uint32_t>(result.strip.size());
    sum.windows_only += result.windows_only ? 1U : 0U;
    sum.deepest = std::max(sum.deepest, result.deepest);
    sum.highest = std::max(sum.highest, result.highest);
    // (the new vertices replace the body's: the first level of detail's strip too; the others keep the original
    // vertices, still first)
    body->vertices = result.vertices;
    body->vertex_count = static_cast<uint16_t>(result.vertex_count);
    std::vector<uint16_t> original(strip.begin(), strip.begin() + core::at(body->index_counts, 0));
    strip = result.strip;
    core::at(body->index_counts, 0) = static_cast<uint16_t>(result.strip.size());
    return original;
}

void log_totals(const char* scene)
{
    Totals& sum = totals();
    if (sum.looked_at != 0) {
        get_log("building_relief: %s - %u of %u facade meshes relieved: %u of their %u wall triangles cut into %u pieces, "
                "%u sides; +%u vertices, indices %u -> %u; at most %.1f units in, %.1f out%s%s; %.1f ms",
                scene != nullptr ? scene : "(scene)", sum.meshes, sum.looked_at, sum.relieved, sum.walls, sum.pieces,
                sum.sides, sum.vertices_added, sum.indices_before, sum.indices_after, static_cast<double>(sum.deepest),
                static_cast<double>(sum.highest),
                sum.windows_only != 0 ? (" (" + std::to_string(sum.windows_only) + " with the windows alone)").c_str() : "",
                sum.too_big != 0 ? (" (" + std::to_string(sum.too_big) + " too big: kept flat)").c_str() : "",
                sum.milliseconds);
    }
    sum = Totals{};
}

} // namespace building_relief
