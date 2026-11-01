// The scene side of the park (park_editing::ParkAssembler, first part: 0x00591C70-0x00592740): the source pieces and the
// pieces cloned from them into the park's scene, the park's restart points' slots, its lights, its scene's bounds and
// collision; and the lists of metapieces the park manager hands out (CMapListNode, CellChainScratch).

#include "game/park_editing/park.h"

#include <cstdint>
#include <cstring>

#include "engine/assets/resource_entry.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "game/park_editing/park_nx.h"
#include "game/park_editing/park_scene.h"
#include "engine/maths/shape_tests.h"
#include "game/park/park_memory.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/park_editor.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace park_editing {

namespace {
int g_cloning_piece;

// Whether the generator rebuilds its collision (off while every piece is deleted).
bool& rebuilds_collision() { return g_collision_allowed; }
// Raised while create_piece clones a sector.
int& cloning_piece() { return g_cloning_piece; }

// The members at +0x24 and +0x58 have empty destructors (calls to the empty function 0x005480A0).
inline void destroy_empty_member(void* /*member*/) {}

// 0x0049F4B0 (a folded `return false`): a colour to give a new piece's geometry - never.
inline bool get_piece_color(ParkTile* /*piece*/, uint32_t* /*rgba*/) { return false; }

// The park's main memory a player takes (the bottom-up heap's free memory per player, 0x0008EF1C).
constexpr int kMemoryPerPlayer = 585500;

} // namespace

// --- CMapListNode / CellChainScratch ---------------------------------------------------------------------------------

void CellChainScratch::dump_to_log()
{
    debug::printf("contents of map list:\n"); // "contents of map list:\n"
    for (const MapListNode* node = list; node; node = node->following_) {
        debug::printf("   address=%p area=", node->piece_group_); // "   address=%p area="
        node->piece_group_->cell_footprint_.dump_to_log();
    }
}

void CellChainScratch::teardown()
{
    if (list)
        MapListNode::free_node_chain(list);
}

// --- ParkAssembler ---------------------------------------------------------------------------------------------

namespace {

uint8_t* scene_of(void* scene) { return static_cast<uint8_t*>(scene); }

} // namespace

ParkAssembler::~ParkAssembler()
{
    if (scene_handle_)
        scene_handle_ = nullptr;
    if (source_world_)
        source_world_ = nullptr;
    // (the node array's owner, made in place by make_new)
    raw::as<assets::NodeTableResource>(node_array_.data())->~NodeTableResource();
    destroy_empty_member(&rails_);
    destroy_empty_member(&source_rails_);
}

void ParkAssembler::glow_every_piece(bool on)
{
    for (ParkTile* piece = pieces_; piece;) {
        ParkTile* next = piece->following_;
        piece->set_highlight(on, false);
        piece = next;
    }
}

void ParkAssembler::set_player_cap(int count) { max_players_ = count; }

int ParkAssembler::get_player_cap_limit()
{
    MemoryStats stats{};
    get_memory_stats(&stats);
    // (the original's 32-bit arithmetic, wrapping)
    const int memory = static_cast<int>(static_cast<uint32_t>(stats.main_heap_free_) +
                                        static_cast<uint32_t>(max_players_) * static_cast<uint32_t>(kMemoryPerPlayer) -
                                        static_cast<uint32_t>(kMemoryPerPlayer));
    return memory / kMemoryPerPlayer + 1;
}

TileTemplate* ParkAssembler::get_piece_source(uint32_t name)
{
    for (TileTemplate* source = sources_; source; source = source->following_) {
        if (source->sector_ == name)
            return source;
    }
    return nullptr;
}

bool ParkAssembler::remove_scene_sector(uint32_t name, int /*unused*/)
{
    return scene::remove_scene_sector(scene_of(scene_handle_), name);
}

void ParkAssembler::destroy_rails()
{
    rails_.get_clear();
    rails_.free_pools();
}

void ParkAssembler::configure_lighting(int count, float ambient_r, float ambient_g, float ambient_b, float falloff_r,
                                       float falloff_g, float falloff_b, float cursor_ambience)
{
    light_total_ = count;
    core::at(ambient_, 0) = ambient_r;
    core::at(ambient_, 1) = ambient_g;
    core::at(ambient_, 2) = ambient_b;
    core::at(falloff_, 0) = falloff_r;
    core::at(falloff_, 1) = falloff_g;
    core::at(falloff_, 2) = falloff_b;
    std::memcpy(&field_43c_, &cursor_ambience, 4); // (the cursor's ambience)
}

