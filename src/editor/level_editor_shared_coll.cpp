// NOT ORIGINAL - the level editor's shared collision (ours, added at the user's request). See
// level_editor_shared_coll.h.

#include "editor/level_editor_shared_coll.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/checksums.h"
#include "engine/memory/mem.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/mesh_records.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_tri.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene.h"
#include "base/core/down_cast.h"
#include "engine/gfx/vertex_view.h"
#include "base/core/checked_index.h"
#include "engine/render/coll_obj.h"
#include "formats/collision_format.h"

namespace frontend::level_editor::shared_collision {

namespace {

constexpr float kDegenerateNormalSquared =
    1e-12F;                                  // a normal this small (relative to the edges) means a line or a point
constexpr std::size_t kFaceRecordBytes = 10; // one face's stored record
constexpr int kUnusableCollisionFlags = 6; // collision flag bits of a sector that can't be edited

} // namespace

// --- the pieces (pure) ------------------------------------------------------------------------------------------

namespace {

float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }

float segment_distance(V3 point, V3 a, V3 b)
{
    const V3 ab = b - a;
    const float len2 = get_dot(ab, ab);
    const float fraction = len2 > 0.0F ? std::clamp(get_dot(point - a, ab) / len2, 0.0F, 1.0F) : 0.0F;
    return len_of(point - (a + ab * fraction));
}

} // namespace

float point_triangle_distance(V3 point, V3 a, V3 b, V3 corner_c)
{
    const V3 ab = b - a;
    const V3 ac = corner_c - a;
    const V3 normal = perpendicular(ab, ac);
    const float n2 = get_dot(normal, normal);
    const float scale = std::max({get_dot(ab, ab), get_dot(ac, ac), get_dot(corner_c - b, corner_c - b)});
    if (!(n2 > scale * scale * kDegenerateNormalSquared)) { // (degenerate: a line or a point)
        return std::min({segment_distance(point, a, b), segment_distance(point, b, corner_c),
                         segment_distance(point, a, corner_c)});
    }
    // Inside the triangle's prism: the distance to its plane; else to the nearest edge.
    const V3 ap = point - a;
    const V3 bp = point - b;
    const V3 cp = point - corner_c;
    const bool inside = get_dot(perpendicular(ab, ap), normal) >= 0.0F &&
                        get_dot(perpendicular(corner_c - b, bp), normal) >= 0.0F &&
                        get_dot(perpendicular(a - corner_c, cp), normal) >= 0.0F;
    if (inside) {
        return std::fabs(get_dot(ap, normal)) / std::sqrt(n2);
    }
    return std::min(
        {segment_distance(point, a, b), segment_distance(point, b, corner_c), segment_distance(point, corner_c, a)});
}

float strip_distance(V3 point, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
                     const uint16_t* indices, StripLimit strip_limit)
{
    const auto index_count = strip_limit.index_count_;
    auto best = strip_limit.best;
    auto at = [&](uint16_t index) {
        const auto floats = gpu::VertexView(positions, stride, vertex_count).float3(index);
        return V3{.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
    };
    const std::span<const uint16_t> index_run(indices, index_count);
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t index_a = core::at(index_run, i - 2);
        const uint16_t index_b = core::at(index_run, i - 1);
        const uint16_t index_c = core::at(index_run, i);
        if (index_a == index_b || index_b == index_c || index_a == index_c || index_a >= vertex_count ||
            index_b >= vertex_count || index_c >= vertex_count) {
            continue;
        }
        const V3 va = at(index_a);
        const V3 vb = at(index_b);
        const V3 vc = at(index_c);
        // (a quick reject: the triangle's box farther than the best so far)
        const float dx =
            std::max({std::min({va.x, vb.x, vc.x}) - point.x, point.x - std::max({va.x, vb.x, vc.x}), 0.0F});
        const float dy =
            std::max({std::min({va.y, vb.y, vc.y}) - point.y, point.y - std::max({va.y, vb.y, vc.y}), 0.0F});
        const float dz =
            std::max({std::min({va.z, vb.z, vc.z}) - point.z, point.z - std::max({va.z, vb.z, vc.z}), 0.0F});
        if ((dx * dx) + (dy * dy) + (dz * dz) >= best * best) {
            continue;
        }
        best = std::min(best, point_triangle_distance(point, va, vb, vc));
    }
    return best;
}

