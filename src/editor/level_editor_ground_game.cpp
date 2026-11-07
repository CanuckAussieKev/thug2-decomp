// NOT ORIGINAL - the level editor's raised / lowered ground: the game side (ours). See level_editor_ground.h.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "base/core/enum_ops.h"
#include "engine/gfx/engine_geom.h"
#include "base/core/checksums.h"
#include "engine/memory/mem.h"
#include "editor/level_editor_ground.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/sector.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "platform/run_log.h"
#include "engine/gfx/surface_material.h"
#include "base/core/down_cast.h"
#include "engine/gfx/vertex_view.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/core/packed_colour.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "base/maths/maths.h"

namespace frontend::level_editor::ground {

namespace {

constexpr std::size_t kLineTextSize = 96;
constexpr std::size_t kMaxIndexedVertices = 0xFFFF; // faces index the vertices with 16 bits
constexpr uint32_t kVertexBytes = 12;       // a collision vertex: three floats
constexpr uint32_t kFaceBytes = 10;         // a collision face: five 16-bit words
constexpr std::size_t kFaceWords = 5;       // flags, terrain and three vertex indices
constexpr int kOctetFill = 0xFF;            // the faces' octets' fill
constexpr uint32_t kMaxMeshes = 64;         // a donor with more meshes is refused
constexpr uint32_t kSharedMeshFlags = 0x11; // mesh flags of a shared mesh
constexpr float kFloorTolerance = 30.0F;    // a face this close in height to the tile's top counts as its floor
constexpr uint32_t kBitsPerByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr double kTinyScale = 1e-4;         // a texture or world extent smaller than this has no scale
constexpr float kTileHeightMargin = 40.0F;  // tiles further than this above or below the ground are not inside it
constexpr float kGroundFaceReach = 30.0F;
constexpr float kTileInsideMargin = 25.0F;
constexpr std::size_t kReportTextSize = 320;

render::Geom* geom_of(render::WorldCell* sector) { return static_cast<render::Geom*>(sector->geom_handle()); }
gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector) { return sector->hit_body(); }
scenery::GeomMeshes meshes_of(render::Geom* geom) { return scenery::meshes_of(geom); }
uint32_t mesh_count_of(render::Geom* geom) { return render::device().geom_mesh_count(geom); }
uint32_t vertex_format(const render::MeshData* mesh) { return mesh->fvf(); }

render::WorldCell* lookup_sector(render::WorldScene* scene, uint32_t checksum)
{
    return scene ? static_cast<render::WorldCell*>(scene->look_up_sector(checksum)) : nullptr;
}
// WorldCell::change_live (0x0049E9B0) / is_operative (0x0049E9E0): drawn and collided with, or neither.
void change_live(render::WorldCell* sector, bool on) { sector->change_live(on); }
bool is_operative(render::WorldCell* sector) { return sector->is_operative(); }

render::StaticCollider* static_of(render::WorldCell* sector)
{
    gsl::owner<render::HitMesh*> coll = coll_of(sector);
    if (!render::is_static_collider(coll)) {
        return nullptr;
    }
    return core::down_cast<render::StaticCollider>(coll);
}

// The buffers as MeshRef::Replicate (render_pc/sub_mesh.cpp) makes them: a vertex buffer (0x004D5760: its memory copy
// and the card's) and an index buffer (the C runtime's malloc 0x00624031, constructor 0x004D4270) - the graphics
// backend's (render::Device::create_vertex_buffer ...).
gsl::owner<render::VertexBufferData*> new_vertex_buffer(uint32_t size)
{
    return render::device().create_vertex_buffer(size);
}
void upload_vertices(render::VertexBufferData* buffer) { render::device().upload_vertex_buffer(buffer); }
gsl::owner<render::IndexBufferData*> new_index_buffer(uint32_t bytes)
{
    return render::device().create_index_buffer(bytes);
}
void upload_indices(render::IndexBufferData* buffer) { render::device().upload_index_buffer(buffer); }

// --- state ---

std::vector<Spec>& stored_specs()
{
    static core::NoDestroy<std::vector<Spec>> specs;
    return specs.get();
}
struct Made {
    uint32_t sector = 0; // the sector made for it (Checksums(name + "_" + material) - a new donor: a new copy)
    Spec built;
};
std::map<std::string, Made>& made_state()
{
    static std::map<std::string, Made> value; // (the pieces in place)
    return value;
}
std::set<uint32_t>& pool()
{
    static std::set<uint32_t> value; // (every sector made this level - kept, hidden, when its piece goes)
    return value;
}
std::map<uint32_t, int>& tile_hides()
{
    static std::map<uint32_t, int> value; // (tiles hidden by pits, and by how many)
    return value;
}
// NOT ORIGINAL grouping - level_editor_ground_game.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_ground_game_state()).
struct LevelEditorGroundGameState {
    render::WorldScene* scene_{};
    uint32_t active_scene_key_{};
};

LevelEditorGroundGameState& level_editor_ground_game_state()
{
    static core::NoDestroy<LevelEditorGroundGameState> state;
    return state.get();
}

uint32_t sector_key_from_spec(const Spec& spec)
{
    std::array<char, kLineTextSize> text{};
    text::format_to(text, "%s_%08X", spec.name.c_str(), spec.material);
    return checksums::hash_from_text(text.data());
}

// A new level (another scene): what the editor knew of the last one forgotten (its sectors are gone).
void note_level()
{
    render::WorldScene* scene = scenery::main_scene();
    const uint32_t name = scenery::world_scene_key(scene);
    if (scene == level_editor_ground_game_state().scene_ &&
        name == level_editor_ground_game_state().active_scene_key_) {
        return;
    }
    level_editor_ground_game_state().scene_ = scene;
    level_editor_ground_game_state().active_scene_key_ = name;
    stored_specs().clear();
    made_state().clear();
    pool().clear();
    tile_hides().clear();
}

void hide_tiles(const std::vector<uint32_t>& tiles, bool hide)
{
    for (const uint32_t tile : tiles) {
        render::WorldCell* sector = scenery::sector_of(tile);
        if (!sector) {
            continue;
        }
        int& count = tile_hides()[tile];
        if (hide) {
            if (count++ == 0) {
                change_live(sector, false);
            }
        } else if (count > 0) {
            --count;
            if (count == 0) {
                change_live(sector, true);
            }
        }
    }
}

struct GroundStep : ModuleStep {
private:
    std::vector<Spec> specs;

public:
    explicit GroundStep(std::vector<Spec> initial_specs) : specs(std::move(initial_specs)) {}
    void get_restore() override
    {
        std::string message;
        apply_to_each(specs, &message);
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override { return std::make_shared<GroundStep>(stored_specs()); }
};

// The copy's other meshes: nothing drawn (their strips all one index - degenerate), centred on the piece.
void blank_other_meshes(render::Geom* geom, const scenery::GeomMeshes& meshes, V3 centre)
{
    for (uint32_t i = 1; i < mesh_count_of(geom); ++i) {
        render::MeshData* other = meshes.at(i);
        if (!other) {
            continue;
        }
        for (int k = 0; k < kBoxCorners; ++k) {
            render::IndexBufferData* buffer = core::at(other->index_buffers(), k);
            if ((core::at(other->index_counts(), k) == 0U) || !buffer || !buffer->indices()) {
                continue;
            }
            if (std::ranges::any_of(std::span<const uint16_t>(buffer->indices(), core::at(other->index_counts(), k)),
                                    [](uint16_t v) { return v != 0; })) {
                std::memset(buffer->indices(), 0, size_t{core::at(other->index_counts(), k)} * 2U);
                upload_indices(buffer);
            }
        }
        core::at(other->centre(), 0) = centre.x, core::at(other->centre(), 1) = centre.y,
                                  core::at(other->centre(), 2) = centre.z;
        other->set_radius(1.0F);
    }
}

// The generated piece written into a copy of its donor (see the header). False and why.
bool emit_out(render::WorldCell* sector, const Spec& spec, const Mesh& mesh, std::string* why)
{
    render::Geom* geom = geom_of(sector);
    if (!render::is_pc_geom(geom) || mesh_count_of(geom) == 0) {
        *why = "the copy has no drawn geometry";
        return false;
    }
    const scenery::GeomMeshes meshes = meshes_of(geom);
    render::MeshData* first = meshes.at(0);
    Layout layout;
    if (!first || !layout_for({.fvf = vertex_format(first), .stride = first->stride()}, &layout) || first->vertex_buffer_count() < 1 ||
        first->vertex_buffer_count() > 3) {
        *why = "the copy's vertex format can't be written";
        return false;
    }
    render::StaticCollider* coll = static_of(sector);
    if (!coll || !coll->geometry() || ((coll->flag_bits() & render::HitMesh::kOwnsGeometry) == 0)) {
        *why = "the copy has no collision of its own";
        return false;
    }
    std::vector<uint16_t> strip = get_strip(mesh.faces, true);
    if (mesh.faces.empty() || mesh.faces.size() > static_cast<size_t>(kMaxFaces) || mesh.vertices.size() > kMaxIndexedVertices ||
        strip.size() > kMaxIndexedVertices) {
        *why = "the piece has too many faces (at most 2048)";
        return false;
    }
    auto count = static_cast<uint32_t>(mesh.vertices.size());
    // The render: new vertex buffers (each of the mesh's) and index buffers (each level of detail it draws).
    std::vector<uint8_t> data(static_cast<size_t>(count) * first->stride());
    write_vertices(mesh, layout, first->stride(), colour::Packed{spec.colour}, data);
    for (int buffer_index = 0; std::cmp_less(buffer_index, first->vertex_buffer_count()); ++buffer_index) {
        gsl::owner<render::VertexBufferData*> buffer =
            new_vertex_buffer(static_cast<uint32_t>(data.size())); // (the mesh's)
        if (!buffer || !buffer->data()) {
            if (buffer) {
                render::device().release_vertex_buffer(buffer); // (made without its copy: given back)
            }
            *why = "no vertex buffer";
            return false;
        }
        std::memcpy(buffer->data(), data.data(), data.size());
        upload_vertices(buffer);
        core::at(first->vertex_buffers(), buffer_index) = buffer;
    }
    first->set_vertex_count(static_cast<uint16_t>(count));
    int level = -1; // (each level of detail's slot: the buffer is stored through the slot itself)
    for (render::IndexBufferData*& slot : first->index_buffers()) {
        ++level;
        if (level > 0 && (core::at(first->index_counts(), level) == 0U)) {
            continue;
        }
        gsl::owner<render::IndexBufferData*> buffer = new_index_buffer(static_cast<uint32_t>(strip.size() * 2));
        if (!buffer || !buffer->indices()) {
            if (buffer) {
                render::device().release_index_buffer(buffer); // (made without its copy: given back)
            }
            *why = "no index buffer";
            return false;
        }
        std::memcpy(buffer->indices(), strip.data(), strip.size() * 2);
        upload_indices(buffer);
        slot = buffer;
        core::at(first->index_counts(), level) = static_cast<uint16_t>(strip.size());
    }
    if (first->per_vertex_bytes()) {
        first->set_per_vertex_bytes(static_cast<uint8_t*>(memory::alloc(count)));
        if (first->per_vertex_bytes()) {
            std::memset(first->per_vertex_bytes(), 0, count);
        }
    }
    first->set_position_count(0); // (the copied lists aren't the piece's)
    first->set_triangle_count(0);
    const V3 centre = (mesh.min + mesh.max) * 0.5F;
    const V3 half = mesh.max - centre;
    core::at(first->centre(), 0) = centre.x, core::at(first->centre(), 1) = centre.y, core::at(first->centre(), 2) = centre.z;
    first->set_radius(std::sqrt(get_dot(half, half)));
    // The other meshes: nothing drawn (their strips all one index - degenerate).
    blank_other_meshes(geom, meshes, centre);
    maths::BBox& bounds = *geom->fetch_bounds(); // (its box as loaded)
    bounds.min.x = mesh.min.x, bounds.min.y = mesh.min.y, bounds.min.z = mesh.min.z;
    bounds.max.x = mesh.max.x, bounds.max.y = mesh.max.y, bounds.max.z = mesh.max.z;

    // The collision: new arrays (memory::alloc - the game frees them with memory::Dealloc, TriMeshSource::destruct), floats,
    // 10-byte faces, one leaf listing every face.
    render::TriMeshSource* tri = coll->geometry();
    auto faces = static_cast<uint32_t>(mesh.faces.size());
    auto* vertices = static_cast<float*>(memory::alloc(count * kVertexBytes));
    auto* face_data = static_cast<uint8_t*>(memory::alloc(faces * kFaceBytes));
    render::HitTreeNode* leaf = render::new_tree_nodes(1);
    auto* leaf_faces = static_cast<uint16_t*>(memory::alloc(faces * 2));
    uint8_t* bytes = tri->vertex_bytes() ? static_cast<uint8_t*>(memory::alloc(count)) : nullptr;
    if (!vertices || !face_data || !leaf || !leaf_faces || (tri->vertex_bytes() && !bytes)) {
        *why = "out of memory for the collision";
        return false;
    }
    const std::span<float> vertex_floats(vertices, size_t{count} * 3);
    for (uint32_t vertex = 0; vertex < count; ++vertex) {
        std::memcpy(&core::at(vertex_floats, 3 * vertex), &core::at(mesh.vertices, vertex).pos, kVertexBytes);
    }
    const std::span<uint8_t> face_bytes(face_data, size_t{faces} * 10);
    const std::span<uint16_t> leaf_run(leaf_faces, faces);
    for (uint32_t face_index = 0; face_index < faces; ++face_index) {
        const Face& face = core::at(mesh.faces, face_index);
        std::array<uint16_t, kFaceWords> words = {{core::to_underlying(face.flags), spec.terrain, face.a, face.b, face.c}};
        std::memcpy(&core::at(face_bytes, kFaceBytes * face_index), words.data(), kFaceBytes);
        core::at(leaf_run, face_index) = static_cast<uint16_t>(face_index);
    }
    if (bytes) {
        std::memset(bytes, kNeutralVertexByte, count);
    }
    if (tri->vertices()) {
        memory::dealloc(tri->vertices());
    }
    if (tri->faces()) {
        memory::dealloc(tri->faces());
    }
    if (tri->vertex_bytes()) {
        memory::dealloc(tri->vertex_bytes());
    }
    tri->delete_bsp();
    tri->set_vertices(vertices);
    tri->set_faces(face_data);
    tri->set_vertex_bytes(bytes);
    tri->set_num_vertices(static_cast<uint16_t>(count));
    tri->set_tri_count(static_cast<uint16_t>(faces));
    tri->set_small_faces(false);
    tri->set_compressed(false);
    tri->bounds().min.x = mesh.min.x, tri->bounds().min.y = mesh.min.y, tri->bounds().min.z = mesh.min.z;
    tri->bounds().max.x = mesh.max.x, tri->bounds().max.y = mesh.max.y, tri->bounds().max.z = mesh.max.z;
    tri->set_unknown3c(0); // (no face skipped by its distance)
    // (new HitTreeNode[1]: one leaf of every face)
    leaf->make_leaf(static_cast<uint16_t>(faces), leaf_faces);
    tri->set_bsp(leaf);
    static_cast<void>(tri->set_up_all_faces());
    // (the face-rejection bytes: one a face, on the bottom-up heap as StaticCollider::Replicate keeps them)
    if (coll->octets()) {
        memory::free_malloc(coll->octets());
    }
    memory::enter_heap_scope(memory::bottom_up_heap());
    coll->set_octets(static_cast<uint8_t*>(memory::alloc_block(faces)));
    memory::leave_heap_scope();
    if (coll->octets()) {
        std::memset(coll->octets(), kOctetFill, faces);
    }
    return true;
}

// The piece put in place: its sector made (or one made before reused) and written, shown; its pit's tiles hidden.
bool get_make(const Spec& spec, std::string* why)
{
    render::WorldScene* scene = scenery::main_scene();
    if (!scene) {
        *why = "no level";
        return false;
    }
    const Mesh mesh = get_build(spec);
    if (mesh.faces.empty()) {
        *why = "the piece has no faces";
        return false;
    }
    uint32_t name = sector_key_from_spec(spec);
    render::WorldCell* sector = lookup_sector(scene, name);
    if (sector && !pool().contains(name)) {
        *why = "a sector of that name is the level's";
        return false;
    }
    if (!sector) {
        if (const char* no = why_not_donor(spec.material)) {
            *why = std::string("its material's piece can't be copied: ") + no;
            return false;
        }
        const uint32_t made = scene->clone_sector_named(spec.material, scene, 0, true, name);
        sector = (made != 0U) ? lookup_sector(scene, made) : nullptr;
        if (!sector) {
            *why = "the material's piece wasn't copied";
            return false;
        }
        pool().insert(made);
        name = made;
    }
    if (!emit_out(sector, spec, mesh, why)) {
        change_live(sector, false);
        return false;
    }
    change_live(sector, true);
    scenery::note_collision_moved(name);
    scenery::finalise();
    // (a piece already there under this name: its old sector hidden when it was another, its tiles shown again)
    auto it = made_state().find(spec.name);
    if (it != made_state().end()) {
        if (it->second.sector != name) {
            if (render::WorldCell* old = lookup_sector(scene, it->second.sector)) {
                change_live(old, false);
            }
        }
        hide_tiles(it->second.built.hides, false);
    }
    hide_tiles(spec.hides, true);
    made_state()[spec.name] = {.sector = name, .built = spec};
    get_log("level editor: ground piece %s (%s, %.0f x %.0f, %.0f %s, %u faces) in place - material %s",
            spec.name.c_str(), spec.lower ? "pit" : "platform", spec.x1 - spec.x0, spec.z1 - spec.z0, spec.height,
            spec.lower ? "deep" : "high", static_cast<unsigned>(mesh.faces.size()),
            object_edit::caption(spec.material).c_str());
    return true;
}

void unmake(const std::string& piece_name)
{
    auto it = made_state().find(piece_name);
    if (it == made_state().end()) {
        return;
    }
    if (render::WorldCell* sector = lookup_sector(scenery::main_scene(), it->second.sector)) {
        change_live(sector, false);
    }
    hide_tiles(it->second.built.hides, false);
    made_state().erase(it);
}

} // namespace

const std::vector<Spec>& get_all() { return stored_specs(); }

bool is_piece(uint32_t sector) { return pool().contains(sector); }

const Spec* get_find(const std::string& name)
{
    for (const Spec& stored_spec : stored_specs()) {
        if (stored_spec.name == name) {
            return &stored_spec;
        }
    }
    return nullptr;
}

void kickoff() { note_level(); }
void halt() {}

const char* why_not_donor(uint32_t checksum)
{
    render::WorldScene* scene = scenery::main_scene();
    render::WorldCell* sector = lookup_sector(scene, checksum);
    if (!sector) {
        return "not a piece of the level's main scene";
    }
    if (pool().contains(checksum)) {
        return "a generated piece";
    }
    render::Geom* geom = geom_of(sector);
    if (!render::is_pc_geom(geom)) {
        return "nothing drawn";
    }
    if (render::device().geom_instance(geom)) {
        return "an instance";
    }
    const uint32_t meshes = mesh_count_of(geom);
    if (!render::device().geom_mesh_list(geom) || meshes == 0 || meshes > kMaxMeshes) {
        return "no meshes (or too many)";
    }
    const render::MeshData* first = meshes_of(geom).at(0);
    Layout layout;
    if (!first || first->billboard() || ((first->flags() & kSharedMeshFlags) != 0U)) {
        return "a billboard or shared mesh";
    }
    if (!layout_for({.fvf = vertex_format(first), .stride = first->stride()}, &layout) || layout.tex_count_ < 1) {
        return "its vertex format (no plain positions and texture coordinates)";
    }
    if (first->vertex_buffer_count() < 1 || first->vertex_buffer_count() > 3 || !core::at(first->vertex_buffers(), 0) ||
        !core::at(first->vertex_buffers(), 0)->data()) {
        return "its vertex buffers";
    }
    if (first->per_vertex_data()) {
        return "per-vertex data the editor can't make";
    }
    const render::MaterialRef* material = first->material();
    if (!material || picking::material_see_through({.pass_flags_ = core::at(material->pass_flags(), 0),
                                                    .blend_mode_ = core::at(material->blend_modes(), 0)})) {
        return "a see-through material";
    }
    const render::StaticCollider* coll = static_of(sector);
    if (!coll || !coll->geometry() || (coll->geometry()->tri_count() == 0U)) {
        return "no collision";
    }
    return nullptr;
}

bool choose_material(uint32_t wanted, Spec* spec, std::string* why)
{
    note_level();
    uint32_t chosen = (wanted != 0U) && !why_not_donor(wanted) ? wanted : 0;
    if (chosen == 0U) {
        // (the nearest piece that can lend its material - a floor-like one, its top near the ground, first)
        struct Walk {
            const Spec* spec{};
            uint32_t best = 0, best_floor_ = 0;
            float best_distance_ = kFarDistance, best_floor_distance_ = kFarDistance;
        } walk = {.spec = spec};
        scenery::for_each_level_sector(
            [](void* context, uint32_t checksum, render::WorldCell* sector) {
                auto* w = static_cast<Walk*>(context);
                if (!is_operative(sector) || why_not_donor(checksum)) {
                    return;
                }
                const render::TriMeshSource* tri = static_of(sector)->geometry();
                const V3 corner = {.x = (w->spec->x0 + w->spec->x1) * 0.5F,
                                   .y = w->spec->base,
                                   .z = (w->spec->z0 + w->spec->z1) * 0.5F};
                const V3 lo = {.x = tri->bounds().min.x, .y = tri->bounds().min.y, .z = tri->bounds().min.z};
                const V3 hi = {.x = tri->bounds().max.x, .y = tri->bounds().max.y, .z = tri->bounds().max.z};
                const V3 nearest = {.x = std::clamp(corner.x, lo.x, hi.x),
                                    .y = std::clamp(corner.y, lo.y, hi.y),
                                    .z = std::clamp(corner.z, lo.z, hi.z)};
                const V3 delta = nearest - corner;
                const float distance = std::sqrt(get_dot(delta, delta));
                if (distance < w->best_distance_) {
                    w->best = checksum, w->best_distance_ = distance;
                }
                if (std::fabs(hi.y - corner.y) < kFloorTolerance && distance < w->best_floor_distance_) {
                    w->best_floor_ = checksum, w->best_floor_distance_ = distance;
                }
            },
            &walk);
        chosen = (walk.best_floor_ != 0U) ? walk.best_floor_ : walk.best;
    }
    if (chosen == 0U) {
        *why = (wanted != 0U) ? std::string("that piece can't lend its material (") + why_not_donor(wanted) +
                                    ") and no other piece of the level can"
                              : "no piece of the level can lend its material";
        return false;
    }
    spec->material = chosen;
    // What it looks like: its first mesh's average colour, how many units a texture repeat spans, its commonest terrain.
    render::WorldCell* sector = lookup_sector(scenery::main_scene(), chosen);
    const render::MeshData* mesh = meshes_of(geom_of(sector)).at(0);
    Layout layout;
    layout_for({.fvf = vertex_format(mesh), .stride = mesh->stride()}, &layout);
    const uint8_t* data = core::at(mesh->vertex_buffers(), 0)->data();
    const std::span<const uint8_t> vertex_bytes(data, size_t{mesh->vertex_count()} * mesh->stride());
    if (layout.diffuse >= 0 && (mesh->vertex_count() != 0U)) {
        std::array<double, 4> sum = {};
        for (uint32_t vertex = 0; vertex < mesh->vertex_count(); ++vertex) {
            uint32_t colour = 0;
            std::memcpy(
                &colour,
                vertex_bytes.subspan((size_t{vertex} * mesh->stride()) + static_cast<size_t>(layout.diffuse)).data(),
                4);
            for (int k = 0; k < 4; ++k) {
                core::at(sum, k) += (colour >> (kBitsPerByte * k)) & kByteMask;
            }
        }
        uint32_t colour = 0;
        for (int k = 0; k < 4; ++k) {
            colour |= maths::round_non_negative<uint32_t>(core::at(sum, k) / mesh->vertex_count()) << (kBitsPerByte * k);
        }
        spec->colour = colour;
    }
    const render::IndexBufferData* indices = core::at(mesh->index_buffers(), 0);
    if (indices && indices->indices() && core::at(mesh->index_counts(), 0) >= 3) {
        double world = 0.0;
        double texture = 0.0;
        const std::span<const uint16_t> index_run(indices->indices(), core::at(mesh->index_counts(), 0));
        for (uint32_t i = 0; i + 1 < core::at(mesh->index_counts(), 0); ++i) {
            const uint16_t index_a = core::at(index_run, i);
            const uint16_t index_b = core::at(index_run, i + 1);
            if (index_a == index_b || index_a >= mesh->vertex_count() || index_b >= mesh->vertex_count()) {
                continue;
            }
            const auto pa = gpu::VertexView(data, mesh->stride(), mesh->vertex_count()).float3(index_a);
            const auto pb = gpu::VertexView(data, mesh->stride(), mesh->vertex_count()).float3(index_b);
            const auto ta = gpu::VertexView(data, mesh->stride(), mesh->vertex_count()).float2(index_a, layout.tex);
            const auto tb = gpu::VertexView(data, mesh->stride(), mesh->vertex_count()).float2(index_b, layout.tex);
            const double dw = std::sqrt(
                (static_cast<double>(core::at(pa, 0) - core::at(pb, 0)) * (core::at(pa, 0) - core::at(pb, 0))) +
                (static_cast<double>(core::at(pa, 1) - core::at(pb, 1)) * (core::at(pa, 1) - core::at(pb, 1))) +
                (static_cast<double>(core::at(pa, 2) - core::at(pb, 2)) * (core::at(pa, 2) - core::at(pb, 2))));
            const double dt = std::sqrt(
                (static_cast<double>(core::at(ta, 0) - core::at(tb, 0)) * (core::at(ta, 0) - core::at(tb, 0))) +
                (static_cast<double>(core::at(ta, 1) - core::at(tb, 1)) * (core::at(ta, 1) - core::at(tb, 1))));
            if (dt > kTinyScale && std::isfinite(dt) && std::isfinite(dw)) {
                world += dw;
                texture += dt;
            }
        }
        if (texture > kTinyScale) {
            spec->uv = std::clamp(static_cast<float>(world / texture), kMinUvScale, kMaxUvScale);
        }
    }
    const render::TriMeshSource* tri = static_of(sector)->geometry();
    std::map<uint16_t, int> terrains;
    for (int face_index = 0; std::cmp_less(face_index, tri->tri_count()) && tri->faces(); ++face_index) {
        ++terrains[tri->face_terrain(face_index)];
    }
    int most = -1;
    for (const auto& terrain : terrains) {
        if (terrain.second > most) {
            most = terrain.second, spec->terrain = terrain.first;
        }
    }
    return true;
}

bool find_pit_tiles(Spec* spec, std::string* why)
{
    note_level();
    spec->hides.clear();
    struct Walk {
        Spec* spec;
        std::vector<uint32_t> tiles;
        std::string refused;
    } walk = {.spec = spec, .tiles = {}, .refused = {}};
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            auto* w = static_cast<Walk*>(context);
            // (a tile a pit hides now still counts - the pit being edited)
            const bool mine = tile_hides().contains(checksum) && tile_hides()[checksum] > 0;
            if (!w->refused.empty() || pool().contains(checksum) || (!is_operative(sector) && !mine)) {
                return;
            }
            const render::StaticCollider* coll = static_of(sector);
            const render::TriMeshSource* tri = coll ? coll->geometry() : nullptr;
            if (!tri || !tri->faces() || !tri->vertices() || ((coll->flag_bits() & 4) && !mine)) {
                return;
            }
            const Spec& placed_spec = *w->spec;
            if (tri->bounds().max.x < placed_spec.x0 || tri->bounds().min.x > placed_spec.x1 ||
                tri->bounds().max.z < placed_spec.z0 || tri->bounds().min.z > placed_spec.z1 ||
                tri->bounds().max.y < placed_spec.base - kTileHeightMargin ||
                tri->bounds().min.y > placed_spec.base + kTileHeightMargin) {
                return;
            }
            bool ground = false;
            const int faces = std::min<int>(tri->tri_count(), 60000);
            for (int face_index = 0; face_index < faces && !ground; ++face_index) {
                const render::FaceFlag flags = tri->face_flags(face_index);
                if (core::any(flags & render::FaceFlag::kNotCollidable)) {
                    continue;
                }
                std::array<V3, 3> corners{};
                bool ok = true;
                for (int k = 0; k < 3 && ok; ++k) {
                    const uint32_t index = tri->face_vertex(face_index, k);
                    ok = index < tri->num_vertices();
                    if (ok) {
                        const maths::Vec4 point = tri->read_stored_vertex(static_cast<int>(index));
                        core::at(corners, k) = {.x = point.x, .y = point.y, .z = point.z};
                    }
                }
                ground = ok && ground_face_under(corners, placed_spec, kGroundFaceReach);
            }
            if (!ground) {
                return;
            }
            const V3 lo = {.x = tri->bounds().min.x, .y = tri->bounds().min.y, .z = tri->bounds().min.z};
            const V3 hi = {.x = tri->bounds().max.x, .y = tri->bounds().max.y, .z = tri->bounds().max.z};
            if (tile_inside(lo, hi, placed_spec, kTileInsideMargin)) {
                w->tiles.push_back(checksum);
                return;
            }
            std::array<char, kReportTextSize> text{};
            text::format_to(
                text,
                "the floor here is %s%s, %.0f x %.0f - bigger than the rectangle. THUG2's floors can't be cut: "
                "make the rectangle cover that whole piece, or raise around it instead",
                object_edit::caption(checksum).c_str(), geom_of(sector) ? "" : " (collision only)", hi.x - lo.x,
                hi.z - lo.z);
            w->refused = text.data();
        },
        &walk);
    if (!walk.refused.empty()) {
        *why = walk.refused;
        return false;
    }
    if (walk.tiles.empty()) {
        *why = "no floor piece was found at the ground's height under the rectangle";
        return false;
    }
    spec->hides = walk.tiles;
    return true;
}