void ParkAssembler::fetch_spawn_slots(int type, int* first, int* last)
{
    switch (type) {
    case RestartPoint::kPlayer1:
        *first = 1;
        *last = 1;
        break;
    case RestartPoint::kPlayer2:
        *first = 0;
        *last = 0;
        break;
    case RestartPoint::kHorse:
        *first = 2;
        *last = 7;
        break;
    case RestartPoint::kCrownRestart:
        *first = 8;
        *last = 13;
        break;
    case RestartPoint::kRedFlag: // the four flags, one each
    case RestartPoint::kRedFlag + 1:
    case RestartPoint::kRedFlag + 2:
    case RestartPoint::kYellowFlag:
        *first = 14 + (type - RestartPoint::kRedFlag);
        *last = *first;
        break;
    default: // (left as they were)
        break;
    }
}

bool ParkAssembler::has_free_restart_slot(int type)
{
    int first = 0;
    int last = 0;
    fetch_spawn_slots(type, &first, &last);
    for (int i = first; i <= last; ++i) {
        if (!core::at(restarts_, i).kind_tag_ || core::at(restarts_, i).automatic_)
            return true;
    }
    return false;
}

void ParkAssembler::clear_restarts()
{
    for (RestartPoint& slot : restarts_)
        slot.kind_tag_ = RestartPoint::kFree;
}

void ParkAssembler::purge_auto_restart_points()
{
    for (RestartPoint& slot : restarts_) {
        if (slot.automatic_)
            slot.kind_tag_ = RestartPoint::kFree;
    }
}

void ParkAssembler::set_up_bounds()
{
    // The most the park can take up: 56 x 56 cells, 16 steps down and 66 up.
    maths::BBox box;
    const maths::Vec4 low = {-3360.0f, -768.0f, -3360.0f, 1.0f};
    box.append_spot(low);
    const maths::Vec4 high = {3360.0f, 3168.0f, 3360.0f, 1.0f};
    box.append_spot(high);
    scene::assign_scene_bounds(scene_of(scene_handle_), &box);
}

void ParkAssembler::rebuild_collision()
{
    if (!rebuilds_collision())
        return;
    flag_bits_ &= ~ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    scene::get_update_super_sectors(scene_of(scene_handle_), nullptr);
    memory::leave_heap_scope();
    flag_bits_ |= ParkAssembler::kSuperSectorsUpdated;
}

void ParkAssembler::update_bounds(uint32_t /*unused*/)
{
    ParkLayout* manager = ParkLayout::singleton();
    uint8_t* shell = scene::look_up_scene(manager->get_shell_checksum());
    if (!shell)
        return;
    // The shell's first box: as big as can be.
    const maths::Vec4 far = {200000.0f, 200000.0f, 200000.0f, 1.0f};
    const maths::Vec4 near = {-200000.0f, -200000.0f, -200000.0f, 1.0f};
    maths::Seg everything(near, far);
    scene::set_box(shell, &everything, 1, 0);
    // The second: the park's bounds, from the middle of its first cell to the middle of its last, every height.
    CellBox area{};
    area.assign_value(0, 0, 0, 0, 0, 0);
    area = ParkLayout::singleton()->build_area_;
    maths::Vec4 corner = ParkLayout::singleton()->cell_to_world_pos(area, nullptr);
    area.x = static_cast<uint8_t>(area.x + (area.w - 1));
    area.z = static_cast<uint8_t>(area.z + (area.l - 1));
    maths::Vec4 far_corner = ParkLayout::singleton()->cell_to_world_pos(area, nullptr);
    corner.x = static_cast<float>(static_cast<double>(corner.x) + 60.0);
    corner.y = -100000.0f;
    corner.z = static_cast<float>(static_cast<double>(corner.z) + 60.0);
    far_corner.y = 100000.0f;
    far_corner.x = static_cast<float>(static_cast<double>(far_corner.x) + 60.0);
    far_corner.z = static_cast<float>(static_cast<double>(far_corner.z) + 60.0);
    maths::Seg park(corner, far_corner);
    scene::set_box(shell, &park, 0, 0);
}

void ParkAssembler::set_pieces_highlighted(int on)
{
    for (ParkTile* piece = pieces_; piece;) {
        ParkTile* next = piece->following_;
        if (piece->flag_bits_ & ParkTile::kGap)
            piece->set_visible(on != 0);
        piece = next;
    }
}

void ParkAssembler::set_restart_pieces_active(bool active)
{
    for (ParkTile* piece = pieces_; piece;) {
        ParkTile* next = piece->following_;
        if (ParkLayout::singleton()->get_restart_index(piece->blend_source_->sector_))
            piece->change_live(active);
        piece = next;
    }
}