bool belongs(float own, float nearest_other) { return own <= kNear && own < nearest_other; }

FaceCopy read_face(const render::TriMeshSource* geometry, int face)
{
    FaceCopy copy{};
    copy.flags = geometry->face_flags(face);
    copy.terrain = geometry->face_terrain(face);
    for (int k = 0; k < 3; ++k) {
        const uint32_t vertex = geometry->face_vertex(face, k);
        maths::Vec4 v;
        geometry->read_stored_vertex(static_cast<int>(vertex), &v);
        core::at(copy.v, k) = {.x = v.x, .y = v.y, .z = v.z};
        core::at(copy.bytes, k) = geometry->vertex_bytes() ? core::at(geometry->vertex_byte_run(), vertex) : kNeutralVertexByte;
    }
    return copy;
}

Companion::Companion(uint32_t checksum, const std::vector<FaceCopy>& faces)
{
    const size_t count = std::min(faces.size(), static_cast<size_t>(kMostFaces));
    std::vector<uint8_t> face_records(count * kFaceRecordBytes);
    std::vector<uint8_t> bytes; // one per vertex
    for (size_t face_index = 0; face_index < count; ++face_index) {
        const FaceCopy& face = core::at(faces, face_index);
        const std::span<uint8_t> out = std::span<uint8_t>(face_records).subspan(face_index * 10, 10);
        std::memcpy(out.data(), &face.flags, 2);
        std::memcpy(out.subspan(2).data(), &face.terrain, 2);
        for (int k = 0; k < 3; ++k) {
            // (corners shared where exactly equal, with the same byte)
            size_t index = base_.size();
            for (size_t i = 0; i < base_.size(); ++i) {
                if (core::at(base_, i).x == core::at(face.v, k).x && core::at(base_, i).y == core::at(face.v, k).y && core::at(base_, i).z == core::at(face.v, k).z &&
                    core::at(bytes, i) == core::at(face.bytes, k)) {
                    index = i;
                    break;
                }
            }
            if (index == base_.size()) {
                base_.push_back(core::at(face.v, k));
                bytes.push_back(core::at(face.bytes, k));
            }
            auto index16 = static_cast<uint16_t>(index);
            std::memcpy(out.subspan(4 + (2 * static_cast<size_t>(k))).data(), &index16, 2);
        }
    }
    rejection_.assign((count != 0U) ? count : 1, 0);
    if (bytes.empty()) {
        bytes.push_back(0);
    }

    // The block (see the header): the record, the faces, the vertices (4-aligned) and the vertex bytes. The vector's
    // storage (operator new's) is aligned for any fundamental type.
    static_assert(alignof(render::TriMeshSource) <= alignof(std::max_align_t) && alignof(float) == 4);
    constexpr size_t kFacesAt = sizeof(render::TriMeshSource);
    const size_t vertices_at = (kFacesAt + face_records.size() + 3) & ~size_t{3};
    const size_t bytes_at = vertices_at + (base_.size() * 3 * sizeof(float));
    block_.resize(bytes_at + bytes.size());
    const std::span<uint8_t> block(block_);
    geometry_ = collision_format::record_in_block<render::TriMeshSource>(block);
    faces_ = block.subspan(kFacesAt, face_records.size());
    const std::span<uint8_t> vertex_bytes = block.subspan(bytes_at, bytes.size());
    std::ranges::copy(face_records, faces_.begin());
    std::ranges::copy(bytes, vertex_bytes.begin());

    geometry_->set_checksum(checksum);
    geometry_->set_unknown04(0);
    geometry_->set_num_vertices(static_cast<uint16_t>(base_.size()));
    geometry_->set_tri_count(static_cast<uint16_t>(count));
    geometry_->set_small_faces(false);
    geometry_->set_compressed(false);
    geometry_->set_faces(faces_.empty() ? nullptr : faces_.data());
    geometry_->set_vertices(base_.empty() ? nullptr : block.subspan(vertices_at).data());
    geometry_->set_bsp(nullptr);
    geometry_->set_vertex_bytes(vertex_bytes.data());
    geometry_->set_unknown3c(0); // (the rejection scale 0.0f: no face rejected by distance)
    vertices_ = geometry_->float_vertices();

    entity_.assign_checksum(checksum); // (a sector's collision object, no flags)
    entity_.set_visit_stamp(0);
    entity_.set_geometry(geometry_);
    entity_.set_octets(rejection_.data());
    entity_.set_unknown14(0);
    get_place({.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}});
}

