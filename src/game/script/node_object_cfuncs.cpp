// Script commands (CFunctions) of the skate game, 0x005A2A00-0x005A34E0: the viewer camera fitted to the scenes,
// and the level's node objects made and killed by their class (see game_cfuncs.h).

#include "game/script/game_cfuncs.h"
#include "game/gfx/d3dx.h"
#include "game/script/trick_cfuncs.h"

#include <cmath>
#include <cstdint>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/tod_manager.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/emitter.h"
#include "game/obj/model_component.h"
#include "game/obj/game_objects.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/proxim.h"
#include "game/obj/rail_manager.h"
#include "game/skate/gap_checklist.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/node_prefixes.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/scene_lights.h"
#include "game/gfx/sector.h"
#include "game/skate/skate_module.h"
#include "game/script/script_startup.h"
#include "game/ed/park_editor.h"
#include "game/obj/compositemgr.h"
#include "game/obj/movie_manager.h"
#include "game/script/skate_cfuncs.h"
#include "game/gfx/cag_cfuncs.h"
#include "game/sys/game_config.h"

namespace ScriptCommands {

float g_nodeObjectMinimumArea{5000.0f};
Gfx2D::CTODManager* g_todManager;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;        // "name"
constexpr uint32_t kClass = 0x12B4E660;       // "class"
constexpr uint32_t kPos = 0x7F261953;         // "pos"
constexpr uint32_t kAngles = 0x9D2D0915;      // "angles"
constexpr uint32_t kScale = 0x13B9DA7B;       // "scale"
constexpr uint32_t kCloneFrom = 0xEDC063B9;   // (name unknown) a level geometry node's sector to copy
constexpr uint32_t kOccluder = 0x4E549FE1;    // (name unknown) a level geometry node's flag (0x0048BF90)
constexpr uint32_t kLevelColor = 0x99A9B716;  // (name unknown) a level object's colour, added from the time of day
constexpr uint32_t kViewerCameraObject = 0xEB17151B; // (name unknown) the viewer's camera object

// The node classes.
constexpr uint32_t kClassPedestrian = 0xA0DFAC98;    // "Pedestrian"
constexpr uint32_t kClass061A741E = 0x061A741E;      // (name unknown) made as a pedestrian
constexpr uint32_t kClassClimbingNode = 0x30C19600;  // "ClimbingNode"
constexpr uint32_t kClassRailNode = 0x8E6B02AD;      // "RailNode"
constexpr uint32_t kClassProximNode = 0x08470F2E;    // "ProximNode"
constexpr uint32_t kClassParticleObject = 0x9E7D469E; // "ParticleObject"
constexpr uint32_t kClassEmitterObject = 0xD64DC7C2; // "EmitterObject"
constexpr uint32_t kClassLevelObject = 0xB7B3BD86;   // "LevelObject"
constexpr uint32_t kClassLevelGeometry = 0xBF4D3536; // "LevelGeometry"
constexpr uint32_t kClassVehicle = 0xE47F1B79;       // "Vehicle"
constexpr uint32_t kClassGameObject = 0xEF59C100;    // "GameObject"
constexpr uint32_t kClassTrigger = 0xE594F0A2;       // "Trigger"
constexpr uint32_t kClassSkater = 0x5B8AB877;        // "skater"
constexpr uint32_t kClassA0E52802 = 0xA0E52802;      // (name unknown) killed through 0x0048FCA0

uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
GameObjects::CGrindRailHub* RailManager() { return *reinterpret_cast<GameObjects::CGrindRailHub**>(Skate() + 0x24); }
MemSys::Hub* MemManager() { return MemSys::Hub::Pull(); }

// Whether the skate module leaves a node out (0x00565BB0: its flag 0x68910AC6, in a game with more than one player).
bool SkipsNode(Scripts::CStruct* node)
{
    return GameModules::Skate::ShouldBeAbsent(node) != 0;
}

// The scenes (Nx), from their list (0x006FA7EC; 0x0048C7F0 gives the first node, its +0xC the next - the one with
// data -1 ends it - and +4 the scene).
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene;     // +0x04
    uint32_t unknown8;
    SceneNode* next;    // +0x0C
};
SceneNode* FirstScene() { return reinterpret_cast<SceneNode*>(Engine3D::FirstSceneNode()); }
SceneNode* NextScene(SceneNode* node) { return node->next->scene == reinterpret_cast<uint8_t*>(-1) ? nullptr : node->next; }

