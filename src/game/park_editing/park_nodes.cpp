// The node array the park's level runs on, built by the park generator: a node for every placed piece, for every
// point of the pieces' rails (made from the rails the piece types carry in the pieces' node array) and the rail
// editor's, for the restart points (and the CTF flags), and the goal editor's nodes. The data is in park_scene.h.

#include "game/park_editing/park_scene.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/assets/resource_entry.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/park_editing/park.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "engine/script/game_cfuncs.h"
#include "engine/core/ints.h"
#include "engine/core/raw_view.h"

namespace park_editing {

namespace {
int g_rail_search_start;

using script::Array;
using script::Struct;

// Node components (kFlag: an unnamed one, a flag).
constexpr uint32_t kFlag = 0;
constexpr uint32_t kClass = 0x12B4E660;          // Class
constexpr uint32_t kName = 0xA1DC81F9;           // Name
constexpr uint32_t kType = 0x7321A8D6;           // Type
constexpr uint32_t kPos = 0x7F261953;            // pos
constexpr uint32_t kAngles = 0x9D2D0915;         // angles
constexpr uint32_t kCluster = 0x1A3A966B;        // Cluster
constexpr uint32_t kLinks = 0x2E7D5EE7;          // links
constexpr uint32_t kSurfaceKindKey = 0x54CF8532; // TerrainType
constexpr uint32_t kTriggerScript = 0x2CA8A299;  // TriggerScript
constexpr uint32_t kNoClimbing = 0xF22BFC6D;     // NoClimbing
constexpr uint32_t kModel = 0x286A8D26;          // model
constexpr uint32_t kRestartName = 0xFCD206E1;    // RestartName
constexpr uint32_t kRestartTypes = 0xDD304987;   // restart_types
constexpr uint32_t kCreatedAtStart = 0x7C2552B9; // CreatedAtStart
constexpr uint32_t kTrickObject = 0x1645B830;    // TrickObject
// Node classes.
constexpr uint32_t kLevelGeometry = 0xBF4D3536; // LevelGeometry
constexpr uint32_t kRailNode = 0x8E6B02AD;      // RailNode
constexpr uint32_t kClimbingNode = 0x30C19600;  // ClimbingNode
constexpr uint32_t kRestart = 0x1806DDF8;       // Restart
constexpr uint32_t kGenericNode = 0xF8565321;   // GenericNode
constexpr uint32_t kGameObject = 0xEF59C100;    // GameObject
// A piece's node: its flag (by its type's flags).
constexpr uint32_t kPieceFlag = 0x4DFAAF7A;
constexpr uint32_t kGameObjectFlag = 0x9783D434;
constexpr uint32_t kNetAbsentFlag = 0x511A7334;
// A rail node's flag (climbable or not), a climbing node's.
constexpr uint32_t kClimbableRailFlag = 0x4953E02A;
constexpr uint32_t kRailFlag = 0xD1E32F9B;
constexpr uint32_t kClimbingFlag = 0x0E4C5B11;
// Restart types.
constexpr uint32_t kPlayerType = 0x67E6859A;       // Player
constexpr uint32_t kPlayer1 = 0x41639CE5;          // Player1
constexpr uint32_t kHorse = 0x9D65D0E7;            // horse
constexpr uint32_t kMultiplayer = 0xBAE0E4DA;      // Multiplayer
constexpr uint32_t kCrown = 0xAF86421B;            // Crown
constexpr uint32_t kCtf = 0xA5AD2B0B;              // CTF
constexpr uint32_t kHighScoreRestart = 0xC62C3240; // TRG_G_TrickAttack_RestartNode
// TRG_CTF_Restart_Red, _Green, _Blue, _Yellow
constexpr uint32_t kCtfRestarts[4] = {0xACD3422F, 0x5C427A5C, 0x9AB434CC, 0x2C3ED607};
constexpr const char* kFlagColours[4] = {"red", "green", "blue", "yellow"};
// The pieces whose climbing rails turn: the ladders (by pi), two more (by pi / 2); the lamp has none.
constexpr uint32_t kLadderSmall = 0x19E41B83; // sk6ed_ladderSmall
constexpr uint32_t kLadderMid = 0x2716A345;   // sk6ed_ladderMid
constexpr uint32_t kLadderTall = 0xA94E4D6A;  // sk6ed_ladderTall
constexpr uint32_t kLamp = 0xD48417FF;        // sk6ed_Lamp
constexpr uint32_t kQuarterTurnedA = 0x649FDDCA;
constexpr uint32_t kQuarterTurnedB = 0x54055089;
constexpr float kPi = 3.14159274101257324f;     // 0x40490FDB
constexpr float kHalfPi = 1.57079637050628662f; // 0x3FC90FDB (also 0x00645484)
// The script run before the source rails are read.
constexpr uint32_t kLoadTerrain = 0xA4F9A278; // LoadTerrain
// The first names of the rail nodes and the restart nodes, and the rail editor's.
constexpr uint32_t kFirstRailNode = 0x001E8481;
constexpr uint32_t kFirstRestartNode = 0x005B8D81;
constexpr uint32_t kFirstRailEditorNode = 0x003D0901;

// Where find_following_rail_node's search for a piece's rail nodes starts (0x007CE600: its first rail node).
int& rail_search_start() { return g_rail_search_start; }

// The rail editor (0x004F4970): its rail points (0x004F3C70), its rails written as nodes (0x004F6920).
RailAuthoring* get_rail_editor() { return RailAuthoring::sole(); }
int get_rail_editor_points() { return get_rail_editor()->rail_point_total(); }
void rail_editor_write_nodes(Array* nodes, int* index, uint32_t first_name)
{
    get_rail_editor()->write_nodes(nodes, index, first_name);
}
// The goal editor's nodes written into the node array (0x004E8F50).
void goal_editor_write_nodes(Array* nodes, int* index, memory::Arena* heap)
{
    add_goal_editor_nodes(nodes, index, heap);
}
// The gap manager: the trigger script of a gap's piece
uint32_t gap_fire_script(DuplicatedTile* piece) { return GapRegistry::sole()->get_gap_trigger_name(piece); }
// cfunc_parse_node_array (0x005A34E0): the node manager's node array parsed (its objects made).
void parse_node_array() { commands::cfunc_parse_node_array(nullptr, nullptr); }

assets::NodeTableResource* node_table_resource(ParkAssembler& d)
{
    return raw::as<assets::NodeTableResource>(d.node_array_);
}
Struct* elem(Array* nodes, int index)
{
    return nodes->struct_at(static_cast<uint32_t>(index));
}
script::VectorValue get_value(const maths::Vec4& v) { return {v.x, v.y, v.z, v.w}; }
script::VectorValue* get_out(maths::Vec4* v) { return v; }
// A restart's angles: (rot + 2) quarter turns about y.
float restart_angle(int rot) { return static_cast<float>((rot + 2.0) * static_cast<double>(kHalfPi)); }

// A new script array of one `type` element.
Array* new_array_of(script::QbValueKind type, uint32_t value)
{
    Array* array = script::alloc_script_array();
    if (array)
        new (array) script::Array;
    array->configure_size_kind(1, type);
    array->set_element(0, value);
    return array;
}
void free_array(Array* array)
{
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}

} // namespace

uint32_t ParkAssembler::next_geometry_node(Array* nodes, int* index)
{
    const int count = static_cast<int>(nodes->extent_);
    for (; *index < count; ++*index) {
        Struct* node = elem(nodes, *index);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        uint32_t name = 0;
        if (node_class == kLevelGeometry && node->read_hash_value(kName, &name, 0)) {
            ++*index;
            return name;
        }
    }
    return 0;
}

uint32_t ParkAssembler::find_following_rail_node(Array* nodes, uint32_t geometry, uint32_t previous, RailNodeInfo* info,
                                                 bool* is_loop)
{
    auto used = [this](int node) {
        bool found = false;
        for (int i = 0; i < num_used_nodes_; ++i) {
            if (used_nodes_[i] == node)
                found = true;
        }
        return found;
    };
    int linked = -1; // a node linked from another (the next after `previous`, or one of a rail with no end)
    int first = -1;  // a node with no links: a rail's first
    int from = 0;
    if (!previous) {
        from = 0;
        rail_search_start() = 0;
    } else {
        from = rail_search_start();
    }
    const int count = static_cast<int>(nodes->extent_);
    for (int i = from; i < count; ++i) {
        const PieceNodeInfo& node = node_info_[i];
        if (node.class_ != kRailNode && node.class_ != kClimbingNode)
            continue;
        uint32_t link = 0;
        if (node.link_set_)
            link = node.link_set_->fetch_part(0);
        if (previous) {
            if (link != previous)
                continue;
            linked = used(i) ? -1 : i;
            if (linked != -1)
                break;
        } else {
            if (geometry != node.cluster_)
                continue;
            if (rail_search_start() == 0)
                rail_search_start() = i;
            if (!link) {
                first = used(i) ? -1 : i;
                if (first != -1)
                    break;
            } else if (linked == -1) {
                linked = used(i) ? -1 : i;
            }
        }
    }
    int found = -1;
    if (linked != -1) {
        found = linked;
        *is_loop = true;
    }
    if (first != -1) {
        found = first;
        *is_loop = false;
    }
    if (found == -1)
        return 0;
    used_nodes_[num_used_nodes_++] = found;
    Struct* node = elem(nodes, found);
    std::memset(info, 0, sizeof(RailNodeInfo));
    node->fetch_vec(kPos, get_out(&info->at_point_), 1);
    node->fetch_vec(kAngles, get_out(&info->headings_), 0);
    node->read_hash_value(kClass, &info->class_, 1);
    node->read_hash_value(kType, &info->kind_tag_, 1);
    if (info->class_ == kRailNode)
        node->read_hash_value(kSurfaceKindKey, &info->surface_type_, 1);
    info->climbable_ = true;
    if (node->has_bare_flag(kNoClimbing))
        info->climbable_ = false;
    return level_nodes::get_node_name(node);
}

void ParkAssembler::build_source_rails(Array* nodes)
{
    RailGroup& rails = source_rails_;
    const int strings = get_memory_value("in_railstring_pool", -1);
    const int points = get_memory_value("in_railpoint_pool", -1);
    rails.prepare_all(points, strings, true);
    script::kill_all_child_scripts();
    script::launch_script(kLoadTerrain, nullptr, nullptr, false, nullptr);
    rails.get_clear();

    // what's needed of each node
    const int count = static_cast<int>(nodes->extent_);
    used_nodes_ = static_cast<int*>(memory::alloc(0x400)); // (room for 256)
    num_used_nodes_ = 0;
    node_info_ = static_cast<PieceNodeInfo*>(memory::alloc(count * sizeof(PieceNodeInfo)));
    for (int i = 0; i < ints::as_signed(nodes->extent_); ++i) {
        Struct* node = elem(nodes, i);
        PieceNodeInfo& info = node_info_[i];
        info.class_ = 0;
        node->read_hash_value(kClass, &info.class_, 0);
        info.cluster_ = 0;
        node->read_hash_value(kCluster, &info.cluster_, 0);
        info.link_set_ = nullptr;
        node->fetch_list(kLinks, &info.link_set_, 0);
    }

    // each piece's rails: from a first node (or, for a loop, any) along the links
    int index = 0;
    uint32_t geometry = next_geometry_node(nodes, &index);
    while (index < ints::as_signed(nodes->extent_)) {
        num_used_nodes_ = 0;
        bool is_loop = false;
        RailNodeInfo info{};
        uint32_t node = find_following_rail_node(nodes, geometry, 0, &info, &is_loop);
        while (node) {
            Rail* rail = rails.get_new_rail();
            rail->class_ = info.class_;
            rail->label_ = geometry;
            rail->flag_bits_ = static_cast<uint8_t>((rail->flag_bits_ & ~Rail::kLoop) | (is_loop ? Rail::kLoop : 0));
            RailPoint* point = rails.append_rail_vertex();
            point->at_point_ = info.at_point_;
            point->headings_ = info.headings_;
            point->kind_tag_ = info.kind_tag_;
            point->surface_type_ = info.surface_type_;
            rail->append_spot(point);
            for (node = find_following_rail_node(nodes, geometry, node, &info, &is_loop); node;
                 node = find_following_rail_node(nodes, geometry, node, &info, &is_loop)) {
                point = rails.append_rail_vertex();
                point->at_point_ = info.at_point_;
                point->headings_ = info.headings_;
                point->kind_tag_ = info.kind_tag_;
                point->surface_type_ = info.surface_type_;
                rail->append_spot(point);
                if ((rail->flag_bits_ & Rail::kClimbable) && !info.climbable_)
                    rail->flag_bits_ &= static_cast<uint8_t>(~Rail::kClimbable);
            }
            rails.add_rail(rail);
            is_loop = false;
            node = find_following_rail_node(nodes, geometry, 0, &info, &is_loop);
        }
        geometry = next_geometry_node(nodes, &index);
    }
    memory::dealloc(used_nodes_);
    memory::dealloc(node_info_);

    // each source piece's rail points and segments
    for (TemplateTile* source = sources_; source; source = source->following_) {
        source->rail_point_total_ = 0;
        source->num_rail_segments_ = 0;
        for (int i = 0; const Rail* rail = rails.find_rail(source->sector_, i); ++i) {
            source->rail_point_total_ += rail->vertex_total_;
            source->num_rail_segments_ += rail->count_segments();
        }
    }
}

bool ParkAssembler::find_nearest_rail_point(const maths::Vec4* position, maths::Vec4* nearest, float* distance_squared)
{
    bool found = false;
    float best = 1e8f;
    for (const Rail* rail = rails_.rail_groups_; rail; rail = rail->following_) {
        for (const RailPoint* point = rail->point_chain_; point; point = point->following_) {
            const double dx = static_cast<double>(point->at_point_.x) - position->x;
            const double dy = static_cast<double>(point->at_point_.y) - position->y;
            const double dz = static_cast<double>(point->at_point_.z) - position->z;
            const double distance = dz * dz + dx * dx + dy * dy;
            if (distance < best) {
                nearest->x = point->at_point_.x;
                nearest->y = point->at_point_.y;
                nearest->z = point->at_point_.z;
                nearest->w = point->at_point_.w;
                best = static_cast<float>(distance);
                *distance_squared = static_cast<float>(distance);
                found = true;
            }
        }
    }
    return found;
}

void ParkAssembler::get_node_counts(int* counts)
{
    for (int i = 0; i < 6; ++i)
        counts[i] = 0;
    for (const DuplicatedTile* piece = pieces_; piece; piece = piece->following_) {
        if (!(piece->flag_bits_ & (DuplicatedTile::kNoNode20 | DuplicatedTile::kNoNode80)))
            ++counts[0];
        if (!(piece->flag_bits_ & DuplicatedTile::kCursor) && !(piece->flag_bits_ & DuplicatedTile::kNoRails)) {
            counts[1] += piece->blend_source_->rail_point_total_;
            counts[2] += piece->blend_source_->num_rail_segments_;
        }
    }
    const int points = counts[1] + get_rail_editor_points();
    ++counts[0]; // (the high score restart)
    counts[1] = points;
    for (const RestartPoint& restart : restarts_) {
        const int type = restart.kind_tag_;
        if (type != RestartPoint::kFree)
            ++counts[0];
        if (type == RestartPoint::kPlayer1)
            ++counts[0]; // (and a multiplayer one)
        if (type == RestartPoint::kHorse)
            ++counts[0];
        if (type >= RestartPoint::kRedFlag && type <= RestartPoint::kYellowFlag)
            counts[0] += 3; // (the flag, its CTF node and its base)
    }
    counts[5] = points;
    counts[3] = points * 7 + counts[0] * 6 + 0x4F6 + counts[2];
    counts[4] = counts[0] + 300 + points * 2;
}

void ParkAssembler::build_goal_node_array()
{
    const int count = goal_editor_constant82();
    int index = 0;
    auto* nodes = static_cast<Array*>(node_table_resource(*this)->build_node_table(kParkNodeArray, count));
    goal_editor_write_nodes(nodes, &index, memory::top_down_heap());
    level_nodes::NodeManager* manager = level_nodes::node_manager_instance();
    if (manager->has_node_array(kParkNodeArray))
        manager->remove_node_array(kParkNodeArray);
    manager->add_node_array(kParkNodeArray, true);
    manager->set_owner(node_array_);
}

void ParkAssembler::remove_node_array()
{
    level_nodes::NodeManager* manager = level_nodes::node_manager_instance();
    if (manager->has_node_array(kParkNodeArray)) {
        manager->remove_node_array(kParkNodeArray);
        if (script::SymbolTableEntry* symbol = script::find_symbol(kParkNodeArray))
            script::clean_up_and_remove_symbol(symbol);
    }
}

void ParkAssembler::build_rails(MapListNode* metas)
{
    RailGroup& rails = rails_;
    rails.get_clear();
    rails.free_pools();
    memory::enter_heap_scope(memory::top_down_heap());
    const int strings = get_memory_value("out_railstring_pool", -1);
    const int points = get_memory_value("out_railpoint_pool", -1);
    rails.prepare_all(points, strings, false);
    memory::leave_heap_scope();
    for (const MapListNode* node = metas; node; node = node->following_) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        if (!(meta->flag_bits_ & PieceGroup::kInPark) || (meta->flag_bits_ & PieceGroup::kNoRails))
            continue;
        const int pieces = static_cast<int>(meta->filled_slots_);
        for (int i = 0; i < pieces; ++i) {
            const DuplicatedTile* piece = get_cloned(meta->get_piece(static_cast<uint32_t>(i)));
            for (int j = 0;; ++j) {
                const uint32_t name = piece->blend_source_->sector_;
                const Rail* source = source_rails_.find_rail(name, j);
                if (!source)
                    break;
                Rail* rail = rails.get_new_rail();
                uint32_t cluster = piece->ident_;
                if (meta->flag_bits_ & PieceGroup::kBuiltShell)
                    cluster = 0;
                float angle = 0.0f;
                if (name == kLadderSmall || name == kLadderMid || name == kLadderTall)
                    angle = kPi;
                if (name == kLamp) {
                    rails.free_rail(rail);
                    continue;
                }
                if (name == kQuarterTurnedA || name == kQuarterTurnedB)
                    angle = kHalfPi;
                rail->copy_from(source, &piece->at_point_, piece->quarter_turns_, cluster, angle);
                rails.add_rail(rail);
            }
        }
    }
}

