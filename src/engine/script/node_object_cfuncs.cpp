// Script commands (CFunctions) of the skate game, 0x005A2A00-0x005A34E0: the viewer camera fitted to the scenes,
// and the level's node objects made and killed by their class (see game_cfuncs.h).

#include "engine/script/game_cfuncs.h"
#include "engine/gfx/d3dx.h"
#include "engine/script/trick_cfuncs.h"

#include <cmath>
#include <cstdint>
#include <utility>

#include "engine/gfx/engine_geom.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/tod_manager.h"
#include "engine/lists/key_map.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/model_component.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/node_objects.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/proximity.h"
#include "engine/objects/rail_manager.h"
#include "game/skate/gap_checklist.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/node_manager.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/sector.h"
#include "game/skate/skate_module.h"
#include "engine/script/script_startup.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movie_manager.h"
#include "engine/script/skate_cfuncs.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/sys/game_config.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace commands {

namespace {
float g_node_object_minimum_area{5000.0f};
} // namespace
gfx::CtodManager* g_tod_manager;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;       // "name"
constexpr uint32_t kClass = 0x12B4E660;      // "class"
constexpr uint32_t kPos = 0x7F261953;        // "pos"
constexpr uint32_t kAngles = 0x9D2D0915;     // "angles"
constexpr uint32_t kScale = 0x13B9DA7B;      // "scale"
constexpr uint32_t kCloneFrom = 0xEDC063B9;  // (name unknown) a level geometry node's sector to copy
constexpr uint32_t kOccluder = 0x4E549FE1;   // (name unknown) a level geometry node's flag (0x0048BF90)
constexpr uint32_t kLevelColor = 0x99A9B716; // (name unknown) a level object's colour, added from the time of day
constexpr uint32_t kViewerCameraObject = 0xEB17151B; // (name unknown) the viewer's camera object

// The node classes.
constexpr uint32_t kClassPedestrian = 0xA0DFAC98;     // "Pedestrian"
constexpr uint32_t kClass061a741e = 0x061A741E;       // (name unknown) made as a pedestrian
constexpr uint32_t kClassClimbingNode = 0x30C19600;   // "ClimbingNode"
constexpr uint32_t kClassRailNode = 0x8E6B02AD;       // "RailNode"
constexpr uint32_t kClassProximNode = 0x08470F2E;     // "ProximNode"
constexpr uint32_t kClassParticleObject = 0x9E7D469E; // "ParticleObject"
constexpr uint32_t kClassEmitterObject = 0xD64DC7C2;  // "EmitterObject"
constexpr uint32_t kClassLevelObject = 0xB7B3BD86;    // "LevelObject"
constexpr uint32_t kClassLevelGeometry = 0xBF4D3536;  // "LevelGeometry"
constexpr uint32_t kClassVehicle = 0xE47F1B79;        // "Vehicle"
constexpr uint32_t kClassGameObject = 0xEF59C100;     // "GameObject"
constexpr uint32_t kClassTrigger = 0xE594F0A2;        // "Trigger"
constexpr uint32_t kClassSkater = 0x5B8AB877;         // "skater"
constexpr uint32_t kClassA0e52802 = 0xA0E52802;       // (name unknown) killed through 0x0048FCA0

uint8_t* get_skate() { return raw::bytes(modules::g_skate_module); }
objects::GrindRailHub* rail_manager() { return modules::g_skate_module->rail_manager_.get(); }
memory::Hub* mem_manager() { return memory::Hub::get_pull(); }

// Whether the skate module leaves a node out (0x00565BB0: its flag 0x68910AC6, in a game with more than one player).
bool skips_node(script::Struct* node) { return modules::Skate::should_be_absent(node) != 0; }

// The scenes (Nx), from their list (0x006FA7EC; 0x0048C7F0 gives the first node, its +8 the next - the one with
// data -1 ends it - and +0 the scene; memory::ListNode).
struct SceneNode {
    uint8_t* scene; // +0x00 (memory::ListNode's data)
    int priority;
    SceneNode* next; // +0x08
};
static_assert(sizeof(SceneNode) == 0x0C, "memory::ListNode's data, priority and next");
SceneNode* first_scene() { return raw::as<SceneNode>(render::first_scene_node()); }
SceneNode* next_scene(SceneNode* node)
{
    return node->next->scene == cell::pointer<uint8_t>(-1) ? nullptr : node->next;
}

