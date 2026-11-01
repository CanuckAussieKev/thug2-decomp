// NOT ORIGINAL - the level editor's shared collision (ours, added at the user's request). See
// level_editor_shared_coll.h.

#include "editor/level_editor_shared_coll.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/engine_scene.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::shared_collision {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

namespace {

float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }

float segment_distance(V3 p, V3 a, V3 b)
{
    const V3 ab = b - a;
    const float len2 = get_dot(ab, ab);
    const float t = len2 > 0.0f ? std::clamp(get_dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return len_of(p - (a + ab * t));
}

} // namespace

float point_triangle_distance(V3 p, V3 a, V3 b, V3 c)
{
    const V3 ab = b - a;
    const V3 ac = c - a;
    const V3 n = perpendicular(ab, ac);
    const float n2 = get_dot(n, n);
    const float scale = std::max({get_dot(ab, ab), get_dot(ac, ac), get_dot(c - b, c - b)});
    if (!(n2 > scale * scale * 1e-12f)) // (degenerate: a line or a point)
        return std::min({segment_distance(p, a, b), segment_distance(p, b, c), segment_distance(p, a, c)});
    // Inside the triangle's prism: the distance to its plane; else to the nearest edge.
    const V3 ap = p - a;
    const V3 bp = p - b;
    const V3 cp = p - c;
    const bool inside = get_dot(perpendicular(ab, ap), n) >= 0.0f && get_dot(perpendicular(c - b, bp), n) >= 0.0f &&
                        get_dot(perpendicular(a - c, cp), n) >= 0.0f;
    if (inside)
        return std::fabs(get_dot(ap, n)) / std::sqrt(n2);
    return std::min({segment_distance(p, a, b), segment_distance(p, b, c), segment_distance(p, c, a)});
}

float strip_distance(V3 p, const uint8_t* positions, uint32_t stride, uint32_t vertex_count, const uint16_t* indices,
                     uint32_t index_count, float best)
{
    auto at = [&](uint16_t i) {
        const auto f = gpu::VertexView(positions, stride).float3(i);
        return V3{core::at(f, 0), core::at(f, 1), core::at(f, 2)};
    };
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t a = indices[i - 2];
        const uint16_t b = indices[i - 1];
        const uint16_t c = indices[i];
        if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count)
            continue;
        const V3 va = at(a);
        const V3 vb = at(b);
        const V3 vc = at(c);
        // (a quick reject: the triangle's box farther than the best so far)
        const float dx = std::max({std::min({va.x, vb.x, vc.x}) - p.x, p.x - std::max({va.x, vb.x, vc.x}), 0.0f});
        const float dy = std::max({std::min({va.y, vb.y, vc.y}) - p.y, p.y - std::max({va.y, vb.y, vc.y}), 0.0f});
        const float dz = std::max({std::min({va.z, vb.z, vc.z}) - p.z, p.z - std::max({va.z, vb.z, vc.z}), 0.0f});
        if (dx * dx + dy * dy + dz * dz >= best * best)
            continue;
        best = std::min(best, point_triangle_distance(p, va, vb, vc));
    }
    return best;
}

bool belongs(float own, float nearest_other) { return own <= kNear && own < nearest_other; }

FaceCopy read_face(const render::TriMeshSource* geometry, int face)
{
    FaceCopy copy{};
    const uint8_t* data = geometry->faces_ + face * (geometry->small_faces_ ? 8 : 10);
    std::memcpy(&copy.flags, data, 2);
    std::memcpy(&copy.terrain, data + 2, 2);
    for (int k = 0; k < 3; ++k) {
        const uint32_t vertex = geometry->face_vertex(face, k);
        maths::Vec4 v;
        geometry->read_stored_vertex(static_cast<int>(vertex), &v);
        core::at(copy.v, k) = {v.x, v.y, v.z};
        core::at(copy.bytes, k) = geometry->vertex_bytes_ ? geometry->vertex_bytes_[vertex] : 0x80;
    }
    return copy;
}