// A scene's sector by name (0x0049CB00) and a copy of one (0x0049D510).
Engine3D::CWorldCell* AsSector(uint8_t* sector) { return reinterpret_cast<Engine3D::CWorldCell*>(sector); }
Engine3D::CWorldScene* AsScene(uint8_t* scene) { return reinterpret_cast<Engine3D::CWorldScene*>(scene); }
uint8_t* LookUpSector(uint8_t* scene, uint32_t name) { return static_cast<uint8_t*>(AsScene(scene)->LookUpSector(name)); }
void DuplicateSceneSector(uint8_t* scene, uint8_t* sector, uint8_t* destination, int a, int b, uint32_t name)
{
    AsScene(scene)->DuplicateSceneSector(AsSector(sector), AsScene(destination), a, b != 0, name);
}
// A sector's (CWorldCell, thiscall) settings: the viewports it shows in (0x0049E680), drawn (0x0049E9B0), collided
// with (0x0049EA30), its orientation (0x0049E700), scale (0x0049E7C0), position (0x0049E6C0).
void ToggleShown(uint8_t* sector, uint32_t mask) { AsSector(sector)->ToggleShown(mask); }
void ChangeLive(uint8_t* sector, bool active) { AsSector(sector)->ChangeLive(active); }
void EnableCollision(uint8_t* sector, bool on) { AsSector(sector)->EnableCollision(on); }
void ChangeFacing(uint8_t* sector, const Maths::Mat4* m) { AsSector(sector)->ChangeFacing(m); }
void ChangeScale(uint8_t* sector, const Scripts::VectorValue* scale)
{
    AsSector(sector)->ChangeScale(reinterpret_cast<const Maths::Vec4*>(scale));
}
void ChangePlace(uint8_t* sector, const Scripts::VectorValue* pos)
{
    AsSector(sector)->PlaceAtAbsolutePoint(reinterpret_cast<const Maths::Vec4*>(pos));
}
// A geometry's virtual slot, called through its vtable.
template <typename Fn>
Fn GeometrySlot(const uint8_t* geometry, int slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(geometry))[slot]);
}
// A sector name's flag 0x4E549FE1 effect on or off (0x0048BF90, a jump to 0x004B22F0).
void MarkAsOccluder(uint32_t name, int on) { Engine3D::FlagDebugLines(name, on != 0); }

} // namespace

// 0x005A2A00: the viewer's camera object (0xEB17151B) placed to look at the sectors of `scene` (those named `only`,
// or all): the box round their active geometry's vertices; the camera turned by `angleX` / `angleY` / `angleZ`
// degrees (in that order, from no turn) and set back from the box's centre along its forward axis by the box's size
// (at least 1000) times `scale` squared.
void __cdecl FitViewerCamera(uint8_t* scene, float scale, float angleX, float angleY, float angleZ, uint32_t only)
{
    Maths::BBox box;
    auto* sectors = *reinterpret_cast<ListKit::KeyMap**>(scene + 0xD0);
    if (sectors) {
        sectors->m_iterBucket = -1;
        sectors->m_iterItem = nullptr;
        for (bool more = sectors->StepIterator(); more && sectors->m_iterItem;) {
            auto* sector = static_cast<uint8_t*>(sectors->m_iterItem->value);
            if (!sector)
                break;
            // (the sector's name: the getter folded at 0x005480C0, its +4)
            if (!only || *reinterpret_cast<uint32_t*>(sector + 4) == only) {
                uint8_t* geometry = nullptr;
                if (AsSector(sector)->IsOperative()) // active
                    geometry = static_cast<uint8_t*>(AsSector(sector)->m_geomHandle);
                int count = geometry ? GeometrySlot<int(__thiscall*)(void*)>(geometry, 36)(geometry) : 0;
                if (count) {
                    if (!MemSys::g_managerReady) // (the memory manager set up, if it isn't)
                        MemSys::EnsureManager();
                    auto* vertices = static_cast<Maths::Vec4*>(MemManager()->Carve(count * sizeof(Maths::Vec4), 1, nullptr));
                    // (the original runs the vertices' empty constructor over the block, 0x00401020 with 0x005902E0)
                    GeometrySlot<void(__thiscall*)(void*, Maths::Vec4*)>(geometry, 37)(geometry, vertices);
                    for (int i = 0; i < count; ++i)
                        box.AppendSpot(vertices[i]);
                    MemManager()->Dealloc(vertices);
                }
            }
            if (!sectors->StepIterator() || !sectors->m_iterItem)
                break;
        }
    }
    auto* camera = static_cast<GameObjects::CGameEntity*>(
        reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager)->LookUpEntity(kViewerCameraObject));
    if (!camera)
        return;
    const Maths::Vec4& lo = box.min;
    const Maths::Vec4& hi = box.max;
    const double half = Gfx2D::g_half; // (0.5)
    Maths::Vec4 centre;
    centre.x = static_cast<float>((static_cast<double>(hi.x) + lo.x) * half);
    centre.y = static_cast<float>((static_cast<double>(hi.y) + lo.y) * half);
    centre.z = static_cast<float>(static_cast<float>(static_cast<double>(hi.z) + lo.z) * half);
    centre.w = static_cast<float>(static_cast<float>(static_cast<double>(hi.w) + lo.w) * half);
    double dx = static_cast<double>(hi.x) - lo.x;
    double dy = static_cast<double>(hi.y) - lo.y;
    double dz = static_cast<double>(hi.z) - lo.z;
    double size = std::sqrt(dz * dz + dy * dy + dx * dx);
    float distance = static_cast<float>(size);
    if (size < Gfx2D::g_thousand) // (1000)
        distance = 1000.0f;
    Maths::Mat4& m = camera->m_basisFrame;
    for (int row = 0; row < 4; ++row) {
        float* r = &m.row[row].x;
        for (int column = 0; column < 4; ++column)
            r[column] = row == column ? 1.0f : 0.0f;
    }
    const double toRadians = g_degreesToRadians;
    auto turn = [&](Maths::Mat4& (*create)(Maths::Mat4&, float), float degrees) {
        Maths::Mat4 rotation;
        create(rotation, static_cast<float>(degrees * toRadians));
        D3dx::MatrixMultiply(&m, &m, &rotation);
    };
    turn(&Maths::MakePitchMatrix, angleX);
    turn(&Maths::MakeYawMatrix, angleY);
    turn(&Maths::MakeRollMatrix, angleZ);
    double back = static_cast<double>(distance) * scale * scale;
    const float* at = &m.row[2].x;
    float bx = static_cast<float>(back * at[0]);
    float by = static_cast<float>(back * at[1]);
    float bz = static_cast<float>(back * at[2]);
    camera->m_atPoint.x = static_cast<float>(static_cast<double>(bx) + centre.x);
    camera->m_atPoint.y = static_cast<float>(static_cast<double>(by) + centre.y);
    camera->m_atPoint.z = static_cast<float>(static_cast<double>(bz) + centre.z);
    camera->m_atPoint.w = static_cast<float>(back * at[3] + centre.w);
}