// A scene's sector by name (0x0049CB00) and a copy of one (0x0049D510).
render::WorldCell* as_sector(uint8_t* sector) { return raw::as<render::WorldCell>(sector); }
render::WorldScene* as_scene(uint8_t* scene) { return raw::as<render::WorldScene>(scene); }
uint8_t* look_up_sector(uint8_t* scene, uint32_t name)
{
    return static_cast<uint8_t*>(as_scene(scene)->look_up_sector(name));
}
void duplicate_scene_sector(uint8_t* scene, uint8_t* sector, uint8_t* destination, int a, int b, uint32_t name)
{
    as_scene(scene)->duplicate_scene_sector(as_sector(sector), as_scene(destination), a, b != 0, name);
}
// A sector's (WorldCell, thiscall) settings: the viewports it shows in (0x0049E680), drawn (0x0049E9B0), collided
// with (0x0049EA30), its orientation (0x0049E700), scale (0x0049E7C0), position (0x0049E6C0).
void toggle_shown(uint8_t* sector, uint32_t mask) { as_sector(sector)->toggle_shown(mask); }
void change_live(uint8_t* sector, bool active) { as_sector(sector)->change_live(active); }
void enable_collision(uint8_t* sector, bool on) { as_sector(sector)->enable_collision(on); }
void change_facing(uint8_t* sector, const maths::Mat4* m) { as_sector(sector)->change_facing(m); }
void change_scale(uint8_t* sector, const script::VectorValue* scale)
{
    as_sector(sector)->change_scale(scale);
}
void change_place(uint8_t* sector, const script::VectorValue* pos)
{
    as_sector(sector)->place_at_absolute_point(pos);
}
// A sector name's flag 0x4E549FE1 effect on or off (0x0048BF90, a jump to 0x004B22F0).
void mark_as_occluder(uint32_t name, int on) { render::flag_debug_lines(name, on != 0); }

} // namespace

// 0x005A2A00: the viewer's camera object (0xEB17151B) placed to look at the sectors of `scene` (those named `only`,
// or all): the box round their active geometry's vertices; the camera turned by `angleX` / `angleY` / `angleZ`
// degrees (in that order, from no turn) and set back from the box's centre along its forward axis by the box's size
// (at least 1000) times `scale` squared.
void fit_viewer_camera(uint8_t* scene, float scale, float angle_x, float angle_y, float angle_z, uint32_t only)
{
    maths::BBox box;
    auto* sectors = *raw::field_at<lists::KeyMap*>(scene, 0xD0);
    if (sectors) {
        sectors->iter_bucket_ = -1;
        sectors->iter_item_ = nullptr;
        for (const bool more = sectors->step_iterator(); more && sectors->iter_item_;) {
            auto* sector = static_cast<uint8_t*>(sectors->iter_item_->value);
            if (!sector)
                break;
            // (the sector's name: the getter folded at 0x005480C0, its +4)
            if (!only || *raw::field_at<uint32_t>(sector, 4) == only) {
                uint8_t* geometry = nullptr;
                if (as_sector(sector)->is_operative()) // active
                    geometry = static_cast<uint8_t*>(as_sector(sector)->geom_handle_);
                auto* geom = raw::as<render::Geom>(geometry);
                const int count = geom ? static_cast<int>(geom->fetch_vertex_tally()) : 0;
                if (count) {
                    if (!memory::g_manager_ready) // (the memory manager set up, if it isn't)
                        memory::ensure_manager();
                    auto* vertices =
                        static_cast<maths::Vec4*>(mem_manager()->carve(count * sizeof(maths::Vec4), 1, nullptr));
                    // (the original runs the vertices' empty constructor over the block, 0x00401020 with 0x005902E0)
                    geom->get_vertices(vertices);
                    for (int i = 0; i < count; ++i)
                        box.append_spot(vertices[i]);
                    mem_manager()->dealloc(vertices);
                }
            }
            if (!sectors->step_iterator() || !sectors->iter_item_)
                break;
        }
    }
    auto* camera = static_cast<objects::GameEntity*>(
        static_cast<objects::ObjectHubBase*>(objects::g_composite_registry)->look_up_entity(kViewerCameraObject));
    if (!camera)
        return;
    const maths::Vec4& lo = box.min;
    const maths::Vec4& hi = box.max;
    const double half = gfx::g_half; // (0.5)
    maths::Vec4 centre;
    centre.x = static_cast<float>((static_cast<double>(hi.x) + lo.x) * half);
    centre.y = static_cast<float>((static_cast<double>(hi.y) + lo.y) * half);
    centre.z = static_cast<float>(static_cast<float>(static_cast<double>(hi.z) + lo.z) * half);
    centre.w = static_cast<float>(static_cast<float>(static_cast<double>(hi.w) + lo.w) * half);
    const double dx = static_cast<double>(hi.x) - lo.x;
    const double dy = static_cast<double>(hi.y) - lo.y;
    const double dz = static_cast<double>(hi.z) - lo.z;
    const double size = std::sqrt(dz * dz + dy * dy + dx * dx);
    auto distance = static_cast<float>(size);
    if (size < gfx::g_thousand) // (1000)
        distance = 1000.0f;
    maths::Mat4& m = camera->basis_frame_;
    for (int row = 0; row < 4; ++row) {
        float* r = &m.row[row].x;
        for (int column = 0; column < 4; ++column)
            r[column] = row == column ? 1.0f : 0.0f;
    }
    const double to_radians = g_degree_to_radian_ratio;
    auto turn = [&](maths::Mat4& (*create)(maths::Mat4&, float), float degrees) {
        maths::Mat4 rotation;
        create(rotation, static_cast<float>(degrees * to_radians));
        d3dx::matrix_multiply(&m, &m, &rotation);
    };
    turn(&maths::make_pitch_matrix, angle_x);
    turn(&maths::make_yaw_matrix, angle_y);
    turn(&maths::make_roll_matrix, angle_z);
    const double back = static_cast<double>(distance) * scale * scale;
    const float* at = &m.row[2].x;
    auto bx = static_cast<float>(back * at[0]);
    auto by = static_cast<float>(back * at[1]);
    auto bz = static_cast<float>(back * at[2]);
    camera->at_point_.x = static_cast<float>(static_cast<double>(bx) + centre.x);
    camera->at_point_.y = static_cast<float>(static_cast<double>(by) + centre.y);
    camera->at_point_.z = static_cast<float>(static_cast<double>(bz) + centre.z);
    camera->at_point_.w = static_cast<float>(back * at[3] + centre.w);
}

