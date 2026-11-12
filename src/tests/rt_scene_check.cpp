// NOT ORIGINAL - thug2_rt_scene_check.exe: ray tracing's pure helpers (engine/gfx/rt_checks.h) - a Direct3D matrix as
// object-to-world rows, strips as lists, the geometry and flag tests the contract check makes. Exit code 0 when every
// check passes.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_checks.h"
#include "engine/gfx/rt_scene.h"
#include "tests/no_error_dialogs.h"

// The buffers' handles are incomplete types outside the backends: here two stand-ins, never read.
namespace render {
struct GpuVertexBuffer {
    int unused;
};
struct GpuIndexBuffer {
    int unused;
};
} // namespace render

namespace {

namespace rt = render::rt;

constexpr float kNear = 1.0e-5F;      // two points alike
constexpr float kTiny = 1.0e-6F;      // a change same_rows overlooks
constexpr float kMove = 1.0F;         // one it doesn't
constexpr std::size_t kTranslation = 12; // a Direct3D matrix's translation: its last row

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const char* what)
{
    if (!ok) {
        ++failures();
        std::printf("FAIL: %s\n", what);
    }
}

bool near3(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(std::fabs(core::at(a, i) - core::at(b, i)) < kNear)) {
            return false;
        }
    }
    return true;
}

// point * M with a Direct3D matrix (row vectors; the point's w 1).
std::array<float, 3> d3d_transform(const std::array<float, rt::kMatrixFloats>& matrix, const std::array<float, 3>& point)
{
    const std::array<float, 4> row_vector = {core::at(point, 0), core::at(point, 1), core::at(point, 2), 1.0F};
    std::array<float, 3> out{};
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::size_t k = 0; k < 4; ++k) {
            core::at(out, j) += core::at(row_vector, k) * core::at(matrix, (k * 4) + j);
        }
    }
    return out;
}

float& cell(rt::Rows& rows, std::size_t row, std::size_t column) { return core::at(core::at(rows, row), column); }
float cell(const rt::Rows& rows, std::size_t row, std::size_t column) { return core::at(core::at(rows, row), column); }

void check_transforms()
{
    // A turn of 90 degrees about y, a scale of 2 and a move by (10, 20, 30), as the game's matrices hold them.
    const std::array<float, rt::kMatrixFloats> matrix = {0.0F, 0.0F, -2.0F, 0.0F, 0.0F,  2.0F,  0.0F,  0.0F,
                                                         2.0F, 0.0F, 0.0F,  0.0F, 10.0F, 20.0F, 30.0F, 1.0F};
    const std::array<float, rt::kMatrixFloats> identity = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                                           0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    const std::array<std::array<float, 3>, 4> points = {
        {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {-3.5F, 2.25F, 7.0F}}};
    const rt::Rows rows = rt::object_to_world_rows(matrix);
    for (const std::array<float, 3>& point : points) {
        expect(near3(rt::apply_rows(rows, point), d3d_transform(matrix, point)), "object_to_world_rows agrees with p * M");
    }
    bool moved = true;
    for (std::size_t i = 0; i < 3; ++i) {
        moved = moved && cell(rows, i, 3) == core::at(matrix, kTranslation + i);
    }
    expect(moved, "the translation in the rows' last column");
    expect(rt::object_to_world_rows(identity) == rt::identity_rows(), "the identity");
    rt::Rows other = rows;
    cell(other, 1, 3) += kTiny;
    expect(rt::same_rows(rows, other), "same_rows within the tolerance");
    cell(other, 1, 3) += kMove;
    expect(!rt::same_rows(rows, other), "same_rows tells a moved transform");
    other = rows;
    cell(other, 2, 1) = std::nanf("");
    expect(!rt::finite_rows(other) && rt::finite_rows(rows), "finite_rows");
    expect(!rt::same_rows(other, other), "a NaN is never the same");
}

struct CountCase {
    uint32_t indices;
    bool strip;
    uint32_t triangles;
};

