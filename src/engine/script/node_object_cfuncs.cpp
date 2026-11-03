// Script commands (CFunctions) of the skate game, 0x005A2A00-0x005A34E0: the viewer camera fitted to the scenes,
// and the level's node objects made and killed by their class (see game_cfuncs.h).

#include "engine/script/game_cfuncs.h"
#include "platform/d3dx.h"
#include "engine/script/trick_cfuncs.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

#include "engine/core/arena_alloc.h"
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
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "engine/script/script_state.h"

namespace {
// A node command's missing-action marker (only whether an object has the id), the colour a node gets by default.
constexpr int kFlagActionNone = 6;
constexpr uint32_t kDefaultNodeColour = 0x808080;
// The distance a view is moved back at least; the Center camera defaults.
constexpr float kMinimumCameraDistance = 1000.0f;
constexpr float kDefaultCenterScale = 0.9f;
constexpr float kDefaultCenterX = -45.0f;
constexpr float kDefaultCenterY = 45.0f;
// toggle_shown's "every viewport"; a sector's flag marking a clone's source.
constexpr uint32_t kEveryViewport = 0xFFFFFFFFu;
constexpr uint32_t kSectorFlagCloneSource = 0x80u;
// Shatter's flags without a text in the dictionary; the minimum area default.
constexpr float kDefaultMinimumArea = 2000.0f;
constexpr uint32_t kShatterSecondSkipFlag = 0xBCEB479Au;   // text unknown
constexpr uint32_t kShatterVelocityFlag = 0x9F98989Eu; // text unknown
constexpr uint32_t kShatterParameterF01B = 0xF01BFA01u; // text unknown
constexpr uint32_t kShatterParameter2E1C = 0x2E1C4A64u; // text unknown
constexpr uint32_t kShatterParameter33DF = 0x33DF5E27u; // text unknown

// Names (the checksums of their text).
constexpr uint32_t kAREA = 0x286BC297u; // AREA
constexpr uint32_t kBlend = 0x54628ED7u; // Blend
constexpr uint32_t kBlendMode = 0xAF1A25F2u; // blend_mode
constexpr uint32_t kBounce = 0x7DC30BA6u; // Bounce
constexpr uint32_t kLIFE = 0xE632C101u; // LIFE
constexpr uint32_t kLinks = 0x2E7D5EE7u; // Links
constexpr uint32_t kMODEL = 0x286A8D26u; // MODEL
constexpr uint32_t kOrigin = 0x210EA9E1u; // Origin
constexpr uint32_t kPrefix = 0x6C4E7971u; // prefix
constexpr uint32_t kRadius = 0xC48391A5u; // Radius
constexpr uint32_t kSpread = 0x834A5F87u; // Spread
constexpr uint32_t kTextureSwap = 0xB3DE2CACu; // texture_swap
constexpr uint32_t kVariance = 0xBD309DD9u; // variance
constexpr uint32_t kVelX = 0x6187906Du; // vel_x
constexpr uint32_t kVelY = 0x1680A0FBu; // vel_y
constexpr uint32_t kVelZ = 0x8F89F141u; // vel_z

} // namespace

namespace commands {

namespace {
constexpr float kNodeObjectMinimumArea{5000.0f};
} // namespace

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

objects::GrindRailHub* rail_manager() { return modules::Skate::sole()->rail_manager().get(); }
memory::Hub* mem_manager() { return memory::Hub::get_pull(); }

// Whether the skate module leaves a node out (0x00565BB0: its flag 0x68910AC6, in a game with more than one player).
bool skips_node(script::Struct* node) { return modules::Skate::should_be_absent(node) != 0; }

// The scenes (Nx), from their list (engine_scene_list.h).
memory::ListNode* first_scene() { return render::first_scene_node(); }
memory::ListNode* next_scene(memory::ListNode* node) { return render::next_scene_node(node); }

// A scene's sector by name (0x0049CB00) and a copy of one (0x0049D510).
render::WorldCell* look_up_sector(render::WorldScene* scene, uint32_t name) { return scene->look_up_sector(name); }
void duplicate_scene_sector(render::WorldScene* scene, render::WorldCell* sector, render::WorldScene* destination,
                            int a, int b, uint32_t name)
{
    scene->duplicate_scene_sector(sector, destination, a, b != 0, name);
}
// A sector's (WorldCell, thiscall) settings: the viewports it shows in (0x0049E680), drawn (0x0049E9B0), collided
// with (0x0049EA30), its orientation (0x0049E700), scale (0x0049E7C0), position (0x0049E6C0).
void toggle_shown(render::WorldCell* sector, uint32_t mask) { sector->toggle_shown(mask); }
void change_live(render::WorldCell* sector, bool active) { sector->change_live(active); }
void enable_collision(render::WorldCell* sector, bool on) { sector->enable_collision(on); }
void change_facing(render::WorldCell* sector, const maths::Mat4* m) { sector->change_facing(m); }
void change_scale(render::WorldCell* sector, const script::VectorValue* scale)
{
    sector->change_scale(scale);
}
void change_place(render::WorldCell* sector, const script::VectorValue* pos)
{
    sector->place_at_absolute_point(pos);
}
// A sector name's flag 0x4E549FE1 effect on or off (0x0048BF90, a jump to 0x004B22F0).
void mark_as_occluder(uint32_t name, int on) { render::flag_debug_lines(name, on != 0); }

} // namespace