// 0x005A2D60: CenterCamera [scale (0.9)] [x (-45)] [y (45)] [z (0)] [id=<sector>]: the viewer's camera fitted to each
// scene in turn (FitViewerCamera; the last one's placing stays). False without the viewer module (0x007CE4A4).
bool __cdecl CFunc_CenterCamera(Scripts::CStruct* params, Scripts::CScript*)
{
    if (!GameObjects::g_viewMode)
        return false;
    float scale = 0.9f;
    params->FetchFloat("scale", &scale, 0); // "scale"
    float x = -45.0f;
    params->FetchFloat("x", &x, 0); // "x"
    float y = 45.0f;
    params->FetchFloat("y", &y, 0); // "y"
    float z = 0.0f;
    params->FetchFloat("z", &z, 0); // "z"
    uint32_t id = 0;
    params->ReadHashValue("id", &id, 0); // "id"
    for (SceneNode* node = FirstScene(); node;) {
        SceneNode* next = NextScene(node);
        FitViewerCamera(node->scene, scale, x, y, z, id);
        node = next;
    }
    return true;
}

// 0x005A2E50: the object of a node made by its class (unless the skate module leaves the node out: true):
// pedestrians, rail and climbing nodes (switched on), proxim nodes and emitters (switched on), particle systems, level
// objects (with the time of day's level colour, if set), level geometry (the sector switched on - or, with
// 0xEDC063B9, a copy of that sector made under the node's name and placed by angles / scale / pos), vehicles, game
// objects. False for any other class (triggers too).
bool __cdecl CreateFromNode(Scripts::CStruct* node)
{
    uint32_t name = LevelNodes::GetNodeName(node);
    if (SkipsNode(node))
        return true;
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (nodeClass == kClassTrigger) {
        uint32_t triggerName = 0;
        node->ReadHashValue(kName, &triggerName, 1); // (read and not used)
    }
    switch (nodeClass) {
    case kClassPedestrian:
    case kClass061A741E:
        MemSys::PushMemProfile("Pedestrians"); // "Pedestrians"
        GameObjects::SpawnPedestrian(node);
        MemSys::PopMemProfile();
        return true;
    case kClassClimbingNode:
    case kClassRailNode:
        RailManager()->ChangeLive(name, 1, node->HasBareFlag("FLAG_WHOLE_RAIL")); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        GameObjects::ProximityNode_Enable(name, 1);
        return true;
    case kClassParticleObject:
        MemSys::PushMemProfile("Particle Systems"); // "Particle Systems"
        GameObjects::SpawnEffectEntity(node);
        MemSys::PopMemProfile();
        return true;
    case kClassEmitterObject:
        GameObjects::Emitter_SetActive(name, 1);
        return true;
    case kClassLevelObject:
        MemSys::PushMemProfile("LevelObjects"); // "LevelObjects"
        if (g_todManager->m_paramsSet) {
            uint32_t colour;
            g_todManager->GetLevelColor(&colour);
            node->PutInt(kLevelColor, static_cast<int>(colour));
        }
        GameObjects::CreateNodeObject(node);
        MemSys::PopMemProfile();
        return true;
    case kClassVehicle:
        MemSys::PushMemProfile("Vehicles"); // "Vehicles"
        GameObjects::SpawnAutomobile(node);
        MemSys::PopMemProfile();
        return true;
    case kClassGameObject:
        MemSys::PushMemProfile("Game Objects"); // "Game Objects"
        GameObjects::CreateGameObjectFromNode(node);
        MemSys::PopMemProfile();
        return true;
    case kClassLevelGeometry:
        break;
    default:
        return false;
    }
    // level geometry
    uint32_t sectionId = 0;
    uint32_t cloneFrom = 0;
    if (!node->ReadHashValue(kName, &sectionId, 0))
        return true;
    node->ReadHashValue(kCloneFrom, &cloneFrom, 0);
    uint8_t* scene = nullptr;
    uint8_t* sector = nullptr;
    for (SceneNode* at = FirstScene(); at && !sector;) {
        SceneNode* next = NextScene(at);
        scene = at->scene;
        sector = LookUpSector(scene, cloneFrom ? cloneFrom : sectionId);
        at = next;
    }
    if (!sector) {
        // " WARNING: sGetSector(0x%x) returned NULL (%s)\n"
        DebugOut::Printf(" WARNING: sGetSector(0x%x) returned NULL (%s)\n", sectionId, Scripts::FindChecksumNameOrUnknown(sectionId));
        return true;
    }
    if (!cloneFrom) {
        ToggleShown(sector, 0xFFFFFFFF);
        ChangeLive(sector, true);
        EnableCollision(sector, true);
        if (node->HasBareFlag(kOccluder))
            MarkAsOccluder(sectionId, 1);
        return true;
    }
    uint8_t* copy = LookUpSector(scene, sectionId);
    if (!copy) {
        DuplicateSceneSector(scene, sector, scene, 1, 1, sectionId);
        copy = LookUpSector(scene, sectionId);
    }
    *reinterpret_cast<uint32_t*>(sector + 8) |= 0x80;
    *reinterpret_cast<uint32_t*>(copy + 8) &= ~0x80u;
    Scripts::VectorValue angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (node->FetchVec(kAngles, &angles, 0)) {
        Maths::Mat4 orientation;
        orientation.InitFromAngles(*reinterpret_cast<const Maths::Vec4*>(&angles));
        ChangeFacing(copy, &orientation);
    }
    Scripts::VectorValue scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (node->FetchVec(kScale, &scale, 0))
        ChangeScale(copy, &scale);
    Scripts::VectorValue pos = {0.0f, 0.0f, 0.0f, 1.0f};
    node->FetchVec(kPos, &pos, 0);
    ChangePlace(copy, &pos);
    ChangeLive(copy, true);
    EnableCollision(copy, true);
    return true;
}

