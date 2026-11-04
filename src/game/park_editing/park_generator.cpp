// The scene side of the park (park_editing::ParkAssembler, first part: 0x00591C70-0x00592740): the source pieces and the
// pieces cloned from them into the park's scene, the park's restart points' slots, its lights, its scene's bounds and
// collision; and the lists of metapieces the park manager hands out (CMapListNode, CellChainScratch).

#include "game/park_editing/park.h"

#include <cstdint>
#include <cstring>

#include "engine/assets/resource_entry.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "game/park_editing/park_nx.h"
#include "game/park_editing/park_scene.h"
#include "engine/maths/shape_tests.h"
#include "game/park/park_memory.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/park_editor.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/park_editing_state.h"
#include <gsl/pointers>

namespace park_editing {

namespace {

constexpr std::uint32_t kAbsentInNetGames = 0x68910AC6U; // AbsentInNetGames
constexpr std::uint32_t kClass = 0x12B4E660U;            // Class
constexpr std::uint32_t kGameObject = 0xEF59C100U;       // GameObject
constexpr std::uint32_t kLevelGeometry = 0xBF4D3536U;    // LevelGeometry
constexpr std::uint32_t kName = 0xA1DC81F9U;             // Name
constexpr std::uint32_t kTriggerScript = 0x2CA8A299U;    // TriggerScript
constexpr int kHorseLastSlot = 7;              // the automatic restart slots: 2 to 7 horse restarts,
constexpr int kCrownFirstSlot = 8;             // 8 to 13 crown restarts,
constexpr int kCrownLastSlot = 13;
constexpr int kFlagFirstSlot = 14;             // then one for each flag
constexpr double kWorldCellMargin = 60.0;      // how far the park's probe segment reaches past the cells' corners
constexpr float kWorldYExtent = 100000.0F;     // the probe segment runs from -this to +this in height
constexpr std::uint32_t kPieceChangingScript1 =
    0x3DEDAFE9U; // trigger scripts whose pieces rebuild_marked rebuilds (text unknown)
constexpr std::uint32_t kPieceChangingScript2 = 0x22C20ED8U;
constexpr std::uint32_t kPieceChangingScript3 = 0x28673697U;
constexpr std::uint32_t kPieceChangingScript4 = 0xF6187E48U;

// Whether the generator rebuilds its collision (off while every piece is deleted).

// Raised while create_piece clones a sector.
int& cloning_piece()
{
    static int cloning_piece{};
    return cloning_piece;
}

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
    for (const MapListNode* node = list_; node; node = node->following()) {
        debug::printf("   address=%p area=", node->piece_group()); // "   address=%p area="
        node->piece_group()->cell_footprint().dump_to_log();
    }
}

void CellChainScratch::teardown()
{
    if (list_) {
        MapListNode::free_node_chain(list_);
    }
}

// --- ParkAssembler ---------------------------------------------------------------------------------------------

ParkAssembler::~ParkAssembler()
{
    if (scene_handle_) {
        scene_handle_ = nullptr;
    }
    if (source_world_) {
        source_world_ = nullptr;
    }
    node_table_.reset(); // (the node array's owner)
    destroy_empty_member(&rails_);
    destroy_empty_member(&source_rails_);
}

void ParkAssembler::glow_every_piece(bool on)
{
    for (ParkTile* piece = pieces_; piece;) {
        ParkTile* next = piece->following();
        piece->set_highlight(on, false);
        piece = next;
    }
}

void ParkAssembler::set_player_cap(int count) { max_players_ = count; }

int ParkAssembler::get_player_cap_limit() const
{
    MemoryStats stats{};
    get_memory_stats(&stats);
    // (the original's 32-bit arithmetic, wrapping)
    const int memory =
        static_cast<int>(static_cast<uint32_t>(stats.main_heap_free_) +
                         (static_cast<uint32_t>(max_players_) * static_cast<uint32_t>(kMemoryPerPlayer)) -
                         static_cast<uint32_t>(kMemoryPerPlayer));
    return (memory / kMemoryPerPlayer) + 1;
}

TileTemplate* ParkAssembler::get_piece_source(uint32_t name)
{
    for (TileTemplate* source = sources_; source; source = source->following()) {
        if (source->sector() == name) {
            return source;
        }
    }
    return nullptr;
}

bool ParkAssembler::remove_scene_sector(uint32_t name, int /*unused*/)
{
    return scene::remove_scene_sector(scene_handle_, name);
}

void ParkAssembler::destroy_rails()
{
    rails_.get_clear();
    rails_.free_pools();
}

void ParkAssembler::configure_lighting(int count, const ParkLighting& lighting)
{
    const auto ambient_r = lighting.ambient_r_;
    const auto ambient_g = lighting.ambient_g_;
    const auto ambient_b = lighting.ambient_b_;
    const auto falloff_r = lighting.falloff_r_;
    const auto falloff_g = lighting.falloff_g_;
    const auto falloff_b = lighting.falloff_b_;
    const auto cursor_ambience = lighting.cursor_ambience_;
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
        *last = kHorseLastSlot;
        break;
    case RestartPoint::kCrownRestart:
        *first = kCrownFirstSlot;
        *last = kCrownLastSlot;
        break;
    case RestartPoint::kRedFlag: // the four flags, one each
    case RestartPoint::kRedFlag + 1:
    case RestartPoint::kRedFlag + 2:
    case RestartPoint::kYellowFlag:
        *first = kFlagFirstSlot + (type - RestartPoint::kRedFlag);
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
        if ((core::at(restarts_, i).kind_tag_ == 0) || (core::at(restarts_, i).automatic_ != 0U)) {
            return true;
        }
    }
    return false;
}