void ParkAssembler::write_piece_nodes(Array* nodes, int* index)
{
    for (DuplicatedTile* piece = pieces_; piece; piece = piece->following_) {
        if (piece->flag_bits_ & (DuplicatedTile::kNoNode20 | DuplicatedTile::kNoNode80))
            continue;
        Struct* node = elem(nodes, *index);
        node->put_vec3(kPos, get_value(piece->at_point_));
        node->put_name_key(kName, piece->sector_);
        const uint32_t flags = piece->blend_source_->flag_bits_;
        if (!(flags & TemplateTile::kGameObject)) {
            node->put_name_key(kFlag, kPieceFlag);
            node->put_name_key(kCluster, ~piece->ident_);
        } else if (flags & TemplateTile::kAbsentInNetGames) {
            node->put_name_key(kFlag, kNetAbsentFlag);
        } else {
            node->put_name_key(kFlag, kGameObjectFlag);
        }
        if (piece->flag_bits_ & DuplicatedTile::kGap)
            node->put_name_key(kTriggerScript, gap_fire_script(piece));
        else if (const uint32_t script = piece->blend_source_->trigger_script_)
            node->put_name_key(kTriggerScript, script);
        ++*index;
    }
}

void ParkAssembler::write_rail_nodes(Array* nodes, int* index)
{
    const RailGroup& rails = rails_;
    const int count = rails.rail_point_total();
    if (!count)
        return;
    const int end = *index + count;
    const Rail* rail = rails.rail_groups_;
    const RailPoint* point = rail->point_chain_;
    int rail_start = *index; // the node a loop's last point links to
    uint32_t name = kFirstRailNode;
    while (*index < end) {
        Struct* node = elem(nodes, *index);
        if (rail->class_ == kRailNode) {
            node->put_vec3(kPos, get_value(point->at_point_));
            node->put_name_key(kName, name++);
            node->put_name_key(kFlag, (rail->flag_bits_ & Rail::kClimbable) ? kClimbableRailFlag : kRailFlag);
            node->put_name_key(kSurfaceKindKey, point->surface_type_);
            if (point->cluster_) {
                node->put_name_key(kFlag, kTrickObject);
                node->put_name_key(kCluster, ~point->cluster_);
            }
        } else {
            node->put_vec3(kPos, get_value(point->at_point_));
            node->put_vec3(kAngles, get_value(point->headings_));
            node->put_name_key(kName, name++);
            node->put_name_key(kFlag, kClimbingFlag);
            node->put_name_key(kType, point->kind_tag_);
        }
        if (point->following_ || (rail->flag_bits_ & Rail::kLoop)) {
            int link = *index + 1;
            if (!point->following_ && (rail->flag_bits_ & Rail::kLoop))
                link = rail_start;
            Array* links = new_array_of(script::kQbvalInteger, static_cast<uint32_t>(link));
            node->append_array_value(kLinks, links);
            free_array(links);
        }
        point = point->following_;
        if (!point) {
            rail = rail->following_;
            if (rail) {
                point = rail->point_chain_;
                rail_start = *index + 1;
            }
        }
        ++*index;
    }
}