// 0x005A2D60: CenterCamera [scale (0.9)] [x (-45)] [y (45)] [z (0)] [id=<sector>]: the viewer's camera fitted to each
// scene in turn (fit_viewer_camera; the last one's placing stays). False without the viewer module (0x007CE4A4).
bool cfunc_center_camera(script::Struct* params, script::Script*)
{
    if (!objects::g_view_mode)
        return false;
    float scale = 0.9f;
    params->fetch_float("scale", &scale, 0); // "scale"
    float x = -45.0f;
    params->fetch_float("x", &x, 0); // "x"
    float y = 45.0f;
    params->fetch_float("y", &y, 0); // "y"
    float z = 0.0f;
    params->fetch_float("z", &z, 0); // "z"
    uint32_t id = 0;
    params->read_hash_value("id", &id, 0); // "id"
    for (SceneNode* node = first_scene(); node;) {
        SceneNode* next = next_scene(node);
        fit_viewer_camera(node->scene, scale, x, y, z, id);
        node = next;
    }
    return true;
}

// 0x005A2E50: the object of a node made by its class (unless the skate module leaves the node out: true):
// pedestrians, rail and climbing nodes (switched on), proxim nodes and emitters (switched on), particle systems, level
// objects (with the time of day's level colour, if set), level geometry (the sector switched on - or, with
// 0xEDC063B9, a copy of that sector made under the node's name and placed by angles / scale / pos), vehicles, game
// objects. False for any other class (triggers too).
bool create_from_node(script::Struct* node)
{
    const uint32_t name = level_nodes::get_node_name(node);
    if (skips_node(node))
        return true;
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class == kClassTrigger) {
        uint32_t trigger_name = 0;
        node->read_hash_value(kName, &trigger_name, 1); // (read and not used)
    }
    switch (node_class) {
    case kClassPedestrian:
    case kClass061a741e:
        memory::push_mem_profile("Pedestrians"); // "Pedestrians"
        objects::spawn_pedestrian(node);
        memory::pop_mem_profile();
        return true;
    case kClassClimbingNode:
    case kClassRailNode:
        rail_manager()->change_live(name, 1, node->has_bare_flag("FLAG_WHOLE_RAIL")); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        objects::proximity_node_enable(name, 1);
        return true;
    case kClassParticleObject:
        memory::push_mem_profile("Particle Systems"); // "Particle Systems"
        objects::spawn_effect_entity(node);
        memory::pop_mem_profile();
        return true;
    case kClassEmitterObject:
        objects::emitter_set_active(name, 1);
        return true;
    case kClassLevelObject:
        memory::push_mem_profile("LevelObjects"); // "LevelObjects"
        if (g_tod_manager->params_set_) {
            uint32_t colour = 0;
            g_tod_manager->get_level_color(&colour);
            node->put_int(kLevelColor, static_cast<int>(colour));
        }
        objects::create_node_object(node);
        memory::pop_mem_profile();
        return true;
    case kClassVehicle:
        memory::push_mem_profile("Vehicles"); // "Vehicles"
        objects::spawn_automobile(node);
        memory::pop_mem_profile();
        return true;
    case kClassGameObject:
        memory::push_mem_profile("Game Objects"); // "Game Objects"
        objects::create_game_object_from_node(node);
        memory::pop_mem_profile();
        return true;
    case kClassLevelGeometry:
        break;
    default:
        return false;
    }
    // level geometry
    uint32_t section_id = 0;
    uint32_t clone_from = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    node->read_hash_value(kCloneFrom, &clone_from, 0);
    uint8_t* scene = nullptr;
    uint8_t* sector = nullptr;
    for (SceneNode* at = first_scene(); at && !sector;) {
        SceneNode* next = next_scene(at);
        scene = at->scene;
        sector = look_up_sector(scene, clone_from ? clone_from : section_id);
        at = next;
    }
    if (!sector) {
        // " WARNING: sGetSector(0x%x) returned NULL (%s)\n"
        debug::printf(" WARNING: sGetSector(0x%x) returned NULL (%s)\n", section_id,
                      script::find_checksum_name_or_unknown(section_id));
        return true;
    }
    if (!clone_from) {
        toggle_shown(sector, 0xFFFFFFFF);
        change_live(sector, true);
        enable_collision(sector, true);
        if (node->has_bare_flag(kOccluder))
            mark_as_occluder(section_id, 1);
        return true;
    }
    uint8_t* copy = look_up_sector(scene, section_id);
    if (!copy) {
        duplicate_scene_sector(scene, sector, scene, 1, 1, section_id);
        copy = look_up_sector(scene, section_id);
    }
    *raw::field_at<uint32_t>(sector, 8) |= 0x80;
    *raw::field_at<uint32_t>(copy, 8) &= ~0x80u;
    script::VectorValue angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (node->fetch_vec(kAngles, &angles, 0)) {
        maths::Mat4 orientation;
        orientation.init_from_angles(*&angles);
        change_facing(copy, &orientation);
    }
    script::VectorValue scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (node->fetch_vec(kScale, &scale, 0))
        change_scale(copy, &scale);
    script::VectorValue pos = {0.0f, 0.0f, 0.0f, 1.0f};
    node->fetch_vec(kPos, &pos, 0);
    change_place(copy, &pos);
    change_live(copy, true);
    enable_collision(copy, true);
    return true;
}