Companion::Companion(uint32_t checksum, const std::vector<FaceCopy>& faces)
{
    const size_t count = std::min(faces.size(), static_cast<size_t>(kMostFaces));
    faces_.resize(count * 10);
    for (size_t f = 0; f < count; ++f) {
        const FaceCopy& face = core::at(faces, f);
        uint8_t* out = &core::at(faces_, f * 10);
        std::memcpy(out, &face.flags, 2);
        std::memcpy(out + 2, &face.terrain, 2);
        for (int k = 0; k < 3; ++k) {
            // (corners shared where exactly equal, with the same byte)
            size_t index = base_.size();
            for (size_t i = 0; i < base_.size(); ++i) {
                if (core::at(base_, i).x == core::at(face.v, k).x && core::at(base_, i).y == core::at(face.v, k).y && core::at(base_, i).z == core::at(face.v, k).z &&
                    core::at(bytes_, i) == core::at(face.bytes, k)) {
                    index = i;
                    break;
                }
            }
            if (index == base_.size()) {
                base_.push_back(core::at(face.v, k));
                bytes_.push_back(core::at(face.bytes, k));
            }
            auto index16 = static_cast<uint16_t>(index);
            std::memcpy(out + 4 + 2 * k, &index16, 2);
        }
    }
    vertices_.resize(base_.size() * 3);
    rejection_.assign(count ? count : 1, 0);
    if (bytes_.empty())
        bytes_.push_back(0);

    geometry_.checksum_ = checksum;
    geometry_.unknown04_ = 0;
    geometry_.num_vertices_ = static_cast<uint16_t>(base_.size());
    geometry_.tri_count_ = static_cast<uint16_t>(count);
    geometry_.small_faces_ = false;
    geometry_.compressed_ = false;
    geometry_.faces_ = faces_.empty() ? nullptr : faces_.data();
    geometry_.vertices_ = vertices_.empty() ? nullptr : vertices_.data();
    geometry_.bsp_ = nullptr;
    geometry_.vertex_bytes_ = bytes_.data();
    geometry_.unknown3c_ = 0; // (the rejection scale 0.0f: no face rejected by distance)

    entity_.checksum_ = checksum; // (a sector's collision object, no flags)
    entity_.visit_stamp_ = 0;
    entity_.geometry_ = &geometry_;
    entity_.octets_ = rejection_.data();
    entity_.unknown14_ = 0;
    get_place({0, {0.0f, 0.0f, 0.0f}});
}

void Companion::get_place(const Placement& placement)
{
    for (size_t i = 0; i < base_.size(); ++i) {
        const V3 v = get_apply(placement, core::at(base_, i));
        core::at(vertices_, i * 3) = v.x;
        core::at(vertices_, i * 3 + 1) = v.y;
        core::at(vertices_, i * 3 + 2) = v.z;
    }
    update_box();
}

void Companion::rebase()
{
    for (size_t i = 0; i < base_.size(); ++i)
        core::at(base_, i) = get_vertex(static_cast<int>(i));
}

V3 Companion::get_vertex(int i) const { return {core::at(vertices_, i * 3), core::at(vertices_, i * 3 + 1), core::at(vertices_, i * 3 + 2)}; }

std::vector<FaceCopy> Companion::get_faces() const
{
    std::vector<FaceCopy> faces(geometry_.tri_count_);
    for (int f = 0; std::cmp_less(f, geometry_.tri_count_); ++f)
        core::at(faces, f) = read_face(&geometry_, f);
    return faces;
}

void Companion::change_live(bool active)
{
    entity_.flag_bits_ = static_cast<uint16_t>(active ? entity_.flag_bits_ & ~4u : entity_.flag_bits_ | 4u);
}

bool Companion::live_now() const { return !(entity_.flag_bits_ & 4); }

void Companion::change_tri_flags(int face, uint16_t flags)
{
    if (face >= 0 && std::cmp_less(face, geometry_.tri_count_))
        std::memcpy(&core::at(faces_, static_cast<size_t>(face) * 10), &flags, 2);
}

