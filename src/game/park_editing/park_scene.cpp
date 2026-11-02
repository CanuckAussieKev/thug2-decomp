// The park generator's scene: the source pieces (the "sk5ed" scene's sectors), the placed pieces cloned from them
// into the "cloned" scene and lit by the park's lights, the scene rebuilt, and the restart points. The data is laid
// out in park_scene.h; the node array the park's level runs on is park_nodes.cpp.

#include <cstdint>
#include <array>

#include "game/park_editing/park_scene.h"

#include "engine/assets/resource_entry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_nx.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine_geom.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_tri.h"
#include "game/park/park_memory.h"
#include "engine/script/nodes.h"
#include "game/park_editing/park_editor.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace park_editing {

namespace {

// The super sectors are brought up to date when sectors are deleted (0x0067F398; off while a rebuild runs).
bool& scene_updates() { return g_collision_allowed; }

// The engine's scenes and sectors (Nx). WorldScene: a sector deleted (0x0049CF80), the sectors flagged for it deleted
// (0x0049D030), the super sectors updated (0x0049D780), a sector cloned into `scene` (0x0049DF30; returns the clone's
// name). WorldCell: placed (0x0049E6C0), turned (0x0049E760), its box (0x0049E6A0), its geometry (+0x14, 0x004A2BA0).
// The geometry's vertices: how many (0x0048DF70), their positions, their colours set and read (0x0049F3E0), its
// collision.
render::WorldScene* look_up_scene(const char* name) { return render::get_scene_by_name(name); }
render::WorldScene* clone_scene(const char* name, render::TextureSet* textures, int flag)
{
    return render::build_world_scene(name, textures, flag != 0);
}
render::WorldCell* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }
void remove_scene_sector(render::WorldScene* scene, uint32_t name) { scene->delete_sector_named(name); }
void delete_marked_sectors(render::WorldScene* scene) { scene->delete_marked_sectors(); }
void update_super_sectors_of(render::WorldScene* scene) { scene->get_update_super_sectors(nullptr); }
uint32_t duplicate_scene_sector(render::WorldScene* source, uint32_t name, render::WorldScene* scene, int cursor,
                                int not_cursor)
{
    return source->clone_sector_named(name, scene, cursor, not_cursor != 0, 0);
}
void place_at_absolute_point(render::WorldCell* sector, const maths::Vec4* position)
{
    sector->place_at_absolute_point(position);
}
void spin_around_up(render::WorldCell* sector, int rot) { sector->spin_around_up(rot); }
const maths::BBox* get_local_bounds(render::WorldCell* sector) { return sector->fetch_bounds(); }
render::Geom* fetch_shape(render::WorldCell* sector) { return static_cast<render::Geom*>(sector->geom_handle_); }
int get_vertex_count(render::Geom* geometry) { return static_cast<int>(geometry->fetch_vertex_tally()); }
void get_vertex_positions(render::Geom* geometry, maths::Vec4* out) { geometry->get_vertices(out); }
void set_vertex_colours(render::Geom* geometry, const uint32_t* colours) { geometry->change_colors(colours); }
void get_vertex_colours(render::Geom* geometry, uint32_t* out) { geometry->plat_get_render_colors(out); }
render::TriMeshSource* get_hit_shape(render::Geom* geometry)
{
    return static_cast<render::TriMeshSource*>(geometry->hit_triangles_);
}

// The rail editor (0x004F4970): its rails taken out of the scene (0x004F45C0, 0x004F45E0), put back (0x004F6650).
RailAuthoring* get_rail_editor() { return RailAuthoring::sole(); }
void rail_editor_remove_a() { get_rail_editor()->delete_all_segments(); }
void rail_editor_remove_b() { get_rail_editor()->delete_all_posts(); }
void rail_editor_rebuild() { get_rail_editor()->reassemble(); }