void Companion::get_place(const Placement& placement)
{
    for (size_t i = 0; i < base_.size(); ++i) {
        const V3 v = get_apply(placement, core::at(base_, i));
        core::at(vertices_, i * 3) = v.x;
        core::at(vertices_, (i * 3) + 1) = v.y;
        core::at(vertices_, (i * 3) + 2) = v.z;
    }
    update_box();
}

void Companion::rebase()
{
    for (size_t i = 0; i < base_.size(); ++i) {
        core::at(base_, i) = get_vertex(static_cast<int>(i));
    }
}

V3 Companion::get_vertex(int index) const
{
    return {.x = core::at(vertices_, index * 3),
            .y = core::at(vertices_, (index * 3) + 1),
            .z = core::at(vertices_, (index * 3) + 2)};
}

std::vector<FaceCopy> Companion::get_faces() const
{
    std::vector<FaceCopy> faces(geometry_->tri_count());
    for (int face_index = 0; std::cmp_less(face_index, geometry_->tri_count()); ++face_index) {
        core::at(faces, face_index) = read_face(geometry_, face_index);
    }
    return faces;
}

void Companion::change_live(bool active)
{
    entity_.flag_bits() = static_cast<uint16_t>(active ? entity_.flag_bits() & ~4U : entity_.flag_bits() | 4U);
}

bool Companion::live_now() const { return (entity_.flag_bits() & 4) == 0; }

void Companion::change_tri_flags(int face, render::FaceFlag flags)
{
    if (face >= 0 && std::cmp_less(face, geometry_->tri_count())) {
        std::memcpy(&core::at(faces_, static_cast<size_t>(face) * kFaceRecordBytes), &flags, 2);
    }
}