bool place_in(const Spec& in, std::string* why)
{
    note_level();
    Spec spec = in;
    if (!normalise(&spec)) {
        *why = "a value isn't a number";
        return false;
    }
    if (spec.lower && !find_pit_tiles(&spec, why)) {
        return false;
    }
    if (!spec.lower) {
        spec.hides.clear();
    }
    const std::vector<Spec> before = stored_specs();
    if (!get_make(spec, why)) {
        return false;
    }
    push_module_undo_labelled(std::make_shared<GroundStep>(before), spec.lower ? "Lower ground" : "Raise ground");
    bool found = false;
    for (Spec& stored_spec : stored_specs()) {
        if (stored_spec.name == spec.name) {
            stored_spec = spec;
            found = true;
        }
    }
    if (!found) {
        stored_specs().push_back(spec);
    }
    return true;
}

void drop_out(const std::string& name)
{
    note_level();
    if (!get_find(name)) {
        return;
    }
    push_module_undo_labelled(std::make_shared<GroundStep>(stored_specs()), "Remove ground piece");
    unmake(name);
    std::erase_if(stored_specs(), [&](const Spec& spec) { return spec.name == name; });
}

int apply_to_each(const std::vector<Spec>& specs, std::string* message)
{
    note_level();
    // (gone: taken out; changed or new: made; the same: left)
    std::vector<std::string> gone;
    for (const auto& made : made_state()) {
        if (std::ranges::none_of(specs, [&](const Spec& spec) { return spec.name == made.first; })) {
            gone.push_back(made.first);
        }
    }
    for (const std::string& name : gone) {
        unmake(name);
    }
    std::vector<Spec> kept;
    int placed = 0;
    for (const Spec& spec : specs) {
        auto it = made_state().find(spec.name);
        std::string why;
        if ((it != made_state().end() && same_spec(it->second.built, spec)) || get_make(spec, &why)) {
            ++placed;
        } else {
            get_log("level editor: ground piece %s not made: %s", spec.name.c_str(), why.c_str());
            if (message && message->empty()) {
                *message = "ground piece " + spec.name + " not made: " + why;
            }
        }
        kept.push_back(spec); // (kept even when not made: its record isn't lost)
    }
    stored_specs() = kept;
    return placed;
}