void ParkAssembler::clear_restarts()
{
    for (RestartPoint& slot : restarts_) {
        slot.kind_tag_ = RestartPoint::kFree;
    }
}

void ParkAssembler::purge_auto_restart_points()
{
    for (RestartPoint& slot : restarts_) {
        if (slot.automatic_ != 0U) {
            slot.kind_tag_ = RestartPoint::kFree;
        }
    }
}

void ParkAssembler::set_up_bounds()
{
    // The most the park can take up: 56 x 56 cells, 16 steps down and 66 up.
    maths::BBox box;
    const maths::Vec4 low = {.x = -3360.0F, .y = -768.0F, .z = -3360.0F, .w = 1.0F};
    box.append_spot(low);
    const maths::Vec4 high = {.x = 3360.0F, .y = 3168.0F, .z = 3360.0F, .w = 1.0F};
    box.append_spot(high);
    scene::assign_scene_bounds(scene_handle_, &box);
}

void ParkAssembler::rebuild_collision()
{
    if (!park_editing_state().collision_allowed()) {
        return;
    }
    flag_bits_ &= ~ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    scene::get_update_super_sectors(scene_handle_, nullptr);
    memory::leave_heap_scope();
    flag_bits_ |= ParkAssembler::kSuperSectorsUpdated;
}

void ParkAssembler::update_bounds(uint32_t /*unused*/)
{
    ParkLayout* manager = ParkLayout::singleton();
    render::WorldScene* shell = scene::look_up_scene(manager->get_shell_checksum());
    if (!shell) {
        return;
    }
    // The shell's first box: as big as can be.
    const maths::Vec4 far = {.x = 200000.0F, .y = 200000.0F, .z = 200000.0F, .w = 1.0F};
    const maths::Vec4 near = {.x = -200000.0F, .y = -200000.0F, .z = -200000.0F, .w = 1.0F};
    maths::Seg everything(near, far);
    scene::set_box(shell, &everything, 1, 0);
    // The second: the park's bounds, from the middle of its first cell to the middle of its last, every height.
    CellBox area{};
    area.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
    area = ParkLayout::singleton()->build_area();
    maths::Vec4 corner = park_editing::ParkLayout::cell_to_world_pos(area, nullptr);
    area.x = static_cast<uint8_t>(area.x + (area.w - 1));
    area.z = static_cast<uint8_t>(area.z + (area.l - 1));
    maths::Vec4 far_corner = park_editing::ParkLayout::cell_to_world_pos(area, nullptr);
    corner.x = static_cast<float>(static_cast<double>(corner.x) + kWorldCellMargin);
    corner.y = -kWorldYExtent;
    corner.z = static_cast<float>(static_cast<double>(corner.z) + kWorldCellMargin);
    far_corner.y = kWorldYExtent;
    far_corner.x = static_cast<float>(static_cast<double>(far_corner.x) + kWorldCellMargin);
    far_corner.z = static_cast<float>(static_cast<double>(far_corner.z) + kWorldCellMargin);
    maths::Seg park(corner, far_corner);
    scene::set_box(shell, &park, 0, 0);
}

void ParkAssembler::set_pieces_highlighted(int on)
{
    for (ParkTile* piece = pieces_; piece;) {
        ParkTile* next = piece->following();
        if ((piece->flag_bits() & ParkTile::kGap) != 0U) {
            piece->set_visible(on != 0);
        }
        piece = next;
    }
}

void ParkAssembler::set_restart_pieces_active(bool active)
{
    for (ParkTile* piece = pieces_; piece;) {
        ParkTile* next = piece->following();
        if (ParkLayout::singleton()->get_restart_index(piece->blend_source()->sector()) != 0) {
            piece->change_live(active);
        }
        piece = next;
    }
}

uint32_t ParkAssembler::duplicate_scene_sector(uint32_t name, int a, int b, int /*unused*/)
{
    flag_bits_ |= ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    const uint32_t clone =
        scene::duplicate_scene_sector(source_world_, name, scene_handle_, a, b, 0);
    memory::leave_heap_scope();
    return clone;
}