void Companion::update_box()
{
    // (as the loaded geometries keep it: w = 1 - Translate moves w by the delta's 0)
    V3 lo = base_.empty() ? V3{.x = 0, .y = 0, .z = 0} : get_vertex(0);
    V3 hi = lo;
    for (size_t i = 1; i < base_.size(); ++i) {
        const V3 v = get_vertex(static_cast<int>(i));
        lo = {.x = std::min(lo.x, v.x), .y = std::min(lo.y, v.y), .z = std::min(lo.z, v.z)};
        hi = {.x = std::max(hi.x, v.x), .y = std::max(hi.y, v.y), .z = std::max(hi.z, v.z)};
    }
    geometry_->bounds().min = {.x = lo.x, .y = lo.y, .z = lo.z, .w = 1.0F};
    geometry_->bounds().max = {.x = hi.x, .y = hi.y, .z = hi.z, .w = 1.0F};
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

render::Geom* geom_of(render::WorldCell* sector) { return static_cast<render::Geom*>(sector->geom_handle()); }
render::StaticCollider* coll_of(render::WorldCell* sector)
{
    return core::down_cast<render::StaticCollider>(sector->hit_body());
}
void* grid_of(render::WorldScene* scene) { return scene->coll_grid(); }

// A face of a collision-only sector turned off for a companion, and its flags before.
struct Taken {
    render::TriMeshSource* geometry;
    int face;
    render::FaceFlag flags;
};

struct TableRow {
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = nullptr;
    render::Geom* geom = nullptr;
    void* grid = nullptr;
    std::unique_ptr<Companion> companion; // (null: nothing belongs to it)
    std::vector<Taken> taken;
    bool in_grid_ = false;
    bool moved = false;
};
std::map<uint32_t, TableRow>& entries()
{
    static std::map<uint32_t, TableRow> value;
    return value;
}
uint32_t& generation()
{
    static uint32_t generation{}; // (faces taken or given back: the collision view's cache made again)
    return generation;
}

bool is_switched_on()
{
    static const bool kEnabled = config_int("editor", "shared_collision", 1) != 0;
    return kEnabled;
}

// A piece's triangles as drawn: its meshes' first index buffer as a strip (billboards left out, as picking does).
template <typename F>
void for_each_strip(render::Geom* geom, F fn)
{
    const scenery::GeomMeshes meshes = scenery::meshes_of(geom);
    for (uint32_t i = 0; i < meshes.count && i < kMeshScanLimit; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        if (!mesh || mesh->billboard() || (mesh->flags() & kMeshHiddenFlag)) {
            continue;
        }
        const render::VertexBufferData* vertices =
            mesh->vertex_buffer_count() ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        const render::IndexBufferData* indices = core::at(mesh->index_buffers(), 0);
        if (!vertices || !vertices->data() || !indices || !indices->indices() || mesh->stride() < kPositionBytes) {
            continue;
        }
        fn(vertices->data(), static_cast<uint32_t>(mesh->stride()), static_cast<uint32_t>(mesh->vertex_count()),
           indices->indices(), static_cast<uint32_t>(core::at(mesh->index_counts(), 0)));
    }
}

float geom_distance(render::Geom* geom, V3 point, float best)
{
    for_each_strip(geom, [&](const uint8_t* positions, uint32_t stride, uint32_t vertex_count, const uint16_t* indices,
                             uint32_t index_count) {
        best = strip_distance(point, positions, stride, vertex_count, indices,
                              {.index_count_ = index_count, .best = best});
    });
    return best;
}

// The farthest of a face's corners from a piece, found no farther than `limit` (past it: the limit's next float).
float face_distance(render::Geom* geom, const FaceCopy& face, float limit)
{
    float worst = 0.0F;
    const float over = std::nextafter(limit, std::numeric_limits<float>::infinity());
    for (const V3& v : face.v) {
        const float distance = geom_distance(geom, v, over);
        if (distance > limit) {
            return over;
        }
        worst = std::max(worst, distance);
    }
    return worst;
}

bool get_inside(V3 point, V3 min, V3 max)
{
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y && point.z >= min.z &&
           point.z <= max.z;
}
bool face_inside(const FaceCopy& face, V3 min, V3 max)
{
    return get_inside(core::at(face.v, 0), min, max) && get_inside(core::at(face.v, 1), min, max) && get_inside(core::at(face.v, 2), min, max);
}

// Where a piece is now, grown by `grow` (false: no box).
bool current_bounds(checksums::Name sector, float grow, V3* min, V3* max)
{
    const uint32_t checksum = sector.value();
    OrientedBox box{};
    if (!scenery::cuboid(checksum, &box)) {
        return false;
    }
    bounds_of(box, min, max);
    *min = *min - V3{.x = grow, .y = grow, .z = grow};
    *max = *max + V3{.x = grow, .y = grow, .z = grow};
    return true;
}

// A collision-only sector that can give faces: no geometry, collision on with geometry.
render::TriMeshSource* get_collision_only(render::WorldCell* sector)
{
    if (!sector || geom_of(sector)) {
        return nullptr;
    }
    const render::StaticCollider* coll = coll_of(sector);
    if (!coll || ((coll->flag_bits() & kUnusableCollisionFlags) != 0) || !coll->geometry() ||
        !coll->geometry()->faces()) {
        return nullptr;
    }
    return coll->geometry();
}

const char* get_name_of(uint32_t checksum)
{
    if (const char* name = script::kept_qb_name(checksum)) {
        return name;
    }
    return script::look_up_hash_name(checksum);
}

// The collision objects named after a piece (<name>_col and the like) that exist as collision-only sectors.
std::vector<uint32_t> named_for(uint32_t checksum)
{
    static const auto kSuffixes = std::to_array<const char*>({"_col", "_ic", "_col01", "_ic01"});
    std::vector<uint32_t> found;
    const char* name = get_name_of(checksum);
    if (!name || (*name == '\0')) {
        return found;
    }
    const uint32_t base = checksums::hash_from_text(name);
    if (base != checksum) { // (a name that isn't the piece's own)
        return found;
    }
    for (const char* suffix : kSuffixes) {
        const uint32_t named = checksums::fold_text_into_checksum(base, suffix);
        if (get_collision_only(scenery::sector_of(named))) {
            found.push_back(named);
        }
    }
    return found;
}

// Every collision object named after some piece of the level (kept per level: its main scene and sector count).
const std::map<uint32_t, uint32_t>& get_named_objects()
{
    static std::map<uint32_t, uint32_t> named;
    static const render::WorldScene* scene = nullptr;
    static int sectors = -1;
    int count = 0;
    scenery::for_each_level_sector([](void* context, uint32_t, render::WorldCell*) { ++*static_cast<int*>(context); },
                                   &count);
    if (scene == scenery::main_scene() && sectors == count) {
        return named;
    }
    scene = scenery::main_scene();
    sectors = count;
    named.clear();
    std::vector<uint32_t> pieces;
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            if (geom_of(sector)) {
                static_cast<std::vector<uint32_t>*>(context)->push_back(checksum);
            }
        },
        &pieces);
    for (const uint32_t piece : pieces) {
        for (const uint32_t object : named_for(piece)) {
            named.emplace(object, piece);
        }
    }
    return named;
}