void Companion::update_box()
{
    // (as the loaded geometries keep it: w = 1 - Translate moves w by the delta's 0)
    V3 lo = base_.empty() ? V3{0, 0, 0} : get_vertex(0), hi = lo;
    for (size_t i = 1; i < base_.size(); ++i) {
        const V3 v = get_vertex(static_cast<int>(i));
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    geometry_.lower_limit_ = {lo.x, lo.y, lo.z, 1.0f};
    geometry_.upper_limit_ = {hi.x, hi.y, hi.z, 1.0f};
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

render::PcGeom* geom_of(render::WorldCell* sector) { return static_cast<render::PcGeom*>(sector->geom_handle_); }
render::StaticCollider* coll_of(render::WorldCell* sector)
{
    return core::down_cast<render::StaticCollider>(sector->hit_body_);
}
void* grid_of(render::WorldScene* scene) { return scene->coll_grid_; }

// A face of a collision-only sector turned off for a companion, and its flags before.
struct Taken {
    render::TriMeshSource* geometry;
    int face;
    uint16_t flags;
};

struct TableRow {
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = nullptr;
    render::PcGeom* geom = nullptr;
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
uint32_t g_generation; // (faces taken or given back: the collision view's cache made again)

bool is_switched_on()
{
    static const bool kEnabled = config_int("editor", "shared_collision", 1) != 0;
    return kEnabled;
}

// A piece's triangles as drawn: its meshes' first index buffer as a strip (billboards left out, as picking does).
template <typename F>
void for_each_strip(render::PcGeom* geom, F fn)
{
    auto* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        const render::MeshRef* mesh = meshes[i];
        if (!mesh || mesh->billboard || (mesh->flags & 0x10))
            continue;
        const render::VertexBuffer* vertices = mesh->vertex_buffer_count_ ? core::at(mesh->vertex_buffers_, 0) : nullptr;
        const render::IdxStore* indices = core::at(mesh->index_buffers_, 0);
        if (!vertices || !vertices->data || !indices || !indices->indices || mesh->stride < 12)
            continue;
        fn(vertices->data, static_cast<uint32_t>(mesh->stride), static_cast<uint32_t>(mesh->vertex_count_),
           indices->indices, static_cast<uint32_t>(core::at(mesh->index_counts_, 0)));
    }
}

float geom_distance(render::PcGeom* geom, V3 p, float best)
{
    for_each_strip(geom, [&](const uint8_t* positions, uint32_t stride, uint32_t vertex_count, const uint16_t* indices,
                             uint32_t index_count) {
        best = strip_distance(p, positions, stride, vertex_count, indices, index_count, best);
    });
    return best;
}

// The farthest of a face's corners from a piece, found no farther than `limit` (past it: the limit's next float).
float face_distance(render::PcGeom* geom, const FaceCopy& face, float limit)
{
    float worst = 0.0f;
    const float over = std::nextafter(limit, std::numeric_limits<float>::infinity());
    for (const V3& v : face.v) {
        const float d = geom_distance(geom, v, over);
        if (d > limit)
            return over;
        worst = std::max(worst, d);
    }
    return worst;
}

bool get_inside(V3 p, V3 min, V3 max)
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
}
bool face_inside(const FaceCopy& face, V3 min, V3 max)
{
    return get_inside(core::at(face.v, 0), min, max) && get_inside(core::at(face.v, 1), min, max) && get_inside(core::at(face.v, 2), min, max);
}

// Where a piece is now, grown by `grow` (false: no box).
bool current_bounds(uint32_t checksum, float grow, V3* min, V3* max)
{
    OrientedBox box{};
    if (!scenery::cuboid(checksum, &box))
        return false;
    bounds_of(box, min, max);
    *min = *min - V3{grow, grow, grow};
    *max = *max + V3{grow, grow, grow};
    return true;
}

// A collision-only sector that can give faces: no geometry, collision on with geometry.
render::TriMeshSource* get_collision_only(render::WorldCell* sector)
{
    if (!sector || geom_of(sector))
        return nullptr;
    const render::StaticCollider* coll = coll_of(sector);
    if (!coll || (coll->flag_bits_ & 6) || !coll->geometry_ || !coll->geometry_->faces_)
        return nullptr;
    return coll->geometry_;
}

const char* get_name_of(uint32_t checksum)
{
    if (const char* name = script::kept_qb_name(checksum))
        return name;
    return script::look_up_hash_name(checksum);
}

// The collision objects named after a piece (<name>_col and the like) that exist as collision-only sectors.
std::vector<uint32_t> named_for(uint32_t checksum)
{
    static const auto kSuffixes = std::to_array<const char*>({"_col", "_ic", "_col01", "_ic01"});
    std::vector<uint32_t> found;
    const char* name = get_name_of(checksum);
    if (!name || !*name)
        return found;
    const uint32_t base = checksums::hash_from_text(name);
    if (base != checksum) // (a name that isn't the piece's own)
        return found;
    for (const char* suffix : kSuffixes) {
        const uint32_t named = checksums::fold_text_into_checksum(base, suffix);
        if (get_collision_only(scenery::sector_of(named)))
            found.push_back(named);
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
    if (scene == scenery::main_scene() && sectors == count)
        return named;
    scene = scenery::main_scene();
    sectors = count;
    named.clear();
    std::vector<uint32_t> pieces;
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            if (geom_of(sector))
                static_cast<std::vector<uint32_t>*>(context)->push_back(checksum);
        },
        &pieces);
    for (const uint32_t piece : pieces) {
        for (const uint32_t object : named_for(piece))
            named.emplace(object, piece);
    }
    return named;
}

