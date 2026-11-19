// NOT ORIGINAL - the draws apart kept for the rays: see rt_apart.h.
#include "render_pc/rt_apart.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "platform/platform_config.h"
#include "engine/gfx/mesh_records.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace render::rt_apart {

namespace {

constexpr uint32_t kQuadCorners = 4;
constexpr std::array<uint32_t, 6> kQuadList{0, 1, 2, 2, 3, 0}; // (the quad index buffer's - pc_d3d_resources.cpp)
constexpr uint32_t kPrimitiveList = 4;                         // a mesh's primitive type: a triangle list
constexpr uint32_t kPrimitiveStrip = 5;                        // ... a strip
constexpr uint32_t kPrimitiveFan = 6;                          // ... a fan
constexpr std::size_t kMostKeptBytes = std::size_t{64} << 20U; // a frame's draws kept at most (beyond: left out)

struct State {
    int view = 0;
    bool particles = false;
    const MaterialRef* material = nullptr;
    // the last run written to the dynamic buffer: its first vertex, stride and bytes
    uint32_t run_first = 0;
    uint32_t run_stride = 0;
    std::vector<uint8_t> run;
    bool run_live = false;
    std::vector<Draw> draws;
    std::size_t kept_bytes = 0;
};

State& state()
{
    static State made;
    return made;
}

uint32_t current_format()
{
    uint32_t fvf = 0;
    gpu().get_vertex_format(&fvf);
    return fvf;
}

// A new draw kept with the current material and format (null: none kept - over the frame's most).
Draw* new_draw(uint32_t stride, std::size_t vertices)
{
    State& st = state();
    const std::size_t bytes = vertices * stride;
    if (st.material == nullptr || stride == 0 || vertices < 3 || st.kept_bytes + bytes > kMostKeptBytes) {
        return nullptr;
    }
    st.kept_bytes += bytes;
    Draw& draw = st.draws.emplace_back();
    draw.material = st.material;
    draw.fvf = current_format();
    draw.stride = stride;
    draw.vertices.reserve(bytes);
    return &draw;
}

void append_vertex(Draw* draw, std::span<const uint8_t> from, std::size_t vertex)
{
    const std::size_t at = vertex * draw->stride;
    if (at + draw->stride > from.size()) {
        return;
    }
    const std::span<const uint8_t> one = from.subspan(at, draw->stride);
    draw->vertices.insert(draw->vertices.end(), one.begin(), one.end());
}

// The draw's last triangle dropped when one of its corners wasn't there (a run cut short).
void whole_triangles(Draw* draw)
{
    const std::size_t triangle = std::size_t{draw->stride} * 3;
    draw->vertices.resize(draw->vertices.size() - (draw->vertices.size() % triangle));
}

} // namespace

void view_begins(int viewport) { state().view = viewport; }

void set_particle_passes(bool inside)
{
    State& st = state();
    st.particles = inside;
    st.material = nullptr;
    st.run_live = false;
}

bool wanted()
{
    static const bool kOn = build_info::fetch_flag("debug", "rt_particles", true);
    return kOn && rt::ray_tracing() != nullptr;
}

bool capturing()
{
    const State& st = state();
    return st.particles && st.view == 0 && wanted();
}

void material_used(const MaterialRef* material)
{
    if (capturing()) {
        state().material = material;
    }
}

void vertices_unlocked(std::span<const uint8_t> bytes, uint32_t first_vertex, uint32_t stride)
{
    if (!capturing()) {
        return;
    }
    State& st = state();
    st.run.assign(bytes.begin(), bytes.end());
    st.run_first = first_vertex;
    st.run_stride = stride;
    st.run_live = true;
}

void quads_drawn(uint32_t first_vertex, uint32_t triangles)
{
    State& st = state();
    if (!capturing() || !st.run_live || st.run_first != first_vertex) {
        return;
    }
    const uint32_t quads = triangles / 2;
    Draw* draw = new_draw(st.run_stride, std::size_t{quads} * kQuadList.size());
    if (draw == nullptr) {
        return;
    }
    for (uint32_t quad = 0; quad < quads; ++quad) {
        for (const uint32_t corner : kQuadList) {
            append_vertex(draw, st.run, (std::size_t{quad} * kQuadCorners) + corner);
        }
    }
    whole_triangles(draw);
}

void list_drawn(uint32_t first_vertex, uint32_t vertices)
{
    State& st = state();
    if (!capturing() || !st.run_live || st.run_first != first_vertex) {
        return;
    }
    Draw* draw = new_draw(st.run_stride, vertices);
    if (draw == nullptr) {
        return;
    }
    for (uint32_t vertex = 0; vertex < vertices; ++vertex) {
        append_vertex(draw, st.run, vertex);
    }
    whole_triangles(draw);
}

void billboard_drawn(const MeshRef* mesh, std::span<const uint8_t> vertices)
{
    State& st = state();
    if (st.view != 0 || !wanted() || mesh == nullptr || mesh->material() == nullptr) {
        return;
    }
    const IdxStore* indices = mesh->index_store(0);
    if (indices == nullptr) {
        return;
    }
    const std::span<const uint16_t> run = indices->index_run();
    const std::size_t count = std::min<std::size_t>(core::at(mesh->index_counts(), 0), run.size());
    const uint32_t stride = mesh->stride();
    // (the billboard's own material, not the particle passes')
    const MaterialRef* before = st.material;
    st.material = mesh->material();
    Draw* draw = new_draw(stride, count * 3);
    st.material = before;
    if (draw == nullptr) {
        return;
    }
    draw->fvf = mesh->fvf();
    const auto triangle = [&](uint16_t first, uint16_t second, uint16_t third) {
        if (first == second || second == third || first == third) {
            return; // (a strip's joins)
        }
        for (const uint16_t corner : {first, second, third}) {
            append_vertex(draw, vertices, corner);
        }
    };
    const uint32_t type = mesh->primitive_type();
    if (type == kPrimitiveList) {
        for (std::size_t i = 0; i + 3 <= count; i += 3) {
            triangle(core::at(run, i), core::at(run, i + 1), core::at(run, i + 2));
        }
    } else if (type == kPrimitiveStrip) {
        for (std::size_t i = 0; i + 3 <= count; ++i) {
            if ((i & 1U) == 0) {
                triangle(core::at(run, i), core::at(run, i + 1), core::at(run, i + 2));
            } else {
                triangle(core::at(run, i + 1), core::at(run, i), core::at(run, i + 2));
            }
        }
    } else if (type == kPrimitiveFan) {
        for (std::size_t i = 1; i + 2 <= count; ++i) {
            triangle(core::at(run, 0), core::at(run, i), core::at(run, i + 1));
        }
    }
    whole_triangles(draw);
    if (draw->vertices.empty()) {
        st.draws.pop_back();
    }
}

std::vector<Draw> take()
{
    State& st = state();
    std::vector<Draw> out = std::move(st.draws);
    st.draws.clear();
    st.kept_bytes = 0;
    return out;
}

void material_released(const MaterialRef* material)
{
    State& st = state();
    std::erase_if(st.draws, [material](const Draw& draw) { return draw.material == material; });
    if (st.material == material) {
        st.material = nullptr;
    }
}

} // namespace render::rt_apart