struct Other {
    uint32_t checksum;
    render::Geom* geom;
    V3 min, max;
};

// A face of no piece's name near piece `geom`'s own: inside its box, near its own triangles, and nearer them than
// any other solid piece near it.
bool belongs_here(const FaceCopy& face, render::Geom* geom, V3 min, V3 max, const std::vector<Other>& others)
{
    if (!face_inside(face, min, max)) {
        return false;
    }
    const float own = face_distance(geom, face, kNear);
    if (!(own <= kNear)) {
        return false;
    }
    float nearest = std::numeric_limits<float>::infinity();
    for (const Other& other : others) {
        if (!face_inside(face, other.min, other.max)) {
            continue;
        }
        nearest = std::min(nearest, face_distance(other.geom, face, own));
        if (!belongs(own, nearest)) {
            break;
        }
    }
    return belongs(own, nearest);
}

// The faces that belong to piece `checksum` (see the header) and, when `taken` is given, where they came from.
std::vector<FaceCopy> gather(uint32_t checksum, std::vector<Taken>* taken)
{
    std::vector<FaceCopy> faces;
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::Geom* geom = sector ? geom_of(sector) : nullptr;
    V3 min{};
    V3 max{};
    if (!geom || scenery::get_see_through(checksum) || !current_bounds(checksums::Name{checksum}, kNear, &min, &max)) {
        return faces;
    }
    const auto& named_objects = get_named_objects();
    std::vector<uint32_t> mine = named_for(checksum);

    // The other solid pieces near it (the editor's copies left out: they'd tie with their sources).
    struct CtxInfo {
        uint32_t self;
        V3 min, max;
        std::vector<Other> others;
        std::vector<std::pair<uint32_t, render::WorldCell*>> sources;
    } context{.self = checksum, .min = min, .max = max, .others = {}, .sources = {}};
    scenery::for_each_level_sector(
        [](void* raw, uint32_t other, render::WorldCell* sector) {
            auto* info = static_cast<CtxInfo*>(raw);
            if (get_collision_only(sector)) {
                info->sources.emplace_back(other, sector);
                return;
            }
            render::Geom* geom = geom_of(sector);
            if (other == info->self || !geom || scenery::is_clone(other) || scenery::get_see_through(other)) {
                return;
            }
            V3 lo{};
            V3 hi{};
            if (!current_bounds(checksums::Name{other}, kNear, &lo, &hi)) {
                return;
            }
            if (lo.x > info->max.x || hi.x < info->min.x || lo.y > info->max.y || hi.y < info->min.y ||
                lo.z > info->max.z || hi.z < info->min.z) {
                return;
            }
            info->others.push_back({.checksum = other, .geom = geom, .min = lo, .max = hi});
        },
        &context);

    for (const auto& source : context.sources) {
        render::TriMeshSource* geometry = get_collision_only(source.second);
        const maths::Vec4& lo = geometry->bounds().min;
        const maths::Vec4& hi = geometry->bounds().max;
        if (lo.x > max.x || hi.x < min.x || lo.y > max.y || hi.y < min.y || lo.z > max.z || hi.z < min.z) {
            continue;
        }
        const bool named = std::ranges::find(mine, source.first) != mine.end();
        if (!named && named_objects.contains(source.first)) {
            continue; // (named after another piece: that one's)
        }
        const V3 named_min = min - V3{.x = 100.0F, .y = 100.0F, .z = 100.0F};
        const V3 named_max = max + V3{.x = 100.0F, .y = 100.0F, .z = 100.0F};
        for (int face_index = 0;
             std::cmp_less(face_index, geometry->tri_count()) && faces.size() < static_cast<size_t>(kMostFaces);
             ++face_index) {
            const FaceCopy face = read_face(geometry, face_index);
            if (!takeable(face.flags)) {
                continue;
            }
            if (named) {
                if (!face_inside(face, named_min, named_max)) {
                    continue;
                }
            } else {
                if (!belongs_here(face, geom, min, max, context.others)) {
                    continue;
                }
            }
            faces.push_back(face);
            if (taken) {
                taken->push_back({.geometry = geometry, .face = face_index, .flags = face.flags});
            }
        }
    }
    return faces;
}