struct Other {
    uint32_t checksum;
    render::PcGeom* geom;
    V3 min, max;
};

// The faces that belong to piece `checksum` (see the header) and, when `taken` is given, where they came from.
std::vector<FaceCopy> gather(uint32_t checksum, std::vector<Taken>* taken)
{
    std::vector<FaceCopy> faces;
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::PcGeom* geom = sector ? geom_of(sector) : nullptr;
    V3 min{}, max{};
    if (!geom || scenery::get_see_through(checksum) || !current_bounds(checksum, kNear, &min, &max))
        return faces;
    const auto& named_objects = get_named_objects();
    std::vector<uint32_t> mine = named_for(checksum);

    // The other solid pieces near it (the editor's copies left out: they'd tie with their sources).
    struct CtxInfo {
        uint32_t self;
        V3 min, max;
        std::vector<Other> others;
        std::vector<std::pair<uint32_t, render::WorldCell*>> sources;
    } context{checksum, min, max, {}, {}};
    scenery::for_each_level_sector(
        [](void* raw, uint32_t other, render::WorldCell* sector) {
            auto* c = static_cast<CtxInfo*>(raw);
            if (get_collision_only(sector)) {
                c->sources.emplace_back(other, sector);
                return;
            }
            render::PcGeom* geom = geom_of(sector);
            if (other == c->self || !geom || scenery::is_clone(other) || scenery::get_see_through(other))
                return;
            V3 lo{}, hi{};
            if (!current_bounds(other, kNear, &lo, &hi))
                return;
            if (lo.x > c->max.x || hi.x < c->min.x || lo.y > c->max.y || hi.y < c->min.y || lo.z > c->max.z ||
                hi.z < c->min.z)
                return;
            c->others.push_back({other, geom, lo, hi});
        },
        &context);

    for (const auto& source : context.sources) {
        render::TriMeshSource* geometry = get_collision_only(source.second);
        const maths::Vec4& lo = geometry->lower_limit_;
        const maths::Vec4& hi = geometry->upper_limit_;
        if (lo.x > max.x || hi.x < min.x || lo.y > max.y || hi.y < min.y || lo.z > max.z || hi.z < min.z)
            continue;
        const bool named = std::ranges::find(mine, source.first) != mine.end();
        if (!named && named_objects.count(source.first))
            continue; // (named after another piece: that one's)
        const V3 named_min = min - V3{100.0f, 100.0f, 100.0f};
        const V3 named_max = max + V3{100.0f, 100.0f, 100.0f};
        for (int f = 0; std::cmp_less(f, geometry->tri_count_) && faces.size() < static_cast<size_t>(kMostFaces); ++f) {
            const FaceCopy face = read_face(geometry, f);
            if (!takeable(face.flags))
                continue;
            if (named) {
                if (!face_inside(face, named_min, named_max))
                    continue;
            } else {
                if (!face_inside(face, min, max))
                    continue;
                const float own = face_distance(geom, face, kNear);
                if (!(own <= kNear))
                    continue;
                float nearest = std::numeric_limits<float>::infinity();
                for (const Other& other : context.others) {
                    if (!face_inside(face, other.min, other.max))
                        continue;
                    nearest = std::min(nearest, face_distance(other.geom, face, own));
                    if (!belongs(own, nearest))
                        break;
                }
                if (!belongs(own, nearest))
                    continue;
            }
            faces.push_back(face);
            if (taken)
                taken->push_back({geometry, f, face.flags});
        }
    }
    return faces;
}

