// NOT ORIGINAL - the sky scene's scenery under the renderer's own sky (sky_scenery.h).
#include "render_pc/sky_scenery.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/sky_parts.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/texel_decode.h"
#include "platform/run_log.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/mesh_list.h"
#include "render_pc/mesh_smoothing.h"
#include "render_pc/rt_record.h"
#include "render_pc/sub_mesh.h"

namespace render::sky_scenery {

namespace {

namespace parts = sky_parts;

struct SceneParts {
    const MeshRef* const* run = nullptr; // (its meshes as they were read: a scene loaded again is read again)
    int count = 0;
    std::unordered_set<const MeshRef*> scenery;
};

struct State {
    std::unordered_map<const SceneRef*, SceneParts> scenes;
    const SceneParts* current = nullptr;
};
State& state()
{
    static core::NoDestroy<State> made;
    return made.get();
}

// A texture's coverage: its first level's alpha (empty: none read - taken as opaque).
parts::Coverage coverage_of(GpuTexture* texture)
{
    parts::Coverage out;
    texel::Image image;
    rt_record::decode_first_level(texture, &image);
    if (image.empty()) {
        return out;
    }
    out.width = image.width;
    out.height = image.height;
    out.alpha.reserve(image.texels.size());
    for (const std::array<uint8_t, 4>& texel : image.texels) {
        out.alpha.push_back(std::get<3>(texel));
    }
    return out;
}

// A mesh as the sky's camera sees it: its vertices and its first level of detail's triangles (a mesh smoothed at load
// read on its original triangles - its original vertices come first).
bool shape_of(const MeshRef* mesh, parts::MeshInput* out)
{
    rt::Geometry geometry;
    const VertexBufferData* source = nullptr;
    if (!rt_record::mesh_geometry(mesh, &geometry, &source) || source == nullptr || source->data() == nullptr ||
        geometry.cpu_indices == nullptr || geometry.vertex_stride == 0) {
        return false;
    }
    const std::span<const uint8_t> bytes(source->data(), source->size());
    const std::size_t stride = geometry.vertex_stride;
    const auto vertices = static_cast<std::size_t>(geometry.vertex_count);
    if (bytes.size() < vertices * stride) {
        return false;
    }
    const bool uvs = geometry.uv_format == rt::kUvFloat2;
    for (std::size_t i = 0; i < vertices; ++i) {
        std::array<float, 3> position{};
        std::memcpy(position.data(), bytes.subspan((i * stride) + geometry.position_offset, sizeof(position)).data(),
                    sizeof(position));
        std::array<float, 2> uv{};
        if (uvs) {
            std::memcpy(uv.data(), bytes.subspan((i * stride) + geometry.uv_offset, sizeof(uv)).data(), sizeof(uv));
        }
        out->positions.push_back(position);
        out->uvs.push_back(uv);
    }
    std::vector<uint32_t> indices;
    const std::span<const uint16_t> original = mesh_smoothing::original_strip(core::at(mesh->index_buffers(), 0));
    bool strip = geometry.strip;
    if (!original.empty()) {
        indices.assign(original.begin(), original.end());
        strip = true;
    } else {
        const std::size_t width = geometry.indices_32bit ? sizeof(uint32_t) : sizeof(uint16_t);
        const std::span<const uint8_t> raw(static_cast<const uint8_t*>(geometry.cpu_indices),
                                           (std::size_t{geometry.index_offset} + geometry.index_count) * width);
        for (uint32_t k = 0; k < geometry.index_count; ++k) {
            const std::size_t at = (std::size_t{geometry.index_offset} + k) * width;
            if (geometry.indices_32bit) {
                uint32_t value = 0;
                std::memcpy(&value, raw.subspan(at, sizeof(value)).data(), sizeof(value));
                indices.push_back(value);
            } else {
                uint16_t value = 0;
                std::memcpy(&value, raw.subspan(at, sizeof(value)).data(), sizeof(value));
                indices.push_back(value);
            }
        }
    }
    const std::size_t triangles = strip ? (indices.size() >= 3 ? indices.size() - 2 : 0) : indices.size() / 3;
    for (std::size_t t = 0; t < triangles; ++t) {
        const std::size_t first = strip ? t : t * 3;
        const std::array<uint32_t, 3> corner{core::at(indices, first), core::at(indices, first + 1),
                                             core::at(indices, first + 2)};
        const auto [a, b, c] = corner;
        if (a != b && b != c && a != c) {
            out->triangles.push_back(corner);
        }
    }
    return true;
}

SceneParts read_scene(const SceneRef* scene, uint32_t checksum)
{
    SceneParts made;
    const std::span<MeshRef*> run = scene->mesh_run();
    made.run = run.data();
    made.count = scene->mesh_count();
    std::unordered_map<GpuTexture*, parts::Coverage> coverages;
    std::vector<parts::MeshFacts> facts;
    std::vector<const MeshRef*> meshes;
    for (const MeshRef* mesh : run) {
        if (mesh == nullptr || mesh->material() == nullptr) {
            continue;
        }
        const MaterialRef* material = mesh->material();
        parts::MeshInput input;
        input.blend = core::at(material->blend_modes(), 0) & static_cast<uint32_t>(kBlendModeMask);
        const TextureImage* texture = core::at(material->textures(), 0);
        if (texture != nullptr && texture->texture != nullptr) {
            const auto [found, fresh] = coverages.try_emplace(texture->texture);
            if (fresh) {
                found->second = coverage_of(texture->texture);
            }
            input.coverage = &found->second;
        }
        if (!shape_of(mesh, &input)) {
            continue;
        }
        facts.push_back(parts::measure(input));
        meshes.push_back(mesh);
    }
    const std::vector<parts::SkyPart> kinds = parts::classify(facts);
    std::array<int, 4> counts{};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const parts::SkyPart kind = core::at(kinds, i);
        ++core::at(counts, static_cast<std::size_t>(kind));
        if (kind == parts::SkyPart::kScenery) {
            made.scenery.insert(core::at(meshes, i));
        }
    }
    get_log("sky: scene %08X's %zu meshes - %d painted, %d glows, %d clouds (the renderer's sky draws those), %d "
            "scenery (drawn over it, lit by the light now)",
            checksum, kinds.size(), std::get<0>(counts), std::get<1>(counts), std::get<2>(counts), std::get<3>(counts));
    return made;
}

} // namespace

bool prepare(const SceneRef* scene, uint32_t checksum)
{
    State& st = state();
    const std::span<MeshRef*> run = scene->mesh_run();
    auto found = st.scenes.find(scene);
    if (found == st.scenes.end() || found->second.run != run.data() || found->second.count != scene->mesh_count()) {
        found = st.scenes.insert_or_assign(scene, read_scene(scene, checksum)).first;
    }
    st.current = &found->second;
    return !found->second.scenery.empty();
}

bool is_scenery(const MeshRef* mesh)
{
    const SceneParts* current = state().current;
    return current != nullptr && current->scenery.contains(mesh);
}

} // namespace render::sky_scenery