// 0x005A32A0: the object of the node named `name` made (CreateFromNode).
bool __cdecl CreateFromNodeName(uint32_t name) { return CreateFromNode(LevelNodes::FetchVertex(name, true)); }

// 0x005A32C0: the object of the node named `name` killed by its class: objects (pedestrians, skaters, level objects,
// vehicles, game objects, particle systems, 0x061A741E) with the node's name deleted (unless locked); rail and
// climbing nodes, proxim nodes, emitters switched off; 0xA0E52802 through 0x0048FCA0; level geometry's sector switched
// off (its 0x4E549FE1 effect too). False for no node or any other class. (The original also passes the script's
// object to the walk; its callback doesn't read it.)
bool __cdecl KillFromNodeName(uint32_t name, Scripts::CScript* script)
{
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    if (!node)
        return false;
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    switch (nodeClass) {
    case kClassPedestrian:
    case kClassSkater:
    case kClass061A741E:
    case kClassLevelObject:
    case kClassVehicle:
    case kClassGameObject:
    case kClassParticleObject: {
        struct {
            uint32_t id;
            GameObjects::CEntity* object;
        } data = {name, script->m_entity.Pull()};
        reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager)->ForEach(&DestroyObjectWithId, &data);
        return true;
    }
    case kClassClimbingNode:
    case kClassRailNode:
        RailManager()->ChangeLive(name, 0, node->HasBareFlag("FLAG_WHOLE_RAIL")); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        GameObjects::ProximityNode_Enable(name, 0);
        return true;
    case kClassA0E52802:
        Engine3D::DeleteSceneLight(name);
        return true;
    case kClassEmitterObject:
        GameObjects::Emitter_SetActive(name, 0);
        return true;
    case kClassLevelGeometry: {
        uint32_t sectionId = 0;
        if (!node->ReadHashValue(kName, &sectionId, 0))
            return true;
        for (SceneNode* at = FirstScene(); at;) {
            SceneNode* next = NextScene(at);
            if (uint8_t* sector = LookUpSector(at->scene, sectionId)) {
                ChangeLive(sector, false);
                if (node->HasBareFlag(kOccluder))
                    MarkAsOccluder(sectionId, 0);
                MarkAsOccluder(sectionId, 0);
                return true;
            }
            at = next;
        }
        MarkAsOccluder(sectionId, 0);
        return true;
    }
    default:
        return false;
    }
}

// 0x005A34D0: CreateFromStructure <node structure>: CreateFromNode with the command's parameters.
bool __cdecl CFunc_CreateFromStructure(Scripts::CStruct* params, Scripts::CScript*) { return CreateFromNode(params); }