// A list of collision objects for CollGrid::tick_frame (lists::Foremost / lists::Elem<HitMesh>, on the stack: Update only reads
// them).
struct GridList {
    memory::ListNode head{};
    std::vector<memory::ListNode> nodes;

    explicit GridList(const std::vector<render::HitMesh*>& objects) : nodes(objects.size())
    {
        head.make_head();
        for (size_t i = 0; i < objects.size(); ++i) {
            memory::ListNode& node = core::at(nodes, i);
            node.data = core::at(objects, i);
            node.priority = 0;
            node.prev = head.prev;
            node.next = &head;
            head.prev->next = &node;
            head.prev = &node;
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
    if (!grid || (added.empty() && removed.empty() && moved.empty()))
        return;
    GridList added_list(added), removed_list(removed), moved_list(moved);
    memory::enter_heap_scope(memory::top_down_heap());
    static_cast<render::CollGrid*>(grid)->tick_frame(&added_list.head, &removed_list.head, &moved_list.head);
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
            if (entry.in_grid_)
                tick_grid(entry.grid, {}, {entry.companion->get_object()}, {});
            for (const Taken& t : entry.taken)
                t.geometry->set_face_flags(t.face, t.flags);
        }
        render::remove_from_caches(entry.companion->get_object());
        ++g_generation;
    }
    entries().erase(it);
}

TableRow* get_find(uint32_t checksum)
{
    auto it = entries().find(checksum);
    if (it == entries().end())
        return nullptr;
    if (!get_live(it->second, checksum)) {
        get_drop(it);
        return nullptr;
    }
    return &it->second;
}

void append_one(uint32_t checksum, render::WorldScene* scene, render::WorldCell* sector, render::PcGeom* geom,
                const std::vector<FaceCopy>& faces, std::vector<Taken> taken)
{
    TableRow& entry = entries()[checksum];
    entry.scene = scene;
    entry.sector = sector;
    entry.geom = geom;
    entry.grid = grid_of(scene);
    if (!faces.empty()) {
        entry.companion = std::make_unique<Companion>(checksum, faces);
        entry.companion->get_object()->geometry_->set_up_all_faces(); // (the shared no-tree face list, 0x0067C6B8)
        for (const Taken& t : taken)
            t.geometry->set_face_flags(t.face, static_cast<uint16_t>(t.flags | 0x10));
        entry.taken = std::move(taken);
        ++g_generation;
    }
}

} // namespace

void ensure(uint32_t checksum, render::WorldScene* scene, render::WorldCell* sector, render::PcGeom* geom)
{
    if (!is_switched_on() || get_find(checksum))
        return;
    std::vector<Taken> taken;
    const std::vector<FaceCopy> faces = gather(checksum, &taken);
    if (debug_enabled() || !faces.empty())
        get_log("level editor: shared collision of %08X: %u face%s taken along", checksum,
                static_cast<unsigned>(faces.size()), faces.size() == 1 ? "" : "s");
    append_one(checksum, scene, sector, geom, faces, std::move(taken));
}

void get_place(uint32_t checksum, const Placement& placement)
{
    TableRow* entry = get_find(checksum);
    if (!entry || !entry->companion)
        return;
    entry->companion->get_place(placement);
    entry->moved = true;
}

void rebase(uint32_t checksum)
{
    const TableRow* entry = get_find(checksum);
    if (entry && entry->companion)
        entry->companion->rebase();
}

void change_live(uint32_t checksum, bool active)
{
    const TableRow* entry = get_find(checksum);
    if (entry && entry->companion)
        entry->companion->change_live(active);
}

std::shared_ptr<std::vector<FaceCopy>> prepare_copy(uint32_t source)
{
    if (!is_switched_on())
        return nullptr;
    if (const TableRow* entry = get_find(source))
        return entry->companion ? std::make_shared<std::vector<FaceCopy>>(entry->companion->get_faces()) : nullptr;
    auto faces = std::make_shared<std::vector<FaceCopy>>(gather(source, nullptr));
    return faces->empty() ? nullptr : faces;
}