bool ParkAssembler::destroy_sector(uint32_t name, int /*unused*/)
{
    const bool deleted = scene::remove_scene_sector(scene_handle_, name);
    rebuild_collision();
    return deleted;
}

gsl::owner<ParkTile*> ParkAssembler::create_piece(ParkPiece* source, uint32_t flags)
{
    flags &= ParkTile::kCursor | ParkTile::kNoNode20 | ParkTile::kNoRails | ParkTile::kDestroyNow;
    TileTemplate* from = ((source->flag_bits() & ParkPiece::kSource) != 0U)
                             ? core::down_cast<TileTemplate>(source)
                             : core::down_cast<ParkTile>(source)->blend_source();
    // The source's sector copied into the park's scene (on the bottom-up heap).
    cloning_piece() = 1;
    const uint32_t name = from->sector();
    flag_bits_ |= ParkAssembler::kSceneChanged;
    memory::enter_heap_scope(memory::bottom_up_heap());
    const uint32_t clone = ((flags & ParkTile::kCursor) != 0U)
                               ? scene::duplicate_scene_sector(source_world_, name, scene_handle_, 1, 0, 0)
                               : scene::duplicate_scene_sector(source_world_, name, scene_handle_, 0, 1, 0);
    memory::leave_heap_scope();
    cloning_piece() = 0;
    // The piece itself on the park editor's heap.
    memory::enter_heap_scope(get_park_heap());
    gsl::owner<park_editing::CopiedPiece*> piece{memory::make_owned<CopiedPiece>()};
    memory::leave_heap_scope();
    piece->set_sector(clone); // (out of memory crashes, as in the original)
    piece->set_ident(next_piece_id_++);
    piece->set_blend_source(from);
    piece->set_quarter_turns({.rot = 0, .update = 0});
    piece->flag_bits() |= flags;
    uint32_t rgba = 0;
    if (get_piece_color(piece, &rgba)) { // (never)
        if (render::WorldCell* sector = scene::look_up_sector(clone)) {
            if (render::Geom* geometry = scene::fetch_shape(sector)) {
                scene::change_tint(geometry, rgba);
            }
        }
    }
    set_counts_dirty();
    flag_bits_ |= ParkAssembler::kPiecesCreated;
    return piece;
}

void ParkAssembler::insert_tile(ParkTile* piece)
{
    piece->flag_bits() |= ParkTile::kInList;
    piece->set_following(pieces_);
    pieces_ = piece;
    flag_bits_ |= ParkAssembler::kHasPieces;
}

void ParkAssembler::delete_piece(ParkTile* piece, int keep_sector)
{
    if ((piece->flag_bits() & ParkTile::kCursor) == 0U) {
        ParkTile* previous = nullptr;
        for (ParkTile* tile = pieces_; tile; previous = tile, tile = tile->following()) {
            if (tile != piece) {
                continue;
            }
            if (previous) {
                previous->set_following(tile->following());
            } else {
                pieces_ = tile->following();
            }
            break;
        }
    }
    if (keep_sector == 0) {
        destroy_sector(piece->sector(), static_cast<int>(piece->flag_bits()));
    }
    set_counts_dirty();
    gsl::owner<decltype(piece)> owned = memory::owned_element(piece); // (the generator's list's)
    delete owned;
    if (!pieces_) {
        flag_bits_ &= ~ParkAssembler::kHasPieces;
    }
}

void ParkAssembler::read_piece_flags(script::Array* nodes)
{
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);         // Class
        if (node_class != kLevelGeometry && node_class != kGameObject) { // LevelGeometry, GameObject
            continue;
        }
        uint32_t name = 0;
        node->read_hash_value(kName, &name, 0); // name
        if (name == 0U) {
            continue;
        }
        TileTemplate* source = get_piece_source(name);
        if (!source) {
            continue;
        }
        if (node_class == kGameObject) {
            source->flag_bits() |= TileTemplate::kGameObject;
            if (node->has_bare_flag(kAbsentInNetGames)) {
                source->flag_bits() |= TileTemplate::kAbsentInNetGames;
            }
        }
        uint32_t script = 0;
        node->read_hash_value(kTriggerScript, &script, 0); // TriggerScript
        if (script == 0U) {
            continue;
        }
        source->set_trigger_script(script);
        switch (script) { // (four scripts that change their pieces: rebuilt by rebuild_marked)
        case kPieceChangingScript1:
        case kPieceChangingScript2:
        case kPieceChangingScript3:
        case kPieceChangingScript4:
            source->flag_bits() |= TileTemplate::kRebuilt;
            break;
        default:
            break;
        }
    }
}

} // namespace park_editing