uint32_t ParkAssembler::duplicate_scene_sector(uint32_t name, int a, int b, int /*unused*/)
{
    flag_bits_ |= ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    const uint32_t clone =
        scene::duplicate_scene_sector(scene_of(source_world_), name, scene_of(scene_handle_), a, b, 0);
    memory::leave_heap_scope();
    return clone;
}

bool ParkAssembler::destroy_sector(uint32_t name, int /*unused*/)
{
    const bool deleted = scene::remove_scene_sector(scene_of(scene_handle_), name);
    rebuild_collision();
    return deleted;
}

ParkTile* ParkAssembler::create_piece(ParkPiece* source, uint32_t flags)
{
    flags &= ParkTile::kCursor | ParkTile::kNoNode20 | ParkTile::kNoRails | ParkTile::kDestroyNow;
    TileTemplate* from = (source->flag_bits_ & ParkPiece::kSource) ? core::down_cast<TileTemplate>(source)
                                                                   : core::down_cast<ParkTile>(source)->blend_source_;
    // The source's sector copied into the park's scene (on the bottom-up heap).
    cloning_piece() = 1;
    const uint32_t name = from->sector_;
    flag_bits_ |= ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    const uint32_t clone =
        (flags & ParkTile::kCursor)
            ? scene::duplicate_scene_sector(scene_of(source_world_), name, scene_of(scene_handle_), 1, 0, 0)
            : scene::duplicate_scene_sector(scene_of(source_world_), name, scene_of(scene_handle_), 0, 1, 0);
    memory::leave_heap_scope();
    cloning_piece() = 0;
    // The piece itself on the park editor's heap.
    memory::enter_heap_scope(get_park_heap());
    auto* piece = new CopiedPiece;
    memory::leave_heap_scope();
    piece->sector_ = clone; // (out of memory crashes, as in the original)
    piece->ident_ = next_piece_id_++;
    piece->blend_source_ = from;
    piece->set_quarter_turns(0, 0);
    piece->flag_bits_ |= flags;
    uint32_t rgba = 0;
    if (get_piece_color(piece, &rgba)) { // (never)
        if (uint8_t* sector = scene::look_up_sector(clone)) {
            if (uint8_t* geometry = scene::fetch_shape(sector))
                scene::change_tint(geometry, rgba);
        }
    }
    set_counts_dirty();
    flag_bits_ |= ParkAssembler::kPiecesCreated;
    return piece;
}

void ParkAssembler::insert_tile(ParkTile* piece)
{
    piece->flag_bits_ |= ParkTile::kInList;
    piece->following_ = pieces_;
    pieces_ = piece;
    flag_bits_ |= ParkAssembler::kHasPieces;
}

void ParkAssembler::delete_piece(ParkTile* piece, int keep_sector)
{
    if (!(piece->flag_bits_ & ParkTile::kCursor)) {
        ParkTile* previous = nullptr;
        for (ParkTile* p = pieces_; p; previous = p, p = p->following_) {
            if (p != piece)
                continue;
            if (previous)
                previous->following_ = p->following_;
            else
                pieces_ = p->following_;
            break;
        }
    }
    if (!keep_sector)
        destroy_sector(piece->sector_, static_cast<int>(piece->flag_bits_));
    set_counts_dirty();
    delete piece;
    if (!pieces_)
        flag_bits_ &= ~ParkAssembler::kHasPieces;
}

void ParkAssembler::read_piece_flags(script::Array* nodes)
{
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_class = 0;
        node->read_hash_value(0x12B4E660u, &node_class, 0);         // Class
        if (node_class != 0xBF4D3536u && node_class != 0xEF59C100u) // LevelGeometry, GameObject
            continue;
        uint32_t name = 0;
        node->read_hash_value(0xA1DC81F9u, &name, 0); // name
        if (!name)
            continue;
        TileTemplate* source = get_piece_source(name);
        if (!source)
            continue;
        if (node_class == 0xEF59C100u) {
            source->flag_bits_ |= TileTemplate::kGameObject;
            if (node->has_bare_flag(0x68910AC6u))
                source->flag_bits_ |= TileTemplate::kAbsentInNetGames;
        }
        uint32_t script = 0;
        node->read_hash_value(0x2CA8A299u, &script, 0); // TriggerScript
        if (!script)
            continue;
        source->trigger_script_ = script;
        switch (script) { // (four scripts that change their pieces: rebuilt by rebuild_marked)
        case 0x3DEDAFE9u:
        case 0x22C20ED8u:
        case 0x28673697u:
        case 0xF6187E48u:
            source->flag_bits_ |= TileTemplate::kRebuilt;
            break;
        default:
            break;
        }
    }
}

} // namespace park_editing