// 0x005A32A0: the object of the node named `name` made (create_from_node).
bool create_from_node_name(uint32_t name) { return create_from_node(level_nodes::fetch_vertex(name, true)); }

// 0x005A32C0: the object of the node named `name` killed by its class: objects (pedestrians, skaters, level objects,
// vehicles, game objects, particle systems, 0x061A741E) with the node's name deleted (unless locked); rail and
// climbing nodes, proxim nodes, emitters switched off; 0xA0E52802 through 0x0048FCA0; level geometry's sector switched
// off (its 0x4E549FE1 effect too). False for no node or any other class. (The original also passes the script's
// object to the walk; its callback doesn't read it.)
bool kill_from_node_name(uint32_t name, script::Script* script)
{
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    if (!node)
        return false;
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    switch (node_class) {
    case kClassPedestrian:
    case kClassSkater:
    case kClass061a741e:
    case kClassLevelObject:
    case kClassVehicle:
    case kClassGameObject:
    case kClassParticleObject: {
        struct {
            uint32_t id;
            objects::Entity* object;
        } data = {name, script->entity_.get_pull()};
        static_cast<objects::ObjectHubBase*>(objects::g_composite_registry)
            ->for_each(&destroy_object_with_id, &data);
        return true;
    }
    case kClassClimbingNode:
    case kClassRailNode:
        rail_manager()->change_live(name, 0, node->has_bare_flag("FLAG_WHOLE_RAIL")); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        objects::proximity_node_enable(name, 0);
        return true;
    case kClassA0e52802:
        render::delete_scene_light(name);
        return true;
    case kClassEmitterObject:
        objects::emitter_set_active(name, 0);
        return true;
    case kClassLevelGeometry: {
        uint32_t section_id = 0;
        if (!node->read_hash_value(kName, &section_id, 0))
            return true;
        for (SceneNode* at = first_scene(); at;) {
            SceneNode* next = next_scene(at);
            if (uint8_t* sector = look_up_sector(at->scene, section_id)) {
                change_live(sector, false);
                if (node->has_bare_flag(kOccluder))
                    mark_as_occluder(section_id, 0);
                mark_as_occluder(section_id, 0);
                return true;
            }
            at = next;
        }
        mark_as_occluder(section_id, 0);
        return true;
    }
    default:
        return false;
    }
}

// 0x005A34D0: CreateFromStructure <node structure>: create_from_node with the command's parameters.
bool cfunc_create_from_structure(script::Struct* params, script::Script*) { return create_from_node(params); }

namespace {

constexpr uint32_t kCreatedAtStart = 0x7C2552B9;  // (name unknown) a node's object made when the level starts
constexpr uint32_t kSurfaceKindKey = 0x7E4813A7;  // (name unknown) a level sector's terrain
constexpr uint32_t kDefaultTerrain = 0xE3714DC1;  // (name unknown) ... when none is given
constexpr uint32_t kTrickObject = 0x1645B830;     // (name unknown) a node that is a trick object
constexpr uint32_t kCluster = 0x1A3A966B;         // (name unknown) its cluster (its own name if none)
constexpr uint32_t kAllTrickObjects = 0xB675F8F6; // (name unknown) the global making every node a trick object
constexpr uint32_t kNoGapChecklist = 0xF7ECD937;  // (name unknown) the global leaving the gap checklist alone
constexpr uint32_t kRailRemove1 = 0x7321A8D6;     // (name unknown) removed from rail nodes with angles
constexpr uint32_t kGeometryRemove = 0x2D7E583B;  // (name unknown) removed from level geometry nodes
constexpr uint32_t kParkEditorOff = 0xD443A2BC;   // ParkBuilderMode::kStateOff

// The sector named `name` in any scene (0x0048C510; null if none).
uint8_t* find_sector(uint32_t name) { return static_cast<uint8_t*>(render::engine::look_up_sector(name)); }
bool park_editor_off() { return park_editing::g_park_editor->cur_phase_ == kParkEditorOff; }
objects::TrickSpotHub* get_trick_objects() { return objects::g_trick_spot_hub; }

} // namespace