// 0x005A2A00: the viewer's camera object (0xEB17151B) placed to look at the sectors of `scene` (those named `only`,
// or all): the box round their active geometry's vertices; the camera turned by `angleX` / `angleY` / `angleZ`
// degrees (in that order, from no turn) and set back from the box's centre along its forward axis by the box's size
// (at least 1000) times `scale` squared.
void fit_viewer_camera(render::WorldScene* scene, float scale, const maths::Vec4& angles, uint32_t only)
{
    const float angle_x = angles.x;
    const float angle_y = angles.y;
    const float angle_z = angles.z;
    maths::BBox box;
    lists::KeyMap* sectors = scene->sectors();
    if (sectors) {
        sectors->set_iter_bucket(-1);
        sectors->set_iter_item(nullptr);
        for (const bool more = sectors->step_iterator(); more && sectors->iter_item();) {
            auto* sector = static_cast<render::WorldCell*>(sectors->iter_item()->value);
            if (!sector)
                break;
            // (the sector's name: the getter folded at 0x005480C0, its +4)
            if (!only || sector->checksum() == only) {
                render::Geom* geom = nullptr;
                if (sector->is_operative()) // active
                    geom = static_cast<render::Geom*>(sector->geom_handle());
                const int count = geom ? static_cast<int>(geom->fetch_vertex_tally()) : 0;
                if (count) {
                    if (!memory::manager_ready_flag()) // (the memory manager set up, if it isn't)
                        memory::ensure_manager();
                    auto* vertices =
                        static_cast<maths::Vec4*>(mem_manager()->carve(count * sizeof(maths::Vec4), 1, nullptr));
                    // (the original runs the vertices' empty constructor over the block, 0x00401020 with 0x005902E0)
                    const std::span<maths::Vec4> vertex_run(vertices, static_cast<size_t>(count));
                    geom->get_vertices(vertex_run);
                    for (const maths::Vec4& vertex : vertex_run)
                        box.append_spot(vertex);
                    mem_manager()->dealloc(vertices);
                }
            }
            if (!sectors->step_iterator() || !sectors->iter_item())
                break;
        }
    }
    auto* camera = core::down_cast<objects::GameEntity>(
        static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get())->look_up_entity(kViewerCameraObject));
    if (!camera)
        return;
    const maths::Vec4& lo = box.min;
    const maths::Vec4& hi = box.max;
    const double half = gfx::kHalf; // (0.5)
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
    if (size < gfx::kThousand) // (1000)
        distance = kMinimumCameraDistance;
    maths::Mat4& m = camera->basis_frame();
    for (int row = 0; row < 4; ++row) {
        maths::Vec4& r = core::at(m.row, row);
        for (int column = 0; column < 4; ++column)
            r[column] = row == column ? 1.0f : 0.0f;
    }
    const double to_radians = kDegreeToRadianRatio;
    auto turn = [&](maths::Mat4& (*create)(maths::Mat4&, float), float degrees) {
        maths::Mat4 rotation;
        create(rotation, static_cast<float>(degrees * to_radians));
        d3dx::matrix_multiply(&m, &m, &rotation);
    };
    turn(&maths::make_pitch_matrix, angle_x);
    turn(&maths::make_yaw_matrix, angle_y);
    turn(&maths::make_roll_matrix, angle_z);
    const double back = static_cast<double>(distance) * scale * scale;
    const maths::Vec4& at = core::at(m.row, 2);
    auto bx = static_cast<float>(back * at.x);
    auto by = static_cast<float>(back * at.y);
    auto bz = static_cast<float>(back * at.z);
    camera->at_point().x = static_cast<float>(static_cast<double>(bx) + centre.x);
    camera->at_point().y = static_cast<float>(static_cast<double>(by) + centre.y);
    camera->at_point().z = static_cast<float>(static_cast<double>(bz) + centre.z);
    camera->at_point().w = static_cast<float>(back * at.w + centre.w);
}