namespace {

constexpr uint32_t kCreatedAtStart = 0x7C2552B9;  // (name unknown) a node's object made when the level starts
constexpr uint32_t kTerrainType = 0x7E4813A7;     // (name unknown) a level sector's terrain
constexpr uint32_t kDefaultTerrain = 0xE3714DC1;  // (name unknown) ... when none is given
constexpr uint32_t kTrickObject = 0x1645B830;     // (name unknown) a node that is a trick object
constexpr uint32_t kCluster = 0x1A3A966B;         // (name unknown) its cluster (its own name if none)
constexpr uint32_t kAllTrickObjects = 0xB675F8F6; // (name unknown) the global making every node a trick object
constexpr uint32_t kNoGapChecklist = 0xF7ECD937;  // (name unknown) the global leaving the gap checklist alone
constexpr uint32_t kRailRemove1 = 0x7321A8D6;     // (name unknown) removed from rail nodes with angles
constexpr uint32_t kGeometryRemove = 0x2D7E583B;  // (name unknown) removed from level geometry nodes
constexpr uint32_t kParkEditorOff = 0xD443A2BC;   // CParkBuilderMode::kStateOff

// The sector named `name` in any scene (0x0048C510; null if none).
uint8_t* FindSector(uint32_t name) { return static_cast<uint8_t*>(Engine3D::Engine::LookUpSector(name)); }
bool ParkEditorOff() { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor) + 0x40) == kParkEditorOff; }
GameObjects::CTrickSpotHub* TrickObjects() { return GameObjects::g_trickObjectManager; }

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
bool __cdecl CFunc_ParseNodeArray(Scripts::CStruct* params, Scripts::CScript*)
{
    MemSys::PushMemProfile("ParseNodeArray"); // "ParseNodeArray"
    auto* nodes = reinterpret_cast<Scripts::CArray*>(LevelNodes::GetNodeOwnerValue58());
    GameObjects::CGrindRailHub* rails = RailManager();
    if (!params || !params->HasBareFlag("no_cleanup")) { // "no_cleanup"
        GameObjects::NearbyZone_Startup();
        GameObjects::Emitter_Init();
        RailManager()->TidyUp();
        TrickObjects()->RemoveAllClusters();
        Engine3D::DeleteDynamicLights();
        Engine3D::DeleteAllSceneLights();
        MemSys::PushMemProfile("Rail Nodes"); // "Rail Nodes"
        rails->AddFromNodeArray(nodes);
        MemSys::PopMemProfile();
    }
    // (a trick object's name: an asserted parameter, so the original's local - kept from one node to the next -
    // holds the last one read when a node has none)
    uint32_t objectName = 0;
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
        uint32_t nodeClass = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        if (nodeClass == kClassRailNode) {
            node->EraseModule(kAngles);
            node->EraseModule(kRailRemove1);
        } else if (nodeClass == kClassLevelObject) {
            uint32_t name = 0;
            node->ReadHashValue(kName, &name, 0);
            if (uint8_t* sector = FindSector(name)) {
                ChangeLive(sector, false);
                uint32_t terrain = kDefaultTerrain;
                node->ReadHashValue(kTerrainType, &terrain, 0);
                *reinterpret_cast<uint32_t*>(sector + 0xC) = terrain;
            }
        } else if (nodeClass == kClassLevelGeometry) {
            node->EraseModule(kGeometryRemove);
            uint32_t name = 0;
            node->ReadHashValue(kName, &name, 0);
            if (!name) {
                Scripts::DumpToLog(node, 0);
                DebugOut::Printf("FAILED BECAUSE Node %d is LevelGeometry, but has no Name=....\n", i); // "FAILED BECAUSE Node %d is LevelGeometry..."
            }
            if (uint8_t* sector = FindSector(name)) {
                if (!ParkEditorOff()) {
                    if (node->HasBareFlag(kCreatedAtStart))
                        SkipsNode(node); // (asked, the answer unused)
                } else if (node->HasBareFlag(kCreatedAtStart) && !SkipsNode(node)) {
                    ToggleShown(sector, 0xFFFFFFFF);
                    ChangeLive(sector, true);
                    EnableCollision(sector, true);
                } else {
                    ChangeLive(sector, false);
                }
                if (node->HasBareFlag(kOccluder)) {
                    AsSector(sector)->MarkAsOccluder(true);
                    ChangeLive(sector, false);
                    if (!node->HasBareFlag(kCreatedAtStart))
                        MarkAsOccluder(name, 0);
                }
                uint32_t terrain = kDefaultTerrain;
                node->ReadHashValue(kTerrainType, &terrain, 0);
                *reinterpret_cast<uint32_t*>(sector + 0xC) = terrain;
            } else {
                uint32_t cloneFrom = 0;
                node->ReadHashValue(kCloneFrom, &cloneFrom, 0);
                if (!cloneFrom && !GameModules::Skate::IsMultiplayer() && ParkEditorOff()) {
                    // "FAILED BECAUSE: Cannot find level geometry or collision for node %d ..."
                    DebugOut::Printf("FAILED BECAUSE: Cannot find level geometry or collision for node %d \nMaybe there are spaces in or after the name of the Max object (%s)\?\nOr maybe the .QN file does not match the .SCN file checked into Perforce\?\n", i, Scripts::FindChecksumNameOrUnknown(name));
                }
            }
            bool trick = node->HasEntryNamed(kTrickObject);
            if (!GameConfig::g_fromCD && Scripts::FetchIntValue(kAllTrickObjects))
                trick = true;
            if (!SkipsNode(node) && trick) {
                MemSys::PushMemProfile("Trick Object Clusters"); // "Trick Object Clusters"
                node->ReadHashValue(kName, &objectName, true);
                uint32_t cluster = objectName;
                node->ReadHashValue(kCluster, &cluster, false);
                TrickObjects()->AddCluster(cluster);
                TrickObjects()->AddObject(objectName, cluster);
                MemSys::PopMemProfile();
            }
        }
        if (SkipsNode(node))
            continue;
        if (nodeClass == kClassLevelGeometry) {
            if (!ParkEditorOff())
                continue;
        } else {
            if (nodeClass == kClassProximNode) {
                MemSys::PushMemProfile("Proximity Nodes"); // "Proximity Nodes"
                bool active = node->HasBareFlag(kCreatedAtStart); // (asked before the name, as in the original)
                GameObjects::NearbyZone_AddNode(LevelNodes::GetNodeName(node), active);
                MemSys::PopMemProfile();
            }
            if (nodeClass == kClassEmitterObject) {
                MemSys::PushMemProfile("Emitter Objects"); // "Emitter Objects"
                bool active = node->HasBareFlag(kCreatedAtStart); // (asked before the name, as in the original)
                GameObjects::Emitter_AddNode(LevelNodes::GetNodeName(node), active);
                MemSys::PopMemProfile();
            }
        }
        if (node->HasBareFlag(kCreatedAtStart))
            CreateFromNode(LevelNodes::FetchVertex(LevelNodes::GetNodeName(node), true));
    }
    MemSys::PopMemProfile();
    rails->RemoveOverlapsIfNotPark();
    if (!Scripts::FetchIntValue(kNoGapChecklist))
        static_cast<GoalSystem::CGapTally*>(reinterpret_cast<GameModules::Skate*>(Skate())->FetchGapList())->Build();
    for (SceneNode* at = FirstScene(); at;) {
        SceneNode* next = NextScene(at);
        AsScene(at->scene)->UpdateSuperSectorsIfMarked(nullptr);
        at = next;
    }
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
        uint32_t nodeClass = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        if (nodeClass == kClassA0E52802)
            Engine3D::CreateLightFromNode(node);
    }
    MemSys::PopMemProfile();
    return true;
}