void check_strips()
{
    const std::array<CountCase, 6> counts = {{{.indices = 0, .strip = true, .triangles = 0},
                                              {.indices = 2, .strip = true, .triangles = 0},
                                              {.indices = 3, .strip = true, .triangles = 1},
                                              {.indices = 10, .strip = true, .triangles = 8},
                                              {.indices = 9, .strip = false, .triangles = 3},
                                              {.indices = 10, .strip = false, .triangles = 3}}};
    for (const CountCase& count : counts) {
        expect(rt::triangle_count(count.indices, count.strip) == count.triangles, "a draw's triangle count");
    }
    // Two strips joined by repeating 3 and 4: (0 1 2) (2 1 3) then the joins, then (4 5 6) (6 5 7).
    const std::array<uint16_t, 10> strip = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    const std::vector<uint32_t> wanted = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    std::vector<uint32_t> list;
    const uint32_t kept = rt::strip_to_list<uint16_t>(strip, list);
    expect(kept == wanted.size() / 3, "the joins' degenerate triangles left out");
    expect(list == wanted, "every second triangle turned back");
    std::vector<uint32_t> none;
    expect(rt::strip_to_list<uint32_t>(std::span<const uint32_t>(), none) == 0 && none.empty(), "an empty strip");
}

void check_geometry()
{
    render::GpuVertexBuffer vertex_buffer{};
    render::GpuIndexBuffer index_buffer{};
    const std::array<uint16_t, 6> indices = {0, 1, 2, 3, 2, 1};
    const rt::Geometry strip{.vertices = &vertex_buffer,
                             .vertex_offset = 64,
                             .vertex_stride = 24,
                             .vertex_count = 4,
                             .position_offset = 0,
                             .indices = &index_buffer,
                             .index_offset = 0,
                             .index_count = 6,
                             .indices_32bit = false,
                             .strip = true,
                             .dynamic = false,
                             .vertex_buffer_bytes = 160,
                             .index_buffer_bytes = 12,
                             .cpu_indices = indices.data()};
    expect(rt::geometry_problem(strip).empty(), "a good mesh passes");
    rt::Geometry good = strip;
    good.strip = false;
    expect(rt::geometry_problem(good).empty(), "a list of 6 passes");
    rt::Geometry bad = good;
    bad.index_count = good.index_count - 1;
    expect(!rt::geometry_problem(bad).empty(), "a list of 5 indices");
    bad = good;
    bad.vertex_buffer_bytes = good.vertex_buffer_bytes - 1;
    expect(!rt::geometry_problem(bad).empty(), "vertices past the buffer");
    bad = good;
    bad.index_buffer_bytes = good.index_buffer_bytes - sizeof(uint16_t);
    expect(!rt::geometry_problem(bad).empty(), "indices past the buffer");
    bad = good;
    bad.vertex_count = good.vertex_count - 1;
    bad.vertex_buffer_bytes = 0;
    expect(!rt::geometry_problem(bad).empty(), "an index past the vertices");
    bad = good;
    bad.position_offset = good.vertex_stride - sizeof(float);
    expect(!rt::geometry_problem(bad).empty(), "a position past the vertex");
    bad = good;
    bad.vertices = nullptr;
    expect(!rt::geometry_problem(bad).empty(), "no vertex buffer");
    bad = good;
    bad.vertex_buffer_bytes = 0;
    bad.index_buffer_bytes = 0;
    bad.cpu_indices = nullptr;
    expect(rt::geometry_problem(bad).empty(), "sizes not known: not checked");
}

void check_flags()
{
    expect(rt::flags_problem(rt::kWorld | rt::kOpaque | rt::kCastsShadow).empty(), "an opaque world mesh");
    expect(rt::flags_problem(rt::kObject | rt::kSkinned | rt::kAlphaTested | rt::kCastsShadow).empty(),
           "an alpha-tested skinned object");
    expect(rt::flags_problem(rt::kObject | rt::kTransparent).empty(), "a transparent object");
    expect(!rt::flags_problem(rt::kOpaque).empty(), "neither world nor object");
    expect(!rt::flags_problem(rt::kWorld | rt::kObject | rt::kOpaque).empty(), "world and object");
    expect(!rt::flags_problem(rt::kWorld).empty(), "no surface kind");
    expect(!rt::flags_problem(rt::kWorld | rt::kOpaque | rt::kTransparent).empty(), "opaque and transparent");
    expect(!rt::flags_problem(rt::kWorld | rt::kSkinned | rt::kOpaque).empty(), "a skinned world mesh");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_transforms();
    check_strips();
    check_geometry();
    check_flags();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("rt_scene_check: all checks passed\n");
    return 0;
}