// The generator's code in park_generator.cpp: a piece made from a source, a piece taken out and deleted (its sector
// too unless `keep_sector`), the restart slots of a type, the bounds, the source pieces' flags from the pieces' node
// array.
DuplicatedTile* make_tile_copy(ParkAssembler* generator, TemplateTile* source, uint32_t flags)
{
    return generator->create_piece(source, flags);
}
void destroy_piece_now(ParkAssembler* generator, DuplicatedTile* piece, int keep_sector)
{
    generator->delete_piece(piece, keep_sector);
}
void spawn_slot_array(ParkAssembler* generator, int type, int* first, int* last)
{
    generator->fetch_spawn_slots(type, first, last);
}
void set_up_bounds(ParkAssembler* generator) { generator->set_up_bounds(); }
void read_piece_flags(ParkAssembler* generator, script::Array* nodes) { generator->read_piece_flags(nodes); }
// The gap manager: a gap piece placed
void place_gap_piece(DuplicatedTile* piece) { GapRegistry::sole()->set_trigger_length(piece); }

// The gap's trigger piece type.
constexpr uint32_t kGapMarkerLabel = 0xC0341DF7;
// The park editor's own level (its rails on the top-down heap).
constexpr uint32_t kParkEditorLevel = 0xCD3682C1;
// The lighting: 1 / (160000 + d^2) for each light, times 255 (0x0064F478, 0x00646B88).
constexpr double kLightDistance = 160000.0;
// A source piece's size in cells: (max - min + 2) * (float)(1 / 120) (0x00645448, 0x0064F630), at least 1.
constexpr double kSizeSlack = 2.0;
constexpr float kInverseCell = 0.008333333767950535f;

// The super sectors brought up to date (on the bottom-up heap).
void get_update_super_sectors(ParkAssembler& d)
{
    d.flag_bits_ &= ~ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    update_super_sectors_of(d.scene_handle_);
    memory::leave_heap_scope();
    d.flag_bits_ |= ParkAssembler::kSuperSectorsUpdated;
}

// Take `piece` out of the list (unless it's the cursor's), delete its sector and the piece (inline in the original, as
// 0x00592570 does).
void delete_piece(ParkAssembler& d, DuplicatedTile* piece)
{
    if (!(piece->flag_bits_ & DuplicatedTile::kCursor)) {
        DuplicatedTile* previous = nullptr;
        for (DuplicatedTile* p = d.pieces_; p; previous = p, p = p->following_) {
            if (p == piece) {
                if (previous)
                    previous->following_ = p->following_;
                else
                    d.pieces_ = p->following_;
                break;
            }
        }
    }
    remove_scene_sector(d.scene_handle_, piece->sector_);
    if (scene_updates())
        get_update_super_sectors(d);
    set_counts_dirty(); // (given the piece)
    delete piece;
    if (!d.pieces_)
        d.flag_bits_ &= ~ParkAssembler::kHasPieces;
}

} // namespace