// 0x005A2D60: CenterCamera [scale (0.9)] [x (-45)] [y (45)] [z (0)] [id=<sector>]: the viewer's camera fitted to each
// scene in turn (fit_viewer_camera; the last one's placing stays). False without the viewer module (0x007CE4A4).
bool cfunc_center_camera(script::Struct* params, script::Script*)
{
    if (!objects::view_mode())
        return false;
    float scale = kDefaultCenterScale;
    params->fetch_float("scale", &scale, 0); // "scale"
    float x = kDefaultCenterX;
    params->fetch_float("x", &x, 0); // "x"
    float y = kDefaultCenterY;
    params->fetch_float("y", &y, 0); // "y"
    float z = 0.0f;
    params->fetch_float("z", &z, 0); // "z"
    uint32_t id = 0;
    params->read_hash_value("id", &id, 0); // "id"
    for (memory::ListNode* node = first_scene(); node;) {
        memory::ListNode* next = next_scene(node);
        fit_viewer_camera(render::scene_of_node(node), scale, {.x = x, .y = y, .z = z, .w = 0.0f}, id);
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
        rail_manager()->change_live(checksums::Name{name}, 1, node->has_bare_flag("FLAG_WHOLE_RAIL")); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        objects::proximity_node_enable(checksums::Name{name}, 1);
        return true;
    case kClassParticleObject:
        memory::push_mem_profile("Particle Systems"); // "Particle Systems"
        objects::spawn_effect_entity(node);
        memory::pop_mem_profile();
        return true;
    case kClassEmitterObject:
        objects::emitter_set_active(checksums::Name{name}, 1);
        return true;
    case kClassLevelObject:
        memory::push_mem_profile("LevelObjects"); // "LevelObjects"
        if (gfx::tod_manager()->params_set()) {
            uint32_t colour = 0;
            gfx::tod_manager()->get_level_color(&colour);
            node->put_int(checksums::Name{kLevelColor}, static_cast<int>(colour));
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
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = nullptr;
    for (memory::ListNode* at = first_scene(); at && !sector;) {
        memory::ListNode* next = next_scene(at);
        scene = render::scene_of_node(at);
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
        toggle_shown(sector, kEveryViewport);
        change_live(sector, true);
        enable_collision(sector, true);
        if (node->has_bare_flag(kOccluder))
            mark_as_occluder(section_id, 1);
        return true;
    }
    render::WorldCell* copy = look_up_sector(scene, section_id);
    if (!copy) {
        duplicate_scene_sector(scene, sector, scene, 1, 1, section_id);
        copy = look_up_sector(scene, section_id);
    }
    sector->flag_bits() |= kSectorFlagCloneSource;
    copy->flag_bits() &= ~kSectorFlagCloneSource;
    script::VectorValue angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (node->fetch_vec(kAngles, &angles, 0)) {
        maths::Mat4 orientation;
        orientation.init_from_angles(*&angles);
        change_facing(copy, &orientation);
    }
    script::VectorValue scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f};
    if (node->fetch_vec(kScale, &scale, 0))
        change_scale(copy, &scale);
    script::VectorValue pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
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
        } data = {.id = name, .object = script->entity().get_pull()};
        static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get())
            ->for_each(&destroy_object_with_id, &data);
        return true;
    }
    case kClassClimbingNode:
    case kClassRailNode:
        rail_manager()->change_live(checksums::Name{name}, 0, node->has_bare_flag("FLAG_WHOLE_RAIL")); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        objects::proximity_node_enable(checksums::Name{name}, 0);
        return true;
    case kClassA0e52802:
        render::delete_scene_light(name);
        return true;
    case kClassEmitterObject:
        objects::emitter_set_active(checksums::Name{name}, 0);
        return true;
    case kClassLevelGeometry: {
        uint32_t section_id = 0;
        if (!node->read_hash_value(kName, &section_id, 0))
            return true;
        for (memory::ListNode* at = first_scene(); at;) {
            memory::ListNode* next = next_scene(at);
            if (render::WorldCell* sector = look_up_sector(render::scene_of_node(at), section_id)) {
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
render::WorldCell* find_sector(uint32_t name) { return render::engine::look_up_sector(name); }
bool park_editor_off() { return park_editing::ParkBuilderMode::sole()->editor_state() == kParkEditorOff; }

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
    script::Array* nodes = level_nodes::get_node_owner_nodes();
    objects::GrindRailHub* rails = rail_manager();
    if (!params || !params->has_bare_flag("no_cleanup")) { // "no_cleanup"
        objects::nearby_zone_startup();
        objects::emitter_init();
        rail_manager()->tidy_up();
        objects::trick_spot_hub()->remove_all_clusters();
        render::delete_dynamic_lights();
        render::delete_all_scene_lights();
        memory::push_mem_profile("Rail Nodes"); // "Rail Nodes"
        rails->add_from_node_array(nodes);
        memory::pop_mem_profile();
    }
    // (a trick object's name: an asserted parameter, so the original's local - kept from one node to the next -
    // holds the last one read when a node has none)
    uint32_t object_name = 0;
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class == kClassRailNode) {
            node->erase_module(kAngles);
            node->erase_module(kRailRemove1);
        } else if (node_class == kClassLevelObject) {
            uint32_t name = 0;
            node->read_hash_value(kName, &name, 0);
            if (render::WorldCell* sector = find_sector(name)) {
                change_live(sector, false);
                uint32_t terrain = kDefaultTerrain;
                node->read_hash_value(kSurfaceKindKey, &terrain, 0);
                sector->set_light_group(terrain);
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
            if (render::WorldCell* sector = find_sector(name)) {
                if (!park_editor_off()) {
                    if (node->has_bare_flag(kCreatedAtStart))
                        skips_node(node); // (asked, the answer unused)
                } else if (node->has_bare_flag(kCreatedAtStart) && !skips_node(node)) {
                    toggle_shown(sector, kEveryViewport);
                    change_live(sector, true);
                    enable_collision(sector, true);
                } else {
                    change_live(sector, false);
                }
                if (node->has_bare_flag(kOccluder)) {
                    sector->mark_as_occluder(true);
                    change_live(sector, false);
                    if (!node->has_bare_flag(kCreatedAtStart))
                        mark_as_occluder(name, 0);
                }
                uint32_t terrain = kDefaultTerrain;
                node->read_hash_value(kSurfaceKindKey, &terrain, 0);
                sector->set_light_group(terrain);
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
            if (!game_config::from_cd() && script::fetch_int_value(kAllTrickObjects))
                trick = true;
            if (!skips_node(node) && trick) {
                memory::push_mem_profile("Trick Object Clusters"); // "Trick Object Clusters"
                node->read_hash_value(kName, &object_name, true);
                uint32_t cluster = object_name;
                node->read_hash_value(kCluster, &cluster, false);
                objects::trick_spot_hub()->add_cluster(cluster);
                objects::trick_spot_hub()->add_object(object_name, cluster);
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
        static_cast<goals::GapTally*>(modules::Skate::sole()->fetch_gap_list())->get_build();
    for (memory::ListNode* at = first_scene(); at;) {
        memory::ListNode* next = next_scene(at);
        render::scene_of_node(at)->update_super_sectors_if_marked(nullptr);
        at = next;
    }
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
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
    mem_manager()->enter_heap_scope(static_cast<memory::MemSource*>(mem_manager()->debug_heap()));
    level_nodes::NodeManager* node_manager = level_nodes::node_manager_instance().get();
    const char* loaded_node_list = script::script_state().node_list_name().data();
    assets::ResourceRegistry* assets = assets::asset_manager();
    assets->discard_loaded_item(assets->obtain_loaded_item(checksums::hash_from_text(loaded_node_list), 0));
    script::Struct file_params; // (on the stack; emptied at the end, never destroyed - as the original)
    script::Struct* file = &file_params;
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
render::WorldCell* find_scene_sector(uint32_t name)
{
    render::WorldCell* sector = nullptr;
    for (memory::ListNode* at = first_scene(); at;) {
        memory::ListNode* next = next_scene(at);
        sector = look_up_sector(render::scene_of_node(at), name);
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
        static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get())
            ->for_each(&apply_flag_command_to_object, &command);
        return true;
    }
    if (node_class != kClassLevelObject && node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    render::WorldCell* sector = find_scene_sector(section_id);
    const uint32_t mask = sector->fetch_shown(); // (not checked for null)
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
        command.action = kFlagActionNone; // (no action: only whether an object has the id)
        command.found = false;
        command.result = false;
        command.id = name;
        static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get())
            ->for_each(&apply_flag_command_to_object, &command);
        return command.found;
    }
    if (node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    return find_scene_sector(section_id)->is_operative(); // (not checked for null)
}

// 0x005A3E80: the node `name`'s colour (0x99A9B716 of `params`, 0xBBGGRR (0x808080), with alpha 0x80): a level
// object's model component, or level geometry's geometry (vtable +0x18). False for other classes.
bool set_node_color(uint32_t name, script::Struct* params)
{
    int value = kDefaultNodeColour;
    params->fetch_int_value(kColor, &value, true);
    const uint32_t rgba = (static_cast<uint32_t>(value) & 0xFF) | (static_cast<uint32_t>(value) >> 8 & 0xFF) << 8 |
                          (static_cast<uint32_t>(value) >> 16 & 0xFF) << 16 | 0x80u << 24;
    script::Struct* node = level_nodes::fetch_vertex(name, true);
    uint32_t node_class = 0;
    node->read_hash_value(kClass, &node_class, 0);
    if (node_class == kClassLevelObject) {
        uint32_t object_name = 0;
        if (node->read_hash_value(kName, &object_name, 0)) {
            auto* object = core::down_cast<objects::GameEntity>(
                static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get())->look_up_entity(object_name));
            // (neither checked for null, as in the original)
            core::down_cast<objects::MeshInstanceModule>(object->fetch_module(kMODEL))
                ->change_tint(rgba); // "model"
        }
        return true;
    }
    if (node_class != kClassLevelGeometry)
        return false;
    uint32_t section_id = 0;
    if (!node->read_hash_value(kName, &section_id, 0))
        return true;
    const render::WorldCell* sector = find_scene_sector(section_id); // (not checked for null)
    if (auto* geometry = static_cast<render::Geom*>(sector->geom_handle()))
        geometry->change_tint(rgba);
    return true;
}

// 0x005A3FD0: the node `name`'s sector (level objects and geometry) shattered (its vtable +4 with 1), unless the
// skate module's +0x44 is set (the getter folded at 0x005331E0) or it skips the node (both: true). False for other
// classes.
bool shatter_node(uint32_t name)
{
    if (modules::Skate::sole()->running_queued_scripts())
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
    render::WorldCell* sector = find_scene_sector(section_id);                      // (not checked for null)
    sector->plat_set_shatter(true); // (0x004A1090: a jump to vtable +4)
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
    find_scene_sector(section_id)->set_light_group(terrain); // (not checked for null)
    return true;
}

// 0x005A4DA0: node_command `action` on the node `name` (false in a network game for nodes the skate module skips):
// 0 create, 1 kill, 2 show, 3 hide, 4 show and shatter, 5 alive?, 6 colour, 7 terrain; other actions false. Shown /
// hidden for the second viewport when it's the second skater's script in a two-viewport game.
bool node_command(checksums::Name node, int action, script::Script* script, script::Struct* params)
{
    const uint32_t name = node.value();
    online::Hub* game_net = online::net_game_state().online_hub();
    if (game_net->is_network_game() && skips_node(level_nodes::fetch_vertex(name, true)))
        return false;
    int second_viewport = 0;
    if (script && script->entity().get_pull() && !game_net->in_net_game() && modules::viewport_count() == 2 &&
        script->entity().get_pull() ==
            modules::Skate::sole()->get_rider_at(1))
        second_viewport = 1;
    switch (action) {
    case kNodeActionCreate:
        return create_from_node_name(name);
    case kNodeActionKill:
        return kill_from_node_name(name, script);
    case kNodeActionShow:
        return set_node_hidden(name, false, second_viewport);
    case kNodeActionHide:
        return set_node_hidden(name, true, second_viewport);
    case kNodeActionShatter:
        set_node_hidden(name, false, second_viewport);
        return shatter_node(name);
    case kNodeActionAlive:
        return node_is_alive(name);
    case kNodeActionColour:
        return set_node_color(name, params);
    case kNodeActionTerrain:
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
    if (params->has_bare_flag(kLinks) || params->has_bare_flag(kShatterSecondSkipFlag))
        return true;
    uint32_t prefix = 0;
    if (params->hash_from_name_or_text(kPrefix, &prefix, 0)) { // "prefix"
        const bool is_prefix = !params->has_bare_flag(0x80804991u);
        (void)is_prefix; // (passed to 0x0045BF90, which ignores it)
        for (const uint32_t node : level_nodes::collect_nodes_by_prefix(prefix)) {
            if (node_command(checksums::Name{node}, action, script, params) && action == kNodeActionAlive)
                break;
        }
        return true;
    }
    uint32_t name = 0;
    if (!params->hash_from_name_or_text(kName, &name, 0))
        return false;
    return node_command(checksums::Name{name}, action, script, params);
}

// 0x005A4FF0: Create name= / prefix=: the dead objects flushed first (the Function 0x005B0A40), then made.
bool cfunc_create(script::Struct* params, script::Script* script)
{
    cfunc_flush_dead_objects(nullptr, nullptr);
    return node_command_by_name(params, script, kNodeActionCreate);
}

// 0x005A5010 / 0x005A5030 / 0x005A5050 / 0x005A5070: SetObjectColor / Kill / Visible / Invisible name= or prefix=.
bool cfunc_set_object_color(script::Struct* params, script::Script* script)
{
    return node_command_by_name(params, script, kNodeActionColour);
}
bool cfunc_kill(script::Struct* params, script::Script* script) { return node_command_by_name(params, script, kNodeActionKill); }
bool cfunc_visible(script::Struct* params, script::Script* script) { return node_command_by_name(params, script, kNodeActionShow); }
bool cfunc_invisible(script::Struct* params, script::Script* script) { return node_command_by_name(params, script, kNodeActionHide); }

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
    if (params->has_bare_flag(kShatterVelocityFlag)) {
        params->fetch_float(kScale, &scale, 0); // "scale"
        const maths::Vec4& v = core::down_cast<objects::GameEntity>(script->entity().get_pull())->vel();
        velocity.x = static_cast<float>(static_cast<double>(scale) * v.x);
        velocity.y = static_cast<float>(static_cast<double>(scale) * v.y);
        velocity.z = static_cast<float>(static_cast<double>(scale) * v.z);
        velocity.w = static_cast<float>(static_cast<double>(scale) * v.w);
    } else {
        params->fetch_float(kVelX, &velocity.x, true);
        params->fetch_float(kVelY, &velocity.y, true);
        params->fetch_float(kVelZ, &velocity.z, true);
        velocity.w = 0.0f;
    }
    float area = 0.0f;
    params->fetch_float(kAREA, &area, 0);
    float value_bd30 = 0.0f;
    params->fetch_float(kVariance, &value_bd30, 0);
    float value_834a = 0.0f;
    params->fetch_float(kSpread, &value_834a, 0);
    float value_e632 = 0.0f;
    params->fetch_float(kLIFE, &value_e632, 0);
    float value_7d_c3 = 0.0f;
    params->fetch_float(kBounce, &value_7d_c3, 0);
    float value_f_01b = 0.0f;
    params->fetch_float(kShatterParameterF01B, &value_f_01b, 0);
    uint32_t checksum_b_3d_e = 0;
    params->read_hash_value(kTextureSwap, &checksum_b_3d_e, 0);
    uint32_t checksum_af_1a = kBlend;
    params->read_hash_value(kBlendMode, &checksum_af_1a, 0);
    float value_2e_1c = 1.0f;
    params->fetch_float(kShatterParameter2E1C, &value_2e_1c, 0);
    script::VectorValue vector = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    params->fetch_vec(kOrigin, &vector, 0);
    float value_c483 = 0.0f;
    params->fetch_float(kRadius, &value_c483, 0);
    int value_33d_f = 0;
    params->fetch_int_value(kShatterParameter33DF, &value_33d_f, 0);
    float minimum_area = kDefaultMinimumArea;
    if (modules::Skate::is_multiplayer())
        minimum_area = kNodeObjectMinimumArea; // (5000)
    area = std::max(area, minimum_area);
    render::engine::change_breakup_settings({.velocity = &velocity, .vector_210e_ = &vector, .value_c483_ = value_c483, .area = area, .speed = value_bd30, .velocity_scale_ = value_834a, .lifetime = value_e632, .floor = value_7d_c3, .bounce = value_f_01b, .value_2e_1c_ = value_2e_1c, .texture = checksum_b_3d_e, .value_af_1a_ = checksum_af_1a, .value_33d_f_ = value_33d_f});
    return node_command_by_name(params, script, kNodeActionShatter);
}

} // namespace commands