// 0x005A39F0: ReLoadNodeArray: the level's node array (named at 0x007CE6D8) loaded again in the debug heap: the old
// asset unloaded and the node manager's array of the file taken out, the file loaded (NodeArrayManLoad and its .qb,
// 0x005A5960) and the node manager's last array put back in.
bool __cdecl CFunc_ReLoadNodeArray(Scripts::CStruct*, Scripts::CScript* script)
{
    MemManager()->EnterHeapScope(reinterpret_cast<MemSys::MemSource*>(MemManager()->m_debugHeap));
    LevelNodes::CNodeManager* nodeManager = LevelNodes::NodeManagerInstance();
    const char* nodeArrayName = g_nodeArrayName;
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    assets->DiscardLoadedItem(assets->ObtainLoadedItem(Checksums::HashFromText(nodeArrayName), 0));
    alignas(4) uint8_t storage[8];
    auto* file = reinterpret_cast<Scripts::CStruct*>(storage);
    file->PrepareAll();
    file->AppendWord(0u, nodeArrayName);
    const char* name = nullptr;
    file->FetchText(0u, &name, 1);
    LevelNodes::NodeManagerInstance()->RemoveNodeArrayOfFile(name);
    CFunc_NodeArrayManLoad(file, script);
    Scripts::LoadQBFile(nodeArrayName, 0);
    LevelNodes::GetLastNodeArray();
    uint32_t last = nodeManager->GetLastNodeArrayName();
    nodeManager->RemoveNodeArray(last);
    nodeManager->AddNodeArray(last, true);
    MemManager()->LeaveHeapScope();
    file->Clear();
    return true;
}

namespace {

constexpr uint32_t kColor = 0x99A9B716;    // (name unknown) SetObjectColor's colour (0xBBGGRR)
constexpr uint32_t kTerrain = 0x9655CFF7;  // (name unknown) the terrain a node's sector gets

// FlagCommandByName's walk data (game_flow_cfuncs.cpp): {id, value, action, result, found}.
struct NodeFlagCommand {
    uint32_t id;
    uint32_t value;
    int action;
    bool result;
    bool found;
};

// The first sector named `name` in any scene (null if none).
uint8_t* FindSceneSector(uint32_t name)
{
    uint8_t* sector = nullptr;
    for (SceneNode* at = FirstScene(); at;) {
        SceneNode* next = NextScene(at);
        sector = LookUpSector(at->scene, name);
        if (sector)
            break;
        at = next;
    }
    return sector;
}

bool IsObjectClass(uint32_t nodeClass)
{
    return nodeClass == kClassParticleObject || nodeClass == kClass061A741E || nodeClass == kClassSkater ||
           nodeClass == kClassPedestrian || nodeClass == kClassVehicle || nodeClass == kClassGameObject;
}

} // namespace

// 0x005A3B80: the node `name`'s objects (pedestrians, skaters, vehicles, game objects, particle systems, 0x061A741E:
// their object flag 4) or sector (level objects and geometry: its viewport 1 bit - viewport 2 with `secondViewport`)
// hidden or shown. True for nodes the skate module skips; false for other classes.
bool __cdecl SetNodeHidden(uint32_t name, bool hide, int secondViewport)
{
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    if (SkipsNode(node))
        return true;
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (IsObjectClass(nodeClass)) {
        NodeFlagCommand command;
        command.result = false;
        command.found = false;
        command.value = 4;
        command.action = hide ? kFlagActionSetObjectFlags : kFlagActionClearObjectFlags;
        command.id = name;
        reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager)->ForEach(&ApplyFlagCommandToObject, &command);
        return true;
    }
    if (nodeClass != kClassLevelObject && nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectionId = 0;
    if (!node->ReadHashValue(kName, &sectionId, 0))
        return true;
    uint8_t* sector = FindSceneSector(sectionId);
    uint32_t mask = AsSector(sector)->FetchShown(); // (not checked for null)
    uint32_t bit = secondViewport == 1 ? 2u : 1u;
    ToggleShown(sector, hide ? mask & ~bit : mask | bit);
    return true;
}

// 0x005A3D10: whether the node `name`'s thing is alive: rail and climbing nodes active; an object with its name
// there (pedestrians, skaters, vehicles, level objects, game objects, particle systems, 0x061A741E); level
// geometry's sector active (true without a name). False for other classes.
bool __cdecl NodeIsAlive(uint32_t name)
{
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (nodeClass == kClassClimbingNode || nodeClass == kClassRailNode)
        return RailManager()->IsOperative(name);
    if (IsObjectClass(nodeClass) || nodeClass == kClassLevelObject) {
        NodeFlagCommand command;
        command.value = 0;
        command.action = 6; // (no action: only whether an object has the id)
        command.found = false;
        command.result = false;
        command.id = name;
        reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager)->ForEach(&ApplyFlagCommandToObject, &command);
        return command.found;
    }
    if (nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectionId = 0;
    if (!node->ReadHashValue(kName, &sectionId, 0))
        return true;
    return AsSector(FindSceneSector(sectionId))->IsOperative(); // (not checked for null)
}