// 0x005A34E0: ParseNodeArray [no_cleanup]: the level's node array (the node manager's 0x0045C220) set up: unless
// no_cleanup, the proxims, emitters, rails, trick object clusters and two more cleared and the rails made from the
// array. Then each node by class: rail nodes lose angles and 0x7321A8D6; level objects' sectors are switched off and
// given their terrain; level geometry loses 0x2D7E583B, its sector (outside the park editor, if the object is made at
// the start and the skate module doesn't skip it) shown and collided with, its occluder flag set, its terrain set
// (missing sector or name: messages), and trick objects (0x1645B830 or the global 0xB675F8F6) become their cluster's.
// Nodes the skate module doesn't skip: proxim nodes and emitters added; the objects made at the start made (level
// geometry only outside the park editor). Then the rails' overlaps, the gap checklist (unless 0xF7ECD937), every
// scene's 0x0049E1B0 and the 0xA0E52802 nodes' 0x00490570. Always true.
bool cfunc_parse_node_array(script::Struct* params, script::Script*)
{
    memory::push_mem_profile("ParseNodeArray"); // "ParseNodeArray"
    auto* nodes = cell::pointer<script::Array>(level_nodes::get_node_owner_value58());
    objects::GrindRailHub* rails = rail_manager();
    if (!params || !params->has_bare_flag("no_cleanup")) { // "no_cleanup"
        objects::nearby_zone_startup();
        objects::emitter_init();
        rail_manager()->tidy_up();
        get_trick_objects()->remove_all_clusters();
        render::delete_dynamic_lights();
        render::delete_all_scene_lights();
        memory::push_mem_profile("Rail Nodes"); // "Rail Nodes"
        rails->add_from_node_array(nodes);
        memory::pop_mem_profile();
    }
    // (a trick object's name: an asserted parameter, so the original's local - kept from one node to the next -
    // holds the last one read when a node has none)
    uint32_t object_name = 0;
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class == kClassRailNode) {
            node->erase_module(kAngles);
            node->erase_module(kRailRemove1);
        } else if (node_class == kClassLevelObject) {
            uint32_t name = 0;
            node->read_hash_value(kName, &name, 0);
            if (uint8_t* sector = find_sector(name)) {
                change_live(sector, false);
                uint32_t terrain = kDefaultTerrain;
                node->read_hash_value(kSurfaceKindKey, &terrain, 0);
                *raw::field_at<uint32_t>(sector, 0xC) = terrain;
            }
        } else if (node_class == kClassLevelGeometry) {
            node->erase_module(kGeometryRemove);
            uint32_t name = 0;
            node->read_hash_value(kName, &name, 0);
            if (!name) {
                script::dump_to_log(node, 0);
                debug::printf("FAILED BECAUSE Node %d is LevelGeometry, but has no Name=....\n",
                              i); // "FAILED BECAUSE Node %d is LevelGeometry..."
            }
            if (uint8_t* sector = find_sector(name)) {
                if (!park_editor_off()) {
                    if (node->has_bare_flag(kCreatedAtStart))
                        skips_node(node); // (asked, the answer unused)
                } else if (node->has_bare_flag(kCreatedAtStart) && !skips_node(node)) {
                    toggle_shown(sector, 0xFFFFFFFF);
                    change_live(sector, true);
                    enable_collision(sector, true);
                } else {
                    change_live(sector, false);
                }
                if (node->has_bare_flag(kOccluder)) {
                    as_sector(sector)->mark_as_occluder(true);
                    change_live(sector, false);
                    if (!node->has_bare_flag(kCreatedAtStart))
                        mark_as_occluder(name, 0);
                }
                uint32_t terrain = kDefaultTerrain;
                node->read_hash_value(kSurfaceKindKey, &terrain, 0);
                *raw::field_at<uint32_t>(sector, 0xC) = terrain;
            } else {
                uint32_t clone_from = 0;
                node->read_hash_value(kCloneFrom, &clone_from, 0);
                if (!clone_from && !modules::Skate::is_multiplayer() && park_editor_off()) {
                    // "FAILED BECAUSE: Cannot find level geometry or collision for node %d ..."
                    debug::printf("FAILED BECAUSE: Cannot find level geometry or collision for node %d \nMaybe there "
                                  "are spaces in or after the name of the Max object (%s)\?\nOr maybe the .QN file "
                                  "does not match the .SCN file checked into Perforce\?\n",
                                  i, script::find_checksum_name_or_unknown(name));
                }
            }
            bool trick = node->has_entry_named(kTrickObject);
            if (!game_config::g_from_cd && script::fetch_int_value(kAllTrickObjects))
                trick = true;
            if (!skips_node(node) && trick) {
                memory::push_mem_profile("Trick Object Clusters"); // "Trick Object Clusters"
                node->read_hash_value(kName, &object_name, true);
                uint32_t cluster = object_name;
                node->read_hash_value(kCluster, &cluster, false);
                get_trick_objects()->add_cluster(cluster);
                get_trick_objects()->add_object(object_name, cluster);
                memory::pop_mem_profile();
            }
        }
        if (skips_node(node))
            continue;
        if (node_class == kClassLevelGeometry) {
            if (!park_editor_off())
                continue;
        } else {
            if (node_class == kClassProximNode) {
                memory::push_mem_profile("Proximity Nodes");        // "Proximity Nodes"
                const bool active = node->has_bare_flag(kCreatedAtStart); // (asked before the name, as in the original)
                objects::nearby_zone_add_node(level_nodes::get_node_name(node), active);
                memory::pop_mem_profile();
            }
            if (node_class == kClassEmitterObject) {
                memory::push_mem_profile("Emitter Objects");        // "Emitter Objects"
                const bool active = node->has_bare_flag(kCreatedAtStart); // (asked before the name, as in the original)
                objects::emitter_add_node(level_nodes::get_node_name(node), active);
                memory::pop_mem_profile();
            }
        }
        if (node->has_bare_flag(kCreatedAtStart))
            create_from_node(level_nodes::fetch_vertex(level_nodes::get_node_name(node), true));
    }
    memory::pop_mem_profile();
    rails->remove_overlaps_if_not_park();
    if (!script::fetch_int_value(kNoGapChecklist))
        static_cast<goals::GapTally*>(raw::as<modules::Skate>(get_skate())->fetch_gap_list())->get_build();
    for (SceneNode* at = first_scene(); at;) {
        SceneNode* next = next_scene(at);
        as_scene(at->scene)->update_super_sectors_if_marked(nullptr);
        at = next;
    }
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class == kClassA0e52802)
            render::create_light_from_node(node);
    }
    memory::pop_mem_profile();
    return true;
}