void finish_copy(const std::shared_ptr<std::vector<FaceCopy>>& faces, uint32_t copy, render::WorldScene* scene,
                 render::WorldCell* sector, render::PcGeom* geom)
{
    if (!faces || faces->empty() || !scene || !sector || !geom)
        return;
    auto it = entries().find(copy);
    if (it != entries().end())
        get_drop(it);
    append_one(copy, scene, sector, geom, *faces, {});
    if (debug_enabled())
        get_log("level editor: shared collision of copy %08X: %u faces", copy, static_cast<unsigned>(faces->size()));
}

void finalise(render::WorldScene* scene)
{
    std::vector<render::HitMesh*> added, moved;
    void* grid = nullptr;
    for (auto it = entries().begin(); it != entries().end();) {
        auto next = std::next(it);
        if (!get_live(it->second, it->first)) {
            get_drop(it);
        } else if (it->second.scene == scene && it->second.companion) {
            TableRow& entry = it->second;
            grid = entry.grid;
            if (!entry.in_grid_)
                added.push_back(entry.companion->get_object());
            else if (entry.moved)
                moved.push_back(entry.companion->get_object());
            entry.in_grid_ = true;
            entry.moved = false;
        }
        it = next;
    }
    tick_grid(grid, added, {}, moved);
}

int total_of(int* faces)
{
    int pieces = 0, total = 0;
    for (const auto& entry : entries()) {
        if (entry.second.companion) {
            ++pieces;
            total += entry.second.companion->face_count();
        }
    }
    if (faces)
        *faces = total;
    return pieces;
}

void for_each_companion(void (*each)(void* context, uint32_t checksum, const Companion& companion), void* context)
{
    for (auto it = entries().begin(); it != entries().end();) {
        auto next = std::next(it);
        if (!get_live(it->second, it->first))
            get_drop(it); // (its level gone: forgotten, nothing touched)
        else if (it->second.companion && it->second.companion->live_now())
            each(context, it->first, *it->second.companion);
        it = next;
    }
}

std::vector<int> taken_faces(const render::TriMeshSource* geometry)
{
    std::vector<int> faces;
    for (const auto& entry : entries()) {
        for (const Taken& t : entry.second.taken) {
            if (t.geometry == geometry)
                faces.push_back(t.face);
        }
    }
    return faces;
}

uint32_t get_generation() { return g_generation; }

bool taken_face(const render::TriMeshSource* geometry, int face, uint32_t* piece, int* index, uint16_t* flags)
{
    for (const auto& entry : entries()) {
        const std::vector<Taken>& taken = entry.second.taken;
        for (size_t i = 0; i < taken.size(); ++i) {
            if (core::at(taken, i).geometry == geometry && core::at(taken, i).face == face) {
                if (piece)
                    *piece = entry.first;
                if (index)
                    *index = static_cast<int>(i);
                if (flags)
                    *flags = core::at(taken, i).flags;
                return true;
            }
        }
    }
    return false;
}

bool origin_of(uint32_t piece, int index, render::TriMeshSource** geometry, int* face)
{
    TableRow* entry = get_find(piece);
    // (the companion's faces are the taken ones, in order - Gather / Add; a copy's took none)
    if (!entry || !entry->companion || index < 0 || std::cmp_greater_equal(index, entry->taken.size()))
        return false;
    *geometry = core::at(entry->taken, index).geometry;
    *face = core::at(entry->taken, index).face;
    return true;
}

const Companion* companion_of(uint32_t piece)
{
    const TableRow* entry = get_find(piece);
    return entry ? entry->companion.get() : nullptr;
}

bool set_taken_flags(const render::TriMeshSource* geometry, int face, uint16_t flags)
{
    for (auto& entry : entries()) {
        std::vector<Taken>& taken = entry.second.taken;
        for (size_t i = 0; i < taken.size(); ++i) {
            if (core::at(taken, i).geometry != geometry || core::at(taken, i).face != face)
                continue;
            core::at(taken, i).flags = flags;
            if (entry.second.companion)
                entry.second.companion->change_tri_flags(static_cast<int>(i), flags);
            core::at(taken, i).geometry->set_face_flags(core::at(taken, i).face, static_cast<uint16_t>(flags | 0x10));
            ++g_generation;
            return true;
        }
    }
    return false;
}

} // namespace frontend::level_editor::shared_collision