// 0x005A3E80: the node `name`'s colour (0x99A9B716 of `params`, 0xBBGGRR (0x808080), with alpha 0x80): a level
// object's model component, or level geometry's geometry (vtable +0x18). False for other classes.
bool __cdecl SetNodeColor(uint32_t name, Scripts::CStruct* params)
{
    int value = 0x808080;
    params->FetchIntValue(kColor, &value, true);
    uint32_t rgba = (static_cast<uint32_t>(value) & 0xFF) | (static_cast<uint32_t>(value) >> 8 & 0xFF) << 8 |
                    (static_cast<uint32_t>(value) >> 16 & 0xFF) << 16 | 0x80u << 24;
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (nodeClass == kClassLevelObject) {
        uint32_t objectName = 0;
        if (node->ReadHashValue(kName, &objectName, 0)) {
            auto* object = static_cast<GameObjects::CGameEntity*>(
                reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager)->LookUpEntity(objectName));
            // (neither checked for null, as in the original)
            reinterpret_cast<GameObjects::CMeshInstanceModule*>(object->FetchModule(0x286A8D26))->ChangeTint(rgba); // "model"
        }
        return true;
    }
    if (nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectionId = 0;
    if (!node->ReadHashValue(kName, &sectionId, 0))
        return true;
    uint8_t* sector = FindSceneSector(sectionId); // (not checked for null)
    if (auto* geometry = *reinterpret_cast<uint8_t**>(sector + 0x14))
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>((*reinterpret_cast<void***>(geometry))[6])(geometry, rgba);
    return true;
}