// A list of collision objects for CollGrid::tick_frame (lists::Foremost / lists::Elem<HitMesh>, on the stack: Update only reads
// them).
struct GridList {
    // The fields other code reads and sets.
    memory::ListNode& head() { return head_; }
    [[nodiscard]] const memory::ListNode& head() const { return head_; }

private:
    memory::ListNode head_{};
    std::vector<memory::ListNode> nodes;

public:

    explicit GridList(const std::vector<render::HitMesh*>& objects) : nodes(objects.size())
    {
        head_.make_head();
        for (size_t i = 0; i < objects.size(); ++i) {
            memory::ListNode& node = core::at(nodes, i);
            node.set_data(core::at(objects, i));
            node.set_priority(0);
            node.set_prev(head_.prev());
            node.set_next(&head_);
            head_.prev()->set_next(&node);
            head_.set_prev(&node);
        }
    }
    GridList(const GridList&) = delete;
    GridList& operator=(const GridList&) = delete;
    ~GridList() = default;
};

// The grid brought up to date for these objects (the top-down heap current, as UpdateSuperSectors has it).
void tick_grid(void* grid, const std::vector<render::HitMesh*>& added, const std::vector<render::HitMesh*>& removed,
               const std::vector<render::HitMesh*>& moved)
{
    if (!grid || (added.empty() && removed.empty() && moved.empty())) {
        return;
    }
    GridList added_list(added);
    GridList removed_list(removed);
    GridList moved_list(moved);
    memory::enter_heap_scope(memory::top_down_heap());
    static_cast<render::CollGrid*>(grid)->tick_frame(&added_list.head(), &removed_list.head(), &moved_list.head());
    memory::leave_heap_scope();
}

bool get_live(const TableRow& entry, uint32_t checksum)
{
    return scenery::scene_live(entry.scene) && grid_of(entry.scene) == entry.grid &&
           scenery::sector_of(checksum) == entry.sector && geom_of(entry.sector) == entry.geom;
}