ParkAssembler::ParkAssembler()
{
    for (RestartPoint& restart : restarts_)
        restart.edit_span_.assign_value(0, 0, 0, 0, 0, 0);
    node_table_.emplace();
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    flag_bits_ = 0;
    field_04_ = 0;
    pieces_ = nullptr;
    sources_ = nullptr;
    scene_handle_ = nullptr;
    source_world_ = nullptr;
    next_piece_id_ = 0x20;
    max_players_ = 2;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

void ParkAssembler::move_light_source(const maths::Vec4* position, int index)
{
    maths::Vec4& light = core::at(lights_, index);
    light.x = position->x;
    light.y = position->y;
    light.z = position->z;
    light.w = position->w;
}

uint32_t ParkAssembler::sample_light_colour(const maths::Vec4* position, uint32_t rgba)
{
    const auto red = static_cast<float>((rgba & 0xFF) * static_cast<double>(core::at(ambient_, 0)));
    const auto green = static_cast<float>(((rgba >> 8) & 0xFF) * static_cast<double>(core::at(ambient_, 1)));
    const double blue = ((rgba >> 16) & 0xFF) * static_cast<double>(core::at(ambient_, 2)); // (kept unrounded)
    double sum = 0.0;
    for (int i = 0; i < light_total_; ++i) {
        const double dx = static_cast<double>(position->x) - core::at(lights_, i).x;
        const double dz = static_cast<double>(position->z) - core::at(lights_, i).z;
        sum += 1.0 / (dz * dz + dx * dx + kLightDistance);
    }
    sum *= 255.0;
    // (over 255 is 255; the original's _ftol otherwise, NaN included)
    auto channel = [](double value) {
        return value > 255.0 ? uint32_t{0xFF} : uint32_t{static_cast<uint8_t>(static_cast<int>(value))};
    };
    return (rgba & 0xFF000000u) | channel(sum * core::at(falloff_, 0) + red) |
           (channel(sum * core::at(falloff_, 1) + green) << 8) | (channel(sum * core::at(falloff_, 2) + blue) << 16);
}

void ParkAssembler::light_piece(ParkTile* p)
{
    const DuplicatedTile* piece = get_cloned(p);
    render::Geom* geometry = fetch_shape(look_up_sector(piece->sector_));
    render::Geom* source_geometry = fetch_shape(look_up_sector(piece->blend_source_->sector_));
    const int count = get_vertex_count(geometry);
    if (count) {
        // (new[] of vectors and colours: their constructors do nothing)
        auto* positions = static_cast<maths::Vec4*>(memory::alloc(count * sizeof(maths::Vec4)));
        auto* colours = static_cast<uint32_t*>(memory::alloc(count * sizeof(uint32_t)));
        get_vertex_positions(geometry, positions);
        get_vertex_colours(source_geometry, colours);
        for (int i = 0; i < count; ++i)
            colours[i] = sample_light_colour(&positions[i], colours[i]);
        set_vertex_colours(geometry, colours);
        memory::dealloc(positions);
        memory::dealloc(colours);
    }
    // the collision's vertex intensities, from the source's (grey)
    const render::TriMeshSource* collision = get_hit_shape(geometry);
    const render::TriMeshSource* source_collision = get_hit_shape(source_geometry);
    const int vertices = collision->num_vertices_;
    for (int i = 0; i < vertices; ++i) {
        const uint8_t grey = source_collision->vertex_bytes_[i];
        const maths::Vec4 position = collision->read_stored_vertex(i);
        const uint32_t rgba = sample_light_colour(&position, 0xFF000000u | grey | (uint32_t{grey} << 8) | (uint32_t{grey} << 16));
        collision->vertex_bytes_[i] = static_cast<uint8_t>(((rgba & 0xFF) + ((rgba >> 8) & 0xFF) + ((rgba >> 16) & 0xFF)) / 3);
    }
}

bool ParkAssembler::plant_restart_point(maths::Vec4* position, int rot, CellBox* area, int type, int automatic,
                                        uint32_t keep_placed)
{
    int first = 0, last = 0;
    spawn_slot_array(this, type, &first, &last);
    maths::Vec4 pos = *position;
    CellBox cells = *area;
    const auto is_automatic = static_cast<uint8_t>(automatic);
    if (is_automatic && static_cast<uint8_t>(keep_placed)) {
        if (first > last)
            return false;
        for (int i = first; i <= last; ++i) {
            const RestartPoint& restart = core::at(restarts_, i);
            if (restart.kind_tag_ == type && !restart.automatic_) {
                pos = restart.at_point_;
                cells = restart.edit_span_;
            }
        }
    }
    for (int i = first; i <= last; ++i) {
        RestartPoint& restart = core::at(restarts_, i);
        if (restart.kind_tag_ == RestartPoint::kFree || (restart.automatic_ && !is_automatic)) {
            restart.at_point_ = pos;
            restart.quarter_turns_ = rot;
            restart.edit_span_ = cells;
            restart.kind_tag_ = type;
            restart.automatic_ = is_automatic;
            return true;
        }
    }
    return false;
}

int ParkAssembler::count_restarts(int type)
{
    int first = 0, last = 0;
    spawn_slot_array(this, type, &first, &last);
    int count = 0;
    for (int i = first; i <= last; ++i) {
        if (core::at(restarts_, i).kind_tag_)
            ++count;
    }
    return count;
}

bool ParkAssembler::needs_restarts(int type, int count) { return count_restarts(type) < count; }

void ParkAssembler::unplant_restart_point(CellBox* area, int player)
{
    int first = 0, last = 0;
    spawn_slot_array(this, player, &first, &last);
    for (int i = first; i <= last; ++i) {
        RestartPoint& restart = core::at(restarts_, i);
        if (restart.edit_span_.x == area->x && restart.edit_span_.y == area->y && restart.edit_span_.z == area->z)
            restart.kind_tag_ = RestartPoint::kFree;
    }
}

void* ParkAssembler::make_gap_marker_piece(maths::Vec4* position, float /*length*/, int rot)
{
    TemplateTile* source = nullptr;
    for (TemplateTile* s = sources_; s; s = s->following_) {
        if (s->sector_ == kGapMarkerLabel) {
            source = s;
            break;
        }
    }
    DuplicatedTile* piece = make_tile_copy(this, source, 0);
    piece->change_place(position, 1);
    piece->set_quarter_turns(rot, 1);
    piece->flag_bits_ |= DuplicatedTile::kGap | DuplicatedTile::kInList;
    piece->following_ = pieces_;
    pieces_ = piece;
    flag_bits_ |= ParkAssembler::kHasPieces;
    return piece;
}

void ParkAssembler::destroy_pieces(int keep_rails)
{
    if (!keep_rails) {
        rail_editor_remove_a();
        rail_editor_remove_b();
    }
    for (DuplicatedTile* piece = pieces_; piece;) {
        DuplicatedTile* next = piece->following_;
        destroy_piece_now(this, piece, keep_rails);
        piece = next;
    }
    if (!keep_rails)
        flag_bits_ &= ~ParkAssembler::kPiecesCreated;
    flag_bits_ &= ~ParkAssembler::kHasPieces;
    if (!keep_rails)
        delete_marked_sectors(scene_handle_);
}

void ParkAssembler::destroy_piece(void* p)
{
    auto* piece = static_cast<DuplicatedTile*>(p);
    const uint32_t flags = piece->flag_bits_;
    piece->flag_bits_ = flags | DuplicatedTile::kDestroy;
    if (flags & DuplicatedTile::kDestroyNow)
        destroy_piece_now(this, piece, 0);
}

void ParkAssembler::clean_up_out_refs()
{
    for (DuplicatedTile* piece = pieces_; piece;) {
        DuplicatedTile* next = piece->following_;
        if (piece->flag_bits_ & DuplicatedTile::kDestroy)
            park_editing::delete_piece(*this, piece);
        piece = next;
    }
}

void ParkAssembler::unload_pieces()
{
    if (flag_bits_ & ParkAssembler::kHasPieces) {
        rail_editor_remove_a();
        rail_editor_remove_b();
        for (DuplicatedTile* piece = pieces_; piece;) {
            DuplicatedTile* next = piece->following_;
            park_editing::delete_piece(*this, piece);
            piece = next;
        }
        flag_bits_ &= ~(ParkAssembler::kPiecesCreated | ParkAssembler::kHasPieces);
        delete_marked_sectors(scene_handle_);
    }
    for (const TemplateTile* source = sources_; source;) {
        const TemplateTile* next = source->following_;
        delete source;
        source = next;
    }
    sources_ = nullptr;
    flag_bits_ &= ~ParkAssembler::kSourcesLoaded;
}

void ParkAssembler::add_piece_to_scene(ParkTile* p)
{
    DuplicatedTile* piece = get_cloned(p);
    flag_bits_ |= ParkAssembler::kSceneChanged;
    const int cursor = (piece->flag_bits_ & DuplicatedTile::kCursor) ? 1 : 0;
    piece->sector_ = park_editing::duplicate_scene_sector(source_world_, piece->blend_source_->sector_, scene_handle_,
                                                          cursor, !cursor);
    render::WorldCell* sector = look_up_sector(piece->sector_);
    const maths::Vec4 position = piece->at_point_;
    place_at_absolute_point(sector, &position);
    spin_around_up(sector, piece->quarter_turns_);
    if (piece->flag_bits_ & DuplicatedTile::kGap)
        place_gap_piece(piece);
    light_piece(p);
}

void ParkAssembler::rebuild_all()
{
    scene_updates() = false;
    for (const DuplicatedTile* piece = pieces_; piece; piece = piece->following_)
        park_editing::remove_scene_sector(scene_handle_, piece->sector_);
    rail_editor_remove_a();
    rail_editor_remove_b();
    ParkLayout::singleton(); // (the result unused)
    remove_node_array();     // (inline in the original)
    scene_updates() = true;
    delete_marked_sectors(scene_handle_);
    for (DuplicatedTile* piece = pieces_; piece; piece = piece->following_)
        add_piece_to_scene(piece);
    if (scene_updates())
        get_update_super_sectors(*this);
    scene_updates() = false;
    rail_editor_rebuild();
    scene_updates() = true;
    get_update_super_sectors(*this);
    mark_progress("CleanMainHeap - post cleanup");
}

void ParkAssembler::rebuild_marked()
{
    scene_updates() = false;
    for (const DuplicatedTile* piece = pieces_; piece; piece = piece->following_) {
        if (piece->blend_source_->flag_bits_ & TemplateTile::kRebuilt) {
            park_editing::remove_scene_sector(scene_handle_, piece->sector_);
            if (scene_updates())
                get_update_super_sectors(*this);
        }
    }
    for (DuplicatedTile* piece = pieces_; piece; piece = piece->following_) {
        if (piece->blend_source_->flag_bits_ & TemplateTile::kRebuilt)
            add_piece_to_scene(piece);
    }
    scene_updates() = true;
    get_update_super_sectors(*this);
}

void ParkAssembler::sweep_up(int /*type*/)
{
    unload_pieces();
    source_rails_.get_clear();
    source_rails_.free_pools();
}

void ParkAssembler::load_source_pieces(int /*unused*/)
{
    if (flag_bits_ & ParkAssembler::kSourcesLoaded)
        unload_pieces();
    flag_bits_ |= ParkAssembler::kSourcesLoaded;
    next_piece_id_ = 0x20;
    source_world_ = look_up_scene("sk5ed");
    // every sector of the scene (its sector table at +0xD0)
    lists::KeyMap* sectors = source_world_->sectors_;
    sectors->iter_bucket_ = -1;
    sectors->iter_item_ = nullptr;
    render::WorldCell* sector = nullptr;
    while (sectors->step_iterator() && sectors->iter_item_ &&
           (sector = static_cast<render::WorldCell*>(sectors->iter_item_->value)) != nullptr) {
        memory::enter_heap_scope(get_park_heap());
        auto source = memory::make_owned<TemplateTile>();
        memory::leave_heap_scope();
        source->following_ = sources_;
        sources_ = source;
        source->sector_ = sector->checksum_;
        const maths::BBox* box = get_local_bounds(sector);
        const maths::Vec4& min = box->min;
        const maths::Vec4& max = box->max;
        int cells = static_cast<int>((static_cast<double>(max.x) - min.x + kSizeSlack) * kInverseCell);
        if (cells < 1)
            cells = 1;
        source->extent_.x = static_cast<float>(cells * static_cast<double>(kGridPitch));
        cells = static_cast<int>((static_cast<double>(max.z) - min.z + kSizeSlack) * kInverseCell);
        if (cells < 1)
            cells = 1;
        source->extent_.z = static_cast<float>(cells * static_cast<double>(kGridPitch));
        const double height = static_cast<double>(max.y) - min.y;
        source->extent_.y = static_cast<float>(height);
        if (height < kGridStepUp && height > 0.0)
            source->extent_.y = kGridStepUp; // (at least a step high)
    }
    scene_handle_ = clone_scene("cloned", source_world_->texture_set_, 1);
    park_editing::set_up_bounds(this);
    for (RestartPoint& restart : restarts_)
        restart.kind_tag_ = RestartPoint::kFree;
}

void ParkAssembler::load_pieces(uint32_t level, int unused)
{
    load_source_pieces(unused);
    script::Array* nodes = level_nodes::get_last_node_array();
    park_editing::read_piece_flags(this, nodes);
    if (level == kParkEditorLevel) {
        memory::enter_heap_scope(memory::top_down_heap());
        build_source_rails(nodes);
        memory::leave_heap_scope();
    } else {
        build_source_rails(nodes);
    }
}

} // namespace park_editing