std::vector<std::string> capture_records()
{
    std::vector<std::string> lines;
    lines.reserve(stored_specs().size());
    for (const Spec& stored_spec : stored_specs()) {
        lines.push_back("ground " + link_record(stored_spec, object_edit::caption(stored_spec.material).c_str()));
    }
    return lines;
}

void apply_records(const std::vector<std::string>& lines)
{
    std::vector<Spec> specs;
    for (const std::string& line : lines) {
        Spec spec;
        const std::string body = line.size() > 7 ? line.substr(7) : std::string();
        if (line.starts_with("ground ") && read_record(body, &spec)) {
            specs.push_back(spec);
        } else {
            get_log("level editor: a layout's ground record isn't readable - skipped: %s", line.c_str());
        }
    }
    std::string message;
    const int placed = apply_to_each(specs, &message);
    if (!specs.empty()) {
        get_log("level editor: the layout's %d of %u ground pieces put in place", placed,
                static_cast<unsigned>(specs.size()));
    }
}

void reset_all()
{
    // (a new level: nothing of the old one touched - its sectors are gone)
    render::WorldScene* scene = scenery::main_scene();
    if (scene == level_editor_ground_game_state().scene_ && scenery::world_scene_key(scene) == level_editor_ground_game_state().active_scene_key_) {
        std::vector<std::string> names;
        names.reserve(made_state().size());
        for (const auto& made : made_state()) {
            names.push_back(made.first);
        }
        for (const std::string& name : names) {
            unmake(name);
        }
    }
    stored_specs().clear();
    made_state().clear();
    tile_hides().clear();
    if (scene != level_editor_ground_game_state().scene_ ||
        scenery::world_scene_key(scene) != level_editor_ground_game_state().active_scene_key_) {
        note_level();
    }
}

void add_preview(const overlays::Painter& painter, const Spec& spec, uint32_t argb)
{
    Mesh mesh = get_build(spec);
    const size_t faces = std::min<size_t>(mesh.faces.size(), 3000);
    for (size_t face_index = 0; face_index < faces; ++face_index) {
        const Face& face = core::at(mesh.faces, face_index);
        const V3 point_a = core::at(mesh.vertices, face.a).pos;
        const V3 point_b = core::at(mesh.vertices, face.b).pos;
        const V3 corner = core::at(mesh.vertices, face.c).pos;
        (void)painter.get_seg(point_a, point_b, argb);
        (void)painter.get_seg(point_b, corner, argb);
        (void)painter.get_seg(corner, point_a, argb);
    }
}

} // namespace frontend::level_editor::ground