// 0x005A39F0: ReLoadNodeArray: the level's node array (named at 0x007CE6D8) loaded again in the debug heap: the old
// asset unloaded and the node manager's array of the file taken out, the file loaded (NodeArrayManLoad and its .qb,
// 0x005A5960) and the node manager's last array put back in.
bool cfunc_re_load_node_array(script::Struct*, script::Script* script)
{
    mem_manager()->enter_heap_scope(static_cast<memory::MemSource*>(mem_manager()->debug_heap_));
    level_nodes::NodeManager* node_manager = level_nodes::node_manager_instance();
    const char* loaded_node_list = g_node_list_name;
    assets::ResourceRegistry* assets = assets::asset_manager();
    assets->discard_loaded_item(assets->obtain_loaded_item(checksums::hash_from_text(loaded_node_list), 0));
    alignas(4) uint8_t storage[8];
    auto* file = raw::as<script::Struct>(storage);
    new (file) script::Struct;
    file->append_word(0u, loaded_node_list);
    const char* name = nullptr;
    file->fetch_text(0u, &name, 1);
    level_nodes::node_manager_instance()->remove_node_array_of_file(name);
    cfunc_node_array_man_load(file, script);
    script::load_qb_file(loaded_node_list, 0);
    level_nodes::get_last_node_array();
    const uint32_t last = node_manager->get_last_node_array_name();
    node_manager->remove_node_array(last);
    node_manager->add_node_array(last, true);
    mem_manager()->leave_heap_scope();
    file->get_clear();
    return true;
}

namespace {

constexpr uint32_t kColor = 0x99A9B716;   // (name unknown) SetObjectColor's colour (0xBBGGRR)
constexpr uint32_t kTerrain = 0x9655CFF7; // (name unknown) the terrain a node's sector gets

// flag_command_by_name's walk data (game_flow_cfuncs.cpp) {id, value, action, result, found}.
struct NodeFlagCommand {
    uint32_t id;
    uint32_t value;
    int action;
    bool result;
    bool found;
};

// The first sector named `name` in any scene (null if none).
uint8_t* find_scene_sector(uint32_t name)
{
    uint8_t* sector = nullptr;
    for (SceneNode* at = first_scene(); at;) {
        SceneNode* next = next_scene(at);
        sector = look_up_sector(at->scene, name);
        if (sector)
            break;
        at = next;
    }
    return sector;
}

bool is_object_class(uint32_t node_class)
{
    return node_class == kClassParticleObject || node_class == kClass061a741e || node_class == kClassSkater ||
           node_class == kClassPedestrian || node_class == kClassVehicle || node_class == kClassGameObject;
}

} // namespace