// 0x005A3FD0: the node `name`'s sector (level objects and geometry) shattered (its vtable +4 with 1), unless the
// skate module's +0x44 is set (the getter folded at 0x005331E0) or it skips the node (both: true). False for other
// classes.
bool __cdecl ShatterNode(uint32_t name)
{
    if (Skate()[0x44])
        return true;
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    if (SkipsNode(node))
        return true;
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (nodeClass != kClassLevelObject && nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectionId = 0;
    if (!node->ReadHashValue(kName, &sectionId, 0))
        return true;
    uint8_t* sector = FindSceneSector(sectionId); // (not checked for null)
    reinterpret_cast<void(__thiscall*)(void*, int)>((*reinterpret_cast<void***>(sector))[1])(sector, 1); // (0x004A1090: a jump to vtable +4)
    return true;
}

// 0x005A40B0: the node `name`'s sector (level geometry only, else false) given the terrain 0x9655CFF7 of `params`.
bool __cdecl SetNodeTerrain(uint32_t name, Scripts::CStruct* params)
{
    uint32_t terrain = 0; // (unset in the original when missing - an asserted parameter)
    params->ReadHashValue(kTerrain, &terrain, true);
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    uint32_t nodeClass = 0;
    node->ReadHashValue(kClass, &nodeClass, 0);
    if (nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectionId = 0;
    if (!node->ReadHashValue(kName, &sectionId, 0))
        return true;
    *reinterpret_cast<uint32_t*>(FindSceneSector(sectionId) + 0xC) = terrain; // (not checked for null)
    return true;
}

// 0x005A4DA0: NodeCommand `action` on the node `name` (false in a network game for nodes the skate module skips):
// 0 create, 1 kill, 2 show, 3 hide, 4 show and shatter, 5 alive?, 6 colour, 7 terrain; other actions false. Shown /
// hidden for the second viewport when it's the second skater's script in a two-viewport game.
bool __cdecl NodeCommand(uint32_t name, int action, Scripts::CScript* script, Scripts::CStruct* params)
{
    OnlinePlay::Hub* gameNet = OnlinePlay::g_gameNetManager;
    if (gameNet->IsNetworkGame() && SkipsNode(LevelNodes::FetchVertex(name, true)))
        return false;
    int secondViewport = 0;
    if (script && script->m_entity.Pull() && !gameNet->InNetGame() && GameModules::g_viewportCount == 2 &&
        script->m_entity.Pull() == reinterpret_cast<GameObjects::CEntity*>(
                                       reinterpret_cast<GameModules::Skate*>(Skate())->GetRiderAt(1)))
        secondViewport = 1;
    switch (action) {
    case 0:
        return CreateFromNodeName(name);
    case 1:
        return KillFromNodeName(name, script);
    case 2:
        return SetNodeHidden(name, false, secondViewport);
    case 3:
        return SetNodeHidden(name, true, secondViewport);
    case 4:
        SetNodeHidden(name, false, secondViewport);
        return ShatterNode(name);
    case 5:
        return NodeIsAlive(name);
    case 6:
        return SetNodeColor(name, params);
    case 7:
        return SetNodeTerrain(name, params);
    default:
        return false;
    }
}

// 0x005A4EF0: NodeCommand on name= (its answer) or every node of prefix= (true; with action 5, stopping at the first
// alive; with 0x80804991 the name is exact, not a prefix). Nothing (true) with the flags 0x2E7D5EE7 / 0xBCEB479A;
// false without either name.
bool __cdecl NodeCommandByName(Scripts::CStruct* params, Scripts::CScript* script, int action)
{
    if (params->HasBareFlag(0x2E7D5EE7u) || params->HasBareFlag(0xBCEB479Au))
        return true;
    uint32_t prefix = 0;
    if (params->HashFromNameOrText(0x6C4E7971u, &prefix, 0)) { // "prefix"
        uint32_t count = 0; // (a 16-bit count in a zeroed 32-bit slot)
        bool isPrefix = !params->HasBareFlag(0x80804991u);
        (void)isPrefix; // (passed to 0x0045BF90, which ignores it)
        uint32_t* nodes = LevelNodes::CollectNodesByPrefix(prefix, reinterpret_cast<uint16_t*>(&count));
        for (int i = 0; i < static_cast<uint16_t>(count); ++i) {
            if (NodeCommand(nodes[i], action, script, params) && action == 5)
                break;
        }
        return true;
    }
    uint32_t name = 0;
    if (!params->HashFromNameOrText(kName, &name, 0))
        return false;
    return NodeCommand(name, action, script, params);
}

// 0x005A4FF0: Create name= / prefix=: the dead objects flushed first (the CFunction 0x005B0A40), then made.
bool __cdecl CFunc_Create(Scripts::CStruct* params, Scripts::CScript* script)
{
    CFunc_FlushDeadObjects(nullptr, nullptr);
    return NodeCommandByName(params, script, 0);
}

// 0x005A5010 / 0x005A5030 / 0x005A5050 / 0x005A5070: SetObjectColor / Kill / Visible / Invisible name= or prefix=.
bool __cdecl CFunc_SetObjectColor(Scripts::CStruct* params, Scripts::CScript* script) { return NodeCommandByName(params, script, 6); }
bool __cdecl CFunc_Kill(Scripts::CStruct* params, Scripts::CScript* script) { return NodeCommandByName(params, script, 1); }
bool __cdecl CFunc_Visible(Scripts::CStruct* params, Scripts::CScript* script) { return NodeCommandByName(params, script, 2); }
bool __cdecl CFunc_Invisible(Scripts::CStruct* params, Scripts::CScript* script) { return NodeCommandByName(params, script, 3); }

// 0x005A5090: Shatter name= / prefix= [0x9F98989E scale= (1): the calling object's velocity times scale, else
// 0x6187906D / 0x1680A0FB / 0x8F89F141 asserted] and the shatter's settings (0x286BC297 - at least 2000, 5000 in a
// multiplayer game -, 0xBD309DD9, 0x834A5F87, 0xE632C101, 0x7DC30BA6, 0xF01BFA01, 0xB3DE2CAC, 0xAF1A25F2 (0x54628ED7),
// 0x2E1C4A64 (1), 0x210EA9E1 (a vector), 0xC48391A5, 0x33DF5E27): the settings given (0x00491110), then the nodes
// shown and shattered (NodeCommandByName 4, its answer).
bool __cdecl CFunc_Shatter(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0; // (read and not used)
    float scale = 1.0f;
    params->ReadHashValue(kName, &name, true);
    Maths::Vec4 velocity;
    if (params->HasBareFlag(0x9F98989Eu)) {
        params->FetchFloat(0x13B9DA7Bu, &scale, 0); // "scale"
        const Maths::Vec4& v = static_cast<GameObjects::CGameEntity*>(script->m_entity.Pull())->m_vel;
        velocity.x = static_cast<float>(static_cast<double>(scale) * v.x);
        velocity.y = static_cast<float>(static_cast<double>(scale) * v.y);
        velocity.z = static_cast<float>(static_cast<double>(scale) * v.z);
        velocity.w = static_cast<float>(static_cast<double>(scale) * v.w);
    } else {
        params->FetchFloat(0x6187906Du, &velocity.x, true);
        params->FetchFloat(0x1680A0FBu, &velocity.y, true);
        params->FetchFloat(0x8F89F141u, &velocity.z, true);
        velocity.w = 0.0f;
    }
    float area = 0.0f;
    params->FetchFloat(0x286BC297u, &area, 0);
    float valueBD30 = 0.0f;
    params->FetchFloat(0xBD309DD9u, &valueBD30, 0);
    float value834A = 0.0f;
    params->FetchFloat(0x834A5F87u, &value834A, 0);
    float valueE632 = 0.0f;
    params->FetchFloat(0xE632C101u, &valueE632, 0);
    float value7DC3 = 0.0f;
    params->FetchFloat(0x7DC30BA6u, &value7DC3, 0);
    float valueF01B = 0.0f;
    params->FetchFloat(0xF01BFA01u, &valueF01B, 0);
    uint32_t checksumB3DE = 0;
    params->ReadHashValue(0xB3DE2CACu, &checksumB3DE, 0);
    uint32_t checksumAF1A = 0x54628ED7;
    params->ReadHashValue(0xAF1A25F2u, &checksumAF1A, 0);
    float value2E1C = 1.0f;
    params->FetchFloat(0x2E1C4A64u, &value2E1C, 0);
    Scripts::VectorValue vector = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0x210EA9E1u, &vector, 0);
    float valueC483 = 0.0f;
    params->FetchFloat(0xC48391A5u, &valueC483, 0);
    int value33DF = 0;
    params->FetchIntValue(0x33DF5E27u, &value33DF, 0);
    float minimumArea = 2000.0f;
    if (GameModules::Skate::IsMultiplayer())
        minimumArea = g_nodeObjectMinimumArea; // (5000)
    if (area < minimumArea)
        area = minimumArea;
    Engine3D::Engine::SetShatterParams(&velocity, reinterpret_cast<const Maths::Vec4*>(&vector), valueC483, area, valueBD30,
                                 value834A, valueE632, value7DC3, valueF01B, value2E1C, checksumB3DE, checksumAF1A,
                                 value33DF);
    return NodeCommandByName(params, script, 4);
}

} // namespace ScriptCommands