void ParkAssembler::write_restart_nodes(Array* nodes, int* index)
{
    uint32_t name = kFirstRestartNode;

    // the high score restart, at the first player 1 restart
    for (const RestartPoint& restart : restarts_) {
        if (restart.kind_tag_ != RestartPoint::kPlayer1)
            continue;
        Struct* node = elem(nodes, *index);
        node->put_vec3(kPos, get_value(restart.at_point_));
        node->put_vec3(kAngles, 0.0f, restart_angle(restart.quarter_turns_), 0.0f);
        node->put_name_key(kName, kHighScoreRestart);
        node->put_name_key(kClass, kRestart);
        node->put_name_key(kType, kPlayerType);
        node->append_word(kRestartName, "High Score");
        node->put_name_key(kFlag, kCreatedAtStart);
        Array* types = new_array_of(script::kQbvalName, kPlayer1);
        node->append_array_value(kRestartTypes, types);
        free_array(types);
        ++*index;
        break;
    }

    for (const RestartPoint& restart : restarts_) {
        const int type = restart.kind_tag_;
        if (type == RestartPoint::kFree)
            continue;
        // player 1 and horse restarts are multiplayer ones too
        const int restart_nodes = (type == RestartPoint::kHorse || type == RestartPoint::kPlayer1) ? 2 : 1;
        const bool flag = type >= RestartPoint::kRedFlag && type <= RestartPoint::kYellowFlag;
        for (int k = 0; k < restart_nodes; ++k) {
            uint32_t node_name = name++;
            uint32_t restart_type = 0;
            if (type == RestartPoint::kPlayer1)
                restart_type = kPlayer1;
            if (type == RestartPoint::kHorse)
                restart_type = kHorse;
            if (type == RestartPoint::kPlayer2 || k == 1)
                restart_type = kMultiplayer;
            if (type == RestartPoint::kCrownRestart)
                restart_type = kCrown;
            if (flag) {
                restart_type = kCtf;
                node_name = kCtfRestarts[type - RestartPoint::kRedFlag];
            }
            if (restart_type) {
                Struct* node = elem(nodes, *index);
                const auto y = static_cast<float>(static_cast<double>(restart.at_point_.y) + 50.0);
                node->put_vec3(kPos, restart.at_point_.x, y, restart.at_point_.z);
                const uint32_t node_class = type == RestartPoint::kCrownRestart ? kGenericNode : kRestart;
                node->add_component_of_type(kClass, script::kQbvalName, node_class);
                node->put_vec3(kAngles, 0.0f, restart_angle(restart.quarter_turns_), 0.0f);
                node->add_component_of_type(kName, script::kQbvalName, node_name);
                node->add_component_of_type(kType, script::kQbvalName, restart_type);
                node->add_component_of_type(kFlag, script::kQbvalName, kCreatedAtStart);
                if (type != RestartPoint::kCrownRestart) {
                    const char* label = "Multi Restart";
                    if (k == 0) {
                        switch (type) {
                        case RestartPoint::kPlayer1:
                            label = "Player 1 Restart";
                            break;
                        case RestartPoint::kPlayer2:
                            label = "Player 2 Restart";
                            break;
                        case RestartPoint::kHorse:
                            label = "Horse Restart";
                            break;
                        case 5:
                        case 6:
                        case 7:
                        case 8:
                            label = "Team: CTF";
                            break;
                        default:
                            break;
                        }
                    }
                    node->append_word(kRestartName, script::kQbvalString, label);
                    Array* types = new_array_of(script::kQbvalName, restart_type);
                    node->append_array_value(kRestartTypes, types);
                    free_array(types);
                }
                ++*index;
            }
            if (!flag)
                continue;

            // the flag: the flag itself, its CTF node and its base
            const char* colour = kFlagColours[type - RestartPoint::kRedFlag];
            char flag_type[256], model[256], script[256], flag_name[256];
            auto add_flag_node = [&](bool with_script) {
                Struct* node = elem(nodes, *index);
                node->put_vec3(kPos, restart.at_point_.x, restart.at_point_.y, restart.at_point_.z);
                node->put_vec3(kAngles, 0.0f, restart_angle(restart.quarter_turns_), 0.0f);
                node->add_component_of_type(kName, script::kQbvalName, checksums::hash_from_text(flag_name));
                node->add_component_of_type(kClass, script::kQbvalName, kGameObject);
                node->add_component_of_type(kType, script::kQbvalName, checksums::hash_from_text(flag_type));
                node->append_word(kModel, script::kQbvalString, model);
                if (with_script)
                    node->add_component_of_type(kTriggerScript, script::kQbvalName, checksums::hash_from_text(script));
                node->put_int("nodeIndex", *index);
                node->put_int("SuspendDistance", 0);
                node->put_int("lod_dist1", 800);
                node->put_int("lod_dist2", 801);
                ++*index;
            };
            std::snprintf(flag_type, sizeof(flag_type), "Flag_%s", colour);
            std::snprintf(model, sizeof(model), R"(gameobjects\flags\flag_%s\flag_%s.mdl)", colour, colour);
            std::snprintf(script, sizeof(script), "TRG_Flag_%sScript_Parked", colour);
            std::snprintf(flag_name, sizeof(flag_name), "TRG_Flag_%s", colour);
            add_flag_node(true);
            std::snprintf(flag_name, sizeof(flag_name), "TRG_CTF_%s", colour);
            add_flag_node(true);
            std::snprintf(flag_name, sizeof(flag_name), "TRG_CTF_%s_Base", colour);
            std::snprintf(flag_type, sizeof(flag_type), "Flag_%s_Base", colour);
            std::snprintf(model, sizeof(model), R"(gameobjects\flags\flag_%s_base\flag_%s_base.mdl)", colour, colour);
            add_flag_node(false);
        }
    }
}

void ParkAssembler::build_node_array(MapListNode* metas)
{
    int counts[6];
    get_node_counts(counts);
    const int rail_editor_points = get_rail_editor_points();
    int total = counts[0] + (goal_editor_constant82() + rail_editor_points);
    build_rails(metas);
    total += rails_.rail_point_total();
    level_nodes::NodeManager* manager = level_nodes::node_manager_instance();
    if (manager->has_node_array(kParkNodeArray))
        manager->remove_node_array(kParkNodeArray);
    auto* nodes = static_cast<Array*>(node_table_resource(*this)->build_node_table(kParkNodeArray, total));
    int index = 0;
    write_piece_nodes(nodes, &index);
    write_rail_nodes(nodes, &index);
    rail_editor_write_nodes(nodes, &index, kFirstRailEditorNode);
    write_restart_nodes(nodes, &index);
    rails_.get_clear();
    rails_.free_pools();
    goal_editor_write_nodes(nodes, &index, memory::top_down_heap());
    manager->add_node_array(kParkNodeArray, true);
    manager->set_owner(node_array_);
    parse_node_array();
    flag_bits_ |= ParkAssembler::kNodeArrayBuilt;
}

} // namespace park_editing