// 0x005A3B80: the node `name`'s objects (pedestrians, skaters, vehicles, game objects, particle systems, 0x061A741E:
// their object flag 4) or sector (level objects and geometry: its viewport 1 bit - viewport 2 with `second_viewport`)
// hidden or shown. True for nodes the skate module skips; false for other classes.
bool set_node_hidden(uint32_t name, bool hide, int second_viewport)
{
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    if (skips_node(node))
        return true;
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (is_object_class(node_class)) {
        NodeFlagCommand command{};
        command.result = false;
        command.found = false;
        command.value = 4;
        command.action = hide ? kFlagActionSetObjectFlags : kFlagActionClearObjectFlags;
        command.id = name;
        static_cast<objects::ObjectHubBase*>(objects::g_composite_registry)
            ->for_each(&apply_flag_command_to_object, &command);
        return true;
    }
    if (node_class != kClassLevelObject && node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    uint8_t* sector = find_scene_sector(section_id);
    const uint32_t mask = as_sector(sector)->fetch_shown(); // (not checked for null)
    const uint32_t bit = second_viewport == 1 ? 2u : 1u;
    toggle_shown(sector, hide ? mask & ~bit : mask | bit);
    return true;
}

// 0x005A3D10: whether the node `name`'s thing is alive: rail and climbing nodes active; an object with its name
// there (pedestrians, skaters, vehicles, level objects, game objects, particle systems, 0x061A741E); level
// geometry's sector active (true without a name). False for other classes.
bool node_is_alive(uint32_t name)
{
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class == kClassClimbingNode || node_class == kClassRailNode)
        return rail_manager()->is_operative(name);
    if (is_object_class(node_class) || node_class == kClassLevelObject) {
        NodeFlagCommand command{};
        command.value = 0;
        command.action = 6; // (no action: only whether an object has the id)
        command.found = false;
        command.result = false;
        command.id = name;
        static_cast<objects::ObjectHubBase*>(objects::g_composite_registry)
            ->for_each(&apply_flag_command_to_object, &command);
        return command.found;
    }
    if (node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    return as_sector(find_scene_sector(section_id))->is_operative(); // (not checked for null)
}

// 0x005A3E80: the node `name`'s colour (0x99A9B716 of `params`, 0xBBGGRR (0x808080), with alpha 0x80): a level
// object's model component, or level geometry's geometry (vtable +0x18). False for other classes.
bool set_node_color(uint32_t name, script::Struct* params)
{
    int value = 0x808080;
    params->fetch_int_value(kColor, &value, true);
    const uint32_t rgba = (static_cast<uint32_t>(value) & 0xFF) | (static_cast<uint32_t>(value) >> 8 & 0xFF) << 8 |
                          (static_cast<uint32_t>(value) >> 16 & 0xFF) << 16 | 0x80u << 24;
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class == kClassLevelObject) {
        uint32_t object_name = 0;
        if (node->read_hash_value(kName, &object_name, 0)) {
            auto* object = static_cast<objects::GameEntity*>(
                static_cast<objects::ObjectHubBase*>(objects::g_composite_registry)->look_up_entity(object_name));
            // (neither checked for null, as in the original)
            static_cast<objects::MeshInstanceModule*>(object->fetch_module(0x286A8D26))
                ->change_tint(rgba); // "model"
        }
        return true;
    }
    if (node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    uint8_t* sector = find_scene_sector(section_id); // (not checked for null)
    if (auto* geometry = *raw::field_at<uint8_t*>(sector, 0x14))
        raw::as<render::Geom>(geometry)->change_tint(rgba);
    return true;
}

// 0x005A3FD0: the node `name`'s sector (level objects and geometry) shattered (its vtable +4 with 1), unless the
// skate module's +0x44 is set (the getter folded at 0x005331E0) or it skips the node (both: true). False for other
// classes.
bool shatter_node(uint32_t name)
{
    if (get_skate()[0x44])
        return true;
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    if (skips_node(node))
        return true;
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class != kClassLevelObject && node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    uint8_t* sector = find_scene_sector(section_id);                      // (not checked for null)
    raw::as<render::WorldCell>(sector)->plat_set_shatter(true); // (0x004A1090: a jump to vtable +4)
    return true;
}

// 0x005A40B0: the node `name`'s sector (level geometry only, else false) given the terrain 0x9655CFF7 of `params`.
bool set_node_terrain(uint32_t name, script::Struct* params)
{
    uint32_t terrain = 0; // (unset in the original when missing - an asserted parameter)
    params->read_hash_value(kTerrain, &terrain, true);
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    *raw::field_at<uint32_t>(find_scene_sector(section_id), 0xC) = terrain; // (not checked for null)
    return true;
}

// 0x005A4DA0: node_command `action` on the node `name` (false in a network game for nodes the skate module skips):
// 0 create, 1 kill, 2 show, 3 hide, 4 show and shatter, 5 alive?, 6 colour, 7 terrain; other actions false. Shown /
// hidden for the second viewport when it's the second skater's script in a two-viewport game.
bool node_command(uint32_t name, int action, script::Script* script, script::Struct* params)
{
    online::Hub* game_net = online::g_online_hub;
    if (game_net->is_network_game() && skips_node(level_nodes::fetch_vertex(name, true)))
        return false;
    int second_viewport = 0;
    if (script && script->entity_.get_pull() && !game_net->in_net_game() && modules::g_viewport_count == 2 &&
        script->entity_.get_pull() ==
            raw::as<objects::Entity>(raw::as<modules::Skate>(get_skate())->get_rider_at(1)))
        second_viewport = 1;
    switch (action) {
    case 0:
        return create_from_node_name(name);
    case 1:
        return kill_from_node_name(name, script);
    case 2:
        return set_node_hidden(name, false, second_viewport);
    case 3:
        return set_node_hidden(name, true, second_viewport);
    case 4:
        set_node_hidden(name, false, second_viewport);
        return shatter_node(name);
    case 5:
        return node_is_alive(name);
    case 6:
        return set_node_color(name, params);
    case 7:
        return set_node_terrain(name, params);
    default:
        return false;
    }
}

// 0x005A4EF0: node_command on name= (its answer) or every node of prefix= (true; with action 5, stopping at the first
// alive; with 0x80804991 the name is exact, not a prefix). Nothing (true) with the flags 0x2E7D5EE7 / 0xBCEB479A;
// false without either name.
bool node_command_by_name(script::Struct* params, script::Script* script, int action)
{
    if (params->has_bare_flag(0x2E7D5EE7u) || params->has_bare_flag(0xBCEB479Au))
        return true;
    uint32_t prefix = 0;
    if (params->hash_from_name_or_text(0x6C4E7971u, &prefix, 0)) { // "prefix"
        uint32_t count = 0;                                        // (a 16-bit count in a zeroed 32-bit slot)
        const bool is_prefix = !params->has_bare_flag(0x80804991u);
        (void)is_prefix; // (passed to 0x0045BF90, which ignores it)
        const uint32_t* nodes = level_nodes::collect_nodes_by_prefix(prefix, raw::as<uint16_t>(&count));
        for (int i = 0; std::cmp_less(i, static_cast<uint16_t>(count)); ++i) {
            if (node_command(nodes[i], action, script, params) && action == 5)
                break;
        }
        return true;
    }
    uint32_t name = 0;
    if (!params->hash_from_name_or_text(kName, &name, 0))
        return false;
    return node_command(name, action, script, params);
}

// 0x005A4FF0: Create name= / prefix=: the dead objects flushed first (the Function 0x005B0A40), then made.
bool cfunc_create(script::Struct* params, script::Script* script)
{
    cfunc_flush_dead_objects(nullptr, nullptr);
    return node_command_by_name(params, script, 0);
}

// 0x005A5010 / 0x005A5030 / 0x005A5050 / 0x005A5070: SetObjectColor / Kill / Visible / Invisible name= or prefix=.
bool cfunc_set_object_color(script::Struct* params, script::Script* script)
{
    return node_command_by_name(params, script, 6);
}
bool cfunc_kill(script::Struct* params, script::Script* script) { return node_command_by_name(params, script, 1); }
bool cfunc_visible(script::Struct* params, script::Script* script) { return node_command_by_name(params, script, 2); }
bool cfunc_invisible(script::Struct* params, script::Script* script) { return node_command_by_name(params, script, 3); }

// 0x005A5090: Shatter name= / prefix= [0x9F98989E scale= (1): the calling object's velocity times scale, else
// 0x6187906D / 0x1680A0FB / 0x8F89F141 asserted] and the shatter's settings (0x286BC297 - at least 2000, 5000 in a
// multiplayer game -, 0xBD309DD9, 0x834A5F87, 0xE632C101, 0x7DC30BA6, 0xF01BFA01, 0xB3DE2CAC, 0xAF1A25F2 (0x54628ED7),
// 0x2E1C4A64 (1), 0x210EA9E1 (a vector), 0xC48391A5, 0x33DF5E27): the settings given (0x00491110), then the nodes
// shown and shattered (node_command_by_name 4, its answer).
bool cfunc_shatter(script::Struct* params, script::Script* script)
{
    uint32_t name = 0; // (read and not used)
    float scale = 1.0f;
    params->read_hash_value(kName, &name, true);
    maths::Vec4 velocity;
    if (params->has_bare_flag(0x9F98989Eu)) {
        params->fetch_float(0x13B9DA7Bu, &scale, 0); // "scale"
        const maths::Vec4& v = static_cast<objects::GameEntity*>(script->entity_.get_pull())->vel_;
        velocity.x = static_cast<float>(static_cast<double>(scale) * v.x);
        velocity.y = static_cast<float>(static_cast<double>(scale) * v.y);
        velocity.z = static_cast<float>(static_cast<double>(scale) * v.z);
        velocity.w = static_cast<float>(static_cast<double>(scale) * v.w);
    } else {
        params->fetch_float(0x6187906Du, &velocity.x, true);
        params->fetch_float(0x1680A0FBu, &velocity.y, true);
        params->fetch_float(0x8F89F141u, &velocity.z, true);
        velocity.w = 0.0f;
    }
    float area = 0.0f;
    params->fetch_float(0x286BC297u, &area, 0);
    float value_bd30 = 0.0f;
    params->fetch_float(0xBD309DD9u, &value_bd30, 0);
    float value_834a = 0.0f;
    params->fetch_float(0x834A5F87u, &value_834a, 0);
    float value_e632 = 0.0f;
    params->fetch_float(0xE632C101u, &value_e632, 0);
    float value_7d_c3 = 0.0f;
    params->fetch_float(0x7DC30BA6u, &value_7d_c3, 0);
    float value_f_01b = 0.0f;
    params->fetch_float(0xF01BFA01u, &value_f_01b, 0);
    uint32_t checksum_b_3d_e = 0;
    params->read_hash_value(0xB3DE2CACu, &checksum_b_3d_e, 0);
    uint32_t checksum_af_1a = 0x54628ED7;
    params->read_hash_value(0xAF1A25F2u, &checksum_af_1a, 0);
    float value_2e_1c = 1.0f;
    params->fetch_float(0x2E1C4A64u, &value_2e_1c, 0);
    script::VectorValue vector = {0.0f, 0.0f, 0.0f, 1.0f};
    params->fetch_vec(0x210EA9E1u, &vector, 0);
    float value_c483 = 0.0f;
    params->fetch_float(0xC48391A5u, &value_c483, 0);
    int value_33d_f = 0;
    params->fetch_int_value(0x33DF5E27u, &value_33d_f, 0);
    float minimum_area = 2000.0f;
    if (modules::Skate::is_multiplayer())
        minimum_area = g_node_object_minimum_area; // (5000)
    if (area < minimum_area)
        area = minimum_area;
    render::engine::change_breakup_settings(&velocity, &vector, value_c483, area,
                                            value_bd30, value_834a, value_e632, value_7d_c3, value_f_01b, value_2e_1c,
                                            checksum_b_3d_e, checksum_af_1a, value_33d_f);
    return node_command_by_name(params, script, 4);
}

} // namespace commands