// An entry dropped: out of its grid and the originals back on while its piece is still there (a level gone: its grid
// and geometries are gone with it - nothing touched); out of every collision cache.
void get_drop(const std::map<uint32_t, TableRow>::iterator& it)
{
    const TableRow& entry = it->second;
    if (entry.companion) {
        if (get_live(entry, it->first)) {
            if (entry.in_grid_) {
                tick_grid(entry.grid, {}, {entry.companion->get_object()}, {});
            }
            for (const Taken& taken : entry.taken) {
                taken.geometry->set_face_flags(taken.face, taken.flags);
            }
        }
        render::remove_from_caches(entry.companion->get_object());
        ++generation();
    }
    entries().erase(it);
}

TableRow* get_find(uint32_t checksum)
{
    auto it = entries().find(checksum);
    if (it == entries().end()) {
        return nullptr;
    }
    if (!get_live(it->second, checksum)) {
        get_drop(it);
        return nullptr;
    }
    return &it->second;
}

void append_one(uint32_t checksum, render::WorldScene* scene, render::WorldCell* sector, render::Geom* geom,
                const std::vector<FaceCopy>& faces, std::vector<Taken> taken)
{
    TableRow& entry = entries()[checksum];
    entry.scene = scene;
    entry.sector = sector;
    entry.geom = geom;
    entry.grid = grid_of(scene);
    if (!faces.empty()) {
        entry.companion = std::make_unique<Companion>(checksum, faces);
        static_cast<void>(entry.companion->get_object()->geometry()->set_up_all_faces()); // (the shared no-tree face list, 0x0067C6B8)
        for (const Taken& taken_face : taken) {
            taken_face.geometry->set_face_flags(taken_face.face, taken_face.flags | render::FaceFlag::kNotCollidable);
        }
        entry.taken = std::move(taken);
        ++generation();
    }
}

} // namespace

void ensure(uint32_t checksum, render::WorldScene* scene, render::WorldCell* sector, render::Geom* geom)
{
    if (!is_switched_on() || get_find(checksum)) {
        return;
    }
    std::vector<Taken> taken;
    const std::vector<FaceCopy> faces = gather(checksum, &taken);
    if (debug_enabled() || !faces.empty()) {
        get_log("level editor: shared collision of %08X: %u face%s taken along", checksum,
                static_cast<unsigned>(faces.size()), faces.size() == 1 ? "" : "s");
    }
    append_one(checksum, scene, sector, geom, faces, std::move(taken));
}

void get_place(uint32_t checksum, const Placement& placement)
{
    TableRow* entry = get_find(checksum);
    if (!entry || !entry->companion) {
        return;
    }
    entry->companion->get_place(placement);
    entry->moved = true;
}

void rebase(uint32_t checksum)
{
    const TableRow* entry = get_find(checksum);
    if (entry && entry->companion) {
        entry->companion->rebase();
    }
}

void change_live(uint32_t checksum, bool active)
{
    const TableRow* entry = get_find(checksum);
    if (entry && entry->companion) {
        entry->companion->change_live(active);
    }
}

std::shared_ptr<std::vector<FaceCopy>> prepare_copy(uint32_t source)
{
    if (!is_switched_on()) {
        return nullptr;
    }
    if (const TableRow* entry = get_find(source)) {
        return entry->companion ? std::make_shared<std::vector<FaceCopy>>(entry->companion->get_faces()) : nullptr;
    }
    auto faces = std::make_shared<std::vector<FaceCopy>>(gather(source, nullptr));
    return faces->empty() ? nullptr : faces;
}

void finish_copy(const std::shared_ptr<std::vector<FaceCopy>>& faces, uint32_t copy, render::WorldScene* scene,
                 render::WorldCell* sector, render::Geom* geom)
{
    if (!faces || faces->empty() || !scene || !sector || !geom) {
        return;
    }
    auto it = entries().find(copy);
    if (it != entries().end()) {
        get_drop(it);
    }
    append_one(copy, scene, sector, geom, *faces, {});
    if (debug_enabled()) {
        get_log("level editor: shared collision of copy %08X: %u faces", copy, static_cast<unsigned>(faces->size()));
    }
}

void finalise(render::WorldScene* scene)
{
    std::vector<render::HitMesh*> added;
    std::vector<render::HitMesh*> moved;
    void* grid = nullptr;
    for (auto it = entries().begin(); it != entries().end();) {
        auto next = std::next(it);
        if (!get_live(it->second, it->first)) {
            get_drop(it);
        } else if (it->second.scene == scene && it->second.companion) {
            TableRow& entry = it->second;
            grid = entry.grid;
            if (!entry.in_grid_) {
                added.push_back(entry.companion->get_object());
            } else if (entry.moved) {
                moved.push_back(entry.companion->get_object());
            }
            entry.in_grid_ = true;
            entry.moved = false;
        }
        it = next;
    }
    tick_grid(grid, added, {}, moved);
}

int total_of(int* faces)
{
    int pieces = 0;
    int total = 0;
    for (const auto& entry : entries()) {
        if (entry.second.companion) {
            ++pieces;
            total += entry.second.companion->face_count();
        }
    }
    if (faces) {
        *faces = total;
    }
    return pieces;
}

void for_each_companion(void (*each)(void* context, uint32_t checksum, const Companion& companion), void* context)
{
    for (auto it = entries().begin(); it != entries().end();) {
        auto next = std::next(it);
        if (!get_live(it->second, it->first)) {
            get_drop(it); // (its level gone: forgotten, nothing touched)
        } else if (it->second.companion && it->second.companion->live_now()) {
            each(context, it->first, *it->second.companion);
        }
        it = next;
    }
}

std::vector<int> taken_faces(const render::TriMeshSource* geometry)
{
    std::vector<int> faces;
    for (const auto& entry : entries()) {
        for (const Taken& taken : entry.second.taken) {
            if (taken.geometry == geometry) {
                faces.push_back(taken.face);
            }
        }
    }
    return faces;
}

uint32_t get_generation() { return generation(); }

bool taken_face(const render::TriMeshSource* geometry, int face, uint32_t* piece, int* index, render::FaceFlag* flags)
{
    for (const auto& entry : entries()) {
        const std::vector<Taken>& taken = entry.second.taken;
        for (size_t i = 0; i < taken.size(); ++i) {
            if (core::at(taken, i).geometry == geometry && core::at(taken, i).face == face) {
                if (piece) {
                    *piece = entry.first;
                }
                if (index) {
                    *index = static_cast<int>(i);
                }
                if (flags) {
                    *flags = core::at(taken, i).flags;
                }
                return true;
            }
        }
    }
    return false;
}

bool origin_of(checksums::Name piece_name, int index, render::TriMeshSource** geometry, int* face)
{
    const uint32_t piece = piece_name.value();
    TableRow* entry = get_find(piece);
    // (the companion's faces are the taken ones, in order - Gather / Add; a copy's took none)
    if (!entry || !entry->companion || index < 0 || std::cmp_greater_equal(index, entry->taken.size())) {
        return false;
    }
    *geometry = core::at(entry->taken, index).geometry;
    *face = core::at(entry->taken, index).face;
    return true;
}

const Companion* companion_of(uint32_t piece)
{
    const TableRow* entry = get_find(piece);
    return entry ? entry->companion.get() : nullptr;
}

bool set_taken_flags(const render::TriMeshSource* geometry, FaceFlags face_flags)
{
    const auto face = face_flags.face;
    const auto flags = face_flags.flags;
    for (auto& entry : entries()) {
        std::vector<Taken>& taken = entry.second.taken;
        for (size_t i = 0; i < taken.size(); ++i) {
            if (core::at(taken, i).geometry != geometry || core::at(taken, i).face != face) {
                continue;
            }
            core::at(taken, i).flags = flags;
            if (entry.second.companion) {
                entry.second.companion->change_tri_flags(static_cast<int>(i), flags);
            }
            core::at(taken, i).geometry->set_face_flags(core::at(taken, i).face, flags | render::FaceFlag::kNotCollidable);
            ++generation();
            return true;
        }
    }
    return false;
}

} // namespace frontend::level_editor::shared_collision
